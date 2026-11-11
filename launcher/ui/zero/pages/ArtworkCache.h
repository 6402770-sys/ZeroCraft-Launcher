// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - cover-cropped, cached instance artwork for painting (grid cards, details header, Home hero).
#pragma once

#include <QPixmap>
#include <QSize>
#include <QString>
#include <QWidget>

namespace Zero {
namespace Artwork {

/// The image at `path` scaled to cover `size` (logical px, at device pixel ratio `dpr`) and center-cropped to it.
/// Null when the file cannot be read. Cached per (path, size, dpr); the cache is dropped whenever
/// Profile::Notifier::artworkChanged fires.
QPixmap cover(const QString& path, const QSize& size, qreal dpr = 1.0);

/// Drops every cached image.
void clearCache();

}  // namespace Artwork

/// A widget that paints an artwork cover-cropped with the rounded (theme-shaped) corners of `radius`. Without an
/// artwork it paints nothing. Purely decorative (mouse transparent).
class ArtworkView : public QWidget {
   public:
    explicit ArtworkView(QWidget* parent = nullptr);

    void setArtwork(const QString& path);
    QString artwork() const { return m_path; }
    /// Corner radius token value (default: the active theme's radiusLg, read at paint time when 0).
    void setRadius(int radius);

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    QString m_path;
    int m_radius = 0;
};

}  // namespace Zero
