// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Crash Detective: gathers the log files of an instance (read-only).
#pragma once

#include <QList>
#include <QString>
#include "DiagnosticsTypes.h"

class MinecraftInstance;

namespace Zero::Diagnostics {

class CrashReportCollector {
   public:
    static constexpr qint64 kMaxLatestLogBytes = 4 * 1024 * 1024;  ///< tail of latest.log
    static constexpr qint64 kMaxReportBytes = 1024 * 1024;         ///< head of a crash report / hs_err file

    /// latest.log (tail), the newest crash report and the newest hs_err file of `instance`.
    static QList<LogSource> collect(MinecraftInstance* instance);

    /// Same, for a plain game directory. `lastLaunchMs` is BaseInstance::lastLaunch() (0 = unknown). Crash reports
    /// and hs_err files modified at/after it are preferred; if none is, the newest overall is returned flagged `older`.
    static QList<LogSource> collectFromGameRoot(const QString& gameRoot, qint64 lastLaunchMs);
};

}  // namespace Zero::Diagnostics
