// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroScene.h"

#include "ZeroMarkData.h"
#include "ZeroPixelFont.h"

#include <QColor>
#include <QEasingCurve>
#include <QFont>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace Zero::Scene {

namespace {

constexpr quint32 kStarSeed = 0x2E50C0DEu;
constexpr quint32 kStoneSeed = 0x5EED1234u;
constexpr int kStoneTileBlocks = 16;  // the noise tile is 16 x 16 stone pixels
constexpr double kStoneNoise = 0.016;
constexpr double kSoilShadowAlpha = 0.45;

QColor c(QRgb v)
{
    return QColor::fromRgba(v);
}

QColor withAlpha(QRgb v, double a)
{
    QColor col = QColor::fromRgba(v);
    col.setAlphaF(a);
    return col;
}

}  // namespace

QVector<Star> makeStars(int w, int h, int count, const QRectF& keepOut, double skyFraction, int starSize)
{
    QVector<Star> stars;
    quint32 state = kStarSeed;
    auto next = [&state]() {
        state = state * 1664525u + 1013904223u;
        return (state >> 8) & 0xFFFF;
    };
    const double skyBottom = h * skyFraction;
    int guard = 0;
    while (stars.size() < count && ++guard < count * 200) {
        const QPointF pt(12 + next() / 65535.0 * (w - 24 - starSize), 8 + next() / 65535.0 * (skyBottom - 8 - starSize));
        const double phase = next() / 65535.0;
        if (keepOut.contains(pt))
            continue;
        stars.push_back({ QPointF(std::floor(pt.x()), std::floor(pt.y())), phase });
    }
    return stars;
}

void paintStars(QPainter& p, const QVector<Star>& stars, int starSize, qint64 timeMs, bool motion, QRgb color, double opacity)
{
    p.save();
    p.setPen(Qt::NoPen);
    for (const Star& s : stars) {
        double a = kStarStaticAlpha;
        if (motion) {
            const double ph = std::fmod(double(timeMs) / kTwinkleMs + s.phase, 1.0);
            a = kStarMinAlpha + (kStarMaxAlpha - kStarMinAlpha) * (0.5 - 0.5 * std::cos(2.0 * M_PI * ph));
        }
        p.setBrush(withAlpha(color, a * opacity));
        p.drawRect(QRectF(s.pos, QSizeF(starSize, starSize)));
    }
    p.restore();
}

void paintGround(QPainter& p, int width, int top, int height, int grassH, int block, const Palette& pal)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    const int tileBlocks = kStoneTileBlocks;
    double noise[kStoneTileBlocks][kStoneTileBlocks];
    quint32 state = kStoneSeed;
    for (int by = 0; by < tileBlocks; ++by)
        for (int bx = 0; bx < tileBlocks; ++bx) {
            state = state * 1664525u + 1013904223u;
            noise[by][bx] = (((state >> 8) & 0xFFFF) / 65535.0 * 2.0 - 1.0) * kStoneNoise;
        }
    const QColor base = c(pal.stone);
    const int bottom = top + height;
    for (int by = 0; top + by * block < bottom; ++by)
        for (int bx = 0; bx * block < width; ++bx) {
            const double d = noise[by % tileBlocks][bx % tileBlocks];
            const QColor col = QColor::fromRgbF(std::clamp(base.redF() + d, 0.0, 1.0), std::clamp(base.greenF() + d, 0.0, 1.0),
                                                std::clamp(base.blueF() + d, 0.0, 1.0));
            p.fillRect(QRect(bx * block, top + by * block, block, block), col);
        }
    const int edge = std::max(2, block / 2);
    p.fillRect(QRect(0, top, width, grassH), c(pal.grass));
    p.fillRect(QRect(0, top + grassH, width, edge), withAlpha(pal.shadow, kSoilShadowAlpha));  // soil shadow under the grass
    // a few lighter grass pixels on the edge, on the stone grid
    state = kStoneSeed ^ 0xA5A5u;
    for (int bx = 0; bx * block < width; ++bx) {
        state = state * 1664525u + 1013904223u;
        if (((state >> 8) & 7) == 0)
            p.fillRect(QRect(bx * block, top, block, edge), c(pal.grassLight));
    }
    p.restore();
}

