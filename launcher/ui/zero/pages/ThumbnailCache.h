// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - in-memory thumbnail cache of the Screenshot Gallery. Thumbnails are decoded on QThreadPool::globalInstance()
// with QImageReader::setScaledSize (the full image is never decoded on the GUI thread), kept in an LRU of kCapacity pixmaps
// keyed by path + modified time, and never written to disk.
#pragma once

#include <QCache>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QSize>
#include <QString>

namespace Zero {

class ThumbnailCache : public QObject {
    Q_OBJECT
   public:
    static constexpr int kCapacity = 300;
    /// Stored size: sharp up to ~360 px wide tiles (the fluid grid scales it down); the key does not depend on the tile size, so a resize
    /// never re-decodes. The shorter side fits, the longer side is cropped on paint.
    static constexpr int kThumbWidth = 400;
    static constexpr int kThumbHeight = 225;

    explicit ThumbnailCache(QObject* parent = nullptr);

    /// The cached thumbnail, or a null pixmap while it is not ready (a load is then queued once; thumbnailReady() follows).
    /// Files that cannot be decoded stay null (no retry until their modified time changes).
    QPixmap thumbnail(const QString& path, qint64 modifiedMs);
    bool isReady(const QString& path, qint64 modifiedMs) const;
    /// 0..1: how far a thumbnail that just arrived has faded in (1 when it arrived long ago or motion is off).
    qreal fadeIn(const QString& path, qint64 modifiedMs) const;
    /// True while at least one thumbnail is still fading in (the page repaints until it is false).
    bool fading() const;
    int cachedCount() const { return static_cast<int>(m_cache.size()); }
    int pendingCount() const { return static_cast<int>(m_pending.size()); }

    /// Decodes `path` scaled to cover kThumbWidth x kThumbHeight (aspect kept); a null image when it cannot be read. Pure, thread-safe.
    static QImage load(const QString& path);

   signals:
    void thumbnailReady(const QString& path);

   private:
    void onLoaded(const QString& key, const QString& path, const QImage& image);
    static QString keyFor(const QString& path, qint64 modifiedMs);

    QCache<QString, QPixmap> m_cache{ kCapacity };  // cost 1 each: least recently used goes first
    QSet<QString> m_pending;
    QSet<QString> m_failed;
    QElapsedTimer m_clock;
    QHash<QString, qint64> m_arrivedAt;  // key -> m_clock time; only entries that may still be fading
};

}  // namespace Zero
