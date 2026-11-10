// SPDX-License-Identifier: GPL-3.0-only
#include "ShellPage.h"

namespace Zero {

ShellPage::ShellPage(QWidget* parent) : QWidget(parent) {}

ShellPage::~ShellPage()
{
    // The action widget may be parented to the top bar at this point: it dies with its page.
    delete m_actions.data();
}

void ShellPage::setTopBarActions(QWidget* actions)
{
    m_actions = actions;
    if (actions) {
        actions->setParent(this);
        actions->hide();
    }
    emit chromeChanged();
}

}  // namespace Zero
