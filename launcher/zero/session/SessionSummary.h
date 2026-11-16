// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the end-of-session summary: what a play session left behind (screenshots taken during it, worlds backed up), built from
// real files only, and the store that keeps the last one in `<data>/zero/last-session.json` until the user dismisses it. No widgets.
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <optional>
#include "zero/session/PlayTracker.h"

namespace Zero {
namespace Session {

/// A session shorter than this gets no summary.
constexpr qint64 kMinSummarySeconds = 60;
/// Screenshots written this long after the game ended still belong to the session (the file is flushed on exit).
constexpr qint64 kScreenshotGraceMs = 5000;
/// Paths kept in the stored summary (the card shows 6 thumbnails and "+N").
constexpr int kMaxStoredScreenshots = 60;

struct LastSession {
    QString instanceId;
    QString instanceName;
    qint64 startedMs = 0;
    qint64 endedMs = 0;
    QStringList screenshots;  ///< absolute paths, oldest first
    int worldsBackedUp = 0;   ///< 0 = no backup was made

    qint64 durationSeconds() const { return endedMs > startedMs ? (endedMs - startedMs) / 1000 : 0; }
    bool operator==(const LastSession&) const = default;
};

/// Image files (png / jpg / jpeg) directly inside `screenshotsDir` modified from `startMs` to `endMs` + kScreenshotGraceMs, oldest first
/// (ties by name). A missing folder gives an empty list.
QStringList screenshotsTakenDuring(const QString& screenshotsDir, qint64 startMs, qint64 endMs);

/// The summary of `ended`, or nullopt when there is none: the game crashed, or it ran less than kMinSummarySeconds.
std::optional<LastSession> buildSummary(const EndedSession& ended, const QString& screenshotsDir);

QByteArray summaryToJson(const LastSession& session);
/// Nullopt for anything that is not a well-formed summary.
std::optional<LastSession> summaryFromJson(const QByteArray& json);

/// Keeps the current summary (one at a time) in a file. Missing / unreadable file = no summary.
class LastSessionStore : public QObject {
    Q_OBJECT
   public:
    explicit LastSessionStore(const QString& filePath, QObject* parent = nullptr);
    ~LastSessionStore() override;

    /// The application's store (the first one created), null before that.
    static LastSessionStore* global();

    const std::optional<LastSession>& current() const { return m_current; }
    /// Replaces the summary (the next session replaces the previous one).
    void set(const LastSession& session);
    /// Adds worlds to the backup line of the stored summary of `instanceId` (the backup finishes after the game ended).
    void addBackups(const QString& instanceId, int count);
    /// Removes the summary (the card's close button).
    void dismiss();
    QString filePath() const { return m_path; }

   signals:
    void changed();

   private:
    void load();
    void save() const;

    QString m_path;
    std::optional<LastSession> m_current;
};

}  // namespace Session
}  // namespace Zero
