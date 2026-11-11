// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *  Copyright (c) 2022 Jamie Mansfield <jmansfield@cadixdev.org>
 *  Copyright (C) 2023 TheKodeToad <TheKodeToad@proton.me>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "PageContainer.h"
#include "BuildConfig.h"
#include "PageContainer_p.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QApplication>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStackedLayout>
#include <QStyledItemDelegate>
#include <QUrl>
#include <utility>

#include "settings/SettingsObject.h"

#include "ui/widgets/IconLabel.h"

#include "Application.h"
#include "DesktopServices.h"
#include "ui/zero/ZeroCard.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroPageHelp.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroTransition.h"

class PageEntryFilterModel : public QSortFilterProxyModel {
   public:
    explicit PageEntryFilterModel(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {}

   protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override
    {
        const QString pattern = filterRegularExpression().pattern();
        auto* const model = static_cast<PageModel*>(sourceModel());
        auto* const page = model->pages().at(sourceRow);
        if (!page->shouldDisplay()) {
            return false;
        }
        // Regular contents check, then check page-filter.
        return QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);
    }
};

PageContainer::PageContainer(BasePageProvider* pageProvider, QString defaultId, QWidget* parent)
    : QWidget(parent), m_proxyModel(new PageEntryFilterModel(this)), m_model(new PageModel(this))
{
    createUI();
    useSidebarStyle(true);

    int counter = 0;
    auto pages = pageProvider->getPages();
    for (auto* page : pages) {
        auto* widget = dynamic_cast<QWidget*>(page);
        widget->setParent(this);
        page->stackIndex = m_pageStack->addWidget(widget);
        page->listIndex = counter;
        page->setParentContainer(this);
        counter++;
        page->updateExtraInfo = [this](const QString& id, const QString& info) {
            if (m_currentPage && id == m_currentPage->id()) {
                m_extraInfo = info;
                m_header->setText(m_currentPage->displayName() + info);
            }
        };
    }
    m_model->setPages(pages);

    m_proxyModel->setSourceModel(m_model);
    m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);

    m_pageList->setIconSize(QSize(22, 22));
    m_pageList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pageList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_pageList->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    m_pageList->setModel(m_proxyModel);
    connect(m_pageList->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &PageContainer::currentChanged);
    m_pageStack->setStackingMode(QStackedLayout::StackOne);
    m_pageList->setFocus();
    selectPage(std::move(defaultId));
}

bool PageContainer::selectPage(QString pageId)
{
    // now find what we want to have selected...
    auto* page = m_model->findPageEntryById(pageId);
    QModelIndex index;
    if (page) {
        index = m_proxyModel->mapFromSource(m_model->index(page->listIndex));
    }
    if (!index.isValid()) {
        index = m_proxyModel->index(0, 0);
    }
    if (index.isValid()) {
        m_pageList->setCurrentIndex(index);
        return true;
    }
    return false;
}

BasePage* PageContainer::getPage(QString pageId)
{
    return m_model->findPageEntryById(pageId);
}

BasePage* PageContainer::selectedPage() const
{
    return m_currentPage;
}

const QList<BasePage*>& PageContainer::getPages() const
{
    return m_model->pages();
}

void PageContainer::refreshContainer()
{
    m_proxyModel->invalidate();
    if (!m_currentPage->shouldDisplay()) {
        auto index = m_proxyModel->index(0, 0);
        if (index.isValid()) {
            m_pageList->setCurrentIndex(index);
        } else {
            // FIXME: unhandled corner case: what to do when there's no page to select?
        }
    }
}

