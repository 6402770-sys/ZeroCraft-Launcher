// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Smart Backup for worlds (service layer, no widgets, no APPLICATION). This is the first ZERO code that WRITES
// inside user worlds, so every function follows the data-safety rules of docs/zero/ARCHITECTURE.md ("Smart Backup"):
//  - a backup is written to `<name>.zip.part`, verified against its manifest and only then renamed to `.zip`;
//  - a restore verifies the backup, saves the current world as a verified safety backup, extracts into a sibling staging folder,
//    verifies the staged files and only then swaps folders with renames (the old world is kept until the swap succeeded);
//  - every archive path is validated (zip-slip) before anything is written;
//  - nothing is ever deleted that ZERO did not create itself.
// All functions are synchronous: call them from a pool thread (see WorldBackupJobs).
#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>

namespace Zero {
namespace Backup {

enum class Kind { Manual, Safety, Auto };

QString kindToString(Kind kind);  ///< "manual" | "safety" | "auto"
Kind kindFromString(const QString& text, bool* ok = nullptr);

constexpr int kManifestVersion = 1;
constexpr int kKeepManual = 10;
constexpr int kKeepSafety = 3;
constexpr int kKeepAuto = 5;
inline constexpr char kManifestName[] = "zero-backup.json";
inline constexpr char kWorldPrefix[] = "world/";

/// What a backup records about its world (stored in the manifest).
struct BackupMeta {
    QString instanceId;
    QString instanceName;
    QString worldFolder;  ///< folder name inside saves/
    QString worldName;    ///< level.dat LevelName
};

/// Seams for tests; every member left empty uses the real implementation.
struct Hooks {
    /// Directory / file rename; must fail when the destination exists. Default: QDir::rename.
    std::function<bool(const QString& from, const QString& to)> rename;
    /// Free bytes on the volume of a path (-1 = unknown, no check). Default: QStorageInfo.
    std::function<qint64(const QString& path)> freeBytes;
    /// Moves a path to the Recycle Bin. Default: FS::trash, then FS::deletePath.
    std::function<bool(const QString& path)> trashPath;
};

/// Extras of createBackup() for callers that back up something that is not a world (the instance snapshot of a modpack update, see
/// PackSnapshot). Every member left empty keeps the world behavior.
struct Options {
    /// True leaves a file or a whole folder out. `rel` is relative to the backed-up folder, '/'-separated.
    std::function<bool(const QString& rel, bool isDir)> exclude;
    /// File name of the zip inside `backupDir` instead of the generated `<stamp>-<kind>.zip`. The call fails when it already exists.
    QString fileName;
    /// Text of the failure when a file has a name that cannot be backed up safely (`rel` = its path); empty = the world wording.
    std::function<QString(const QString& rel)> unsafeName;
    /// Stored in the manifest under "extra" (read back with readBackupInfo).
    QJsonObject extra;
    /// Polled between files; true stops the backup (nothing is left behind, BackupResult::cancelled is set).
    std::function<bool()> cancelled;
    /// Called from the calling thread after every file of each pass with (done, total) over three passes of the data: hashing, writing
    /// and (once) the verification. Keep it cheap.
    std::function<void(qint64 done, qint64 total)> progress;
};

struct BackupResult {
    bool ok = false;
    bool cancelled = false;
    QString error;
    QString zipPath;
};

struct VerifyResult {
    bool ok = false;
    QString error;
};

struct RestoreResult {
    bool ok = false;
    QString error;
    QString safetyZip;  ///< the safety backup of the replaced world (empty when the world did not exist)
};

struct BackupInfo {
    QString zipPath;
    Kind kind = Kind::Manual;
    qint64 createdMs = 0;
    QString worldName;
    qint64 totalBytes = 0;  ///< world bytes recorded in the manifest
    qint64 fileSize = 0;    ///< size of the zip on disk
    bool valid = false;     ///< the manifest could be read. Not a full verification: restoreBackup() verifies everything.
};

/// `<dataRoot>/zero/backups`
QString backupsRoot(const QString& dataRoot);
/// `<dataRoot>/zero/backups/<instanceId>/<worldFolder>`
QString backupDirFor(const QString& dataRoot, const QString& instanceId, const QString& worldFolder);

/// True for a relative, '/'-separated path that is safe to create below a folder: no empty / "." / ".." segments, no leading or
/// trailing '/', no backslash, no ':' (drive letters, NTFS streams), no control characters or <>"|?*, no Windows device names
/// (CON, NUL, COM1...) and no segment ending in '.' or ' '.
bool isSafeRelativePath(const QString& path);

/// Backs up `worldFolderPath` into `backupDir` (created when missing). Links are skipped, `session.lock` is excluded. See file header.
BackupResult createBackup(const QString& worldFolderPath, const QString& backupDir, Kind kind, const BackupMeta& meta, const Hooks& hooks = {},
                          const Options& options = {});

/// What the manifest of a backup says (without reading the files). `ok` is false when the zip or its manifest cannot be read.
struct ManifestInfo {
    bool ok = false;
    QString error;
    qint64 totalBytes = 0;
    qint64 createdMs = 0;
    BackupMeta meta;
    QJsonObject extra;
};
ManifestInfo readBackupInfo(const QString& zipPath);

/// Verifies `zipPath` (like verifyBackup), writes every entry below `stagingFolder` (created when missing, paths validated again) and
/// re-reads the written files against the manifest. On a failure the folder is left as it is: it belongs to the caller.
VerifyResult extractBackup(const QString& zipPath, const QString& stagingFolder);

/// QDir::rename with a few retries (an indexer or antivirus can hold a handle on a file for a moment). Fails when `to` exists.
bool renamePath(const QString& from, const QString& to);

/// Newest first. Reads only the manifest of every `*.zip`; `.part` files are ignored; an unreadable zip has valid = false.
QList<BackupInfo> listBackups(const QString& backupDir);

/// Reads every entry and compares it with the manifest (size + SHA-256). Rejects unsafe paths, duplicates and unknown entries.
VerifyResult verifyBackup(const QString& zipPath);

/// Replaces `worldFolderPath` with the backup. Stops at the first failure; until the final swap the original world is untouched.
/// Order: verify the zip, safety backup of the current world, extract to `<saves>/.zero-restore-<folder>-<ts>`, verify the staged
/// files, rename the world to `<saves>/.zero-replaced-<folder>-<ts>`, rename the staging folder to the world, trash the old one.
RestoreResult restoreBackup(const QString& zipPath, const QString& worldFolderPath, const QString& backupDir, const BackupMeta& meta,
                            const Hooks& hooks = {});

/// Keeps the `keepManual` newest `*-manual.zip`, `keepSafety` newest `*-safety.zip` and `keepAuto` newest `*-auto.zip` (by the
/// timestamp in the name; each kind independently) and removes the older ones. Never removes a path in `protectedZips`, never touches files whose name does not match ZERO's pattern.
/// Returns how many files were removed.
int applyRetention(const QString& backupDir, const QStringList& protectedZips = {}, int keepManual = kKeepManual, int keepSafety = kKeepSafety,
                   int keepAuto = kKeepAuto);

/// Moves one backup to the Recycle Bin (fallback: removes it). Refuses anything that is not a `.zip` file inside `backupsRootDir`.
bool deleteBackup(const QString& zipPath, const QString& backupsRootDir, QString* error = nullptr, const Hooks& hooks = {});

}  // namespace Backup
}  // namespace Zero
