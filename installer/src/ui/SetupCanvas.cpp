// SPDX-License-Identifier: GPL-3.0-only
#include "SetupCanvas.h"

#include "MarkData.h"
#include "Motion.h"
#include "PixelWidgets.h"
#include "SetupTokens.h"
#include "ZeroPixelFont.h"

#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <cmath>

namespace ZeroSetup {

namespace T = Tok;

namespace {

// Pixel sizes of the ZERO Pixel font that are a multiple of 10 *device* pixels draw each font pixel as a whole number
// of screen pixels. Pick the one nearest to the design size for this scale factor.
int wordmarkPixelSize(double dpr)
{
    int best = T::pxWordmark;
    double bestScore = 1e9;
    for (int px = 36; px <= 64; ++px) {
        const double dev = px * dpr;
        const double off = std::abs(dev / 10.0 - std::round(dev / 10.0)) * 10.0;
        const double score = off + std::abs(px - T::pxWordmark) * 0.05;
        if (score <= bestScore) {
            bestScore = score;
            best = px;
        }
    }
    return best;
}

}  // namespace

SetupCanvas::SetupCanvas(QWidget* parent) : QWidget(parent), m_timer(new QTimer(this))
{
    setFixedSize(T::winW, T::winH);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent);

    // deterministic stars (same sky on every run), kept out of the box where the hero and tagline are
    quint32 state = 0x2E50C0DEu;
    auto next = [&state]() {
        state = state * 1664525u + 1013904223u;
        return (state >> 8) & 0xFFFF;
    };
    const QRectF keepOut(230, 30, 300, 240);
    const double skyBottom = T::winH * T::starSkyFraction;
    while (m_stars.size() < T::starCount) {
        const QPointF pt(12 + next() / 65535.0 * (T::winW - 24 - T::starSize), 8 + next() / 65535.0 * (skyBottom - 8 - T::starSize));
        const double phase = next() / 65535.0;
        if (keepOut.contains(pt))
            continue;
        m_stars.push_back({ QPointF(std::floor(pt.x()), std::floor(pt.y())), phase });
    }

    for (int y = 0; y < MarkData::kCells; ++y)
        for (int x = 0; x < MarkData::kCells; ++x) {
            const char ch = MarkData::kRows[y][x];
            if (ch != '.')
                m_blocks.push_back({ QPoint(x, y), MarkData::kPalette[ch - 'a'] });
        }

    m_markStart = Motion::now();
    m_timer->setInterval(T::frameMs);
    connect(m_timer, &QTimer::timeout, this, &SetupCanvas::tick);
    if (Motion::enabled() && !Motion::frozen())
        m_timer->start();
}

void SetupCanvas::setWindowCaption(const QString& s)
{
    m_caption = s;
    update(QRect(0, 0, 300, T::titleH));
}

void SetupCanvas::setTagline(const QString& s)
{
    if (s == m_tagline)
        return;
    m_prevTagline = m_tagline;
    m_tagline = s;
    m_tagChangeMs = Motion::now();
    update(QRect(0, T::taglineTop - 8, T::winW, 40));
}

void SetupCanvas::setHeroShift(double t)
{
    m_shift = t;
    update(QRect(0, 0, T::winW, T::taglineTop + 40));
}

void SetupCanvas::setBobbing(bool on)
{
    m_bob = on;
    update(QRect(0, 0, T::winW, T::taglineTop));
}

void SetupCanvas::setMarkStart(qint64 ms)
{
    m_markStart = ms;
    update();
}

void SetupCanvas::setMarkVisible(bool on)
{
    m_markVisible = on;
    update(QRect(0, 0, T::winW, T::taglineTop + 40));
}

