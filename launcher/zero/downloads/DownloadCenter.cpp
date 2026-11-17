// SPDX-License-Identifier: GPL-3.0-only
#include "DownloadCenter.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>

namespace Zero {
namespace Downloads {

namespace {

DownloadCenter* g_global = nullptr;

constexpr int kTickMs = 100;           ///< timer that flushes throttled jobChanged() signals
constexpr qint64 kMinEmitGapMs = 80;   ///< jobChanged() per job at most every 80 ms (about 10 a second)
constexpr qint64 kSpeedWindowMs = 3000;
constexpr qint64 kSampleGapMs = 100;   ///< progress samples closer than this are merged

}  // namespace

DownloadCenter* DownloadCenter::global()
{
    return g_global;
}

DownloadCenter::DownloadCenter(const QString& historyPath, Clock clock, QObject* parent)
    : QObject(parent), m_path(historyPath), m_clock(std::move(clock))
{
    if (!g_global)
        g_global = this;
    m_tick.setInterval(kTickMs);
    connect(&m_tick, &QTimer::timeout, this, &DownloadCenter::tick);
    load();
}

DownloadCenter::~DownloadCenter()
{
    if (g_global == this)
        g_global = nullptr;
    // the tasks outlive us only through their own owners: never call back into a destroyed center
    for (const auto& job : m_jobs)
        if (job->spec.task)
            QObject::disconnect(job->spec.task.get(), nullptr, this, nullptr);
}

qint64 DownloadCenter::now() const
{
    return m_clock ? m_clock() : QDateTime::currentMSecsSinceEpoch();
}

// --- starting, cancelling -------------------------------------------------------------------------------------------------

QString DownloadCenter::start(DownloadJob spec)
{
    if (!spec.task) {
        qWarning() << "DownloadCenter::start: job without a task:" << spec.title;
        return {};
    }
    auto job = std::make_shared<Job>();
    job->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job->spec = std::move(spec);
    job->lastEmitMs = now() - kMinEmitGapMs;  // the first change is announced at once
    job->samples.append({ now(), 0 });  // baseline of the speed average
    const QString id = job->id;
    Task::Ptr task = job->spec.task;

    m_jobs.insert(id, job);
    m_order.append(id);
    connectTask(job);
    if (!m_tick.isActive())
        m_tick.start();
    emit jobAdded(id);
    emit activeCountChanged(activeCount());

    task->start();  // asynchronous for every real task; a synchronous end is handled by finish()
    return id;
}

bool DownloadCenter::cancel(const QString& id)
{
    const auto it = m_jobs.constFind(id);
    if (it == m_jobs.constEnd() || (*it)->finished || !(*it)->spec.task)
        return false;
    Task::Ptr task = (*it)->spec.task;
    if (!task->canAbort())
        return false;
    (*it)->cancelRequested = true;
    task->abort();  // the task reports through aborted() (or failed()): finish() ends the job as Cancelled
    return true;
}

void DownloadCenter::cancelAll()
{
    const QStringList ids = m_order;  // finish() edits m_order
    for (const QString& id : ids)
        cancel(id);
}

bool DownloadCenter::isBusy(const QString& instanceId) const
{
    if (instanceId.isEmpty())
        return false;
    for (const QString& id : m_order)
        if (m_jobs.value(id)->spec.instanceId == instanceId)
            return true;
    return false;
}

// --- task signals ---------------------------------------------------------------------------------------------------------

void DownloadCenter::connectTask(const std::shared_ptr<Job>& job)
{
    Task* task = job->spec.task.get();
    const QString id = job->id;
    auto lookup = [this, id]() -> std::shared_ptr<Job> { return m_jobs.value(id); };

    connect(task, &Task::stepProgress, this, [this, lookup](const TaskStepProgress& step) {
        if (auto j = lookup(); j && !j->finished)
            onStepProgress(j, step);
    });
    connect(task, &Task::progress, this, [this, lookup](qint64 current, qint64 total) {
        if (auto j = lookup(); j && !j->finished) {
            j->progressCurrent = current;
            j->progressTotal = total;
            recompute(*j);
            touch(*j);
        }
    });
    connect(task, &Task::status, this, [this, lookup](QString text) {
        if (auto j = lookup(); j && !j->finished) {
            j->status = text;
            touch(*j);
        }
    });
    connect(task, &Task::details, this, [this, lookup](QString text) {
        if (auto j = lookup(); j && !j->finished) {
            j->details = text;
            touch(*j);
        }
    });
    connect(task, &Task::succeeded, this, [this, id]() { finish(id, DownloadResult::State::Done, {}); });
    connect(task, &Task::failed, this, [this, id](QString reason) { finish(id, DownloadResult::State::Failed, reason); });
    connect(task, &Task::aborted, this, [this, id]() { finish(id, DownloadResult::State::Cancelled, {}); });
}

void DownloadCenter::onStepProgress(const std::shared_ptr<Job>& job, const TaskStepProgress& step)
{
    Step& s = job->steps[step.uid];
    s.current = step.current;
    s.total = step.total;
    recompute(*job);
    touch(*job);
}

void DownloadCenter::recompute(Job& job)
{
    qint64 done = 0;
    qint64 total = 0;
    for (const Step& s : std::as_const(job.steps)) {
        if (s.total <= 0)
            continue;  // size unknown (yet)
        total += s.total;
        done += std::clamp<qint64>(s.current, 0, s.total);
    }
    job.bytesDone = done;
    job.bytesTotal = total;

    double fraction = -1;
    if (total >= kMinBytes)
        fraction = double(done) / double(total);
    else if (job.progressTotal > 0)
        fraction = double(std::clamp<qint64>(job.progressCurrent, 0, job.progressTotal)) / double(job.progressTotal);
    else if (total > 0)
        fraction = double(done) / double(total);
    if (fraction >= 0)
        job.fraction = std::max(job.fraction, std::clamp(fraction, 0.0, 1.0));  // the bar never runs backwards

    // speed samples: one per ~100 ms, only when the byte count moved; the baseline sample (index 0) is never merged
    const qint64 t = now();
    if (job.samples.isEmpty() || job.samples.last().bytes != done) {
        if (job.samples.size() > 1 && t - job.samples.last().ms < kSampleGapMs)
            job.samples.last().bytes = done;
        else
            job.samples.append({ t, done });
        // drop what is older than the anchor of the window
        const qint64 windowStart = t - kSpeedWindowMs;
        int anchor = 0;
        for (int i = 0; i < job.samples.size(); ++i)
            if (job.samples.at(i).ms <= windowStart)
                anchor = i;
        if (anchor > 0)
            job.samples.remove(0, anchor);
    }
}

double DownloadCenter::speedOf(const Job& job) const
{
    if (job.samples.isEmpty())
        return 0;
    const qint64 t = now();
    const qint64 windowStart = t - kSpeedWindowMs;
    Sample anchor = job.samples.first();
    for (const Sample& s : job.samples)
        if (s.ms <= windowStart)
            anchor = s;
    const qint64 dt = t - anchor.ms;
    const qint64 delta = job.bytesDone - anchor.bytes;
    if (dt <= 0 || delta <= 0)
        return 0;
    return double(delta) * 1000.0 / double(dt);
}

ActiveDownload DownloadCenter::snapshot(const Job& job) const
{
    ActiveDownload a;
    a.id = job.id;
    a.title = job.spec.title;
    a.instanceId = job.spec.instanceId;
    a.instanceName = job.spec.instanceName;
    a.kind = job.spec.kind;
    a.bytesDone = job.bytesDone;
    a.bytesTotal = job.bytesTotal;
    a.fraction = job.fraction;
    a.speed = speedOf(job);
    a.status = job.details.isEmpty() ? job.status : job.details;
    a.canCancel = job.spec.task && job.spec.task->canAbort();
    return a;
}

QList<ActiveDownload> DownloadCenter::activeJobs() const
{
    QList<ActiveDownload> out;
    for (const QString& id : m_order)
        out.append(snapshot(*m_jobs.value(id)));
    return out;
}

std::optional<ActiveDownload> DownloadCenter::activeJob(const QString& id) const
{
    const auto it = m_jobs.constFind(id);
    if (it == m_jobs.constEnd())
        return std::nullopt;
    return snapshot(**it);
}

void DownloadCenter::touch(Job& job)
{
    job.dirty = true;
    const qint64 t = now();
    if (t - job.lastEmitMs >= kMinEmitGapMs) {
        job.lastEmitMs = t;
        job.dirty = false;
        emit jobChanged(job.id);
    }
}

void DownloadCenter::tick()
{
    // flushes what the throttle held back, and refreshes the speed of stalled jobs
    const QStringList ids = m_order;
    for (const QString& id : ids) {
        const auto job = m_jobs.value(id);
        if (!job || job->finished)
            continue;
        const qint64 t = now();
        if (t - job->lastEmitMs >= kMinEmitGapMs && (job->dirty || speedOf(*job) > 0)) {
            job->lastEmitMs = t;
            job->dirty = false;
            emit jobChanged(id);
        }
    }
    if (m_order.isEmpty())
        m_tick.stop();
}

// --- finishing ------------------------------------------------------------------------------------------------------------

void DownloadCenter::finish(const QString& id, DownloadResult::State state, const QString& error)
{
    const auto it = m_jobs.find(id);
    if (it == m_jobs.end() || (*it)->finished)
        return;  // failed() and aborted() can both fire: the first one counts
    std::shared_ptr<Job> job = *it;
    job->finished = true;
    if (job->cancelRequested && state == DownloadResult::State::Failed)
        state = DownloadResult::State::Cancelled;  // the user asked to stop it: how the task unwinds is not their problem

    Task::Ptr task = job->spec.task;
    if (task)
        QObject::disconnect(task.get(), nullptr, this, nullptr);

    DownloadResult result;
    result.state = state;
    result.error = state == DownloadResult::State::Failed ? (error.isEmpty() && task ? task->failReason() : error) : QString();
    if (task)
        result.warnings = task->warnings();
    if (job->bytesTotal >= kMinBytes)
        result.bytes = state == DownloadResult::State::Done ? job->bytesTotal : job->bytesDone;

    HistoryEntry entry;
    entry.id = job->id;
    entry.title = job->spec.title;
    entry.instanceId = job->spec.instanceId;
    entry.instanceName = job->spec.instanceName;
    entry.kind = job->spec.kind;
    entry.state = state;
    entry.error = result.error;
    entry.bytes = result.bytes;
    entry.finishedAt = QDateTime::fromMSecsSinceEpoch(now(), Qt::UTC);

    m_jobs.erase(it);
    m_order.removeAll(id);
    m_history.prepend(entry);
    while (m_history.size() > kMaxHistory)
        m_history.removeLast();
    save();
    if (m_order.isEmpty())
        m_tick.stop();

    auto onFinished = job->spec.onFinished;
    emit jobFinished(id);
    emit historyChanged();
    emit activeCountChanged(activeCount());
    if (onFinished)
        onFinished(result);
    // `job` (and with it the task pointer) goes away here, after every slot has run
}

// --- history file ---------------------------------------------------------------------------------------------------------

void DownloadCenter::clearHistory()
{
    if (m_history.isEmpty())
        return;
    m_history.clear();
    save();
    emit historyChanged();
}

void DownloadCenter::load()
{
    m_history.clear();
    if (m_path.isEmpty())
        return;
    QFile file(m_path);
    if (!file.exists())
        return;

    bool ok = false;
    QJsonArray array;
    if (file.open(QIODevice::ReadOnly)) {
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error == QJsonParseError::NoError && doc.isObject() && doc.object().value(QStringLiteral("entries")).isArray()) {
            array = doc.object().value(QStringLiteral("entries")).toArray();
            ok = true;
        }
        file.close();
    }
    if (!ok) {
        // keep the unreadable file for the user instead of overwriting it with the next save
        const QString corrupt = m_path + QStringLiteral(".corrupt");
        QFile::remove(corrupt);
        if (!QFile::rename(m_path, corrupt))
            qWarning() << "DownloadCenter: could not move aside the unreadable" << m_path;
        return;
    }

