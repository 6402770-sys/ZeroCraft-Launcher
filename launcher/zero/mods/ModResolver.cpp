// SPDX-License-Identifier: GPL-3.0-only
#include "ModResolver.h"

namespace Zero {
namespace Mods {
namespace Resolve {

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

const QStringList& environmentIds()
{
    static const QStringList ids = { "minecraft", "java",  "fabricloader", "quilt_loader", "forge",
                                     "neoforge",  "fml",   "javafml",      "lowcodefml" };
    return ids;
}

QString environmentVersion(const InstanceContext& ctx, const QString& id, bool* provided)
{
    *provided = environmentIds().contains(id);
    if (id == QLatin1String("minecraft"))
        return ctx.mcVersion;
    if (id == QLatin1String("java"))
        return ctx.javaMajor > 0 ? QString::number(ctx.javaMajor) : QString();
    if (id == QLatin1String("fabricloader") && ctx.loader == InstanceLoader::Fabric)
        return ctx.loaderVersion;
    if (id == QLatin1String("quilt_loader") && ctx.loader == InstanceLoader::Quilt)
        return ctx.loaderVersion;
    if (id == QLatin1String("forge") && ctx.loader == InstanceLoader::Forge)
        return ctx.loaderVersion;
    if (id == QLatin1String("neoforge") && ctx.loader == InstanceLoader::NeoForge)
        return ctx.loaderVersion;
    return {};
}

Tri evalPredicate(LoaderFormat declarer, const QString& version, const QString& predicate)
{
    return isMaven(declarer) ? matchesMavenRange(version, predicate) : matchesFabricPredicate(version, predicate);
}

std::pair<Confidence, WrongReason> wrongLoaderFor(const InstanceContext& ctx, LoaderFormat format, bool connector)
{
    switch (ctx.loader) {
        case InstanceLoader::Fabric:
        case InstanceLoader::Quilt:
            if (format == LoaderFormat::LegacyForge)
                return { Confidence::Likely, WrongReason::Plain };
            return { Confidence::Confirmed, WrongReason::Plain };
        case InstanceLoader::Forge:
            if (format == LoaderFormat::Fabric && connector)
                return { Confidence::Possible, WrongReason::Connector };
            return { Confidence::Confirmed, WrongReason::Plain };
        case InstanceLoader::NeoForge:
            if (format == LoaderFormat::Fabric && connector)
                return { Confidence::Possible, WrongReason::Connector };
            if (format == LoaderFormat::Forge) {
                // NeoForge 1.20.1 is built on Forge; the first NeoForge versions kept reading mods.toml. Unknown version: do not claim.
                const auto lo = compareVersions(ctx.mcVersion, QStringLiteral("1.20.1"));
                const auto hi = compareVersions(ctx.mcVersion, QStringLiteral("1.20.4"));
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

Resolver::Resolver(const InstanceContext& ctx) : m_ctx(ctx), m_preferred(preferredFormats(ctx.loader)) {}

QList<const ModEntry*> Resolver::select(const ModMetadata& jar) const
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

void Resolver::collect(const ModMetadata& jar, const ModMetadata* top, bool nested)
{
    for (const ModEntry* e : select(jar))
        m_loaded.append({ e, top, nested });
    for (const ModMetadata& n : jar.nested) {
        if (!select(n).isEmpty())
            collect(n, top, true);
        else if (!n.parseError.isEmpty())
            m_opaque = true;  // a bundled jar we could not read might provide anything
    }
}

void Resolver::load(const QList<const ModMetadata*>& jars)
{
    m_loaded.clear();
    m_notLoaded.clear();
    m_index.clear();
    m_uncertainIds.clear();
    m_opaque = false;
    m_connector = false;

    // pass 1: which jars load
    QList<const ModMetadata*> notLoaded;
    for (const ModMetadata* jar : jars) {
        if (select(*jar).isEmpty())
            notLoaded.append(jar);
        else
            collect(*jar, jar, false);
    }
    for (const Located& l : m_loaded) {
        if (l.entry->id == QLatin1String("connector"))
            m_connector = true;  // Sinytra Connector: runs Fabric mods on Forge / NeoForge
    }

    // pass 2: the jars that do not load
    for (const ModMetadata* jar : notLoaded) {
        NotLoaded n;
        n.jar = jar;
        bool unreadable = jar->mods.isEmpty();
        for (LoaderFormat f : jar->unparsedFormats)
            unreadable = unreadable || m_preferred.contains(f);
        n.unreadable = unreadable;
        if (unreadable) {
            m_opaque = true;
        } else {
            // the jar holds mods, but for another loader; the weakest alternative wins: the jar may load the best way it can
            for (LoaderFormat fmt : jar->formats()) {
                const auto w = wrongLoaderFor(m_ctx, fmt, m_connector);
                if (int(w.first) >= int(n.confidence)) {
                    n.confidence = w.first;
                    n.reason = w.second;
                }
            }
            if (n.confidence == Confidence::Possible) {
                // it may still load: whatever it provides must not be reported as missing
                for (const ModEntry& e : jar->mods) {
                    m_uncertainIds.insert(e.id);
                    for (const ProvidedId& p : e.provides)
                        m_uncertainIds.insert(p.id);
                }
            }
        }
        m_notLoaded.append(n);
    }

    for (const Located& l : m_loaded) {
        m_index[l.entry->id].append({ l.entry, l.entry->version, l.top, l.nested });
        for (const ProvidedId& p : l.entry->provides)
            m_index[p.id].append({ l.entry, p.version.isEmpty() ? l.entry->version : p.version, l.top, l.nested });
    }
}

bool Resolver::isPresent(const QString& id) const
{
    bool env = false;
    environmentVersion(id, &env);
    return env || m_index.contains(id) || m_uncertainIds.contains(id);
}

QList<Provider> Resolver::providersFor(const QString& id, const ModEntry* exclude) const
{
    QList<Provider> out;
    for (const Provider& p : m_index.value(id))
        if (p.entry != exclude)
            out.append(p);
    return out;
}

}  // namespace Resolve
}  // namespace Mods
}  // namespace Zero
