// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroSettingsPage.h"

#include <QShowEvent>
#include <QVBoxLayout>
#include "Application.h"
#include "ui/pages/BasePage.h"
#include "ui/pages/BasePageProvider.h"
#include "ui/widgets/PageContainer.h"

namespace Zero {

ZeroSettingsPage::ZeroSettingsPage(BasePageProvider* provider, QWidget* parent) : ShellPage(parent), m_provider(provider)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
}

ZeroSettingsPage::~ZeroSettingsPage() = default;  // MainWindow::closeEvent applies an open Settings page

QString ZeroSettingsPage::title() const
{
    return tr("Settings");
}

QString ZeroSettingsPage::subtitle() const
{
    if (m_container && m_container->selectedPage())
        return m_container->selectedPage()->displayName();
    return {};
}

void ZeroSettingsPage::selectSection(const QString& pageId)
{
    if (!pageId.isEmpty())
        m_section = pageId;
    if (m_container)
        m_container->selectPage(m_section);
}

bool ZeroSettingsPage::applyAll()
{
    if (!m_container)
        return true;
    const bool ok = m_container->saveAll();
    if (ok && APPLICATION)
        emit APPLICATION->globalSettingsApplied();
    return ok;
}

void ZeroSettingsPage::showEvent(QShowEvent* event)
{
    ShellPage::showEvent(event);
    if (!event->spontaneous())
        open();
}

void ZeroSettingsPage::hideEvent(QHideEvent* event)
{
    ShellPage::hideEvent(event);
    if (!event->spontaneous())
        close();
}

void ZeroSettingsPage::open()
{
    if (m_container || !m_provider)
        return;
    if (APPLICATION)
        emit APPLICATION->globalSettingsAboutToOpen();
    m_container = new PageContainer(m_provider, m_section, this);
    m_container->setOptionHelpEnabled(true);
    m_container->setSidebarFooter(tr("Changes are saved when you leave Settings."));
    m_layout->addWidget(m_container);
    connect(m_container, &PageContainer::selectedPageChanged, this, [this](BasePage*, BasePage* selected) {
        if (selected)
            m_section = selected->id();
        emit chromeChanged();
    });
    emit chromeChanged();
}

void ZeroSettingsPage::close()
{
    if (!m_container)
        return;
    const bool ok = m_container->prepareToClose();  // applies every section, then closes the current one
    if (ok && APPLICATION)
        emit APPLICATION->globalSettingsApplied();
    m_container->hide();
    m_container->deleteLater();
    m_container = nullptr;
}

}  // namespace Zero
