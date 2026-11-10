// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - human readable time and duration strings shared by the Home and Library pages.
#pragma once

#include <QDateTime>
#include <QString>

namespace Zero {
namespace Format {

/// "Never" (timestamp <= 0), "Just now", "5 minutes ago", "2 hours ago", "Yesterday", "3 days ago", "2 weeks ago",
/// "4 months ago", "2 years ago". `msSinceEpoch` is a BaseInstance::lastLaunch() style timestamp. The "Yesterday"
/// rule is calendar based (local time), the other buckets use elapsed time.
QString relativeTime(qint64 msSinceEpoch, const QDateTime& now);
QString relativeTime(qint64 msSinceEpoch);

/// "34 h 12 min", "3 h", "12 min", "< 1 min" and "0 min" for a duration in seconds.
QString playtime(qint64 seconds);

}  // namespace Format
}  // namespace Zero
