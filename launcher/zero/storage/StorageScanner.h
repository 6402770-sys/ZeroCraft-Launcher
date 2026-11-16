// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Storage Manager core (no widgets, no APPLICATION, no Prism instance classes). Measures what the launcher and its
// instances take on disk, finds disposable files (old logs / crash reports) and moves them to the Recycle Bin on request.
// Rules (docs/zero/ARCHITECTURE.md, "Storage Manager"):
//  - measuring never writes; `trashCandidates` is the ONLY writer and only ever moves files to the Recycle Bin: when that fails the
//    file stays where it is (there is no permanent-delete fallback);
//  - nothing is offered or trashed unless it is a regular, non-link file directly inside `logs` / `crash-reports` of a known instance
//    that is not running, has the expected name and is older than 30 days; the same predicates re-validate every file at trash time;
//  - the quick clean-up above never touches user content; "Largest items" (worlds, mods, packs, screenshots, big files...) may be
//    moved to the Recycle Bin one by one, only on an explicit request (`trashLargeItems`: same rules, never a running instance, never
//    ZERO's own backups / snapshots / data);
//  - every byte is counted once: a configured directory that is nested in another one is subtracted from its parent.
#pragma once

#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <array>
#include <functional>

class QFileInfo;

namespace Zero {
namespace Storage {

/// Files older than this many days are cleanup candidates.
inline constexpr int kCleanupAgeDays = 30;
inline constexpr qint64 kCleanupAgeMs = qint64(kCleanupAgeDays) * 24 * 60 * 60 * 1000;

enum class Category { Worlds, Mods, Packs, Screenshots, Logs, OtherInstance, Backups, Libraries, Java, Cache, OtherLauncher };
inline constexpr int kCategoryCount = 11;

/// Stable ids: "worlds" "mods" "packs" "screenshots" "logs" "other-instance" "backups" "libraries" "java" "cache" "other-launcher".
QString categoryId(Category category);
/// All categories in declaration order.
const std::array<Category, kCategoryCount>& allCategories();

enum class CleanupKind { OldLogs, OldCrashReports };
QString cleanupKindId(CleanupKind kind);  ///< "old-logs" | "old-crash-reports"

/// One instance, as plain paths (the UI fills it from the instance list; nothing here includes Prism's instance classes).
struct InstanceSource {
    QString id;
    QString name;
    QString instanceRoot;
    QString gameRoot;
    QString worldsDir;
    QString modsDir;
    QStringList packDirs;  ///< resource / texture / shader packs
    bool running = false;
};

struct LauncherSource {
    QString dataRoot;
    QString instancesDir;  ///< may lie inside or outside dataRoot
};

/// Bytes per category (index = static_cast<int>(Category)).
struct CategoryBytes {
    std::array<qint64, kCategoryCount> bytes{};
    qint64 of(Category c) const { return bytes[static_cast<size_t>(c)]; }
    qint64& at(Category c) { return bytes[static_cast<size_t>(c)]; }
    qint64 sum() const;
};

struct InstanceRow {
    QString id;
    QString name;
    QString instanceRoot;
    qint64 total = 0;  ///< sum of `bytes` (instance categories only)
    CategoryBytes bytes;
};

struct CleanupFile {
    CleanupKind kind = CleanupKind::OldLogs;
    QString path;
    qint64 sizeBytes = 0;
    qint64 modifiedMs = 0;
    QString instanceId;
};

struct CleanupGroup {
    CleanupKind kind = CleanupKind::OldLogs;
    QList<CleanupFile> files;  ///< biggest first
    qint64 totalBytes = 0;
};

/// A `.zero-restore-*` / `.zero-replaced-*` folder left in a worlds folder by an interrupted Smart Backup restore. Information
/// only: ZERO never offers to delete it (a `.zero-replaced-*` can be the only copy of a world).
struct RestoreLeftover {
    QString path;
    QString instanceId;
    QString instanceName;
    qint64 sizeBytes = 0;
};

// ---- Largest items -------------------------------------------------------------------------------------------------------------

/// Files bigger than this are listed on their own wherever they lie in the game root.
inline constexpr qint64 kBigFileBytes = qint64(100) * 1024 * 1024;
/// "Largest items" shows this many rows.
inline constexpr int kLargestLimit = 20;

/// What a "largest item" is. WorldBackups / Snapshots / ZeroData are owned by ZERO: they are listed but never trashed from here.
enum class ItemKind { World, Mod, Pack, Logs, CrashReports, Screenshots, WorldBackups, Snapshots, ZeroData, Folder, BigFile };
QString itemKindId(ItemKind kind);
/// World, Mod, Pack, Logs, CrashReports, Screenshots, Folder and BigFile can be moved to the Recycle Bin; the ZERO-owned kinds cannot.
bool isTrashableKind(ItemKind kind);

struct LargeItem {
    ItemKind kind = ItemKind::Folder;
    QString path;  ///< absolute, '/' separators
    QString name;  ///< file / folder name
    QString instanceId;
    QString instanceName;
    qint64 sizeBytes = 0;
    bool isDir = false;
    bool running = false;  ///< the instance was running when the report was built
};

struct StorageReport {
    qint64 generatedMs = 0;   ///< the `nowMs` the report was built with
    QString dataRoot;
    CategoryBytes totals;     ///< over all instances + the launcher
    qint64 totalBytes = 0;    ///< sum of `totals`
    QList<InstanceRow> instances;     ///< biggest first
    QList<CleanupGroup> cleanupGroups;  ///< always both kinds, in CleanupKind order (a group can be empty)
    QList<RestoreLeftover> leftovers;
    QList<LargeItem> largest;  ///< the biggest items of every instance (kLargestLimit each), biggest first; see aggregateLargest()
    qint64 volumeFreeBytes = -1;   ///< volume of dataRoot; -1 = unknown
    qint64 volumeTotalBytes = -1;  ///< -1 = unknown

