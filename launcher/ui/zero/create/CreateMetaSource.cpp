// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Create Instance metadata source (see CreateMetaSource.h).
#include "CreateMetaSource.h"

#include "Application.h"
#include "meta/Index.h"
#include "meta/VersionList.h"

namespace Zero {
namespace Create {

PrismMetaSource::PrismMetaSource(QObject* parent) : MetaSource(parent) {}

PrismMetaSource::~PrismMetaSource()
{
    // A running load keeps going inside Prism (the list's own BaseEntity task); only our listeners go away with us.
    if (m_task)
        disconnect(m_task.get(), nullptr, this, nullptr);
}

MetaSource::Status PrismMetaSource::status(const QString& uid) const
{
    if (uid == m_running || m_queue.contains(uid))
        return Status::Loading;
    if (m_errors.contains(uid))
        return Status::Failed;
    if (APPLICATION->metadataIndex()->get(uid)->isLoaded())
        return Status::Ready;
    return Status::Idle;
}

QString PrismMetaSource::errorText(const QString& uid) const
{
    return m_errors.value(uid);
}

void PrismMetaSource::request(const QString& uid)
{
    const Status s = status(uid);
    if (s == Status::Ready || s == Status::Loading)
        return;
    m_errors.remove(uid);
    m_queue.append(uid);
    emit statusChanged(uid);
    startNext();
}

QList<Meta::Version::Ptr> PrismMetaSource::versions(const QString& uid) const
{
    const auto list = APPLICATION->metadataIndex()->get(uid);
    if (!list->isLoaded())
        return {};
    return list->versions();
}

void PrismMetaSource::startNext()
{
    if (!m_running.isEmpty() || m_queue.isEmpty())
        return;
    m_running = m_queue.takeFirst();
    const auto list = APPLICATION->metadataIndex()->get(m_running);
    const QString uid = m_running;
    if (list->isLoaded()) {
        // Loaded meanwhile (Prism loaded the same list): nothing to run.
        m_running.clear();
        emit statusChanged(uid);
        startNext();
        return;
    }
    m_task = list->getLoadTask(false);
    auto finish = [this, uid](const QString& error) {
        if (m_running != uid)
            return;
        if (error.isEmpty() && !APPLICATION->metadataIndex()->get(uid)->isLoaded())
            m_errors.insert(uid, tr("The version list could not be read."));
        else if (!error.isEmpty())
            m_errors.insert(uid, error);
        m_running.clear();
        m_task.reset();
        emit statusChanged(uid);
        startNext();
    };
    // Queued: `finish` drops the task, which must not happen while the task is still emitting.
    connect(m_task.get(), &Task::succeeded, this, [finish]() { finish({}); }, Qt::QueuedConnection);
    connect(
        m_task.get(), &Task::failed, this,
        [this, finish](const QString& reason) {
            finish(reason.trimmed().isEmpty() ? tr("The version list could not be downloaded.") : reason.trimmed());
        },
        Qt::QueuedConnection);
    connect(
        m_task.get(), &Task::aborted, this, [this, finish]() { finish(tr("Loading the version list was cancelled.")); },
        Qt::QueuedConnection);
    // The task may already be running (the dialog was reopened mid-download, or Prism is loading the same list): starting
    // it again would trip Task's "already running" assertion, so only listen to it.
    if (!m_task->isRunning())
        m_task->start();
}

}  // namespace Create
}  // namespace Zero
