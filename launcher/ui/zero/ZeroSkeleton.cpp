// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroSkeleton.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include "ZeroMotion.h"
#include "ZeroPaint.h"
#include "ZeroStyle.h"
#include "ZeroThemeState.h"

namespace Zero {

namespace {
constexpr int kCaptionGap = 14;  // caption -> first shape
constexpr int kRowHeight = 66;
constexpr int kTileMin = 240;
constexpr int kCardMin = 280;  // Discover grid card: 280-340 px wide, 16:9 artwork on top
constexpr int kCardMax = 340;
constexpr int kGalleryMin = 220;
constexpr int kModCardMin = 300;  // Discover mod card: 300-420 px wide, 132 px high, no artwork
constexpr int kModCardMax = 420;
constexpr int kModCardHeight = 132;
constexpr int kGap = 16;
}  // namespace

ZeroSkeleton::ZeroSkeleton(Shape shape, const QString& caption, QWidget* parent) : QWidget(parent), m_shape(shape), m_caption(caption)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_timer.setInterval(33);
    connect(&m_timer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    setAccessibleName(caption);
}

void ZeroSkeleton::setCompactCards(bool compact)
{
    if (compact != m_compactCards) {
        m_compactCards = compact;
        update();
    }
}

void ZeroSkeleton::setCaption(const QString& caption)
{
    m_caption = caption;
    setAccessibleName(caption);
    update();
}

void ZeroSkeleton::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    m_clock.restart();
    if (Motion::enabled())
        m_timer.start();
}

void ZeroSkeleton::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    m_timer.stop();
}

void ZeroSkeleton::block(QPainter& p, const QRectF& r) const
{
    const auto& t = Theme::current();
    const QColor base = t.border;  // clearly visible on bg and on surface cards, still quiet
    if (!m_timer.isActive()) {
        p.fillPath(Paint::panelPath(r, t.radiusSm), base);
        return;
    }
    // One highlight band sweeps the whole widget left to right; every block shows its slice of it.
    const qreal phase = qreal(m_clock.elapsed() % kSweepMs) / kSweepMs;
    const qreal span = width() * 0.35;
    const qreal x = -span + phase * (width() + 2 * span);
    QLinearGradient g(QPointF(x - span, 0), QPointF(x + span, 0));
    const QColor shine = t.borderStrong.lighter(118);
    g.setColorAt(0.0, base);
    g.setColorAt(0.5, shine);
    g.setColorAt(1.0, base);
    g.setSpread(QGradient::PadSpread);
    p.fillPath(Paint::panelPath(r, t.radiusSm), g);
}

void ZeroSkeleton::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    int top = 0;
    if (!m_caption.isEmpty()) {
        const QFont f = Style::font(Role::Body);
        p.setFont(f);
        p.setPen(t.textSecondary);
        const QFontMetrics fm(f);
        p.drawText(QRect(0, 0, width(), fm.height()), Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(m_caption, Qt::ElideRight, width()));
        top = fm.height() + kCaptionGap;
    }
    const QRect area(0, top, width(), height() - top);
    switch (m_shape) {
        case Shape::Rows:
            paintRows(p, area);
            break;
        case Shape::Tiles:
            paintTiles(p, area);
            break;
        case Shape::Report:
            paintReport(p, area);
            break;
        case Shape::Cards:
            if (m_compactCards) {
                paintModCards(p, area);
            } else {
                paintCards(p, area);
            }
            break;
        case Shape::Gallery:
            paintGallery(p, area);
            break;
        case Shape::Cinematic:
            paintCinematic(p, area);
            break;
    }
}

void ZeroSkeleton::paintRows(QPainter& p, const QRect& area) const
{
    // Like a Worlds / Servers row: icon, two text lines, a short value on the right. Widths vary per row so it reads as text.
    static const qreal title[] = { 0.34, 0.26, 0.41, 0.30, 0.37, 0.23, 0.32, 0.28 };
    static const qreal sub[] = { 0.22, 0.30, 0.18, 0.25, 0.20, 0.27, 0.16, 0.24 };
    const int rows = std::max(1, area.height() / kRowHeight);
    for (int i = 0; i < rows; ++i) {
        const int y = area.top() + i * kRowHeight + 12;
        block(p, QRectF(area.left() + 12, y, 40, 40));
        const qreal textLeft = area.left() + 68;
        const qreal textWidth = area.width() - 68 - 120;
        block(p, QRectF(textLeft, y + 6, textWidth * title[i % 8], 12));
        block(p, QRectF(textLeft, y + 26, textWidth * sub[i % 8], 10));
        block(p, QRectF(area.right() - 12 - 64, y + 15, 64, 10));
    }
}

