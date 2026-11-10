// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Crash Detective: plain data types shared by the analyzer and the UI. No widgets.
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace Zero::Diagnostics {

/// One piece of text the analyzer reads (latest.log, a crash report, an hs_err file).
struct LogSource {
    QString name;  ///< short display name, e.g. "latest.log"
    QString path;  ///< absolute path on disk (may be empty in tests)
    QString text;
    QDateTime modified;
    bool older = false;      ///< predates the last game launch: it may describe a previous session
    bool truncated = false;  ///< only the tail / head of the file was read
};

/// A log line that backs a finding.
struct Evidence {
    QString source;  ///< LogSource::name
    int line = 0;    ///< 1-based line number inside the source text
    QString text;
};

enum class Severity { Critical = 0, Warning = 1, Info = 2 };
/// Confirmed is reserved for causes the log states explicitly.
enum class Confidence { Confirmed = 0, Likely = 1, Possible = 2 };

enum class Action { OpenInstanceSettings, OpenModsFolder, OpenLogsFolder, OpenCrashReport, OpenGameFolder };

struct Finding {
    QString ruleId;
    Severity severity = Severity::Info;
    Confidence confidence = Confidence::Possible;
    QString title;
    QString explanation;
    QList<Evidence> evidence;  ///< at most 5
    QStringList suggestions;
    QList<Action> actions;
};

}  // namespace Zero::Diagnostics
