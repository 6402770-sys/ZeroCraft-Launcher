// SPDX-License-Identifier: GPL-3.0-only
#include "InstallChoiceCard.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>

#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero::Content {

namespace {
constexpr int kDot = 16;       ///< the radio circle
constexpr int kDotGap = Tokens::spaceMd;
constexpr int kTitleGap = 2;   ///< between the title and the caption
constexpr int kDisabledOpacityPercent = 40;
}  // namespace

InstallChoiceCard::InstallChoiceCard(const QString& title, const QString& caption, int captionLines, QWidget* parent)
    : QAbstractButton(parent), m_caption(caption), m_captionLines(caption.isEmpty() ? 0 : captionLines)
{
    setText(title);
    setCheckable(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAccessibleName(title);
    setAccessibleDescription(caption);
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void InstallChoiceCard::setCaption(const QString& caption)
{
    m_caption = caption;
    m_captionLines = caption.isEmpty() ? 0 : std::max(m_captionLines, 1);
    setAccessibleDescription(caption);
    updateGeometry();
    update();
}

int InstallChoiceCard::textLeft() const
{
    return Tokens::spaceMd + kDot + kDotGap;
}

QSize InstallChoiceCard::sizeHint() const
{
    const QFontMetrics title(Style::font(Role::Label));
    const QFontMetrics caption(Style::font(Role::Caption));
    int h = 2 * Tokens::spaceMd + title.height();
    if (m_captionLines > 0) {
        h += kTitleGap + m_captionLines * caption.height();
    }
    const int w = textLeft() + Tokens::spaceMd + std::max(title.horizontalAdvance(text()), caption.horizontalAdvance(m_caption));
    return { w, std::max(h, Tokens::controlHeightLg) };
}

QSize InstallChoiceCard::minimumSizeHint() const
{
    return { textLeft() + Tokens::spaceMd + 60, sizeHint().height() };
}

void InstallChoiceCard::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (!isEnabled()) {
        p.setOpacity(kDisabledOpacityPercent / 100.0);
    }
    const bool on = isChecked();
    const bool hover = underMouse() && isEnabled();

    QColor bg = on ? mix(t.surface, t.accent2, 0.12) : (hover ? t.surfaceHover : t.surface);
    QColor border = on ? t.accent2 : (hover ? t.borderStrong : t.border);
    if (isDown()) {
        bg = t.elevated;
    }
    const bool ring = hasFocus() && focusPolicy() != Qt::NoFocus;
    const QRectF outer = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setBrush(bg);
    p.setPen(QPen(border, 1));
    p.drawPath(Paint::panelPath(outer, t.radiusMd));
    if (ring) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(t.accent2, 2));
        p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(1, 1, -1, -1), t.radiusMd));
    }

    // radio dot, centred on the first line of text
    const QFont titleFont = Style::font(Role::Label);
    const QFontMetrics titleMetrics(titleFont);
    const QFont captionFont = Style::font(Role::Caption);
    const QFontMetrics captionMetrics(captionFont);
    const int textHeight = titleMetrics.height() + (m_captionLines > 0 ? kTitleGap + m_captionLines * captionMetrics.height() : 0);
    const int top = (height() - textHeight) / 2;
    const QPointF dotCentre(Tokens::spaceMd + kDot / 2.0, top + titleMetrics.height() / 2.0);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(on ? t.accent2 : t.borderStrong, 1.5));
    p.drawEllipse(dotCentre, kDot / 2.0 - 1, kDot / 2.0 - 1);
    if (on) {
        p.setPen(Qt::NoPen);
        p.setBrush(t.accent2);
        p.drawEllipse(dotCentre, 4.0, 4.0);
    }

    const int left = textLeft();
    const int textWidth = width() - left - Tokens::spaceMd;
    p.setFont(titleFont);
    p.setPen(t.text);
    p.drawText(QRect(left, top, textWidth, titleMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
               titleMetrics.elidedText(text(), Qt::ElideRight, textWidth));
    if (m_captionLines > 0) {
        p.setFont(captionFont);
        p.setPen(t.textSecondary);
        p.drawText(QRect(left, top + titleMetrics.height() + kTitleGap, textWidth, m_captionLines * captionMetrics.height()),
                   Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_caption);
    }
}

void InstallChoiceCard::focusInEvent(QFocusEvent* event)
{
    QAbstractButton::focusInEvent(event);
    update();
}

void InstallChoiceCard::focusOutEvent(QFocusEvent* event)
{
    QAbstractButton::focusOutEvent(event);
    update();
}

}  // namespace Zero::Content