void ZeroSkeleton::paintTiles(QPainter& p, const QRect& area) const
{
    const int gap = 16;
    const int columns = std::max(1, (area.width() + gap) / (kTileMin + gap));
    const qreal tileW = (area.width() - gap * (columns - 1)) / qreal(columns);
    const qreal thumbH = tileW * 9.0 / 16.0;
    const qreal tileH = thumbH + 44;
    for (int r = 0; area.top() + r * (tileH + gap) < area.bottom(); ++r) {
        for (int c = 0; c < columns; ++c) {
            const qreal x = area.left() + c * (tileW + gap);
            const qreal y = area.top() + r * (tileH + gap);
            block(p, QRectF(x, y, tileW, thumbH));
            block(p, QRectF(x, y + thumbH + 10, tileW * 0.45, 11));
            block(p, QRectF(x, y + thumbH + 27, tileW * 0.25, 9));
        }
    }
}

void ZeroSkeleton::paintReport(QPainter& p, const QRect& area) const
{
    // A summary card, then a card of labelled bars (Storage / Java Doctor / Mod Health while they work).
    const auto& t = Theme::current();
    const QRectF summary(area.left(), area.top(), area.width(), 110);
    p.setPen(QPen(t.border, 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(Paint::panelPath(summary.adjusted(0.5, 0.5, -0.5, -0.5), t.radiusLg));
    block(p, QRectF(summary.left() + 16, summary.top() + 18, 80, 10));
    block(p, QRectF(summary.left() + 16, summary.top() + 38, 180, 24));
    block(p, QRectF(summary.left() + 16, summary.top() + 74, summary.width() * 0.45, 10));

    const qreal listTop = summary.bottom() + 16;
    const QRectF list(area.left(), listTop, area.width(), std::max<qreal>(0, area.bottom() - listTop));
    if (list.height() < 60)
        return;
    p.setPen(QPen(t.border, 1));
    p.drawPath(Paint::panelPath(list.adjusted(0.5, 0.5, -0.5, -0.5), t.radiusLg));
    block(p, QRectF(list.left() + 16, list.top() + 18, 140, 14));
    static const qreal fill[] = { 0.62, 0.40, 0.18, 0.09, 0.05, 0.03 };
    for (int i = 0; i < 6 && list.top() + 56 + i * 52 + 30 < list.bottom(); ++i) {
        const qreal y = list.top() + 56 + i * 52;
        block(p, QRectF(list.left() + 16, y, 120 + 30 * (i % 3), 11));
        block(p, QRectF(list.right() - 16 - 56, y, 56, 11));
        block(p, QRectF(list.left() + 16, y + 22, (list.width() - 32) * fill[i], 4));
    }
}

void ZeroSkeleton::paintCards(QPainter& p, const QRect& area) const
{
    // The grid card: 16:9 artwork, the icon overlapping its bottom-left corner, name, author, two description lines and a
    // footer of pills. Columns fill the width like the real grid (280-340 px per card).
    const auto& t = Theme::current();
    const int columns = std::max(1, (area.width() + kGap) / (kCardMin + kGap));
    const qreal cardW = std::min<qreal>(kCardMax, (area.width() - kGap * (columns - 1)) / qreal(columns));
    const qreal art = cardW * 9.0 / 16.0;
    const qreal cardH = art + 164;
    for (int r = 0; area.top() + r * (cardH + kGap) < area.bottom(); ++r) {
        for (int c = 0; c < columns; ++c) {
            const QRectF card(area.left() + c * (cardW + kGap), area.top() + r * (cardH + kGap), cardW, cardH);
            p.setPen(QPen(t.border, 1));
            p.setBrush(Qt::NoBrush);
            p.drawPath(Paint::panelPath(card.adjusted(0.5, 0.5, -0.5, -0.5), t.radiusLg));
            block(p, QRectF(card.left() + 1, card.top() + 1, card.width() - 2, art - 1));
            const QRectF icon(card.left() + 12, card.top() + art - 24, 48, 48);
            p.fillPath(Paint::panelPath(icon.adjusted(-3, -3, 3, 3), t.radiusMd), t.bg);  // a ring in the page color: the icon overlaps the artwork
            block(p, icon);
            const qreal text = card.top() + art + 32;
            block(p, QRectF(card.left() + 12, text, cardW * 0.55, 14));
            block(p, QRectF(card.left() + 12, text + 22, cardW * 0.28, 10));
            block(p, QRectF(card.left() + 12, text + 42, cardW - 24, 10));
            block(p, QRectF(card.left() + 12, text + 58, (cardW - 24) * 0.7, 10));
            block(p, QRectF(card.left() + 12, card.bottom() - 30, 56, 18));
            block(p, QRectF(card.left() + 74, card.bottom() - 30, 48, 18));
            block(p, QRectF(card.right() - 12 - 60, card.bottom() - 30, 60, 18));
        }
    }
}

void ZeroSkeleton::paintModCards(QPainter& p, const QRect& area) const
{
    // The mod card: padding 16, the 48 px icon at the left, name and author at its right, two description lines under them, a
    // footer of pills. Columns fill the width like the real grid.
    const auto& t = Theme::current();
    const int columns = std::max(1, (area.width() + kGap) / (kModCardMin + kGap));
    const qreal cardW = std::min<qreal>(kModCardMax, (area.width() - kGap * (columns - 1)) / qreal(columns));
    const qreal pad = 16;
    for (int r = 0; area.top() + r * (kModCardHeight + kGap) < area.bottom(); ++r) {
        for (int c = 0; c < columns; ++c) {
            const QRectF card(area.left() + c * (cardW + kGap), area.top() + r * (kModCardHeight + kGap), cardW, kModCardHeight);
            p.setPen(QPen(t.border, 1));
            p.setBrush(Qt::NoBrush);
            p.drawPath(Paint::panelPath(card.adjusted(0.5, 0.5, -0.5, -0.5), t.radiusLg));
            block(p, QRectF(card.left() + pad, card.top() + pad, 48, 48));
            const qreal text = card.left() + pad + 48 + 12;
            const qreal textW = card.right() - pad - text;
            block(p, QRectF(text, card.top() + pad + 2, textW * 0.5, 13));
            block(p, QRectF(text, card.top() + pad + 22, textW * 0.28, 10));
            block(p, QRectF(text, card.top() + pad + 46, textW, 10));
            block(p, QRectF(text, card.top() + pad + 62, textW * 0.7, 10));
            block(p, QRectF(card.left() + pad, card.bottom() - pad - 20, 52, 20));
            block(p, QRectF(card.left() + pad + 58, card.bottom() - pad - 20, 44, 20));
            block(p, QRectF(card.right() - pad - 52, card.bottom() - pad - 20, 52, 20));
        }
    }
}

void ZeroSkeleton::paintGallery(QPainter& p, const QRect& area) const
{
    // Artwork tiles (resource packs, shaders): a tall picture, a name line and a short second line, no card frame.
    const int columns = std::max(1, (area.width() + kGap) / (kGalleryMin + kGap));
    const qreal tileW = (area.width() - kGap * (columns - 1)) / qreal(columns);
    const qreal pic = tileW * 10.0 / 16.0;
    const qreal tileH = pic + 52;
    for (int r = 0; area.top() + r * (tileH + kGap) < area.bottom(); ++r) {
        for (int c = 0; c < columns; ++c) {
            const qreal x = area.left() + c * (tileW + kGap);
            const qreal y = area.top() + r * (tileH + kGap);
            block(p, QRectF(x, y, tileW, pic));
            block(p, QRectF(x, y + pic + 12, tileW * 0.6, 12));
            block(p, QRectF(x, y + pic + 30, tileW * 0.35, 10));
        }
    }
}

void ZeroSkeleton::paintCinematic(QPainter& p, const QRect& area) const
{
    // The featured strip: wide 16:9 hero cards side by side (the strip scrolls), each with its text block bottom-left and a
    // button bottom-right over the artwork.
    const auto& t = Theme::current();
    const qreal heroW = std::clamp<qreal>(area.width() * 0.62, 320, 640);
    const qreal heroH = std::min<qreal>(heroW * 9.0 / 16.0, std::max<qreal>(120, area.height()));
    const qreal w = heroH * 16.0 / 9.0;
    for (int i = 0; area.left() + i * (w + kGap) < area.right(); ++i) {
        const QRectF hero(area.left() + i * (w + kGap), area.top(), w, heroH);
        block(p, hero);
        // The text block sits on a darker plate so the shapes read like the overlay of the real card.
        p.setPen(Qt::NoPen);
        p.setBrush(withAlpha(t.bg, 0.45));
        p.drawPath(Paint::panelPath(QRectF(hero.left(), hero.bottom() - 84, hero.width(), 84), t.radiusLg));
        p.setBrush(withAlpha(t.textTertiary, 0.35));
        p.drawPath(Paint::panelPath(QRectF(hero.left() + 16, hero.bottom() - 68, hero.width() * 0.4, 16), t.radiusSm));
        p.drawPath(Paint::panelPath(QRectF(hero.left() + 16, hero.bottom() - 44, hero.width() * 0.6, 10), t.radiusSm));
        p.drawPath(Paint::panelPath(QRectF(hero.left() + 16, hero.bottom() - 28, hero.width() * 0.25, 10), t.radiusSm));
        p.drawPath(Paint::panelPath(QRectF(hero.right() - 16 - 84, hero.bottom() - 52, 84, 32), t.radiusMd));
    }
}

}  // namespace Zero
