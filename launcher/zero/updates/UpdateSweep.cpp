// SPDX-License-Identifier: GPL-3.0-only
#include "UpdateSweep.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>
#include <QDebug>

#include <algorithm>
#include <memory>

namespace Zero {
namespace Updates {

namespace {

QString T(const char* text)
{
    return QCoreApplication::translate("Zero::Updates", text);
}

QString countText(int count, const char* one, const char* many)
{
    return count == 1 ? T(one) : T(many).arg(count);
}

bool sameEntries(const QList<UpdateEntry>& a, const QList<UpdateEntry>& b)
{
    return a == b;
}

bool sameResults(const QList<InstanceUpdates>& a, const QList<InstanceUpdates>& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (int i = 0; i < a.size(); ++i) {
        if (a.at(i).instanceId != b.at(i).instanceId || a.at(i).instanceName != b.at(i).instanceName ||
            !sameEntries(a.at(i).entries, b.at(i).entries)) {
            return false;
        }
    }
    return true;
}

}  // namespace

QString captionFor(const QList<UpdateEntry>& entries)
{
    int mods = 0;
    int shaders = 0;
    int resources = 0;
    for (const UpdateEntry& entry : entries) {
        if (entry.kind == QLatin1String(kKindShaderPack)) {
            ++shaders;
        } else if (entry.kind == QLatin1String(kKindResourcePack)) {
            ++resources;
        } else {
            ++mods;
        }
    }
    QStringList parts;
    if (mods > 0) {
        parts << countText(mods, "1 mod", "%1 mods");
    }
    if (shaders > 0) {
        parts << countText(shaders, "1 shader pack", "%1 shader packs");
    }
    if (resources > 0) {
        parts << countText(resources, "1 resource pack", "%1 resource packs");
    }
    return parts.join(QStringLiteral(" · "));
}

UpdateSweep::UpdateSweep(const QString& path, Hooks hooks, QObject* parent) : UpdateSweep(path, std::move(hooks), Timing(), parent) {}

UpdateSweep::UpdateSweep(const QString& path, Hooks hooks, Timing timing, QObject* parent)
    : QObject(parent), m_path(path), m_hooks(std::move(hooks)), m_timing(timing)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, &UpdateSweep::onTimeout);
    m_autoTimer.setSingleShot(true);
    connect(&m_autoTimer, &QTimer::timeout, this, &UpdateSweep::autoTick);
    load();
}

UpdateSweep::~UpdateSweep()
{
    if (m_check) {
        disconnect(m_check, nullptr, this, nullptr);
        m_check->abort();
        m_check->deleteLater();
        m_check = nullptr;
    }
}

qint64 UpdateSweep::now() const
{
    return m_hooks.now ? m_hooks.now() : QDateTime::currentMSecsSinceEpoch();
}

QList<InstanceSource> UpdateSweep::sources() const
{
    return m_hooks.instances ? m_hooks.instances() : QList<InstanceSource>();
}

std::optional<InstanceSource> UpdateSweep::sourceOf(const QString& id) const
{
    for (const InstanceSource& source : sources()) {
        if (source.id == id) {
            return source;
        }
    }
    return std::nullopt;
}

bool UpdateSweep::anyRunning() const
{
    const QList<InstanceSource> all = sources();
    return std::any_of(all.begin(), all.end(), [](const InstanceSource& source) { return source.running; });
}

// ---- persistence ------------------------------------------------------------------------------------------------------------------