void PageContainer::createUI()
{
    // ZERO layout: side list (icon, name, summary per page) | page title + plain-language explanation, the page, the help
    // strip that explains the option under the pointer | buttons.
    using namespace Zero;
    m_pageStack = new QStackedLayout;
    m_pageList = new PageView;

    m_sidebar = new QWidget(this);
    auto* sideColumn = new QVBoxLayout(m_sidebar);
    sideColumn->setContentsMargins(0, Tokens::spaceMd, 0, Tokens::spaceMd);
    sideColumn->setSpacing(Tokens::spaceSm);
    auto* headerSlot = new QVBoxLayout;
    headerSlot->setContentsMargins(Tokens::spaceLg, 0, Tokens::spaceLg, 0);
    m_sidebarHeaderSlot = headerSlot;
    sideColumn->addLayout(headerSlot);
    sideColumn->addWidget(m_pageList, 1);
    m_sidebarFooter = new QLabel(m_sidebar);
    Style::applyRole(m_sidebarFooter, Role::Caption);
    Style::setProperty(m_sidebarFooter, "zeroTone", "tertiary");
    m_sidebarFooter->setWordWrap(true);
    m_sidebarFooter->setContentsMargins(Tokens::spaceLg, 0, Tokens::spaceLg, 0);
    m_sidebarFooter->hide();
    sideColumn->addWidget(m_sidebarFooter);

    m_header = new QLabel();
    Style::applyRole(m_header, Role::Title);
    m_about = new QLabel();
    Style::applyRole(m_about, Role::Body);
    Style::setProperty(m_about, "zeroTone", "secondary");
    m_about->setWordWrap(true);
    m_about->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // (online help stays on the containers' own Help buttons: PageDialog, InstanceWindow)

    m_contentHost = new QWidget(this);
    auto* content = new QVBoxLayout(m_contentHost);
    content->setContentsMargins(Tokens::spaceXl, Tokens::spaceLg, Tokens::spaceXl, 0);
    content->setSpacing(Tokens::spaceSm);
    content->addWidget(m_header);
    content->addWidget(m_about);
    content->addSpacing(Tokens::spaceSm);
    m_pageStack->setContentsMargins(0, 0, 0, 0);
    m_pageStack->addWidget(new QWidget(this));
    content->addLayout(m_pageStack, 1);

    auto* strip = new ZeroCard(this);
    strip->setHoverEnabled(false);
    m_helpStrip = strip;
    auto* stripRow = new QHBoxLayout(strip);
    stripRow->setContentsMargins(Tokens::spaceLg, Tokens::spaceMd, Tokens::spaceLg, Tokens::spaceMd);
    stripRow->setSpacing(Tokens::spaceMd);
    m_helpIcon = new QLabel(strip);
    m_helpIcon->setFixedSize(18, 18);
    stripRow->addWidget(m_helpIcon, 0, Qt::AlignTop);
    m_helpText = new QLabel(strip);
    m_helpText->setWordWrap(true);
    m_helpText->setTextFormat(Qt::RichText);
    Style::applyRole(m_helpText, Role::Body);
    stripRow->addWidget(m_helpText, 1);
    strip->setMinimumHeight(52);
    strip->hide();
    auto refreshHelpIcon = [this]() {
        m_helpIcon->setPixmap(Icons::pixmap(QStringLiteral("info"), Theme::current().info, 18, m_helpIcon->devicePixelRatioF()));
    };
    refreshHelpIcon();
    connect(&Theme::instance(), &Theme::changed, this, refreshHelpIcon);

    auto* stripHost = new QWidget(this);
    auto* stripMargins = new QVBoxLayout(stripHost);
    stripMargins->setContentsMargins(Tokens::spaceXl, Tokens::spaceMd, Tokens::spaceXl, Tokens::spaceMd);
    stripMargins->addWidget(strip);

    m_layout = new QGridLayout;
    m_layout->addWidget(m_sidebar, 0, 0, 3, 1);
    m_layout->addWidget(m_contentHost, 0, 1, 1, 1);
    m_layout->addWidget(stripHost, 1, 1, 1, 1);
    m_layout->setColumnStretch(1, 4);
    m_layout->setRowStretch(0, 1);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
    setLayout(m_layout);
}

void PageContainer::setSidebarHeader(QWidget* header)
{
    header->setParent(m_sidebar);
    m_sidebarHeaderSlot->addWidget(header);
}

void PageContainer::setSidebarFooter(const QString& text)
{
    m_sidebarFooter->setText(text);
    m_sidebarFooter->setVisible(!text.isEmpty());
}

void PageContainer::setOptionHelpEnabled(bool enabled)
{
    if (enabled == m_optionHelp)
        return;
    m_optionHelp = enabled;
    m_helpStrip->setVisible(enabled);
    if (enabled)
        qApp->installEventFilter(this);
    else
        qApp->removeEventFilter(this);
    resetOptionHelp();
}

bool PageContainer::eventFilter(QObject* watched, QEvent* event)
{
    // ZERO: the help strip follows the pointer and the keyboard focus inside the current page.
    if (m_optionHelp && (event->type() == QEvent::Enter || event->type() == QEvent::FocusIn) && watched->isWidgetType()) {
        auto* widget = static_cast<QWidget*>(watched);
        auto* page = m_currentPage ? dynamic_cast<QWidget*>(m_currentPage) : nullptr;
        if (page && widget != page && page->isAncestorOf(widget))
            showOptionHelp(widget);
    }
    return QWidget::eventFilter(watched, event);
}

