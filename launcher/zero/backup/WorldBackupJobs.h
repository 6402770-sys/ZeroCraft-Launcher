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
#include <functional>
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

    /// `backupsRootDir` = `<dataRoot>/zero/backups`.
    WorldBackupJobs(QString backupsRootDir, RunningCheck isRunning, QObject* parent = nullptr);
    ~WorldBackupJobs() override;

    /// Test seam: replaces the hooks used by the jobs (set before starting them).
    void setHooks(const Hooks& hooks) { m_hooks = hooks; }

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
    void backupsListed(const QString& key, const QList<Zero::Backup::BackupInfo>& backups);

   private:
    bool refuse(const QString& key, const QString& reason, bool automatic = false);
    bool begin(const Worlds::WorldEntry& entry, Op op, bool automatic = false);
    void finish(const QString& key, const QString& instanceId, Op op, bool ok, const QString& message);
    bool ownsBackup(const Worlds::WorldEntry& entry, const QString& zipPath) const;

    QString m_root;
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
