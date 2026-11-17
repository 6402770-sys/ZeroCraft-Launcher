// SPDX-License-Identifier: GPL-3.0-only
#include "PlaytimeStats.h"

#include <QCoreApplication>
#include <algorithm>

namespace Zero {
namespace History {
namespace Playtime {

namespace {

QDateTime startOfDay(const QDate& date)
{
    return QDateTime(date, QTime(0, 0));
}

}  // namespace

QList<Day> days(const QList<Session>& sessions, const QDateTime& now, int count)
{
    QList<Day> out;
    if (count <= 0)
        return out;
    const QDate today = now.toLocalTime().date();
    for (int i = count - 1; i >= 0; --i) {
        Day d;
        d.date = today.addDays(-i);
        out.append(d);
    }
    const qint64 windowStart = startOfDay(out.first().date).toMSecsSinceEpoch();
    const qint64 windowEnd = startOfDay(today.addDays(1)).toMSecsSinceEpoch();
    for (const Session& s : sessions) {
        const qint64 begin = s.startedMs;
        const qint64 end = std::max(s.endedMs, s.startedMs);
        if (end < windowStart || begin >= windowEnd)
            continue;
        QDate day = QDateTime::fromMSecsSinceEpoch(begin).date();
        const QDate lastDay = QDateTime::fromMSecsSinceEpoch(end > begin ? end - 1 : end).date();
        for (; day <= lastDay; day = day.addDays(1)) {
            const qint64 dayStart = startOfDay(day).toMSecsSinceEpoch();
            const qint64 dayEnd = startOfDay(day.addDays(1)).toMSecsSinceEpoch();
            const qint64 from = std::max(begin, dayStart);
            const qint64 to = std::min(end, dayEnd);
            const int index = int(out.first().date.daysTo(day));
            if (index < 0 || index >= out.size())
                continue;
            out[index].ms += std::max<qint64>(0, to - from);
            out[index].sessionCount += 1;
        }
    }
    return out;
}

qint64 thisWeekMs(const QList<Session>& sessions, const QDateTime& now)
{
    qint64 total = 0;
    for (const Day& d : days(sessions, now, 7))
        total += d.ms;
    return total;
}

int sessionsLast30(const QList<Session>& sessions, const QDateTime& now)
{
    const qint64 from = startOfDay(now.toLocalTime().date().addDays(-29)).toMSecsSinceEpoch();
    int n = 0;
    for (const Session& s : sessions)
        if (s.startedMs >= from)
            ++n;
    return n;
}

Longest longestSession(const QList<Session>& sessions)
{
    Longest best;
    for (const Session& s : sessions) {
        if (s.durationMs() > best.ms) {
            best.ms = s.durationMs();
            best.instanceName = s.instanceName.isEmpty() ? s.instanceId : s.instanceName;
            best.date = QDateTime::fromMSecsSinceEpoch(s.startedMs).date();
        }
    }
    return best;
}

qint64 allTimeMs(const QList<InstanceTotal>& totals)
{
    qint64 sum = 0;
    for (const InstanceTotal& t : totals)
        sum += std::max<qint64>(0, t.totalSeconds) * 1000;
    return sum;
}

QList<InstanceShare> byInstance(const QList<InstanceTotal>& totals)
{
    QList<InstanceShare> out;
    const qint64 all = allTimeMs(totals);
    for (const InstanceTotal& t : totals) {
        if (t.totalSeconds <= 0)
            continue;
        InstanceShare s;
        s.instance = t;
        s.share = all > 0 ? double(t.totalSeconds * 1000) / double(all) : 0.0;
        out.append(s);
    }
    std::stable_sort(out.begin(), out.end(), [](const InstanceShare& a, const InstanceShare& b) {
        if (a.instance.totalSeconds != b.instance.totalSeconds)
            return a.instance.totalSeconds > b.instance.totalSeconds;
        return QString::localeAwareCompare(a.instance.name, b.instance.name) < 0;
    });
    return out;
}

QString formatDuration(qint64 ms)
{
    if (ms <= 0)
        return QCoreApplication::translate("Zero::History::Playtime", "0 min");
    if (ms < 60 * 1000)
        return QCoreApplication::translate("Zero::History::Playtime", "< 1 min");
    const qint64 totalMinutes = ms / 60000;
    const qint64 hours = totalMinutes / 60;
    const qint64 minutes = totalMinutes % 60;
    if (hours == 0)
        return QCoreApplication::translate("Zero::History::Playtime", "%1 min").arg(minutes);
    if (minutes == 0)
        return QCoreApplication::translate("Zero::History::Playtime", "%1 h").arg(hours);
    return QCoreApplication::translate("Zero::History::Playtime", "%1 h %2 min").arg(hours).arg(minutes);
}

qint64 niceStepMs(qint64 maxMs)
{
    const qint64 halfHour = 30LL * 60 * 1000;
    // three gridlines at step, 2*step, 3*step: the step is the smallest of 30 min, 1 h, 2 h, 4 h... whose third line reaches the maximum
    qint64 step = halfHour;
    while (step * 3 < maxMs)
        step *= 2;
    return step;
}

}  // namespace Playtime
}  // namespace History
}  // namespace Zero
