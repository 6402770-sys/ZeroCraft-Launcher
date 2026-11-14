// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - empty and error states of the Discover pages.
#include "ContentStates.h"

#include <QPushButton>

namespace Zero {
namespace Content {

NoMatchState::NoMatchState(QWidget* parent)
    : ZeroEmptyState(QStringLiteral("funnel-x"), tr("Nothing matches these filters"), bodyFor({}), tr("Clear all"), parent),
      m_showEverything(addSecondaryAction(tr("Show everything"))),
      m_bodyText(bodyFor({}))
{
    connect(this, &ZeroEmptyState::actionTriggered, this, &NoMatchState::clearAllRequested);
    connect(m_showEverything, &QPushButton::clicked, this, &NoMatchState::showEverythingRequested);
    m_showEverything->hide();
}

QString NoMatchState::bodyFor(const QString& restrictiveChip)
{
    if (restrictiveChip.isEmpty())
        return tr("Try removing a filter, or clear them all to see more.");
    return tr("The filter “%1” is the most restrictive. Remove it, or clear all filters to see more.").arg(restrictiveChip);
}

void NoMatchState::setContext(const QString& restrictiveChip, bool defaultsExist)
{
    m_bodyText = bodyFor(restrictiveChip);
    setBody(m_bodyText);
    m_showEverything->setVisible(defaultsExist);
}

NetworkErrorState::NetworkErrorState(QWidget* parent)
    : ZeroEmptyState(QStringLiteral("triangle-alert"), titleFor(tr("the provider")), {}, tr("Retry"), parent), m_titleText(titleFor(tr("the provider")))
{
    connect(this, &ZeroEmptyState::actionTriggered, this, &NetworkErrorState::retryRequested);
}

QString NetworkErrorState::titleFor(const QString& providerName)
{
    return tr("Couldn't reach %1").arg(providerName);
}

void NetworkErrorState::setError(const QString& providerName, const QString& errorText)
{
    m_titleText = titleFor(providerName);
    m_bodyText = errorText;
    setTitle(m_titleText);
    setBody(errorText);
}

}  // namespace Content
}  // namespace Zero
