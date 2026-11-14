// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the safety net of mod updates: a snapshot of the files an update is about to replace, and the one-click roll back
// (docs/zero/SHELL.md, "Mods page"). Service layer: no widgets, no APPLICATION; every function is synchronous (the page runs them on
// a pool thread).
//
// A snapshot lives in `<data>/zero/snapshots/<instance id>/<yyyyMMdd-HHmmss>[-n]/` as `snapshot.json` + `files/<path relative to the
// mods folder>`. It holds only the jar (enabled or `.disabled`) and the packwiz entry (`.index/<slug>.pw.toml`) of the mods being updated,
// so it is small. Data-safety rules (this code writes inside the user's mods folder):
//  - a snapshot is built in `<name>.part`, every copy is written to `<file>.part`, verified against its SHA-256 and renamed; the folder is
//    renamed to its final name only when snapshot.json is on disk;
//  - the roll back never overwrites a file in place and never deletes a user file permanently: everything it replaces goes to the
//    Recycle Bin (`Hooks::trashPath`, default FS::trash, NO fallback to a permanent delete). The restored copies are staged and verified
//    in `<mods>/.zero-rollback-<n>/` before the first file is touched;
//  - the only things this code deletes for good are ZERO's own copies: snapshot folders (retention, an update that changed nothing) and
//    its staging folder.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace Zero {
namespace Mods {
namespace Snapshot {

constexpr int kFormat = 1;
constexpr int kKeep = 5;  ///< snapshots kept per instance
inline constexpr char kFileName[] = "snapshot.json";
inline constexpr char kKindModsUpdate[] = "mods-update";
inline constexpr char kOutcomeOk[] = "ok";
inline constexpr char kOutcomeFailed[] = "failed";

/// Seams for tests; every member left empty uses the real implementation.
struct Hooks {
    /// Moves a path to the Recycle Bin. Default: FS::trash and nothing else (never a permanent delete).
    std::function<bool(const QString& path)> trashPath;
    /// File rename; must fail when the destination exists. Default: QFile::rename.
    std::function<bool(const QString& from, const QString& to)> rename;
    /// Free bytes on the volume of a path (-1 = unknown, no check). Default: QStorageInfo.
    std::function<qint64(const QString& path)> freeBytes;
    /// Milliseconds since the epoch (names and the created time). Default: the clock.
    std::function<qint64()> nowMs;
};

/// One file of a snapshot: `path` is relative to the mods folder ("sodium.jar", ".index/sodium.pw.toml"), '/'-separated.
struct FileRef {
    QString path;
    qint64 size = 0;
    QString sha256;
};

/// What the caller says about one mod it is about to update. `files` are absolute paths inside the mods folder (the jar, the packwiz entry).
struct ModFiles {
    QString name;
    QString oldVersion;
    QString newVersion;
    QStringList files;
};

/// One mod of a snapshot: the files it owns (relative paths) and whether the update changed any of them.
struct ModEntry {
    QString name;
    QString oldVersion;
    QString newVersion;
    QStringList files;
    bool changed = false;
};

/// A snapshot as read back from snapshot.json.
struct Info {
    bool valid = false;
    QString error;  ///< why it is not valid
    QString dir;    ///< the snapshot folder (absolute)
    QString id;     ///< its folder name
    QString instanceId;
    qint64 createdMs = 0;
    QString kind;
    QString outcome;  ///< "" = the update has not finished (or the app died), "ok" | "failed"
    bool rolledBack = false;
    bool dismissed = false;
    QList<FileRef> files;
    QList<ModEntry> mods;
    QStringList added;  ///< paths that appeared during the update (new jars, new packwiz entries)
    /// Mods whose files changed. For a snapshot not finalized yet: 0.
    int changedCount() const;
    QStringList changedNames() const;
};

/// `<dataRoot>/zero/snapshots/<instanceId>`
QString snapshotsDirFor(const QString& dataRoot, const QString& instanceId);

/// What is in the mods folder: the top-level files and the files of `.index/` (relative paths, sorted). Links are listed like files;
/// directories are not.
QStringList listFolder(const QString& modsDir);

struct CreateResult {
    bool ok = false;
    QString error;      ///< user-facing reason when !ok ("Not enough free space ...")
    QString dir;        ///< the snapshot folder
    QStringList before; ///< listFolder() taken just before the copies; hand it to finalizeSnapshot()
};

/// Copies the files of `mods` into a new snapshot below `snapshotsDir` (`snapshotDirFor`). Fails (and leaves nothing behind) when a file
/// is outside the mods folder, a mod has no existing file, the space is short or a copy / verification fails. Applies the retention
/// after a successful creation (`kKeep` newest kept).
CreateResult createSnapshot(const QString& snapshotsDir, const QString& instanceId, const QString& modsDir, const QList<ModFiles>& mods,
                            const Hooks& hooks = {});

struct FinalizeResult {
    bool ok = false;
    QString error;
    bool pruned = false;  ///< nothing changed: the snapshot folder was removed
    int changedMods = 0;
    QStringList changedNames;
};

/// After the update ran: `added` = what is there now and was not in `before`; every mod gets `changed` (one of its files is gone or
/// differs); `outcome` = ok / failed (`success`). A snapshot that protects nothing (nothing added, nothing changed) is removed.
FinalizeResult finalizeSnapshot(const QString& snapshotDir, const QString& modsDir, const QStringList& before, bool success);

Info readSnapshot(const QString& snapshotDir);
/// Newest first (by created time). A folder that is not a snapshot folder name is ignored; an unreadable snapshot is listed with valid = false.
QList<Info> listSnapshots(const QString& snapshotsDir);
/// The newest snapshot, when it is finished (outcome set), valid, not rolled back and not dismissed. The newest one only: an older
/// one is never offered behind a newer one that was rolled back or dismissed.
Info newestActive(const QString& snapshotsDir);

/// Writes `dismissed: true` into the snapshot (it stays for the retention).
bool markDismissed(const QString& snapshotDir, QString* error = nullptr);

/// Keeps the `keep` newest snapshots, removes the older ones (and stale `.part` folders). Snapshot folders only. Returns how many were removed.
int applyRetention(const QString& snapshotsDir, int keep = kKeep);

struct RollbackResult {
    bool ok = false;
    /// True when the mods folder was touched (some file went to the Recycle Bin or was restored): after a failure the snapshot is kept
    /// and `error` says what state the folder is in.
    bool touched = false;
    QString error;
    int restored = 0;           ///< files put back
    int trashed = 0;            ///< files moved to the Recycle Bin
    QStringList trashedPaths;   ///< those files, relative to the mods folder
    QString warning;            ///< the roll back worked but something minor did not (the snapshot could not be marked)
};

/// Puts the mods folder back to the state recorded in `info` (see the file header and docs/zero/SHELL.md). Refuses a snapshot that is
/// not valid, was rolled back already, or has a damaged copy (nothing is touched then). Idempotent: after a failure it may be called again.
RollbackResult rollback(const Info& info, const QString& modsDir, const Hooks& hooks = {});

/// "just now" | "5 min ago" | "2 h ago" | "3 d ago" | a date.
QString ageText(qint64 createdMs, qint64 nowMs);
/// "Updated 3 mods · 2 h ago"
QString noticeText(const Info& info, qint64 nowMs);

}  // namespace Snapshot
}  // namespace Mods
}  // namespace Zero