void UpdateSweep::load()
{
    QFile file(m_path);
    if (!file.exists()) {
        return;
    }
    bool ok = false;
    QJsonObject root;
    if (file.open(QIODevice::ReadOnly)) {
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error == QJsonParseError::NoError && doc.isObject() && doc.object().value(QStringLiteral("instances")).isArray()) {
            root = doc.object();
            ok = true;
        }
        file.close();
    }
    if (!ok) {
        // keep the unreadable file for the user instead of overwriting it with the next save
        const QString corrupt = m_path + QStringLiteral(".corrupt");
        QFile::remove(corrupt);
        if (!QFile::rename(m_path, corrupt)) {
            qWarning() << "UpdateSweep: could not move aside the unreadable" << m_path;
        }
        return;
    }

    m_checkedAt = static_cast<qint64>(root.value(QStringLiteral("checkedAt")).toDouble());
    for (const QJsonValue& value : root.value(QStringLiteral("instances")).toArray()) {
        const QJsonObject o = value.toObject();
        InstanceUpdates record;
        record.instanceId = o.value(QStringLiteral("id")).toString();
        if (record.instanceId.isEmpty()) {
            continue;
        }
        record.instanceName = o.value(QStringLiteral("name")).toString();
        record.checkedAt = static_cast<qint64>(o.value(QStringLiteral("checkedAt")).toDouble());
        record.error = o.value(QStringLiteral("error")).toString();
        for (const QJsonValue& item : o.value(QStringLiteral("entries")).toArray()) {
            const QJsonObject e = item.toObject();
            UpdateEntry entry;
            entry.key = e.value(QStringLiteral("key")).toString();
            entry.name = e.value(QStringLiteral("name")).toString();
            entry.newVersion = e.value(QStringLiteral("version")).toString();
            entry.kind = e.value(QStringLiteral("kind")).toString(QString::fromLatin1(kKindMod));
            if (!entry.key.isEmpty()) {
                record.entries.append(entry);
            }
        }
        if (record.entries.isEmpty() && record.error.isEmpty()) {
            continue;
        }
        m_records.append(record);
    }
    if (!sources().isEmpty()) {
        const qsizetype before = m_records.size();
        pruneVanished();
        if (m_records.size() != before) {
            save();
        }
    }
}

void UpdateSweep::save() const
{
    QJsonArray instances;
    for (const InstanceUpdates& record : m_records) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), record.instanceId);
        o.insert(QStringLiteral("name"), record.instanceName);
        o.insert(QStringLiteral("checkedAt"), record.checkedAt);
        if (!record.error.isEmpty()) {
            o.insert(QStringLiteral("error"), record.error);
        }
        QJsonArray entries;
        for (const UpdateEntry& entry : record.entries) {
            QJsonObject e;
            e.insert(QStringLiteral("key"), entry.key);
            e.insert(QStringLiteral("name"), entry.name);
            e.insert(QStringLiteral("version"), entry.newVersion);
            e.insert(QStringLiteral("kind"), entry.kind);
            entries.append(e);
        }
        o.insert(QStringLiteral("entries"), entries);
        instances.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), kFormat);
    root.insert(QStringLiteral("checkedAt"), m_checkedAt);
    root.insert(QStringLiteral("instances"), instances);

    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "UpdateSweep: cannot write" << m_path;
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qWarning() << "UpdateSweep: could not save" << m_path;
    }
}

void UpdateSweep::pruneVanished()
{
    QSet<QString> alive;
    for (const InstanceSource& source : sources()) {
        alive.insert(source.id);
    }
    m_records.erase(std::remove_if(m_records.begin(), m_records.end(),
                                   [&alive](const InstanceUpdates& record) { return !alive.contains(record.instanceId); }),
                    m_records.end());
}

// ---- what is known ----------------------------------------------------------------------------------------------------------------

QList<InstanceUpdates> UpdateSweep::results() const
{
    QHash<QString, QString> names;
    for (const InstanceSource& source : sources()) {
        names.insert(source.id, source.name);
    }
    QList<InstanceUpdates> out;
    for (const InstanceUpdates& record : m_records) {
        const auto name = names.constFind(record.instanceId);
        if (name == names.constEnd() || !record.hasUpdates()) {
            continue;
        }
        InstanceUpdates copy = record;
        copy.instanceName = name.value();  // a renamed instance shows its current name
        out.append(copy);
    }
    std::sort(out.begin(), out.end(), [](const InstanceUpdates& a, const InstanceUpdates& b) {
        const int byName = a.instanceName.compare(b.instanceName, Qt::CaseInsensitive);
        return byName != 0 ? byName < 0 : a.instanceId < b.instanceId;
    });
    return out;
}

int UpdateSweep::totalUpdates() const
{
    int total = 0;
    for (const InstanceUpdates& record : results()) {
        total += int(record.entries.size());
    }
    return total;
}

QString UpdateSweep::currentName() const
{
    return m_current ? m_currentName : QString();
}

