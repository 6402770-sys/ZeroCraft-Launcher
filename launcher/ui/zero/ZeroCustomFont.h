// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the UI font of a custom theme: registers the theme's font file with Qt and remembers which family that gave.
// At most one theme font is registered at a time; a new file (or none) removes the previous one. A file Qt cannot load is not an error:
// the default font stays, and one warning is logged per file. The file is read into memory (no open handle on the theme folder).
#pragma once

#include <QString>

namespace Zero::CustomFont {

/// Registers the font file at `path` (QFontDatabase::addApplicationFont) and returns its family; an empty `path` unregisters the
/// current theme font and returns an empty string. The same path with the same modification time is a no-op that returns the same
/// answer (a file that failed to load is not retried or logged again). Empty when the file is missing, not a font, or Qt rejects it.
/// Needs a QGuiApplication.
QString load(const QString& path);

/// The family `load()` last returned for the registered file (empty when none).
QString currentFamily();

/// True when `family` is, or ever was, a theme font family. Widgets whose font names such a family but that no longer should have it
/// are given the role font again when the theme changes (Style::refreshRoleFonts).
bool isThemeFontFamily(const QString& family);

}  // namespace Zero::CustomFont
