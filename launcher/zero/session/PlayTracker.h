// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - which instances are being played right now, and how each play session ended. No widgets.
// Fed by Application's launch signals (phase changes, crashes, launch end); the clock, the name lookup and the process id lookup are
// injectable. Drives the tray, the "While playing" behaviour and the Now playing / Last session cards of Home.
#pragma once

#include <QMap>
#include <QObject>
#include <QString>
#include <functional>
#include <optional>
#include "zero/history/SessionHistory.h"

namespace Zero {
namespace Session {

/// "12:04" under an hour, "1:20:33" from one hour up (seconds, never negative).
QString formatClock(qint64 seconds);

struct RunningSession {
    QString instanceId;
    QString instanceName;
    qint64 startedMs = 0;
    qint64 pid = 0;  ///< 0 = unknown
};

struct EndedSession {
    QString instanceId;
    QString instanceName;
    qint64 startedMs = 0;
    qint64 endedMs = 0;
    bool crashed = false;  ///< the game ended abnormally without the user stopping it
    bool stoppedByUser = false;

    qint64 durationMs() const { return endedMs > startedMs ? endedMs - startedMs : 0; }
};

class PlayTracker : public QObject {
    Q_OBJECT
   public:
    using Clock = std::function<qint64()>;
    using NameLookup = std::function<QString(const QString&)>;
    using PidLookup = std::function<qint64(const QString&)>;

    explicit PlayTracker(Clock clock = {}, NameLookup names = {}, PidLookup pids = {}, QObject* parent = nullptr);
    ~PlayTracker() override;

    /// The application's tracker (the first one created), null before that.
    static PlayTracker* global();

    // fed by the launch signals
    void onPhaseChanged(const QString& instanceId, History::Phase phase);
    void onStoppedUnexpectedly(const QString& instanceId, int exitCode);
    void onLaunchEnded(const QString& instanceId, bool success, bool aborted, bool gameStarted);

    /// Running sessions, ordered by instance id.
    const QMap<QString, RunningSession>& running() const { return m_running; }
    bool isRunning(const QString& instanceId) const { return m_running.contains(instanceId); }
    qint64 nowMs() const;
    /// Sets the process id of a running session (dev aids; the application feeds it through the PidLookup).
    void notePid(const QString& instanceId, qint64 pid);

   signals:
    /// Something started or ended.
    void runningChanged();
    /// The game of an instance ended (only for launches where the game really started).
    void sessionEnded(const Zero::Session::EndedSession& session);
    /// The last running instance ended.
    void allEnded();

   private:
    Clock m_clock;
    NameLookup m_names;
    PidLookup m_pids;
    QMap<QString, RunningSession> m_running;
    QMap<QString, int> m_crashCodes;
};

}  // namespace Session
}  // namespace Zero

Q_DECLARE_METATYPE(Zero::Session::EndedSession)
