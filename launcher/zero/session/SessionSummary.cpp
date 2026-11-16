// SPDX-License-Identifier: GPL-3.0-only
#include "SessionSummary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>

namespace Zero {
namespace Session {

namespace {
LastSessionStore* g_global = nullptr;
}

QStringList screenshotsTakenDuring(const QString& screenshotsDir, qint64 startMs, qint64 endMs)
{
    QDir dir(screenshotsDir);
    if (!dir.exists())
        return {};
    struct Item {
        qint64 modified;
        QString name;
        QString path;
    };
    QList<Item> items;
    const auto infos =
        dir.entryInfoList({ QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg") }, QDir::Files | QDir::NoSymLinks);
    for (const QFileInfo& info : infos) {
        const qint64 m = info.lastModified().toMSecsSinceEpoch();
        if (m >= startMs && m <= endMs + kScreenshotGraceMs)
            items.append({ m, info.fileName(), info.absoluteFilePath() });
    }
    std::sort(items.begin(), items.end(),
              [](const Item& a, const Item& b) { return a.modified != b.modified ? a.modified < b.modified : a.name < b.name; });
    QStringList out;
    for (const auto& i : std::as_const(items))
        out << i.path;
    return out;
}

std::optional<LastSession> buildSummary(const EndedSession& ended, const QString& screenshotsDir)
{
    if (ended.crashed || ended.durationMs() / 1000 < kMinSummarySeconds)
        return std::nullopt;
    LastSession s;
    s.instanceId = ended.instanceId;
    s.instanceName = ended.instanceName;
    s.startedMs = ended.startedMs;
    s.endedMs = ended.endedMs;
    s.screenshots = screenshotsTakenDuring(screenshotsDir, ended.startedMs, ended.endedMs);
    if (s.screenshots.size() > kMaxStoredScreenshots)
        s.screenshots = s.screenshots.mid(0, kMaxStoredScreenshots);
    return s;
}

QByteArray summaryToJson(const LastSession& s)
{
    QJsonObject o;
    o.insert(QStringLiteral("instanceId"), s.instanceId);
    o.insert(QStringLiteral("instanceName"), s.instanceName);
    o.insert(QStringLiteral("startedMs"), double(s.startedMs));
    o.insert(QStringLiteral("endedMs"), double(s.endedMs));
    o.insert(QStringLiteral("screenshots"), QJsonArray::fromStringList(s.screenshots));
    o.insert(QStringLiteral("worldsBackedUp"), s.worldsBackedUp);
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

std::optional<LastSession> summaryFromJson(const QByteArray& json)
{
    QJsonParseError err;
    const auto doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return std::nullopt;
    const QJsonObject o = doc.object();
    LastSession s;
    s.instanceId = o.value(QStringLiteral("instanceId")).toString();
    s.instanceName = o.value(QStringLiteral("instanceName")).toString();
    s.startedMs = qint64(o.value(QStringLiteral("startedMs")).toDouble());
    s.endedMs = qint64(o.value(QStringLiteral("endedMs")).toDouble());
    s.worldsBackedUp = std::max(0, o.value(QStringLiteral("worldsBackedUp")).toInt());
    for (const auto& v : o.value(QStringLiteral("screenshots")).toArray()) {
        if (v.isString() && !v.toString().isEmpty())
            s.screenshots << v.toString();
    }
    if (s.instanceId.isEmpty() || s.endedMs <= 0 || s.endedMs < s.startedMs)
        return std::nullopt;
    if (s.instanceName.isEmpty())
        s.instanceName = s.instanceId;
    return s;
}

LastSessionStore::LastSessionStore(const QString& filePath, QObject* parent) : QObject(parent), m_path(filePath)
{
    if (!g_global)
        g_global = this;
    load();
}

LastSessionStore::~LastSessionStore()
{
    if (g_global == this)
        g_global = nullptr;
}

LastSessionStore* LastSessionStore::global()
{
    return g_global;
}

void LastSessionStore::load()
{
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly))
        return;
    m_current = summaryFromJson(f.readAll());
}

void LastSessionStore::save() const
{
    if (!m_current) {
        QFile::remove(m_path);
        return;
    }
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(summaryToJson(*m_current));
    f.commit();
}

void LastSessionStore::set(const LastSession& session)
{
    m_current = session;
    save();
    emit changed();
}

void LastSessionStore::addBackups(const QString& instanceId, int count)
{
    if (!m_current || m_current->instanceId != instanceId || count <= 0)
        return;
    m_current->worldsBackedUp += count;
    save();
    emit changed();
}

void LastSessionStore::dismiss()
{
    if (!m_current)
        return;
    m_current.reset();
    save();
    emit changed();
}

}  // namespace Session
}  // namespace Zero
