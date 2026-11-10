// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - press ripple for ZERO buttons and nav items (flat themes only).
//
// One application-wide event filter. A QAbstractButton gets a ripple when it carries a `zeroVariant` property or
// `zeroRipple` = true (the nav rail items; `zeroRippleInset` = horizontal inset of the clip shape in px). On mouse
// press a mouse-transparent child is added: a radial `text` color at ~10% alpha, clipped to the theme's panel shape,
// expanding from the press point (280 ms OutCubic) and fading 200 ms after release, then deleting itself.
// Nothing happens with motion off or while a bevel theme (Minecraft: block press in QSS) is active.
#pragma once

class QObject;

namespace Zero {
namespace Ripple {

/// Installs the filter on the application (once). Safe to call repeatedly.
void install();

/// Number of ripple children currently attached to `button` (used by tests).
int rippleCount(QObject* button);

}  // namespace Ripple
}  // namespace Zero
