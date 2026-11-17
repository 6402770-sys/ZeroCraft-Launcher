// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - where world backups are written (Smart Backup, see docs/zero/ARCHITECTURE.md). Pure helpers, no widgets, no
// APPLICATION: the default root `<data>/zero/backups`, an optional folder the user picked (`ZeroBackupRoot`; backups then go to
// `<folder>/ZERO Backups/<instanceId>/<worldFolder>`), detection of the cloud-sync folders of this PC (OneDrive, Google Drive,
// Dropbox) through an injectable environment, the writability check, the fallback to the default root and the list of every root
// that was ever used (so old backups stay listed and restorable after the destination changed).
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>

namespace Zero {
namespace Backup {

/// Name of the folder ZERO creates inside the folder the user picked.
inline constexpr char kBackupsFolderName[] = "ZERO Backups";

/// A cloud-sync folder found on this PC.
struct CloudFolder {
    QString label;  ///< "OneDrive" | "Google Drive" | "Dropbox" | "Dropbox (business)"
    QString path;   ///< the sync folder (cleaned, '/'-separated)
};

/// What the detection needs from the machine; every member left empty uses the real implementation (systemEnvironment()).
struct Environment {
    std::function<QString(const QString& name)> env;     ///< environment variable ("" when unset)
    std::function<bool(const QString& dir)> dirExists;   ///< existing directory
    std::function<QByteArray(const QString& file)> readFile;  ///< file contents, empty when it cannot be read
    std::function<QStringList()> driveRoots;              ///< ready volumes, e.g. "C:/", "G:/"
};

/// The real environment.
Environment systemEnvironment();

/// OneDrive (`%OneDrive%`, else `%OneDriveConsumer%`), Google Drive for desktop (`<drive>:/My Drive` on any mounted volume, else
/// `%USERPROFILE%/Google Drive/My Drive` or `%USERPROFILE%/My Drive`) and Dropbox (`personal.path`, then `business.path`, of
/// `%LOCALAPPDATA%/Dropbox/info.json` or `%APPDATA%/Dropbox/info.json`). Only folders that exist; no duplicates; that order.
QList<CloudFolder> detectCloudFolders(const Environment& environment);

/// `<chosenFolder>/ZERO Backups` (cleaned); empty when `chosenFolder` is empty.
QString rootInFolder(const QString& chosenFolder);

/// `<root>/<instanceId>/<worldFolder>`
QString worldBackupDirIn(const QString& root, const QString& instanceId, const QString& worldFolder);

/// The root new backups go to when the destination works. `chosenFolder` empty = `defaultRoot`.
QString activeRoot(const QString& defaultRoot, const QString& chosenFolder);

struct Probe {
    bool ok = false;
    QString error;  ///< user-facing reason
};
/// Checks that `chosenFolder` exists and that `<chosenFolder>/ZERO Backups` can be created and written (a probe file is written and
/// removed). Synchronous disk I/O: call it from a pool thread when the folder may be on a slow drive.
Probe probeFolder(const QString& chosenFolder);

struct Resolved {
    QString root;        ///< where this backup is written
    bool fellBack = false;  ///< the chosen folder is not usable: `root` is the default root
    QString chosenFolder;   ///< the folder that failed (only when fellBack)
    QString reason;         ///< why (only when fellBack)
};
/// The root to write into right now: the chosen folder's root when it is writable, else `defaultRoot` with fellBack set.
Resolved resolveRoot(const QString& defaultRoot, const QString& chosenFolder);

/// "Saved in the ZERO folder: <folder> isn't reachable" (translated; the folder is shown with native separators).
QString fallbackMessage(const QString& chosenFolder);

/// `defaultRoot` first, then `extra` and `active`; cleaned, case-insensitive duplicates and empty entries removed.
QStringList mergeRoots(const QString& defaultRoot, const QStringList& extra, const QString& active = {});

/// The roots ever used besides the default one, kept in `<data>/zero/backup-roots.json`. A file that cannot be read is treated as
/// empty and replaced by the next add().
class KnownRoots {
   public:
    explicit KnownRoots(QString path);
    QStringList list() const { return m_roots; }
    /// Remembers `root` (cleaned) and saves. False when it was already known, empty, or the file could not be written.
    bool add(const QString& root);

   private:
    bool save() const;
    QString m_path;
    QStringList m_roots;
};

}  // namespace Backup
}  // namespace Zero
