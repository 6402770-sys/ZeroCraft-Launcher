// SPDX-License-Identifier: GPL-3.0-only
#include "ModHealth.h"

#include <QCollator>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMap>
#include <QSet>
#include <algorithm>
#include <utility>

#include "zero/mods/ModResolver.h"
#include "zero/mods/Versions.h"

namespace Zero {
namespace Mods {

namespace {

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
    return QCoreApplication::translate("ModHealth", "no mod loader");
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
            return QCoreApplication::translate("ModHealth", "an older version of Forge");
        case LoaderFormat::Unknown:
            break;
    }
    return QCoreApplication::translate("ModHealth", "another mod loader");
}

using Resolve::environmentIds;
using Resolve::isMaven;
using Resolve::Located;
using Resolve::Provider;
using Resolve::WrongReason;

QString joinNames(const QStringList& names)
{
    if (names.size() <= 1)
        return names.value(0);
    if (names.size() == 2)
        return QCoreApplication::translate("ModHealth", "%1 and %2").arg(names[0], names[1]);
    if (names.size() == 3)
        return QCoreApplication::translate("ModHealth", "%1, %2 and %3").arg(names[0], names[1], names[2]);
    return QCoreApplication::translate("ModHealth", "%1, %2 and %3 more").arg(names[0], names[1]).arg(names.size() - 2);
}

QString versionText(const QString& v)
{
    return v.isEmpty() ? QCoreApplication::translate("ModHealth", "(version unknown)") : v;
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

struct MissingGroup {
    QString depId;
    Confidence confidence = Confidence::Confirmed;
    QStringList names;
    QStringList ids;
    QStringList files;
    QStringList predicates;
};

class Analyzer {
   public:
    Analyzer(const InstanceContext& ctx, const QStringList& disabledNames, const QList<ModMetadata>& disabledJars)
        : m_ctx(ctx), m_disabledNames(disabledNames), m_disabledJars(disabledJars), m_res(ctx)
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

        // which jars load and what they provide (shared with the DependencyInspector), then the jars that do not load
        m_res.load(jars);
        for (const Resolve::NotLoaded& n : m_res.notLoaded())
            classifyNotLoaded(n);

        for (const Located& l : m_res.loaded())
            checkEntry(l);
        emitMissing();
        checkDuplicates();

        Report report;
        report.modsScanned = int(enabled.size());
        report.findings = m_findings;
        // Natural order ("mod2" before "mod10"), the same for every UI language.
        QCollator collator{ QLocale(QLocale::English) };
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        std::stable_sort(report.findings.begin(), report.findings.end(), [&collator](const Finding& a, const Finding& b) {
            if (a.confidence != b.confidence)
                return int(a.confidence) < int(b.confidence);
            const int n = collator.compare(a.args.value(0), b.args.value(0));
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
    // ---------------------------------------------------------------- jars that do not load

    void classifyNotLoaded(const Resolve::NotLoaded& notLoaded)
    {
        const ModMetadata& jar = *notLoaded.jar;
        if (notLoaded.unreadable) {
            Finding f;
            f.kind = FindingKind::UnreadableMetadata;
            f.confidence = Confidence::Unknown;
            f.fileNames = { jar.fileName };
            f.args = { jar.fileName };
            f.title = QCoreApplication::translate("ModHealth", "Cannot read %1").arg(jar.fileName);
            f.detail = QCoreApplication::translate("ModHealth", "ZERO can't read this mod's metadata; it may still work.");
            if (!jar.parseError.isEmpty())
                f.detail += QLatin1Char(' ') + jar.parseError;
            f.suggestion = QCoreApplication::translate("ModHealth", "Nothing to do unless the game has problems. If it does, try starting without this file.");
            add(f);
            return;
        }

        // the jar holds mods, but for another loader (the resolver picked the weakest claim: the jar may load the best way it can)
        const Confidence conf = notLoaded.confidence;
        const WrongReason reason = notLoaded.reason;
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
        f.title = QCoreApplication::translate("ModHealth", "%1 is built for %2, not %3").arg(modName, formatText, loaderText);
        switch (conf) {
            case Confidence::Confirmed:
                f.detail = QCoreApplication::translate("ModHealth", "%1 is built for %2, but this instance uses %3, so the game will not load it.").arg(modName, formatText, loaderText);
                f.suggestion = QCoreApplication::translate("ModHealth", "Download the %1 version of this mod, or remove it.").arg(loaderText);
                break;
            case Confidence::Likely:
                f.detail = QCoreApplication::translate("ModHealth", "%1 looks like a mod for %2, so it probably will not load on %3.").arg(modName, formatText, loaderText);
                f.suggestion = QCoreApplication::translate("ModHealth", "Download the %1 version of this mod, or remove it.").arg(loaderText);
                break;
            default:
                if (reason == WrongReason::Connector)
                    f.detail = QCoreApplication::translate("ModHealth", "%1 is built for %2, but this instance uses %3. A compatibility layer (Connector) is installed, "
                                  "so it may still load, but it may also fail.")
                                   .arg(modName, formatText, loaderText);
                else
                    f.detail = QCoreApplication::translate("ModHealth", "%1 is built for %2, but this instance uses %3. It may not load.").arg(modName, formatText, loaderText);
                f.suggestion = QCoreApplication::translate("ModHealth", "If the game has problems, get the %1 version of this mod or remove it.").arg(loaderText);
                break;
        }
        add(f);
    }

    QString loaderId() const { return instanceLoaderId(m_ctx.loader); }

    // ---------------------------------------------------------------- providers (shared rules: ModResolver)

    QString environmentVersion(const QString& id, bool* provided) const { return m_res.environmentVersion(id, provided); }
    bool isPresent(const QString& id) const { return m_res.isPresent(id); }
    static Tri eval(const ModEntry& declarer, const QString& version, const QString& predicate)
    {
        return Resolve::evalPredicate(declarer.format, version, predicate);
    }
    QList<Provider> providersFor(const QString& id, const ModEntry* exclude) const { return m_res.providersFor(id, exclude); }

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
            if (m_res.isUncertain(dep.id))
                return;  // a jar that may load supplies it
            const Confidence conf = m_res.opaque() ? Confidence::Possible : Confidence::Confirmed;
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
        f.title = QCoreApplication::translate("ModHealth", "%1 does not accept this version of %2").arg(e.displayName(), depName);
        if (anyUnknown) {
            f.detail = QCoreApplication::translate("ModHealth", "%1 asks for %2 %3. The installed version is %4, and ZERO cannot tell whether that fits, so this may be a problem.")
                           .arg(e.displayName(), depName, wanted, versionText(p.version));
            f.suggestion = QCoreApplication::translate("ModHealth", "If the game has problems, update %1 and %2 so their versions fit together.").arg(e.displayName(), depName);
        } else {
            f.detail = QCoreApplication::translate("ModHealth", "%1 needs %2 %3, but version %4 is installed.").arg(e.displayName(), depName, wanted, versionText(p.version));
            f.suggestion = QCoreApplication::translate("ModHealth", "Update or replace %1 or %2 so their versions fit together.").arg(e.displayName(), depName);
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
        f.title = QCoreApplication::translate("ModHealth", "%1 is made for another Minecraft version").arg(e.displayName());
        f.detail = QCoreApplication::translate("ModHealth", "%1 is made for Minecraft %2, but this instance runs %3, so the game will refuse to load it.")
                       .arg(e.displayName(), wanted, m_ctx.mcVersion);
        f.suggestion = QCoreApplication::translate("ModHealth", "Download the version of %1 made for Minecraft %2, or remove it.").arg(e.displayName(), m_ctx.mcVersion);
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
        f.title = QCoreApplication::translate("ModHealth", "%1 needs a different %2 version").arg(e.displayName(), loader);
        f.detail = QCoreApplication::translate("ModHealth", "%1 needs %2 %3, but this instance uses %4, so the game will refuse to load it.").arg(e.displayName(), loader, wanted, have);
        f.suggestion = QCoreApplication::translate("ModHealth", "Change the %1 version of the instance, or use another version of %2.").arg(loader, e.displayName());
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
        f.title = QCoreApplication::translate("ModHealth", "%1 may not work with this version of %2").arg(e.displayName(), p.entry->displayName());
        f.detail = QCoreApplication::translate("ModHealth", "%1 only accepts %2 %3, and version %4 is installed. This probably causes problems.")
                       .arg(e.displayName(), p.entry->displayName(), wanted, versionText(p.version));
        f.suggestion = QCoreApplication::translate("ModHealth", "Update or replace %1 or %2 so their versions fit together.").arg(e.displayName(), p.entry->displayName());
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
            f.title = QCoreApplication::translate("ModHealth", "%1 does not work with %2").arg(e.displayName(), otherName);
            f.detail = QCoreApplication::translate("ModHealth", "%1 says it does not work with %2 (%3), and %2 %4 is installed. The game will refuse to start.")
                           .arg(e.displayName(), otherName, wanted, versionText(otherVersion));
            f.suggestion = QCoreApplication::translate("ModHealth", "Remove one of them, or update both to versions that work together.");
        } else {
            f.title = QCoreApplication::translate("ModHealth", "%1 conflicts with %2").arg(e.displayName(), otherName);
            f.detail = QCoreApplication::translate("ModHealth", "%1 warns that it conflicts with %2 (%3), and %2 %4 is installed. This probably causes problems, "
                          "though the game may still start.")
                           .arg(e.displayName(), otherName, wanted, versionText(otherVersion));
            f.suggestion = QCoreApplication::translate("ModHealth", "Remove one of them, or update both to versions that work together.");
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
            f.title = QCoreApplication::translate("ModHealth", "Missing mod: %1").arg(g.depId);
            if (g.confidence == Confidence::Confirmed)
                f.detail = names.size() == 1 ? QCoreApplication::translate("ModHealth", "%1 needs %2, but no installed mod provides it.").arg(who, g.depId)
                                             : QCoreApplication::translate("ModHealth", "%1 need %2, but no installed mod provides it.").arg(who, g.depId);
            else
                f.detail = QCoreApplication::translate("ModHealth", "%1 needs %2. ZERO did not find it, but some mods could not be read, so one of them may provide it.")
                               .arg(who, g.depId);
            if (g.predicates.size() == 1)
                f.detail += QLatin1Char(' ') + QCoreApplication::translate("ModHealth", "Accepted versions: %1.").arg(g.predicates.first());

            bool certain = false;
            const QString disabled = findDisabledProvider(g.depId, &certain);
            if (!disabled.isEmpty())
                f.suggestion = certain ? QCoreApplication::translate("ModHealth", "Enable %1.").arg(disabled)
                                       : QCoreApplication::translate("ModHealth", "The disabled file %1 may be this mod. Enable it, or install %2.").arg(disabled, g.depId);
            else
                f.suggestion = QCoreApplication::translate("ModHealth", "Install the mod \"%1\".").arg(g.depId);
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
                if (provides && m_res.preferred().contains(e.format)) {
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
        for (const Located& l : m_res.loaded()) {
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
            f.title = QCoreApplication::translate("ModHealth", "Duplicate mod: %1").arg(name);
            f.detail = legacy ? QCoreApplication::translate("ModHealth", "%1 is installed more than once (%2). The game probably will not start with two copies of the same mod.")
                                    .arg(name, shown.join(QStringLiteral(", ")))
                              : QCoreApplication::translate("ModHealth", "%1 is installed more than once (%2). The game refuses to start with two copies of the same mod.")
                                    .arg(name, shown.join(QStringLiteral(", ")));
            if (comparable && newest)
                f.suggestion = QCoreApplication::translate("ModHealth", "Keep %1 (the newest version) and remove the other copies.").arg(newest->top->fileName);
            else
                f.suggestion = QCoreApplication::translate("ModHealth", "Keep only one of these files and remove the others.");
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
    Resolve::Resolver m_res;
    QMap<QString, MissingGroup> m_missing;
    QList<Finding> m_findings;
    QSet<QString> m_seen;
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

QList<RepairAction> Report::allActions() const
{
    QList<RepairAction> out;
    QSet<QString> seen;
    for (const Finding& f : findings) {
        for (const RepairAction& a : f.actions) {
            if (seen.contains(a.fileName))
                continue;
            seen.insert(a.fileName);
            out.append(a);
        }
    }
    return out;
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