const QVector<Block>& markBlocks()
{
    static const QVector<Block> blocks = [] {
        QVector<Block> out;
        for (int y = 0; y < MarkData::kCells; ++y)
            for (int x = 0; x < MarkData::kCells; ++x) {
                const char ch = MarkData::kRows[y][x];
                if (ch != '.')
                    out.push_back({ QPoint(x, y), MarkData::kPalette[ch - 'a'] });
            }
        return out;
    }();
    return blocks;
}

double markStaggerMs(int total)
{
    return std::min<double>(kDropStaggerMs, double(kDropMaxBuildMs) / std::max(1, total));
}

int markBuildMs(int total)
{
    return int(double(total) * markStaggerMs(total)) + kDropMs;
}

void paintMark(QPainter& p, QPointF origin, int cell, qint64 elapsedMs, bool motion, double bobPx, double dropFromPx, double opacity)
{
    const QVector<Block>& blocks = markBlocks();
    const int total = int(blocks.size());
    const double stagger = markStaggerMs(total);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(Qt::NoPen);
    QEasingCurve ease(QEasingCurve::OutBack);
    ease.setOvershoot(kDropOvershoot);
    for (int i = 0; i < total; ++i) {
        const Block& b = blocks[i];
        double k = 1.0, op = 1.0, dy = 0;
        if (motion) {
            const double lin = double(elapsedMs - qint64(i * stagger)) / kDropMs;
            if (lin <= 0)
                continue;
            k = ease.valueForProgress(std::clamp(lin, 0.0, 1.0));
            op = std::min(1.0, lin * 3.0);
            dy = -dropFromPx * (1.0 - k);
        }
        p.setOpacity(op * opacity);
        p.setBrush(c(b.color));
        p.drawRect(QRectF(origin.x() + b.cell.x() * cell, origin.y() + b.cell.y() * cell + dy + bobPx, cell, cell));
    }
    p.restore();
}

double bobOffset(qint64 timeMs, bool motion, double amplitudePx)
{
    if (!motion)
        return 0.0;
    return amplitudePx * std::sin(2.0 * M_PI * double(timeMs % kBobMs) / kBobMs);
}

int snapPixelSize(int designPx, double dpr)
{
    const int lo = int(std::lround(designPx * 0.72)), hi = int(std::lround(designPx * 1.28));
    int best = designPx;
    double bestScore = 1e9;
    for (int px = lo; px <= hi; ++px) {
        const double dev = px * dpr;
        const double off = std::abs(dev / 10.0 - std::round(dev / 10.0)) * 10.0;
        const double score = off + std::abs(px - designPx) * 0.05;
        if (score <= bestScore) {
            bestScore = score;
            best = px;
        }
    }
    return best;
}

void paintWordmark(QPainter& p, const QRectF& box, int pixelSize, int spacing, int shadowPx, double alpha, const Palette& pal)
{
    p.save();
    QFont f(pixelFamily());
    f.setPixelSize(pixelSize);
    f.setLetterSpacing(QFont::AbsoluteSpacing, spacing);
    f.setStyleStrategy(QFont::NoAntialias);  // a pixel font: hard edges
    p.setFont(f);
    const QRectF shadow = box.translated(shadowPx, shadowPx);
    p.setOpacity(alpha);
    p.setPen(c(pal.shadow));
    p.drawText(shadow.translated(spacing / 2.0, 0), Qt::AlignHCenter | Qt::AlignTop, QStringLiteral("ZERO"));
    p.setPen(c(pal.text));
    p.drawText(box.translated(spacing / 2.0, 0), Qt::AlignHCenter | Qt::AlignTop, QStringLiteral("ZERO"));
    p.restore();
}

}  // namespace Zero::Scene
