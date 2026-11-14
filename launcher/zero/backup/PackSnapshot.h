// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the safety net of modpack updates: a copy of the instance taken before the update, and the roll back that brings the
// instance back to it (docs/zero/ARCHITECTURE.md, "Smart Backup" > "Modpack snapshots"). Service layer: no widgets, no APPLICATION; every
// function is synchronous (PackSnapshotTasks runs them on a pool thread).
//
// Prism's update removes the old pack files inside the instance folder while its task runs and then copies the new ones over it, so the
// copy must exist BEFORE the task starts. The copy is the zip engine of WorldBackup (verified zip + SHA-256 manifest, `.part` then rename)
// with an exclude predicate: the player's own data (`saves`, `screenshots`, `logs`, `crash-reports`, `.cache` of the game folder and any
// `.zero-*` staging folder) is never part of it. It lives in `<data>/zero/snapshots/<instance id>/pack-<yyyyMMdd-HHmmss>.zip` next to a
// small `.json` that holds what the zip cannot change after it was written: the outcome of the update and "rolled back".
//
// Data-safety rules of the roll back (this code replaces a whole instance folder):
//  - the snapshot is verified, then extracted into `<instances>/.zero-restore-<id>-<ts>` and verified again; nothing of the instance is touched
//    before that worked;
//  - the player's data is moved (renamed, never copied) from the instance into the staging folder; `instance.cfg` of the staging folder is the
//    CURRENT file with only the `ManagedPack*` keys of the snapshot (play time, ZERO profile keys and Java settings stay);
//  - the swap is two renames (instance -> `.zero-replaced-<id>-<ts>`, staging -> instance); any failure puts everything back;
//  - the replaced folder goes to the Recycle Bin (Hooks::trashPath, default FS::trash and NOTHING else: no permanent delete). When that
//    fails it stays where it is and the result says so;
//  - the only things deleted for good are ZERO's own: older snapshots (retention, newest 2 kept) and its empty staging folder.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <functional>