void SetupCanvas::tick()
{
    ++m_tickCount;
    const qint64 now = Motion::now();
    // stars: every other frame (about 30 fps)
    if ((m_tickCount & 1) == 0) {
        QRegion r;
        for (const Star& s : std::as_const(m_stars))
            r += QRect(int(s.pos.x()) - 1, int(s.pos.y()) - 1, T::starSize + 2, T::starSize + 2);
        update(r);
    }
    const double buildMs = double(m_blocks.size()) * std::min<double>(T::dropStaggerMs, double(T::dropMaxBuildMs) / std::max<qsizetype>(1, m_blocks.size()));
    const bool dropping = now - m_markStart < buildMs + T::dropMs + 50;
    const bool fading = now - m_tagChangeMs < T::fadeMs + 50;
    if (dropping || m_bob || fading || m_shift > 0)
        update(QRect(100, 0, 560, T::taglineTop + 40));
}

void SetupCanvas::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && e->position().y() < T::titleH)
        emit dragRequested();
    QWidget::mousePressEvent(e);
}

void SetupCanvas::rebuildBackground()
{
    const qreal dpr = devicePixelRatioF();
    m_bgDpr = dpr;
    m_bg = QPixmap(QSize(int(std::ceil(T::winW * dpr)), int(std::ceil(T::winH * dpr))));
    m_bg.setDevicePixelRatio(dpr);
    QPainter p(&m_bg);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(QRect(0, 0, T::winW, T::winH), T::c(T::bg));

    // stone strip: 4 px blocks with +-1.6 % luminance noise from a seeded LCG, a 16 x 16 block tile (DESIGN.md "Texture")
    const int tileBlocks = T::stoneTile / T::stoneBlock;
    double noise[16][16];
    quint32 state = T::stoneSeed;
    for (int by = 0; by < tileBlocks; ++by)
        for (int bx = 0; bx < tileBlocks; ++bx) {
            state = state * 1664525u + 1013904223u;
            noise[by][bx] = (((state >> 8) & 0xFFFF) / 65535.0 * 2.0 - 1.0) * T::stoneNoise;
        }
    const QColor base = T::c(T::surfaceHover);
    const int top = T::winH - T::groundH;
    for (int by = 0; top + by * T::stoneBlock < T::winH; ++by)
        for (int bx = 0; bx * T::stoneBlock < T::winW; ++bx) {
            const double d = noise[by % tileBlocks][bx % tileBlocks];
            const QColor c = QColor::fromRgbF(std::clamp(base.redF() + d, 0.0, 1.0), std::clamp(base.greenF() + d, 0.0, 1.0),
                                              std::clamp(base.blueF() + d, 0.0, 1.0));
            p.fillRect(QRect(bx * T::stoneBlock, top + by * T::stoneBlock, T::stoneBlock, T::stoneBlock), c);
        }
    p.fillRect(QRect(0, top, T::winW, T::grassH), T::c(T::accentPressed));
    p.fillRect(QRect(0, top + T::grassH, T::winW, 2), T::withAlpha(T::bevelDark, 0.45));  // soil shadow under the grass
    // a few lighter grass pixels on the edge, on a 4 px grid
    state = T::stoneSeed ^ 0xA5A5u;
    for (int bx = 0; bx * T::stoneBlock < T::winW; ++bx) {
        state = state * 1664525u + 1013904223u;
        if (((state >> 8) & 7) == 0)
            p.fillRect(QRect(bx * T::stoneBlock, top, T::stoneBlock, 2), T::c(T::accent));
    }
}

void SetupCanvas::paintWordmark(QPainter& p, double alpha)
{
    QFont f(Zero::pixelFamily());
    f.setPixelSize(wordmarkPixelSize(devicePixelRatioF()));
    f.setLetterSpacing(QFont::AbsoluteSpacing, T::wordmarkSpacing);
    f.setStyleStrategy(QFont::NoAntialias);  // a pixel font: hard edges
    p.setFont(f);
    const QRectF box(0, T::wordmarkTop, T::winW, 60);
    const QRectF shadow = box.translated(2, 2);
    p.setOpacity(alpha);
    p.setPen(T::c(T::bevelDark));
    p.drawText(shadow.translated(T::wordmarkSpacing / 2.0, 0), Qt::AlignHCenter | Qt::AlignTop, QStringLiteral("ZERO"));
    p.setPen(T::c(T::text));
    p.drawText(box.translated(T::wordmarkSpacing / 2.0, 0), Qt::AlignHCenter | Qt::AlignTop, QStringLiteral("ZERO"));
    p.setOpacity(1.0);
}

