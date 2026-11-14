// SPDX-License-Identifier: GPL-3.0-only
// One clock and one reduced-motion switch for every animation of the installer. Animations are plain functions of
// time (no QPropertyAnimation), so a screenshot can render any moment deterministically.
#pragma once

#include <QElapsedTimer>
#include <QEasingCurve>

namespace ZeroSetup::Motion {

/// Milliseconds since the program started (or the frozen value, for screenshots).
qint64 now();
void freezeAt(qint64 ms);  // screenshots: time stops here
bool frozen();

/// false when Windows "Show animations in Windows" is off, or ZERO_SETUP_REDUCE_MOTION=1 (tests). Then there are no
/// drops, twinkles, bobs or slides: states change instantly.
bool enabled();
void setEnabled(bool on);

/// Progress 0..1 of an animation that started at startMs and lasts durMs, eased. Reduced motion jumps to 1.
double progress(qint64 startMs, int durMs, QEasingCurve::Type curve = QEasingCurve::OutCubic, double overshoot = 1.70158);

}  // namespace ZeroSetup::Motion
