// SPDX-License-Identifier: GPL-3.0-only
#include "DependencyInspector.h"

#include <QCollator>
#include <QLocale>
#include <QPair>
#include <QSet>
#include <algorithm>

#include "zero/mods/ModResolver.h"
#include "zero/mods/Versions.h"

namespace Zero {
namespace Mods {

namespace {

using namespace Resolve;

QString stripSuffixes(QString name)
{
    for (const char* suffix : { ".disabled", ".jar" }) {
        if (name.endsWith(QLatin1String(suffix), Qt::CaseInsensitive))
            name.chop(int(qstrlen(suffix)));
    }
    return name;
}

/// Plain-language rule; "" = any version; "?" = the rule could not be read.
QString predicateText(const Dependency& dep, LoaderFormat format)
{
    const QString raw = dep.versionPredicate.trimmed();
    if (raw == QLatin1String("?"))
        return raw;
    if (raw.isEmpty() || raw == QLatin1String("*"))
        return {};
    return describePredicate(raw, isMaven(format));
}

/// The ids the environment supplies that the panel shows under "Game".
bool isGameId(const QString& id)
{
    return id == QLatin1String("minecraft") || id == QLatin1String("java") || id == QLatin1String("fabricloader") ||
           id == QLatin1String("quilt_loader") || id == QLatin1String("forge") || id == QLatin1String("neoforge");
}

QString gameName(const QString& id)
{
    if (id == QLatin1String("minecraft"))
        return QStringLiteral("Minecraft");
    if (id == QLatin1String("java"))
        return QStringLiteral("Java");
    if (id == QLatin1String("fabricloader"))
        return QStringLiteral("Fabric Loader");
    if (id == QLatin1String("quilt_loader"))
        return QStringLiteral("Quilt Loader");
    if (id == QLatin1String("forge"))
        return QStringLiteral("Forge");
    return QStringLiteral("NeoForge");
}

DepEntry fromProvider(const Dependency& dep, const ModEntry& declarer, const Provider& p, DepState state)
{
    DepEntry out;
    out.id = dep.id;
    out.name = p.entry->displayName();
    out.predicate = predicateText(dep, declarer.format);
    out.state = state;
    out.version = p.version;
    out.fileName = p.top->fileName;
    out.path = p.top->path;
    return out;
}

/// The entries of one top-level file that count, and how it stands.
struct FileEntries {
    QList<Located> entries;
    bool readable = true;
    bool loads = true;
};

FileEntries entriesOf(const Resolver& res, const ModMetadata& jar)
{
    FileEntries out;
    for (const Located& l : res.loaded()) {
        if (l.top == &jar)
            out.entries.append(l);
    }
    if (!out.entries.isEmpty())
        return out;
    for (const NotLoaded& n : res.notLoaded()) {
        if (n.jar != &jar)
            continue;
        if (n.unreadable) {
            out.readable = false;
            return out;
        }
        // made for another loader: show what it declares (the first format it has), flagged as not loading
        out.loads = false;
        const LoaderFormat format = jar.formats().first();
        for (const ModEntry& e : jar.mods) {
            if (e.format == format)
                out.entries.append({ &e, &jar, false });
        }
        return out;
    }
    out.readable = false;  // neither loaded nor classified: nothing is known
    return out;
}

class Inspector {
   public:
    Inspector(const Resolver& enabled, const Resolver& disabled) : m_en(enabled), m_dis(disabled) {}

