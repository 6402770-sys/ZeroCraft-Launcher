// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the install half of Discover > Mods without widgets (docs/zero/CONTENT-MODS.md, "Mod install").
//
//  - which versions of a mod fit a target instance (`compatibleVersions`: the client-side rules of ModModel::checkVersionFilters,
//    minus the filter widget) and which one is preselected (`defaultModVersionIndex`),
//  - the INSTALL PLAN: the chosen mod plus the dependencies the check found, each with the reason and the "required by" Prism's
//    ResourceDownloadTask wants (`buildInstallPlan`),
//  - the names of the required dependencies the target already has (`alreadyInstalledNames`),
//  - the summary texts (button, toast),
//  - `DependencyResolver`: the small async interface the dialog asks "what else does this version need?". The production one over
//    GetModDependenciesTask is in ModDependencies.h; tests derive fakes. Answers echo a generation token so the caller can drop
//    the answer for a version that was replaced.
//
// No widgets, no APPLICATION, no network in this file.
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "modplatform/ModIndex.h"
#include "zero/content/InstalledMods.h"
#include "zero/content/ModpackSource.h"

namespace Zero::Content {

// ---- compatibility ----

/// The version fits the target: its Minecraft versions contain `mcVersion` (an empty list = the provider did not say = fits) and its
/// loaders intersect `loaders` (no loaders on either side = fits). Same rules as ModsSection::versionFits.
bool versionFitsTarget(const ModPlatform::IndexedVersion& version, const QString& mcVersion, ModPlatform::ModLoaderTypes loaders);
/// The versions of `all` that fit the target and can be downloaded (a download URL and a file name), in the provider's order
/// (newest first).
QList<ModPlatform::IndexedVersion> compatibleVersions(const QList<ModPlatform::IndexedVersion>& all, const QString& mcVersion,
                                                      ModPlatform::ModLoaderTypes loaders);
/// The version preselected among `compatible`: the newest release, else the newest of anything; -1 when empty.
int defaultModVersionIndex(const QList<ModPlatform::IndexedVersion>& compatible);

// ---- plan ----

/// Prism's `downloadReason` values of ResourceDownloadTask.
QString reasonStandalone();
QString reasonDependency();
QString reasonUpdate();

/// One jar to download.
struct PlannedMod {
    ModPlatform::IndexedPack::Ptr pack;
    ModPlatform::IndexedVersion version;
    QString reason;           ///< reasonStandalone() | reasonDependency() | reasonUpdate()
    QString dependentOn;      ///< the file id of the version that requires it (dependencies only; may be empty)
    QStringList requiredBy;   ///< names of the mods that require it (the row's "Required by ...")
    /// A file with a similar name is already in the mods folder (GetModDependenciesTask's `maybeInstalled`): Prism's review leaves it
    /// unchecked, so the dialog does not add it and lists it under "Already installed".
    bool maybeInstalled = false;

    QString name() const { return pack ? pack->name : QString(); }
};

struct ModInstallPlan {
    QList<PlannedMod> mods;  ///< the chosen mod first, then its dependencies

    int count() const { return int(mods.size()); }
    int dependencyCount() const { return qMax(0, count() - 1); }
    bool isEmpty() const { return mods.isEmpty(); }
};

/// The chosen mod (`reasonUpdate()` when `update`, else `reasonStandalone()`) and `dependencies` (reason forced to
/// `reasonDependency()`; the chosen mod itself, entries without pack or version, and repeated projects are dropped).
ModInstallPlan buildInstallPlan(const ModPlatform::IndexedPack::Ptr& pack, const ModPlatform::IndexedVersion& version, bool update,
                                const QList<PlannedMod>& dependencies);

/// The names of the REQUIRED dependencies of `version` that `installed` already holds (matched by provider + project id), in the
/// order of the dependency list. A dependency without a project id (Modrinth version-only dependencies) cannot be matched.
QStringList alreadyInstalledNames(ModPlatform::ResourceProvider provider, const ModPlatform::IndexedVersion& version,
                                  const QList<InstalledMod>& installed);

/// The plan of a resource pack: the chosen pack alone (`reasonUpdate()` when `update`, else `reasonStandalone()`); packs have no
/// dependency step.
ModInstallPlan buildPackInstallPlan(const ModPlatform::IndexedPack::Ptr& pack, const ModPlatform::IndexedVersion& version, bool update);

// ---- texts ----

/// "Add mod" / "Add 3 mods"; for an update "Update" / "Update and add 2 mods". `modCount` includes the chosen mod.
QString primaryActionText(int modCount, bool update);
/// "Add pack" / "Update" (a resource pack).
QString packPrimaryActionText(bool update);
/// "Add shader" / "Update" (a shader pack).
QString shaderPrimaryActionText(bool update);
/// "1 dependency" / "2 dependencies".
QString dependencyCountText(int count);
/// "Added Iris Shaders to Fabric Fun" / "... and 2 dependencies".
QString addedToastText(const QString& modName, const QString& instanceName, int dependencyCount);
/// "Updated Sodium in Fabric Fun" / "... and added 1 dependency".
QString updatedToastText(const QString& modName, const QString& instanceName, int dependencyCount);
/// "Required by Sodium" / "Required by Sodium, Lithium".
QString requiredByText(const QStringList& names);

// ---- the dependency check ----

class DependencyResolver : public QObject {
    Q_OBJECT
   public:
    /// One mod version the check covers.
    struct Selected {
        ModPlatform::IndexedPack::Ptr pack;
        ModPlatform::IndexedVersion version;
    };
    struct Request {
        ModPlatform::IndexedPack::Ptr pack;  ///< the chosen mod (the single review); null when `also` carries them all
        ModPlatform::IndexedVersion version;
        QString instanceId;
        /// More mods checked in the SAME run (the install cart's review): their dependencies are merged and a dependency that is one
        /// of the selected mods is not asked for again.
        QList<Selected> also;

        /// `pack` / `version` (when there is a pack) followed by `also`; entries without a pack are dropped.
        QList<Selected> all() const
        {
            QList<Selected> out;
            if (pack) {
                out.append({ pack, version });
            }
            for (const Selected& selected : also) {
                if (selected.pack) {
                    out.append(selected);
                }
            }
            return out;
        }
    };

    using QObject::QObject;
    ~DependencyResolver() override = default;

    /// Starts the check of `request` (answers resolved() or failed() with `token`). A new call replaces the one running: that one never
    /// answers.
    virtual void resolve(quint64 token, const Request& request) = 0;
    /// Stops the check running; nothing is answered for it.
    virtual void cancel() = 0;

   signals:
    /// The dependencies that will be added (`reason` dependency, `requiredBy` and `dependentOn` filled); empty = nothing else is needed.
    void resolved(quint64 token, const QList<Zero::Content::PlannedMod>& dependencies);
    void failed(quint64 token, const QString& reason);
};

}  // namespace Zero::Content

Q_DECLARE_METATYPE(Zero::Content::PlannedMod)
Q_DECLARE_METATYPE(QList<Zero::Content::PlannedMod>)
