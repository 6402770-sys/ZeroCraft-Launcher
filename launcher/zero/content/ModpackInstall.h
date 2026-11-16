// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the install half of Discover > Modpacks (docs/zero/CONTENT.md, "Modpacks" > "Install"), without widgets.
//
// `planInstall()` turns "this pack, this version, these instance settings" into an `InstallPlan`: exactly what Prism's classic
// pages build (ModrinthPage / FlamePage / TechnicPage::suggestCurrent) plus what NewInstanceDialog::extractTask() sets. The
// plan is plain data, so tests read it; `createInstallTask()` turns it into the real InstanceTask. The Update flow (an instance
// that manages the pack) is the provider's own: the same task with `original_instance_id` (the way ManagedPackPage starts an
// update). The user chose between "New instance" and "Update an instance" in the install dialog, so the provider never asks again:
// `setConfirmUpdate(false)` on every task, and a new instance of a pack an instance already manages is told not to look for it.
//
// No widgets, no APPLICATION, no network in this file.
#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QUrl>

#include <optional>

#include "zero/content/ModpackSource.h"

class InstanceTask;
class QNetworkAccessManager;
class QObject;
class QWidget;

namespace Zero::Content {

/// What the install dialog collected.
struct InstallRequest {
    ModpackItemPtr item;
    ModpackVersion version;
    QString name;             ///< the instance name (trimmed by the caller)
    QString group;
    QString targetDir;        ///< canonical instance folder; empty = the default one
    bool copyTemplate = false;
    QString iconKey = QStringLiteral("default");
    QString updateInstanceId; ///< non-empty: update this instance (the provider's flow), else a new instance
    QString updateVersionName; ///< the managed version name of that instance (used to rename it, as ManagedPackPage does)
};

/// Update an instance in place is the provider's flow (Modrinth, CurseForge); Technic packs have none.
bool canUpdateInPlace(Provider provider);

/// The task to build and how its InstanceTask fields are set.
struct InstallPlan {
    enum class Kind {
        Import,         ///< InstanceImportTask(url, trusted = true, extraInfo): Modrinth and CurseForge
        TechnicSolder,  ///< Technic::SolderPackInstallTask(network, url, slug, build, minecraftVersion)
        TechnicZip      ///< Technic::SingleZipPackInstallTask(url, minecraftVersion)
    } kind = Kind::Import;
    QUrl url;
    QMap<QString, QString> extraInfo;  ///< Import: pack_id, pack_version_id (+ original_instance_id for an update)
    bool createNew = true;             ///< a new instance (no `original_instance_id`): the provider must not search for one to update
    QString technicSlug;
    QString technicBuild;
    QString minecraftVersion;

    // InstanceTask::set*()
    QString name;
    QString originalName;
    QString originalVersion;
    QString group;
    QString targetDir;
    QString iconKey;
    bool copyTemplate = false;
};

/// The plan for `request`, or nothing when the pack cannot be installed (no item, no download URL, Technic details missing).
std::optional<InstallPlan> planInstall(const InstallRequest& request);

/// The real task of `plan`, owned by the caller. `parent` is the widget the provider tasks parent their own dialogs to (it must
/// live as long as the task runs); `network` is only used by Technic Solder packs.
InstanceTask* createInstallTask(const InstallPlan& plan, QWidget* parent, QNetworkAccessManager* network);

/// The version this pack installs when the user does not choose: the Technic Solder build marked recommended, else the newest
/// release (the first of the provider's newest-first list when dates tie), else the newest of anything. Versions that cannot be
/// installed (`isInstallable`) never count. -1 when none can.
int defaultVersionIndex(const QList<ModpackVersion>& versions, Provider provider);
/// A version can be installed: Technic builds always (the task resolves the URL), the others need a download URL.
bool isInstallable(const ModpackVersion& version, Provider provider);

/// The versions the dialog starts from without asking the provider: Modrinth / CurseForge versions already loaded into the pack,
/// the single version of a Technic zip pack. Empty when a request is needed (`loaded` stays false).
QList<ModpackVersion> knownVersions(const ModpackItem& item, bool* loaded = nullptr);

/// "modrinth_", "curseforge_", "technic_": the prefix of an installed pack logo's icon name (the classic pages' keys).
QString logoPrefix(Provider provider);
/// The logo file name a classic page would use ("<logoName>", e.g. "my-pack.png"); `<slug>.<suffix of the icon URL>` when the provider
/// gave none. Characters that are not safe in a file name are replaced. Empty when the pack has no icon URL.
QString logoFileName(const ModpackItem& item);
/// The instance icon key an installed logo gets: "<prefix><logo file name without its suffix>"; "default" without a logo.
QString logoIconKey(const ModpackItem& item);
/// The icon file name handed to IconList::installIcon: "<prefix><logo file name>".
QString logoIconName(const ModpackItem& item);

/// The name the instance starts with: the pack name, trimmed to the instance name limit.
QString suggestedInstanceName(const ModpackItem& item);
/// "Release" / "Beta" / "Alpha" for the pill of a version; empty for an unknown type.
QString versionTypeText(ModpackVersion::Type type);
/// "1.20.1", "1.18.2, 1.19" ... : the Minecraft versions of a pack version, empty when unknown.
QString minecraftText(const ModpackVersion& version);
/// "Fabric, Forge": the loaders of a version (display names), empty when unknown.
QString loaderText(const ModpackVersion& version);

}  // namespace Zero::Content
