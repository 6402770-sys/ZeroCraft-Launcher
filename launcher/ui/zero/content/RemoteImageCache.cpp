// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - remote images of the Discover page.
#include "RemoteImageCache.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QMetaObject>
#include <QThread>
#include <QThreadPool>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include "Application.h"
#include "net/ApiRequest.h"
#include "net/HttpMetaCache.h"
#include "net/NetJob.h"
#include "ui/zero/ZeroMotion.h"

namespace Zero {

namespace {

constexpr const char* kCacheBase = "ZeroContent";  // registered in Application.cpp

bool isRemote(const QString& url)
{
    const QUrl u(url);
    return u.isValid() && (u.scheme() == QLatin1String("https") || u.scheme() == QLatin1String("http")) && !u.host().isEmpty();
}

/// Production fetcher: one NetJob per URL into the metacache ("ZeroContent/<sha1(url)>"). A fresh file that is already in
/// the cache is used without touching the network. Fails at once without an Application or without the cache base.
class NetworkFetcher : public QObject {
   public:
    explicit NetworkFetcher(QObject* parent) : QObject(parent) {}

    void fetch(const QString& url, std::function<void(const RemoteImageCache::FetchResult&)> done)
    {
        auto* app = APPLICATION_DYN;
        if (!app || !app->metacache() || !app->network()) {
            done({});
            return;
        }
        const QString name = QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex());
        auto entry = app->metacache()->resolveEntry(kCacheBase, name);
        if (!entry) {  // the base is not registered
            done({});
            return;
        }
        const QString path = entry->getFullPath();
        if (!entry->isStale() && QFileInfo(path).isFile()) {
            done({ true, path, {} });
            return;
        }
        NetJob::Ptr job(new NetJob(QStringLiteral("ZeroContentImage"), app->network()));
        job->setAskRetry(false);
        job->addNetAction(Net::ApiRequest::makeCached(QUrl(url), entry));
        auto finish = [this, url, path, done](bool ok) {
            done(ok ? RemoteImageCache::FetchResult{ true, path, {} } : RemoteImageCache::FetchResult{});
            // Never delete a job inside its own signal.
            QMetaObject::invokeMethod(this, [this, url]() { m_jobs.remove(url); }, Qt::QueuedConnection);
        };
        connect(job.get(), &Task::succeeded, this, [finish]() { finish(true); });
        connect(job.get(), &Task::failed, this, [finish](const QString&) { finish(false); });
        connect(job.get(), &Task::aborted, this, [finish]() { finish(false); });
        m_jobs.insert(url, job);
        job->start();
    }

   private:
    QHash<QString, NetJob::Ptr> m_jobs;
};

}  // namespace

int RemoteImageCache::bucketFor(int widthPx)
{
    for (int b : kBuckets)
        if (widthPx <= b)
            return b;
    return kBuckets[kBucketCount - 1];
}

int RemoteImageCache::bucketFor(qreal logicalWidth, qreal dpr)
{
    return bucketFor(int(std::ceil(logicalWidth * (dpr > 0 ? dpr : 1.0))));
}

RemoteImageCache::RemoteImageCache(Fetcher fetcher, QObject* parent) : QObject(parent), m_fetcher(std::move(fetcher))
{
    m_clock.start();
    if (!m_fetcher) {
        auto* net = new NetworkFetcher(this);
        m_fetcher = [net](const QString& url, std::function<void(const FetchResult&)> done) { net->fetch(url, std::move(done)); };
    }
}

RemoteImageCache::~RemoteImageCache() = default;

RemoteImageCache* RemoteImageCache::instance()
{
    static QPointer<RemoteImageCache> s_instance;
    if (!s_instance && QCoreApplication::instance())
        s_instance = new RemoteImageCache({}, QCoreApplication::instance());
    return s_instance;
}

QString RemoteImageCache::keyFor(const QString& id, int bucket)
{
    return id + QLatin1Char('|') + QString::number(bucket);
}

bool RemoteImageCache::isCached(const QString& id, int bucket) const
{
    return m_cache.contains(keyFor(id, bucketFor(bucket)));
}

QPixmap RemoteImageCache::peek(const QString& id, int bucket) const
{
    const int wanted = bucketFor(bucket);
    if (const QPixmap* hit = m_cache.object(keyFor(id, wanted)))  // object() marks it most recently used
        return *hit;
    // The nearest decoded size: the next larger one first (sharper), then the largest smaller one.
    for (int b : kBuckets)
        if (b > wanted)
            if (const QPixmap* p = m_cache.object(keyFor(id, b)))
                return *p;
    for (int i = kBucketCount - 1; i >= 0; --i)
        if (kBuckets[i] < wanted)
            if (const QPixmap* p = m_cache.object(keyFor(id, kBuckets[i])))
                return *p;
    return {};
}

