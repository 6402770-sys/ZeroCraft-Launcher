// SPDX-License-Identifier: GPL-3.0-only
#include "ModHealth.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <algorithm>
#include <utility>

#include "zero/mods/Versions.h"

namespace Zero {
namespace Mods {

namespace {

QString tr(const char* s)
{
    return QCoreApplication::translate("ModHealth", s);
}

QString loaderName(InstanceLoader loader)
{
    switch (loader) {
        case InstanceLoader::Fabric:
            return QStringLiteral("Fabric");
        case InstanceLoader::Quilt:
            return QStringLiteral("Quilt");
        case InstanceLoader::Forge:
            return QStringLiteral("Forge");
        case InstanceLoader::NeoForge:
            return QStringLiteral("NeoForge");
        case InstanceLoader::None:
            break;
    }
    return tr("no mod loader");
}

QString formatName(LoaderFormat format)
{
    switch (format) {
        case LoaderFormat::Fabric:
            return QStringLiteral("Fabric");
        case LoaderFormat::Quilt:
            return QStringLiteral("Quilt");
        case LoaderFormat::Forge:
            return QStringLiteral("Forge");
        case LoaderFormat::NeoForge:
            return QStringLiteral("NeoForge");
        case LoaderFormat::LegacyForge:
            return tr("an older version of Forge");
        case LoaderFormat::Unknown:
            break;
    }
    return tr("another mod loader");
}

bool isMaven(LoaderFormat f)
{
    return f == LoaderFormat::Forge || f == LoaderFormat::NeoForge || f == LoaderFormat::LegacyForge;
}

QList<LoaderFormat> preferredFormats(InstanceLoader loader)
{
    switch (loader) {
        case InstanceLoader::Fabric:
            return { LoaderFormat::Fabric };
        case InstanceLoader::Quilt:
            return { LoaderFormat::Quilt, LoaderFormat::Fabric };
        case InstanceLoader::Forge:
            return { LoaderFormat::Forge, LoaderFormat::LegacyForge };
        case InstanceLoader::NeoForge:
            return { LoaderFormat::NeoForge };
        case InstanceLoader::None:
            break;
    }
    return { LoaderFormat::Fabric, LoaderFormat::Quilt, LoaderFormat::NeoForge, LoaderFormat::Forge, LoaderFormat::LegacyForge };
}

QString joinNames(const QStringList& names)
{
    if (names.size() <= 1)
        return names.value(0);
    if (names.size() == 2)
        return tr("%1 and %2").arg(names[0], names[1]);
    if (names.size() == 3)
        return tr("%1, %2 and %3").arg(names[0], names[1], names[2]);
    return tr("%1, %2 and %3 more").arg(names[0], names[1]).arg(names.size() - 2);
}

QString versionText(const QString& v)
{
    return v.isEmpty() ? tr("(version unknown)") : v;
}

QString stripDisabledSuffix(QString name)
{
    for (const char* suffix : { ".disabled", ".jar" }) {
        if (name.endsWith(QLatin1String(suffix), Qt::CaseInsensitive))
            name.chop(int(qstrlen(suffix)));
    }
    return name;
}

/// True when `id` appears in the (normalized) file name as a whole word.
bool fileNameMentions(const QString& fileName, const QString& id)
{
    QString stem = stripDisabledSuffix(fileName).toLower();
    stem.replace(QLatin1Char('_'), QLatin1Char('-'));
    QString needle = id.toLower();
    needle.replace(QLatin1Char('_'), QLatin1Char('-'));
    if (needle.size() < 3)
        return false;
    int from = 0;
    for (;;) {
        const int pos = stem.indexOf(needle, from);
        if (pos < 0)
            return false;
        const bool startOk = pos == 0 || !stem[pos - 1].isLetterOrNumber();
        const int end = pos + needle.size();
        const bool endOk = end >= stem.size() || !stem[end].isLetterOrNumber();
        if (startOk && endOk)
            return true;
        from = pos + 1;
    }
}

struct Located {
    const ModEntry* entry = nullptr;
    const ModMetadata* top = nullptr;
};

struct Provider {
    const ModEntry* entry = nullptr;
    QString version;
    const ModMetadata* top = nullptr;
};

enum class WrongReason { Plain, Connector, NeoForgeCompat };

struct MissingGroup {
    QString depId;
    Confidence confidence = Confidence::Confirmed;
    QStringList names;
    QStringList ids;
    QStringList files;
    QStringList predicates;
};

const QStringList& environmentIds()
{
    static const QStringList ids = { "minecraft", "java",  "fabricloader", "quilt_loader", "forge",
                                     "neoforge",  "fml",   "javafml",      "lowcodefml" };
    return ids;
}

class Analyzer {
   public:
    Analyzer(const InstanceContext& ctx, const QStringList& disabledNames, const QList<ModMetadata>& disabledJars)
        : m_ctx(ctx), m_disabledNames(disabledNames), m_disabledJars(disabledJars), m_preferred(preferredFormats(ctx.loader))
    {}

