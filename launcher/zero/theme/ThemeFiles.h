// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - writing custom themes to disk: save (new / edit) and duplicate. Pure logic (no widgets), built on the
// zero-theme.json helpers of CustomTheme.h. Export / import / trash live in ThemeArchive.h.
#pragma once

#include <QString>
#include <QStringList>

#include "CustomTheme.h"
#include "Result.h"
#include "ThemeArchive.h"

namespace Zero::Custom {

/// Extensions a picked background IMAGE may have (png jpg jpeg webp bmp gif).
QStringList imageExtensions();
/// Lowercase extension of `path` when it is one of imageExtensions(), else an empty string.
QString imageExtension(const QString& path);
/// Extensions a picked background VIDEO may have (mp4 webm mkv mov).
QStringList videoExtensions();
/// Lowercase extension of `path` when it is one of videoExtensions(), else an empty string.
QString videoExtension(const QString& path);
/// Lowercase extension of `path` when it is one of iconExtensions() / fontExtensions() (CustomTheme.h), else an empty string.
QString iconExtension(const QString& path);
QString fontExtension(const QString& path);

/// Checks a launcher icon picked in the editor: a file of an iconExtensions() type, at most `maxBytes`, and (raster icons) at least
/// 32 px on both sides (an .ico counts by its largest image, an .svg is scalable). The error is readable.
Result<> validateIconSource(const QString& path, qint64 maxBytes = kMaxIconBytes);
/// Checks a font picked in the editor: a readable ttf / otf of at most `maxBytes`. (Whether Qt can load it is found out when it is applied.)
Result<> validateFontSource(const QString& path, qint64 maxBytes = kMaxFontBytes);

/// Limits applied by saveTheme() (a parameter so tests can use a tiny cap).
struct SaveLimits {
    qint64 maxVideoBytes = kMaxBackgroundBytes;
    qint64 maxIconBytes = kMaxIconBytes;
    qint64 maxFontBytes = kMaxFontBytes;
};

/// The theme's own launcher icon and UI font picked in the editor, as source files that are not in the theme folder yet. Empty = keep
/// what `def.iconFile` / `def.fontFile` name (or nothing when they are empty).
struct AssetSources {
    QString icon;  // png svg ico, <= SaveLimits::maxIconBytes, copied in as `icon.<ext>`
    QString font;  // ttf otf, <= SaveLimits::maxFontBytes, copied in as `font.<ext>`
};

struct SavedTheme {
    QString id;      // "zero-custom-<slug>"
    QString folder;  // absolute path of the theme folder
};

/// Writes `def` as the theme in `folder` (an existing theme folder = edit; renaming keeps the folder, so the id stays), or as a
/// new theme under `themesRoot` when `folder` is empty (the folder is named by uniqueSlug(themesRoot, def.name)).
///
/// `backgroundSource`: a file to use as the new background, an image (imageExtensions()) or a video (videoExtensions()), copied in
/// as `background.<ext>` through a temporary name, then renamed; `def.backgroundKind` (Image / Video by the extension) and
/// `backgroundFile` are set accordingly. A video over `limits.maxVideoBytes` is refused. Empty = keep `def`'s background as it
/// is. The previous `background.*` file is removed only after the JSON was written, and only when `def` no longer refers to it
/// (so Image <-> Video leaves one background file).
///
/// `assets`: the same for the launcher icon (`icon.<ext>`) and the font (`font.<ext>`); each one goes through its own temporary name /
/// backup, and the previous `icon.*` / `font.*` goes away once the JSON is written and `def` no longer names it.
///
/// Nothing is left half-done: on any failure the new folder (for a new theme) is removed, a replaced file is put back and the
/// returned error is readable.
Result<SavedTheme> saveTheme(const QString& themesRoot, const QString& folder, Definition def, const QString& backgroundSource = {},
                             const SaveLimits& limits = {}, const AssetSources& assets = {});

/// Copies the theme in `folder` (definition, background, icon and font files) into a new folder under `themesRoot`, named
/// `uniqueName("<name> copy")`. The copy is never loaded or applied here.
Result<SavedTheme> duplicateTheme(const QString& themesRoot, const QString& folder);

}  // namespace Zero::Custom