    const CleanupGroup* group(CleanupKind kind) const;
};

// ---- Pure helpers (shared by detection and trash-time validation: single source of truth) ----------------------------------

/// `<gameRoot>/logs` for OldLogs, `<gameRoot>/crash-reports` for OldCrashReports; empty when gameRoot is empty.
QString cleanupDir(CleanupKind kind, const QString& gameRoot);

/// OldLogs: ends with .log or .log.gz (case-insensitive), not latest.log / debug.log. OldCrashReports: ends with .txt.
bool matchesCleanupName(CleanupKind kind, const QString& fileName);

/// True when `modifiedMs` is strictly older than kCleanupAgeDays before `nowMs`.
bool isOlderThanCleanupAge(qint64 modifiedMs, qint64 nowMs);

/// Regular file that is not a symbolic link, junction or shortcut.
bool isPlainFile(const QFileInfo& info);

/// The one predicate for "may be offered / trashed": plain file + name pattern + age. `why` (optional) receives the reason when false.
bool isCleanupCandidate(CleanupKind kind, const QFileInfo& info, qint64 nowMs, QString* why = nullptr);

/// Case-insensitive, normalized absolute path (comparison key only, never use it as a real path).
QString pathKey(const QString& path);

/// Old log files / crash reports of one instance (non-recursive). Empty when the instance is running or the folder is missing or a link.
QList<CleanupFile> findCleanupFiles(const InstanceSource& source, CleanupKind kind, qint64 nowMs);

/// `.zero-restore-*` / `.zero-replaced-*` folders directly in the worlds folder (links skipped), sizes included.
QList<RestoreLeftover> findRestoreLeftovers(const InstanceSource& source);

/// Candidates for "Largest items" of one instance, biggest first, at most `limit`: each world folder, each mod / pack entry, `logs`,
/// `crash-reports`, `screenshots`, the other top-level folders of the game root, the instance's `.zero` folder and single files
/// over `bigFileBytes` anywhere in the game root (worlds, mods and packs excluded: they are listed as a whole). Links are skipped,
/// every path appears once. Read-only.
QList<LargeItem> findLargeItems(const InstanceSource& source, int limit = kLargestLimit, qint64 bigFileBytes = kBigFileBytes);

/// ZERO's own copies for one instance: every `<dataRoot>/zero/backups/<id>/<world>` and `<dataRoot>/zero/snapshots/<id>`.
QList<LargeItem> findZeroBackupItems(const InstanceSource& source, const QString& dataRoot);

/// The `limit` biggest of `all` (ties: path), restricted to `instanceId` unless it is empty.
QList<LargeItem> aggregateLargest(const QList<LargeItem>& all, const QString& instanceId, int limit = kLargestLimit);

/// The one predicate for "may this item go to the Recycle Bin now": a trashable kind, a known instance that is not running, the
/// path where that kind lives (a child of the worlds / mods / pack folder, the logs / crash-reports / screenshots folder itself, a
/// child of the game root, or a big file inside the game root outside worlds / mods / packs), not a link, and it exists.
bool isTrashableItem(const LargeItem& item, const InstanceSource& source, QString* why = nullptr);

/// Instance breakdown. Every byte once; dirs that do not exist count 0; the same path configured twice counts once.
InstanceRow measureInstance(const InstanceSource& source);

/// Launcher-side categories (Worlds..OtherInstance stay 0).
CategoryBytes measureLauncher(const LauncherSource& launcher);

/// Called before each instance (and the launcher) is measured: `done` of `total` steps finished, `current` = the name about to be measured.
using ProgressFn = std::function<void(int done, int total, const QString& current)>;

/// Does all the disk work synchronously (call it from a pool thread).
StorageReport buildReport(const QList<InstanceSource>& instances, const LauncherSource& launcher, qint64 nowMs, const ProgressFn& progress = {});

// ---- Trash ---------------------------------------------------------------------------------------------------------------

/// Test seam. Default `trashPath` = FS::trash (Recycle Bin). There is deliberately no delete hook and no fallback.
struct TrashHooks {
    std::function<bool(const QString& path)> trashPath;
};

struct TrashProblem {
    QString path;
    QString error;
    bool skipped = false;  ///< true = refused by validation (never attempted); false = the move to the Recycle Bin failed
};

struct TrashResult {
    int trashedCount = 0;
    qint64 trashedBytes = 0;
    int skippedCount = 0;
    int failedCount = 0;
    QList<TrashProblem> problems;
    QString error;  ///< set when the whole request was refused (e.g. another trash job is running)
};

/// The only writer. Re-validates EVERY file now (same predicates as detection): regular non-link file, parent exactly the logs /
/// crash-reports folder of a known non-running instance, name still matching, still older than 30 days. Valid files go to the
/// Recycle Bin; a failed move leaves the file in place and is reported. Never deletes permanently.
TrashResult trashCandidates(const QList<CleanupFile>& files, const QList<InstanceSource>& sources, qint64 nowMs, const TrashHooks& hooks = {});

/// Moves large items to the Recycle Bin one by one. Re-validates EVERY item now with isTrashableItem(); a failed move leaves the item
/// in place and is reported. Never deletes permanently.
TrashResult trashLargeItems(const QList<LargeItem>& items, const QList<InstanceSource>& sources, const TrashHooks& hooks = {});

// ---- Async wrapper -------------------------------------------------------------------------------------------------------

/// rescan() reads the sources on the GUI thread, builds the report on QThreadPool::globalInstance() and delivers it back here;
/// results of an older generation are dropped. trash() does the same for trashCandidates (one job at a time).
class StorageScanner : public QObject {
    Q_OBJECT
   public:
    /// Called on the GUI thread by rescan() and by trash() (so the `running` flags are fresh).
    using SourceProvider = std::function<QPair<QList<InstanceSource>, LauncherSource>()>;