    Report run(const QList<ModMetadata>& enabled)
    {
        // deterministic order
        QList<const ModMetadata*> jars;
        for (const ModMetadata& j : enabled)
            jars.append(&j);
        std::stable_sort(jars.begin(), jars.end(), [](const ModMetadata* a, const ModMetadata* b) {
            const int c = a->fileName.compare(b->fileName, Qt::CaseInsensitive);
            return c != 0 ? c < 0 : a->path < b->path;
        });

        // pass 1: which jars load
        QList<const ModMetadata*> notLoaded;
        for (const ModMetadata* jar : jars) {
            if (select(*jar).isEmpty())
                notLoaded.append(jar);
            else
                collectLoaded(*jar, jar);
        }
        for (const Located& l : m_loaded) {
            if (l.entry->id == QLatin1String("connector"))
                m_connector = true;  // Sinytra Connector: runs Fabric mods on Forge / NeoForge
        }

        // pass 2: the jars that do not load
        for (const ModMetadata* jar : notLoaded)
            classifyNotLoaded(*jar);

        buildIndex();

        for (const Located& l : m_loaded)
            checkEntry(l);
        emitMissing();
        checkDuplicates();

        Report report;
        report.modsScanned = int(enabled.size());
        report.findings = m_findings;
        std::stable_sort(report.findings.begin(), report.findings.end(), [](const Finding& a, const Finding& b) {
            if (a.confidence != b.confidence)
                return int(a.confidence) < int(b.confidence);
            const int n = a.args.value(0).compare(b.args.value(0), Qt::CaseInsensitive);
            if (n != 0)
                return n < 0;
            if (a.kind != b.kind)
                return int(a.kind) < int(b.kind);
            if (a.relatedId != b.relatedId)
                return a.relatedId < b.relatedId;
            return a.fileNames.join(QLatin1Char('|')) < b.fileNames.join(QLatin1Char('|'));
        });
        for (const Finding& f : report.findings) {
            switch (f.confidence) {
                case Confidence::Confirmed:
                    ++report.confirmed;
                    break;
                case Confidence::Likely:
                    ++report.likely;
                    break;
                case Confidence::Possible:
                    ++report.possible;
                    break;
                case Confidence::Unknown:
                    ++report.unknown;
                    break;
            }
        }
        return report;
    }

   private:
    // ---------------------------------------------------------------- selection

    QList<const ModEntry*> select(const ModMetadata& jar) const
    {
        for (LoaderFormat f : m_preferred) {
            if (!jar.hasFormat(f))
                continue;
            QList<const ModEntry*> out;
            for (const ModEntry& e : jar.mods)
                if (e.format == f)
                    out.append(&e);
            return out;
        }
        return {};
    }

    void collectLoaded(const ModMetadata& jar, const ModMetadata* top)
    {
        for (const ModEntry* e : select(jar))
            m_loaded.append({ e, top });
        for (const ModMetadata& n : jar.nested) {
            if (!select(n).isEmpty())
                collectLoaded(n, top);
            else if (!n.parseError.isEmpty())
                m_opaque = true;  // a bundled jar we could not read might provide anything
        }
    }

