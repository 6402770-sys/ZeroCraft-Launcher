// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroCard.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include "ZeroPaint.h"
#include "ZeroThemeState.h"

namespace Zero {

ZeroCard::ZeroCard(QWidget* parent) : QFrame(parent), m_hover(new Motion::StateAnimator(this))
{
    setFrameShape(QFrame::NoFrame);
    setAttribute(Qt::WA_Hover, true);
    setContentsMargins(Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg);
    connect(m_hover, &Motion::StateAnimator::progressChanged, this, qOverload<>(&QWidget::update));
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void ZeroCard::setClickable(bool clickable)
{
    m_clickable = clickable;
    setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
    setFocusPolicy(clickable ? Qt::StrongFocus : Qt::NoFocus);
}

void ZeroCard::setHoverEnabled(bool enabled)
{
    m_hoverEnabled = enabled;
    if (!enabled)
        m_hover->setActive(false);
}

void ZeroCard::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    const qreal p = m_hoverEnabled ? m_hover->progress() : 0.0;

    QColor bg = mix(t.surface, t.surfaceHover, p);
    QColor border = mix(t.border, t.borderStrong, p);
    if (m_clickable && hasFocus())
        border = t.accent;
    if (m_pressed)
        bg = t.elevated;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Inset by half a pixel so the 1px border is crisp and fully inside the widget.
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setBrush(bg);
    painter.setPen(QPen(border, 1));
    painter.drawPath(Paint::panelPath(r, t.radiusLg));
    Paint::paintBevel(painter, QRectF(rect()), m_pressed);
}

void ZeroCard::enterEvent(QEnterEvent* event)
{
    QFrame::enterEvent(event);
    if (m_hoverEnabled)
        m_hover->setActive(true);
}

void ZeroCard::leaveEvent(QEvent* event)
{
    QFrame::leaveEvent(event);
    m_hover->setActive(false);
}

void ZeroCard::mousePressEvent(QMouseEvent* event)
{
    if (m_clickable && event->button() == Qt::LeftButton) {
        m_pressed = true;
        update();
        event->accept();
        return;
    }
    QFrame::mousePressEvent(event);
}

void ZeroCard::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_clickable && m_pressed && event->button() == Qt::LeftButton) {
        m_pressed = false;
        update();
        if (rect().contains(event->position().toPoint()))
            emit clicked();
        event->accept();
        return;
    }
    QFrame::mouseReleaseEvent(event);
}

void ZeroCard::keyPressEvent(QKeyEvent* event)
{
    if (m_clickable && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space)) {
        emit clicked();
        event->accept();
        return;
    }
    QFrame::keyPressEvent(event);
}

void ZeroCard::focusInEvent(QFocusEvent* event)
{
    QFrame::focusInEvent(event);
    update();
}

void ZeroCard::focusOutEvent(QFocusEvent* event)
{
    QFrame::focusOutEvent(event);
    update();
}

}  // namespace Zero
