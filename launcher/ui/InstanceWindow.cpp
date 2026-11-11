// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
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

#include "InstanceWindow.h"
#include "Application.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>

#include "ui/widgets/PageContainer.h"
#include "ui/zero/PillTag.h"
#include "ui/zero/ZeroCard.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/pages/ElidedLabel.h"
#include "ui/zero/pages/IconTile.h"
#include "ui/zero/pages/InstanceInfo.h"

#include "InstancePageProvider.h"

#include "icons/IconList.h"

InstanceWindow::InstanceWindow(MinecraftInstance* instance, QWidget* parent) : QMainWindow(parent), m_instance(instance)
{
    setAttribute(Qt::WA_DeleteOnClose);

    auto icon = APPLICATION->icons()->getIcon(m_instance->iconKey());
    QString windowTitle = m_instance->name();  // ZERO: the window is the instance, not only its console

    // Set window properties
    {
        setWindowIcon(icon);
        setWindowTitle(windowTitle);
    }

    // Add page container
    {
        auto provider = std::make_shared<InstancePageProvider>(m_instance);
        m_container = new PageContainer(provider.get(), "console", this);
        m_container->setParentContainer(this);
        // ZERO: the instance on top of the side list, and the help strip that explains the option under the pointer
        m_container->setSidebarHeader(buildInstanceHeader());
        m_container->setOptionHelpEnabled(true);
        setCentralWidget(m_container);
        setContentsMargins(0, 0, 0, 0);
    }

    // Add custom buttons to the page container layout.
    {
        auto horizontalLayout = new QHBoxLayout(this);
        horizontalLayout->setObjectName(QStringLiteral("horizontalLayout"));
        horizontalLayout->setContentsMargins(0, 0, 6, 6);

        auto btnHelp = new QPushButton(this);
        btnHelp->setText(tr("Help"));
        btnHelp->setToolTip(tr("Open the online help about this section"));
        Zero::Style::setProperty(btnHelp, "zeroVariant", "ghost");
        horizontalLayout->addWidget(btnHelp);
        connect(btnHelp, &QPushButton::clicked, m_container, &PageContainer::help);

        auto spacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);
        horizontalLayout->addSpacerItem(spacer);

        m_launchButton = new QToolButton(this);
        m_launchButton->setText(tr("&Play"));
        m_launchButton->setToolTip(tr("Start the game (the arrow has more ways to launch: offline, with a profiler...)"));
        Zero::Style::setProperty(m_launchButton, "zeroVariant", "primary");
        m_launchButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_launchButton->setIconSize(QSize(16, 16));
        m_launchButton->setPopupMode(QToolButton::MenuButtonPopup);
        m_launchButton->setMinimumWidth(80);  // HACK!!
        horizontalLayout->addWidget(m_launchButton);
        connect(m_launchButton, &QPushButton::clicked, this, [this] { APPLICATION->launch(m_instance); });

        m_restartButton = new QPushButton(this);
        m_restartButton->setText(tr("&Restart"));
        m_restartButton->setToolTip(tr("Close the game and start it again"));
        horizontalLayout->addWidget(m_restartButton);
        connect(m_restartButton, &QPushButton::clicked, this, &InstanceWindow::restartInstance);

        m_restartButton->hide();

        m_killButton = new QPushButton(this);
        m_killButton->setText(tr("&Stop"));
        m_killButton->setToolTip(tr("Force the game to close right away. Progress since the last autosave is lost."));
        Zero::Style::setProperty(m_killButton, "zeroVariant", "danger");
        m_killButton->setShortcut(QKeySequence(tr("Ctrl+K")));
        horizontalLayout->addWidget(m_killButton);
        connect(m_killButton, &QPushButton::clicked, this, [this] { APPLICATION->kill(m_instance); });

        updateButtons();

        m_closeButton = new QPushButton(this);
        m_closeButton->setText(tr("Close"));
        m_closeButton->setToolTip(tr("Close this window. The game keeps running if it was started."));
        horizontalLayout->addWidget(m_closeButton);
        connect(m_closeButton, &QPushButton::clicked, this, &QMainWindow::close);

        m_container->addButtons(horizontalLayout);

        connect(m_instance, &BaseInstance::profilerChanged, this, &InstanceWindow::updateButtons);
        connect(APPLICATION, &Application::globalSettingsApplied, this, &InstanceWindow::updateButtons);
    }

    // restore window state
    {
        auto base64State = APPLICATION->settings()->get("ConsoleWindowState").toString().toUtf8();
        restoreState(QByteArray::fromBase64(base64State));
        auto base64Geometry = APPLICATION->settings()->get("ConsoleWindowGeometry").toString().toUtf8();
        restoreGeometry(QByteArray::fromBase64(base64Geometry));
    }

    // set up instance and launch process recognition
    {
        auto launchTask = m_instance->getLaunchTask();
        instanceLaunchTaskChanged(launchTask);
        connect(m_instance, &BaseInstance::launchTaskChanged, this, &InstanceWindow::instanceLaunchTaskChanged);
        connect(m_instance, &BaseInstance::runningStatusChanged, this, &InstanceWindow::runningStateChanged);
    }

    // set up instance destruction detection
    {
        connect(m_instance, &BaseInstance::statusChanged, this, &InstanceWindow::on_instanceStatusChanged);
    }

    // add ourself as the modpack page's instance window
    {
        static_cast<ManagedPackPage*>(m_container->getPage("managed_pack"))->setInstanceWindow(this);
    }

    show();
}

