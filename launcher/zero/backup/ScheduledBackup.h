// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - scheduled world backups (`ZeroBackupSchedule`: off / daily / weekly). A catch-up check runs a couple of minutes after
// start-up and then every hour; a run is due when the last completed run (`<data>/zero/backup-schedule.json`) is older than the
// interval. It only starts while no game runs or launches. A run backs up, for every instance, every world whose level.dat is newer than
// its newest backup (any kind, in any known root) or that has no backup, as Kind::Auto through WorldBackupJobs, one world at a time;
// worlds over kAutoMaxBytes are counted as "too large". An instance stays locked for Application::launch while its queue is pending
// (WorldBackupJobs::lockInstance). No widgets, no APPLICATION: everything the service needs is injected. See docs/zero/ARCHITECTURE.md
// ("Smart Backup").
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <functional>
#include "AutoBackup.h"
#include "WorldBackupJobs.h"
#include "zero/worlds/WorldScanner.h"

namespace Zero {
namespace Backup {

enum class Schedule { Off, Daily, Weekly };

constexpr qint64 kDailyMs = 24LL * 60 * 60 * 1000;
constexpr qint64 kWeeklyMs = 7 * kDailyMs;

/// "off" | "daily" | "weekly"; anything else reads as Off.
Schedule scheduleFromString(const QString& text);
QString scheduleToString(Schedule schedule);
/// 0 for Off.
qint64 scheduleIntervalMs(Schedule schedule);
/// True when a run is due: the schedule is not Off and the last completed run is older than the interval, never happened
/// (`lastRunMs` <= 0) or lies in the future (the clock was set back, the stored time cannot be trusted).
bool isDue(Schedule schedule, qint64 lastRunMs, qint64 nowMs);

/// A world found by a scheduled run, with what the selection needs.
struct ScheduleCandidate {
    Worlds::WorldEntry world;
    qint64 levelDatModifiedMs = 0;  ///< modification time of `<world>/level.dat`, 0 when unreadable
    qint64 newestBackupMs = 0;      ///< creation time of the newest backup of the world in any root, 0 = none
    qint64 sizeBytes = 0;           ///< folder size; only meaningful when needsBackup() is true
};

/// A world needs a backup when it has none, or its level.dat is newer than the newest backup.
bool needsBackup(qint64 levelDatModifiedMs, qint64 newestBackupMs);

/// Pure. Worlds that need a backup go to `toBackUp`, or to `tooLarge` when bigger than kAutoMaxBytes. Input order is kept.
AutoSelection selectScheduled(const QList<ScheduleCandidate>& candidates);

/// Creation time (ms) of the newest readable backup found in `dirs`, 0 when there is none. Disk access.
qint64 newestBackupMs(const QStringList& dirs);

/// "Scheduled backup: 3 worlds backed up, 1 world failed (reason). Saved in the ZERO folder: X isn't reachable." Empty when nothing
/// was backed up and nothing failed (no toast then). `tooLarge` is only mentioned next to something else.
QString summaryText(int backedUp, int failed, int tooLarge, const QString& firstError, const QString& fallbackFrom);

class ScheduledBackup : public QObject {
    Q_OBJECT
   public:
    struct Hooks {
        std::function<Schedule()> schedule;                          ///< the setting (empty = Off)
        std::function<bool()> gameActive;                            ///< any game runs or is launching (empty = false)
        std::function<QList<Worlds::InstanceSource>()> instances;    ///< every instance (GUI thread)
        std::function<QStringList()> roots;                          ///< every backup root (default: jobs->allRoots())
        std::function<qint64()> now;                                 ///< ms since the epoch (default: the clock)
    };
    struct Timing {
        int startDelayMs = 2 * 60 * 1000;  ///< the catch-up check after start()
        int recheckMs = 60 * 60 * 1000;    ///< between two checks
    };

    /// `jobs` runs the backups (not owned). `statePath` = `<data>/zero/backup-schedule.json`, read now.
    ScheduledBackup(WorldBackupJobs* jobs, QString statePath, Hooks hooks, QObject* parent = nullptr);
    ScheduledBackup(WorldBackupJobs* jobs, QString statePath, Hooks hooks, Timing timing, QObject* parent = nullptr);
    ~ScheduledBackup() override;

    /// The process-wide instance created by the application (nullptr before that). The first created instance is global.
    static ScheduledBackup* global();

    /// Arms the catch-up check (`startDelayMs` from now).
    void start();
    /// True from the moment a run began (scan + queue) until it ended.
    bool isRunning() const { return m_busy; }
    /// When the last completed run ended (ms since the epoch), 0 = never.
    qint64 lastRunMs() const { return m_lastRunMs; }

   signals:
    /// A run ended (also when it was interrupted because a game started: then it is not recorded and the next check retries).
    /// `fallbackFrom` is the chosen backup folder that was not reachable (the backups went to the default folder), empty when none.
    void finished(int backedUp, int failed, int tooLarge, const QString& firstError, const QString& fallbackFrom);

   private:
    struct Plan {
        QString instanceId;
        QList<Worlds::WorldEntry> queue;
    };

    void tick();
    void beginRun();
    void onPlanned(const QList<Plan>& plans, int tooLarge);
    void advance();
    void endRun(bool completed);
    void onJobFinished(const QString& key, bool ok, const QString& message, bool automatic);
    void onFellBack(const QString& key, const QString& chosenFolder);
    void releaseLock();
    void load();
    void save() const;
    qint64 now() const;

    WorldBackupJobs* m_jobs;
    QString m_path;
    Hooks m_hooks;
    Timing m_timing;
    QTimer m_timer;
    qint64 m_lastRunMs = 0;

    bool m_busy = false;
    QList<Plan> m_plans;
    int m_planIdx = 0;
    bool m_lockHeld = false;
    QString m_current;  ///< key of the world whose job was requested and has not reported yet
    bool m_inCall = false;
    int m_backedUp = 0;
    int m_failed = 0;
    int m_tooLarge = 0;
    QString m_firstError;
    QString m_fallbackFrom;
};

}  // namespace Backup
}  // namespace Zero