    /// Wrong-loader confidence of one foreign format in this instance.
    std::pair<Confidence, WrongReason> wrongFor(LoaderFormat format) const
    {
        switch (m_ctx.loader) {
            case InstanceLoader::Fabric:
            case InstanceLoader::Quilt:
                if (format == LoaderFormat::LegacyForge)
                    return { Confidence::Likely, WrongReason::Plain };
                return { Confidence::Confirmed, WrongReason::Plain };
            case InstanceLoader::Forge:
                if (format == LoaderFormat::Fabric && m_connector)
                    return { Confidence::Possible, WrongReason::Connector };
                return { Confidence::Confirmed, WrongReason::Plain };
            case InstanceLoader::NeoForge:
                if (format == LoaderFormat::Fabric && m_connector)
                    return { Confidence::Possible, WrongReason::Connector };
                if (format == LoaderFormat::Forge) {
                    // NeoForge 1.20.1 is built on Forge; the first NeoForge versions kept reading mods.toml. Unknown version: do not claim.
                    const auto lo = compareVersions(m_ctx.mcVersion, QStringLiteral("1.20.1"));
                    const auto hi = compareVersions(m_ctx.mcVersion, QStringLiteral("1.20.4"));
                    if (!lo || !hi || (*lo >= 0 && *hi <= 0))
                        return { Confidence::Possible, WrongReason::NeoForgeCompat };
                    return { Confidence::Confirmed, WrongReason::Plain };
                }
                if (format == LoaderFormat::LegacyForge)
                    return { Confidence::Likely, WrongReason::Plain };
                return { Confidence::Confirmed, WrongReason::Plain };
            case InstanceLoader::None:
                break;
        }
        return { Confidence::Unknown, WrongReason::Plain };
    }

    void classifyNotLoaded(const ModMetadata& jar)
    {
        bool unreadable = jar.mods.isEmpty();
        for (LoaderFormat f : jar.unparsedFormats)
            unreadable = unreadable || m_preferred.contains(f);
        if (unreadable) {
            m_opaque = true;
            Finding f;
            f.kind = FindingKind::UnreadableMetadata;
            f.confidence = Confidence::Unknown;
            f.fileNames = { jar.fileName };
            f.args = { jar.fileName };
            f.title = tr("Cannot read %1").arg(jar.fileName);
            f.detail = tr("ZERO can't read this mod's metadata; it may still work.");
            if (!jar.parseError.isEmpty())
                f.detail += QLatin1Char(' ') + jar.parseError;
            f.suggestion = tr("Nothing to do unless the game has problems. If it does, try starting without this file.");
            add(f);
            return;
        }

        // the jar holds mods, but for another loader
        Confidence conf = Confidence::Confirmed;
        WrongReason reason = WrongReason::Plain;
        for (LoaderFormat fmt : jar.formats()) {
            const auto w = wrongFor(fmt);
            if (int(w.first) >= int(conf)) {  // weakest alternative wins: the jar may load the best way it can
                conf = w.first;
                reason = w.second;
            }
        }
        const QString modName = jar.mods.first().displayName();
        const QString formatText = formatName(jar.formats().first());
        const QString loaderText = loaderName(m_ctx.loader);

        Finding f;
        f.kind = FindingKind::WrongLoader;
        f.confidence = conf;
        QSet<QString> ids;
        for (const ModEntry& e : jar.mods)
            ids.insert(e.id);
        f.modIds = ids.values();
        f.modIds.sort();
        f.fileNames = { jar.fileName };
        f.relatedId = loaderId();
        f.args = { modName, formatText, loaderText };
        f.title = tr("%1 is built for %2, not %3").arg(modName, formatText, loaderText);
        switch (conf) {
            case Confidence::Confirmed:
                f.detail = tr("%1 is built for %2, but this instance uses %3, so the game will not load it.").arg(modName, formatText, loaderText);
                f.suggestion = tr("Download the %1 version of this mod, or remove it.").arg(loaderText);
                break;
            case Confidence::Likely:
                f.detail = tr("%1 looks like a mod for %2, so it probably will not load on %3.").arg(modName, formatText, loaderText);
                f.suggestion = tr("Download the %1 version of this mod, or remove it.").arg(loaderText);
                break;
            default:
                if (reason == WrongReason::Connector)
                    f.detail = tr("%1 is built for %2, but this instance uses %3. A compatibility layer (Connector) is installed, "
                                  "so it may still load, but it may also fail.")
                                   .arg(modName, formatText, loaderText);
                else
                    f.detail = tr("%1 is built for %2, but this instance uses %3. It may not load.").arg(modName, formatText, loaderText);
                f.suggestion = tr("If the game has problems, get the %1 version of this mod or remove it.").arg(loaderText);
                break;
        }
        add(f);
        if (conf == Confidence::Possible) {
            // it may still load: whatever it provides must not be reported as missing
            for (const ModEntry& e : jar.mods) {
                m_uncertainIds.insert(e.id);
                for (const ProvidedId& p : e.provides)
                    m_uncertainIds.insert(p.id);
            }
        }
    }

    QString loaderId() const { return instanceLoaderId(m_ctx.loader); }

