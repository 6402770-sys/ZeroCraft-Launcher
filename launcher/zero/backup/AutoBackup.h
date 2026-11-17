// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - automatic world backups after playing. When a game session ends (played, stopped or crashed) the worlds that
// were touched during the session (level.dat modified since the session start) are backed up as Kind::Auto, one world at a time,
// through WorldBackupJobs. No widgets, no APPLICATION: everything the service needs is injected. See docs/zero/ARCHITECTURE.md
// ("Smart Backup").
//
// While the work of one instance is pending (scan + queue) the instance stays locked for Application::launch
// (WorldBackupJobs::lockInstance), so a new launch can never slip in between two queued backups.
#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>
#include "WorldBackup.h"
#include "WorldBackupJobs.h"
#include "zero/worlds/WorldScanner.h"

namespace Zero {
namespace Backup {

/// Worlds bigger than this are not backed up automatically (reported as "too large").
constexpr qint64 kAutoMaxBytes = 2LL * 1024 * 1024 * 1024;
/// A world counts as played when its level.dat changed no earlier than the session start minus this (clock / filesystem slack).
constexpr qint64 kAutoSlackMs = 5000;

/// A world found after a session, with what the selection needs.
struct AutoCandidate {
    Worlds::WorldEntry world;
    qint64 levelDatModifiedMs = 0;  ///< modification time of `<world>/level.dat`, 0 when unreadable
    qint64 sizeBytes = 0;           ///< folder size; only meaningful when touchedSince() is true
};

struct AutoSelection {
    QList<Worlds::WorldEntry> toBackUp;
    QList<Worlds::WorldEntry> tooLarge;
};

/// True when a level.dat modified at `levelDatModifiedMs` belongs to a session that started at `sessionStartMs`.
bool touchedSince(qint64 levelDatModifiedMs, qint64 sessionStartMs);

/// Pure. Touched worlds (touchedSince) are split by size: > kAutoMaxBytes goes to `tooLarge`, the rest to `toBackUp`. Input order is kept.
AutoSelection selectWorlds(const QList<AutoCandidate>& candidates, qint64 sessionStartMs);

/// Modification time of `<worldFolder>/level.dat` in ms since the epoch, 0 when it cannot be read.
qint64 levelDatModifiedMs(const QString& worldFolder);

class AutoBackup : public QObject {
    Q_OBJECT
   public:
    using Clock = std::function<qint64()>;
    using EnabledFn = std::function<bool()>;
    /// Name and saves folder of an instance (GUI thread). An empty `id` in the result means "unknown instance".
    using SourceLookup = std::function<Worlds::InstanceSource(const QString& instanceId)>;

    /// `jobs` runs the backups (not owned). `clock` defaults to QDateTime::currentMSecsSinceEpoch.
    AutoBackup(WorldBackupJobs* jobs, EnabledFn enabled, SourceLookup source, Clock clock = {}, QObject* parent = nullptr);
    ~AutoBackup() override;

    /// The process-wide instance created by the application (nullptr before that). The first created instance is global.
    static AutoBackup* global();

    /// The game of `instanceId` reached the Running phase: remembers the session start.
    void onGameRunning(const QString& instanceId);
    /// The launch ended. Does nothing unless the game started and automatic backups are enabled.
    void onLaunchEnded(const QString& instanceId, bool success, bool aborted, bool gameStarted);

    /// True while the work of `instanceId` (scan + queued backups) is pending.
    bool isPending(const QString& instanceId) const { return m_runs.contains(instanceId); }

   signals:
    /// The work of one instance ended. Not emitted when no world was touched. `tooLarge` holds world names. `fallbackFrom` is the
    /// chosen backup folder that was not reachable (the backups went to the default folder), empty when none.
    void finished(const QString& instanceId, const QString& instanceName, int backedUp, const QStringList& tooLarge, int failed,
                  const QString& firstError, const QString& fallbackFrom = {});

   private:
    struct Run {
        QString instanceName;
        QList<Worlds::WorldEntry> queue;
        QString current;  ///< key of the world whose job was requested and has not reported yet
        bool inCall = false;
        int backedUp = 0;
        int failed = 0;
        QStringList tooLarge;
        QString firstError;
        QString fallbackFrom;
    };

    void scan(const QString& instanceId, const Worlds::InstanceSource& source, qint64 sessionStartMs);
    void onSelected(const QString& instanceId, const AutoSelection& selection);
    void startNext(QString instanceId);
    void finishRun(QString instanceId);
    void onJobFinished(const QString& key, bool ok, const QString& message, bool automatic);
    void onFellBack(const QString& key, const QString& chosenFolder);
    qint64 now() const;

    WorldBackupJobs* m_jobs;
    EnabledFn m_enabled;
    SourceLookup m_source;
    Clock m_clock;
    QHash<QString, qint64> m_started;  // instance id -> session start (ms)
    QHash<QString, Run> m_runs;        // instance id -> pending work (holds one launch lock each)
};

}  // namespace Backup
}  // namespace Zero
