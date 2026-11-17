// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QPushButton>

namespace Zero {

/// A zeroSize "sm" button that is really kCompactButtonHeight tall: the style sheet's `min-height: 0` for "sm" resets the minimum height
/// set in code on every polish, and a Fixed policy then takes the (shorter) size hint, so the hint itself carries the height.
class CompactButton : public QPushButton {
   public:
    static constexpr int kCompactButtonHeight = 28;

    using QPushButton::QPushButton;
    QSize sizeHint() const override { return { QPushButton::sizeHint().width(), kCompactButtonHeight }; }
    QSize minimumSizeHint() const override { return { QPushButton::minimumSizeHint().width(), kCompactButtonHeight }; }
};

}  // namespace Zero
