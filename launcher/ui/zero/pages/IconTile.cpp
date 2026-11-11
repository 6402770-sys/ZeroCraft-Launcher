// SPDX-License-Identifier: GPL-3.0-only
#include "IconTile.h"

#include <QMouseEvent>
#include <QPainter>
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {

IconTile::IconTile(int tileSize, int iconSize, QWidget* parent) : QAbstractButton(parent), m_tile(tileSize), m_icon(iconSize)
{
    setFixedSize(tileSize, tileSize);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover, true);
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void IconTile::setInstanceIcon(const QIcon& icon)
{
    m_iconImage = icon;
    update();
}

void IconTile::setClickable(bool clickable)
{
    m_clickable = clickable;
    setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void IconTile::setOutlined(bool outlined)
{
    m_outlined = outlined;
    update();
}

void IconTile::mousePressEvent(QMouseEvent* event)
{
    if (m_clickable)
        QAbstractButton::mousePressEvent(event);
    else
        event->ignore();
}

void IconTile::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(m_outlined ? QPen(t.border, 1) : QPen(Qt::NoPen));
    p.setBrush(m_clickable && underMouse() ? t.higher : t.elevated);
    p.drawPath(Paint::panelPath(m_outlined ? QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5) : QRectF(rect()), t.radiusLg));
    if (!m_iconImage.isNull()) {
        const QRect target((width() - m_icon) / 2, (height() - m_icon) / 2, m_icon, m_icon);
        m_iconImage.paint(&p, target);
    }
}

}  // namespace Zero
