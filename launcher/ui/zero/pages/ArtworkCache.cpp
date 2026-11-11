// SPDX-License-Identifier: GPL-3.0-only
#include "ArtworkCache.h"

#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroThemeState.h"
#include "zero/profiles/InstanceProfile.h"

namespace Zero {
namespace Artwork {

namespace {

constexpr int kMaxEntries = 96;
constexpr int kMaxSources = 16;

QHash<QString, QPixmap>& scaledCache()
{
    static QHash<QString, QPixmap> cache;
    return cache;
}

QHash<QString, QImage>& sourceCache()
{
    static QHash<QString, QImage> cache;
    return cache;
}

void connectInvalidation()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    QObject::connect(Profile::Notifier::instance(), &Profile::Notifier::artworkChanged, Profile::Notifier::instance(), []() { clearCache(); });
}

}  // namespace

void clearCache()
{
    scaledCache().clear();
    sourceCache().clear();
}

QPixmap cover(const QString& path, const QSize& size, qreal dpr)
{
    if (path.isEmpty() || size.isEmpty())
        return {};
    connectInvalidation();
    const QString key = QStringLiteral("%1|%2x%3|%4").arg(path).arg(size.width()).arg(size.height()).arg(dpr);
    if (auto it = scaledCache().constFind(key); it != scaledCache().constEnd())
        return *it;

    QImage source;
    if (auto it = sourceCache().constFind(path); it != sourceCache().constEnd()) {
        source = *it;
    } else {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        source = reader.read();
        if (sourceCache().size() >= kMaxSources)
            sourceCache().clear();
        sourceCache().insert(path, source);
    }
    if (source.isNull())
        return {};

    const QSize target(qRound(size.width() * dpr), qRound(size.height() * dpr));
    const QImage scaled = source.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const QRect crop(QPoint((scaled.width() - target.width()) / 2, (scaled.height() - target.height()) / 2), target);
    QPixmap pixmap = QPixmap::fromImage(scaled.copy(crop));
    pixmap.setDevicePixelRatio(dpr);
    if (scaledCache().size() >= kMaxEntries)
        scaledCache().clear();
    scaledCache().insert(key, pixmap);
    return pixmap;
}

}  // namespace Artwork

ArtworkView::ArtworkView(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void ArtworkView::setArtwork(const QString& path)
{
    if (path == m_path)
        return;
    m_path = path;
    update();
}

void ArtworkView::setRadius(int radius)
{
    m_radius = radius;
    update();
}

void ArtworkView::paintEvent(QPaintEvent*)
{
    if (m_path.isEmpty())
        return;
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPainterPath shape = Paint::panelPath(QRectF(rect()), m_radius > 0 ? m_radius : t.radiusLg);
    p.setClipPath(shape);
    const QPixmap pixmap = Artwork::cover(m_path, size(), devicePixelRatioF());
    if (pixmap.isNull()) {
        p.fillRect(rect(), t.elevated);
    } else {
        p.drawPixmap(0, 0, pixmap);
    }
    p.setClipping(false);
    p.setPen(QPen(t.border, 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), m_radius > 0 ? m_radius : t.radiusLg));
}

}  // namespace Zero
