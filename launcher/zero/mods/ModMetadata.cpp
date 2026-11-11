// SPDX-License-Identifier: GPL-3.0-only
#include "ModMetadata.h"

#include <archive.h>
#include <archive_entry.h>
#include <toml++/toml.h>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <functional>
#include <memory>

#include "archive/ArchiveReader.h"

namespace Zero {
namespace Mods {

namespace {

constexpr qint64 kMaxMetadataBytes = 8 * 1024 * 1024;
constexpr qint64 kMaxManifestBytes = 1024 * 1024;
constexpr qint64 kMaxNestedJarBytes = 128 * 1024 * 1024;

const QString kFabricJson = QStringLiteral("fabric.mod.json");
const QString kQuiltJson = QStringLiteral("quilt.mod.json");
const QString kNeoForgeToml = QStringLiteral("META-INF/neoforge.mods.toml");
const QString kForgeToml = QStringLiteral("META-INF/mods.toml");
const QString kMcmodInfo = QStringLiteral("mcmod.info");
const QString kManifest = QStringLiteral("META-INF/MANIFEST.MF");
const QString kJarJar = QStringLiteral("META-INF/jarjar/metadata.json");

QString tr(const char* s)
{
    return QCoreApplication::translate("ModHealth", s);
}

// ------------------------------------------------------------------------------------------------ archive access

struct Source {
    QString path;
    const QByteArray* bytes = nullptr;
};

/// Reads the entries `limitFor` accepts (it returns the byte cap for a name, <= 0 to skip it) in ONE pass over the archive.
/// Entries over their cap are dropped. Returns an error text when the archive cannot be opened.
QString readEntries(const Source& src, const std::function<qint64(const QString&)>& limitFor, QHash<QString, QByteArray>& out)
{
    if (!src.bytes) {
        MMCZip::ArchiveReader reader(src.path);
        const std::function<Result<>(MMCZip::ArchiveReader::File*)> visit = [&](MMCZip::ArchiveReader::File* f) -> Result<> {
            if (!f->isFile())
                return f->skip();
            QString name = f->filename();
            if (name.startsWith(QLatin1Char('/')))
                name.remove(0, 1);
            const qint64 cap = limitFor(name);
            if (cap <= 0)
                return f->skip();
            QByteArray data;
            bool tooBig = false;
            const auto r = f->readBlocks([&](const char* block, qint64 len) {
                if (data.size() + len > cap) {
                    tooBig = true;
                    return false;
                }
                data.append(block, len);
                return true;
            });
            if (tooBig)
                return f->skip();
            if (!r)
                return std::unexpected{ r.error() };
            if (!out.contains(name))
                out.insert(name, data);
            return {};
        };
        const auto r = reader.parse(visit);
        return r ? QString() : r.error();
    }

    std::unique_ptr<archive, int (*)(archive*)> a(archive_read_new(), archive_read_free);
    archive_read_support_format_zip(a.get());
    if (archive_read_open_memory(a.get(), src.bytes->constData(), size_t(src.bytes->size())) != ARCHIVE_OK)
        return QString::fromUtf8(archive_error_string(a.get()));
    archive_entry* entry = nullptr;
    while (archive_read_next_header(a.get(), &entry) == ARCHIVE_OK) {
        if ((archive_entry_filetype(entry) & AE_IFMT) != AE_IFREG)
            continue;
        QString name = QString::fromUtf8(archive_entry_pathname_utf8(entry));
        if (name.isEmpty())
            name = QString::fromLocal8Bit(archive_entry_pathname(entry));
        if (name.startsWith(QLatin1Char('/')))
            name.remove(0, 1);
        const qint64 cap = limitFor(name);
        if (cap <= 0 || out.contains(name) || (archive_entry_size_is_set(entry) && archive_entry_size(entry) > cap)) {
            archive_read_data_skip(a.get());
            continue;
        }
        QByteArray data;
        const void* block = nullptr;
        size_t len = 0;
        la_int64_t offset = 0;
        int status = 0;
        bool tooBig = false;
        while ((status = archive_read_data_block(a.get(), &block, &len, &offset)) == ARCHIVE_OK) {
            if (data.size() + qint64(len) > cap) {
                tooBig = true;
                break;
            }
            data.append(static_cast<const char*>(block), qsizetype(len));
        }
        if (tooBig) {
            archive_read_data_skip(a.get());
            continue;
        }
        if (status != ARCHIVE_EOF)
            continue;  // damaged entry: treat as absent
        out.insert(name, data);
    }
    archive_read_close(a.get());
    return {};
}

// ------------------------------------------------------------------------------------------------ JSON helpers

QByteArray stripBom(const QByteArray& in)
{
    return in.startsWith("\xEF\xBB\xBF") ? in.mid(3) : in;
}

bool parseJsonObject(const QByteArray& contents, QJsonObject& out, QString& error)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(stripBom(contents), &pe);
    if (pe.error != QJsonParseError::NoError) {
        error = tr("The metadata file is not valid JSON (%1).").arg(pe.errorString());
        return false;
    }
    if (!doc.isObject()) {
        error = tr("The metadata file does not contain a JSON object.");
        return false;
    }
    out = doc.object();
    return true;
}

/// Version requirement from a JSON value: string, array of strings (alternatives), or Quilt `{any: [...]}` / `{all: [...]}`.
/// Anything else becomes "?" (never matches definitively).
QString predicateFromJson(const QJsonValue& v)
{
    if (v.isUndefined() || v.isNull())
        return {};
    if (v.isString())
        return v.toString().trimmed();
    auto joinStrings = [](const QJsonArray& arr, const QString& sep, QString& result) {
        QStringList parts;
        for (const QJsonValue& e : arr) {
            if (!e.isString())
                return false;
            const QString s = e.toString().trimmed();
            if (s.isEmpty())
                continue;
            if (sep == QLatin1String(" ") && s.contains(QLatin1String("||")))
                return false;
            parts.append(s);
        }
        result = parts.join(sep);
        return true;
    };
    QString result;
    if (v.isArray())
        return joinStrings(v.toArray(), QStringLiteral(" || "), result) ? result : QStringLiteral("?");
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        if (o.size() == 1 && o.value(QStringLiteral("any")).isArray())
            return joinStrings(o.value(QStringLiteral("any")).toArray(), QStringLiteral(" || "), result) ? result : QStringLiteral("?");
        if (o.size() == 1 && o.value(QStringLiteral("all")).isArray())
            return joinStrings(o.value(QStringLiteral("all")).toArray(), QStringLiteral(" "), result) ? result : QStringLiteral("?");
    }
    return QStringLiteral("?");
}

void addFabricDeps(const QJsonObject& obj, const QString& key, QList<Dependency>& into, bool mandatory)
{
    const QJsonObject deps = obj.value(key).toObject();
    for (auto it = deps.begin(); it != deps.end(); ++it) {
        Dependency d;
        d.id = it.key();
        d.versionPredicate = predicateFromJson(it.value());
        d.mandatory = mandatory;
        if (!d.id.isEmpty())
            into.append(d);
    }
}

QString fileNameOf(const QString& path)
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return slash >= 0 ? path.mid(slash + 1) : path;
}