    ModInspection inspectFile(const ModMetadata& jar, bool enabled) const
    {
        ModInspection out;
        out.fileName = jar.fileName;
        out.path = jar.path;
        out.enabled = enabled;
        const FileEntries file = entriesOf(enabled ? m_en : m_dis, jar);
        out.readable = file.readable;
        out.loads = file.loads;
        out.name = jar.mods.isEmpty() ? stripSuffixes(jar.fileName) : jar.mods.first().displayName();
        if (!file.readable)
            return out;
        if (!file.entries.isEmpty())
            out.name = file.entries.first().entry->displayName();

        // The file's own entries first, then the ones of the jars bundled inside it: those only add what is a problem (a needs row
        // "for bundled X", a conflict), never a row that is just fine, so Mod Menu does not list what its bundled libraries need.
        for (const bool nested : { false, true }) {
            for (const Located& l : file.entries) {
                if (l.nested != nested)
                    continue;
                const ModEntry& e = *l.entry;
                for (const Dependency& dep : e.depends) {
                    if (skipped(dep))
                        continue;
                    if (isGameId(dep.id)) {
                        if (!nested)
                            addGame(out.game, e, dep);
                    } else if (!environmentIds().contains(dep.id)) {
                        DepEntry need = resolveNeed(e, dep);
                        if (need.path == jar.path && need.state == DepState::Installed)
                            continue;  // another entry of this very file: nothing to tell
                        if (nested) {
                            if (!isProblem(need.state))
                                continue;
                            need.forBundled = e.displayName();
                        }
                        addNeed(out.needs, need);
                    }
                }
                if (nested)
                    continue;
                for (const Dependency& dep : e.recommends) {
                    if (environmentIds().contains(dep.id) || dep.side == QLatin1String("SERVER"))
                        continue;
                    addUniqueById(out.optional, optionalState(l, dep));
                }
            }
        }
        for (const Located& l : file.entries) {
            const ModEntry& e = *l.entry;
            for (const QList<Dependency>* list : { &e.breaks, &e.conflicts }) {
                for (const Dependency& dep : *list) {
                    if (skipped(dep) || environmentIds().contains(dep.id))
                        continue;
                    const DepEntry conflict = conflictState(l, dep);
                    if (conflict.state == DepState::Installed)
                        addUniqueById(out.conflicts, conflict);
                    else if (!out.otherConflicts.contains(dep.id))
                        out.otherConflicts.append(dep.id);
                }
            }
        }
        return out;
    }

   private:
    /// A requirement that does not apply: server-only (the launcher starts clients), or waived by a mod that is there (Quilt `unless`).
    bool skipped(const Dependency& dep) const
    {
        if (dep.side == QLatin1String("SERVER"))
            return true;
        return !dep.unless.isEmpty() && m_en.isPresent(dep.unless);
    }

    static bool isProblem(DepState state)
    {
        return state == DepState::Missing || state == DepState::Disabled || state == DepState::WrongVersion;
    }

    /// One row per id: the first one wins, unless it is fine and a later one is a problem (the row pills must keep seeing it).
    static void addNeed(QList<DepEntry>& list, const DepEntry& entry)
    {
        for (DepEntry& e : list) {
            if (e.id != entry.id)
                continue;
            if (!isProblem(e.state) && isProblem(entry.state))
                e = entry;
            return;
        }
        list.append(entry);
    }

    static void addUniqueById(QList<DepEntry>& list, const DepEntry& entry)
    {
        for (const DepEntry& e : list) {
            if (e.id == entry.id)
                return;
        }
        list.append(entry);
    }


    void addGame(QList<GameEntry>& list, const ModEntry& e, const Dependency& dep) const
    {
        GameEntry g;
        g.id = dep.id;
        g.name = gameName(dep.id);
        g.kind = dep.id == QLatin1String("minecraft") ? GameKind::Minecraft : dep.id == QLatin1String("java") ? GameKind::Java : GameKind::Loader;
        g.predicate = predicateText(dep, e.format);
        bool env = false;
        g.version = m_en.environmentVersion(dep.id, &env);
        if (g.kind == GameKind::Java || g.version.isEmpty()) {
            g.state = GameState::Unknown;  // Java is not checked; an unknown version is never a claim
        } else {
            switch (evalPredicate(e.format, g.version, dep.versionPredicate)) {
                case Tri::Yes:
                    g.state = GameState::Ok;
                    break;
                case Tri::No:
                    g.state = GameState::Mismatch;
                    break;
                case Tri::Unknown:
                    g.state = GameState::Unknown;
                    break;
            }
        }
        for (const GameEntry& other : list) {
            if (other.id == g.id)  // one row per id: the top-level jar's predicate was added first
                return;
        }
        list.append(g);
    }

