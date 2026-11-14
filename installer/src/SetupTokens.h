// SPDX-License-Identifier: GPL-3.0-only
// Design tokens of ZERO Setup. Colours are the Minecraft Theme of docs/zero/DESIGN.md ("Minecraft Theme"; the same
// values as launcher/ui/zero/ZeroTokens.h, namespace Mc), durations follow "Motion language" plus the installer spec
// of .claude/zero-briefs/brief-installer-v2.md. No colour, size or duration literal belongs in a widget: use these.
#pragma once

#include <QColor>
#include <QRgb>

namespace ZeroSetup::Tok {

// ---- colours (Minecraft Theme)
constexpr QRgb bg = 0xFF16171A;            // deep deepslate: the night sky
constexpr QRgb surface = 0xFF1F2125;
constexpr QRgb surfaceHover = 0xFF25282D;  // stone ground base
constexpr QRgb elevated = 0xFF2B2E33;
constexpr QRgb higher = 0xFF33373D;
constexpr QRgb pressed = 0xFF3A3F46;
constexpr QRgb border = 0xFF393D44;
constexpr QRgb borderStrong = 0xFF4A4F57;
constexpr QRgb bevelDark = 0xFF0D0E10;
constexpr QRgb accent = 0xFF67B84A;        // grass
constexpr QRgb accentHover = 0xFF78C95A;
constexpr QRgb accentPressed = 0xFF4F9A35;
constexpr QRgb onAccent = 0xFF0E1A09;
constexpr QRgb accent2 = 0xFF4FD0D6;       // diamond: focus ring, links
constexpr QRgb text = 0xFFEFEBE0;
constexpr QRgb textSecondary = 0xFFA8A397;
constexpr QRgb textTertiary = 0xFF757067;
constexpr QRgb danger = 0xFFE25A4F;
constexpr QRgb dangerHover = 0xFFEB7167;
constexpr QRgb dangerPressed = 0xFFC24A41;

constexpr int bevelLightAlpha = 18;        // white 7 %: the 1 px light top edge of raised controls
constexpr int dimAlpha = 170;              // overlay behind a prompt
constexpr double disabledOpacity = 0.4;

inline QColor c(QRgb v) { return QColor::fromRgba(v); }
inline QColor withAlpha(QRgb v, double a)
{
    QColor col = QColor::fromRgba(v);
    col.setAlphaF(a);
    return col;
}
inline QColor mix(QRgb a, QRgb b, double t)  // t = 0 -> a, 1 -> b
{
    const QColor x = QColor::fromRgba(a), y = QColor::fromRgba(b);
    return QColor::fromRgbF(x.redF() + (y.redF() - x.redF()) * t, x.greenF() + (y.greenF() - x.greenF()) * t,
                            x.blueF() + (y.blueF() - x.blueF()) * t);
}

// ---- type
constexpr const char* fontPrimary = "Segoe UI Variable Text";
constexpr const char* fontFallback = "Segoe UI";
constexpr int pxTitleStrip = 12;
constexpr int pxBody = 14;
constexpr int pxCaption = 12;
constexpr int pxTagline = 15;
constexpr int pxButton = 15;
constexpr int pxWordmark = 50;       // ZERO Pixel: one font pixel = em/10, so multiples of 10 stay crisp
constexpr int wordmarkSpacing = 6;   // letter-spacing
constexpr int pxPromptTitle = 17;

// ---- window geometry (logical px)
constexpr int winW = 760;
constexpr int winH = 460;
constexpr int titleH = 36;
constexpr int titleButtonW = 40;
constexpr int titleButtonH = 30;
constexpr int groundH = 46;          // stone strip at the bottom
constexpr int grassH = 6;            // accentPressed edge on top of it
constexpr int starCount = 30;
constexpr int starSize = 3;
constexpr double starMinAlpha = 0.15;
constexpr double starMaxAlpha = 0.9;
constexpr double starStaticAlpha = 0.55;  // reduced motion
constexpr double starSkyFraction = 0.55;  // stars live in the top 55 % of the window
constexpr int stoneTile = 64;
constexpr int stoneBlock = 4;
constexpr double stoneNoise = 0.016;
constexpr quint32 stoneSeed = 0x5EED1234u;

constexpr int markCell = 6;          // 16 cells -> 96 px
constexpr int markTop = 60;
constexpr int wordmarkTop = 166;
constexpr int taglineTop = 232;
constexpr int buttonW = 220;
constexpr int buttonH = 48;          // body of the primary button (the widget adds the focus margin)
constexpr int buttonMargin = 3;      // room for the focus ring around a button body
constexpr int smallButtonW = 120;
constexpr int smallButtonH = 36;
constexpr int primaryTop = 282;
constexpr int radiusNotch = 2;       // painted notched corner
constexpr int bevelBottom = 3;
constexpr int focusRing = 2;

constexpr int barW = 440;
constexpr int barH = 24;
constexpr int barSegments = 20;
constexpr int barGap = 2;
constexpr int barPad = 4;

constexpr int panelX = 130;
constexpr int panelW = 500;

// ---- motion (ms / px)
constexpr int dropFromPx = 60;
constexpr int dropMs = 420;
constexpr int dropStaggerMs = 25;
constexpr int dropMaxBuildMs = 1400;   // the stagger shrinks so the whole mark is built within this (252 blocks x 25 ms = 6.3 s)
constexpr double dropOvershoot = 1.0;  // OutBack, small
constexpr int slideMs = 320;           // Options in / out, OutCubic
constexpr int flashMs = 300;           // newly lit XP segment
constexpr int twinkleMs = 2400;
constexpr int bobMs = 1600;
constexpr double bobPx = 2.0;
constexpr int pressMs = 90;
constexpr int fadeMs = 200;            // tagline / page cross-fade
constexpr int frameMs = 16;
constexpr int starFrameMs = 33;
constexpr int heroSlidePx = 140;       // how far the hero drifts left while Options slides in

}  // namespace ZeroSetup::Tok