// ------------------------------------------------------------------------------------------------ TOML helpers

bool parseToml(const QByteArray& contents, toml::table& out)
{
    const std::string text = stripBom(contents).toStdString();
#if TOML_EXCEPTIONS
    try {
        out = toml::parse(text);
        return true;
    } catch (const std::exception&) {
        return false;
    }
#else
    toml::parse_result result = toml::parse(text);
    if (!result)
        return false;
    out = std::move(result).table();
    return true;
#endif
}

QString tomlString(const toml::table& t, const char* key)
{
    if (const auto* node = t.get(key); node && node->is_string())
        return QString::fromStdString(node->as_string()->get());
    return {};
}

}  // namespace

// ------------------------------------------------------------------------------------------------ public helpers

QString loaderFormatId(LoaderFormat format)
{
    switch (format) {
        case LoaderFormat::Fabric:
            return QStringLiteral("fabric");
        case LoaderFormat::Quilt:
            return QStringLiteral("quilt");
        case LoaderFormat::Forge:
            return QStringLiteral("forge");
        case LoaderFormat::NeoForge:
            return QStringLiteral("neoforge");
        case LoaderFormat::LegacyForge:
            return QStringLiteral("legacy-forge");
        case LoaderFormat::Unknown:
            break;
    }
    return QStringLiteral("unknown");
}

