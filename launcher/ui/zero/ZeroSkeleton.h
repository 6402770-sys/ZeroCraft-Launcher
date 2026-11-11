// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - loading placeholder: a short caption over grey shapes that look like the content on its way (list rows,
// picture tiles or report cards), with a soft highlight sweeping across them. The sweep only runs while the widget is
// visible and motion is on; otherwise the shapes are static.
#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

namespace Zero {

class ZeroSkeleton : public QWidget {
    Q_OBJECT
   public:
    enum class Shape { Rows, Tiles, Report };
    static constexpr int kSweepMs = 1400;

    ZeroSkeleton(Shape shape, const QString& caption, QWidget* parent = nullptr);

    void setCaption(const QString& caption);
    QString caption() const { return m_caption; }
    bool isSweeping() const { return m_timer.isActive(); }

   protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

   private:
    void paintRows(QPainter& p, const QRect& area) const;
    void paintTiles(QPainter& p, const QRect& area) const;
    void paintReport(QPainter& p, const QRect& area) const;
    void block(QPainter& p, const QRectF& r) const;

    Shape m_shape;
    QString m_caption;
    QTimer m_timer;
    QElapsedTimer m_clock;
};

}  // namespace Zero
