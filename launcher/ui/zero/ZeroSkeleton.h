// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - loading placeholder: a short caption over grey shapes that look like the content on its way (list rows,
// picture tiles or report cards), with a soft highlight sweeping across them. The sweep only runs while the widget is
// visible and motion is on; otherwise the shapes are static.
// Discover shapes (docs/zero/CONTENT.md, "Motion" > Loading): Cards = the modpack / mod grid card, Gallery = artwork tiles of
// resource packs and shaders, Cinematic = the wide hero cards of a featured strip.
#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

namespace Zero {

class ZeroSkeleton : public QWidget {
    Q_OBJECT
   public:
    enum class Shape { Rows, Tiles, Report, Cards, Gallery, Cinematic };
    static constexpr int kSweepMs = 1400;

    ZeroSkeleton(Shape shape, const QString& caption, QWidget* parent = nullptr);

    void setCaption(const QString& caption);
    QString caption() const { return m_caption; }
    bool isSweeping() const { return m_timer.isActive(); }
    Shape shape() const { return m_shape; }
    /// Cards only: the artwork-less mod card (icon at the left, name / author / two lines, a footer of pills; 300-420 px wide,
    /// 132 px high) instead of the modpack card.
    void setCompactCards(bool compact);
    bool compactCards() const { return m_compactCards; }

   protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

   private:
    void paintRows(QPainter& p, const QRect& area) const;
    void paintTiles(QPainter& p, const QRect& area) const;
    void paintReport(QPainter& p, const QRect& area) const;
    void paintCards(QPainter& p, const QRect& area) const;
    void paintModCards(QPainter& p, const QRect& area) const;
    void paintGallery(QPainter& p, const QRect& area) const;
    void paintCinematic(QPainter& p, const QRect& area) const;
    void block(QPainter& p, const QRectF& r) const;

    Shape m_shape;
    bool m_compactCards = false;
    QString m_caption;
    QTimer m_timer;
    QElapsedTimer m_clock;
};

}  // namespace Zero
