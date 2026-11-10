// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroSpinner.h"

#include <QPainter>
#include "ZeroMotion.h"
#include "ZeroThemeState.h"

namespace Zero {

ZeroSpinner::ZeroSpinner(Size size, QWidget* parent) : QWidget(parent), m_anim(this), m_size(size)
{
    setFixedSize(size, size);
    m_anim.setStartValue(0.0);
    m_anim.setEndValue(360.0);
    m_anim.setDuration(Tokens::durSpinner);
    m_anim.setEasingCurve(QEasingCurve::Linear);
    m_anim.setLoopCount(-1);
    connect(&m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_angle = v.toReal();
        update();
    });
    connect(&Theme::instance(), &Theme::changed, this, &ZeroSpinner::sync);
}

void ZeroSpinner::setSpinnerSize(Size size)
{
    m_size = size;
    setFixedSize(size, size);
    updateGeometry();
}

QSize ZeroSpinner::sizeHint() const
{
    return { m_size, m_size };
}

void ZeroSpinner::sync()
{
    const bool run = isVisible() && Motion::enabled();
    if (run && m_anim.state() != QAbstractAnimation::Running)
        m_anim.start();
    else if (!run && m_anim.state() == QAbstractAnimation::Running)
        m_anim.stop();
    update();
}

void ZeroSpinner::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    sync();
}

void ZeroSpinner::hideEvent(QHideEvent* e)
{
    QWidget::hideEvent(e);
    m_anim.stop();
}

void ZeroSpinner::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    const qreal stroke = m_size >= Medium ? 2.5 : 2.0;
    const QRectF r = QRectF(rect()).adjusted(stroke / 2, stroke / 2, -stroke / 2, -stroke / 2);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(t.border, stroke);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.drawEllipse(r);

    pen.setColor(t.accent);
    p.setPen(pen);
    // Qt angles are 1/16 degree, counter-clockwise from 3 o'clock; negate for clockwise rotation.
    const int start = int((90.0 - m_angle) * 16);
    p.drawArc(r, start, -90 * 16);
}

}  // namespace Zero
