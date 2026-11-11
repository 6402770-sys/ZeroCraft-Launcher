// SPDX-License-Identifier: GPL-3.0-only
#include "WorldScanner.h"

#include <QDateTime>
#include <QDir>
#include <QDirListing>
#include <QFileInfo>
#include <QMetaObject>
#include <QCoreApplication>
#include <QPointer>
#include <QThreadPool>
#include <algorithm>
#include "minecraft/World.h"

namespace Zero {
namespace Worlds {

QList<WorldEntry> scanWorldsDir(const QString& instanceId, const QString& instanceName, const QString& savesDir)
{
    QList<WorldEntry> out;
    const QDir saves(savesDir);
    if (!saves.exists())
        return out;

    const QFileInfoList folders = saves.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& folder : folders) {
        World world(folder);
        if (!world.isValid())  // no level.dat
            continue;
        world.loadMetadata();
        if (!world.isValid())  // level.dat unreadable
            continue;

        WorldEntry e;
        e.instanceId = instanceId;
        e.instanceName = instanceName;
        e.folderPath = folder.absoluteFilePath();
        e.folderName = folder.fileName();
        e.name = world.name().isEmpty() ? e.folderName : world.name();
        e.versionName = world.versionName();
        const GameType type = world.gameType();
        e.gameTypeCode = type.type;
        e.gameMode = type.type == GameType::Unknown ? QString() : type.toTranslatedString();
        e.hardcore = world.isHardcore();
        const QDateTime played = world.lastPlayed();
        e.lastPlayedMs = played.isValid() ? played.toMSecsSinceEpoch()
                                          : QFileInfo(e.folderPath + QStringLiteral("/level.dat")).lastModified().toMSecsSinceEpoch();
        e.seed = world.seed();
        e.iconPath = world.iconFile();
        out.append(e);
    }
    return out;
}

qint64 folderSize(const QString& path)
{
    // Own traversal instead of QDirListing's Recursive flag: Qt does not stop at NTFS junctions, and a link back up the tree
    // would never end. Links (symbolic or junction) are skipped, files and folders alike.
    qint64 total = 0;
    QStringList pending{ path };
    while (!pending.isEmpty()) {
        const QString dir = pending.takeLast();
        for (const auto& entry : QDirListing(dir, QDirListing::IteratorFlag::IncludeHidden)) {
            if (entry.isSymLink() || entry.fileInfo().isJunction())
                continue;
            if (entry.isDir())
                pending.append(entry.filePath());
            else if (entry.isFile())
                total += entry.size();
        }
    }
    return total;
}

QString sortModeToString(SortMode mode)
{
    switch (mode) {
        case SortMode::Name:
            return QStringLiteral("name");
        case SortMode::Size:
            return QStringLiteral("size");
        case SortMode::Instance:
            return QStringLiteral("instance");
        case SortMode::Recent:
            break;
    }
    return QStringLiteral("recent");
}

SortMode sortModeFromString(const QString& text)
{
    if (text == QLatin1String("name"))
        return SortMode::Name;
    if (text == QLatin1String("size"))
        return SortMode::Size;
    if (text == QLatin1String("instance"))
        return SortMode::Instance;
    return SortMode::Recent;
}

bool matchesSearch(const WorldEntry& entry, const QString& text)
{
    const QString needle = text.trimmed();
    if (needle.isEmpty())
        return true;
    for (const QString& field : { entry.name, entry.folderName, entry.instanceName, entry.versionName }) {
        if (field.contains(needle, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

void sortWorlds(QList<WorldEntry>& worlds, SortMode mode)
{
    auto byName = [](const WorldEntry& a, const WorldEntry& b) {
        const int c = a.name.localeAwareCompare(b.name);
        return c != 0 ? c < 0 : a.key() < b.key();
    };
    std::stable_sort(worlds.begin(), worlds.end(), [&](const WorldEntry& a, const WorldEntry& b) {
        switch (mode) {
            case SortMode::Name:
                return byName(a, b);
            case SortMode::Size:
                if (a.sizeBytes != b.sizeBytes)
                    return a.sizeBytes > b.sizeBytes;  // -1 (unknown) ends up last
                return byName(a, b);
            case SortMode::Instance: {
                const int c = a.instanceName.localeAwareCompare(b.instanceName);
                if (c != 0)
                    return c < 0;
                if (a.lastPlayedMs != b.lastPlayedMs)
                    return a.lastPlayedMs > b.lastPlayedMs;
                return byName(a, b);
            }
            case SortMode::Recent:
                break;
        }
        if (a.lastPlayedMs != b.lastPlayedMs)
            return a.lastPlayedMs > b.lastPlayedMs;
        return byName(a, b);
    });
}

WorldScanner::WorldScanner(SourceProvider provider, QObject* parent) : QObject(parent), m_provider(std::move(provider)) {}

void WorldScanner::setScanning(bool scanning)
{
    if (m_scanning == scanning)
        return;
    m_scanning = scanning;
    emit scanningChanged(scanning);
}

void WorldScanner::rescan()
{
    const quint64 generation = ++m_generation;
    const QList<InstanceSource> sources = m_provider ? m_provider() : QList<InstanceSource>();
    m_pendingSizes = 0;
    setScanning(true);

    // The pool thread never touches `this` or reads the QPointer (not thread-safe against a concurrent delete): it posts to the
    // application object, which outlives every scanner, and the pointer is checked there, on the GUI thread.
    QPointer<WorldScanner> self(this);
    QThreadPool::globalInstance()->start([self, sources, generation]() {
        QList<WorldEntry> found;
        for (const InstanceSource& source : sources)
            found += scanWorldsDir(source.id, source.name, source.savesDir);
        auto* app = QCoreApplication::instance();
        if (!app)
            return;
        QMetaObject::invokeMethod(
            app,
            [self, generation, found]() {
                if (self)
                    self->onScanned(generation, found);
            },
            Qt::QueuedConnection);
    });
}

void WorldScanner::onScanned(quint64 generation, const QList<WorldEntry>& found)
{
    if (generation != m_generation)
        return;  // a newer rescan() superseded this one

    QList<WorldEntry> next = found;
    for (WorldEntry& e : next) {  // keep known sizes so a refresh does not flicker; they are recomputed below
        const auto it = m_index.constFind(e.key());
        if (it != m_index.constEnd())
            e.sizeBytes = m_entries.at(it.value()).sizeBytes;
    }
    m_entries = next;
    m_index.clear();
    for (int i = 0; i < m_entries.size(); ++i)
        m_index.insert(m_entries.at(i).key(), i);
    m_hasScanned = true;
    m_pendingSizes = m_entries.size();
    emit worldsChanged();

    if (m_entries.isEmpty()) {
        setScanning(false);
        return;
    }

    QPointer<WorldScanner> self(this);
    for (const WorldEntry& e : std::as_const(m_entries)) {
        const QString key = e.key();
        const QString path = e.folderPath;
        QThreadPool::globalInstance()->start([self, generation, key, path]() {
            const qint64 bytes = folderSize(path);
            auto* app = QCoreApplication::instance();
            if (!app)
                return;
            QMetaObject::invokeMethod(
                app,
                [self, generation, key, bytes]() {
                    if (self)
                        self->onSize(generation, key, bytes);
                },
                Qt::QueuedConnection);
        });
    }
}

void WorldScanner::onSize(quint64 generation, const QString& key, qint64 bytes)
{
    if (generation != m_generation)
        return;
    const auto it = m_index.constFind(key);
    if (it != m_index.constEnd()) {
        m_entries[it.value()].sizeBytes = bytes;
        emit sizeReady(key, bytes);
    }
    if (--m_pendingSizes <= 0)
        setScanning(false);
}

}  // namespace Worlds
}  // namespace Zero
