// SPDX-License-Identifier: GPL-3.0-only
// ZERO - the brand "night scene" shared by ZERO Setup (installer/) and the launcher's first-run intro: night sky with twinkling
// stars, stone ground with a grass edge, the pixel ZERO mark built from blocks that drop in, and the wordmark.
//
// This file is compiled into both programs, so it needs Qt (Core, Gui) and ZeroPixelFont only: no Application, no theme state, no
// Zero::Motion. Colours come in as a Palette (the Minecraft palette of ZeroTokens.h in the launcher, SetupTokens.h in the
// installer); time comes in as plain milliseconds. Every function paints with explicit geometry, so each program lays the
// scene out its own way (fixed 760x460 window there, a window-filling overlay here).
#pragma once

#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QRgb>
#include <QVector>

class QPainter;

namespace Zero::Scene {

/// The six colours the scene is painted with (0xAARRGGBB).
struct Palette
{
    QRgb sky;          // night sky (Minecraft bg)
    QRgb stone;        // stone ground base (surfaceHover)
    QRgb grass;        // grass edge (accentPressed)
    QRgb grassLight;   // lighter grass pixels on the edge (accent)
    QRgb shadow;       // soil shadow under the grass, wordmark drop shadow (bevelDark)
    QRgb text;         // stars and wordmark (text)
};

// ---- look constants shared by both programs (ms / px at the design size of one mark cell = 6 px)
constexpr int kTwinkleMs = 2400;
constexpr double kStarMinAlpha = 0.15;
constexpr double kStarMaxAlpha = 0.9;
constexpr double kStarStaticAlpha = 0.55;  // reduced motion
constexpr int kDropMs = 420;               // one block falling in
constexpr int kDropStaggerMs = 25;         // delay between two neighbouring blocks (shrinks so the build fits kDropMaxBuildMs)
constexpr int kDropMaxBuildMs = 1400;
constexpr double kDropOvershoot = 1.0;     // OutBack, small
constexpr int kBobMs = 1600;
constexpr int kDesignCell = 6;             // mark cell the other design lengths (drop distance, bob, wordmark ...) are given for
constexpr int kDesignDropFromPx = 60;
constexpr double kDesignBobPx = 2.0;
constexpr int kDesignWordmarkPx = 50;
constexpr int kDesignWordmarkSpacing = 6;

struct Star
{
    QPointF pos;
    double phase;
};

struct Block
{
    QPoint cell;
    QRgb color;
};

/// Deterministic stars (the same sky on every run) inside the top `skyFraction` of a w x h area, kept out of `keepOut`.
QVector<Star> makeStars(int w, int h, int count, const QRectF& keepOut, double skyFraction, int starSize);

/// Draws the stars. `timeMs` drives the twinkle (ignored when `motion` is false: constant alpha). `opacity` scales all of them.
void paintStars(QPainter& p, const QVector<Star>& stars, int starSize, qint64 timeMs, bool motion, QRgb color, double opacity = 1.0);

/// The stone strip (4 px "pixels" with +-1.6 % luminance noise from a seeded generator) with its grass edge, painted from y = `top`
/// down `height` px over `width` px: `block` is the stone pixel size, `grassH` the height of the grass edge.
void paintGround(QPainter& p, int width, int top, int height, int grassH, int block, const Palette& pal);

/// The 252 coloured cells of the mark, row by row.
const QVector<Block>& markBlocks();
/// Delay between two neighbouring blocks for a mark of `total` blocks.
double markStaggerMs(int total);
/// Time from the start until the last block has landed.
int markBuildMs(int total);

/// Draws the mark with its top-left corner at `origin`, `cell` px per cell. `elapsedMs` is the time since the build started;
/// with `motion` false every block stands in place. `bobPx` shifts the whole mark vertically, `dropFromPx` is how far above its
/// place a block starts, `opacity` scales everything.
void paintMark(QPainter& p, QPointF origin, int cell, qint64 elapsedMs, bool motion, double bobPx, double dropFromPx, double opacity = 1.0);

/// Vertical bob offset of the idle mark at `timeMs` (0 when `motion` is false).
double bobOffset(qint64 timeMs, bool motion, double amplitudePx);

/// A pixel size near `designPx` (within +-28 %) that draws each ZERO Pixel font pixel as a whole number of device pixels at this
/// device pixel ratio (a multiple of 10 device pixels).
int snapPixelSize(int designPx, double dpr);

/// The wordmark "ZERO" in the ZERO Pixel font, centred in `box` (top-aligned), with a drop shadow `shadowPx` px down and right, at
/// `alpha`. `spacing` is the letter spacing in px.
void paintWordmark(QPainter& p, const QRectF& box, int pixelSize, int spacing, int shadowPx, double alpha, const Palette& pal);

}  // namespace Zero::Scene