QList<LoaderFormat> ModMetadata::formats() const
{
    QList<LoaderFormat> out;
    for (LoaderFormat f : { LoaderFormat::Fabric, LoaderFormat::Quilt, LoaderFormat::NeoForge, LoaderFormat::Forge, LoaderFormat::LegacyForge }) {
        if (hasFormat(f))
            out.append(f);
    }
    return out;
}

bool ModMetadata::hasFormat(LoaderFormat f) const
{
    for (const ModEntry& e : mods)
        if (e.format == f)
            return true;
    return false;
}

LoaderFormat ModMetadata::loaderFormat() const
{
    const QList<LoaderFormat> f = formats();
    return f.isEmpty() ? LoaderFormat::Unknown : f.first();
}

// ------------------------------------------------------------------------------------------------ format parsers

ParsedFile parseFabricModJson(const QByteArray& contents)
{
    ParsedFile out;
    QJsonObject root;
    if (!parseJsonObject(contents, root, out.error))
        return out;
    ModEntry e;
    e.format = LoaderFormat::Fabric;
    e.id = root.value(QStringLiteral("id")).toString().trimmed();
    if (e.id.isEmpty()) {
        out.error = tr("fabric.mod.json has no mod id.");
        return out;
    }
    e.version = root.value(QStringLiteral("version")).toString().trimmed();
    e.name = root.value(QStringLiteral("name")).toString().trimmed();
    for (const QJsonValue& p : root.value(QStringLiteral("provides")).toArray()) {
        if (p.isString() && !p.toString().isEmpty())
            e.provides.append({ p.toString(), QString() });
    }
    addFabricDeps(root, QStringLiteral("depends"), e.depends, true);
    addFabricDeps(root, QStringLiteral("recommends"), e.recommends, false);
    addFabricDeps(root, QStringLiteral("breaks"), e.breaks, true);
    addFabricDeps(root, QStringLiteral("conflicts"), e.conflicts, true);
    out.mods.append(e);
    for (const QJsonValue& j : root.value(QStringLiteral("jars")).toArray()) {
        const QString file = j.toObject().value(QStringLiteral("file")).toString();
        if (!file.isEmpty())
            out.nestedJars.append(file);
    }
    return out;
}

namespace {

/// Quilt `depends` / `breaks` entry: "id" or {id, versions, optional, unless}.
void addQuiltDeps(const QJsonArray& arr, QList<Dependency>& required, QList<Dependency>* optional)
{
    for (const QJsonValue& v : arr) {
        Dependency d;
        bool isOptional = false;
        if (v.isString()) {
            d.id = v.toString();
        } else if (v.isObject()) {
            const QJsonObject o = v.toObject();
            d.id = o.value(QStringLiteral("id")).toString();
            d.versionPredicate = predicateFromJson(o.value(QStringLiteral("versions")));
            isOptional = o.value(QStringLiteral("optional")).toBool(false);
            const QJsonValue unless = o.value(QStringLiteral("unless"));
            d.unless = unless.isObject() ? unless.toObject().value(QStringLiteral("id")).toString() : unless.toString();
        }
        if (d.id.isEmpty())
            continue;
        if (isOptional && optional) {
            d.mandatory = false;
            optional->append(d);
        } else {
            required.append(d);
        }
    }
}

}  // namespace

ParsedFile parseQuiltModJson(const QByteArray& contents)
{
    ParsedFile out;
    QJsonObject root;
    if (!parseJsonObject(contents, root, out.error))
        return out;
    const QJsonObject loader = root.value(QStringLiteral("quilt_loader")).toObject();
    ModEntry e;
    e.format = LoaderFormat::Quilt;
    e.id = loader.value(QStringLiteral("id")).toString().trimmed();
    if (e.id.isEmpty()) {
        out.error = tr("quilt.mod.json has no mod id.");
        return out;
    }
    e.version = loader.value(QStringLiteral("version")).toString().trimmed();
    e.name = loader.value(QStringLiteral("metadata")).toObject().value(QStringLiteral("name")).toString().trimmed();
    for (const QJsonValue& p : loader.value(QStringLiteral("provides")).toArray()) {
        if (p.isString() && !p.toString().isEmpty()) {
            e.provides.append({ p.toString(), QString() });
        } else if (p.isObject()) {
            const QJsonObject po = p.toObject();
            const QString id = po.value(QStringLiteral("id")).toString();
            if (!id.isEmpty())
                e.provides.append({ id, po.value(QStringLiteral("version")).toString() });
        }
    }
    addQuiltDeps(loader.value(QStringLiteral("depends")).toArray(), e.depends, &e.recommends);
    QList<Dependency> unusedOptional;
    addQuiltDeps(loader.value(QStringLiteral("breaks")).toArray(), e.breaks, &unusedOptional);
    e.breaks.append(unusedOptional);  // an "optional" break does not exist in practice; keep it rather than lose it
    out.mods.append(e);
    for (const QJsonValue& j : loader.value(QStringLiteral("jars")).toArray()) {
        const QString file = j.isString() ? j.toString() : j.toObject().value(QStringLiteral("file")).toString();
        if (!file.isEmpty())
            out.nestedJars.append(file);
    }
    return out;
}

