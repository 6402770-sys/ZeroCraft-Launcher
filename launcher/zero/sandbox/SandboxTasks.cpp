// SPDX-License-Identifier: GPL-3.0-only
#include "SandboxTasks.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QThreadPool>
#include <atomic>
#include <memory>

#include "zero/backup/WorldBackupJobs.h"

namespace Zero {
namespace Sandbox {

// ---------------------------------------------------------------- compare

struct CompareTask::Shared {
    std::atomic<bool> cancel{ false };
};

CompareTask::CompareTask(Layout original, Layout copy)
    : Task(false), m_original(std::move(original)), m_copy(std::move(copy)), m_shared(std::make_shared<Shared>())
{
    setAbortable(true);
}

CompareTask::~CompareTask() = default;

bool CompareTask::abort()
{
    if (!isRunning())
        return false;
    m_shared->cancel = true;
    return true;
}

void CompareTask::executeTask()
{
    setStatus(tr("Comparing the test copy with the original…"));
    setProgress(0, 0);
    const std::shared_ptr<Shared> shared = m_shared;
    const Layout original = m_original;
    const Layout copy = m_copy;
    QPointer<CompareTask> self(this);
    QThreadPool::globalInstance()->start([self, shared, original, copy]() {
        CompareOptions options;
        options.cancelled = [shared]() { return shared->cancel.load(); };
        const QList<PartChange> plan = comparePlan(original, copy, options);
        const bool cancelled = shared->cancel.load();
        auto* app = QCoreApplication::instance();
        if (!app)
            return;
        QMetaObject::invokeMethod(
            app,
            [self, plan, cancelled]() {
                if (!self)
                    return;
                if (cancelled) {
                    self->emitAborted();
                    return;
                }
                self->m_plan = plan;
                self->emitSucceeded();
            },
            Qt::QueuedConnection);
    });
}

// ---------------------------------------------------------------- bring back

namespace {
QString refusal(const Backup::BusyCheck& busy, const QStringList& ids)
{
    if (busy) {
        const QString why = busy();
        if (!why.isEmpty())
            return why;
    }
    for (const QString& id : ids) {
        if (Backup::WorldBackupJobs::isInstanceLocked(id))
            return QCoreApplication::translate("Zero::Sandbox", "Another backup action for this instance is still running.");
    }
    return {};
}
}  // namespace

BringBackTask::BringBackTask(QString originalId, QString copyId, Request request, Hooks hooks, Backup::BusyCheck busy)
    : Task(false)
    , m_originalId(std::move(originalId))
    , m_copyId(std::move(copyId))
    , m_request(std::move(request))
    , m_hooks(std::move(hooks))
    , m_busy(std::move(busy))
{
}

BringBackTask::~BringBackTask()
{
    unlock();
}

void BringBackTask::unlock()
{
    if (!m_locked)
        return;
    Backup::WorldBackupJobs::unlockInstance(m_originalId);
    Backup::WorldBackupJobs::unlockInstance(m_copyId);
    m_locked = false;
}

void BringBackTask::executeTask()
{
    if (const QString why = refusal(m_busy, { m_originalId, m_copyId }); !why.isEmpty()) {
        m_result.error = why;
        emitFailed(why);
        return;
    }
    setStatus(tr("Bringing the changes back…"));
    setProgress(0, 0);  // no steps to count: a busy bar
    Backup::WorldBackupJobs::lockInstance(m_originalId);
    Backup::WorldBackupJobs::lockInstance(m_copyId);
    m_locked = true;

    const Request request = m_request;
    const Hooks hooks = m_hooks;
    QPointer<BringBackTask> self(this);
    QThreadPool::globalInstance()->start([self, request, hooks]() {
        const Result result = apply(request, hooks);
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

void BringBackTask::finishWith(const Result& result)
{
    unlock();
    m_result = result;
    if (result.ok)
        emitSucceeded();
    else
        emitFailed(result.error);
}

}  // namespace Sandbox
}  // namespace Zero
