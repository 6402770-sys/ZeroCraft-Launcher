// SPDX-License-Identifier: GPL-3.0-only
#include "ShaderPackIcons.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <list>
#include <memory>

#include "Application.h"
#include "modplatform/flame/FlameAPI.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "tasks/Task.h"

namespace Zero::ShaderIcons {

QString keyOf(ModPlatform::ResourceProvider provider, const QString& projectId)
{
    const QString id = projectId.trimmed();
    if (id.isEmpty()) {
        return {};
    }
    return (provider == ModPlatform::ResourceProvider::FLAME ? QStringLiteral("curseforge:") : QStringLiteral("modrinth:")) + id;
}

Ids collectIds(const QList<Ref>& refs, const QHash<QString, QString>& known, const QSet<QString>& asked)
{
    Ids ids;
    for (const Ref& ref : refs) {
        const QString key = keyOf(ref.provider, ref.projectId);
        if (key.isEmpty() || known.contains(key) || asked.contains(key)) {
            continue;
        }
        QStringList& list = ref.provider == ModPlatform::ResourceProvider::FLAME ? ids.curseforge : ids.modrinth;
        const QString id = ref.projectId.trimmed();
        if (!list.contains(id)) {
            list.append(id);
        }
    }
    ids.modrinth.sort();
    ids.curseforge.sort();
    return ids;
}

QHash<QString, QString> parseModrinth(const QByteArray& json)
{
    QHash<QString, QString> out;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isArray()) {
        return out;
    }
    for (const QJsonValue& value : doc.array()) {
        const QJsonObject project = value.toObject();
        const QString id = project.value(QStringLiteral("id")).toString();
        const QString icon = project.value(QStringLiteral("icon_url")).toString();
        if (!id.isEmpty() && !icon.isEmpty()) {
            out.insert(keyOf(ModPlatform::ResourceProvider::MODRINTH, id), icon);
            const QString slug = project.value(QStringLiteral("slug")).toString();  // an entry written with the slug still finds its icon
            if (!slug.isEmpty()) {
                out.insert(keyOf(ModPlatform::ResourceProvider::MODRINTH, slug), icon);
            }
        }
    }
    return out;
}

QHash<QString, QString> parseCurseForge(const QByteArray& json)
{
    QHash<QString, QString> out;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        return out;
    }
    for (const QJsonValue& value : doc.object().value(QStringLiteral("data")).toArray()) {
        const QJsonObject mod = value.toObject();
        const QString id = QString::number(mod.value(QStringLiteral("id")).toInteger());
        const QJsonObject logo = mod.value(QStringLiteral("logo")).toObject();
        QString icon = logo.value(QStringLiteral("thumbnailUrl")).toString();
        if (icon.isEmpty()) {
            icon = logo.value(QStringLiteral("url")).toString();
        }
        if (id != QLatin1String("0") && !icon.isEmpty()) {
            out.insert(keyOf(ModPlatform::ResourceProvider::FLAME, id), icon);
        }
    }
    return out;
}

namespace {

constexpr int kBatch = 50;

using Done = std::function<void(const QHash<QString, QString>&)>;

/// Tasks in flight (nothing else owns them); each removes itself when it ends.
std::list<Task::Ptr>& inFlight()
{
    static std::list<Task::Ptr> tasks;
    return tasks;
}

void run(const std::pair<Task::Ptr, QByteArray*>& started, QHash<QString, QString> (*parse)(const QByteArray&), const Done& done)
{
    const Task::Ptr task = started.first;
    QByteArray* response = started.second;
    if (!task) {
        return;
    }
    inFlight().push_back(task);
    Task* raw = task.get();
    QObject::connect(raw, &Task::succeeded, raw, [response, parse, done]() {
        const auto urls = parse(*response);
        if (!urls.isEmpty()) {
            done(urls);
        }
    });
    QObject::connect(raw, &Task::finished, raw, [raw]() {
        // Deferred: the task is still emitting.
        auto& tasks = inFlight();
        for (auto it = tasks.begin(); it != tasks.end(); ++it) {
            if (it->get() == raw) {
                const Task::Ptr keep = *it;
                tasks.erase(it);
                QMetaObject::invokeMethod(raw, [keep]() {}, Qt::QueuedConnection);  // released after the event loop turn
                break;
            }
        }
    });
    task->start();
}

}  // namespace

Lookup networkLookup()
{
    return [](const Ids& ids, Done done) {
        for (int i = 0; i < ids.modrinth.size(); i += kBatch) {
            run(ModrinthAPI::get().getProjects(ids.modrinth.mid(i, kBatch)), &parseModrinth, done);
        }
        for (int i = 0; i < ids.curseforge.size(); i += kBatch) {
            run(FlameAPI::get().getProjects(ids.curseforge.mid(i, kBatch)), &parseCurseForge, done);
        }
    };
}

QHash<QString, QString>& sessionUrls()
{
    static QHash<QString, QString> urls;
    return urls;
}

QSet<QString>& sessionAsked()
{
    static QSet<QString> asked;
    return asked;
}

}  // namespace Zero::ShaderIcons
