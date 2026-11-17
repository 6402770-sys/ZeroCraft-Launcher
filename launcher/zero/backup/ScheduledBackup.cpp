// SPDX-License-Identifier: GPL-3.0-only
#include "ScheduledBackup.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QSaveFile>
#include <QThreadPool>
#include <algorithm>

namespace Zero {
namespace Backup {

namespace {
ScheduledBackup* g_global = nullptr;

QString tr(const char* text)
{
    return QCoreApplication::translate("Zero::Backup::ScheduledBackup", text);
}
}  // namespace

Schedule scheduleFromString(const QString& text)
{
    const QString t = text.trimmed().toLower();
    if (t == QLatin1String("daily"))
        return Schedule::Daily;
    if (t == QLatin1String("weekly"))
        return Schedule::Weekly;
    return Schedule::Off;
}

QString scheduleToString(Schedule schedule)
{
    switch (schedule) {
        case Schedule::Daily:
            return QStringLiteral("daily");
        case Schedule::Weekly:
            return QStringLiteral("weekly");
        case Schedule::Off:
            break;
    }
    return QStringLiteral("off");
}

qint64 scheduleIntervalMs(Schedule schedule)
{
    switch (schedule) {
        case Schedule::Daily:
            return kDailyMs;
        case Schedule::Weekly:
            return kWeeklyMs;
        case Schedule::Off:
            break;
    }
    return 0;
}

bool isDue(Schedule schedule, qint64 lastRunMs, qint64 nowMs)
{
    const qint64 interval = scheduleIntervalMs(schedule);
    if (interval <= 0)
        return false;
    if (lastRunMs <= 0 || lastRunMs > nowMs)
        return true;
    return nowMs - lastRunMs >= interval;
}

bool needsBackup(qint64 levelDatModifiedMs, qint64 newestBackupMs)
{
    return newestBackupMs <= 0 || levelDatModifiedMs > newestBackupMs;
}

AutoSelection selectScheduled(const QList<ScheduleCandidate>& candidates)
{
    AutoSelection out;
    for (const ScheduleCandidate& c : candidates) {
        if (!needsBackup(c.levelDatModifiedMs, c.newestBackupMs))
            continue;
        (c.sizeBytes > kAutoMaxBytes ? out.tooLarge : out.toBackUp).append(c.world);
    }
    return out;
}

qint64 newestBackupMs(const QStringList& dirs)
{
    qint64 newest = 0;
    for (const QString& dir : dirs) {
        for (const BackupInfo& info : listBackups(dir))
            newest = std::max(newest, info.createdMs);
    }
    return newest;
}

QString summaryText(int backedUp, int failed, int tooLarge, const QString& firstError, const QString& fallbackFrom)
{
    if (backedUp + failed <= 0)
        return {};
    QStringList parts;
    if (backedUp > 0)
        parts << (backedUp == 1 ? tr("1 world backed up") : tr("%1 worlds backed up").arg(backedUp));
    if (failed > 0) {
        QString part = failed == 1 ? tr("1 world failed") : tr("%1 worlds failed").arg(failed);
        if (!firstError.isEmpty())
            part += QStringLiteral(" (") + firstError + QLatin1Char(')');
        parts << part;
    }
    if (tooLarge > 0)
        parts << (tooLarge == 1 ? tr("1 world too large to back up automatically") : tr("%1 worlds too large to back up automatically").arg(tooLarge));
    QString text = tr("Scheduled backup: ") + parts.join(QStringLiteral(", ")) + QLatin1Char('.');
    if (!fallbackFrom.isEmpty())
        text += QLatin1Char(' ') + fallbackMessage(fallbackFrom) + QLatin1Char('.');
    return text;
}

ScheduledBackup::ScheduledBackup(WorldBackupJobs* jobs, QString statePath, Hooks hooks, QObject* parent)
    : ScheduledBackup(jobs, std::move(statePath), std::move(hooks), Timing{}, parent)
{}

ScheduledBackup::ScheduledBackup(WorldBackupJobs* jobs, QString statePath, Hooks hooks, Timing timing, QObject* parent)
    : QObject(parent), m_jobs(jobs), m_path(std::move(statePath)), m_hooks(std::move(hooks)), m_timing(timing)
{
    if (!m_hooks.roots && m_jobs)
        m_hooks.roots = [jobs]() { return jobs->allRoots(); };
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &ScheduledBackup::tick);
    if (m_jobs) {
        connect(m_jobs, &WorldBackupJobs::jobFinished, this, &ScheduledBackup::onJobFinished);
        connect(m_jobs, &WorldBackupJobs::fellBackToDefault, this, &ScheduledBackup::onFellBack);
    }
    load();
    if (!g_global)
        g_global = this;
}

ScheduledBackup::~ScheduledBackup()
{
    if (g_global == this)
        g_global = nullptr;
    releaseLock();
}

ScheduledBackup* ScheduledBackup::global()
{
    return g_global;
}

qint64 ScheduledBackup::now() const
{
    return m_hooks.now ? m_hooks.now() : QDateTime::currentMSecsSinceEpoch();
}

void ScheduledBackup::start()
{
    m_timer.start(m_timing.startDelayMs);
}

void ScheduledBackup::load()
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return;  // unreadable: treated as "never ran"; the next completed run replaces it
    m_lastRunMs = static_cast<qint64>(doc.object().value(QStringLiteral("lastRunMs")).toDouble());
}

void ScheduledBackup::save() const
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("lastRunMs"), static_cast<double>(m_lastRunMs));
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "ScheduledBackup: cannot write" << m_path;
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit())
        qWarning() << "ScheduledBackup: could not save" << m_path;
}