void InstanceWindow::on_instanceStatusChanged(BaseInstance::Status, BaseInstance::Status newStatus)
{
    if (newStatus == BaseInstance::Status::Gone) {
        m_doNotSave = true;
        close();
    }
}

QWidget* InstanceWindow::buildInstanceHeader()
{
    // ZERO: picture, name, "1.20.1 · Forge" and a Running / Ready pill above the section list
    using namespace Zero;
    auto* card = new ZeroCard;
    card->setHoverEnabled(false);
    auto* row = new QHBoxLayout(card);
    row->setContentsMargins(Tokens::spaceMd, Tokens::spaceMd, Tokens::spaceMd, Tokens::spaceMd);
    row->setSpacing(Tokens::spaceMd);
    auto* tile = new IconTile(44, 32, card);
    tile->setInstanceIcon(APPLICATION->icons()->getIcon(m_instance->iconKey()));
    row->addWidget(tile, 0, Qt::AlignTop);
    auto* texts = new QVBoxLayout;
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(2);
    auto* name = new QLabel(m_instance->name(), card);  // wraps instead of cutting long names
    name->setWordWrap(true);
    name->setTextFormat(Qt::PlainText);
    Style::applyRole(name, Role::Heading);
    texts->addWidget(name);
    auto* meta = new ElidedLabel(card);
    meta->setFullText(InstanceInfo::of(m_instance).summary());
    Style::applyRole(meta, Role::Caption);
    Style::setProperty(meta, "zeroTone", "secondary");
    texts->addWidget(meta);
    m_statePill = new PillTag(QString(), PillTag::Tone::Plain, card);
    texts->addSpacing(Tokens::spaceXs);
    texts->addWidget(m_statePill, 0, Qt::AlignLeft);
    row->addLayout(texts, 1);
    connect(m_instance, &BaseInstance::runningStatusChanged, this, &InstanceWindow::updateStatePill);
    updateStatePill();
    return card;
}

void InstanceWindow::updateStatePill()
{
    if (!m_statePill)
        return;
    const bool running = m_instance->isRunning();
    m_statePill->setText(running ? tr("Running") : tr("Not running"));
    m_statePill->setTone(running ? Zero::PillTag::Tone::Success : Zero::PillTag::Tone::Plain);
}

void InstanceWindow::updateButtons()
{
    const bool running = m_instance->isRunning();
    if (m_launchButton)
        m_launchButton->setIcon(Zero::Icons::icon(QStringLiteral("play"), Zero::Theme::current().onAccent));

    m_launchButton->setVisible(!running);
    m_restartButton->setVisible(running);

    m_launchButton->setEnabled(m_instance->canLaunch());
    m_restartButton->setEnabled(running && !m_restartQueued);
    m_killButton->setEnabled(running);

    QMenu* launchMenu = m_launchButton->menu();
    if (launchMenu)
        launchMenu->clear();
    else
        launchMenu = new QMenu(this);
    m_instance->populateLaunchMenu(launchMenu);
    m_launchButton->setMenu(launchMenu);
}

void InstanceWindow::instanceLaunchTaskChanged(LaunchTask* proc)
{
    m_proc = proc;
}

void InstanceWindow::runningStateChanged(bool running)
{
    updateButtons();
    m_container->refreshContainer();
    if (running) {
        selectPage("log");
    } else if (m_restartQueued) {
        m_restartQueued = false;
        // Wait until the current launch controller has handled the stopped instance before launching again.
        QTimer::singleShot(0, this, [this] {
            APPLICATION->launch(m_instance);
            updateButtons();
        });
    }
}

void InstanceWindow::restartInstance()
{
    if (!m_instance->isRunning()) {
        return;
    }

    m_restartQueued = APPLICATION->kill(m_instance);
    updateButtons();
}

void InstanceWindow::closeEvent(QCloseEvent* event)
{
    bool proceed = true;
    if (!m_doNotSave) {
        proceed &= m_container->prepareToClose();
    }

    if (!proceed) {
        return;
    }

    APPLICATION->settings()->set("ConsoleWindowState", QString::fromUtf8(saveState().toBase64()));
    APPLICATION->settings()->set("ConsoleWindowGeometry", QString::fromUtf8(saveGeometry().toBase64()));
    emit isClosing();
    event->accept();
}

bool InstanceWindow::saveAll()
{
    return m_container->saveAll();
}

QString InstanceWindow::instanceId()
{
    return m_instance->id();
}

bool InstanceWindow::selectPage(QString pageId)
{
    return m_container->selectPage(pageId);
}

void InstanceWindow::refreshContainer()
{
    m_container->refreshContainer();
}

BasePage* InstanceWindow::selectedPage() const
{
    return m_container->selectedPage();
}

bool InstanceWindow::requestClose()
{
    if (m_container->prepareToClose()) {
        close();
        return true;
    }
    return false;
}
