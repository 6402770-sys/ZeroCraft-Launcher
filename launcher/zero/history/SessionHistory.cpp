// SPDX-License-Identifier: GPL-3.0-only
#include "SessionHistory.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace Zero {
namespace History {

namespace {
SessionHistory* g_global = nullptr;
}

std::optional<Outcome> classify(bool success, bool aborted, bool gameStarted)
{
    if (gameStarted) {
        if (success)
            return Outcome::Played;
        return aborted ? Outcome::Stopped : Outcome::Crashed;
    }
    if (success || aborted)
        return std::nullopt;
    return Outcome::Failed;
}

QString outcomeToString(Outcome outcome)
{
    switch (outcome) {
        case Outcome::Played:
            return QStringLiteral("played");
        case Outcome::Stopped:
            return QStringLiteral("stopped");
        case Outcome::Crashed:
            return QStringLiteral("crashed");
        case Outcome::Failed:
            return QStringLiteral("failed");
    }
    return QStringLiteral("played");
}

std::optional<Outcome> outcomeFromString(const QString& text)
{
    for (Outcome o : { Outcome::Played, Outcome::Stopped, Outcome::Crashed, Outcome::Failed }) {
        if (text == outcomeToString(o))
            return o;
    }
    return std::nullopt;
}

SessionHistory::SessionHistory(const QString& filePath, Clock clock, NameLookup nameLookup, QObject* parent)
    : QObject(parent), m_path(filePath), m_clock(std::move(clock)), m_nameLookup(std::move(nameLookup))
{
    if (!g_global)
        g_global = this;
}

SessionHistory::~SessionHistory()
{
    if (g_global == this)
        g_global = nullptr;
}

SessionHistory* SessionHistory::global()
{
    return g_global;
}

qint64 SessionHistory::now() const
{
    return m_clock ? m_clock() : QDateTime::currentMSecsSinceEpoch();
}

const QList<Session>& SessionHistory::sessions() const
{
    ensureLoaded();
    return m_sessions;
}

void SessionHistory::ensureLoaded() const
{
    if (m_loaded)
        return;
    m_loaded = true;
    m_sessions.clear();

    QFile file(m_path);
    if (!file.exists())
        return;

    bool ok = false;
    QJsonArray array;
    if (file.open(QIODevice::ReadOnly)) {
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error == QJsonParseError::NoError && doc.isObject() && doc.object().value(QStringLiteral("sessions")).isArray()) {
            array = doc.object().value(QStringLiteral("sessions")).toArray();
            ok = true;
        }
        file.close();
    }
    if (!ok) {
        // keep the unreadable file for the user instead of overwriting it with the next save
        const QString corrupt = m_path + QStringLiteral(".corrupt");
        QFile::remove(corrupt);
        if (!QFile::rename(m_path, corrupt))
            qWarning() << "SessionHistory: could not move aside the unreadable" << m_path;
        return;
    }

    for (const QJsonValue& value : array) {
        const QJsonObject o = value.toObject();
        const QString id = o.value(QStringLiteral("instanceId")).toString();
        const auto outcome = outcomeFromString(o.value(QStringLiteral("outcome")).toString());
        if (id.isEmpty() || !outcome || !o.value(QStringLiteral("started")).isDouble() || !o.value(QStringLiteral("ended")).isDouble())
            continue;
        Session s;
        s.instanceId = id;
        s.instanceName = o.value(QStringLiteral("instanceName")).toString();
        s.startedMs = static_cast<qint64>(o.value(QStringLiteral("started")).toDouble());
        s.endedMs = static_cast<qint64>(o.value(QStringLiteral("ended")).toDouble());
        s.outcome = *outcome;
        if (o.value(QStringLiteral("exitCode")).isDouble())
            s.exitCode = o.value(QStringLiteral("exitCode")).toInt();
        m_sessions.append(s);
        if (m_sessions.size() >= kMaxSessions)
            break;
    }
}

void SessionHistory::save() const
{
    QJsonArray array;
    for (const Session& s : m_sessions) {
        QJsonObject o;
        o.insert(QStringLiteral("instanceId"), s.instanceId);
        o.insert(QStringLiteral("instanceName"), s.instanceName);
        o.insert(QStringLiteral("started"), s.startedMs);
        o.insert(QStringLiteral("ended"), s.endedMs);
        o.insert(QStringLiteral("outcome"), outcomeToString(s.outcome));
        if (s.exitCode)
            o.insert(QStringLiteral("exitCode"), *s.exitCode);
        array.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("sessions"), array);

    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "SessionHistory: cannot write" << m_path;
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit())
        qWarning() << "SessionHistory: could not save" << m_path;
}

void SessionHistory::record(const Session& session)
{
    ensureLoaded();
    m_sessions.prepend(session);
    while (m_sessions.size() > kMaxSessions)
        m_sessions.removeLast();
    save();
    emit changed();
}

void SessionHistory::onPhaseChanged(const QString& instanceId, Phase phase)
{
    switch (phase) {
        case Phase::Preparing: {
            Pending p;
            p.preparingMs = now();
            m_pending.insert(instanceId, p);
            break;
        }
        case Phase::Running: {
            auto it = m_pending.find(instanceId);
            if (it == m_pending.end()) {
                Pending p;
                p.preparingMs = now();
                it = m_pending.insert(instanceId, p);
            }
            it->runningMs = now();
            break;
        }
        case Phase::Idle:
            break;
    }
}

void SessionHistory::onStoppedUnexpectedly(const QString& instanceId, int exitCode)
{
    auto it = m_pending.find(instanceId);
    if (it != m_pending.end())
        it->exitCode = exitCode;
}

void SessionHistory::onLaunchEnded(const QString& instanceId, bool success, bool aborted, bool gameStarted)
{
    const qint64 ended = now();
    const Pending pending = m_pending.take(instanceId);
    const auto outcome = classify(success, aborted, gameStarted);
    if (!outcome)
        return;

    Session s;
    s.instanceId = instanceId;
    s.endedMs = ended;
    s.outcome = *outcome;
    const qint64 prepared = pending.preparingMs ? pending.preparingMs : ended;
    s.startedMs = (*outcome == Outcome::Failed) ? prepared : pending.runningMs.value_or(prepared);
    if (*outcome == Outcome::Crashed)
        s.exitCode = pending.exitCode;
    if (m_nameLookup)
        s.instanceName = m_nameLookup(instanceId);
    if (s.instanceName.isEmpty())
        s.instanceName = instanceId;
    record(s);
}

}  // namespace History
}  // namespace Zero
