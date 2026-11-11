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

}  // namespace Zero