    // ---------------------------------------------------------------- providers

    void buildIndex()
    {
        for (const Located& l : m_loaded) {
            m_index[l.entry->id].append({ l.entry, l.entry->version, l.top });
            for (const ProvidedId& p : l.entry->provides)
                m_index[p.id].append({ l.entry, p.version.isEmpty() ? l.entry->version : p.version, l.top });
        }
    }

    /// Version of an id the environment supplies; `provided` tells whether it is one at all.
    QString environmentVersion(const QString& id, bool* provided) const
    {
        *provided = environmentIds().contains(id);
        if (id == QLatin1String("minecraft"))
            return m_ctx.mcVersion;
        if (id == QLatin1String("java"))
            return m_ctx.javaMajor > 0 ? QString::number(m_ctx.javaMajor) : QString();
        if (id == QLatin1String("fabricloader") && m_ctx.loader == InstanceLoader::Fabric)
            return m_ctx.loaderVersion;
        if (id == QLatin1String("quilt_loader") && m_ctx.loader == InstanceLoader::Quilt)
            return m_ctx.loaderVersion;
        if (id == QLatin1String("forge") && m_ctx.loader == InstanceLoader::Forge)
            return m_ctx.loaderVersion;
        if (id == QLatin1String("neoforge") && m_ctx.loader == InstanceLoader::NeoForge)
            return m_ctx.loaderVersion;
        return {};
    }

    bool isPresent(const QString& id) const
    {
        bool env = false;
        environmentVersion(id, &env);
        return env || m_index.contains(id) || m_uncertainIds.contains(id);
    }

    static Tri eval(const ModEntry& declarer, const QString& version, const QString& predicate)
    {
        return isMaven(declarer.format) ? matchesMavenRange(version, predicate) : matchesFabricPredicate(version, predicate);
    }

    QList<Provider> providersFor(const QString& id, const ModEntry* exclude) const
    {
        QList<Provider> out;
        for (const Provider& p : m_index.value(id))
            if (p.entry != exclude)
                out.append(p);
        return out;
    }

    // ---------------------------------------------------------------- checks

    void checkEntry(const Located& l)
    {
        const ModEntry& e = *l.entry;
        for (const Dependency& dep : e.depends) {
            if (dep.side == QLatin1String("SERVER"))
                continue;  // the launcher starts clients
            if (!dep.unless.isEmpty() && isPresent(dep.unless))
                continue;
            checkRequired(l, dep);
        }
        for (const Dependency& dep : e.recommends)
            checkOptionalRange(l, dep);
        for (const Dependency& dep : e.breaks)
            checkDeclared(l, dep, true);
        for (const Dependency& dep : e.conflicts)
            checkDeclared(l, dep, false);
    }

    void checkRequired(const Located& l, const Dependency& dep)
    {
        const ModEntry& e = *l.entry;
        bool env = false;
        const QString envVersion = environmentVersion(dep.id, &env);
        if (env) {
            if (eval(e, envVersion, dep.versionPredicate) != Tri::No)
                return;  // Yes or Unknown: never a claim
            if (dep.id == QLatin1String("minecraft"))
                addMinecraft(l, dep);
            else if (dep.id != QLatin1String("java") && !envVersion.isEmpty())
                addLoaderVersion(l, dep, envVersion);
            return;
        }

        const QList<Provider> providers = providersFor(dep.id, l.entry);
        if (providers.isEmpty()) {
            if (m_uncertainIds.contains(dep.id))
                return;  // a jar that may load supplies it
            const Confidence conf = m_opaque ? Confidence::Possible : Confidence::Confirmed;
            const QString key = dep.id + QLatin1Char('|') + QString::number(int(conf));
            MissingGroup& g = m_missing[key];
            g.depId = dep.id;
            g.confidence = conf;
            if (!g.names.contains(e.displayName()))
                g.names.append(e.displayName());
            if (!g.ids.contains(e.id))
                g.ids.append(e.id);
            if (!g.files.contains(l.top->fileName))
                g.files.append(l.top->fileName);
            if (!dep.versionPredicate.isEmpty() && !g.predicates.contains(describePredicate(dep.versionPredicate, isMaven(e.format))))
                g.predicates.append(describePredicate(dep.versionPredicate, isMaven(e.format)));
            return;
        }

        bool anyUnknown = false;
        for (const Provider& p : providers) {
            const Tri t = eval(e, p.version, dep.versionPredicate);
            if (t == Tri::Yes)
                return;
            anyUnknown = anyUnknown || t == Tri::Unknown;
        }
        const Provider& p = providers.first();
        const QString wanted = describePredicate(dep.versionPredicate, isMaven(e.format));
        Finding f;
        f.kind = FindingKind::DependencyVersion;
        f.confidence = anyUnknown ? Confidence::Possible : Confidence::Confirmed;
        f.modIds = sortedUnique({ e.id, p.entry->id });
        f.fileNames = sortedUnique({ l.top->fileName, p.top->fileName });
        f.relatedId = dep.id;
        const QString depName = p.entry->displayName();
        f.args = { e.displayName(), depName, wanted, p.version };
        f.title = tr("%1 does not accept this version of %2").arg(e.displayName(), depName);
        if (anyUnknown) {
            f.detail = tr("%1 asks for %2 %3. The installed version is %4, and ZERO cannot tell whether that fits, so this may be a problem.")
                           .arg(e.displayName(), depName, wanted, versionText(p.version));
            f.suggestion = tr("If the game has problems, update %1 and %2 so their versions fit together.").arg(e.displayName(), depName);
        } else {
            f.detail = tr("%1 needs %2 %3, but version %4 is installed.").arg(e.displayName(), depName, wanted, versionText(p.version));
            f.suggestion = tr("Update or replace %1 or %2 so their versions fit together.").arg(e.displayName(), depName);
        }
        add(f);
    }

