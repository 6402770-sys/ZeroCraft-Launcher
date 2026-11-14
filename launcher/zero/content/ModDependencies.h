// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the production dependency check of the mod install review: Prism's GetModDependenciesTask run headless (no
// ProgressDialog, no modal boxes) for one mod version and one instance.
//
// Why it is more than "construct and start": the task snapshots the instance's installed mods in its CONSTRUCTOR, so the mods
// folder model must have finished loading first (otherwise it proposes mods the instance already has). The resolver therefore asks
// the model for an update() and builds the task only once updateFinished() arrives (kLoadTimeoutMs at most). update() while the
// model already loads is fine: Prism schedules one more load and emits updateFinished() after it.
//
// Every run ends: an answer (resolved / failed), or a failure when the model takes longer than kLoadTimeoutMs, when the model goes
// away, when the task ends without an answer, or when the WHOLE run (model wait + network task) takes longer than kRunTimeoutMs. A
// new resolve() or cancel() drops everything of the previous run: the model wait, the timers and the task (aborted, its signals
// disconnected), so a late answer can neither reach the caller nor touch a destroyed dialog (the resolver is a child of the dialog
// and cancels in its destructor). After a failure, resolve() again (Retry) starts a fresh run.
//
// The model lookup, the load request and the task are virtual so tests can drive the run with fakes.
#pragma once

#include <QMetaObject>
#include <QPointer>
#include <QTimer>

#include "tasks/Task.h"
#include "zero/content/ModInstall.h"

class ResourceFolderModel;

namespace Zero::Content {

class InstanceDependencyResolver : public DependencyResolver {
    Q_OBJECT
   public:
    static constexpr int kLoadTimeoutMs = 15000;  ///< the longest the mods folder may take to load
    static constexpr int kRunTimeoutMs = 45000;   ///< the longest a whole run may take (model wait and the network task)

    explicit InstanceDependencyResolver(QObject* parent = nullptr);
    ~InstanceDependencyResolver() override;

    void resolve(quint64 token, const Request& request) override;
    void cancel() override;

    /// Tests: shorter limits than kLoadTimeoutMs / kRunTimeoutMs.
    void setTimeouts(int loadMs, int runMs);
    /// A run is in progress (model wait or task).
    bool isRunning() const { return m_phase != Phase::Idle; }

   protected:
    /// The mods folder model of the request's instance; null with `error` set when there is none. Remembers what makeTask() needs.
    virtual ResourceFolderModel* modelFor(const Request& request, QString& error);
    /// Asks `model` for a fresh load (its updateFinished() lets the run go on).
    virtual void requestLoad(ResourceFolderModel* model);
    /// The dependency task over the loaded `model`; null when it cannot be built.
    virtual Task::Ptr makeTask(const Request& request, ResourceFolderModel* model);
    /// The dependencies found by `task` (a task makeTask() returned) once it succeeded.
    virtual QList<PlannedMod> dependenciesOf(Task* task);

   private:
    enum class Phase { Idle, Loading, Task };

    void onModelLoaded(quint64 token);
    /// Ends the run `token` (when it is still the current one) with resolved(found) or, with a non-empty `failure`, failed(failure).
    void finish(quint64 token, const QString& failure, const QList<PlannedMod>& found = {});

    quint64 m_token = 0;
    Request m_request;
    Phase m_phase = Phase::Idle;
    QPointer<QObject> m_instance;  ///< a MinecraftInstance (QPointer needs no complete type this way)
    QPointer<QObject> m_model;     ///< its mods folder model (a ResourceFolderModel)
    QMetaObject::Connection m_loadedConnection;
    QMetaObject::Connection m_modelGoneConnection;
    QTimer m_loadTimeout;
    QTimer m_runTimeout;
    Task::Ptr m_task;
};

}  // namespace Zero::Content