    /// Same walk as ModHealth::checkRequired, with a state instead of a finding.
    DepEntry resolveNeed(const ModEntry& e, const Dependency& dep) const
    {
        const QList<Provider> providers = m_en.providersFor(dep.id, &e);
        if (providers.isEmpty()) {
            DepEntry out;
            out.id = dep.id;
            out.name = dep.id;
            out.predicate = predicateText(dep, e.format);
            if (m_en.isUncertain(dep.id)) {
                out.state = DepState::Unknown;  // a jar made for another loader that may still load supplies it
                return out;
            }
            const QList<Provider> off = m_dis.providersFor(dep.id, nullptr);
            if (!off.isEmpty()) {
                const Provider* pick = &off.first();
                for (const Provider& p : off) {
                    if (!p.nested) {
                        pick = &p;
                        break;
                    }
                }
                return fromProvider(dep, e, *pick, DepState::Disabled);
            }
            out.state = m_en.opaque() ? DepState::Unknown : DepState::Missing;  // an unreadable jar may provide it
            return out;
        }

        bool anyUnknown = false;
        const Provider* yes = nullptr;
        for (const Provider& p : providers) {
            const Tri t = evalPredicate(e.format, p.version, dep.versionPredicate);
            if (t == Tri::Yes) {
                if (!yes || (yes->nested && !p.nested))
                    yes = &p;  // a top-level provider beats a bundled copy
            }
            anyUnknown = anyUnknown || t == Tri::Unknown;
        }
        if (yes)
            return fromProvider(dep, e, *yes, yes->nested ? DepState::Bundled : DepState::Installed);
        return fromProvider(dep, e, providers.first(), anyUnknown ? DepState::Unknown : DepState::WrongVersion);
    }

    DepEntry optionalState(const Located& l, const Dependency& dep) const
    {
        const QList<Provider> providers = m_en.providersFor(dep.id, l.entry);
        if (providers.isEmpty()) {
            DepEntry out;
            out.id = dep.id;
            out.name = dep.id;
            out.predicate = predicateText(dep, l.entry->format);
            out.state = DepState::NotInstalled;
            return out;
        }
        const Provider* pick = &providers.first();
        for (const Provider& p : providers) {
            if (!p.nested) {
                pick = &p;
                break;
            }
        }
        return fromProvider(dep, *l.entry, *pick, DepState::Installed);
    }

    /// Installed = an enabled mod outside this file matches the declared version (as in ModHealth::checkDeclared).
    DepEntry conflictState(const Located& l, const Dependency& dep) const
    {
        DepEntry out;
        out.id = dep.id;
        out.name = dep.id;
        out.predicate = predicateText(dep, l.entry->format);
        out.state = DepState::NotInstalled;
        for (const Provider& p : m_en.providersFor(dep.id, l.entry)) {
            if (p.top == l.top)
                continue;  // inside the same jar
            if (evalPredicate(l.entry->format, p.version, dep.versionPredicate) == Tri::Yes) {
                return fromProvider(dep, *l.entry, p, DepState::Installed);
            }
        }
        return out;
    }

