// SPDX-License-Identifier: GPL-3.0-only
#include "ThumbnailCache.h"

#include <QCoreApplication>
#include <QImage>
#include <QImageReader>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>

namespace Zero {

ThumbnailCache::ThumbnailCache(QObject* parent) : QObject(parent) {}

QString ThumbnailCache::keyFor(const QString& path, qint64 modifiedMs)
{
    return path + QLatin1Char('|') + QString::number(modifiedMs);
}

QImage ThumbnailCache::load(const QString& path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize source = reader.size();
    if (source.isValid() && !source.isEmpty())
        reader.setScaledSize(source.scaled(kThumbWidth, kThumbHeight, Qt::KeepAspectRatioByExpanding));
    return reader.read();
}

bool ThumbnailCache::isReady(const QString& path, qint64 modifiedMs) const
{
    return m_cache.contains(keyFor(path, modifiedMs));
}

QPixmap ThumbnailCache::thumbnail(const QString& path, qint64 modifiedMs)
{
    const QString key = keyFor(path, modifiedMs);
    if (const QPixmap* hit = m_cache.object(key))  // object() marks it most recently used
        return *hit;
    if (m_pending.contains(key) || m_failed.contains(key))
        return {};
    m_pending.insert(key);

    // The pool thread never reads the QPointer: it posts to the application object and the pointer is checked there, on the GUI thread.
    QPointer<ThumbnailCache> self(this);
    QThreadPool::globalInstance()->start([self, key, path]() {
        const QImage image = load(path);
        auto* app = QCoreApplication::instance();
        if (!app)
            return;
        QMetaObject::invokeMethod(
            app,
            [self, key, path, image]() {
                if (self)
                    self->onLoaded(key, path, image);
            },
            Qt::QueuedConnection);
    });
    return {};
}

void ThumbnailCache::onLoaded(const QString& key, const QString& path, const QImage& image)
{
    m_pending.remove(key);
    if (image.isNull()) {
        m_failed.insert(key);
        return;
    }
    m_cache.insert(key, new QPixmap(QPixmap::fromImage(image)));
    emit thumbnailReady(path);
}

}  // namespace Zero
