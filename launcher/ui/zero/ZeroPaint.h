// SPDX-License-Identifier: GPL-3.0-only
/*
 *  ZERO Launcher - theme-aware painting helpers (panel shape, bevel, texture).
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

#include <QPainterPath>
#include <QRectF>
#include "ZeroTokens.h"

class QPainter;

namespace Zero {
namespace Paint {

/// Outline of a panel for the active theme: a rounded rectangle (Corners::Rounded) or a rectangle with a square
/// step of Tokens::notchStep px cut out of every corner (Corners::Notched; `radius` only decides whether there is a
/// step at all). The two-argument form reads Theme::current().
QPainterPath panelPath(const QRectF& rect, qreal radius);
QPainterPath panelPath(const QRectF& rect, qreal radius, Corners corners);

/// Block bevel of a raised control, drawn inside the 1 px border of `rect` (the widget rect): a 1 px light top edge
/// and a 2 px bevelDark bottom edge. No-op unless the active spec has `bevel`; `pressed` also draws nothing (the
/// bevel collapses when a block is pressed).
void paintBevel(QPainter& painter, const QRectF& rect, bool pressed = false);

/// Tiles Style::texture() over `area` (aligned to the widget origin) when the active spec has `texture`, otherwise
/// fills with the flat color. `base` is the color the stone noise is generated around.
void paintBackground(QPainter& painter, const QRect& area, const QColor& base);

}  // namespace Paint
}  // namespace Zero
