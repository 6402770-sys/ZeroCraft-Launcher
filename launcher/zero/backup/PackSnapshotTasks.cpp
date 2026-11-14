// SPDX-License-Identifier: GPL-3.0-only
#include "PackSnapshotTasks.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QMetaObject>
#include <QThreadPool>
#include <atomic>
#include "WorldBackupJobs.h"

namespace Zero {
namespace Backup {

struct PackSnapshotTask::Shared {
    std::atomic<bool> cancel{ false };
    std::atomic<qint64> done{ 0 };
    std::atomic<qint64> total{ 0 };
    std::atomic<bool> progressPending{ false };
};

PackSnapshotTask::PackSnapshotTask(QString instanceRoot, QString snapshotsDir, Pack::Meta meta, Pack::Hooks hooks, BusyCheck busy)
    : Task(false)
    , m_instanceRoot(std::move(instanceRoot))
    , m_snapshotsDir(std::move(snapshotsDir))
    , m_meta(std::move(meta))
    , m_hooks(std::move(hooks))
    , m_busy(std::move(busy))
    , m_shared(std::make_shared<Shared>())
{
    setAbortable(true);
}

PackSnapshotTask::~PackSnapshotTask()
{
    if (m_locked)
        WorldBackupJobs::unlockInstance(m_meta.instanceId);
}

bool PackSnapshotTask::abort()
{
    if (!isRunning())
        return false;
    m_shared->cancel = true;  // the pool job notices, removes its half-written copy and reports "cancelled"
    setStatus(tr("Cancelling…"));
    return true;
}

namespace {
/// Why the task must not start (empty = it can).
QString refusal(const BusyCheck& busy, const QString& instanceId)
{
    if (busy) {
        const QString why = busy();
        if (!why.isEmpty())
            return why;
    }
    if (WorldBackupJobs::isInstanceLocked(instanceId))
        return QCoreApplication::translate("Zero::Backup::PackTasks", "Another backup action for this instance is still running.");
    return {};
}
}  // namespace

void PackSnapshotTask::executeTask()
{
    if (const QString why = refusal(m_busy, m_meta.instanceId); !why.isEmpty()) {
        emitFailed(why);
        return;
    }
    setStatus(tr("Saving a copy of %1 first…").arg(m_meta.packName.isEmpty() ? m_meta.instanceName : m_meta.packName));
    setProgress(0, 100);
    WorldBackupJobs::lockInstance(m_meta.instanceId);
    m_locked = true;

    const std::shared_ptr<Shared> shared = m_shared;
    const QString root = m_instanceRoot;
    const QString dir = m_snapshotsDir;
    const Pack::Meta meta = m_meta;
    const Pack::Hooks hooks = m_hooks;
    QPointer<PackSnapshotTask> self(this);
    QThreadPool::globalInstance()->start([self, shared, root, dir, meta, hooks]() {
        auto* app = QCoreApplication::instance();
        Pack::Control control;
        control.cancelled = [shared]() { return shared->cancel.load(); };
        control.progress = [self, shared, app](qint64 done, qint64 total) {
            shared->done = done;
            shared->total = total;
            if (!app || shared->progressPending.exchange(true))
                return;  // one update in flight is enough
            QMetaObject::invokeMethod(
                app,
                [self, shared]() {
                    shared->progressPending = false;
                    if (self)
                        self->setProgress(shared->done, shared->total);
                },
                Qt::QueuedConnection);
        };
        const Pack::CreateResult result = Pack::createSnapshot(root, dir, meta, hooks, control);
        if (!app)
            return;
        QMetaObject::invokeMethod(
            app,
            [self, result]() {
                if (self)
                    self->finishWith(result);
            },
            Qt::QueuedConnection);
    });
}

void PackSnapshotTask::finishWith(const Pack::CreateResult& result)
{
    if (m_locked) {
        WorldBackupJobs::unlockInstance(m_meta.instanceId);
        m_locked = false;
    }
    if (result.ok) {
        m_zipPath = result.zipPath;
        emitSucceeded();
    } else if (result.cancelled) {
        emitAborted();
    } else {
        emitFailed(result.error);
    }
}

// ---------------------------------------------------------------- roll back

PackRollbackTask::PackRollbackTask(QString instanceRoot, QString zipPath, QString snapshotsDir, QString expectedPackId, Pack::Hooks hooks, BusyCheck busy)
    : Task(false)
    , m_instanceRoot(std::move(instanceRoot))
    , m_zipPath(std::move(zipPath))
    , m_snapshotsDir(std::move(snapshotsDir))
    , m_expectedPackId(std::move(expectedPackId))
    , m_instanceId(QFileInfo(m_instanceRoot).fileName())
    , m_hooks(std::move(hooks))
    , m_busy(std::move(busy))
{
}

PackRollbackTask::~PackRollbackTask()
{
    if (m_locked)
        WorldBackupJobs::unlockInstance(m_instanceId);
}

void PackRollbackTask::executeTask()
{
    if (const QString why = refusal(m_busy, m_instanceId); !why.isEmpty()) {
        m_result.error = why;
        emitFailed(why);
        return;
    }
    setStatus(tr("Rolling back…"));
    setProgress(0, 0);  // no steps to count: a busy bar
    WorldBackupJobs::lockInstance(m_instanceId);
    m_locked = true;

    const QString root = m_instanceRoot;
    const QString zip = m_zipPath;
    const QString dir = m_snapshotsDir;
    const QString packId = m_expectedPackId;
    const Pack::Hooks hooks = m_hooks;
    QPointer<PackRollbackTask> self(this);
    QThreadPool::globalInstance()->start([self, root, zip, dir, packId, hooks]() {
        const Pack::RollbackResult result = Pack::rollback(root, zip, dir, packId, hooks);
        auto* app = QCoreApplication::instance();
        if (!app)
            return;
        QMetaObject::invokeMethod(
            app,
            [self, result]() {
                if (self)
                    self->finishWith(result);
            },
            Qt::QueuedConnection);
    });
}

void PackRollbackTask::finishWith(const Pack::RollbackResult& result)
{
    if (m_locked) {
        WorldBackupJobs::unlockInstance(m_instanceId);
        m_locked = false;
    }
    m_result = result;
    if (result.ok)
        emitSucceeded();
    else
        emitFailed(result.error);
}

}  // namespace Backup
}  // namespace Zero
