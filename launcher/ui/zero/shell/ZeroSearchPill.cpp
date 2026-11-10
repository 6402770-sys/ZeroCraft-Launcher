// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroSearchPill.h"

#include <QFocusEvent>
#include <QFontMetrics>
#include <QPainter>

#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/palette/PaletteDraw.h"

namespace Zero {

ZeroSearchPill::ZeroSearchPill(QWidget* parent) : QAbstractButton(parent), m_hover(new Motion::StateAnimator(this))
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover, true);
    setFixedHeight(kHeight);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setMaximumWidth(kWidth);
    setAccessibleName(tr("Search or jump to"));
    setToolTip(tr("Command palette  Ctrl+K"));
    connect(m_hover, &Motion::StateAnimator::progressChanged, this, qOverload<>(&QWidget::update));
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void ZeroSearchPill::enterEvent(QEnterEvent* event)
{
    QAbstractButton::enterEvent(event);
    m_hover->setActive(true);
}

void ZeroSearchPill::leaveEvent(QEvent* event)
{
    QAbstractButton::leaveEvent(event);
    m_hover->setActive(false);
}

void ZeroSearchPill::focusInEvent(QFocusEvent* event)
{
    m_keyboardFocus = event->reason() == Qt::TabFocusReason || event->reason() == Qt::BacktabFocusReason;
    QAbstractButton::focusInEvent(event);
    update();
}

void ZeroSearchPill::focusOutEvent(QFocusEvent* event)
{
    m_keyboardFocus = false;
    QAbstractButton::focusOutEvent(event);
    update();
}

void ZeroSearchPill::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal h = m_hover->progress();

    const QRectF box(rect());
    p.setPen(Qt::NoPen);
    p.setBrush(mix(t.border, t.borderStrong, h));
    p.drawPath(Paint::panelPath(box, t.radiusMd));
    p.setBrush(mix(t.surface, t.surfaceHover, h));
    p.drawPath(Paint::panelPath(box.adjusted(1, 1, -1, -1), qMax(0, t.radiusMd - 1)));
    if (m_keyboardFocus && hasFocus()) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(t.accent2, 2));
        p.drawPath(Paint::panelPath(box.adjusted(1, 1, -1, -1), t.radiusMd));
    }

    Icons::icon(QStringLiteral("search"), t.textTertiary).paint(&p, QRect(12, (height() - 16) / 2, 16, 16));

    const QString keys = tr("Ctrl K");
    const int capW = PaletteDraw::keycapWidth(keys);
    PaletteDraw::paintKeycap(p, width() - 8 - capW, height() / 2, keys);

    const QFont font = Style::font(Role::Body);
    p.setFont(font);
    p.setPen(t.textTertiary);
    const int textX = 12 + 16 + 8;
    const int room = width() - 8 - capW - 8 - textX;
    p.drawText(QRect(textX, 0, room, height()), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(font).elidedText(tr("Search or jump to…"), Qt::ElideRight, room));
}

}  // namespace Zero
