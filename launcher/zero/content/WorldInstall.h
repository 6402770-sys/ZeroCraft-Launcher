// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the pure half of Discover > Worlds (docs/zero/SHELL.md, "Discover > Worlds - as built"): which Minecraft version a
// world was made for, the one-line note by version fit, the card pills, the "can only be downloaded on CurseForge" state, and where
// the download goes before Prism's InstallWorldTask unpacks it. No widgets, no APPLICATION, no network.
#pragma once

#include <QDir>
#include <QStringList>
#include <QUrl>

#include "modplatform/ModIndex.h"
#include "zero/content/ModpackSource.h"

namespace Zero::Content {

/// The newest release Minecraft version ("1.20.1") among `versions`. Only plain release numbers count (1.20, 1.20.1): snapshots,
/// pre-releases and loader / platform tags are ignored. Empty when there is none.
QString newestReleaseVersion(const QStringList& versions);

/// How a world file fits the target instance.
struct WorldFit {
    enum class Tone { None, Info, Warning };
    Tone tone = Tone::None;
    QString text;  ///< empty for Tone::None
};

/// `fileVersions` = the Minecraft versions of the chosen file, `targetVersion` = the instance's Minecraft version.
///  - the file's newest release newer than the target: Warning "Made for 1.21.4, newer than <name> (1.21.1): the game may not open it."
///  - older: Info "Made for 1.20.1. Minecraft upgrades it the first time you open it."
///  - the same, or either side unknown / not comparable (snapshots): no note.
WorldFit worldFit(const QStringList& fileVersions, const QString& targetVersion, const QString& targetName);

/// The file the add dialog preselects. `fileVersions` = the Minecraft versions of each file, newest file first. The first file that is
/// not "newer than the target" (worldFit() gives no Warning) wins, so a project whose newest file is for a newer game still offers its
/// newest file the target can open; when every file is newer (or the list is empty) it is 0 (-1 for an empty list).
int defaultWorldFileIndex(const QList<QStringList>& fileVersions, const QString& targetVersion);

/// "Made for 1.20.1": the newest release among the project's Minecraft versions; empty when unknown.
QString madeForText(const QStringList& projectVersions);

/// The footer pills of a world card: up to two category names (CurseForge's display names), then "Made for 1.20.1" when known.
QStringList worldPills(const ContentItem& item);

/// The file can be downloaded by the launcher (a download URL and a file name). CurseForge files of authors who turned third-party
/// downloads off come without a URL.
bool worldFileDownloadable(const ModPlatform::IndexedVersion& version);

/// `url` as a web address the launcher may open: http / https only, else an empty URL.
QUrl webUrl(const QString& url);

/// "This world can only be downloaded on CurseForge." and the text of the primary action then.
QString worldOnlyOnWebsiteText();
QString worldOpenWebsiteText();
/// "Add world".
QString worldPrimaryActionText();

// ---- the download before the install ----

/// `<instance root>/.zero/world-download`: where the zip of a world waits until InstallWorldTask has unpacked it.
QString worldDownloadDir(const QString& instanceRoot);
/// The file inside it: `<file id>-<file name>` with the file name reduced to a plain name (no folders, no characters a path cannot have).
QString worldDownloadPath(const QString& instanceRoot, const QString& fileId, const QString& fileName);
/// Deletes the downloaded file (whatever the outcome was: installed, failed, aborted) and the folders it made when they are empty now.
/// Returns false when the file is still there afterwards.
bool removeWorldDownload(const QString& path, const QString& instanceRoot);

/// The folder InstallWorldTask made: the single name in `after` that was not in `before`; empty when none or several are new.
QString newWorldFolder(const QStringList& before, const QStringList& after);

}  // namespace Zero::Content