namespace Zero {
namespace Backup {
namespace Pack {

constexpr int kFormat = 1;
constexpr int kKeep = 2;  ///< snapshots kept per instance (they are big)
inline constexpr char kOutcomePending[] = "pending";  ///< the update has not finished (or the launcher ended in the middle of it)
inline constexpr char kOutcomeOk[] = "ok";
inline constexpr char kOutcomeFailed[] = "failed";

/// Seams for tests; every member left empty uses the real implementation.
struct Hooks {
    /// Moves a path to the Recycle Bin. Default: FS::trash and nothing else (never a permanent delete).
    std::function<bool(const QString& path)> trashPath;
    /// Directory / file rename; must fail when the destination exists. Default: Backup::renamePath.
    std::function<bool(const QString& from, const QString& to)> rename;
    /// Free bytes on the volume of a path (-1 = unknown, no check). Default: QStorageInfo.
    std::function<qint64(const QString& path)> freeBytes;
    /// Milliseconds since the epoch (names and the created time). Default: the clock.
    std::function<qint64()> nowMs;
    /// True for a symbolic link, a junction or a shortcut (the zip engine skips those). Default: QFileInfo.
    std::function<bool(const QString& path)> isLink;
};

/// What a snapshot says about the update it was taken for.
struct Meta {
    QString instanceId;    ///< the instance folder name
    QString instanceName;
    QString packId;        ///< ManagedPackID: a snapshot only fits an instance of the same pack
    QString packName;      ///< ManagedPackName
    QString oldVersion;    ///< ManagedPackVersionName before the update
    QString newVersion;    ///< the version the update goes to (may be empty)
    QString gameDir;       ///< the game folder relative to the instance folder (".minecraft" / "minecraft", empty = the instance folder)
};

struct Info {
    bool valid = false;  ///< the snapshot can be read (a full verification happens in rollback())
    QString error;
    QString zipPath;
    Meta meta;
    qint64 createdMs = 0;
    QString outcome = QString::fromLatin1(kOutcomePending);
    bool rolledBack = false;
    qint64 fileSize = 0;
};

/// Cancel / progress of createSnapshot (called on the calling thread).
struct Control {
    std::function<bool()> cancelled;
    std::function<void(qint64 done, qint64 total)> progress;
};

struct CreateResult {
    bool ok = false;
    bool cancelled = false;
    QString error;
    QString zipPath;
};

struct RollbackResult {
    bool ok = false;
    QString error;
    /// Set when the roll back worked but the replaced folder could not go to the Recycle Bin: it is still there (named `.zero-leftover-*`
    /// when it could be renamed), safe to delete.
    QString leftoverPath;
};

/// `<dataRoot>/zero/snapshots/<instanceId>`
QString snapshotsDirFor(const QString& dataRoot, const QString& instanceId);
/// The game folder (`gameRoot`) relative to the instance folder, '/'-separated ("" when it is the instance folder itself).
QString relativeGameDir(const QString& instanceRoot, const QString& gameRoot);

/// True for a path (relative to the instance folder, '/'-separated) that is never part of a snapshot: `<game dir>/saves`, `screenshots`,
/// `logs`, `crash-reports`, `.cache` (and everything below) and anything below a `.zero-*` folder.
bool isExcluded(const QString& rel, const QString& gameDir);
/// The folders of the player's data that a roll back moves into the restored instance, relative to the instance folder.
QStringList playDataFolders(const QString& gameDir);

/// `current` = the instance.cfg as it is now, `snapshot` = the copy in the snapshot. Result: `current` where only the `ManagedPack*` keys of
/// the [General] section are the snapshot's (keys the snapshot does not have are dropped). Line endings of `current` are kept.
QString mergeInstanceCfg(const QString& current, const QString& snapshot);

/// Copies the instance into a new snapshot (see file header), applies the retention (newest kKeep, the new one included).
CreateResult createSnapshot(const QString& instanceRoot, const QString& snapshotsDir, const Meta& meta, const Hooks& hooks = {},
                            const Control& control = {});

/// Newest first. Reads the small `.json` of every `pack-*.zip` (its manifest when the `.json` is missing); `.part` files are ignored.
QList<Info> listSnapshots(const QString& snapshotsDir);
/// The newest snapshot that was not rolled back and fits the pack (`packId` empty = any). `valid = false` when there is none.
Info newestActive(const QList<Info>& snapshots, const QString& packId);

/// Records how the update ended ("ok" | "failed").
bool markOutcome(const QString& zipPath, const QString& outcome);
bool markRolledBack(const QString& zipPath);

/// Removes the newest-kKeep-excluded snapshots (zip + json) and `.part` files older than an hour; only `pack-*` names, never a path in `protectedZips`.
int applyRetention(const QString& snapshotsDir, const QStringList& protectedZips = {}, int keep = kKeep);
/// Removes one snapshot (zip + json) of `snapshotsDir`: ZERO's own file (an update that turned out to change nothing).
bool removeSnapshot(const QString& zipPath, const QString& snapshotsDir);

/// What `recoverInterrupted` did, for the log.
struct RecoverResult {
    QStringList actions;   ///< one line per thing that was put back / removed
    QStringList problems;  ///< one line per thing that could not be finished (it is left where it is)
};

/// Startup recovery of a roll back that was cut off (crash, power loss) inside `instancesDir`. Per instance id, with the hidden folders
/// `.zero-restore-<id>-<ts>` (staging; holds the player's moved folders and links) and `.zero-replaced-<id>-<ts>` (the instance that was
/// stepped aside):
///  - replaced exists, the instance does not: it is renamed back; then the staging folder (when there is one) is handled as below;
///  - the instance and a staging folder exist: every play-data folder and link of the staging folder that is free in the instance goes
///    back into it; the staging folder (ZERO's own extracted copy) is removed only when nothing of the player remains in it;
///  - the instance and a replaced folder exist and no staging folder: the replaced folder goes to the Recycle Bin (Hooks::trashPath).
/// A `.zero-leftover-<id>-<ts>` (a replaced folder that could not be trashed after a finished roll back) is only offered to the Recycle Bin
/// again. Nothing is deleted for good except the extracted copy; a failure leaves everything in place and is listed in `problems`.
RecoverResult recoverInterrupted(const QString& instancesDir, const Hooks& hooks = {});

/// Brings `instanceRoot` back to the snapshot, keeping the player's data (see file header). `expectedPackId` (when set) must match the snapshot.
RollbackResult rollback(const QString& instanceRoot, const QString& zipPath, const QString& snapshotsDir, const QString& expectedPackId,
                        const Hooks& hooks = {});

}  // namespace Pack
}  // namespace Backup
}  // namespace Zero
