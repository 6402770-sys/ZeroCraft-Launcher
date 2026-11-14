// SPDX-License-Identifier: GPL-3.0-only
#include "EnsureAuthlibInjector.h"

#include "Application.h"

using namespace Zero::Auth::AuthlibInjector;

EnsureAuthlibInjector::EnsureAuthlibInjector(LaunchTask* parent, AuthSessionPtr session) : LaunchStep(parent), m_session(std::move(session)) {}

void EnsureAuthlibInjector::executeTask()
{
    emit logLine(tr("Preparing authlib-injector for the Ely.by account...\n"), MessageLevel::Launcher);
    m_task = std::make_shared<AuthlibInjectorTask>(cacheDirectory(APPLICATION->dataRoot()), makeNetFetcher(APPLICATION->network()));
    connect(m_task.get(), &Task::status, this, &Task::setStatus);
    connect(m_task.get(), &Task::warningLogged, this, [this](const QString& warning) { emit logLine(warning + "\n", MessageLevel::Warning); });
    connect(m_task.get(), &Task::succeeded, this, [this] {
        m_session->authlibInjectorJar = m_task->jarPath();
        emit logLine(tr("authlib-injector %1 is ready.\n").arg(m_task->version()), MessageLevel::Launcher);
        emitSucceeded();
    });
    connect(m_task.get(), &Task::failed, this, [this](const QString& reason) {
        emit logLine(reason, MessageLevel::Fatal);
        emitFailed(reason);
    });
    connect(m_task.get(), &Task::aborted, this, [this] { emitAborted(); });
    m_task->start();
}

bool EnsureAuthlibInjector::abort()
{
    if (m_task && m_task->isRunning()) {
        m_task->abort();
    }
    return true;
}
