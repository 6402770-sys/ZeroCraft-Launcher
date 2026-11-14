// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the universal search pill in the centre of the top bar. Click (or Ctrl+K) opens the command palette.
#pragma once

#include <QAbstractButton>

namespace Zero {

namespace Motion {
class StateAnimator;
}

class ZeroSearchPill : public QAbstractButton {
    Q_OBJECT
   public:
    static constexpr int kHeight = 34;
    static constexpr int kWidth = 360;
    static constexpr int kMinWidth = 200;  // five Discover sections need the room at 1100 px (it was 240)

    explicit ZeroSearchPill(QWidget* parent = nullptr);

    QSize sizeHint() const override { return { kWidth, kHeight }; }
    QSize minimumSizeHint() const override { return { kMinWidth, kHeight }; }

   protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

   private:
    Motion::StateAnimator* m_hover;
    bool m_keyboardFocus = false;
};

}  // namespace Zero
