// SPDX-License-Identifier: GPL-3.0-only
/*
 *  ZERO Launcher - design tokens ("Obsidian")
 *  Source of truth: docs/zero/DESIGN.md
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

#include <QColor>
#include <QString>
#include <QtGlobal>

namespace Zero {

// ---------------------------------------------------------------------------------------------
// Obsidian defaults. Widgets must NOT read these directly: they read the active ThemeSpec
// (Zero::Theme::current()) so that Theme Studio can edit it at runtime.
// ---------------------------------------------------------------------------------------------
namespace Tokens {

// Colors (0xAARRGGBB)
constexpr QRgb bg = 0xFF101010;
constexpr QRgb surface = 0xFF191919;
constexpr QRgb surfaceHover = 0xFF1F1F1F;
constexpr QRgb elevated = 0xFF222222;
constexpr QRgb higher = 0xFF292929;
constexpr QRgb pressed = 0xFF2E2E2E;
constexpr QRgb border = 0xFF303030;
constexpr QRgb borderStrong = 0xFF3A3A3A;
constexpr QRgb accent = 0xFFD4D4D4;
constexpr QRgb accentHover = 0xFFE8E8E8;
constexpr QRgb accentPressed = 0xFFBDBDBD;
constexpr QRgb onAccent = 0xFF101010;
constexpr QRgb text = 0xFFF5F5F5;
constexpr QRgb textSecondary = 0xFF929292;
constexpr QRgb textTertiary = 0xFF6B6B6B;
constexpr QRgb success = 0xFF7BC59A;
constexpr QRgb warning = 0xFFD9B36C;
constexpr QRgb danger = 0xFFE07A7A;
constexpr QRgb info = 0xFF8AB4D9;

// Status tint opacity (badges, danger hover): 12%
constexpr double statusTint = 0.12;

// Typography
constexpr const char* fontFamilyPrimary = "Segoe UI Variable Text";
constexpr const char* fontFamilyFallback = "Segoe UI";

struct TypeRole {
    int px;
    int weight;             // CSS-style 100..900
    double letterSpacingPx; // absolute letter spacing
    bool uppercase;
};
constexpr TypeRole display{ 28, 600, 0.0, false };
constexpr TypeRole title{ 20, 600, 0.0, false };
constexpr TypeRole heading{ 15, 600, 0.0, false };
constexpr TypeRole body{ 13, 400, 0.0, false };
constexpr TypeRole label{ 13, 500, 0.0, false };
constexpr TypeRole caption{ 11, 400, 0.0, false };
constexpr TypeRole overline{ 10, 600, 0.8, true };

// Spacing scale (px)
constexpr int spaceXs = 4;
constexpr int spaceSm = 8;
constexpr int spaceMd = 12;
constexpr int spaceLg = 16;
constexpr int spaceXl = 24;
constexpr int spaceXxl = 32;
constexpr int spaceXxxl = 48;

// Radius (px)
constexpr int radiusSm = 4;
constexpr int radiusMd = 6;
constexpr int radiusLg = 10;
constexpr int radiusXl = 14;

// Component metrics (px)
constexpr int controlHeight = 32;   // buttons, inputs (md)
constexpr int controlHeightLg = 40; // lg buttons
constexpr int navRailWidth = 220;
constexpr int navRailCollapsedWidth = 64;
constexpr int navItemHeight = 36;
constexpr int navIconSize = 18;
constexpr int scrollbarWidth = 8;
constexpr int progressHeight = 4;
constexpr int badgeHeight = 20;
constexpr int emptyStateIconSize = 40;
constexpr int emptyStateMaxWidth = 360;

// Motion (ms). Docs: DESIGN.md "Motion language". durHover/durPage are the pre-motion-system names, kept for
// existing users (new code uses the dur* tokens below).
constexpr int durPress = 90;      // press feedback
constexpr int durFast = 140;      // hover, toggles, chips
constexpr int durBase = 200;      // page enter, panels, indicator glide
constexpr int durSlow = 280;      // dialogs, palette, toasts
constexpr int durTheme = 360;     // theme transition
constexpr int durDissolve = 420;  // Minecraft pixel dissolve
constexpr int durCascade = 380;   // page enter (top-to-bottom reveal)
constexpr int durReveal = 260;    // one list item / card fading up into place
constexpr int durStagger = 28;    // delay between two neighbouring items of a reveal
constexpr int durHover = 120;
constexpr int durPage = 180;
constexpr int durToast = 4000;
constexpr int durSpinner = 1000;

// Elevation (shadow for popovers, toasts, dialogs)
constexpr int shadowBlur = 24;
constexpr int shadowOffsetY = 8;
constexpr int shadowAlpha = 140; // rgba(0,0,0,0.55)

} // namespace Tokens

// ---------------------------------------------------------------------------------------------
// "Minecraft Theme" palette (docs/zero/DESIGN.md, Design v2). Only ZeroTokens.h may hold hex values.
// ---------------------------------------------------------------------------------------------
namespace Tokens {
namespace Minecraft {

constexpr QRgb bg = 0xFF16171A;
constexpr QRgb surface = 0xFF1F2125;
constexpr QRgb surfaceHover = 0xFF25282D;
constexpr QRgb elevated = 0xFF2B2E33;
constexpr QRgb higher = 0xFF33373D;
constexpr QRgb pressed = 0xFF3A3F46;
constexpr QRgb border = 0xFF393D44;
constexpr QRgb borderStrong = 0xFF4A4F57;
constexpr QRgb bevelDark = 0xFF0D0E10;
constexpr QRgb accent = 0xFF67B84A;
constexpr QRgb accentHover = 0xFF78C95A;
constexpr QRgb accentPressed = 0xFF4F9A35;
constexpr QRgb onAccent = 0xFF0E1A09;
constexpr QRgb accent2 = 0xFF4FD0D6;
constexpr QRgb highlight = 0xFFE6B83E;
constexpr QRgb text = 0xFFEFEBE0;
constexpr QRgb textSecondary = 0xFFA8A397;
constexpr QRgb textTertiary = 0xFF757067;
constexpr QRgb success = 0xFF3CC77A;
constexpr QRgb warning = 0xFFE6B83E;
constexpr QRgb danger = 0xFFE25A4F;
constexpr QRgb info = 0xFF5DB3E8;

constexpr int radiusSm = 2;
constexpr int radiusMd = 2;
constexpr int radiusLg = 3;
constexpr int radiusXl = 4;

} // namespace Minecraft

// Accent presets offered by the theme editor (muted, readable on both dark and light backgrounds).
struct AccentPreset {
    const char* name;  // untranslated; the editor translates it
    QRgb rgb;
};
inline constexpr AccentPreset accentPresets[] = {
    { "Blue", 0xFF5B8DEF },
    { "Green", 0xFF5FBF7F },
    { "Purple", 0xFF9B7BE0 },
    { "Gold", 0xFFE0B04A },
    { "Red", 0xFFE0645C },
};

// Light edge of a bevel: white at 7%.
constexpr double bevelLightAlpha = 0.07;
// Size of the square step cut into each corner of a Notched panel (px).
constexpr int notchStep = 2;

} // namespace Tokens

// ---------------------------------------------------------------------------------------------
// Runtime theme specification. Everything downstream (stylesheet, palette, components) takes this.
// ---------------------------------------------------------------------------------------------
enum class Density { Compact, Comfortable, Spacious };

/// Type roles used by Style::font() and the `zeroRole` property.
enum class Role { Display, Title, Heading, Body, Label, Caption, Overline };

/// Corner style of painted panels (see Zero::Paint::panelPath).
enum class Corners { Rounded, Notched };

constexpr quint32 roleBit(Role r)
{
    return quint32(1) << static_cast<int>(r);
}

struct ThemeSpec {
    QColor bg, surface, surfaceHover, elevated, higher, pressed, border, borderStrong;
    QColor accent, accentHover, accentPressed, onAccent;
    QColor text, textSecondary, textTertiary;
    QColor success, warning, danger, info;
    QString id = QStringLiteral("obsidian");
    QString name = QStringLiteral("Obsidian");
    QColor accent2, highlight, bevelDark;
    int radiusSm = Tokens::radiusSm, radiusMd = Tokens::radiusMd, radiusLg = Tokens::radiusLg, radiusXl = Tokens::radiusXl;
    Corners corners = Corners::Rounded;
    bool bevel = false;
    bool texture = false;
    quint32 pixelRoles = 0;  // bitmask of roleBit(Role): roles drawn with the "ZERO Pixel" font
    Density density = Density::Comfortable;
    bool reduceMotion = false;

    // Custom (user-made) themes only; the built-in themes leave these at their defaults.
    QString baseId;             // built-in theme this one derives from ("obsidian" / "minecraft"): selects the overlay stylesheet; empty = id
    QString backgroundImage;    // absolute path of the window background image; empty = flat bg (or the stone texture)
    QString backgroundVideo;    // absolute path of the window background video (muted loop); empty = none. Wins over the image
    double backgroundDim = 0.0; // 0..1: bg color laid over the image / video
    double cardOpacity = 1.0;   // alpha of `surface` / `surfaceHover` (cards and panels); everything else stays opaque
    bool videoSound = false;    // the background video plays its sound (only with a video; default off)
    double videoVolume = 0.5;   // 0..1, only meaningful with videoSound
    QString appIcon;            // absolute path of the theme's own launcher icon (window, taskbar, rail header); empty = the ZERO mark
    QString fontFile;           // absolute path of the theme's own UI font file; empty = the default font
    QString fontFamily;         // family registered from fontFile (resolved when the theme is applied); empty = default or failed to load

    /// The id that picks the overlay stylesheet and the pixel-art touches (a custom theme answers with its base theme).
    QString styleId() const { return baseId.isEmpty() ? id : baseId; }
    /// `surface` / `surfaceHover` without the card opacity: for popups, menus and overlays that must never be see-through.
    QColor surfaceSolid() const { return opaque(surface); }
    QColor surfaceHoverSolid() const { return opaque(surfaceHover); }

    static QColor opaque(QColor c)
    {
        c.setAlpha(255);
        return c;
    }

    static ThemeSpec obsidian()
    {
        ThemeSpec s;
        s.bg = QColor::fromRgba(Tokens::bg);
        s.surface = QColor::fromRgba(Tokens::surface);
        s.surfaceHover = QColor::fromRgba(Tokens::surfaceHover);
        s.elevated = QColor::fromRgba(Tokens::elevated);
        s.higher = QColor::fromRgba(Tokens::higher);
        s.pressed = QColor::fromRgba(Tokens::pressed);
        s.border = QColor::fromRgba(Tokens::border);
        s.borderStrong = QColor::fromRgba(Tokens::borderStrong);
        s.accent = QColor::fromRgba(Tokens::accent);
        s.accentHover = QColor::fromRgba(Tokens::accentHover);
        s.accentPressed = QColor::fromRgba(Tokens::accentPressed);
        s.onAccent = QColor::fromRgba(Tokens::onAccent);
        s.text = QColor::fromRgba(Tokens::text);
        s.textSecondary = QColor::fromRgba(Tokens::textSecondary);
        s.textTertiary = QColor::fromRgba(Tokens::textTertiary);
        s.success = QColor::fromRgba(Tokens::success);
        s.warning = QColor::fromRgba(Tokens::warning);
        s.danger = QColor::fromRgba(Tokens::danger);
        s.info = QColor::fromRgba(Tokens::info);
        s.accent2 = s.accent;
        s.highlight = s.warning;
        s.bevelDark = s.bg;
        return s;
    }

    static ThemeSpec minecraft()
    {
        namespace M = Tokens::Minecraft;
        ThemeSpec s;
        s.id = QStringLiteral("minecraft");
        s.name = QStringLiteral("Minecraft");
        s.bg = QColor::fromRgba(M::bg);
        s.surface = QColor::fromRgba(M::surface);
        s.surfaceHover = QColor::fromRgba(M::surfaceHover);
        s.elevated = QColor::fromRgba(M::elevated);
        s.higher = QColor::fromRgba(M::higher);
        s.pressed = QColor::fromRgba(M::pressed);
        s.border = QColor::fromRgba(M::border);
        s.borderStrong = QColor::fromRgba(M::borderStrong);
        s.accent = QColor::fromRgba(M::accent);
        s.accentHover = QColor::fromRgba(M::accentHover);
        s.accentPressed = QColor::fromRgba(M::accentPressed);
        s.onAccent = QColor::fromRgba(M::onAccent);
        s.text = QColor::fromRgba(M::text);
        s.textSecondary = QColor::fromRgba(M::textSecondary);
        s.textTertiary = QColor::fromRgba(M::textTertiary);
        s.success = QColor::fromRgba(M::success);
        s.warning = QColor::fromRgba(M::warning);
        s.danger = QColor::fromRgba(M::danger);
        s.info = QColor::fromRgba(M::info);
        s.accent2 = QColor::fromRgba(M::accent2);
        s.highlight = QColor::fromRgba(M::highlight);
        s.bevelDark = QColor::fromRgba(M::bevelDark);
        s.radiusSm = M::radiusSm;
        s.radiusMd = M::radiusMd;
        s.radiusLg = M::radiusLg;
        s.radiusXl = M::radiusXl;
        s.corners = Corners::Notched;
        s.bevel = true;
        s.texture = true;
        s.pixelRoles = roleBit(Role::Title) | roleBit(Role::Overline);
        return s;
    }

    bool usesPixelFont(Role r) const { return (pixelRoles & roleBit(r)) != 0; }

    int itemPaddingV() const
    {
        switch (density) {
            case Density::Compact:
                return 3;
            case Density::Spacious:
                return 8;
            default:
                return 5;
        }
    }
};

/// Mixes two colors (t = 0 -> a, t = 1 -> b). Used for animated hover transitions and 12% status tints.
inline QColor mix(const QColor& a, const QColor& b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t, a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

/// Color with alpha replaced (alpha 0..1).
inline QColor withAlpha(QColor c, qreal alpha)
{
    c.setAlphaF(alpha);
    return c;
}

} // namespace Zero