    for (const QJsonValue& value : array) {
        const QJsonObject o = value.toObject();
        const QString id = o.value(QStringLiteral("id")).toString();
        const QString title = o.value(QStringLiteral("title")).toString();
        if (id.isEmpty() || title.isEmpty())
            continue;
        HistoryEntry e;
        e.id = id;
        e.title = title;
        e.instanceId = o.value(QStringLiteral("instanceId")).toString();
        e.instanceName = o.value(QStringLiteral("instanceName")).toString();
        e.kind = kindFromString(o.value(QStringLiteral("kind")).toString());
        e.state = stateFromString(o.value(QStringLiteral("state")).toString());
        e.error = o.value(QStringLiteral("error")).toString();
        e.bytes = qint64(o.value(QStringLiteral("bytes")).toDouble());
        e.finishedAt = QDateTime::fromString(o.value(QStringLiteral("finishedAt")).toString(), Qt::ISODate);
        if (e.finishedAt.isValid())
            e.finishedAt = e.finishedAt.toUTC();
        m_history.append(e);
        if (m_history.size() >= kMaxHistory)
            break;
    }
}

void DownloadCenter::save() const
{
    if (m_path.isEmpty())
        return;
    QJsonArray array;
    for (const HistoryEntry& e : m_history) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), e.id);
        o.insert(QStringLiteral("title"), e.title);
        o.insert(QStringLiteral("instanceId"), e.instanceId);
        o.insert(QStringLiteral("instanceName"), e.instanceName);
        o.insert(QStringLiteral("kind"), kindToString(e.kind));
        o.insert(QStringLiteral("state"), stateToString(e.state));
        o.insert(QStringLiteral("error"), e.error);
        o.insert(QStringLiteral("bytes"), e.bytes);
        o.insert(QStringLiteral("finishedAt"), e.finishedAt.toUTC().toString(Qt::ISODate));
        array.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("entries"), array);

    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "DownloadCenter: cannot write" << m_path;
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit())
        qWarning() << "DownloadCenter: could not save" << m_path;
}

}  // namespace Downloads
}  // namespace Zero
