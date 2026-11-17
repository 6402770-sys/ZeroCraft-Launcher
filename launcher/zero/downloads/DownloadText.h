// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the plain data of the Downloads center (jobs, results, history entries) and the pure text helpers the panel and the
// top bar button show. No widgets, no Application. See docs/zero/ARCHITECTURE.md ("Downloads center").
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>
#include "tasks/Task.h"

namespace Zero {
namespace Downloads {

/// A summed size below this is treated as "the task reported no byte sizes" (a task that only counts steps adds small numbers).
constexpr qint64 kMinBytes = 1024;

/// What a job installs. Drives the icon of its row and the page that opens when a finished row is clicked.
enum class Kind { Mod, ResourcePack, ShaderPack, World, Modpack, Mixed, Update };

QString kindToString(Kind kind);
Kind kindFromString(const QString& text);  ///< Mixed for an unknown text

struct DownloadResult {
    enum class State { Done, Failed, Cancelled };
    State state = State::Done;
    QString error;
    QStringList warnings;
    qint64 bytes = 0;  ///< bytes downloaded, 0 when the task did not report any (see kMinBytes)
};

QString stateToString(DownloadResult::State state);
DownloadResult::State stateFromString(const QString& text);  ///< Failed for an unknown text
/// "Done" / "Failed" / "Cancelled".
QString stateText(DownloadResult::State state);

/// One job that runs right now, as the UI sees it.
struct ActiveDownload {
    QString id;
    QString title;
    QString instanceId;
    QString instanceName;
    Kind kind = Kind::Mixed;
    qint64 bytesDone = 0;
    qint64 bytesTotal = 0;  ///< 0 while no step reported a size
    double fraction = -1;   ///< 0..1, or -1 when nothing is known (indeterminate bar). Never decreases.
    double speed = 0;       ///< bytes per second, smoothed over the last ~3 s
    QString status;
    bool canCancel = true;

    bool bytesKnown() const { return bytesTotal >= kMinBytes; }
};

/// One finished job.
struct HistoryEntry {
    QString id;
    QString title;
    QString instanceId;
    QString instanceName;
    Kind kind = Kind::Mixed;
    DownloadResult::State state = DownloadResult::State::Done;
    QString error;
    qint64 bytes = 0;
    QDateTime finishedAt;  ///< UTC

    qint64 finishedMs() const { return finishedAt.isValid() ? finishedAt.toMSecsSinceEpoch() : 0; }
};

/// What a caller hands to DownloadCenter::start().
struct DownloadJob {
    using Kind = Zero::Downloads::Kind;
    QString title;
    QString instanceId;    ///< the instance that must not be launched meanwhile (empty = none)
    QString instanceName;  ///< shown in the captions
    Kind kind = Kind::Mixed;
    Task::Ptr task;
    /// Called exactly once, on the GUI thread, after the job left the active list (state, error, warnings and bytes of the result).
    std::function<void(const DownloadResult&)> onFinished;
};

// --- text helpers -----------------------------------------------------------------------------------------------------

/// "30 MB", "1.5 GB", "812 KB" (Format::bytes without the trailing ".0").
QString sizeText(qint64 bytes);
/// "2.1 MB/s".
QString speedText(double bytesPerSecond);
/// "12.4 MB of 30 MB · 2.1 MB/s"; without a total "12.4 MB · 2.1 MB/s"; the speed part is left out when it is below 1 B/s.
QString progressText(qint64 done, qint64 total, double speed);
/// "Fabric Fun · 12.4 MB of 30 MB · 2.1 MB/s" (the instance part is left out for a job without an instance). Without bytes but with
/// a fraction: "Fabric Fun · 45%"; with nothing known yet: "Fabric Fun · Preparing...".
QString activeCaption(const ActiveDownload& job);
/// "Fabric Fun · 30 MB · 2 min ago" (parts without data are left out). `nowMs` = the current time in ms since the epoch.
QString historyCaption(const HistoryEntry& entry, qint64 nowMs);
/// Overall fraction of several jobs: weighted by total bytes when every job with a known fraction reports bytes, else the plain mean
/// of the known fractions; -1 when no job knows its progress.
double overallFraction(const QList<ActiveDownload>& jobs);
/// "Downloading 1 item" / "Downloading 2 items", plus " · 45%" when the overall fraction is known. Empty for no jobs.
QString summaryText(const QList<ActiveDownload>& jobs);

// --- texts of the flows that start a job (toasts and refusals) --------------------------------------------------------------

/// "Downloading Sodium…" (the toast when a job starts).
QString startedToastText(const QString& title);
/// "Couldn't download Sodium" (the toast of a Failed job).
QString failedToastText(const QString& title);
/// "Downloaded with warnings" (the toast of a Done job that reported warnings).
QString warningsToastText();
/// "Downloads" (the action of those toasts: opens the panel).
QString downloadsActionText();
/// "Files for this instance are still downloading. Try again when they finish." (a delete / folder rename of a busy instance).
QString instanceBusyText();
/// "Sodium and 2 dependencies" (`dependencyCount` 0 = just the name).
QString modTitleText(const QString& name, int dependencyCount);
/// "Update 3 mods", or the mod name for one update.
QString updateTitleText(const QStringList& names);
/// "3 mods and 1 shader" for a mixed list; "Boost" flows and the cart use it (counts of 0 are left out).
QString contentTitleText(int mods, int resourcePacks, int shaders);

}  // namespace Downloads
}  // namespace Zero
