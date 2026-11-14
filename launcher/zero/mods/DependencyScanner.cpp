// SPDX-License-Identifier: GPL-3.0-only
#include "DependencyScanner.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QThreadPool>

namespace Zero {
namespace Mods {

QString fileStampOf(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return {};
    return QStringLiteral("%1/%2").arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch());
}

// ---- parse cache -----------------------------------------------------------------------------------------------------------------

ParseCache::ParseCache(ParseFn parse) : m_parse(std::move(parse))
{
    if (!m_parse)
        m_parse = [](const QString& path) { return parseJar(path); };
}

std::shared_ptr<const ModMetadata> ParseCache::get(const QString& path, const QString& stamp)
{
    if (!stamp.isEmpty()) {
        QMutexLocker lock(&m_mutex);
        const auto it = m_entries.constFind(path);
        if (it != m_entries.constEnd() && it->stamp == stamp)
            return it->meta;
    }
    auto meta = std::make_shared<const ModMetadata>(m_parse(path));  // parsed outside the lock: jars can be big
    QMutexLocker lock(&m_mutex);
    ++m_parses;
    if (!stamp.isEmpty())
        m_entries.insert(path, { stamp, meta });
    return meta;
}

void ParseCache::keepOnly(const QSet<QString>& paths)
{
    QMutexLocker lock(&m_mutex);
    for (auto it = m_entries.begin(); it != m_entries.end();)
        it = paths.contains(it.key()) ? std::next(it) : m_entries.erase(it);
}

int ParseCache::parseCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_parses;
}

int ParseCache::size() const
{
    QMutexLocker lock(&m_mutex);
    return int(m_entries.size());
}

// ---- the scan --------------------------------------------------------------------------------------------------------------------

Inspection scanFiles(const ScanRequest& request, ParseCache& cache)
{
    QList<ModMetadata> enabled;
    QList<ModMetadata> disabled;
    QHash<QString, QString> stamps;
    QSet<QString> paths;
    for (const ScanFile& file : request.files) {
        const QString stamp = fileStampOf(file.path);
        stamps.insert(file.path, stamp);
        paths.insert(file.path);
        const auto meta = cache.get(file.path, stamp);
        ModMetadata copy = *meta;
        copy.path = file.path;  // the cache key is the truth, whatever the parser stored
        (file.enabled ? enabled : disabled).append(copy);
    }
    cache.keepOnly(paths);

    Inspection result = inspect(enabled, disabled, request.context);
    for (ModInspection& mod : result.mods)
        mod.stamp = stamps.value(mod.path);
    return result;
}

// ---- scanner ---------------------------------------------------------------------------------------------------------------------

DependencyScanner::DependencyScanner(ScanRunner runner, QObject* parent) : QObject(parent), m_runner(std::move(runner))
{
    if (!m_runner) {
        m_cache = std::make_shared<ParseCache>();
        const std::shared_ptr<ParseCache> cache = m_cache;
        m_runner = [cache](const ScanRequest& request, ScanDone done) {
            // The job never touches the scanner: it only calls `done`, which posts to the application object.
            QThreadPool::globalInstance()->start([cache, request, done]() { done(scanFiles(request, *cache)); });
        };
    }
}

void DependencyScanner::start(const ScanRequest& request)
{
    const quint64 generation = ++m_generation;
    m_scanning = true;
    QPointer<DependencyScanner> self(this);
    m_runner(request, [self, generation](Inspection result) {
        auto* app = QCoreApplication::instance();
        if (!app)
            return;
        QMetaObject::invokeMethod(
            app,
            [self, generation, result]() {
                if (self)
                    self->onDone(generation, result);
            },
            Qt::QueuedConnection);
    });
}

void DependencyScanner::cancel()
{
    ++m_generation;
    m_scanning = false;
}

void DependencyScanner::onDone(quint64 generation, const Inspection& inspection)
{
    if (generation != m_generation)
        return;  // a newer scan (or a cancel) superseded this one
    m_scanning = false;
    emit finished(inspection);
}

}  // namespace Mods
}  // namespace Zero
