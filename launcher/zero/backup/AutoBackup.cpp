// SPDX-License-Identifier: GPL-3.0-only
#include "AutoBackup.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>

namespace Zero {
namespace Backup {

namespace {
AutoBackup* g_global = nullptr;
}

bool touchedSince(qint64 levelDatModifiedMs, qint64 sessionStartMs)
{
    return levelDatModifiedMs > 0 && levelDatModifiedMs >= sessionStartMs - kAutoSlackMs;
}

AutoSelection selectWorlds(const QList<AutoCandidate>& candidates, qint64 sessionStartMs)
{
    AutoSelection out;
    for (const AutoCandidate& c : candidates) {
        if (!touchedSince(c.levelDatModifiedMs, sessionStartMs))
            continue;
        (c.sizeBytes > kAutoMaxBytes ? out.tooLarge : out.toBackUp).append(c.world);
    }
    return out;
}

qint64 levelDatModifiedMs(const QString& worldFolder)
{
    const QFileInfo info(worldFolder + QStringLiteral("/level.dat"));
    return info.exists() ? info.lastModified().toMSecsSinceEpoch() : 0;
}

AutoBackup::AutoBackup(WorldBackupJobs* jobs, EnabledFn enabled, SourceLookup source, Clock clock, QObject* parent)
    : QObject(parent), m_jobs(jobs), m_enabled(std::move(enabled)), m_source(std::move(source)), m_clock(std::move(clock))
{
    if (m_jobs) {
        connect(m_jobs, &WorldBackupJobs::jobFinished, this, &AutoBackup::onJobFinished);
        connect(m_jobs, &WorldBackupJobs::fellBackToDefault, this, &AutoBackup::onFellBack);
    }
    if (!g_global)
        g_global = this;
}

AutoBackup* AutoBackup::global()
{
    return g_global;
}

AutoBackup::~AutoBackup()
{
    if (g_global == this)
        g_global = nullptr;
    // pending work can no longer finish: release its launch locks
    for (auto it = m_runs.cbegin(); it != m_runs.cend(); ++it)
        WorldBackupJobs::unlockInstance(it.key());
}

qint64 AutoBackup::now() const
{
    return m_clock ? m_clock() : QDateTime::currentMSecsSinceEpoch();
}

void AutoBackup::onGameRunning(const QString& instanceId)
{
    m_started.insert(instanceId, now());
}

void AutoBackup::onLaunchEnded(const QString& instanceId, bool, bool, bool gameStarted)
{
    const auto it = m_started.find(instanceId);
    const qint64 start = it == m_started.end() ? 0 : it.value();
    if (it != m_started.end())
        m_started.erase(it);
    if (!gameStarted || start <= 0 || !m_jobs || !m_enabled || !m_enabled() || m_runs.contains(instanceId))
        return;
    const Worlds::InstanceSource source = m_source ? m_source(instanceId) : Worlds::InstanceSource();
    if (source.id.isEmpty() || source.savesDir.isEmpty())
        return;

    // `launchEnded` is emitted just before the launch phase returns to Idle: lock the instance now so it cannot be relaunched,
    // and do the work one event-loop turn later, when the running check passes.
    WorldBackupJobs::lockInstance(instanceId);
    Run run;
    run.instanceName = source.name;
    m_runs.insert(instanceId, run);
    QPointer<AutoBackup> self(this);
    QTimer::singleShot(0, this, [self, instanceId, source, start]() {
        if (self)
            self->scan(instanceId, source, start);
    });
}

void AutoBackup::scan(const QString& instanceId, const Worlds::InstanceSource& source, qint64 sessionStartMs)
{
    if (!m_runs.contains(instanceId))
        return;
    // disk work on the pool; the result is posted to the application object and the pointer is checked there, on the GUI thread
    QPointer<AutoBackup> self(this);
    QThreadPool::globalInstance()->start([self, instanceId, source, sessionStartMs]() {
        QList<AutoCandidate> candidates;
        const QList<Worlds::WorldEntry> worlds = Worlds::scanWorldsDir(source.id, source.name, source.savesDir);
        for (const Worlds::WorldEntry& w : worlds) {
            AutoCandidate c;
            c.world = w;
            c.levelDatModifiedMs = levelDatModifiedMs(w.folderPath);
            if (touchedSince(c.levelDatModifiedMs, sessionStartMs))
                c.sizeBytes = Worlds::folderSize(w.folderPath);
            candidates.append(c);
        }
        const AutoSelection selection = selectWorlds(candidates, sessionStartMs);
        auto* app = QCoreApplication::instance();
        if (!app)
            return;
        QMetaObject::invokeMethod(
            app,
            [self, instanceId, selection]() {
                if (self)
                    self->onSelected(instanceId, selection);
            },
            Qt::QueuedConnection);
    });
}

void AutoBackup::onSelected(const QString& instanceId, const AutoSelection& selection)
{
    auto it = m_runs.find(instanceId);
    if (it == m_runs.end())
        return;
    it->queue = selection.toBackUp;
    for (const Worlds::WorldEntry& w : selection.tooLarge)
        it->tooLarge.append(w.name);
    startNext(instanceId);
}

void AutoBackup::startNext(QString instanceId)  // by value: callers may pass a key that lives inside m_runs
{
    // One world at a time. A refused job (jobFinished fires inside backup()) counts as failed and the queue goes on.
    for (;;) {
        auto it = m_runs.find(instanceId);
        if (it == m_runs.end())
            return;
        if (it->queue.isEmpty())
            break;
        const Worlds::WorldEntry entry = it->queue.takeFirst();
        it->current = entry.key();
        it->inCall = true;
        const bool started = m_jobs->backup(entry, Kind::Auto);
        it = m_runs.find(instanceId);
        if (it == m_runs.end())
            return;
        it->inCall = false;
        if (started)
            return;  // the result arrives in onJobFinished
        it->current.clear();
    }
    finishRun(instanceId);
}

void AutoBackup::onJobFinished(const QString& key, bool ok, const QString& message, bool automatic)
{
    if (!automatic)
        return;
    for (auto it = m_runs.begin(); it != m_runs.end(); ++it) {
        if (it->current != key)
            continue;
        if (ok) {
            ++it->backedUp;
        } else {
            ++it->failed;
            if (it->firstError.isEmpty())
                it->firstError = message;
        }
        if (it->inCall)
            return;  // refused synchronously: startNext() continues
        it->current.clear();
        const QString instanceId = it.key();
        startNext(instanceId);
        return;
    }
}

void AutoBackup::onFellBack(const QString& key, const QString& chosenFolder)
{
    for (auto it = m_runs.begin(); it != m_runs.end(); ++it) {
        if (it->current == key) {
            it->fallbackFrom = chosenFolder;
            return;
        }
    }
}

void AutoBackup::finishRun(QString instanceId)
{
    const Run run = m_runs.take(instanceId);
    WorldBackupJobs::unlockInstance(instanceId);
    if (run.backedUp + run.failed + run.tooLarge.size() > 0)
        emit finished(instanceId, run.instanceName, run.backedUp, run.tooLarge, run.failed, run.firstError, run.fallbackFrom);
}

}  // namespace Backup
}  // namespace Zero