QPixmap RemoteImageCache::request(const QString& url, int bucket, QObject* requester, Priority priority)
{
    return requestImpl(url, bucket, requester, priority, false);
}

QPixmap RemoteImageCache::requestLocal(const QString& path, int bucket, QObject* requester, Priority priority)
{
    return requestImpl(path, bucket, requester, priority, true);
}

QPixmap RemoteImageCache::requestImpl(const QString& id, int bucket, QObject* requester, Priority priority, bool local)
{
    if (id.isEmpty() || m_failed.contains(id))
        return {};
    if (!local && !isRemote(id)) {
        m_failed.insert(id);
        return {};
    }
    bucket = bucketFor(bucket);
    if (m_cache.contains(keyFor(id, bucket)))
        return peek(id, bucket);

    if (local && !m_sources.contains(id)) {
        if (!QFileInfo(id).isFile()) {
            fail(id);
            return {};
        }
        m_sources.insert(id, FetchResult{ true, id, {} });
    }
    if (m_sources.contains(id)) {
        startDecode(id, bucket);
    } else {
        Interest interest;
        interest.requester = requester;
        interest.anonymous = requester == nullptr;
        interest.bucket = bucket;
        interest.priority = priority;
        addInterest(id, interest);
        enqueue(id, priority);
        pump();
    }
    return peek(id, bucket);
}

void RemoteImageCache::addInterest(const QString& id, const Interest& interest)
{
    QList<Interest>& list = m_interests[id];
    for (Interest& i : list) {
        if (i.bucket == interest.bucket && i.anonymous == interest.anonymous && i.requester == interest.requester) {
            i.priority = std::max(i.priority, interest.priority);
            return;
        }
    }
    list.append(interest);
}

void RemoteImageCache::enqueue(const QString& id, Priority priority)
{
    if (m_inFlight.contains(id))
        return;
    const auto it = std::find_if(m_queue.begin(), m_queue.end(), [&id](const Queued& q) { return q.id == id; });
    if (it != m_queue.end()) {
        priority = std::max(it->priority, priority);
        m_queue.erase(it);  // the newest request is what the user looks at now: back of the queue = served first
    }
    m_queue.append(Queued{ id, priority });
}

void RemoteImageCache::cancel(QObject* requester)
{
    if (!requester)
        return;
    for (auto it = m_interests.begin(); it != m_interests.end();) {
        QList<Interest>& list = it.value();
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [requester](const Interest& i) { return !i.anonymous && (i.requester.isNull() || i.requester == requester); }),
                   list.end());
        if (list.isEmpty()) {
            const QString id = it.key();
            m_queue.erase(std::remove_if(m_queue.begin(), m_queue.end(), [&id](const Queued& q) { return q.id == id; }), m_queue.end());
            it = m_interests.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_failedInterests.begin(); it != m_failedInterests.end();) {
        QList<Interest>& list = it.value();
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [requester](const Interest& i) { return !i.anonymous && (i.requester.isNull() || i.requester == requester); }),
                   list.end());
        it = list.isEmpty() ? m_failedInterests.erase(it) : std::next(it);
    }
}

void RemoteImageCache::schedulePump()
{
    if (m_pumpScheduled)
        return;
    m_pumpScheduled = true;
    QMetaObject::invokeMethod(
        this,
        [this]() {
            m_pumpScheduled = false;
            pump();
        },
        Qt::QueuedConnection);
}

void RemoteImageCache::pump()
{
    while (m_inFlight.size() < kMaxInFlight && !m_queue.isEmpty()) {
        // Visible before background; within a class the newest (last) first.
        int best = -1;
        for (int i = int(m_queue.size()) - 1; i >= 0; --i)
            if (best < 0 || m_queue[i].priority > m_queue[best].priority)
                best = i;
        const QString id = m_queue.takeAt(best).id;
        auto interests = m_interests.value(id);
        if (std::none_of(interests.cbegin(), interests.cend(), [](const Interest& i) { return i.alive(); })) {
            m_interests.remove(id);  // everybody who wanted it is gone
            continue;
        }
        m_inFlight.insert(id);
        ++m_fetches;
        QPointer<RemoteImageCache> self(this);
        m_fetcher(id, [self, id](const FetchResult& result) {
            if (self)
                self->onFetched(id, result);
        });
    }
}