    void addMinecraft(const Located& l, const Dependency& dep)
    {
        const ModEntry& e = *l.entry;
        const QString wanted = describePredicate(dep.versionPredicate, isMaven(e.format));
        Finding f;
        f.kind = FindingKind::MinecraftVersion;
        f.confidence = Confidence::Confirmed;
        f.modIds = { e.id };
        f.fileNames = { l.top->fileName };
        f.relatedId = QStringLiteral("minecraft");
        f.args = { e.displayName(), wanted, m_ctx.mcVersion };
        f.title = tr("%1 is made for another Minecraft version").arg(e.displayName());
        f.detail = tr("%1 is made for Minecraft %2, but this instance runs %3, so the game will refuse to load it.")
                       .arg(e.displayName(), wanted, m_ctx.mcVersion);
        f.suggestion = tr("Download the version of %1 made for Minecraft %2, or remove it.").arg(e.displayName(), m_ctx.mcVersion);
        add(f);
    }

    void addLoaderVersion(const Located& l, const Dependency& dep, const QString& have)
    {
        const ModEntry& e = *l.entry;
        const QString wanted = describePredicate(dep.versionPredicate, isMaven(e.format));
        const QString loader = loaderName(m_ctx.loader);
        Finding f;
        f.kind = FindingKind::LoaderVersion;
        f.confidence = Confidence::Confirmed;
        f.modIds = { e.id };
        f.fileNames = { l.top->fileName };
        f.relatedId = dep.id;
        f.args = { e.displayName(), loader, wanted, have };
        f.title = tr("%1 needs a different %2 version").arg(e.displayName(), loader);
        f.detail = tr("%1 needs %2 %3, but this instance uses %4, so the game will refuse to load it.").arg(e.displayName(), loader, wanted, have);
        f.suggestion = tr("Change the %1 version of the instance, or use another version of %2.").arg(loader, e.displayName());
        add(f);
    }

    /// Forge / NeoForge: an optional dependency still has to fit when it is installed. Declared by the mod, but weaker than
    /// a broken requirement, so Likely.
    void checkOptionalRange(const Located& l, const Dependency& dep)
    {
        const ModEntry& e = *l.entry;
        if ((e.format != LoaderFormat::Forge && e.format != LoaderFormat::NeoForge) || dep.versionPredicate.isEmpty())
            return;
        if (dep.side == QLatin1String("SERVER") || environmentIds().contains(dep.id))
            return;
        const QList<Provider> providers = providersFor(dep.id, l.entry);
        if (providers.isEmpty())
            return;
        for (const Provider& p : providers) {
            if (eval(e, p.version, dep.versionPredicate) != Tri::No)
                return;  // fits, or cannot be told
        }
        const Provider& p = providers.first();
        const QString wanted = describePredicate(dep.versionPredicate, true);
        Finding f;
        f.kind = FindingKind::DeclaredIncompatibility;
        f.confidence = Confidence::Likely;
        f.modIds = sortedUnique({ e.id, p.entry->id });
        f.fileNames = sortedUnique({ l.top->fileName, p.top->fileName });
        f.relatedId = dep.id;
        f.args = { e.displayName(), p.entry->displayName(), wanted, p.version };
        f.title = tr("%1 may not work with this version of %2").arg(e.displayName(), p.entry->displayName());
        f.detail = tr("%1 only accepts %2 %3, and version %4 is installed. This probably causes problems.")
                       .arg(e.displayName(), p.entry->displayName(), wanted, versionText(p.version));
        f.suggestion = tr("Update or replace %1 or %2 so their versions fit together.").arg(e.displayName(), p.entry->displayName());
        add(f);
    }

