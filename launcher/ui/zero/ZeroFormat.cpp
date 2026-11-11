// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroFormat.h"

#include <QCoreApplication>

namespace Zero {
namespace Format {

namespace {
QString tr(const char* s)
{
    return QCoreApplication::translate("Zero::Format", s);
}
}  // namespace

QString relativeTime(qint64 ms, const QDateTime& now)
{
    if (ms <= 0)
        return tr("Never");

    const QDateTime then = QDateTime::fromMSecsSinceEpoch(ms);
    const qint64 secs = then.secsTo(now);  // negative for timestamps in the future (clock skew)
    if (secs < 60)
        return tr("Just now");

    if (secs < 3600) {
        const qint64 n = secs / 60;
        return n == 1 ? tr("1 minute ago") : tr("%1 minutes ago").arg(n);
    }

    const qint64 days = then.date().daysTo(now.date());
    if (days <= 0) {
        const qint64 n = secs / 3600;
        return n == 1 ? tr("1 hour ago") : tr("%1 hours ago").arg(n);
    }
    if (days == 1)
        return tr("Yesterday");
    if (days < 7)
        return tr("%1 days ago").arg(days);
    if (days < 30) {
        const qint64 n = days / 7;
        return n == 1 ? tr("1 week ago") : tr("%1 weeks ago").arg(n);
    }
    if (days < 365) {
        const qint64 n = days / 30;
        return n == 1 ? tr("1 month ago") : tr("%1 months ago").arg(n);
    }
    const qint64 n = days / 365;
    return n == 1 ? tr("1 year ago") : tr("%1 years ago").arg(n);
}

QString relativeTime(qint64 ms)
{
    return relativeTime(ms, QDateTime::currentDateTime());
}

QString playtime(qint64 seconds)
{
    if (seconds <= 0)
        return tr("0 min");
    if (seconds < 60)
        return tr("< 1 min");

    const qint64 totalMinutes = seconds / 60;
    const qint64 hours = totalMinutes / 60;
    const qint64 minutes = totalMinutes % 60;
    if (hours == 0)
        return tr("%1 min").arg(minutes);
    if (minutes == 0)
        return tr("%1 h").arg(hours);
    return tr("%1 h %2 min").arg(hours).arg(minutes);
}

QString bytes(qint64 size)
{
    if (size < 1024)
        return tr("%1 B").arg(qMax<qint64>(size, 0));
    double value = size / 1024.0;
    if (qRound64(value) < 1024)
        return tr("%1 KB").arg(qRound64(value));
    static const char* const units[] = { "MB", "GB", "TB" };
    int unit = 0;
    value /= 1024.0;
    while (unit < 2 && qRound(value * 10) >= 10240) {
        value /= 1024.0;
        ++unit;
    }
    return QStringLiteral("%1 %2").arg(QString::number(value, 'f', 1), QString::fromLatin1(units[unit]));
}

}  // namespace Format
}  // namespace Zero