void RemoteImageCache::onFetched(const QString& id, const FetchResult& result)
{
    if (!m_inFlight.remove(id))
        return;  // a fetcher that answered twice
    const QList<Interest> interests = m_interests.take(id);
    if (!result.ok) {
        m_failedInterests.insert(id, interests);
        fail(id);
    } else {
        m_sources.insert(id, result);
        QSet<int> buckets;
        for (const Interest& i : interests)
            if (i.alive())
                buckets.insert(i.bucket);
        for (int b : buckets)
            startDecode(id, b);
    }
    schedulePump();  // a synchronous fetcher must not recurse into pump()
}

void RemoteImageCache::startDecode(const QString& id, int bucket)
{
    const QString key = keyFor(id, bucket);
    if (m_decoding.contains(key) || m_cache.contains(key))
        return;
    m_decoding.insert(key);
    ++m_decodes;
    const FetchResult source = m_sources.value(id);
    // The pool thread only copies the QPointer; it is read on the GUI thread, where the answer is posted through the
    // application object (which outlives every cache).
    QPointer<RemoteImageCache> self(this);
    QThreadPool::globalInstance()->start([self, id, bucket, source]() {
        const QImage image = decode(source, bucket);
        QThread* thread = QThread::currentThread();
        auto* app = QCoreApplication::instance();
        if (!app)
            return;
        QMetaObject::invokeMethod(
            app,
            [self, id, bucket, image, thread]() {
                if (self)
                    self->onDecoded(id, bucket, image, thread);
            },
            Qt::QueuedConnection);
    });
}

QImage RemoteImageCache::decode(const FetchResult& source, int bucket)
{
    QBuffer buffer;
    QImageReader reader;
    if (!source.filePath.isEmpty()) {
        reader.setFileName(source.filePath);
    } else {
        buffer.setData(source.data);
        buffer.open(QIODevice::ReadOnly);
        reader.setDevice(&buffer);
    }
    reader.setAutoTransform(true);
    const QSize src = reader.size();
    if (src.isValid() && src.width() > bucket)  // never enlarge
        reader.setScaledSize(QSize(bucket, std::max(1, int(std::lround(double(src.height()) * bucket / src.width())))));
    QImage image = reader.read();
    if (!image.isNull() && image.format() != QImage::Format_ARGB32_Premultiplied && image.hasAlphaChannel())
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    return image;
}

void RemoteImageCache::onDecoded(const QString& id, int bucket, const QImage& image, QThread* thread)
{
    m_decoding.remove(keyFor(id, bucket));
    m_lastDecodeThread = thread;
    if (image.isNull()) {
        m_sources.remove(id);
        fail(id);  // undecodable: not retried until retryFailed()
        return;
    }
    auto* pixmap = new QPixmap(QPixmap::fromImage(image));
    const qint64 cost = qint64(image.width()) * image.height() * 4;
    if (!m_cache.insert(keyFor(id, bucket), pixmap, cost))  // larger than the whole cache: QCache deleted it
        return;
    if (!m_arrivedAt.contains(id) && Motion::enabled())
        m_arrivedAt.insert(id, m_clock.elapsed());
    emit imageReady(id, bucket);
}

void RemoteImageCache::fail(const QString& id)
{
    m_failed.insert(id);
    m_queue.erase(std::remove_if(m_queue.begin(), m_queue.end(), [&id](const Queued& q) { return q.id == id; }), m_queue.end());
    emit imageFailed(id);
}

void RemoteImageCache::retryFailed()
{
    m_failed.clear();
    const auto waiting = std::move(m_failedInterests);
    m_failedInterests.clear();
    for (auto it = waiting.cbegin(); it != waiting.cend(); ++it) {
        bool any = false;
        for (const Interest& i : it.value())
            if (i.alive()) {
                addInterest(it.key(), i);
                any = true;
            }
        if (any)
            enqueue(it.key(), Priority::Visible);
    }
    pump();
}

void RemoteImageCache::invalidate(const QString& id)
{
    for (int b : kBuckets)
        m_cache.remove(keyFor(id, b));
    m_sources.remove(id);
    m_failed.remove(id);
    m_arrivedAt.remove(id);
}

qreal RemoteImageCache::fadeIn(const QString& id) const
{
    const auto it = m_arrivedAt.constFind(id);
    if (it == m_arrivedAt.constEnd() || !Motion::enabled())
        return 1.0;
    return std::clamp(qreal(m_clock.elapsed() - it.value()) / kFadeMs, 0.0, 1.0);
}

bool RemoteImageCache::fading() const
{
    if (!Motion::enabled())
        return false;
    const qint64 now = m_clock.elapsed();
    for (auto it = m_arrivedAt.cbegin(); it != m_arrivedAt.cend(); ++it)
        if (now - it.value() < kFadeMs)
            return true;
    return false;
}

}  // namespace Zero
