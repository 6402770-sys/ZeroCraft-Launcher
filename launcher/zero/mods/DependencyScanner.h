// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Mod Intelligence: the asynchronous scan behind the Mods page's Dependency Inspector. A scan reads the jars of the mods
// the page lists (read-only), keeps what it parsed in a cache keyed by path + size + modification time (a jar that did not change is not
// parsed again) and runs `inspect()`. It runs on the global thread pool; every `start()` supersedes the earlier ones: only the newest
// answer is delivered (generation token, the same pattern as ModHealthScanner). The runner is injectable so tests need no pool.
#pragma once

#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>

#include <functional>
#include <memory>

#include "zero/mods/DependencyInspector.h"

namespace Zero {
namespace Mods {

/// "<size>/<modification time in ms>" of a file; "" when it does not exist. Same shape as the page's `Row::fileStamp`.
QString fileStampOf(const QString& path);

struct ScanFile {
    QString path;  ///< absolute path of the mod file (`x.jar`, or `x.jar.disabled` when switched off)
    bool enabled = true;
};

struct ScanRequest {
    QList<ScanFile> files;
    InstanceContext context;
};

/// The parsed jars of earlier scans. Thread-safe (a superseded scan may still run while the next one starts).
class ParseCache {
   public:
    using ParseFn = std::function<ModMetadata(const QString& path)>;
    explicit ParseCache(ParseFn parse = {});  ///< empty = `parseJar`

    /// The metadata of `path`; parsed only when the cache has no entry with this `stamp` ("" = unknown stamp: always parsed, never kept).
    std::shared_ptr<const ModMetadata> get(const QString& path, const QString& stamp);
    /// Forgets every path not in `paths`.
    void keepOnly(const QSet<QString>& paths);
    int parseCount() const;  ///< how many parses ran (for tests)
    int size() const;

   private:
    struct Entry {
        QString stamp;
        std::shared_ptr<const ModMetadata> meta;
    };
    ParseFn m_parse;
    mutable QMutex m_mutex;
    QHash<QString, Entry> m_entries;
    int m_parses = 0;
};

/// The synchronous core (what the pool job runs): stat + parse (through `cache`) every file, then `inspect()`; the result's `stamp`s are
/// the file stamps read here.
Inspection scanFiles(const ScanRequest& request, ParseCache& cache);

using ScanDone = std::function<void(Inspection)>;  ///< may be called from any thread, once
/// Starts the work of one request and calls `done` with its result. The default runs `scanFiles` on the global thread pool.
using ScanRunner = std::function<void(const ScanRequest&, ScanDone)>;

class DependencyScanner : public QObject {
    Q_OBJECT
   public:
    explicit DependencyScanner(ScanRunner runner = {}, QObject* parent = nullptr);

    /// Starts a scan; an earlier one that has not answered yet is superseded (its answer is dropped).
    void start(const ScanRequest& request);
    /// Drops the answer of a running scan.
    void cancel();

    bool isScanning() const { return m_scanning; }
    quint64 generation() const { return m_generation; }
    /// The cache of the default runner (null with an injected runner).
    ParseCache* cache() const { return m_cache.get(); }

   signals:
    void finished(const Zero::Mods::Inspection& inspection);

   private:
    void onDone(quint64 generation, const Inspection& inspection);

    ScanRunner m_runner;
    std::shared_ptr<ParseCache> m_cache;
    quint64 m_generation = 0;
    bool m_scanning = false;
};

}  // namespace Mods
}  // namespace Zero