UpdateSweep::State UpdateSweep::stateOf(const QString& instanceId) const
{
    if (m_current && m_current->id == instanceId) {
        return m_updating.contains(instanceId) ? State::Updating : State::Checking;
    }
    if (m_updating.contains(instanceId)) {
        return State::Updating;
    }
    for (const Work& work : m_queue) {
        if (work.id == instanceId) {
            return State::Queued;
        }
    }
    return State::Idle;
}

InstanceUpdates* UpdateSweep::recordFor(const QString& id)
{
    for (InstanceUpdates& record : m_records) {
        if (record.instanceId == id) {
            return &record;
        }
    }
    return nullptr;
}

// ---- the queue --------------------------------------------------------------------------------------------------------------------

void UpdateSweep::enqueue(const Work& work, bool front)
{
    if (!front) {
        m_queue.append(work);
        return;
    }
    // Updates run before the checks of a running sweep, in the order they were asked for.
    qsizetype at = 0;
    while (at < m_queue.size() && m_queue.at(at).kind == Work::Kind::Update) {
        ++at;
    }
    m_queue.insert(at, work);
}

void UpdateSweep::pump()
{
    if (m_current) {
        return;
    }
    while (!m_queue.isEmpty()) {
        const Work work = m_queue.takeFirst();
        const bool counted = work.kind == Work::Kind::Check && m_sweepPending.contains(work.id);
        const std::optional<InstanceSource> source = sourceOf(work.id);
        const QString name = source ? source->name : QString();

        auto skip = [&](const QString& why) {
            if (work.kind == Work::Kind::Update && !why.isEmpty()) {
                emit problem(work.id, why);
            }
            if (counted) {
                m_sweepPending.remove(work.id);
                ++m_sweepDone;
            }
            emit stateChanged(work.id);
        };

        if (!source) {
            skip(QString());
            continue;
        }
        if (m_updating.contains(work.id)) {
            skip(QString());  // its job runs: nothing to check or start
            continue;
        }
        if (source->managed) {
            skip(QString());
            continue;
        }
        if (source->running) {
            skip(tr("%1 is running, so it was not updated.").arg(name));
            continue;
        }
        if (source->busy) {
            skip(tr("%1 is still downloading, so it was not updated.").arg(name));
            continue;
        }
        if (startCheckOf(work, *source)) {
            return;
        }
    }
    finishSweepIfDone();
}

bool UpdateSweep::startCheckOf(const Work& work, const InstanceSource& source)
{
    ModsUi::ModUpdateCheck* check = m_hooks.checkFactory ? m_hooks.checkFactory(work.id, this) : nullptr;
    if (!check) {
        // nothing to check in this instance (no mod with metadata): no entries, no error
        const QList<InstanceUpdates> before = results();
        m_records.erase(std::remove_if(m_records.begin(), m_records.end(),
                                       [&work](const InstanceUpdates& record) { return record.instanceId == work.id; }),
                        m_records.end());
        if (work.kind == Work::Kind::Check && m_sweepPending.remove(work.id)) {
            ++m_sweepDone;
        }
        save();
        emit stateChanged(work.id);
        if (!sameResults(before, results())) {
            emit resultsChanged();
        }
        return false;
    }
    m_current = work;
    m_currentName = source.name;
    m_check = check;
    connect(check, &ModsUi::ModUpdateCheck::finished, this, &UpdateSweep::onCheckFinished);
    m_timeout.start(m_timing.timeoutMs);
    if (work.kind == Work::Kind::Check && m_sweepPending.contains(work.id)) {
        emit progress(m_sweepDone, m_sweepTotal, source.name);
    }
    emit stateChanged(work.id);
    check->start();
    return true;
}

void UpdateSweep::finishCurrent()
{
    if (!m_current) {
        return;
    }
    const QString id = m_current->id;
    m_current.reset();
    m_currentName.clear();
    emit stateChanged(id);
}

