// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - playtime statistics computed from the session history and the Prism per-instance totals. No widgets, no APPLICATION:
// "now" is passed in and days are local-time calendar days (QDateTime local time), so the numbers are testable. See docs/zero/ARCHITECTURE.md.
#pragma once

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include "SessionHistory.h"

namespace Zero {
namespace History {
namespace Playtime {

struct Day {
    QDate date;
    qint64 ms = 0;
    int sessionCount = 0;  ///< sessions that touch this day (a session crossing midnight counts on both days)
};

struct InstanceTotal {
    QString id;
    QString name;
    qint64 totalSeconds = 0;  ///< Prism's BaseInstance::totalTimePlayed()
    qint64 lastPlayedMs = 0;  ///< ms since epoch, 0 = never
};

struct InstanceShare {
    InstanceTotal instance;
    double share = 0.0;  ///< 0..1 of the all-time total
};

struct Longest {
    qint64 ms = 0;
    QString instanceName;
    QDate date;
    bool valid() const { return ms > 0; }
};

/// `count` calendar days ending with the day of `now` (oldest first). A session crossing midnight is split between the days.
QList<Day> days(const QList<Session>& sessions, const QDateTime& now, int count = 14);
/// Last 7 days including today.
qint64 thisWeekMs(const QList<Session>& sessions, const QDateTime& now);
/// Sessions that started in the last 30 days including today.
int sessionsLast30(const QList<Session>& sessions, const QDateTime& now);
/// The longest session of the history (invalid when there is none).
Longest longestSession(const QList<Session>& sessions);
/// Sum of the Prism totals, in ms.
qint64 allTimeMs(const QList<InstanceTotal>& totals);
/// Instances with a total > 0, longest first (ties by name), with their share of the all-time total.
QList<InstanceShare> byInstance(const QList<InstanceTotal>& totals);

/// "2 h 15 min", "3 h", "45 min", "< 1 min", "0 min".
QString formatDuration(qint64 ms);
/// A nice gridline step for a chart whose largest value is `maxMs`: 30 min, 1 h, 2 h, 4 h, ... (doubling).
qint64 niceStepMs(qint64 maxMs);

}  // namespace Playtime
}  // namespace History
}  // namespace Zero
