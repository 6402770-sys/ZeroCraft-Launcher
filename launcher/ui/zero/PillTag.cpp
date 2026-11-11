// SPDX-License-Identifier: GPL-3.0-only
#include "PillTag.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include "ZeroPaint.h"
#include "ZeroStyle.h"
#include "ZeroThemeState.h"
#include "ZeroTokens.h"

namespace Zero {

PillTag::PillTag(const QString& text, Tone tone, QWidget* parent) : QWidget(parent), m_text(text), m_tone(tone)
{
    setFixedHeight(Tokens::badgeHeight);
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

PillTag::PillTag(const QString& text, bool danger, QWidget* parent) : PillTag(text, danger ? Tone::PlainDanger : Tone::Plain, parent) {}

void PillTag::setText(const QString& text)
{
    if (text == m_text)
        return;
    m_text = text;
    updateGeometry();
    update();
}

QSize PillTag::sizeHint() const
{
    return { QFontMetrics(Style::font(Role::Caption)).horizontalAdvance(m_text) + 2 * Tokens::spaceSm, Tokens::badgeHeight };
}

void PillTag::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QColor status;
    switch (m_tone) {
        case Tone::Success: status = t.success; break;
        case Tone::Warning: status = t.warning; break;
        case Tone::Danger: status = t.danger; break;
        case Tone::Info: status = t.info; break;
        default: break;
    }

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    QColor fill = t.surface, border = t.border, text = m_tone == Tone::PlainDanger ? t.danger : t.textSecondary;
    if (status.isValid()) {
        fill = mix(t.surface, status, Tokens::statusTint);
        border = mix(t.surface, status, Tokens::statusTint * 3);
        text = status;
    }
    p.setPen(QPen(border, 1));
    p.setBrush(fill);
    p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusSm));
    p.setFont(Style::font(Role::Caption));
    p.setPen(text);
    p.drawText(rect(), Qt::AlignCenter, m_text);
}

}  // namespace Zero
