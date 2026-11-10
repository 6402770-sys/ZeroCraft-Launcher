// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroEmptyState.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include "ZeroIcons.h"
#include "ZeroStyle.h"
#include "ZeroThemeState.h"

namespace Zero {

ZeroEmptyState::ZeroEmptyState(const QString& iconName, const QString& title, const QString& body, const QString& actionText,
                               QWidget* parent)
    : QWidget(parent)
    , m_icon(iconName)
    , m_iconLabel(new QLabel(this))
    , m_title(new QLabel(title, this))
    , m_body(new QLabel(body, this))
    , m_action(new QPushButton(actionText, this))
    , m_actions(new QHBoxLayout)
{
    m_iconLabel->setFixedSize(Tokens::emptyStateIconSize, Tokens::emptyStateIconSize);
    m_iconLabel->setAlignment(Qt::AlignCenter);

    Style::applyRole(m_title, Role::Heading);
    m_title->setAlignment(Qt::AlignCenter);
    m_title->setWordWrap(true);

    Style::applyRole(m_body, Role::Body);
    Style::setProperty(m_body, "zeroTone", "secondary");
    m_body->setAlignment(Qt::AlignCenter);
    m_body->setWordWrap(true);
    m_body->setMaximumWidth(Tokens::emptyStateMaxWidth);
    m_title->setMaximumWidth(Tokens::emptyStateMaxWidth);

    Style::setProperty(m_action, "zeroVariant", "primary");
    m_action->setVisible(!actionText.isEmpty());
    connect(m_action, &QPushButton::clicked, this, &ZeroEmptyState::actionTriggered);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl);
    lay->setSpacing(Tokens::spaceSm);
    lay->addStretch(1);
    lay->addWidget(m_iconLabel, 0, Qt::AlignHCenter);
    lay->addSpacing(Tokens::spaceSm);
    // Wrapped labels are centered with side stretches instead of an alignment flag: with AlignHCenter the layout
    // gives them their unwrapped size hint and long text loses lines (height-for-width is skipped).
    for (QLabel* label : { m_title, m_body }) {
        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->addStretch(1);
        row->addWidget(label, 100);
        row->addStretch(1);
        lay->addLayout(row);
    }
    lay->addSpacing(Tokens::spaceMd);
    m_actions->setContentsMargins(0, 0, 0, 0);
    m_actions->setSpacing(Tokens::spaceSm);
    m_actions->addWidget(m_action);
    lay->addLayout(m_actions);
    lay->setAlignment(m_actions, Qt::AlignHCenter);
    lay->addStretch(1);

    refreshIcon();
    connect(&Theme::instance(), &Theme::changed, this, &ZeroEmptyState::refreshIcon);
}

void ZeroEmptyState::refreshIcon()
{
    m_iconLabel->setPixmap(Icons::pixmap(m_icon, Theme::current().textTertiary, Tokens::emptyStateIconSize, devicePixelRatioF()));
}

void ZeroEmptyState::setIcon(const QString& iconName)
{
    m_icon = iconName;
    refreshIcon();
}

void ZeroEmptyState::setTitle(const QString& title)
{
    m_title->setText(title);
}

void ZeroEmptyState::setBody(const QString& body)
{
    m_body->setText(body);
}

void ZeroEmptyState::setActionText(const QString& text)
{
    m_action->setText(text);
    m_action->setVisible(!text.isEmpty());
}

QPushButton* ZeroEmptyState::addSecondaryAction(const QString& text)
{
    auto* button = new QPushButton(text, this);
    Style::setProperty(button, "zeroVariant", "secondary");
    m_actions->addWidget(button);
    return button;
}

}  // namespace Zero
