// SPDX-License-Identifier: GPL-3.0-only
#include "ModDependencies.h"

#include <algorithm>

#include "Application.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/mod/ModFolderModel.h"
#include "minecraft/mod/ResourceFolderModel.h"
#include "minecraft/mod/tasks/GetModDependenciesTask.h"

namespace Zero::Content {

InstanceDependencyResolver::InstanceDependencyResolver(QObject* parent) : DependencyResolver(parent)
{
    m_loadTimeout.setSingleShot(true);
    m_loadTimeout.setInterval(kLoadTimeoutMs);
    connect(&m_loadTimeout, &QTimer::timeout, this, [this]() {
        if (m_phase == Phase::Loading) {
            finish(m_token, tr("Reading the installed mods took too long."));
        }
    });
    m_runTimeout.setSingleShot(true);
    m_runTimeout.setInterval(kRunTimeoutMs);
    connect(&m_runTimeout, &QTimer::timeout, this, [this]() {
        if (m_phase != Phase::Idle) {
            finish(m_token, tr("The dependency check took too long."));  // the task is aborted, Retry starts a fresh run
        }
    });
}

InstanceDependencyResolver::~InstanceDependencyResolver()
{
    cancel();  // aborts the task and drops the model connection: nothing of this run can reach a destroyed dialog
}

void InstanceDependencyResolver::setTimeouts(int loadMs, int runMs)
{
    m_loadTimeout.setInterval(std::max(1, loadMs));
    m_runTimeout.setInterval(std::max(1, runMs));
}

void InstanceDependencyResolver::cancel()
{
    m_phase = Phase::Idle;
    m_loadTimeout.stop();
    m_runTimeout.stop();
    if (m_loadedConnection) {
        disconnect(m_loadedConnection);
    }
    m_loadedConnection = QMetaObject::Connection();
    if (m_modelGoneConnection) {
        disconnect(m_modelGoneConnection);
    }
    m_modelGoneConnection = QMetaObject::Connection();
    if (m_task) {
        Task::Ptr task = m_task;
        m_task.reset();
        task->disconnect(this);  // no late answer, not even the aborted() of the abort below
        if (task->isRunning()) {
            task->abort();
        }
    }
    m_model = nullptr;
    m_instance = nullptr;
}

void InstanceDependencyResolver::finish(quint64 token, const QString& failure, const QList<PlannedMod>& found)
{
    if (m_phase == Phase::Idle || token != m_token) {
        return;  // a run that was replaced or already answered
    }
    cancel();  // first: the caller may resolve() again from its slot
    if (failure.isEmpty()) {
        emit resolved(token, found);
    } else {
        emit failed(token, failure);
    }
}

void InstanceDependencyResolver::resolve(quint64 token, const Request& request)
{
    cancel();
    m_token = token;
    m_request = request;
    m_phase = Phase::Loading;
    m_runTimeout.start();  // the whole run, model wait and task together
    if (request.all().isEmpty()) {
        finish(token, tr("This mod cannot be checked."));
        return;
    }
    QString error;
    ResourceFolderModel* model = modelFor(request, error);
    if (!model) {
        finish(token, error.isEmpty() ? tr("The instance is no longer available.") : error);
        return;
    }
    m_model = model;
    // The task reads the installed mods in its constructor: wait for a load that started now (update() coalesces with one that is
    // already running and then runs once more, so updateFinished() always follows a load at least as new as this call). The
    // connections exist before the request, so an answer given at once is not missed.
    m_loadedConnection = connect(model, &ResourceFolderModel::updateFinished, this, [this, token]() { onModelLoaded(token); });
    m_modelGoneConnection =
        connect(model, &QObject::destroyed, this, [this, token]() { finish(token, tr("The instance is no longer available.")); });
    m_loadTimeout.start();
    requestLoad(model);
}

void InstanceDependencyResolver::onModelLoaded(quint64 token)
{
    if (m_phase != Phase::Loading || token != m_token) {
        return;
    }
    disconnect(m_loadedConnection);
    m_loadedConnection = QMetaObject::Connection();
    disconnect(m_modelGoneConnection);  // the task keeps its own snapshot of the installed mods
    m_modelGoneConnection = QMetaObject::Connection();
    m_loadTimeout.stop();

    auto* model = static_cast<ResourceFolderModel*>(m_model.data());
    Task::Ptr task = model ? makeTask(m_request, model) : Task::Ptr();
    if (!task) {
        finish(token, tr("The instance is no longer available."));
        return;
    }
    m_phase = Phase::Task;
    m_task = task;
    Task* raw = task.get();

    const auto current = [this, token, raw]() { return m_phase == Phase::Task && token == m_token && raw == m_task.get(); };
    connect(raw, &Task::succeeded, this, [this, token, raw, current]() {
        if (current()) {
            finish(token, {}, dependenciesOf(raw));
        }
    });
    connect(raw, &Task::failed, this, [this, token, current](const QString& reason) {
        if (current()) {
            finish(token, reason.isEmpty() ? tr("The dependency check failed.") : reason);
        }
    });
    connect(raw, &Task::aborted, this, [this, token, current]() {
        if (current()) {
            finish(token, tr("The dependency check was cancelled."));
        }
    });
    // succeeded / failed / aborted come before finished(): only a task that ended without saying how gets here.
    connect(raw, &Task::finished, this, [this, token, current]() {
        if (current()) {
            finish(token, tr("The dependency check ended without an answer."));
        }
    });
    raw->start();  // may answer at once: everything above is connected
}

// ---- production ----

ResourceFolderModel* InstanceDependencyResolver::modelFor(const Request& request, QString& error)
{
    if (!APPLICATION || !APPLICATION->instances()) {
        error = tr("This mod cannot be checked.");
        return nullptr;
    }
    MinecraftInstance* instance = APPLICATION->instances()->getInstanceById(request.instanceId);
    ModFolderModel* model = instance ? instance->loaderModList() : nullptr;
    if (!instance || !model) {
        error = tr("The instance is no longer available.");
        return nullptr;
    }
    m_instance = instance;
    return model;
}

void InstanceDependencyResolver::requestLoad(ResourceFolderModel* model)
{
    model->update();
}

Task::Ptr InstanceDependencyResolver::makeTask(const Request& request, ResourceFolderModel* model)
{
    auto* instance = static_cast<MinecraftInstance*>(m_instance.data());
    auto* mods = qobject_cast<ModFolderModel*>(model);
    if (!instance || !mods) {
        return Task::Ptr();
    }
    // Every chosen mod goes into ONE task: it merges the dependencies, skips the ones that are themselves chosen and lists all the
    // mods that need a dependency (getExtraInfo).
    QList<std::shared_ptr<GetModDependenciesTask::PackDependency>> selected;
    for (const auto& chosen : request.all()) {
        selected.append(std::make_shared<GetModDependenciesTask::PackDependency>(chosen.pack, chosen.version));
    }
    return makeShared<GetModDependenciesTask>(instance, mods, selected);
}

QList<PlannedMod> InstanceDependencyResolver::dependenciesOf(Task* raw)
{
    auto* task = qobject_cast<GetModDependenciesTask*>(raw);
    if (!task) {
        return {};
    }
    const auto extra = task->getExtraInfo();
    QList<PlannedMod> found;
    for (const auto& dep : task->getDependecies()) {
        if (!dep || !dep->pack || dep->version.downloadUrl.trimmed().isEmpty() || dep->version.fileName.trimmed().isEmpty()) {
            continue;  // a lookup that found no downloadable file
        }
        PlannedMod mod;
        mod.pack = dep->pack;
        mod.version = dep->version;
        mod.reason = reasonDependency();
        const auto info = extra.value(dep->pack->addonId.toString());
        mod.requiredBy = info.requiredByNames;
        mod.dependentOn = info.requiredByIds.value(0);  // may be empty: Prism's own dialog would read first() of an empty list
        mod.maybeInstalled = info.maybeInstalled;
        found.append(mod);
    }
    return found;
}

}  // namespace Zero::Content
