// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - user-made themes: the on-disk definition (zero-theme.json), color derivation, readability check.
// Pure logic (no widgets). The ITheme that applies a definition is Zero::ZeroCustomTheme (ui/zero/ZeroCustomTheme.h).
#pragma once

#include <QByteArray>
#include <QColor>
#include <QMap>
#include <QString>
#include <QStringList>

#include "Result.h"
#include "ui/zero/ZeroTokens.h"

namespace Zero::Custom {

/// Theme folder layout: <themes dir>/<slug>/zero-theme.json (+ the background file next to it).
inline constexpr const char* kDefinitionFile = "zero-theme.json";
inline constexpr const char* kIdPrefix = "zero-custom-";
inline constexpr int kFormat = 1;
inline constexpr double kDefaultDim = 0.35;
inline constexpr double kMinCardOpacity = 0.4;  // below this, text would sit on (almost) nothing
inline constexpr double kDefaultVolume = 0.5;  // background video volume when sound is on

enum class Base { Obsidian, Minecraft };
enum class CornerStyle { Rounded, Square };
enum class BackgroundKind { None, Image, Video };  // Video: muted looping window background (ZeroVideoBackground)

struct Definition {
    QString name;
    Base base = Base::Obsidian;
    /// The four colors the editor exposes. An invalid QColor means "the base theme's color" (surface: derived from background).
    QColor accent, background, text, surface;
    /// ThemeSpec color member name ("border", "danger", ...) -> color. Applied last, so they win over the derivation.
    QMap<QString, QColor> overrides;
    CornerStyle corners = CornerStyle::Rounded;
    BackgroundKind backgroundKind = BackgroundKind::None;
    QString backgroundFile;  // plain file name inside the theme folder ("background.png")
    double dim = kDefaultDim;  // 0..1, bg color laid over the background image
    double cardOpacity = 1.0;  // kMinCardOpacity..1, alpha of surface / surfaceHover
    bool sound = false;  // the background VIDEO plays its sound (default off; ignored without a video)
    double volume = kDefaultVolume;  // 0..1, only used with sound
    QString iconFile;  // plain file name inside the theme folder ("icon.png"); empty = the ZERO mark
    QString fontFile;  // plain file name inside the theme folder ("font.ttf"); empty = the default font

    bool operator==(const Definition& o) const = default;

    /// A definition filled with the base theme's own colors and corner style (the editor's starting point).
    static Definition fromBase(Base base, const QString& name);
};

QString baseToString(Base base);
/// Anything but "minecraft" is Obsidian.
Base baseFromString(const QString& s);
/// The built-in spec of a base theme.
ThemeSpec baseSpec(Base base);
/// Extensions of the theme's own launcher icon (png svg ico) and UI font (ttf otf).
QStringList iconExtensions();
QStringList fontExtensions();

/// Names accepted as `overrides` keys (the ThemeSpec color members).
QStringList overrideKeys();

// ---- JSON ---------------------------------------------------------------------------------------------------------

/// Parses zero-theme.json. Fails (with a readable message) on unparsable JSON or a missing / unknown / greater "format".
/// Missing or invalid fields fall back to the base theme's values; values are clamped (dim 0..1, cardOpacity 0.4..1);
/// a background file name with a path separator is ignored.
Result<Definition> parseDefinition(const QByteArray& json);
QByteArray serializeDefinition(const Definition& def);
/// Reads <folder>/zero-theme.json.
Result<Definition> loadDefinition(const QString& folder);
/// Writes <folder>/zero-theme.json atomically (creates the folder).
Result<> saveDefinition(const QString& folder, const Definition& def);

// ---- Identity -----------------------------------------------------------------------------------------------------

/// Lowercase ascii [a-z0-9-] from a display name; never empty ("theme" as the fallback).
QString slugify(const QString& name);
/// `slugify(name)`, with -2, -3, ... appended until no folder of that name exists under `themesRoot`.
QString uniqueSlug(const QString& themesRoot, const QString& name);
inline QString idForFolderName(const QString& folderName)
{
    return QString::fromLatin1(kIdPrefix) + folderName;
}
/// "zero-custom-<slug>" ids only.
bool isCustomThemeId(const QString& id);
/// `name`, or "name (2)", "name (3)", ... when `taken` already holds that name (case-insensitive).
QString uniqueName(const QString& name, const QStringList& taken);

// ---- Derivation ---------------------------------------------------------------------------------------------------

/// Builds the full ThemeSpec of a definition (pure; see docs/zero/ARCHITECTURE.md "Custom themes"). `folder` is the theme
/// folder: the background image only counts when it is a readable-name file there; without a folder, there is no image.
ThemeSpec deriveSpec(const Definition& def, const QString& folder = {}, const QString& themeId = {});

// ---- Readability --------------------------------------------------------------------------------------------------

/// WCAG 2.x contrast ratio, 1..21 (alpha ignored).
double contrastRatio(const QColor& a, const QColor& b);

enum class Readability { Readable, HardToRead };
struct ReadabilityReport {
    Readability level = Readability::Readable;
    bool textOnBackground = true;  // text / bg        >= 4.5
    bool textOnSurface = true;     // text / surface   >= 4.5
    bool accentLabel = true;       // onAccent / accent >= 3.0
    /// One line naming the failed pairs ("Text on background, Button labels"), empty when readable. For labels / tooltips.
    QString summary;
};
ReadabilityReport readability(const ThemeSpec& spec);

}  // namespace Zero::Custom
