// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Mod Intelligence: the part of the dependency rules that `ModHealth` (Tools > Mod Health) and the `DependencyInspector`
// (instance Mods page) share, so the two never disagree: which jars load for the instance's loader, which ids a loaded jar provides
// (its own, `provides`, bundled jars), what the environment supplies (Minecraft, the loader, Java) and how a requirement is checked
// against a version. Pure; no I/O. Internal to `zero/mods`.
#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include "zero/mods/ModHealth.h"
#include "zero/mods/ModMetadata.h"
#include "zero/mods/Versions.h"

namespace Zero {
namespace Mods {
namespace Resolve {

bool isMaven(LoaderFormat format);
/// The formats the loader reads, best first (`None`: every format).
QList<LoaderFormat> preferredFormats(InstanceLoader loader);
/// Ids the environment supplies: minecraft, java, the loaders and their language providers.
const QStringList& environmentIds();
/// Version of an id the environment supplies; `provided` tells whether it is one at all.
QString environmentVersion(const InstanceContext& ctx, const QString& id, bool* provided);
/// Does `version` satisfy `predicate` as declared by an entry of `declarer`'s format (Maven range or Fabric predicate)?
Tri evalPredicate(LoaderFormat declarer, const QString& version, const QString& predicate);

/// An entry of a loaded jar. `nested` = it sits in a jar bundled inside `top` (not in `top` itself).
struct Located {
    const ModEntry* entry = nullptr;
    const ModMetadata* top = nullptr;
    bool nested = false;
};

struct Provider {
    const ModEntry* entry = nullptr;
    QString version;
    const ModMetadata* top = nullptr;
    bool nested = false;
};

enum class WrongReason { Plain, Connector, NeoForgeCompat };

/// A jar that does not load for the instance's loader.
struct NotLoaded {
    const ModMetadata* jar = nullptr;
    bool unreadable = false;  ///< no metadata, or the metadata of a format the loader reads did not parse
    Confidence confidence = Confidence::Confirmed;  ///< when `!unreadable`: how sure it is that the jar will not load
    WrongReason reason = WrongReason::Plain;
};

/// The weakest claim about a mod for another loader ("Confirmed" = the loader refuses it, "Possible" = a compatibility layer may load it).
std::pair<Confidence, WrongReason> wrongLoaderFor(const InstanceContext& ctx, LoaderFormat format, bool connector);

/// What a set of top-level jars supplies. `jars` must outlive the resolver and stay in the order the caller wants the entries listed.
class Resolver {
   public:
    explicit Resolver(const InstanceContext& ctx);

    void load(const QList<const ModMetadata*>& jars);

    const InstanceContext& context() const { return m_ctx; }
    const QList<LoaderFormat>& preferred() const { return m_preferred; }
    /// The entries of `jar` the loader reads (the first preferred format present); empty = the jar does not load.
    QList<const ModEntry*> select(const ModMetadata& jar) const;

    const QList<Located>& loaded() const { return m_loaded; }
    const QList<NotLoaded>& notLoaded() const { return m_notLoaded; }
    /// A jar ZERO could not read (top level or bundled) might supply anything: "missing" claims weaken.
    bool opaque() const { return m_opaque; }
    /// Sinytra Connector is loaded: Fabric mods may run on Forge / NeoForge.
    bool connector() const { return m_connector; }
    /// Ids of jars for another loader that may still load (Possible): never reported as missing.
    bool isUncertain(const QString& id) const { return m_uncertainIds.contains(id); }

    QString environmentVersion(const QString& id, bool* provided) const { return Resolve::environmentVersion(m_ctx, id, provided); }
    /// The environment, a loaded entry (own id or `provides`) or an uncertain id.
    bool isPresent(const QString& id) const;
    /// Providers of `id` among the loaded entries, `exclude` (the asking entry) left out.
    QList<Provider> providersFor(const QString& id, const ModEntry* exclude) const;

   private:
    void collect(const ModMetadata& jar, const ModMetadata* top, bool nested);

    InstanceContext m_ctx;
    QList<LoaderFormat> m_preferred;
    QList<Located> m_loaded;
    QList<NotLoaded> m_notLoaded;
    QHash<QString, QList<Provider>> m_index;
    QSet<QString> m_uncertainIds;
    bool m_opaque = false;
    bool m_connector = false;
};

}  // namespace Resolve
}  // namespace Mods
}  // namespace Zero