ParsedFile parseModsToml(const QByteArray& contents, LoaderFormat format, const QString& manifestImplementationVersion)
{
    ParsedFile out;
    toml::table root;
    if (!parseToml(contents, root)) {
        out.error = tr("The metadata file is not valid TOML.");
        return out;
    }
    const toml::array* modsArray = root.get_as<toml::array>("mods");
    if (!modsArray || modsArray->empty()) {
        out.error = tr("The metadata file has no [[mods]] entry.");
        return out;
    }
    const bool neo = format == LoaderFormat::NeoForge;
    for (const toml::node& node : *modsArray) {
        const toml::table* t = node.as_table();
        if (!t)
            continue;
        ModEntry e;
        e.format = format;
        e.id = tomlString(*t, "modId").trimmed();
        if (e.id.isEmpty())
            continue;
        e.name = tomlString(*t, "displayName").trimmed();
        QString version = tomlString(*t, "version").trimmed();
        if (version == QLatin1String("${file.jarVersion}"))
            version = manifestImplementationVersion.trimmed();
        if (version.contains(QLatin1String("${")))
            version.clear();  // an unresolved build placeholder says nothing about the version
        e.version = version;
        out.mods.append(e);
    }
    if (out.mods.isEmpty()) {
        out.error = tr("The metadata file has no usable [[mods]] entry.");
        return out;
    }

    auto addDeps = [&](const toml::array& deps, ModEntry& target) {
        for (const toml::node& n : deps) {
            const toml::table* d = n.as_table();
            if (!d)
                continue;
            Dependency dep;
            dep.id = tomlString(*d, "modId").trimmed();
            if (dep.id.isEmpty() || dep.id == target.id)
                continue;
            dep.versionPredicate = tomlString(*d, "versionRange").trimmed();
            dep.side = tomlString(*d, "side").trimmed().toUpper();
            const QString type = tomlString(*d, "type").trimmed().toLower();
            const toml::node* mandatoryNode = d->get("mandatory");
            const bool hasMandatory = mandatoryNode && mandatoryNode->is_boolean();
            if (!type.isEmpty()) {
                if (type == QLatin1String("required")) {
                    target.depends.append(dep);
                } else if (type == QLatin1String("optional")) {
                    dep.mandatory = false;
                    target.recommends.append(dep);
                } else if (type == QLatin1String("incompatible")) {
                    target.breaks.append(dep);
                } else if (type == QLatin1String("discouraged")) {
                    target.conflicts.append(dep);
                }
            } else if (hasMandatory) {
                if (mandatoryNode->as_boolean()->get()) {
                    target.depends.append(dep);
                } else {
                    dep.mandatory = false;
                    target.recommends.append(dep);
                }
            } else if (neo) {
                target.depends.append(dep);  // NeoForge: `type` defaults to "required"
            } else {
                dep.mandatory = false;  // Forge: `mandatory` is required; when absent assume nothing
                target.recommends.append(dep);
            }
        }
    };

    if (const toml::node* depsNode = root.get("dependencies")) {
        if (const toml::table* byMod = depsNode->as_table()) {
            for (const auto& [key, value] : *byMod) {
                const toml::array* arr = value.as_array();
                if (!arr)
                    continue;
                const std::string_view keyText = key.str();
                const QString owner = QString::fromUtf8(keyText.data(), qsizetype(keyText.size()));
                ModEntry* target = &out.mods.first();
                for (ModEntry& m : out.mods) {
                    if (m.id == owner) {
                        target = &m;
                        break;
                    }
                }
                addDeps(*arr, *target);
            }
        } else if (const toml::array* arr = depsNode->as_array()) {
            addDeps(*arr, out.mods.first());
        }
    }
    return out;
}

