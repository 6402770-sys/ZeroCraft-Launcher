// SPDX-License-Identifier: GPL-3.0-only
#include "FavoriteStore.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>
#include <algorithm>
#include "Application.h"
#include "FileSystem.h"

namespace Zero {
namespace Favorites {

namespace {
constexpr int kVersion = 1;

QJsonArray sortedArray(const QSet<QString>& keys)
{
    QStringList list(keys.cbegin(), keys.cend());
    std::sort(list.begin(), list.end());
    QJsonArray out;
    for (const QString& k : list)
        out.append(k);
    return out;
}

QSet<QString> readKeys(const QJsonValue& value)
{
    QSet<QString> out;
    for (const QJsonValue& v : value.toArray()) {
        const QString key = v.toString().trimmed();
        if (!key.isEmpty())
            out.insert(key);
    }
    return out;
}
}  // namespace

FavoriteStore::FavoriteStore(const QString& filePath, QObject* parent) : QObject(parent), m_path(filePath)
{
    load();
}

FavoriteStore* FavoriteStore::instance()
{
    static QPointer<FavoriteStore> store;
    if (!store && APPLICATION)
        store = new FavoriteStore(FS::PathCombine(APPLICATION->dataRoot(), "zero", "favorites.json"), APPLICATION);
    return store;
}

bool FavoriteStore::contains(Kind kind, const QString& key) const
{
    return (kind == Kind::World ? m_worlds : m_servers).contains(key);
}

bool FavoriteStore::set(Kind kind, const QString& key, bool favorite)
{
    if (key.trimmed().isEmpty())
        return true;
    QSet<QString>& keys = setFor(kind);
    if (keys.contains(key) == favorite)
        return true;
    if (favorite)
        keys.insert(key);
    else
        keys.remove(key);
    const bool saved = save();
    emit changed(kind, key, favorite);
    return saved;
}

bool FavoriteStore::toggle(Kind kind, const QString& key)
{
    const bool now = !contains(kind, key);
    set(kind, key, now);
    return now;
}

void FavoriteStore::load()
{
    QFile file(m_path);
    if (!file.exists())
        return;
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = file.errorString();
        qWarning() << "ZERO favorites: cannot read" << m_path << m_error;
        return;
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        // Keep the broken file for inspection and start empty (never overwrite it silently).
        const QString aside = m_path + QStringLiteral(".corrupt");
        QFile::remove(aside);
        QFile::rename(m_path, aside);
        m_error = parseError.errorString();
        qWarning() << "ZERO favorites: unreadable file moved to" << aside;
        return;
    }
    const QJsonObject root = doc.object();
    m_worlds = readKeys(root.value(QStringLiteral("worlds")));
    m_servers = readKeys(root.value(QStringLiteral("servers")));
}

bool FavoriteStore::save()
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QJsonObject root;
    root.insert(QStringLiteral("version"), kVersion);
    root.insert(QStringLiteral("worlds"), sortedArray(m_worlds));
    root.insert(QStringLiteral("servers"), sortedArray(m_servers));
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        m_error = file.errorString();
        qWarning() << "ZERO favorites: cannot save" << m_path << m_error;
        return false;
    }
    m_error.clear();
    return true;
}

}  // namespace Favorites
}  // namespace Zero
