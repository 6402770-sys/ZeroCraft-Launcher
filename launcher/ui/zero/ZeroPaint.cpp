// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroPaint.h"

#include <QPainter>
#include <algorithm>
#include "ZeroStyle.h"
#include "ZeroThemeState.h"

namespace Zero {
namespace Paint {

namespace {

qreal notchStep(const QRectF& r, qreal radius)
{
    if (radius <= 0)
        return 0;
    return std::min<qreal>({ Tokens::notchStep, r.width() / 2, r.height() / 2 });
}

}  // namespace

QPainterPath panelPath(const QRectF& r, qreal radius, Corners corners)
{
    QPainterPath path;
    if (corners == Corners::Rounded) {
        path.addRoundedRect(r, radius, radius);
        return path;
    }
    const qreal n = notchStep(r, radius);
    const qreal l = r.left(), t = r.top(), rt = r.right(), b = r.bottom();
    path.moveTo(l + n, t);
    path.lineTo(rt - n, t);
    path.lineTo(rt - n, t + n);
    path.lineTo(rt, t + n);
    path.lineTo(rt, b - n);
    path.lineTo(rt - n, b - n);
    path.lineTo(rt - n, b);
    path.lineTo(l + n, b);
    path.lineTo(l + n, b - n);
    path.lineTo(l, b - n);
    path.lineTo(l, t + n);
    path.lineTo(l + n, t + n);
    path.closeSubpath();
    return path;
}

QPainterPath panelPath(const QRectF& r, qreal radius)
{
    return panelPath(r, radius, Theme::current().corners);
}

void paintBevel(QPainter& p, const QRectF& rect, bool pressed)
{
    const auto& t = Theme::current();
    if (!t.bevel || pressed)
        return;
    // Inside the 1 px border; stay clear of the corner steps.
    const QRectF inner = rect.adjusted(1, 1, -1, -1);
    const qreal n = t.corners == Corners::Notched ? notchStep(rect, t.radiusMd) : t.radiusMd;
    const qreal x0 = rect.left() + std::max<qreal>(n, 1), x1 = rect.right() - std::max<qreal>(n, 1);
    if (x1 <= x0 || inner.height() < 4)
        return;
    p.save();
    p.setPen(Qt::NoPen);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setBrush(withAlpha(Qt::white, Tokens::bevelLightAlpha));
    p.drawRect(QRectF(x0, inner.top(), x1 - x0, 1));
    p.setBrush(t.bevelDark);
    p.drawRect(QRectF(x0, inner.bottom() - 2, x1 - x0, 2));
    p.restore();
}

void paintBackground(QPainter& p, const QRect& area, const QColor& base)
{
    const auto& t = Theme::current();
    if (t.texture)
        p.drawTiledPixmap(area, Style::texture(t, base), QPoint(area.x() % 64, area.y() % 64));
    else
        p.fillRect(area, base);
}

}  // namespace Paint
}  // namespace Zero