    explicit StorageScanner(SourceProvider provider, QObject* parent = nullptr, TrashHooks hooks = {});

    void rescan();
    /// Starts a trash job. While another one runs the request is refused: trashFinished() carries a result with `error` set.
    void trash(const QList<CleanupFile>& files);
    /// Same for "Largest items" (trashLargeItems); shares the one-job-at-a-time rule and trashFinished().
    void trashItems(const QList<LargeItem>& items);

    const StorageReport& report() const { return m_report; }
    bool hasReport() const { return m_hasReport; }
    bool isScanning() const { return m_scanning; }
    bool isTrashing() const { return m_trashing; }
    quint64 generation() const { return m_generation; }

   signals:
    void scanStarted();
    /// While scanning: `done` of `total` steps (instances + the launcher) finished, `current` = what is measured now. GUI thread.
    void scanProgress(int done, int total, const QString& current);
    void reportReady(const Zero::Storage::StorageReport& report);
    void trashFinished(const Zero::Storage::TrashResult& result);

   private:
    void onReport(quint64 generation, const StorageReport& report);
    void onTrashed(const TrashResult& result);
    void startTrash(std::function<TrashResult(const QList<InstanceSource>&, const TrashHooks&)> job);

    SourceProvider m_provider;
    TrashHooks m_hooks;
    StorageReport m_report;
    quint64 m_generation = 0;
    bool m_scanning = false;
    bool m_trashing = false;
    bool m_hasReport = false;
};

}  // namespace Storage
}  // namespace Zero

Q_DECLARE_METATYPE(Zero::Storage::StorageReport)
Q_DECLARE_METATYPE(Zero::Storage::TrashResult)
