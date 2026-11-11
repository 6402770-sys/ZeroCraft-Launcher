// SPDX-License-Identifier: GPL-3.0-only
#include "WorldIcon.h"

#include <QCache>
#include <QPainter>
#include <QPainterPath>
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {
namespace WorldIcon {

namespace {
QCache<QString, QPixmap>& cache()
{
    static QCache<QString, QPixmap> instance(256);
    return instance;
}
}  // namespace

QPixmap pixmap(const QString& path, int size, qreal dpr)
{
    if (path.isEmpty() || size <= 0)
        return {};
    const QString key = QStringLiteral("%1|%2|%3").arg(path).arg(size).arg(dpr);
    if (QPixmap* hit = cache().object(key))
        return *hit;
    const QPixmap source(path);
    QPixmap scaled;  // stays null (and cached as such) when the file is not a readable image
    if (!source.isNull()) {
        const int device = qRound(size * dpr);
        scaled = source.scaled(device, device, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        scaled.setDevicePixelRatio(dpr);
    }
    cache().insert(key, new QPixmap(scaled));
    return scaled;
}

void clearCache()
{
    cache().clear();
}

void paintTile(QPainter* painter, const QRectF& rect, const QString& path, int glyphSize, qreal dpr)
{
    const auto& t = Theme::current();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QPainterPath shape = Paint::panelPath(rect, t.radiusMd);
    painter->setPen(Qt::NoPen);
    painter->setBrush(t.elevated);
    painter->drawPath(shape);

    const QPixmap icon = pixmap(path, qRound(rect.width()), dpr);
    if (!icon.isNull()) {
        painter->setClipPath(shape);
        painter->drawPixmap(rect.topLeft(), icon);
    } else {
        const QRectF glyph(rect.center().x() - glyphSize / 2.0, rect.center().y() - glyphSize / 2.0, glyphSize, glyphSize);
        Icons::icon(QStringLiteral("earth"), t.textTertiary).paint(painter, glyph.toRect());
    }
    painter->restore();
}

}  // namespace WorldIcon

WorldIconTile::WorldIconTile(int tileSize, int glyphSize, QWidget* parent) : QWidget(parent), m_tile(tileSize), m_glyph(glyphSize)
{
    setFixedSize(tileSize, tileSize);
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void WorldIconTile::setIconPath(const QString& path)
{
    if (m_path == path)
        return;
    m_path = path;
    update();
}

void WorldIconTile::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    WorldIcon::paintTile(&p, QRectF(0, 0, m_tile, m_tile), m_path, m_glyph, devicePixelRatioF());
}

}  // namespace Zero
