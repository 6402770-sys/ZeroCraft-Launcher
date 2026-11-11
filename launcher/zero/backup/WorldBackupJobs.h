// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - runs Smart Backup operations (backup, restore, delete, list) on QThreadPool::globalInstance(), at most ONE job per
// world, and never while the instance of the world runs. While a job is active its instance is "locked": Application::launch
// refuses to start it (see isInstanceLocked), so the game can never open a world that is being copied or swapped. All state lives
// on the GUI thread; pool jobs only post their result back to the application object.
#pragma once

#include <QHash>
#include <QList>
#include <QObject>
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

    /// True while this instance has a job anywhere (GUI thread only). Checked by Application::launch.
    static bool isInstanceLocked(const QString& instanceId);

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
    /// jobFinished(key, false, reason) right away and returns false: nothing was done.
    bool backup(const Worlds::WorldEntry& entry);
    bool restore(const Worlds::WorldEntry& entry, const QString& zipPath);
    bool remove(const Worlds::WorldEntry& entry, const QString& zipPath);

    /// Lists the backups of `entry` on the pool; the newest request per world wins.
    void refreshList(const Worlds::WorldEntry& entry);

   signals:
    void jobStarted(const QString& key);
    /// `message` is the user-facing result: "Backup created" / "World restored" / "Backup deleted", or the error.
    void jobFinished(const QString& key, bool ok, const QString& message);
    /// The set of backups of this world may have changed (always follows jobFinished of a started job).
    void backupsChanged(const QString& key);
    /// A restore succeeded: the world folder was replaced, rescan it.
    void worldRestored(const QString& key);
    void backupsListed(const QString& key, const QList<Zero::Backup::BackupInfo>& backups);

   private:
    bool refuse(const QString& key, const QString& reason);
    bool begin(const Worlds::WorldEntry& entry, Op op);
    void finish(const QString& key, const QString& instanceId, Op op, bool ok, const QString& message);
    bool ownsBackup(const Worlds::WorldEntry& entry, const QString& zipPath) const;

    QString m_root;
    RunningCheck m_isRunning;
    Hooks m_hooks;
    QHash<QString, Op> m_active;           // key -> running op
    QHash<QString, QString> m_activeInst;  // key -> instance id (for the launch lock)
    QHash<QString, quint64> m_listGen;
    quint64 m_nextGen = 0;
};

}  // namespace Backup
}  // namespace Zero