    void checkDeclared(const Located& l, const Dependency& dep, bool breaks)
    {
        const ModEntry& e = *l.entry;
        if (dep.side == QLatin1String("SERVER"))
            return;
        if (!dep.unless.isEmpty() && isPresent(dep.unless))
            return;
        const Confidence conf = breaks ? Confidence::Confirmed : Confidence::Likely;
        const QString wanted = describePredicate(dep.versionPredicate, isMaven(e.format));

        bool env = false;
        const QString envVersion = environmentVersion(dep.id, &env);
        QString otherName;
        QStringList otherFiles;
        QStringList otherIds;
        QString otherVersion;
        if (env) {
            if (eval(e, envVersion, dep.versionPredicate) != Tri::Yes)
                return;
            otherName = dep.id == QLatin1String("minecraft") ? QStringLiteral("Minecraft") : dep.id;
            otherVersion = envVersion;
        } else {
            const Provider* hit = nullptr;
            const QList<Provider> providers = providersFor(dep.id, l.entry);
            for (const Provider& p : providers) {
                if (p.top == l.top)
                    continue;  // inside the same jar
                if (eval(e, p.version, dep.versionPredicate) == Tri::Yes) {
                    hit = &p;
                    break;
                }
            }
            if (!hit)
                return;
            otherName = hit->entry->displayName();
            otherFiles = { hit->top->fileName };
            otherIds = { hit->entry->id };
            otherVersion = hit->version;
        }

        Finding f;
        f.kind = FindingKind::DeclaredIncompatibility;
        f.confidence = conf;
        f.modIds = sortedUnique(QStringList{ e.id } + otherIds);
        f.fileNames = sortedUnique(QStringList{ l.top->fileName } + otherFiles);
        f.relatedId = dep.id;
        f.args = { e.displayName(), otherName, wanted, otherVersion };
        if (breaks) {
            f.title = tr("%1 does not work with %2").arg(e.displayName(), otherName);
            f.detail = tr("%1 says it does not work with %2 (%3), and %2 %4 is installed. The game will refuse to start.")
                           .arg(e.displayName(), otherName, wanted, versionText(otherVersion));
            f.suggestion = tr("Remove one of them, or update both to versions that work together.");
        } else {
            f.title = tr("%1 conflicts with %2").arg(e.displayName(), otherName);
            f.detail = tr("%1 warns that it conflicts with %2 (%3), and %2 %4 is installed. This probably causes problems, "
                          "though the game may still start.")
                           .arg(e.displayName(), otherName, wanted, versionText(otherVersion));
            f.suggestion = tr("Remove one of them, or update both to versions that work together.");
        }
        add(f);
    }

    void emitMissing()
    {
        for (auto it = m_missing.cbegin(); it != m_missing.cend(); ++it) {
            const MissingGroup& g = it.value();
            QStringList names = g.names;
            names.sort(Qt::CaseInsensitive);
            Finding f;
            f.kind = FindingKind::MissingDependency;
            f.confidence = g.confidence;
            f.modIds = sortedUnique(g.ids);
            f.fileNames = sortedUnique(g.files);
            f.relatedId = g.depId;
            f.args = names;
            f.args.prepend(names.value(0));  // args[0] = main mod; the rest is the full list
            const QString who = joinNames(names);
            f.title = tr("Missing mod: %1").arg(g.depId);
            if (g.confidence == Confidence::Confirmed)
                f.detail = names.size() == 1 ? tr("%1 needs %2, but no installed mod provides it.").arg(who, g.depId)
                                             : tr("%1 need %2, but no installed mod provides it.").arg(who, g.depId);
            else
                f.detail = tr("%1 needs %2. ZERO did not find it, but some mods could not be read, so one of them may provide it.")
                               .arg(who, g.depId);
            if (g.predicates.size() == 1)
                f.detail += QLatin1Char(' ') + tr("Accepted versions: %1.").arg(g.predicates.first());

            bool certain = false;
            const QString disabled = findDisabledProvider(g.depId, &certain);
            if (!disabled.isEmpty())
                f.suggestion = certain ? tr("Enable %1.").arg(disabled)
                                       : tr("The disabled file %1 may be this mod. Enable it, or install %2.").arg(disabled, g.depId);
            else
                f.suggestion = tr("Install the mod \"%1\".").arg(g.depId);
            add(f);
        }
    }

