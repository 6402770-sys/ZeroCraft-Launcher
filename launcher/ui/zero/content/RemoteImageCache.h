// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - remote images of the Discover page (project icons, gallery and featured artwork) and local image files
// (install icons). docs/zero/CONTENT.md, "Remote images".
//
// URL -> disk cache (production fetcher: metacache base "ZeroContent", key sha1(url)) -> decode on QThreadPool with
// QImageReader::setScaledSize to one of the width buckets 64 / 128 / 400 / 1280 -> QCache LRU (~64 MB). At most 6 fetches are
// in flight, one per URL however many cards ask; visible requests are served before background ones. Requesters are held as
// QPointer and can cancel everything they asked for. Failures are remembered for the session until retryFailed().
// Nothing is decoded on the GUI thread and nothing blocks: request() returns what is already there (or a null pixmap) and
// imageReady() follows. The fetcher is injectable, so tests need neither the network nor an Application.
#pragma once

#include <QByteArray>
#include <QCache>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>
#include <QPixmap>
#include <QPointer>
#include <QSet>
#include <QString>
#include <functional>

class QThread;

namespace Zero {

class RemoteImageCache : public QObject {
    Q_OBJECT
   public:
    /// Visible requests are fetched before Background ones (prefetch); within a class the newest request goes first.
    enum class Priority { Background = 0, Visible = 1 };

    /// What a fetch produced: a local file (production: the metacache file) or the bytes themselves (tests).
    struct FetchResult {
        bool ok = false;
        QString filePath;
        QByteArray data;
    };
    /// Downloads `url` and calls `done` exactly once on the GUI thread (synchronously or later).
    using Fetcher = std::function<void(const QString& url, std::function<void(const FetchResult&)> done)>;

    static constexpr int kMaxInFlight = 6;
    static constexpr qint64 kMaxCostBytes = 64LL * 1024 * 1024;  ///< QCache budget: decoded width x height x 4 bytes
    static constexpr int kFadeMs = 160;                           ///< fade-in of a picture that just arrived
    static constexpr int kBucketCount = 4;
    static constexpr int kBuckets[kBucketCount] = { 64, 128, 400, 1280 };

    /// The smallest bucket (px wide) that holds `widthPx` device pixels; 64 for <= 0, 1280 above 1280.
    static int bucketFor(int widthPx);
    /// bucketFor(ceil(logicalWidth * dpr)).
    static int bucketFor(qreal logicalWidth, qreal dpr);

    /// An empty `fetcher` is the production network fetcher (it fails every request when there is no Application).
    explicit RemoteImageCache(Fetcher fetcher = {}, QObject* parent = nullptr);
    ~RemoteImageCache() override;

    /// The application-wide cache (created on first use, owned by the QCoreApplication).
    static RemoteImageCache* instance();

    /// The picture of http(s) `url` at `bucket` (see bucketFor), or - while that size is still being prepared - the nearest
    /// size already decoded, or a null pixmap. The fetch and the decode are started once; imageReady(url, bucket) follows.
    /// `requester` (may be null = never cancelled) is remembered as a QPointer and never dereferenced.
    /// Non-http(s) strings and URLs that failed are never fetched.
    QPixmap request(const QString& url, int bucket, QObject* requester = nullptr, Priority priority = Priority::Visible);
    /// The same for a local image file (scaled through the same pool; no network, no disk copy). imageReady() carries `path`.
    QPixmap requestLocal(const QString& path, int bucket, QObject* requester = nullptr, Priority priority = Priority::Visible);

    /// What is cached for `id` (url or path) without requesting anything: exactly `bucket`, else the nearest size, else null.
    QPixmap peek(const QString& id, int bucket) const;
    /// True when exactly this bucket is cached.
    bool isCached(const QString& id, int bucket) const;

    /// Drops everything `requester` asked for that has not been fetched yet (a queued URL nobody else wants leaves the
    /// queue). A fetch already running finishes into the disk cache but is not decoded for it.
    void cancel(QObject* requester);

    bool hasFailed(const QString& id) const { return m_failed.contains(id); }
    /// Forgets every failure and asks again for the URLs somebody was waiting for ("Refresh").
    void retryFailed();
    /// Forgets the decoded sizes (and a remembered failure) of `id`, e.g. when a local file changed.
    void invalidate(const QString& id);

    /// 0..1: how far the first picture of `id` has faded in since it arrived (1 when long ago, unknown or motion is off).
    qreal fadeIn(const QString& id) const;
    /// True while at least one picture is still fading in.
    bool fading() const;

    int inFlightCount() const { return int(m_inFlight.size()); }
    int queuedCount() const { return int(m_queue.size()); }
    int fetchCount() const { return m_fetches; }      ///< fetcher calls so far
    int decodeCount() const { return m_decodes; }     ///< decodes started so far
    qint64 cachedCost() const { return m_cache.totalCost(); }
    void setMaxCostBytes(qint64 bytes) { m_cache.setMaxCost(bytes); }
    /// The thread the last decode ran on (tests: it is never the GUI thread).
    QThread* lastDecodeThread() const { return m_lastDecodeThread; }

    /// Decodes `source` scaled to `bucket` px wide (aspect kept, never enlarged). Pure and thread-safe; null on failure.
    static QImage decode(const FetchResult& source, int bucket);

   signals:
    /// A picture of `id` (url or path) is ready at `bucket`.
    void imageReady(const QString& url, int bucket);
    /// `id` could not be fetched or decoded; it is not tried again until retryFailed().
    void imageFailed(const QString& url);

   private:
    struct Interest {
        QPointer<QObject> requester;
        bool anonymous = true;  ///< no requester: never cancelled, never dead
        int bucket = 0;
        Priority priority = Priority::Visible;
        bool alive() const { return anonymous || !requester.isNull(); }
    };
    struct Queued {
        QString id;
        Priority priority;
    };

    static QString keyFor(const QString& id, int bucket);
    QPixmap requestImpl(const QString& id, int bucket, QObject* requester, Priority priority, bool local);
    void addInterest(const QString& id, const Interest& interest);
    void enqueue(const QString& id, Priority priority);
    void schedulePump();
    void pump();
    void onFetched(const QString& id, const FetchResult& result);
    void startDecode(const QString& id, int bucket);
    void onDecoded(const QString& id, int bucket, const QImage& image, QThread* thread);
    void fail(const QString& id);

    Fetcher m_fetcher;
    QCache<QString, QPixmap> m_cache{ kMaxCostBytes };
    QHash<QString, QList<Interest>> m_interests;  ///< id -> who waits for the fetch
    QHash<QString, FetchResult> m_sources;        ///< id -> what the fetch produced (or the local file)
    QHash<QString, QList<Interest>> m_failedInterests;
    QSet<QString> m_decoding;                     ///< keys being decoded
    QSet<QString> m_inFlight;                     ///< ids being fetched
    QList<Queued> m_queue;                        ///< waiting for a fetch slot
    QSet<QString> m_failed;
    QElapsedTimer m_clock;
    QHash<QString, qint64> m_arrivedAt;
    int m_fetches = 0;
    int m_decodes = 0;
    bool m_pumpScheduled = false;
    QThread* m_lastDecodeThread = nullptr;
};

}  // namespace Zero