void PageContainer::showOptionHelp(QWidget* widget)
{
    QString title;
    const QString text = Zero::PageHelp::forWidget(widget, &title);
    if (text.isEmpty())
        return;  // keep what is shown: moving across empty space should not blank the strip
    const QString html = title.isEmpty() ? text.toHtmlEscaped()
                                         : QStringLiteral("<b>%1</b> &nbsp;%2").arg(title.toHtmlEscaped(), text.toHtmlEscaped());
    if (m_helpText->text() != html)
        m_helpText->setText(html);
}

void PageContainer::resetOptionHelp()
{
    m_helpText->setText(tr("Point at an option, or move to it with Tab, to see what it does."));
}

void PageContainer::updateHeader()
{
    if (!m_currentPage) {
        m_header->setText(QString());
        m_about->clear();
        m_about->hide();
        return;
    }
    m_header->setText(m_currentPage->displayName() + m_extraInfo);
    const QString about = Zero::PageHelp::forPage(m_currentPage->id()).about;
    m_about->setText(about);
    m_about->setVisible(!about.isEmpty());
}

void PageContainer::retranslate()
{
    updateHeader();

    for (auto* page : m_model->pages()) {
        page->retranslate();
    }
}

void PageContainer::addButtons(QWidget* buttons)
{
    buttons->setContentsMargins(Zero::Tokens::spaceXl, 0, Zero::Tokens::spaceXl, Zero::Tokens::spaceLg);
    m_layout->addWidget(buttons, 2, 1, 1, 2);
}

void PageContainer::addButtons(QLayout* buttons)
{
    buttons->setContentsMargins(Zero::Tokens::spaceXl, 0, Zero::Tokens::spaceXl, Zero::Tokens::spaceLg);
    m_layout->addLayout(buttons, 2, 1, 1, 2);
}

void PageContainer::useSidebarStyle(bool sidebar)
{
    m_pageList->setProperty("_kde_side_panel_view", sidebar);
}

void PageContainer::showPage(int row)
{
    // ZERO: switching sections plays the page-enter cascade over the title, explanation and page
    const bool animate = m_currentPage && m_contentHost->isVisible();
    const Zero::Transition::Snapshot before = animate ? Zero::Transition::capture(m_contentHost) : Zero::Transition::Snapshot();
    if (m_currentPage) {
        m_currentPage->closed();
    }
    if (row != -1) {
        m_currentPage = m_model->pages().at(row);
    } else {
        m_currentPage = nullptr;
    }
    m_extraInfo.clear();
    if (m_currentPage) {
        m_pageStack->setCurrentIndex(m_currentPage->stackIndex);
        updateHeader();
        m_currentPage->opened();
    } else {
        m_pageStack->setCurrentIndex(0);
        updateHeader();
    }
    if (m_optionHelp)
        resetOptionHelp();
    if (!before.isNull()) {
        m_contentHost->layout()->activate();
        Zero::Transition::run(m_contentHost, before, Zero::Transition::Kind::Cascade, Zero::Tokens::durCascade);
    }
}

void PageContainer::help()
{
    if (m_currentPage) {
        QString pageId = m_currentPage->helpPage();
        if (pageId.isEmpty()) {
            return;
        }
        DesktopServices::openUrl(QUrl(BuildConfig.HELP_URL.arg(pageId)));
    }
}

void PageContainer::currentChanged(const QModelIndex& current)
{
    int selectedIndex = current.isValid() ? m_proxyModel->mapToSource(current).row() : -1;

    Q_ASSERT(selectedIndex < m_model->pages().size() && selectedIndex >= 0);

    auto* selected = m_model->pages().at(selectedIndex);
    auto* previous = m_currentPage;

    emit selectedPageChanged(previous, selected);

    showPage(selectedIndex);
}

bool PageContainer::prepareToClose()
{
    if (!saveAll()) {
        return false;
    }
    if (m_currentPage) {
        m_currentPage->closed();
    }
    return true;
}

bool PageContainer::saveAll()
{
    for (auto* page : m_model->pages()) {
        if (!page->apply()) {
            return false;
        }
    }
    return true;
}

void PageContainer::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QWidget::changeEvent(event);
}
