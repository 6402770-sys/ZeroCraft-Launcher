// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the two long jobs of PackSnapshot as Tasks, so the window can show them in Prism's modal ProgressDialog: the snapshot taken
// before a modpack update (cancellable: cancelling stops the update too) and the roll back. Both run their work on QThreadPool::globalInstance()
// (never on the GUI thread) and keep the instance "locked" while they run: Application::launch refuses to start a locked instance, the same
// mechanism as the world backup jobs (WorldBackupJobs::lockInstance). All task state lives on the GUI thread; the pool job only posts its result
// back to the application object.
#pragma once

#include <QPointer>
#include <functional>
#include <memory>
#include "PackSnapshot.h"
#include "tasks/Task.h"

namespace Zero {
namespace Backup {

/// Asked on the GUI thread when a task starts: a non-empty text refuses it with that text ("Close the game first.": the instance runs).
/// A task also refuses on its own while another backup job holds the instance.
using BusyCheck = std::function<QString()>;

class PackSnapshotTask : public Task {
    Q_OBJECT
   public:
    PackSnapshotTask(QString instanceRoot, QString snapshotsDir, Pack::Meta meta, Pack::Hooks hooks = {}, BusyCheck busy = {});
    ~PackSnapshotTask() override;

    /// The snapshot that was made (empty until the task succeeded).
    QString zipPath() const { return m_zipPath; }
    bool abort() override;

   protected:
    void executeTask() override;

   private:
    struct Shared;
    void finishWith(const Pack::CreateResult& result);

    QString m_instanceRoot;
    QString m_snapshotsDir;
    Pack::Meta m_meta;
    Pack::Hooks m_hooks;
    BusyCheck m_busy;
    QString m_zipPath;
    std::shared_ptr<Shared> m_shared;
    bool m_locked = false;
};

class PackRollbackTask : public Task {
    Q_OBJECT
   public:
    PackRollbackTask(QString instanceRoot, QString zipPath, QString snapshotsDir, QString expectedPackId, Pack::Hooks hooks = {},
                     BusyCheck busy = {});
    ~PackRollbackTask() override;

    const Pack::RollbackResult& result() const { return m_result; }

   protected:
    void executeTask() override;

   private:
    void finishWith(const Pack::RollbackResult& result);

    QString m_instanceRoot;
    QString m_zipPath;
    QString m_snapshotsDir;
    QString m_expectedPackId;
    QString m_instanceId;
    Pack::Hooks m_hooks;
    BusyCheck m_busy;
    Pack::RollbackResult m_result;
    bool m_locked = false;
};

}  // namespace Backup
}  // namespace Zero
