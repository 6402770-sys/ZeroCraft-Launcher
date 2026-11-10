// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Crash Detective: local, rule-based analysis of Minecraft logs and crash reports.
#pragma once

#include <QList>
#include <optional>
#include "DiagnosticsTypes.h"

namespace Zero::Diagnostics {

class CrashDetective {
   public:
    /// Runs every rule over `sources`. Findings are deduplicated and sorted by severity, then confidence.
    /// `exitCode` is the game's exit code when known (used only when no log-based rule explains the failure).
    static QList<Finding> analyze(const QList<LogSource>& sources, std::optional<int> exitCode = std::nullopt);

    /// Number of rules in the table (for tests / docs).
    static int ruleCount();
    /// Rule ids, in table order.
    static QStringList ruleIds();
};

}  // namespace Zero::Diagnostics