    QString findDisabledProvider(const QString& id, bool* certain) const
    {
        *certain = false;
        QStringList names = m_disabledNames;
        for (const ModMetadata& j : m_disabledJars)
            if (!names.contains(j.fileName))
                names.append(j.fileName);
        names.sort(Qt::CaseInsensitive);

        QSet<QString> withMetadata;
        for (const ModMetadata& j : m_disabledJars) {
            withMetadata.insert(j.fileName);
            for (const ModEntry& e : j.mods) {
                bool provides = e.id == id;
                for (const ProvidedId& p : e.provides)
                    provides = provides || p.id == id;
                if (provides && m_preferred.contains(e.format)) {
                    *certain = true;
                    return j.fileName;
                }
            }
        }
        for (const QString& n : names) {
            if (!withMetadata.contains(n) && fileNameMentions(n, id))
                return n;
        }
        return {};
    }

    void checkDuplicates()
    {
        QMap<QString, QList<Located>> byId;
        for (const Located& l : m_loaded) {
            // only the top-level declarations: a copy bundled inside another jar is deduplicated by the loader
            for (const ModEntry& e : l.top->mods) {
                if (&e == l.entry)
                    byId[e.id].append(l);
            }
        }
        for (auto it = byId.cbegin(); it != byId.cend(); ++it) {
            QStringList files;
            for (const Located& l : it.value())
                if (!files.contains(l.top->fileName))
                    files.append(l.top->fileName);
            if (files.size() < 2)
                continue;
            files.sort(Qt::CaseInsensitive);
            bool legacy = false;
            QString name = it.value().first().entry->displayName();
            const Located* newest = nullptr;
            bool comparable = true;
            for (const Located& l : it.value()) {
                legacy = legacy || l.entry->format == LoaderFormat::LegacyForge;
                if (!newest) {
                    newest = &l;
                } else {
                    const auto c = compareVersions(l.entry->version, newest->entry->version);
                    if (!c)
                        comparable = false;
                    else if (*c > 0)
                        newest = &l;
                }
            }
            QStringList shown;
            for (const QString& file : files)
                shown.append(file);
            Finding f;
            f.kind = FindingKind::DuplicateMod;
            f.confidence = legacy ? Confidence::Likely : Confidence::Confirmed;
            f.modIds = { it.key() };
            f.fileNames = sortedUnique(files);
            f.relatedId = it.key();
            f.args = { name, shown.join(QStringLiteral(", ")) };
            f.title = tr("Duplicate mod: %1").arg(name);
            f.detail = legacy ? tr("%1 is installed more than once (%2). The game probably will not start with two copies of the same mod.")
                                    .arg(name, shown.join(QStringLiteral(", ")))
                              : tr("%1 is installed more than once (%2). The game refuses to start with two copies of the same mod.")
                                    .arg(name, shown.join(QStringLiteral(", ")));
            if (comparable && newest)
                f.suggestion = tr("Keep %1 (the newest version) and remove the other copies.").arg(newest->top->fileName);
            else
                f.suggestion = tr("Keep only one of these files and remove the others.");
            add(f);
        }
    }

    // ---------------------------------------------------------------- helpers

    static QStringList sortedUnique(const QStringList& in)
    {
        QStringList out;
        for (const QString& s : in)
            if (!s.isEmpty() && !out.contains(s))
                out.append(s);
        out.sort(Qt::CaseInsensitive);
        return out;
    }

    void add(const Finding& f)
    {
        QString key = QString::number(int(f.kind)) + QLatin1Char('|') + QString::number(int(f.confidence)) + QLatin1Char('|') +
                            f.modIds.join(QLatin1Char(',')) + QLatin1Char('|') + f.fileNames.join(QLatin1Char(',')) + QLatin1Char('|');
        // "A breaks B" and "B breaks A" are one conflict between the same two installed mods
        const bool pair = f.kind == FindingKind::DeclaredIncompatibility && f.modIds.size() > 1;
        if (!pair)
            key += f.relatedId;
        if (m_seen.contains(key))
            return;
        m_seen.insert(key);
        m_findings.append(f);
    }

