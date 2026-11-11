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

void PillTag::setTone(Tone tone)
{
    if (tone == m_tone)
        return;
    m_tone = tone;
    update();
}

int PillTag::widthForText(const QString& text)
{
    return QFontMetrics(Style::font(Role::Caption)).horizontalAdvance(text) + 2 * Tokens::spaceSm;
}

QSize PillTag::sizeHint() const
{
    return { widthForText(m_text), Tokens::badgeHeight };
}

void PillTag::paintPill(QPainter* painter, const QRectF& rect, const QString& text, Tone tone)
{
    const auto& t = Theme::current();
    QColor status;
    switch (tone) {
        case Tone::Success: status = t.success; break;
        case Tone::Warning: status = t.warning; break;
        case Tone::Danger: status = t.danger; break;
        case Tone::Info: status = t.info; break;
        default: break;
    }
    QColor fill = t.surface, border = t.border, textColor = tone == Tone::PlainDanger ? t.danger : t.textSecondary;
    if (status.isValid()) {
        fill = mix(t.surface, status, Tokens::statusTint);
        border = mix(t.surface, status, Tokens::statusTint * 3);
        textColor = status;
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(border, 1));
    painter->setBrush(fill);
    painter->drawPath(Paint::panelPath(rect.adjusted(0.5, 0.5, -0.5, -0.5), t.radiusSm));
    painter->setFont(Style::font(Role::Caption));
    painter->setPen(textColor);
    painter->drawText(rect, Qt::AlignCenter, text);
    painter->restore();
}

void PillTag::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    paintPill(&p, QRectF(rect()), m_text, m_tone);
}

}  // namespace Zero
