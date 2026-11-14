// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - custom theme export / import (zip) and delete (Recycle Bin). Pure logic, no widgets.
#pragma once

#include <QString>
#include <QStringList>

#include "Result.h"

namespace Zero::Custom {

/// The largest background file (a video, in practice) a theme may carry: the cap of an import and of the editor's video picker.
inline constexpr qint64 kMaxBackgroundBytes = 200LL * 1024 * 1024;
/// The largest launcher icon (2 MB) and UI font file (20 MB) a theme may carry (editor pickers, import).
inline constexpr qint64 kMaxIconBytes = 2LL * 1024 * 1024;
inline constexpr qint64 kMaxFontBytes = 20LL * 1024 * 1024;

/// Size caps applied while an import streams the archive.
struct ImportLimits {
    qint64 maxJsonBytes = 256 * 1024;
    qint64 maxBackgroundBytes = kMaxBackgroundBytes;
    qint64 maxIconBytes = kMaxIconBytes;
    qint64 maxFontBytes = kMaxFontBytes;
};

/// File extensions a background entry (`background.<ext>`) may have: imageExtensions() + videoExtensions() (ThemeFiles.h).
QStringList backgroundExtensions();

/// Display names of every custom theme folder under `themesRoot` (for "name (2)" de-duplication).
QStringList existingThemeNames(const QString& themesRoot);

/// Writes `zipPath` (replacing it) with `zero-theme.json` and the background, icon and font files only (stored as `background.<ext>`,
/// `icon.<ext>`, `font.<ext>`; a missing file is exported as "none" / default). Fails when the folder holds no readable theme.
Result<> exportTheme(const QString& themeFolder, const QString& zipPath);

struct ImportedTheme {
    QString id;      // "zero-custom-<slug>"
    QString folder;  // absolute path of the new theme folder
    QString name;    // display name (" (2)" appended when it clashed)
};

/// Imports a theme zip into a NEW folder under `themesRoot` (never overwrites). Only the flat entries `zero-theme.json` and
/// `background.<ext>`, `icon.<png/svg/ico>` and `font.<ttf/otf>` are accepted; any other name, any path separator / ".." / absolute path, an oversize entry or an
/// invalid definition rejects the whole archive. Extracted to a temporary folder first and renamed into place, so a failed
/// import leaves nothing behind. The error text is human-readable.
Result<ImportedTheme> importTheme(const QString& zipPath, const QString& themesRoot, const ImportLimits& limits = {});

/// Moves a theme folder to the Recycle Bin (FS::trash). On failure returns the error and leaves the folder alone: a custom
/// theme is never deleted permanently. Refuses folders without a zero-theme.json.
Result<> trashTheme(const QString& themeFolder);

}  // namespace Zero::Custom