void UpdateSweep::onTimeout()
{
    if (!m_check || !m_current) {
        return;
    }
    ModsUi::ModUpdateCheck* check = m_check;
    disconnect(check, nullptr, this, nullptr);
    check->abort();
    check->deleteLater();
    m_check = nullptr;
    const Work work = *m_current;
    const QString name = m_currentName;
    recordCheck(work.id, name, false, tr("The check took too long."), {});
    if (work.kind == Work::Kind::Update) {
        emit problem(work.id, tr("Could not check %1 for updates: the check took too long.").arg(name));
    }
    finishCurrent();
    if (work.kind == Work::Kind::Check && m_sweepPending.remove(work.id)) {
        ++m_sweepDone;
    }
    pump();
    finishSweepIfDone();
}

void UpdateSweep::onCheckFinished(bool ok, const QString& error)
{
    if (!m_check || !m_current || sender() != m_check) {
        return;
    }
    m_timeout.stop();
    ModsUi::ModUpdateCheck* check = m_check;
    const QList<ModsUi::ModUpdate> found = ok ? check->updates() : QList<ModsUi::ModUpdate>();
    disconnect(check, nullptr, this, nullptr);
    check->deleteLater();
    m_check = nullptr;

    const Work work = *m_current;
    const QString name = m_currentName;
    const QString reason = error.isEmpty() ? tr("no answer") : error;
    recordCheck(work.id, name, ok, reason, found);

    if (work.kind == Work::Kind::Check) {
        finishCurrent();
        if (m_sweepPending.remove(work.id)) {
            ++m_sweepDone;
        }
        pump();
        finishSweepIfDone();
        return;
    }
    if (!ok) {
        emit problem(work.id, tr("Could not check %1 for updates: %2").arg(name, reason));
        finishCurrent();
        pump();
        return;
    }
    if (found.isEmpty()) {
        finishCurrent();  // nothing left to update (it was updated elsewhere in the meantime)
        pump();
        return;
    }
    runApplier(work, name, found);
}

void UpdateSweep::recordCheck(const QString& id, const QString& name, bool ok, const QString& error, const QList<ModsUi::ModUpdate>& found)
{
    const QList<InstanceUpdates> before = results();
    InstanceUpdates fresh;
    fresh.instanceId = id;
    fresh.instanceName = name;
    fresh.checkedAt = now();
    QSet<QString> seen;
    if (ok) {
        for (const ModsUi::ModUpdate& update : found) {
            if (update.key.isEmpty() || seen.contains(update.key)) {
                continue;
            }
            seen.insert(update.key);
            UpdateEntry entry;
            entry.key = update.key;
            entry.name = update.name.isEmpty() ? update.key : update.name;
            entry.newVersion = update.newVersion;
            entry.kind = QString::fromLatin1(kKindMod);
            fresh.entries.append(entry);
        }
    } else {
        fresh.error = error.isEmpty() ? tr("no answer") : error;
    }
    m_records.erase(std::remove_if(m_records.begin(), m_records.end(), [&id](const InstanceUpdates& record) { return record.instanceId == id; }),
                    m_records.end());
    if (!fresh.entries.isEmpty() || !fresh.error.isEmpty()) {
        m_records.append(fresh);
    }
    save();
    if (!sameResults(before, results())) {
        emit resultsChanged();
    }
}

void UpdateSweep::dropEntries(const QString& id)
{
    const qsizetype size = m_records.size();
    m_records.erase(std::remove_if(m_records.begin(), m_records.end(), [&id](const InstanceUpdates& record) { return record.instanceId == id; }),
                    m_records.end());
    if (m_records.size() != size) {
        save();
        emit resultsChanged();
    }
}

