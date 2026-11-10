// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - shared base of the command palette and the shortcut sheet: a child overlay that covers the main
// window (backdrop at 55 % of `bg`), holds one panel (surface, 1 px border, radius xl, popover shadow) and animates it.
// Open: backdrop 160 ms, panel fade + 8 px rise + scale 0.98 -> 1 in 180 ms OutQuint. Close: 120 ms fade. Motion off:
// instant. Esc or a click on the backdrop closes; focus returns to where it was.
#pragma once

#include <QPixmap>
#include <QPointer>
#include <QVariantAnimation>
#include <QWidget>

namespace Zero {

/// The panel itself: draws surface + border with the theme's panel shape.
class PalettePanel : public QWidget {
    Q_OBJECT
   public:
    explicit PalettePanel(QWidget* parent) : QWidget(parent) {}

   protected:
    void paintEvent(QPaintEvent* event) override;
};

class PaletteOverlay : public QWidget {
    Q_OBJECT
   public:
    /// `host` is the window the overlay covers (it becomes the parent).
    explicit PaletteOverlay(QWidget* host);

    /// Visible and not on its way out.
    bool isOpen() const { return m_open; }
    void open();
    /// Closes (fades out); the previous focus widget is restored immediately.
    void closeOverlay();

    PalettePanel* panel() const { return m_panel; }
    /// Geometry of the panel in overlay coordinates once settled (also while the animation runs).
    QRect panelRect() const { return m_panelRect; }

   signals:
    void opened();
    void closed();

   protected:
    /// Called by open() before the panel is shown: refresh content here.
    virtual void aboutToOpen() {}
    virtual QWidget* initialFocus() { return nullptr; }
    /// Panel width wanted (clamped to 90 % of the host); height comes from `panelHeight()`.
    virtual int preferredWidth() const { return 640; }
    virtual int panelHeight() const = 0;
    /// Recomputes the panel geometry (call after the content's height changed).
    void updatePanelGeometry();

    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override { event->accept(); }

   private:
    void finishOpen();
    void finishClose();
    void startAnimation(bool opening);
    void paintShadow(QPainter& p, const QRectF& rect, qreal opacity) const;

    PalettePanel* m_panel;
    QWidget* m_host;
    QRect m_panelRect;
    bool m_open = false;
    bool m_animating = false;
    bool m_opening = false;
    qreal m_progress = 1.0;  // 0..1 of the running animation (opening: 0 -> 1, closing: 1 -> 0)
    QPixmap m_snapshot;
    QVariantAnimation m_anim;
    QPointer<QWidget> m_previousFocus;
};

}  // namespace Zero
