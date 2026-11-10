// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - card surface.
#pragma once

#include <QFrame>
#include "ZeroMotion.h"

namespace Zero {

/// Painted card: bg `surface`, 1px `border`, radius `lg`. Hover animates to `surfaceHover` + `borderStrong`.
/// Content margins default to `lg` (16 px). When clickable: pointing-hand cursor, keyboard focus (Space/Enter)
/// and `clicked()`.
class ZeroCard : public QFrame {
    Q_OBJECT
   public:
    explicit ZeroCard(QWidget* parent = nullptr);

    void setClickable(bool clickable);
    bool isClickable() const { return m_clickable; }

    /// Hover feedback is on by default; turn it off for purely static cards.
    void setHoverEnabled(bool enabled);

   signals:
    void clicked();

   protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

   private:
    Motion::StateAnimator* m_hover;
    bool m_clickable = false;
    bool m_hoverEnabled = true;
    bool m_pressed = false;
};

}  // namespace Zero
