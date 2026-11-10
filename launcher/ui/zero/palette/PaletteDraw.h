// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - painting helpers shared by the palette, the shortcut sheet and the search pill.
#pragma once

#include <QPainter>
#include <QRect>
#include <QString>
#include <QStringList>

namespace Zero {
namespace PaletteDraw {

/// Height of a key cap (caption text + 2 px padding top and bottom).
int keycapHeight();
/// Width of one key cap showing `text` (caption text + 6 px padding each side).
int keycapWidth(const QString& text);
/// Draws a key cap with its left edge at `x`, vertically centred on `centerY`: `elevated` fill, radius sm, caption
/// text in `textSecondary` (or `color` when valid). Returns the width.
int paintKeycap(QPainter& p, int x, int centerY, const QString& text, const QColor& color = QColor());
/// Total width of a row of key caps (4 px between caps).
int keycapsWidth(const QStringList& keys);
/// Draws the key caps right-aligned so that the last one ends at `rightX`. Returns the left edge of the group.
int paintKeycapsRight(QPainter& p, int rightX, int centerY, const QStringList& keys);

}  // namespace PaletteDraw
}  // namespace Zero
