// SPDX-License-Identifier: GPL-3.0-only
#include "ScreenshotScanner.h"

#include <QCoreApplication>
#include <QDirListing>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <algorithm>

namespace Zero {
namespace Gallery {

QList<ScreenshotEntry> scanScreenshotsDir(const QString& instanceId, const QString& instanceName, const QString& dir)
{
    QList<ScreenshotEntry> out;
    if (dir.isEmpty() || !QFileInfo(dir).isDir())
        return out;
    for (const auto& item : QDirListing(dir, QDirListing::IteratorFlag::FilesOnly)) {
        if (item.isSymLink())
            continue;
        const QFileInfo info = item.fileInfo();
        if (info.isJunction() || !info.isFile())
            continue;
        const QString suffix = info.suffix().toLower();
        if (suffix != QLatin1String("png") && suffix != QLatin1String("jpg") && suffix != QLatin1String("jpeg"))
            continue;
        ScreenshotEntry e;
        e.instanceId = instanceId;
        e.instanceName = instanceName;
        e.path = info.absoluteFilePath();
        e.fileName = info.fileName();
        e.modifiedMs = info.lastModified().toMSecsSinceEpoch();
        e.sizeBytes = info.size();
        out.append(e);
    }
    std::sort(out.begin(), out.end(), [](const ScreenshotEntry& a, const ScreenshotEntry& b) { return a.fileName < b.fileName; });
    return out;
}

QString sortModeToString(SortMode mode)
{
    switch (mode) {
        case SortMode::Oldest:
            return QStringLiteral("oldest");
        case SortMode::Instance:
            return QStringLiteral("instance");
        case SortMode::Newest:
            break;
    }
    return QStringLiteral("newest");
}

SortMode sortModeFromString(const QString& text)
{
    if (text == QLatin1String("oldest"))
        return SortMode::Oldest;
    if (text == QLatin1String("instance"))
        return SortMode::Instance;
    return SortMode::Newest;
}

bool matchesSearch(const ScreenshotEntry& entry, const QString& text)
{
    const QString needle = text.trimmed();
    if (needle.isEmpty())
        return true;
    return entry.fileName.contains(needle, Qt::CaseInsensitive) || entry.instanceName.contains(needle, Qt::CaseInsensitive);
}

void sortScreenshots(QList<ScreenshotEntry>& list, SortMode mode)
{
    auto tie = [](const ScreenshotEntry& a, const ScreenshotEntry& b) {
        const int c = a.fileName.localeAwareCompare(b.fileName);
        return c != 0 ? c < 0 : a.path < b.path;
    };
    std::stable_sort(list.begin(), list.end(), [&](const ScreenshotEntry& a, const ScreenshotEntry& b) {
        switch (mode) {
            case SortMode::Oldest:
                if (a.modifiedMs != b.modifiedMs)
                    return a.modifiedMs < b.modifiedMs;
                return tie(a, b);
            case SortMode::Instance: {
                const int c = a.instanceName.localeAwareCompare(b.instanceName);
                if (c != 0)
                    return c < 0;
                if (a.modifiedMs != b.modifiedMs)
                    return a.modifiedMs > b.modifiedMs;
                return tie(a, b);
            }
            case SortMode::Newest:
                break;
        }
        if (a.modifiedMs != b.modifiedMs)
            return a.modifiedMs > b.modifiedMs;
        return tie(a, b);
    });
}

ScreenshotScanner::ScreenshotScanner(SourceProvider provider, QObject* parent) : QObject(parent), m_provider(std::move(provider)) {}

void ScreenshotScanner::setScanning(bool scanning)
{
    if (m_scanning == scanning)
        return;
    m_scanning = scanning;
    emit scanningChanged(scanning);
}

void ScreenshotScanner::rescan()
{
    const quint64 generation = ++m_generation;
    const QList<InstanceSource> sources = m_provider ? m_provider() : QList<InstanceSource>();
    setScanning(true);

    // The pool thread never touches `this` or reads the QPointer: it posts to the application object (which outlives every
    // scanner) and the pointer is checked there, on the GUI thread.
    QPointer<ScreenshotScanner> self(this);
    QThreadPool::globalInstance()->start([self, sources, generation]() {
        QList<ScreenshotEntry> found;
        for (const InstanceSource& source : sources)
            found += scanScreenshotsDir(source.id, source.name, source.screenshotsDir);
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

void ScreenshotScanner::onScanned(quint64 generation, const QList<ScreenshotEntry>& found)
{
    if (generation != m_generation)
        return;  // a newer rescan() superseded this one
    m_entries = found;
    m_hasScanned = true;
    emit listChanged();
    setScanning(false);
}

}  // namespace Gallery
}  // namespace Zero
