// SPDX-License-Identifier: GPL-3.0-only
#include "AccountChip.h"

#include <QFocusEvent>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {

namespace {
constexpr int kAvatar = 28;
constexpr int kHeight = 40;
constexpr int kPadLeft = 6;    // pill edge to avatar
constexpr int kPadRight = 10;  // chevron to pill edge
constexpr int kGap = 8;        // avatar to name, name to chevron
constexpr int kChevron = 12;
constexpr int kMaxText = 180;  // longer names are elided
}  // namespace

AccountChip::AccountChip(QWidget* parent) : QAbstractButton(parent), m_hover(new Motion::StateAnimator(this))
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover, true);
    setFixedHeight(kHeight);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setAccessibleName(tr("Account"));
    connect(m_hover, &Motion::StateAnimator::progressChanged, this, qOverload<>(&QWidget::update));
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        updateGeometry();  // the label font can change family with the theme
        update();
    });
}

int AccountChip::textWidth() const
{
    return qMin(kMaxText, QFontMetrics(Style::font(Role::Label)).horizontalAdvance(m_name));
}

QSize AccountChip::sizeHint() const
{
    return { kPadLeft + kAvatar + kGap + textWidth() + kGap + kChevron + kPadRight, kHeight };
}

QSize AccountChip::minimumSizeHint() const
{
    return sizeHint();
}

void AccountChip::setAccount(const QString& name, const QString& caption, const QPixmap& face)
{
    m_name = name;
    m_caption = caption;
    m_face = face;
    setToolTip(caption.isEmpty() ? name : name + QStringLiteral(" - ") + caption);
    setAccessibleDescription(caption);
    updateGeometry();
    update();
}

void AccountChip::enterEvent(QEnterEvent* event)
{
    QAbstractButton::enterEvent(event);
    m_hover->setActive(true);
}

void AccountChip::leaveEvent(QEvent* event)
{
    QAbstractButton::leaveEvent(event);
    m_hover->setActive(false);
}

void AccountChip::focusInEvent(QFocusEvent* event)
{
    m_keyboardFocus = event->reason() == Qt::TabFocusReason || event->reason() == Qt::BacktabFocusReason;
    QAbstractButton::focusInEvent(event);
    update();
}

void AccountChip::focusOutEvent(QFocusEvent* event)
{
    m_keyboardFocus = false;
    QAbstractButton::focusOutEvent(event);
    update();
}

void AccountChip::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const qreal h = m_hover->progress();
    if (h > 0.0) {
        p.setPen(Qt::NoPen);
        p.setBrush(withAlpha(t.surfaceHover, h));
        p.drawPath(Paint::panelPath(QRectF(rect()), t.radiusMd));
    }
    if (m_keyboardFocus && hasFocus() && !isDown()) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(t.textTertiary, 1));
        p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusMd));
    }

    const QRectF avatar(kPadLeft, (height() - kAvatar) / 2.0, kAvatar, kAvatar);
    QPainterPath circle;
    circle.addEllipse(avatar);
    if (!m_face.isNull()) {
        p.save();
        p.setClipPath(circle);
        p.drawPixmap(avatar.toRect(), m_face);
        p.restore();
    } else {
        p.setPen(Qt::NoPen);
        p.setBrush(t.elevated);
        p.drawPath(circle);
        const int icon = 16;
        Icons::icon(QStringLiteral("user"), t.textSecondary)
            .paint(&p, QRect(int(avatar.center().x()) - icon / 2, int(avatar.center().y()) - icon / 2, icon, icon));
    }

    const int textX = int(avatar.right()) + kGap;
    const int textW = textWidth();
    const QFont nameFont = Style::font(Role::Label);
    const QFontMetrics nm(nameFont);
    p.setFont(nameFont);
    p.setPen(t.text);
    p.drawText(QRect(textX, 0, textW, height()), Qt::AlignLeft | Qt::AlignVCenter, nm.elidedText(m_name, Qt::ElideRight, textW));

    const QColor chevron = mix(t.textTertiary, t.textSecondary, h);
    Icons::icon(QStringLiteral("chevron-down"), chevron)
        .paint(&p, QRect(textX + textW + kGap, (height() - kChevron) / 2, kChevron, kChevron));
}

}  // namespace Zero
