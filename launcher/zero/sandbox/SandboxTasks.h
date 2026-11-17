// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - "Try it in a copy": the long jobs as Tasks, so the window can show them in Prism's modal ProgressDialog. Both run their work on
// QThreadPool::globalInstance() (never on the GUI thread) and keep the instances "locked" while they run (Application::launch refuses a locked
// instance, the same mechanism as the world backup jobs). All task state lives on the GUI thread; the pool job only posts its result back to
// the application object.
#pragma once

#include <QList>
#include <QPointer>
#include <functional>

#include "SandboxApply.h"
#include "SandboxPlan.h"
#include "tasks/Task.h"
#include "zero/backup/PackSnapshotTasks.h"  // BusyCheck

namespace Zero {
namespace Sandbox {

/// Compares a test copy with its original off the GUI thread. The result is read after the task finished.
class CompareTask : public Task {
    Q_OBJECT
   public:
    CompareTask(Layout original, Layout copy);
    ~CompareTask() override;

    const QList<PartChange>& plan() const { return m_plan; }
    bool abort() override;

   protected:
    void executeTask() override;

   private:
    struct Shared;
    Layout m_original;
    Layout m_copy;
    QList<PartChange> m_plan;
    std::shared_ptr<Shared> m_shared;
};

/// apply() of SandboxApply. Refuses (like the snapshot tasks) while either instance is busy.
class BringBackTask : public Task {
    Q_OBJECT
   public:
    /// `originalId` / `copyId` are the instance ids (their folder names); `busy` is asked on the GUI thread when the task starts.
    BringBackTask(QString originalId, QString copyId, Request request, Hooks hooks = {}, Backup::BusyCheck busy = {});
    ~BringBackTask() override;

    const Result& result() const { return m_result; }

   protected:
    void executeTask() override;

   private:
    void finishWith(const Result& result);
    void unlock();

    QString m_originalId;
    QString m_copyId;
    Request m_request;
    Hooks m_hooks;
    Backup::BusyCheck m_busy;
    Result m_result;
    bool m_locked = false;
};

}  // namespace Sandbox
}  // namespace Zero
