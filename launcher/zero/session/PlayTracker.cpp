// SPDX-License-Identifier: GPL-3.0-only
#include "PlayTracker.h"

#include <QDateTime>

namespace Zero {
namespace Session {

namespace {
PlayTracker* g_global = nullptr;
}

QString formatClock(qint64 seconds)
{
    if (seconds < 0)
        seconds = 0;
    const qint64 h = seconds / 3600;
    const qint64 m = (seconds % 3600) / 60;
    const qint64 s = seconds % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
}

PlayTracker::PlayTracker(Clock clock, NameLookup names, PidLookup pids, QObject* parent)
    : QObject(parent), m_clock(std::move(clock)), m_names(std::move(names)), m_pids(std::move(pids))
{
    qRegisterMetaType<Zero::Session::EndedSession>();
    if (!g_global)
        g_global = this;
}

PlayTracker::~PlayTracker()
{
    if (g_global == this)
        g_global = nullptr;
}

PlayTracker* PlayTracker::global()
{
    return g_global;
}

qint64 PlayTracker::nowMs() const
{
    return m_clock ? m_clock() : QDateTime::currentMSecsSinceEpoch();
}

void PlayTracker::notePid(const QString& instanceId, qint64 pid)
{
    auto it = m_running.find(instanceId);
    if (it != m_running.end())
    {
        it->pid = pid > 0 ? pid : 0;
        emit runningChanged();
    }
}

void PlayTracker::onPhaseChanged(const QString& instanceId, History::Phase phase)
{
    if (phase != History::Phase::Running || m_running.contains(instanceId))
        return;
    RunningSession s;
    s.instanceId = instanceId;
    s.instanceName = m_names ? m_names(instanceId) : QString();
    if (s.instanceName.isEmpty())
        s.instanceName = instanceId;
    s.startedMs = nowMs();
    s.pid = m_pids ? m_pids(instanceId) : 0;
    if (s.pid < 0)
        s.pid = 0;
    m_running.insert(instanceId, s);
    m_crashCodes.remove(instanceId);
    emit runningChanged();
}

void PlayTracker::onStoppedUnexpectedly(const QString& instanceId, int exitCode)
{
    m_crashCodes.insert(instanceId, exitCode);
}

void PlayTracker::onLaunchEnded(const QString& instanceId, bool success, bool aborted, bool gameStarted)
{
    const bool hadCrash = m_crashCodes.remove(instanceId) > 0;
    auto it = m_running.find(instanceId);
    if (it == m_running.end())
        return;
    EndedSession ended;
    ended.instanceId = instanceId;
    ended.instanceName = it->instanceName;
    ended.startedMs = it->startedMs;
    ended.endedMs = nowMs();
    ended.stoppedByUser = aborted;
    ended.crashed = gameStarted && (hadCrash || (!success && !aborted));
    m_running.erase(it);
    emit runningChanged();
    emit sessionEnded(ended);
    if (m_running.isEmpty())
        emit allEnded();
}

}  // namespace Session
}  // namespace Zero