ParsedFile parseMcmodInfo(const QByteArray& contents)
{
    ParsedFile out;
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(stripBom(contents), &pe);
    if (pe.error != QJsonParseError::NoError || (!doc.isArray() && !doc.isObject())) {
        out.error = tr("mcmod.info is not valid JSON.");
        return out;
    }
    QJsonArray list;
    if (doc.isArray())
        list = doc.array();
    else
        list = doc.object().value(QStringLiteral("modList")).toArray();
    for (const QJsonValue& v : list) {
        const QJsonObject o = v.toObject();
        ModEntry e;
        e.format = LoaderFormat::LegacyForge;
        e.id = o.value(QStringLiteral("modid")).toString().trimmed();
        if (e.id.isEmpty())
            continue;
        e.name = o.value(QStringLiteral("name")).toString().trimmed();
        e.version = o.value(QStringLiteral("version")).toString().trimmed();
        if (e.version.contains(QLatin1String("${")))
            e.version.clear();
        out.mods.append(e);
    }
    return out;
}

ParsedFile parseJarJarMetadata(const QByteArray& contents)
{
    ParsedFile out;
    QJsonObject root;
    if (!parseJsonObject(contents, root, out.error))
        return out;
    for (const QJsonValue& j : root.value(QStringLiteral("jars")).toArray()) {
        const QString path = j.toObject().value(QStringLiteral("path")).toString();
        if (!path.isEmpty())
            out.nestedJars.append(path);
    }
    return out;
}

QString parseManifestImplementationVersion(const QByteArray& manifest)
{
    const QString text = QString::fromUtf8(manifest);
    QStringList lines;
    for (const QString& raw : text.split(QLatin1Char('\n'))) {
        QString line = raw;
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        if (line.isEmpty()) {
            if (!lines.isEmpty())
                break;  // end of the main section
            continue;
        }
        if (line.startsWith(QLatin1Char(' ')) && !lines.isEmpty())
            lines.last().append(line.mid(1));  // continuation of the previous header
        else
            lines.append(line);
    }
    for (const QString& line : lines) {
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon > 0 && line.left(colon).compare(QLatin1String("Implementation-Version"), Qt::CaseInsensitive) == 0)
            return line.mid(colon + 1).trimmed();
    }
    return {};
}

// ------------------------------------------------------------------------------------------------ jar reading