void SetupCanvas::paintMark(QPainter& p, double baseOpacity)
{
    const int total = int(m_blocks.size());
    const double stagger = std::min<double>(T::dropStaggerMs, double(T::dropMaxBuildMs) / std::max(1, total));
    const double originX = (T::winW - MarkData::kCells * T::markCell) / 2.0;
    double bob = 0;
    if (m_bob && Motion::enabled())
        bob = T::bobPx * std::sin(2.0 * M_PI * double(Motion::now() % T::bobMs) / T::bobMs);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(Qt::NoPen);
    for (int i = 0; i < total; ++i) {
        const Block& b = m_blocks[i];
        double k = 1.0, opacity = 1.0, dy = 0;
        if (Motion::enabled()) {
            const qint64 start = m_markStart + qint64(i * stagger);
            const double lin = double(Motion::now() - start) / T::dropMs;
            if (lin <= 0)
                continue;
            k = Motion::progress(start, T::dropMs, QEasingCurve::OutBack, T::dropOvershoot);
            opacity = std::min(1.0, lin * 3.0);
            dy = -T::dropFromPx * (1.0 - k);
        }
        p.setOpacity(opacity * baseOpacity);
        p.setBrush(T::c(b.color));
        p.drawRect(QRectF(originX + b.cell.x() * T::markCell, T::markTop + b.cell.y() * T::markCell + dy + bob, T::markCell, T::markCell));
    }
    p.setOpacity(1.0);
}

void SetupCanvas::paintHero(QPainter& p)
{
    p.save();
    const double visible = 1.0 - m_shift;
    p.translate(-m_shift * T::heroSlidePx, 0);
    if (m_markVisible && visible > 0.0) {
        paintMark(p, visible);
        paintWordmark(p, visible);
    }
    // tagline with a cross-fade from the previous text
    p.setFont(uiFont(T::pxTagline));
    const QRectF box(0, T::taglineTop, T::winW, 30);
    const double fade = Motion::progress(m_tagChangeMs, T::fadeMs, QEasingCurve::OutCubic);
    if (fade < 1.0 && !m_prevTagline.isEmpty()) {
        p.setOpacity(visible * (1.0 - fade));
        p.setPen(T::c(T::textSecondary));
        p.drawText(box, Qt::AlignHCenter | Qt::AlignTop, m_prevTagline);
    }
    p.setOpacity(visible * fade);
    p.setPen(T::c(T::textSecondary));
    p.drawText(box, Qt::AlignHCenter | Qt::AlignTop, m_tagline);
    p.restore();
}

void SetupCanvas::paintEvent(QPaintEvent* e)
{
    if (m_bg.isNull() || !qFuzzyCompare(m_bgDpr, devicePixelRatioF()))
        rebuildBackground();
    QPainter p(this);
    p.setClipRegion(e->region());
    p.drawPixmap(0, 0, m_bg);

    // stars
    p.setPen(Qt::NoPen);
    const qint64 now = Motion::now();
    for (const Star& s : std::as_const(m_stars)) {
        double a = T::starStaticAlpha;
        if (Motion::enabled()) {
            const double ph = std::fmod(double(now) / T::twinkleMs + s.phase, 1.0);
            a = T::starMinAlpha + (T::starMaxAlpha - T::starMinAlpha) * (0.5 - 0.5 * std::cos(2.0 * M_PI * ph));
        }
        p.setBrush(T::withAlpha(T::text, a));
        p.drawRect(QRectF(s.pos, QSizeF(T::starSize, T::starSize)));
    }

    // title strip caption
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setFont(uiFont(T::pxTitleStrip, QFont::DemiBold));
    p.setPen(T::c(T::textTertiary));
    p.drawText(QRectF(16, 0, 300, T::titleH), Qt::AlignVCenter | Qt::AlignLeft, m_caption);

    paintHero(p);
}

}  // namespace ZeroSetup