void ScheduledBackup::tick()
{
    m_timer.start(m_timing.recheckMs);  // the next look, whatever happens now
    if (m_busy || !m_jobs)
        return;
    const Schedule schedule = m_hooks.schedule ? m_hooks.schedule() : Schedule::Off;
    if (!isDue(schedule, m_lastRunMs, now()))
        return;
    if (m_hooks.gameActive && m_hooks.gameActive())
        return;  // a game runs or launches: try again at the next check
    beginRun();
}

void ScheduledBackup::beginRun()
{
    m_busy = true;
    m_plans.clear();
    m_planIdx = 0;
    m_current.clear();
    m_inCall = false;
    m_backedUp = m_failed = m_tooLarge = 0;
    m_firstError.clear();
    m_fallbackFrom.clear();

    const QList<Worlds::InstanceSource> sources = m_hooks.instances ? m_hooks.instances() : QList<Worlds::InstanceSource>();
    const QStringList roots = m_hooks.roots ? m_hooks.roots() : QStringList();

    // disk work on the pool; the result is posted to the application object and the pointer is checked there, on the GUI thread
    QPointer<ScheduledBackup> self(this);
    QThreadPool::globalInstance()->start([self, sources, roots]() {
        QList<Plan> plans;
        int tooLarge = 0;
        for (const Worlds::InstanceSource& source : sources) {
            if (source.id.isEmpty() || source.savesDir.isEmpty())
                continue;
            QList<ScheduleCandidate> candidates;
            for (const Worlds::WorldEntry& w : Worlds::scanWorldsDir(source.id, source.name, source.savesDir)) {
                ScheduleCandidate c;
                c.world = w;
                c.levelDatModifiedMs = levelDatModifiedMs(w.folderPath);
                QStringList dirs;
                for (const QString& root : roots)
                    dirs << worldBackupDirIn(root, w.instanceId, w.folderName);
                c.newestBackupMs = newestBackupMs(dirs);
                if (needsBackup(c.levelDatModifiedMs, c.newestBackupMs))
                    c.sizeBytes = Worlds::folderSize(w.folderPath);
                candidates.append(c);
            }
            const AutoSelection selection = selectScheduled(candidates);
            tooLarge += static_cast<int>(selection.tooLarge.size());
            if (!selection.toBackUp.isEmpty())
                plans.append({ source.id, selection.toBackUp });
        }
        auto* app = QCoreApplication::instance();
        if (!app)
            return;
        QMetaObject::invokeMethod(
            app,
            [self, plans, tooLarge]() {
                if (self)
                    self->onPlanned(plans, tooLarge);
            },
            Qt::QueuedConnection);
    });
}

void ScheduledBackup::onPlanned(const QList<Plan>& plans, int tooLarge)
{
    if (!m_busy)
        return;
    m_plans = plans;
    m_planIdx = 0;
    m_tooLarge = tooLarge;
    advance();
}

void ScheduledBackup::advance()
{
    // One world at a time. A refused job (jobFinished fires inside backup()) counts as failed and the queue goes on.
    for (;;) {
        if (!m_busy)
            return;
        if (m_planIdx >= m_plans.size()) {
            endRun(true);
            return;
        }
        Plan& plan = m_plans[m_planIdx];
        if (!m_lockHeld) {
            if (m_hooks.gameActive && m_hooks.gameActive()) {
                endRun(false);  // a game started meanwhile: stop, the next check retries (finished worlds are not backed up twice)
                return;
            }
            if (WorldBackupJobs::isInstanceLocked(plan.instanceId)) {
                ++m_planIdx;  // another service (automatic backup of a session that just ended) works on it
                continue;
            }
            WorldBackupJobs::lockInstance(plan.instanceId);
            m_lockHeld = true;
        }
        if (plan.queue.isEmpty()) {
            releaseLock();
            ++m_planIdx;
            continue;
        }
        const Worlds::WorldEntry entry = plan.queue.takeFirst();
        m_current = entry.key();
        m_inCall = true;
        const bool started = m_jobs->backup(entry, Kind::Auto);
        m_inCall = false;
        if (started)
            return;  // the result arrives in onJobFinished
        m_current.clear();
    }
}

void ScheduledBackup::onJobFinished(const QString& key, bool ok, const QString& message, bool automatic)
{
    if (!m_busy || !automatic || m_current.isEmpty() || key != m_current)
        return;
    if (ok) {
        ++m_backedUp;
    } else {
        ++m_failed;
        if (m_firstError.isEmpty())
            m_firstError = message;
    }
    if (m_inCall)
        return;  // refused synchronously: advance() continues
    m_current.clear();
    advance();
}

void ScheduledBackup::onFellBack(const QString& key, const QString& chosenFolder)
{
    if (m_busy && !m_current.isEmpty() && key == m_current)
        m_fallbackFrom = chosenFolder;
}

void ScheduledBackup::releaseLock()
{
    if (!m_lockHeld || m_planIdx >= m_plans.size())
        return;
    m_lockHeld = false;
    WorldBackupJobs::unlockInstance(m_plans[m_planIdx].instanceId);
}

void ScheduledBackup::endRun(bool completed)
{
    releaseLock();
    m_busy = false;
    m_plans.clear();
    m_planIdx = 0;
    m_current.clear();
    if (completed) {
        m_lastRunMs = now();
        save();
    }
    emit finished(m_backedUp, m_failed, m_tooLarge, m_firstError, m_fallbackFrom);
}

}  // namespace Backup
}  // namespace Zero
