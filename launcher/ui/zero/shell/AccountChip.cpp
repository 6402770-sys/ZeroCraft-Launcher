// SPDX-License-Identifier: GPL-3.0-only
#include "AccountChip.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {

namespace {
constexpr int kAvatar = 28;
constexpr int kWideThreshold = 120;  // below this the chip shows only the avatar (collapsed rail)
}  // namespace

AccountChip::AccountChip(QWidget* parent) : QAbstractButton(parent), m_hover(new Motion::StateAnimator(this))
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover, true);
    setFixedHeight(48);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_hover, &Motion::StateAnimator::progressChanged, this, qOverload<>(&QWidget::update));
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

QSize AccountChip::sizeHint() const
{
    return { Tokens::navRailWidth - 2 * Tokens::spaceSm, 48 };
}

void AccountChip::setAccount(const QString& name, const QString& caption, const QPixmap& face)
{
    m_name = name;
    m_caption = caption;
    m_face = face;
    setToolTip(width() < kWideThreshold ? name + QStringLiteral(" - ") + caption : QString());
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
        p.drawRoundedRect(QRectF(rect()), Tokens::radiusMd, Tokens::radiusMd);
    }
    if (hasFocus() && !isDown()) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(t.textTertiary, 1));
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), Tokens::radiusMd, Tokens::radiusMd);
    }

    const bool wide = width() >= kWideThreshold;
    const int avatarX = wide ? 10 : (width() - kAvatar) / 2;
    const QRectF avatar(avatarX, (height() - kAvatar) / 2.0, kAvatar, kAvatar);

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

    if (!wide)
        return;

    const int textX = int(avatar.right()) + 10;
    const int textW = width() - textX - 8;
    const QFont nameFont = Style::font(Role::Label);
    const QFont captionFont = Style::font(Role::Caption);
    const QFontMetrics nm(nameFont);
    const QFontMetrics cm(captionFont);
    const int blockH = nm.height() + cm.height();
    const int top = (height() - blockH) / 2;

    p.setFont(nameFont);
    p.setPen(t.text);
    p.drawText(QRect(textX, top, textW, nm.height()), Qt::AlignLeft | Qt::AlignVCenter, nm.elidedText(m_name, Qt::ElideRight, textW));
    p.setFont(captionFont);
    p.setPen(t.textSecondary);
    p.drawText(QRect(textX, top + nm.height(), textW, cm.height()), Qt::AlignLeft | Qt::AlignVCenter,
               cm.elidedText(m_caption, Qt::ElideRight, textW));
}

}  // namespace Zero
