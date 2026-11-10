// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroPageHeader.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include "ZeroStyle.h"
#include "ZeroTokens.h"

namespace Zero {

ZeroPageHeader::ZeroPageHeader(const QString& title, const QString& subtitle, QWidget* parent)
    : QWidget(parent), m_title(new QLabel(title, this)), m_subtitle(new QLabel(subtitle, this)), m_actions(new QHBoxLayout)
{
    Style::applyRole(m_title, Role::Title);
    Style::applyRole(m_subtitle, Role::Caption);
    m_subtitle->setVisible(!subtitle.isEmpty());

    auto* texts = new QVBoxLayout;
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(2);
    texts->addWidget(m_title);
    texts->addWidget(m_subtitle);

    m_actions->setContentsMargins(0, 0, 0, 0);
    m_actions->setSpacing(Tokens::spaceSm);

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, Tokens::spaceXl);
    row->setSpacing(Tokens::spaceLg);
    row->addLayout(texts, 1);
    row->addLayout(m_actions);
    row->setAlignment(m_actions, Qt::AlignVCenter);
}

void ZeroPageHeader::setTitle(const QString& title)
{
    m_title->setText(title);
}

QString ZeroPageHeader::title() const
{
    return m_title->text();
}

void ZeroPageHeader::setSubtitle(const QString& subtitle)
{
    m_subtitle->setText(subtitle);
    m_subtitle->setVisible(!subtitle.isEmpty());
}

QString ZeroPageHeader::subtitle() const
{
    return m_subtitle->text();
}

void ZeroPageHeader::addAction(QWidget* action)
{
    m_actions->addWidget(action);
}

}  // namespace Zero
