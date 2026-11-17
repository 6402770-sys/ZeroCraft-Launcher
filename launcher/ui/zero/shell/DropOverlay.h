// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the veil that covers the main window while files or links are dragged over it (docs/zero/SHELL.md, "Drag and drop").
// It is transparent for the mouse, so the window's own drag and drop handlers keep working; it only follows the drag (an application level
// event filter sees DragEnter / DragLeave / Drop of every widget of its window).
#pragma once

#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

namespace Zero {

class DropOverlay : public QWidget {
    Q_OBJECT
   public:
    static constexpr int kFadeMs = 120;      ///< fade in / out (instant with reduced motion)
    static constexpr int kLeaveGraceMs = 60;  ///< a drag leaving one child usually enters the next one at once

    /// Child of `window`, covering all of it; follows the window's size.
    explicit DropOverlay(QWidget* window);
    ~DropOverlay() override;

    void showOverlay();
    void hideOverlay();
    /// True from showOverlay() until the fade out ends.
    bool overlayShown() const { return m_shown; }
    qreal opacity() const { return m_opacity; }

    bool eventFilter(QObject* watched, QEvent* event) override;

   protected:
    void paintEvent(QPaintEvent*) override;

   private:
    void animateTo(qreal target);

    bool m_shown = false;
    qreal m_opacity = 0.0;
    QVariantAnimation m_anim;
    QTimer m_leaveTimer;
};

}  // namespace Zero