namespace {

struct Budget {
    int jarsLeft = 128;
    qint64 bytesLeft = 256LL * 1024 * 1024;
};

ModMetadata parseSource(const Source& src, const QString& fileName, const QString& path, int depth, Budget& budget)
{
    ModMetadata md;
    md.fileName = fileName;
    md.path = path;

    QHash<QString, QByteArray> files;
    const QSet<QString> fixed = { kFabricJson, kQuiltJson, kNeoForgeToml, kForgeToml, kMcmodInfo, kManifest, kJarJar };
    const QString openError = readEntries(
        src,
        [&](const QString& name) -> qint64 {
            if (!fixed.contains(name))
                return 0;
            return name == kManifest ? kMaxManifestBytes : kMaxMetadataBytes;
        },
        files);
    if (!openError.isEmpty()) {
        md.parseError = tr("ZERO could not open this file as an archive.");
        return md;
    }

    const QString implVersion = files.contains(kManifest) ? parseManifestImplementationVersion(files.value(kManifest)) : QString();
    QStringList nestedPaths;
    QStringList declaredByModLoader;  // nested jars that must be mods (Fabric / Quilt): no metadata inside them is a problem
    QStringList errors;

    auto take = [&](const QString& name, LoaderFormat format, const ParsedFile& parsed, bool mustBeMods) {
        if (!parsed.error.isEmpty()) {
            md.unparsedFormats.append(format);
            errors.append(parsed.error);
        }
        md.mods.append(parsed.mods);
        for (const QString& p : parsed.nestedJars) {
            if (!nestedPaths.contains(p))
                nestedPaths.append(p);
            if (mustBeMods && !declaredByModLoader.contains(p))
                declaredByModLoader.append(p);
        }
        Q_UNUSED(name)
    };
    if (files.contains(kFabricJson))
        take(kFabricJson, LoaderFormat::Fabric, parseFabricModJson(files.value(kFabricJson)), true);
    if (files.contains(kQuiltJson))
        take(kQuiltJson, LoaderFormat::Quilt, parseQuiltModJson(files.value(kQuiltJson)), true);
    if (files.contains(kNeoForgeToml))
        take(kNeoForgeToml, LoaderFormat::NeoForge, parseModsToml(files.value(kNeoForgeToml), LoaderFormat::NeoForge, implVersion), false);
    if (files.contains(kForgeToml)) {
        ParsedFile parsed = parseModsToml(files.value(kForgeToml), LoaderFormat::Forge, implVersion);
        // NeoForge 1.20.2 - 1.20.4 mods still used META-INF/mods.toml; a mandatory dependency on "neoforge" marks them as NeoForge mods.
        for (ModEntry& e : parsed.mods) {
            for (const Dependency& d : e.depends) {
                if (d.id.compare(QLatin1String("neoforge"), Qt::CaseInsensitive) == 0) {
                    e.format = LoaderFormat::NeoForge;
                    break;
                }
            }
        }
        take(kForgeToml, LoaderFormat::Forge, parsed, false);
    }
    if (files.contains(kMcmodInfo))
        take(kMcmodInfo, LoaderFormat::LegacyForge, parseMcmodInfo(files.value(kMcmodInfo)), false);
    if (files.contains(kJarJar))
        take(kJarJar, LoaderFormat::Unknown, parseJarJarMetadata(files.value(kJarJar)), false);
    // the jarjar file is not a mod format: do not list it as failed Unknown format
    md.unparsedFormats.removeAll(LoaderFormat::Unknown);
    if (!errors.isEmpty())
        md.parseError = errors.join(QLatin1Char(' '));

    // ---- nested jars: read their bytes in one more pass and parse them from memory
    if (!nestedPaths.isEmpty() && depth < kMaxNestingDepth) {
        QSet<QString> wanted;
        for (const QString& p : nestedPaths) {
            QString n = QDir::cleanPath(p);
            if (n.startsWith(QLatin1Char('/')))
                n.remove(0, 1);
            wanted.insert(n);
        }
        QHash<QString, QByteArray> blobs;
        readEntries(
            src, [&](const QString& name) -> qint64 { return wanted.contains(name) ? kMaxNestedJarBytes : 0; }, blobs);
        for (const QString& p : nestedPaths) {
            QString n = QDir::cleanPath(p);
            if (n.startsWith(QLatin1Char('/')))
                n.remove(0, 1);
            ModMetadata child;
            child.fileName = fileNameOf(n);
            child.path = n;
            const bool mustBeMod = declaredByModLoader.contains(p);
            if (!blobs.contains(n)) {
                if (mustBeMod)
                    child.parseError = tr("A bundled jar listed in the metadata could not be read.");
            } else if (budget.jarsLeft <= 0 || budget.bytesLeft < blobs.value(n).size()) {
                child.parseError = tr("This jar bundles too many other jars to read them all.");
            } else {
                --budget.jarsLeft;
                budget.bytesLeft -= blobs.value(n).size();
                const QByteArray& bytes = blobs[n];
                Source nestedSource;
                nestedSource.bytes = &bytes;
                child = parseSource(nestedSource, fileNameOf(n), n, depth + 1, budget);
                if (mustBeMod && child.mods.isEmpty() && child.parseError.isEmpty())
                    child.parseError = tr("A bundled jar has no readable mod metadata.");
            }
            md.nested.push_back(std::move(child));
        }
    }
    return md;
}

}  // namespace

ModMetadata parseJar(const QString& path)
{
    Budget budget;
    Source src;
    src.path = path;
    return parseSource(src, QFileInfo(path).fileName(), path, 0, budget);
}

ModMetadata parseJarBytes(const QByteArray& bytes, const QString& fileName)
{
    Budget budget;
    Source src;
    src.bytes = &bytes;
    return parseSource(src, fileName, fileName, 0, budget);
}

}  // namespace Mods
}  // namespace Zero
