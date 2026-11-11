// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - world icons for the Worlds hub: icon.png scaled with nearest-neighbour (pixel art stays crisp) on a rounded
// elevated tile, or the `earth` glyph when the world has no icon.
#pragma once

#include <QPixmap>
#include <QRectF>
#include <QString>
#include <QWidget>

class QPainter;

namespace Zero {
namespace WorldIcon {

/// `path` scaled to `size` x `size` logical pixels (device pixels = size * dpr) with Qt::FastTransformation. Cached by
/// (path, size, dpr); null when the file cannot be read.
QPixmap pixmap(const QString& path, int size, qreal dpr);

/// Drops every cached pixmap (called on rescans: an icon file may have changed).
void clearCache();

/// Paints the tile into `rect`: `elevated` rounded rectangle (radius `radiusMd`); the icon fills it (clipped to the rounded
/// shape), or `earth` in `textTertiary` centered at `glyphSize` when `path` is empty / unreadable.
void paintTile(QPainter* painter, const QRectF& rect, const QString& path, int glyphSize, qreal dpr);

}  // namespace WorldIcon

/// Decorative fixed-size tile that paints a world icon (Worlds details panel).
class WorldIconTile : public QWidget {
    Q_OBJECT
   public:
    WorldIconTile(int tileSize, int glyphSize, QWidget* parent = nullptr);

    void setIconPath(const QString& path);
    QString iconPath() const { return m_path; }

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    int m_tile;
    int m_glyph;
    QString m_path;
};

}  // namespace Zero