void UpdateSweep::runApplier(const Work& work, const QString& name, const QList<ModsUi::ModUpdate>& updates)
{
    const QString id = work.id;
    const std::optional<InstanceSource> source = sourceOf(id);
    // the check took a while: the instance may have been launched meanwhile
    if (!m_hooks.apply || !source || source->running || source->busy || source->managed) {
        if (m_hooks.apply && source) {
            emit problem(id, source->running ? tr("%1 is running, so it was not updated.").arg(name)
                                             : tr("%1 is still downloading, so it was not updated.").arg(name));
        }
        finishCurrent();
        pump();
        return;
    }
    m_updating.insert(id);
    emit stateChanged(id);

    QPointer<UpdateSweep> self(this);
    auto startedFlag = std::make_shared<bool>(false);
    auto doneFlag = std::make_shared<bool>(false);
    std::function<void()> started = [self, startedFlag]() {
        if (*startedFlag) {
            return;
        }
        *startedFlag = true;
        if (!self) {
            return;
        }
        self->finishCurrent();  // the next check may start; the downloads go on in the background
        self->pump();
    };
    std::function<void(bool, const QString&)> done = [self, startedFlag, doneFlag, started, id](bool ok, const QString& error) {
        if (*doneFlag) {
            return;
        }
        *doneFlag = true;
        started();  // never before `started`
        if (!self) {
            return;
        }
        self->m_updating.remove(id);
        if (ok) {
            self->dropEntries(id);  // used up
        } else if (!error.isEmpty()) {
            emit self->problem(id, error);
        }
        emit self->stateChanged(id);
    };
    m_hooks.apply(id, updates, started, done);
}

// ---- check of all instances -------------------------------------------------------------------------------------------------------

void UpdateSweep::checkNow()
{
    if (m_sweepActive) {
        return;
    }
    QList<Work> works;
    m_sweepPending.clear();
    for (const InstanceSource& source : sources()) {
        if (source.running || source.busy || source.managed || m_updating.contains(source.id)) {
            continue;
        }
        Work work;
        work.kind = Work::Kind::Check;
        work.id = source.id;
        works.append(work);
        m_sweepPending.insert(source.id);
    }
    m_sweepActive = true;
    m_sweepTotal = int(works.size());
    m_sweepDone = 0;
    emit started();
    for (const Work& work : std::as_const(works)) {
        enqueue(work, false);
        emit stateChanged(work.id);
    }
    pump();
    finishSweepIfDone();
}

void UpdateSweep::finishSweepIfDone()
{
    if (!m_sweepActive || !m_sweepPending.isEmpty()) {
        return;
    }
    m_sweepActive = false;
    const QList<InstanceUpdates> before = results();
    pruneVanished();
    m_checkedAt = now();
    save();
    if (!sameResults(before, results())) {
        emit resultsChanged();
    }
    emit finished();
}

// ---- updating ---------------------------------------------------------------------------------------------------------------------

void UpdateSweep::updateInstance(const QString& instanceId)
{
    if (instanceId.isEmpty() || m_updating.contains(instanceId)) {
        return;
    }
    if (m_current && m_current->id == instanceId && m_current->kind == Work::Kind::Update) {
        return;
    }
    for (const Work& work : std::as_const(m_queue)) {
        if (work.id == instanceId && work.kind == Work::Kind::Update) {
            return;
        }
    }
    Work work;
    work.kind = Work::Kind::Update;
    work.id = instanceId;
    enqueue(work, true);
    emit stateChanged(instanceId);
    pump();
}

void UpdateSweep::updateAll()
{
    for (const InstanceUpdates& record : results()) {
        updateInstance(record.instanceId);
    }
}

// ---- automatic check --------------------------------------------------------------------------------------------------------------

void UpdateSweep::start()
{
    scheduleAuto(m_timing.startDelayMs);
}

void UpdateSweep::scheduleAuto(int ms)
{
    m_autoTimer.start(qMax(1, ms));
}

void UpdateSweep::autoTick()
{
    m_waitingForGame = false;
    if (m_hooks.autoEnabled && !m_hooks.autoEnabled()) {
        scheduleAuto(m_timing.recheckMs);  // the setting may be switched on later
        return;
    }
    const qint64 age = now() - m_checkedAt;
    if (m_checkedAt > 0 && age >= 0 && age < m_timing.intervalMs) {
        scheduleAuto(int(qMin<qint64>(m_timing.recheckMs, m_timing.intervalMs - age)));
        return;
    }
    if (anyRunning()) {
        m_waitingForGame = true;
        scheduleAuto(m_timing.retryMs);
        return;
    }
    if (!m_sweepActive) {
        checkNow();
    }
    scheduleAuto(m_timing.recheckMs);
}

void UpdateSweep::onGameStateChanged()
{
    if (m_waitingForGame && !anyRunning()) {
        scheduleAuto(m_timing.afterGameMs);
    }
}

}  // namespace Updates
}  // namespace Zero
