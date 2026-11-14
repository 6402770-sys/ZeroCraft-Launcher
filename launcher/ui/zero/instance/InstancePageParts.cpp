// SPDX-License-Identifier: GPL-3.0-only
#include "InstancePageParts.h"

#include <QFileInfo>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>

#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/content/ContentDraw.h"

namespace Zero {
namespace ModsUi {

RunningBanner::RunningBanner(const QString& text, QWidget* parent) : QWidget(parent), m_text(text)
{
    setFixedHeight(36);
    setAccessibleName(text);
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void RunningBanner::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(Content::Draw::withAlpha(t.warning, Tokens::statusTint * 3), 1));
    p.setBrush(Content::Draw::withAlpha(t.warning, Tokens::statusTint));
    p.drawPath(Paint::panelPath(r, t.radiusMd));
    Icons::icon(QStringLiteral("triangle-alert"), t.warning).paint(&p, QRect(Tokens::spaceMd, (height() - 16) / 2, 16, 16));
    p.setFont(Style::font(Role::Label));
    p.setPen(t.warning);
    p.drawText(QRect(Tokens::spaceMd + 16 + Tokens::spaceSm, 0, width() - 2 * Tokens::spaceMd - 24, height()), Qt::AlignLeft | Qt::AlignVCenter,
               m_text);
}

void paintSwitch(QPainter* p, const QRect& rect, bool on, bool locked)
{
    const auto& t = Theme::current();
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    if (locked) {
        p->setOpacity(p->opacity() * 0.45);
    }
    const qreal radius = t.corners == Corners::Rounded ? rect.height() / 2.0 : t.radiusMd;
    const QRectF track = QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5);
    p->setPen(QPen(on ? t.accent : t.borderStrong, 1));
    p->setBrush(on ? t.accent : t.higher);
    p->drawPath(Paint::panelPath(track, radius));
    const qreal knob = rect.height() - 8;
    const qreal x = on ? rect.right() + 1 - 4 - knob : rect.left() + 4;
    p->setPen(Qt::NoPen);
    p->setBrush(on ? t.onAccent : t.textSecondary);
    p->drawPath(Paint::panelPath(QRectF(x, rect.top() + 4, knob, knob), t.corners == Corners::Rounded ? knob / 2.0 : t.radiusSm));
    p->restore();
}

QLineEdit* makeSearchField(QWidget* parent, const QString& placeholder)
{
    auto* search = new QLineEdit(parent);
    search->setObjectName(QStringLiteral("zeroContentSearch"));
    search->setFixedHeight(Tokens::controlHeightLg);
    search->setMinimumWidth(112);
    search->setMaximumWidth(300);
    search->setClearButtonEnabled(true);
    search->setPlaceholderText(placeholder);
    search->setAccessibleName(placeholder);
    return search;
}

QPushButton* makeAddButton(const QString& text, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    Style::setProperty(button, "zeroVariant", "primary");
    Style::setProperty(button, "zeroSize", "lg");
    button->setFixedHeight(Tokens::controlHeightLg);
    button->setIconSize(QSize(16, 16));
    button->setAccessibleName(text);
    return button;
}

QToolButton* makeOverflowButton(QWidget* parent, const QString& objectName)
{
    auto* button = new QToolButton(parent);
    button->setObjectName(objectName);
    Style::setProperty(button, "zeroVariant", "ghost");
    button->setFixedSize(Tokens::controlHeightLg, Tokens::controlHeightLg);
    button->setIconSize(QSize(18, 18));
    button->setPopupMode(QToolButton::InstantPopup);
    button->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; width: 0px; }"));  // the icon alone, no arrow
    button->setAccessibleName(QObject::tr("More"));
    button->setToolTip(QObject::tr("More"));
    return button;
}

bool moveToRecycleBin(const QString& path, const std::function<bool(const QString&)>& trash)
{
    return !QFileInfo::exists(path) || trash(path);
}

}  // namespace ModsUi
}  // namespace Zero