    InstanceContext m_ctx;
    QStringList m_disabledNames;
    QList<ModMetadata> m_disabledJars;
    QList<LoaderFormat> m_preferred;
    QList<Located> m_loaded;
    QHash<QString, QList<Provider>> m_index;
    QSet<QString> m_uncertainIds;
    QMap<QString, MissingGroup> m_missing;
    QList<Finding> m_findings;
    QSet<QString> m_seen;
    bool m_opaque = false;
    bool m_connector = false;
};

}  // namespace

QString instanceLoaderId(InstanceLoader loader)
{
    switch (loader) {
        case InstanceLoader::Fabric:
            return QStringLiteral("fabric");
        case InstanceLoader::Quilt:
            return QStringLiteral("quilt");
        case InstanceLoader::Forge:
            return QStringLiteral("forge");
        case InstanceLoader::NeoForge:
            return QStringLiteral("neoforge");
        case InstanceLoader::None:
            break;
    }
    return QStringLiteral("none");
}

QString confidenceId(Confidence c)
{
    switch (c) {
        case Confidence::Confirmed:
            return QStringLiteral("confirmed");
        case Confidence::Likely:
            return QStringLiteral("likely");
        case Confidence::Possible:
            return QStringLiteral("possible");
        case Confidence::Unknown:
            break;
    }
    return QStringLiteral("unknown");
}

QString findingKindId(FindingKind kind)
{
    switch (kind) {
        case FindingKind::MissingDependency:
            return QStringLiteral("missing-dependency");
        case FindingKind::DependencyVersion:
            return QStringLiteral("dependency-version");
        case FindingKind::MinecraftVersion:
            return QStringLiteral("minecraft-version");
        case FindingKind::LoaderVersion:
            return QStringLiteral("loader-version");
        case FindingKind::WrongLoader:
            return QStringLiteral("wrong-loader");
        case FindingKind::DeclaredIncompatibility:
            return QStringLiteral("declared-incompatibility");
        case FindingKind::DuplicateMod:
            return QStringLiteral("duplicate-mod");
        case FindingKind::UnreadableMetadata:
            break;
    }
    return QStringLiteral("unreadable-metadata");
}

int Report::count(Confidence c) const
{
    switch (c) {
        case Confidence::Confirmed:
            return confirmed;
        case Confidence::Likely:
            return likely;
        case Confidence::Possible:
            return possible;
        case Confidence::Unknown:
            break;
    }
    return unknown;
}

InstanceContext readInstanceContext(const QString& instanceRoot)
{
    InstanceContext ctx;
    QFile file(QDir(instanceRoot).filePath(QStringLiteral("mmc-pack.json")));
    if (!file.open(QIODevice::ReadOnly))
        return ctx;
    const QJsonObject pack = QJsonDocument::fromJson(file.readAll()).object();
    for (const QJsonValue& value : pack.value(QStringLiteral("components")).toArray()) {
        const QJsonObject component = value.toObject();
        if (component.value(QStringLiteral("disabled")).toBool())
            continue;
        const QString uid = component.value(QStringLiteral("uid")).toString();
        const QString version = component.value(QStringLiteral("version")).toString();
        if (uid == QLatin1String("net.minecraft")) {
            ctx.mcVersion = version;
        } else if (ctx.loader == InstanceLoader::None) {
            if (uid == QLatin1String("net.fabricmc.fabric-loader"))
                ctx.loader = InstanceLoader::Fabric;
            else if (uid == QLatin1String("org.quiltmc.quilt-loader"))
                ctx.loader = InstanceLoader::Quilt;
            else if (uid == QLatin1String("net.minecraftforge"))
                ctx.loader = InstanceLoader::Forge;
            else if (uid == QLatin1String("net.neoforged"))
                ctx.loader = InstanceLoader::NeoForge;
            else
                continue;
            ctx.loaderVersion = version;
        }
    }
    return ctx;
}

Report analyze(const QList<ModMetadata>& enabledJars, const InstanceContext& context, const QStringList& disabledFileNames,
               const QList<ModMetadata>& disabledJars)
{
    Analyzer analyzer(context, disabledFileNames, disabledJars);
    return analyzer.run(enabledJars);
}

}  // namespace Mods
}  // namespace Zero
