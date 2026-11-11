// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - session history: one record per game launch (played / stopped / crashed / failed). No widgets.
// Stored in `<data root>/zero/session-history.json`, newest first, capped at kMaxSessions. See docs/zero/ARCHITECTURE.md.
#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <functional>
#include <optional>

namespace Zero {
namespace History {

constexpr int kMaxSessions = 500;

enum class Outcome { Played, Stopped, Crashed, Failed };

struct Session {
    QString instanceId;
    QString instanceName;
    qint64 startedMs = 0;
    qint64 endedMs = 0;
    Outcome outcome = Outcome::Played;
    std::optional<int> exitCode;

    qint64 durationMs() const { return endedMs > startedMs ? endedMs - startedMs : 0; }
};

/// Own copy of Application::LaunchPhase so the service does not depend on the application.
enum class Phase { Idle, Preparing, Running };

/// The outcome of a finished launch, or nullopt when nothing is recorded (cancelled during preparation).
std::optional<Outcome> classify(bool success, bool aborted, bool gameStarted);

QString outcomeToString(Outcome outcome);
std::optional<Outcome> outcomeFromString(const QString& text);

class SessionHistory : public QObject {
    Q_OBJECT
   public:
    using Clock = std::function<qint64()>;
    using NameLookup = std::function<QString(const QString& instanceId)>;

    /// `clock` defaults to QDateTime::currentMSecsSinceEpoch. `nameLookup` gives the instance name at launch end
    /// (the instance id is used when it is missing or returns an empty string).
    explicit SessionHistory(const QString& filePath, Clock clock = {}, NameLookup nameLookup = {}, QObject* parent = nullptr);
    ~SessionHistory() override;

    /// The process-wide instance created by the application (nullptr before that). The first created instance is global.
    static SessionHistory* global();

    /// Newest first. Loads the file on first call.
    const QList<Session>& sessions() const;
    /// Adds `session` as the newest entry, drops the oldest beyond kMaxSessions, saves and emits changed().
    void record(const Session& session);

    // recorder slots (one launch per instance at a time)
    void onPhaseChanged(const QString& instanceId, Phase phase);
    void onStoppedUnexpectedly(const QString& instanceId, int exitCode);
    void onLaunchEnded(const QString& instanceId, bool success, bool aborted, bool gameStarted);

    QString filePath() const { return m_path; }

   signals:
    void changed();

   private:
    struct Pending {
        qint64 preparingMs = 0;
        std::optional<qint64> runningMs;
        std::optional<int> exitCode;
    };

    void ensureLoaded() const;
    void save() const;
    qint64 now() const;

    QString m_path;
    Clock m_clock;
    NameLookup m_nameLookup;
    mutable bool m_loaded = false;
    mutable QList<Session> m_sessions;
    QHash<QString, Pending> m_pending;
};

}  // namespace History
}  // namespace Zero