    const Resolver& m_en;
    const Resolver& m_dis;
};

QList<const ModMetadata*> sortedPointers(const QList<ModMetadata>& jars)
{
    QList<const ModMetadata*> out;
    for (const ModMetadata& j : jars)
        out.append(&j);
    std::stable_sort(out.begin(), out.end(), [](const ModMetadata* a, const ModMetadata* b) {
        const int c = a->fileName.compare(b->fileName, Qt::CaseInsensitive);
        return c != 0 ? c < 0 : a->path < b->path;
    });
    return out;
}

}  // namespace

QString depStateId(DepState state)
{
    switch (state) {
        case DepState::Installed:
            return QStringLiteral("installed");
        case DepState::Bundled:
            return QStringLiteral("bundled");
        case DepState::Disabled:
            return QStringLiteral("disabled");
        case DepState::WrongVersion:
            return QStringLiteral("wrong-version");
        case DepState::Missing:
            return QStringLiteral("missing");
        case DepState::Unknown:
            return QStringLiteral("unknown");
        case DepState::NotInstalled:
            break;
    }
    return QStringLiteral("not-installed");
}

const ModInspection* Inspection::find(const QString& path) const
{
    const auto it = byPath.constFind(path);
    return it == byPath.constEnd() ? nullptr : &mods.at(it.value());
}

Problems problemsOf(const ModInspection& mod)
{
    Problems out;
    if (!mod.readable || !mod.loads)
        return out;
    for (const DepEntry& d : mod.needs) {
        switch (d.state) {
            case DepState::Missing:
                out.missing.append(d.name);
                break;
            case DepState::Disabled:
                out.disabled.append(d.name);
                break;
            case DepState::WrongVersion:
                out.wrongVersion.append(d.name);
                break;
            default:
                break;
        }
    }
    for (const DepEntry& d : mod.conflicts) {
        if (d.state == DepState::Installed)
            out.conflicts.append(d.name);
    }
    return out;
}

Inspection inspect(const QList<ModMetadata>& enabledJars, const QList<ModMetadata>& disabledJars, const InstanceContext& context)
{
    const QList<const ModMetadata*> enabled = sortedPointers(enabledJars);
    const QList<const ModMetadata*> disabled = sortedPointers(disabledJars);

    Resolver enabledResolver(context);
    enabledResolver.load(enabled);
    Resolver disabledResolver(context);
    disabledResolver.load(disabled);
    const Inspector inspector(enabledResolver, disabledResolver);

    Inspection out;
    for (const ModMetadata* jar : enabled)
        out.mods.append(inspector.inspectFile(*jar, true));
    for (const ModMetadata* jar : disabled)
        out.mods.append(inspector.inspectFile(*jar, false));
    for (int i = 0; i < out.mods.size(); ++i)
        out.byPath.insert(out.mods.at(i).path, i);

    // Needed by: the files whose mandatory dependency resolved to a file. A bundled copy is not "needed": its carrier just ships it.
    for (int i = 0; i < out.mods.size(); ++i) {
        const ModInspection& g = out.mods.at(i);
        QSet<QString> done;
        if (!g.readable || !g.loads)
            continue;  // a mod that will not load does not need anything
        for (const DepEntry& d : g.needs) {
            if (d.path.isEmpty() || d.path == g.path || d.state == DepState::Bundled || d.state == DepState::Missing || done.contains(d.path))
                continue;
            const auto it = out.byPath.constFind(d.path);
            if (it == out.byPath.constEnd())
                continue;
            done.insert(d.path);
            out.mods[it.value()].neededBy.append({ g.fileName, g.path, g.name, g.enabled });
        }
    }

    // Conflicts both ways: when B declares a conflict with an installed A, A's panel names B too (Mod Health pairs them the same way).
    QList<QPair<int, DepEntry>> reverse;
    for (int i = 0; i < out.mods.size(); ++i) {
        const ModInspection& b = out.mods.at(i);
        if (!b.enabled || !b.readable || !b.loads)
            continue;
        for (const DepEntry& d : b.conflicts) {
            if (d.state != DepState::Installed || d.path.isEmpty() || d.path == b.path)
                continue;
            const auto it = out.byPath.constFind(d.path);
            if (it == out.byPath.constEnd())
                continue;
            DepEntry back;
            back.id = b.fileName;
            back.name = b.name;
            back.state = DepState::Installed;
            back.fileName = b.fileName;
            back.path = b.path;
            reverse.append({ it.value(), back });
        }
    }
    for (const auto& r : reverse) {
        ModInspection& a = out.mods[r.first];
        const bool known = std::any_of(a.conflicts.begin(), a.conflicts.end(),
                                       [&r](const DepEntry& c) { return c.state == DepState::Installed && c.path == r.second.path; });
        if (!known && a.readable && a.loads)
            a.conflicts.append(r.second);
    }

    // stable, readable order inside each list: by name (the JSON the parsers read has no order of its own); the game starts with
    // Minecraft; the conflicts that are really installed come first
    QCollator collator{ QLocale(QLocale::English) };
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    const auto byName = [&collator](const DepEntry& a, const DepEntry& b) {
        const int c = collator.compare(a.name, b.name);
        return c != 0 ? c < 0 : a.id < b.id;
    };
    for (ModInspection& m : out.mods) {
        std::stable_sort(m.needs.begin(), m.needs.end(), byName);
        std::stable_sort(m.optional.begin(), m.optional.end(), byName);
        std::stable_sort(m.conflicts.begin(), m.conflicts.end(), [&byName](const DepEntry& a, const DepEntry& b) {
            const bool ai = a.state == DepState::Installed;
            const bool bi = b.state == DepState::Installed;
            return ai != bi ? ai : byName(a, b);
        });
        std::stable_sort(m.game.begin(), m.game.end(), [](const GameEntry& a, const GameEntry& b) { return int(a.kind) < int(b.kind); });
        std::stable_sort(m.neededBy.begin(), m.neededBy.end(), [&collator](const NeededBy& a, const NeededBy& b) {
            const int c = collator.compare(a.name, b.name);
            return c != 0 ? c < 0 : a.fileName < b.fileName;
        });
    }
    return out;
}

}  // namespace Mods
}  // namespace Zero
