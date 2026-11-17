// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - runs Smart Backup operations (backup, restore, delete, list) on QThreadPool::globalInstance(), at most ONE job per
// world, and never while the instance of the world runs. While a job is active its instance is "locked": Application::launch
// refuses to start it (see isInstanceLocked), so the game can never open a world that is being copied or swapped. All state lives
// on the GUI thread; pool jobs only post their result back to the application object.
// The object is an app-level service created by Application (see global()); the Worlds page and the AutoBackup service share it.
#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>
#include "BackupDestination.h"
#include "WorldBackup.h"
#include "zero/worlds/WorldScanner.h"

namespace Zero {
namespace Backup {

class WorldBackupJobs : public QObject {
    Q_OBJECT
   public:
    enum class Op { None, Backup, Restore, Remove };

    /// True while this instance has a job anywhere (or an explicit lock, see lockInstance) (GUI thread only). Checked by Application::launch.
    static bool isInstanceLocked(const QString& instanceId);
    /// Counted launch locks held by other services (AutoBackup keeps an instance locked while its queue is pending). Every
    /// lockInstance() needs one unlockInstance(); GUI thread only.
    static void lockInstance(const QString& instanceId);
    static void unlockInstance(const QString& instanceId);

    /// The process-wide instance created by the application (nullptr before that). The first created instance is global.
    static WorldBackupJobs* global();

    /// Called on the GUI thread at request time: true when the instance runs (or is being launched).
    using RunningCheck = std::function<bool(const QString& instanceId)>;

    /// Where new backups go. Every member is read on the GUI thread, at request time (never cached), so a changed setting applies to
    /// the next backup. All empty = always the default root.
    struct Destination {
        std::function<QString()> chosenFolder;          ///< the folder the user picked (`ZeroBackupRoot`); empty = default root
        std::function<QStringList()> knownRoots;        ///< roots used before (besides the default one): their backups stay listed
        std::function<void(const QString&)> rootUsed;   ///< a root received a backup (or was chosen): remember it
    };

    /// `backupsRootDir` = `<dataRoot>/zero/backups` (the default root).
    WorldBackupJobs(QString backupsRootDir, RunningCheck isRunning, QObject* parent = nullptr);
    ~WorldBackupJobs() override;

    /// Test seam: replaces the hooks used by the jobs (set before starting them).
    void setHooks(const Hooks& hooks) { m_hooks = hooks; }
    void setDestination(Destination destination) { m_dest = std::move(destination); }

    /// The default root.
    QString defaultRoot() const { return m_root; }
    /// Where the next backup goes when the chosen folder works (no disk access): `<chosen>/ZERO Backups`, or the default root.
    QString activeRoot() const;
    /// The default root, every known root and the active one (cleaned, no duplicates, default first).
    QStringList allRoots() const;
    /// The backup folder of `entry` in every root (same order as allRoots()).
    QStringList backupDirs(const Worlds::WorldEntry& entry) const;
    /// Remembers a root (e.g. the one just applied in Settings) through Destination::rootUsed.
    void rememberRoot(const QString& root);

    /// The backup folder of `entry` to show ("Open backups folder"): the active destination's, or, when it does not exist, the first
    /// other root's that does (the active one when none exists).
    QString backupDir(const Worlds::WorldEntry& entry) const;
    bool isBusy(const QString& key) const { return m_active.contains(key); }
    Op operation(const QString& key) const { return m_active.value(key, Op::None); }

    /// Each request returns true when a job started. A refusal (instance running, world already busy, bad path) emits
    /// jobFinished(key, false, reason, automatic) right away and returns false: nothing was done.
    /// `kind` is Manual (the user's button) or Auto (AutoBackup); Safety backups are only made by restore().
    bool backup(const Worlds::WorldEntry& entry, Kind kind = Kind::Manual);
    bool restore(const Worlds::WorldEntry& entry, const QString& zipPath);
    bool remove(const Worlds::WorldEntry& entry, const QString& zipPath);

    /// Lists the backups of `entry` on the pool; the newest request per world wins.
    void refreshList(const Worlds::WorldEntry& entry);

   signals:
    void jobStarted(const QString& key);
    /// `message` is the user-facing result: "Backup created" / "World restored" / "Backup deleted", or the error.
    /// `automatic` is true for jobs requested with Kind::Auto (their results are summarised by AutoBackup, not toasted per world).
    void jobFinished(const QString& key, bool ok, const QString& message, bool automatic = false);
    /// The set of backups of this world may have changed (always follows jobFinished of a started job).
    void backupsChanged(const QString& key);
    /// A restore succeeded: the world folder was replaced, rescan it.
    void worldRestored(const QString& key);
    /// The chosen backup folder was not usable, so the backup (or the safety backup of a restore) of this world went to the default
    /// root. Emitted right before jobFinished of that job; `chosenFolder` is the folder that failed.
    void fellBackToDefault(const QString& key, const QString& chosenFolder);
    void backupsListed(const QString& key, const QList<Zero::Backup::BackupInfo>& backups);

   private:
    bool refuse(const QString& key, const QString& reason, bool automatic = false);
    bool begin(const Worlds::WorldEntry& entry, Op op, bool automatic = false);
    struct Outcome {
        bool ok = false;
        QString message;      ///< user-facing result (the error when !ok)
        QString usedRoot;     ///< root the backup was written into (empty for a delete)
        QString fellBackFrom; ///< the chosen folder that was not usable (empty when it worked or none is chosen)
    };
    void finish(const QString& key, const QString& instanceId, Op op, const Outcome& outcome);
    /// The known root that holds `zipPath` as a backup of `entry` (empty when none does).
    QString rootOwning(const Worlds::WorldEntry& entry, const QString& zipPath) const;
    QString chosenFolder() const { return m_dest.chosenFolder ? m_dest.chosenFolder() : QString(); }

    QString m_root;
    Destination m_dest;
    RunningCheck m_isRunning;
    Hooks m_hooks;
    QHash<QString, Op> m_active;           // key -> running op
    QHash<QString, QString> m_activeInst;  // key -> instance id (for the launch lock)
    QSet<QString> m_activeAuto;            // keys whose running job was requested as Kind::Auto
    QHash<QString, quint64> m_listGen;
    quint64 m_nextGen = 0;
};

}  // namespace Backup
}  // namespace Zero
