// SPDX-License-Identifier: GPL-3.0-only
/*
 *  ZERO Launcher - "ZERO Pixel", an original 5x7 bitmap font compiled into a TrueType font at runtime.
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

#include <QByteArray>
#include <QString>

namespace Zero {

/// Family name of the built-in pixel font ("ZERO Pixel").
QString pixelFamily();

/// Registers the font with Qt (QFontDatabase::addApplicationFontFromData). Idempotent: later calls return the first
/// outcome. Needs a QGuiApplication. Returns false when Qt rejected the font data; callers must then fall back.
bool registerPixelFont();

/// The generated TrueType file (printable ASCII 32-126). One pixel = em/10, so pixel sizes that are a multiple of 10
/// draw every glyph pixel as an exact number of device pixels. Exposed for tests.
QByteArray pixelFontData();

}  // namespace Zero
