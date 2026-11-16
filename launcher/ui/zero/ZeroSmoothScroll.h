// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - app-wide smooth mouse-wheel scrolling. One event filter on qApp turns the stepped wheel of every
// QAbstractScrollArea into an eased glide (Zero::Motion). Touchpads (pixel deltas / scroll phases), Ctrl+wheel and areas
// flagged with the dynamic property `zeroNoSmoothScroll` are left alone; with motion off the target is applied at once.
#pragma once

#include <QObject>

namespace Zero {
namespace SmoothScroll {

/// Dynamic property (bool) that opts a scroll area out, for widgets that map the wheel themselves.
inline constexpr const char* kOptOutProperty = "zeroNoSmoothScroll";

/// Pixels moved per wheel line (a notch scrolls QApplication::wheelScrollLines() lines).
inline constexpr int kPixelsPerLine = 40;

/// Installs the filter on `app` (idempotent). Returns the filter.
QObject* install(QObject* app);

}  // namespace SmoothScroll
}  // namespace Zero
