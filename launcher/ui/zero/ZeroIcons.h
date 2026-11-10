// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Lucide icons (ISC, see docs/zero/THIRD_PARTY.md), tinted at runtime.
#pragma once

#include <QByteArray>
#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

namespace Zero {

/// Registers the compiled-in ":/zero/..." resources (idempotent). Called internally by Icons/Style.
void initResources();

namespace Icons {

/// Lucide icon `name` (file name without .svg, e.g. "house") tinted to `color`. Renders crisply at any size
/// and device pixel ratio (vector engine). The Disabled mode is drawn at 40% opacity. Cached by (name, color).
/// Returns a null icon (and logs a warning) if the name is unknown.
QIcon icon(const QString& name, const QColor& color);

/// Convenience: rasterised icon at a logical size and device pixel ratio.
QPixmap pixmap(const QString& name, const QColor& color, int logicalSize, qreal dpr = 1.0);

/// Raw SVG source of `name` with `currentColor` replaced by `color`; empty if unknown.
QByteArray tintedSvg(const QString& name, const QColor& color);

/// All icon names shipped in the resource bundle.
QStringList availableNames();

}  // namespace Icons
}  // namespace Zero
