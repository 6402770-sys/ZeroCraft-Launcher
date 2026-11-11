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
 *      Authors: Andrew Okin
 *               Peterix
 *               Orochimarufan <orochimarufan.x3@gmail.com>
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

#include "Application.h"
#include "BuildConfig.h"
#include "FileSystem.h"

#include "MainWindow.h"
#include "ui_MainWindow.h"

#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileInfo>
#include <QUrl>
#include <QUrlQuery>
#include <QVariant>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QShortcut>
#include <QStatusBar>
#include <QTextEdit>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>
#include <QWidgetAction>
#include <memory>

#include <BaseInstance.h>
#include <BuildConfig.h>
#include <DesktopServices.h>
#include <InstanceList.h>
#include <MMCZip.h>
#include <icons/IconList.h>
#include <java/JavaInstallList.h>
#include <java/JavaUtils.h>
#include <launch/LaunchTask.h>
#include <minecraft/MinecraftInstance.h>
#include <minecraft/auth/AccountList.h>
#include <net/ApiRequest.h>
#include <net/NetJob.h>
#include <news/NewsChecker.h>
#include <tools/BaseProfiler.h>
#include <updater/ExternalUpdater.h>
#include "InstanceWindow.h"

#include "ui/dialogs/AboutDialog.h"
#include "ui/dialogs/CopyInstanceDialog.h"
#include "ui/dialogs/CreateShortcutDialog.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/ExportInstanceDialog.h"
#include "ui/dialogs/ExportPackDialog.h"
#include "ui/dialogs/IconPickerDialog.h"
#include "ui/dialogs/ImportResourceDialog.h"
#include "ui/dialogs/NewInstanceDialog.h"
#include "ui/dialogs/NewsDialog.h"
#include "ui/dialogs/ProgressDialog.h"
#include "ui/dialogs/skins/SkinManageDialog.h"
#include "ui/instanceview/InstanceDelegate.h"
#include "ui/zero/ZeroToast.h"
#include "zero/ZeroLanguage.h"  // ZERO
#include "zero/backup/AutoBackup.h"  // ZERO
#include "zero/backup/WorldBackup.h"  // ZERO
#include "ui/zero/ZeroTheme.h"
#include "ui/zero/ZeroFormat.h"
#include "ui/zero/palette/ZeroCommandPalette.h"
#include "ui/zero/palette/ZeroShortcutSheet.h"
#include "ui/zero/pages/InstanceInfo.h"
#include "ui/zero/shell/ZeroSearchPill.h"
#include "ui/zero/shell/ZeroTopBar.h"
#include "ui/zero/pages/HomePage.h"
#include "ui/zero/pages/LibraryPage.h"
#include "ui/zero/pages/WorldsPage.h"  // ZERO
#include "ui/zero/pages/ScreenshotsPage.h"  // ZERO
#include "ui/zero/pages/ServersPage.h"  // ZERO
#include "ui/zero/pages/ScreenshotViewer.h"  // ZERO
#include "zero/profiles/InstanceProfile.h"
#include "ui/zero/pages/CrashDetectiveView.h"
#include "ui/zero/pages/AppearancePage.h"
#include "ui/zero/pages/JavaDoctorView.h"
#include "ui/zero/pages/ModHealthView.h"
#include "ui/zero/pages/StorageView.h"
#include "ui/zero/pages/ToolsPage.h"
#include "ui/zero/pages/ZeroInstanceDelegate.h"
#include "ui/zero/shell/ZeroShell.h"
#include "ui/instanceview/InstanceProxyModel.h"
#include "ui/instanceview/InstanceView.h"
#include "ui/themes/ITheme.h"
#include "ui/themes/ThemeManager.h"
#include "ui/widgets/EditableLabel.h"

#include "minecraft/PackProfile.h"
#include "minecraft/VersionFile.h"
#include "minecraft/WorldList.h"
#include "minecraft/mod/ModFolderModel.h"
#include "minecraft/mod/ResourcePackFolderModel.h"
#include "minecraft/mod/ShaderPackFolderModel.h"
#include "minecraft/mod/TexturePackFolderModel.h"
#include "minecraft/mod/tasks/LocalResourceParse.h"

#include "modplatform/ModIndex.h"
#include "modplatform/flame/FlameAPI.h"
#include "modplatform/flame/FlameModIndex.h"
#include "modplatform/modrinth/ModrinthAPI.h"

#include "KonamiCode.h"

#include "InstanceCopyTask.h"
#include "InstanceDirUpdate.h"

#include "Json.h"

#include "MMCTime.h"

namespace {
QString profileInUseFilter(const QString& profile, bool used)
{
    if (used) {
        return QObject::tr("%1 (in use)").arg(profile);
    } else {
        return profile;
    }
}
}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // ZERO: default / minimum size (a saved geometry from a previous run overrides the default in showMainWindow)
    resize(1280, 800);
    setMinimumSize(1100, 700);
    setAcceptDrops(true);

    // ZERO: no own icon, so the window follows the application icon, which ThemeManager switches with the theme.
    QGuiApplication::setWindowIcon(APPLICATION->logo());
    setWindowTitle(APPLICATION->applicationDisplayName());
#ifndef QT_NO_ACCESSIBILITY
    setAccessibleName(BuildConfig.LAUNCHER_DISPLAYNAME);
#endif

    // instance toolbar stuff
    {
        connect(ui->instanceToolBar, &QToolBar::orientationChanged, this, &MainWindow::updateInstanceToolBarOrientation);

        // FIXME: why doesn't this work when done in the UI file?
        ui->instanceToolBar->setMinimumWidth(140);

        // if you try to add a widget to a toolbar in a .ui file
        // qt designer will delete it when you save the file >:(
        // HACK: the toolbar interferes with the sizing for some reason
        auto* infoWidget = new QWidget(this);
        auto* infoLayout = new QVBoxLayout(infoWidget);
        infoLayout->setContentsMargins(2, 0, 2, 0);

        m_changeIconButton = new QToolButton(infoWidget);
        m_changeIconButton->setObjectName(QStringLiteral("changeIconButton"));
        m_changeIconButton->setIconSize({ ui->instanceToolBar->minimumWidth(), 80 });
        m_changeIconButton->setAutoRaise(true);
        updateInstanceToolIcon("default");
        m_changeIconButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(m_changeIconButton, &QToolButton::clicked, this, &MainWindow::on_actionChangeInstIcon_triggered);
        infoLayout->addWidget(m_changeIconButton);

        m_nameLabel = new EditableLabel(infoWidget);
        m_nameLabel->setObjectName(QStringLiteral("nameLabel"));
        m_nameLabel->setText(tr("No Instance Selected"));
        connect(m_nameLabel, &EditableLabel::textChanged, this, &MainWindow::renameSelectedInstance);

        infoLayout->addWidget(m_nameLabel);

        m_instanceInfoWidget = ui->instanceToolBar->insertWidgetBefore(ui->actionLaunchInstance, infoWidget);
        Q_ASSERT(m_instanceInfoWidget != nullptr);

        m_instanceToolBarTopSeparator = ui->instanceToolBar->insertSeparator(ui->actionLaunchInstance);
    }

    // set the menu for the folders help, accounts, and export tool buttons
    {
        auto foldersMenuButton = dynamic_cast<QToolButton*>(ui->mainToolBar->widgetForAction(ui->actionFoldersButton));
        ui->actionFoldersButton->setMenu(ui->foldersMenu);
        foldersMenuButton->setPopupMode(QToolButton::InstantPopup);

        helpMenuButton = dynamic_cast<QToolButton*>(ui->mainToolBar->widgetForAction(ui->actionHelpButton));
        ui->actionHelpButton->setMenu(new QMenu(this));
        ui->actionHelpButton->menu()->addActions(ui->helpMenu->actions());
        ui->actionHelpButton->menu()->removeAction(ui->actionCheckUpdate);
        helpMenuButton->setPopupMode(QToolButton::InstantPopup);

        auto accountMenuButton = dynamic_cast<QToolButton*>(ui->mainToolBar->widgetForAction(ui->actionAccountsButton));
        accountMenuButton->setPopupMode(QToolButton::InstantPopup);

        auto exportInstanceMenu = new QMenu(this);
        exportInstanceMenu->addAction(ui->actionExportInstanceZip);
        exportInstanceMenu->addAction(ui->actionExportInstanceMrPack);
        exportInstanceMenu->addAction(ui->actionExportInstanceFlamePack);
        ui->actionExportInstance->setMenu(exportInstanceMenu);
    }

    // hide, disable and show stuff
    {
        ui->actionReportBug->setVisible(!BuildConfig.BUG_TRACKER_URL.isEmpty());
        ui->actionMATRIX->setVisible(!BuildConfig.MATRIX_URL.isEmpty());
        ui->actionDISCORD->setVisible(!BuildConfig.DISCORD_URL.isEmpty());
        ui->actionREDDIT->setVisible(!BuildConfig.SUBREDDIT_URL.isEmpty());

        ui->actionCheckUpdate->setVisible(APPLICATION->updaterEnabled());

#ifndef Q_OS_MACOS
        ui->actionAddToPATH->setVisible(false);
#endif

        // disabled until we have an instance selected
        ui->instanceToolBar->setEnabled(false);
        setInstanceActionsEnabled(false);

        // add a close button at the end of the main toolbar when running on gamescope / steam deck
        // this is only needed on gamescope because it defaults to an X11/XWayland session and
        // does not implement decorations
        if (qgetenv("XDG_CURRENT_DESKTOP") == "gamescope") {
            ui->mainToolBar->addAction(ui->actionCloseWindow);
        }

        ui->actionViewJavaFolder->setEnabled(BuildConfig.JAVA_DOWNLOADER_ENABLED);
    }

    {  // logs viewing
        connect(ui->actionViewLog, &QAction::triggered, this, [] { APPLICATION->showLogWindow(); });
    }

    // add the toolbar toggles to the view menu
    ui->viewMenu->addAction(ui->instanceToolBar->toggleViewAction());
    ui->viewMenu->addAction(ui->newsToolBar->toggleViewAction());

    updateThemeMenu();
    updateMainToolBar();
    // OSX magic.
    setUnifiedTitleAndToolBarOnMac(true);

    // Global shortcuts
    {
        // you can't set QKeySequence::StandardKey shortcuts in qt designer >:(
        ui->actionAddInstance->setShortcut(QKeySequence::New);
        ui->actionSettings->setShortcut(QKeySequence::Preferences);
        if (ui->actionSettings->shortcut().isEmpty())  // ZERO: Windows has no standard Preferences key
            ui->actionSettings->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
        ui->actionUndoTrashInstance->setShortcut(QKeySequence::Undo);
        ui->actionDeleteInstance->setShortcuts({ QKeySequence(tr("Backspace")), QKeySequence::Delete });
        ui->actionCloseWindow->setShortcut(QKeySequence::Close);
        connect(ui->actionCloseWindow, &QAction::triggered, APPLICATION, &Application::closeCurrentWindow);

        // FIXME: This is kinda weird. and bad. We need some kind of managed shutdown.
        auto q = new QShortcut(QKeySequence::Quit, this);
        connect(q, &QShortcut::activated, APPLICATION, &Application::quit);
    }

    // Konami Code
    {
        secretEventFilter = new KonamiCode(this);
        connect(secretEventFilter, &KonamiCode::triggered, this, &MainWindow::konamiTriggered);
    }

    // Add the news label to the news toolbar.
    {
        m_newsChecker.reset(new NewsChecker(APPLICATION->network(), BuildConfig.NEWS_RSS_URL));
        newsLabel = new QToolButton();
        newsLabel->setIcon(QIcon::fromTheme("news"));
        newsLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        newsLabel->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        newsLabel->setFocusPolicy(Qt::NoFocus);
        ui->newsToolBar->insertWidget(ui->actionMoreNews, newsLabel);

        connect(newsLabel, &QAbstractButton::clicked, this, &MainWindow::newsButtonClicked);
        connect(m_newsChecker.get(), &NewsChecker::newsLoaded, this, &MainWindow::updateNewsLabel);
        updateNewsLabel();
    }

    // Create the instance list widget
    {
        view = new InstanceView(this);

        view->setSelectionMode(QAbstractItemView::SingleSelection);
        // FIXME: leaks ListViewDelegate
        auto delegate = new Zero::ZeroInstanceDelegate(this);
        view->setItemDelegate(delegate);
        delegate->attachTo(view);  // hover tracking for the card lift
        connect(delegate, &Zero::ZeroInstanceDelegate::favoriteToggleRequested, this, &MainWindow::toggleFavorite);
        view->setFrameShape(QFrame::NoFrame);
        // do not show ugly blue border on the mac
        view->setAttribute(Qt::WA_MacShowFocusRect, false);
        connect(delegate, &ListViewDelegate::textChanged, this, &MainWindow::handleInstanceRename);

        view->installEventFilter(this);
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(view, &QWidget::customContextMenuRequested, this, &MainWindow::showInstanceContextMenu);
        connect(view, &InstanceView::droppedURLs, this, &MainWindow::processURLs, Qt::QueuedConnection);

        proxymodel = new InstanceProxyModel(this);
        proxymodel->setSourceModel(APPLICATION->instances());
        proxymodel->sort(0);
        connect(proxymodel, &InstanceProxyModel::dataChanged, this, &MainWindow::instanceDataChanged);

        view->setModel(proxymodel);
        view->setSourceOfGroupCollapseStatus(
            [](const QString& groupName) -> bool { return APPLICATION->instances()->isGroupCollapsed(groupName); });
        connect(view, &InstanceView::groupStateChanged, APPLICATION->instances(), &InstanceList::on_GroupStateChanged);
    }
    setupZeroShell();
    // The cat background
    {
        // set the cat action priority here so you can still see the action in qt designer
        ui->actionCAT->setPriority(QAction::LowPriority);
        updateCatState();
        connect(ui->actionCAT, &QAction::toggled, this, &MainWindow::onCatToggled);
        connect(APPLICATION, &Application::currentCatChanged, this, &MainWindow::onCatChanged);
    }

    // Togglable status bar
    {
        bool statusBarVisible = APPLICATION->settings()->get("StatusBarVisible").toBool();
        ui->actionToggleStatusBar->setChecked(statusBarVisible);
        connect(ui->actionToggleStatusBar, &QAction::toggled, this, &MainWindow::setStatusBarVisibility);
        setStatusBarVisibility(statusBarVisible);
    }

    // Lock toolbars
    {
        bool toolbarsLocked = APPLICATION->settings()->get("ToolbarsLocked").toBool();
        ui->actionLockToolbars->setChecked(toolbarsLocked);
        connect(ui->actionLockToolbars, &QAction::toggled, this, &MainWindow::lockToolbars);
        lockToolbars(toolbarsLocked);
    }
    // start instance when double-clicked
    connect(view, &InstanceView::activated, this, &MainWindow::instanceActivated);

    // track the selection -- update the instance toolbar
    connect(view->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::instanceChanged);

    // track icon changes and update the toolbar!
    connect(APPLICATION->icons(), &IconList::iconUpdated, this, &MainWindow::iconUpdated);

    // handle newly added instances
    connect(APPLICATION->instances(), &InstanceList::instanceSelectRequest, this, &MainWindow::instanceSelectRequest);

    // When the global settings page closes, we want to know about it and update our state
    connect(APPLICATION, &Application::globalSettingsApplied, this, &MainWindow::globalSettingsClosed);

    m_statusLeft = new QLabel(tr("No instance selected"), this);
    m_statusCenter = new QLabel(tr("Total playtime: 0s"), this);
    statusBar()->addPermanentWidget(m_statusLeft, 1);
    statusBar()->addPermanentWidget(m_statusCenter, 0);

    // Add "manage accounts" button, right align
    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->mainToolBar->insertWidget(ui->actionAccountsButton, spacer);

    // Use undocumented property... https://stackoverflow.com/questions/7121718/create-a-scrollbar-in-a-submenu-qt
    ui->accountsMenu->setStyleSheet("QMenu { menu-scrollable: 1; }");

    repopulateAccountsMenu();

    // Update the menu when the active account changes.
    // Shouldn't have to use lambdas here like this, but if I don't, the compiler throws a fit.
    // Template hell sucks...
    connect(APPLICATION->accounts(), &AccountList::defaultAccountChanged, this, [this] { defaultAccountChanged(); });
    connect(APPLICATION->accounts(), &AccountList::listActivityChanged, this, [this] { defaultAccountChanged(); });
    connect(APPLICATION->accounts(), &AccountList::listChanged, this, [this] { defaultAccountChanged(); });

    // Show initial account
    defaultAccountChanged();

    // TODO: refresh accounts here?
    // auto accounts = APPLICATION->accounts();

    // load the news
    {
        m_newsChecker->reloadNews();
        updateNewsLabel();
    }

    if (APPLICATION->updaterEnabled()) {
        bool updatesAllowed = APPLICATION->updatesAreAllowed();
        updatesAllowedChanged(updatesAllowed);

        connect(ui->actionCheckUpdate, &QAction::triggered, this, &MainWindow::checkForUpdates);

        // set up the updater object.
        auto updater = APPLICATION->updater();

        if (updater) {
            connect(updater, &ExternalUpdater::canCheckForUpdatesChanged, this, &MainWindow::updatesAllowedChanged);
        }
    }

    connect(ui->actionUndoTrashInstance, &QAction::triggered, this, &MainWindow::undoTrashInstance);

    setSelectedInstanceById(APPLICATION->settings()->get("SelectedInstance").toString());

    // removing this looks stupid
    view->setFocus();

    retranslateUi();
}

// macOS always has a native menu bar, so these fixes are not applicable
// Other systems may or may not have a native menu bar (most do not - it seems like only Ubuntu Unity does)
#ifndef Q_OS_MACOS
void MainWindow::keyReleaseEvent(QKeyEvent* event)
{
    // ZERO: the menu bar is gone, Alt no longer toggles it.
    QMainWindow::keyReleaseEvent(event);
}
#endif

void MainWindow::retranslateUi()
{
    if (m_selectedInstance) {
        m_statusLeft->setText(m_selectedInstance->getStatusbarDescription());
    } else {
        m_statusLeft->setText(tr("No instance selected"));
        m_nameLabel->setText(tr("No Instance Selected"));
    }

    ui->retranslateUi(this);

    MinecraftAccountPtr defaultAccount = APPLICATION->accounts()->defaultAccount();
    if (defaultAccount) {
        auto profileLabel = profileInUseFilter(defaultAccount->displayName(), defaultAccount->isInUse());
        ui->actionAccountsButton->setText(profileLabel);
    }

    m_changeIconButton->setToolTip(ui->actionChangeInstIcon->toolTip());

    // replace the %1 with the launcher display name in some actions
    if (helpMenuButton->toolTip().contains("%1"))
        helpMenuButton->setToolTip(helpMenuButton->toolTip().arg(BuildConfig.LAUNCHER_DISPLAYNAME));

    for (auto action : ui->helpMenu->actions()) {
        if (action->text().contains("%1"))
            action->setText(action->text().arg(BuildConfig.LAUNCHER_DISPLAYNAME));
        if (action->toolTip().contains("%1"))
            action->setToolTip(action->toolTip().arg(BuildConfig.LAUNCHER_DISPLAYNAME));
    }
}

MainWindow::~MainWindow() {}

QMenu* MainWindow::createPopupMenu()
{
    QMenu* filteredMenu = QMainWindow::createPopupMenu();
    filteredMenu->removeAction(ui->mainToolBar->toggleViewAction());

    filteredMenu->addAction(ui->actionToggleStatusBar);
    filteredMenu->addAction(ui->actionLockToolbars);

    return filteredMenu;
}
void MainWindow::setStatusBarVisibility(bool state)
{
    // ZERO: the status bar is not part of the shell; the setting is kept for the legacy action.
    statusBar()->setVisible(false);
    APPLICATION->settings()->set("StatusBarVisible", state);
}
void MainWindow::lockToolbars(bool state)
{
    ui->mainToolBar->setMovable(!state);
    ui->instanceToolBar->setMovable(!state);
    ui->newsToolBar->setMovable(!state);
    APPLICATION->settings()->set("ToolbarsLocked", state);
}

void MainWindow::konamiTriggered()
{
    QString gradient =
        " stop:0 rgba(125, 0, 0, 255), stop:0.166 rgba(125, 125, 0, 255), stop:0.333 rgba(0, 125, 0, 255), stop:0.5 rgba(0, 125, 125, "
        "255), stop:0.666 rgba(0, 0, 125, 255), stop:0.833 rgba(125, 0, 125, 255), stop:1 rgba(125, 0, 0, 255));";
    QString stylesheet = "background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0," + gradient;
    if (ui->mainToolBar->styleSheet() == stylesheet) {
        ui->mainToolBar->setStyleSheet("");
        ui->instanceToolBar->setStyleSheet("");
        ui->centralWidget->setStyleSheet("");
        ui->newsToolBar->setStyleSheet("");
        ui->statusBar->setStyleSheet("");
        qDebug() << "Super Secret Mode DEACTIVATED!";
    } else {
        ui->mainToolBar->setStyleSheet(stylesheet);
        ui->instanceToolBar->setStyleSheet("background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1," + gradient);
        ui->centralWidget->setStyleSheet("background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1," + gradient);
        ui->newsToolBar->setStyleSheet(stylesheet);
        ui->statusBar->setStyleSheet(stylesheet);
        qDebug() << "Super Secret Mode ACTIVATED!";
    }
}

void MainWindow::showInstanceContextMenu(const QPoint& pos)
{
    const bool onInstance = view->indexAt(pos).isValid();
    showInstanceContextMenuAt(view->mapToGlobal(pos), onInstance, onInstance ? QString() : view->groupNameAt(pos));
}

void MainWindow::showInstanceContextMenuAt(const QPoint& globalPos, bool onInstance, const QString& group)
{
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    QList<QAction*> actions;

    auto* actionSep = new QAction("", menu);
    actionSep->setSeparator(true);

    if (onInstance && m_selectedInstance) {
        // reuse the file menu actions
        actions = ui->fileMenu->actions();

        // remove the add instance action, launcher settings action and close action
        actions.removeFirst();
        actions.removeLast();
        actions.removeLast();

        actions.prepend(ui->actionChangeInstIcon);
        actions.prepend(ui->actionRenameInstance);
        {
            // ZERO: favorite toggle at the top of the instance menu
            const bool favorite = Zero::Profile::isFavorite(m_selectedInstance);
            auto* actionFavorite = new QAction(favorite ? tr("Remove from favorites") : tr("Add to favorites"), menu);
            const QString id = m_selectedInstance->id();
            connect(actionFavorite, &QAction::triggered, this, [this, id]() { toggleFavorite(id); });
            actions.prepend(actionFavorite);
        }

        // add header
        actions.prepend(actionSep);
        auto* actionVoid = new QAction(m_selectedInstance->name(), menu);
        actionVoid->setEnabled(false);
        actions.prepend(actionVoid);
    } else {
        auto* actionVoid = new QAction(group.isNull() ? BuildConfig.LAUNCHER_DISPLAYNAME : group, menu);
        actionVoid->setEnabled(false);

        auto* actionCreateInstance = new QAction(tr("&Create instance"), menu);
        actionCreateInstance->setToolTip(ui->actionAddInstance->toolTip());
        if (!group.isNull()) {
            QVariantMap instanceActionData;
            instanceActionData["group"] = group;
            actionCreateInstance->setData(instanceActionData);
        }

        connect(actionCreateInstance, &QAction::triggered, this, &MainWindow::on_actionAddInstance_triggered);

        actions.prepend(actionSep);
        actions.prepend(actionVoid);
        actions.append(actionCreateInstance);
        if (!group.isNull()) {
            auto* actionDeleteGroup = new QAction(tr("&Delete group"), menu);
            connect(actionDeleteGroup, &QAction::triggered, this, [this, group] { deleteGroup(group); });
            actions.append(actionDeleteGroup);

            auto* actionRenameGroup = new QAction(tr("&Rename group"), menu);
            connect(actionRenameGroup, &QAction::triggered, this, [this, group] { renameGroup(group); });
            actions.append(actionRenameGroup);

            QAction* actionChangeGroupIcon = new QAction(tr("Change group &icon"), this);
            connect(actionChangeGroupIcon, &QAction::triggered, this, [this, group] { setGroupIcon(group); });
            actions.append(actionChangeGroupIcon);

            if (!APPLICATION->instances()->getGroupIconKey(group).isEmpty()) {
                QAction* actionRemoveGroupIcon = new QAction(tr("Delete &group icon"), this);
                connect(actionRemoveGroupIcon, &QAction::triggered, this, [this, group] { removeGroupIcon(group); });
                actions.append(actionRemoveGroupIcon);
            }
        }
    }
    menu->addActions(actions);
    menu->popup(globalPos);
}

void MainWindow::updateMainToolBar()
{
    // ZERO: toolbars, menu bar and status bar are replaced by the shell. The QActions they hold stay alive.
    hideLegacyChrome();
}

void MainWindow::updateLaunchButton()
{
    QMenu* launchMenu = ui->actionLaunchInstance->menu();
    if (launchMenu)
        launchMenu->clear();
    else
        launchMenu = new QMenu(this);
    if (m_selectedInstance)
        m_selectedInstance->populateLaunchMenu(launchMenu);
    ui->actionLaunchInstance->setMenu(launchMenu);
}

void MainWindow::updateThemeMenu()
{
    QMenu* themeMenu = ui->actionChangeTheme->menu();

    if (themeMenu) {
        themeMenu->clear();
    } else {
        themeMenu = new QMenu(this);
    }

    auto themes = APPLICATION->themeManager()->getValidApplicationThemes();

    QActionGroup* themesGroup = new QActionGroup(this);

    for (auto* theme : themes) {
        QAction* themeAction = themeMenu->addAction(theme->name());

        themeAction->setCheckable(true);
        if (APPLICATION->settings()->get("ApplicationTheme").toString() == theme->id()) {
            themeAction->setChecked(true);
        }
        themeAction->setActionGroup(themesGroup);

        connect(themeAction, &QAction::triggered, APPLICATION, [id = theme->id()]() {
            APPLICATION->themeManager()->setApplicationTheme(id);
            APPLICATION->settings()->set("ApplicationTheme", id);
        });
    }

    ui->actionChangeTheme->setMenu(themeMenu);
}

void MainWindow::repopulateAccountsMenu()
{
    ui->accountsMenu->clear();

    // NOTE: this is done so the accounts button text is not set to the accounts menu title
    QMenu* accountsButtonMenu = ui->actionAccountsButton->menu();
    if (accountsButtonMenu) {
        accountsButtonMenu->clear();
    } else {
        accountsButtonMenu = new QMenu(this);
        ui->actionAccountsButton->setMenu(accountsButtonMenu);
    }

    auto accounts = APPLICATION->accounts();
    MinecraftAccountPtr defaultAccount = accounts->defaultAccount();

    bool canChangeSkin = defaultAccount && (defaultAccount->accountType() == AccountType::MSA) && !defaultAccount->isActive();
    ui->actionManageSkins->setEnabled(canChangeSkin);

    QString active_profileId = "";
    if (defaultAccount) {
        // this can be called before accountMenuButton exists
        if (ui->actionAccountsButton) {
            auto profileLabel = profileInUseFilter(defaultAccount->displayName(), defaultAccount->isInUse());
            ui->actionAccountsButton->setText(profileLabel);
        }
    }

    QActionGroup* accountsGroup = new QActionGroup(this);

    for (QAction* old : std::as_const(m_accountActions)) {
        removeAction(old);
        old->deleteLater();
    }
    m_accountActions.clear();

    if (accounts->count() <= 0) {
        ui->actionNoAccountsAdded->setEnabled(false);
        ui->accountsMenu->addAction(ui->actionNoAccountsAdded);
    } else {
        // TODO: Nicer way to iterate?
        for (int i = 0; i < accounts->count(); i++) {
            MinecraftAccountPtr account = accounts->at(i);
            auto profileLabel = profileInUseFilter(account->displayName(), account->isInUse());
            QAction* action = new QAction(profileLabel, this);
            action->setData(i);
            action->setCheckable(true);
            action->setActionGroup(accountsGroup);
            if (defaultAccount == account) {
                action->setChecked(true);
            }

            auto face = account->getFace();
            if (!face.isNull()) {
                action->setIcon(face);
            } else {
                action->setIcon(QIcon::fromTheme("noaccount"));
            }

            const int highestNumberKey = 9;
            if (i < highestNumberKey) {
                action->setShortcut(QKeySequence(tr("Ctrl+%1").arg(i + 1)));
            }

            ui->accountsMenu->addAction(action);
            // The menu bar is hidden in the ZERO shell, so register the action (and its shortcut) on the window itself.
            addAction(action);
            m_accountActions.append(action);
            connect(action, &QAction::triggered, this, &MainWindow::changeActiveAccount);
        }
    }

    ui->accountsMenu->addSeparator();

    ui->actionNoDefaultAccount->setData(-1);
    ui->actionNoDefaultAccount->setChecked(!defaultAccount);
    ui->actionNoDefaultAccount->setActionGroup(accountsGroup);

    ui->accountsMenu->addAction(ui->actionNoDefaultAccount);

    connect(ui->actionNoDefaultAccount, &QAction::triggered, this, &MainWindow::changeActiveAccount);

    ui->accountsMenu->addSeparator();
    ui->accountsMenu->addAction(ui->actionManageSkins);
    ui->accountsMenu->addAction(ui->actionManageAccounts);

    accountsButtonMenu->addActions(ui->accountsMenu->actions());
}

void MainWindow::updatesAllowedChanged(bool allowed)
{
    if (!APPLICATION->updaterEnabled()) {
        return;
    }
    ui->actionCheckUpdate->setEnabled(allowed);
}

/*
 * Assumes the sender is a QAction
 */
void MainWindow::changeActiveAccount()
{
    QAction* sAction = (QAction*)sender();

    // Profile's associated Mojang username
    if (sAction->data().typeId() != QMetaType::Int)
        return;

    QVariant action_data = sAction->data();
    bool valid = false;
    int index = action_data.toInt(&valid);
    if (!valid) {
        index = -1;
    }
    auto accounts = APPLICATION->accounts();
    accounts->setDefaultAccount(index == -1 ? nullptr : accounts->at(index));
    defaultAccountChanged();
}

void MainWindow::defaultAccountChanged()
{
    repopulateAccountsMenu();
    updateAccountChip();

    MinecraftAccountPtr account = APPLICATION->accounts()->defaultAccount();

    // FIXME: this needs adjustment for MSA
    if (account && account->profileName() != "") {
        auto profileLabel = profileInUseFilter(account->displayName(), account->isInUse());
        ui->actionAccountsButton->setText(profileLabel);
        auto face = account->getFace();
        if (face.isNull()) {
            ui->actionAccountsButton->setIcon(QIcon::fromTheme("noaccount"));
        } else {
            ui->actionAccountsButton->setIcon(face);
        }
        return;
    }

    // Set the icon to the "no account" icon.
    ui->actionAccountsButton->setIcon(QIcon::fromTheme("noaccount"));
    ui->actionAccountsButton->setText(tr("Accounts"));
}

bool MainWindow::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == view || (m_library && obj == m_library->listView())) {
        if (ev->type() == QEvent::KeyPress) {
            secretEventFilter->input(ev);
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(ev);
            switch (keyEvent->key()) {
                    /*
                case Qt::Key_Enter:
                case Qt::Key_Return:
                    activateInstance(m_selectedInstance);
                    return true;
                    */
                case Qt::Key_Delete:
                    on_actionDeleteInstance_triggered();
                    return true;
                case Qt::Key_F5:
                    refreshInstances();
                    return true;
                case Qt::Key_F2:
                    on_actionRenameInstance_triggered();
                    return true;
                default:
                    break;
            }
        }
    }
    return QMainWindow::eventFilter(obj, ev);
}

void MainWindow::updateNewsLabel()
{
    if (m_newsChecker->isLoadingNews()) {
        newsLabel->setText(tr("Loading news..."));
        newsLabel->setEnabled(false);
        ui->actionMoreNews->setVisible(false);
    } else {
        QList<NewsEntryPtr> entries = m_newsChecker->getNewsEntries();
        if (entries.length() > 0) {
            newsLabel->setText(entries[0]->title);
            newsLabel->setEnabled(true);
            ui->actionMoreNews->setVisible(true);
        } else {
            newsLabel->setText(tr("No news available."));
            newsLabel->setEnabled(false);
            ui->actionMoreNews->setVisible(false);
        }
    }
}

void MainWindow::onCatToggled(bool state)
{
    setCatBackground(state);
    APPLICATION->settings()->set("TheCat", state);
}

void MainWindow::setCatBackground(bool enabled)
{
    // ZERO: the cat background is a Prism feature the card grid does not support.
    Q_UNUSED(enabled);
    view->setPaintCat(false);
}

void MainWindow::updateCatState()
{
    SettingsObject* settings = APPLICATION->settings();
    const bool catEnabled = settings->get("EnableCat").toBool();
    bool catVisible = settings->get("TheCat").toBool();
    if (!catEnabled && catVisible) {
        settings->set("TheCat", false);
        catVisible = false;
    }

    ui->actionCAT->setVisible(catEnabled);
    ui->actionCAT->setChecked(catVisible);
    setCatBackground(catVisible);
}

void MainWindow::runModalTask(Task* task)
{
    connect(task, &Task::failed, this,
            [this](QString reason) { CustomMessageBox::selectable(this, tr("Error"), reason, QMessageBox::Critical)->show(); });
    connect(task, &Task::succeeded, this, [this, task]() {
        QStringList warnings = task->warnings();
        if (warnings.count()) {
            CustomMessageBox::selectable(this, tr("Warnings"), warnings.join('\n'), QMessageBox::Warning)->show();
        }
    });
    ProgressDialog loadDialog(this);
    loadDialog.showSkipButton();
    loadDialog.execWithTask(task);
}

void MainWindow::instanceFromInstanceTask(InstanceTask* rawTask)
{
    unique_qobject_ptr<Task> task(APPLICATION->instances()->wrapInstanceTask(rawTask));
    runModalTask(task.get());
}

void MainWindow::on_actionCopyInstance_triggered()
{
    if (!m_selectedInstance)
        return;

    CopyInstanceDialog copyInstDlg(m_selectedInstance, this);
    if (!copyInstDlg.exec())
        return;

    auto copyTask = new InstanceCopyTask(m_selectedInstance, copyInstDlg.getChosenOptions());
    copyTask->setName(copyInstDlg.instName());
    copyTask->setGroup(copyInstDlg.instGroup());
    copyTask->setIcon(copyInstDlg.iconKey());
    unique_qobject_ptr<Task> task(APPLICATION->instances()->wrapInstanceTask(copyTask));
    runModalTask(task.get());
}

void MainWindow::addInstance(const QString& url, const QMap<QString, QString>& extra_info, const QString& initialPage)
{
    QString groupName;
    do {
        QObject* obj = sender();
        if (!obj)
            break;
        QAction* action = qobject_cast<QAction*>(obj);
        if (!action)
            break;
        auto map = action->data().toMap();
        if (!map.contains("group"))
            break;
        groupName = map["group"].toString();
    } while (0);

    if (groupName.isEmpty()) {
        groupName = APPLICATION->settings()->get("LastUsedGroupForNewInstance").toString();
    }

    NewInstanceDialog newInstDlg(groupName, url, extra_info, this);
    if (!initialPage.isEmpty())
        newInstDlg.selectPage(initialPage);
    if (!newInstDlg.exec())
        return;

    APPLICATION->settings()->set("LastUsedGroupForNewInstance", newInstDlg.instGroup());
    APPLICATION->settings()->set("LastUsedInstDirForNewInstance", newInstDlg.instDir());

    InstanceTask* creationTask = newInstDlg.extractTask();
    if (creationTask) {
        instanceFromInstanceTask(creationTask);
    }
}

void MainWindow::on_actionAddInstance_triggered()
{
    addInstance();
}

void MainWindow::processURLs(QList<QUrl> urls)
{
    // NOTE: This loop only processes one dropped file!
    for (auto& url : urls) {
        if (url.isEmpty() || url.toString().trimmed().isEmpty())
            continue;

        qDebug() << "Processing" << url;

        // The isLocalFile() check below doesn't work as intended without an explicit scheme.
        if (url.scheme().isEmpty())
            url.setScheme("file");

        ModPlatform::IndexedVersion version;
        QMap<QString, QString> extra_info;
        QUrl local_url;
        if (!url.isLocalFile()) {  // download the remote resource and identify
            if (url.scheme().compare("modrinth", Qt::CaseInsensitive) == 0) {
                const auto packId = ModrinthAPI::getModpackIdFromUrl(url);
                if (!packId.isEmpty()) {
                    extra_info.insert("pack_id", packId);
                    addInstance(url.toString(), extra_info);
                } else {
                    CustomMessageBox::selectable(
                        this, tr("Error"),
                        tr("Unsupported Modrinth link.\n\nPrism Launcher currently only supports modpack links such as "
                           "modrinth://modpack/fabulously-optimized."),
                        QMessageBox::Critical)
                        ->show();
                }
                continue;
            }

            const bool isExternalURLImport = (url.host().toLower() == "import") || (url.path().startsWith("/import", Qt::CaseInsensitive));

            QUrl dl_url;
            if (url.scheme() == "curseforge" || (url.scheme() == BuildConfig.LAUNCHER_APP_BINARY_NAME && url.host() == "install")) {
                // need to find the download link for the modpack / resource
                // format of url curseforge://install?addonId=IDHERE&fileId=IDHERE
                // format of url binaryname://install?platform=curseforge&addonId=IDHERE&fileId=IDHERE
                QUrlQuery query(url);

                // check if this is a binaryname:// url
                if (url.scheme() == BuildConfig.LAUNCHER_APP_BINARY_NAME) {
                    // check this is an curseforge platform request
                    if (query.queryItemValue("platform").toLower() != "curseforge") {
                        qDebug() << "Invalid mod distribution platform:" << query.queryItemValue("platform");
                        continue;
                    }
                }

                if (query.allQueryItemValues("addonId").isEmpty() || query.allQueryItemValues("fileId").isEmpty()) {
                    qDebug() << "Invalid curseforge link:" << url;
                    continue;
                }

                auto addonId = query.allQueryItemValues("addonId")[0];
                auto fileId = query.allQueryItemValues("fileId")[0];

                extra_info.insert("pack_id", addonId);
                extra_info.insert("pack_version_id", fileId);

                auto [job, array] = FlameAPI::getFile(addonId, fileId);

                connect(job.get(), &Task::failed, this, [this](const QString& reason) {
                    CustomMessageBox::selectable(this, tr("Error"), reason, QMessageBox::Critical)->show();
                });
                connect(job.get(), &Task::succeeded, this, [this, array, addonId, fileId, &dl_url, &version] {
                    qDebug() << "Returned CFURL Json:\n" << array->toStdString().c_str();
                    auto doc = Json::requireDocument(*array);
                    if (!doc) {
                        CustomMessageBox::selectable(this, tr("Error"), doc.error(), QMessageBox::Critical)->show();
                        return;
                    }
                    auto data = doc->object()["data"].toObject();
                    // No way to find out if it's a mod or a modpack before here
                    // And also we need to check if it ends with .zip, instead of any better way
                    auto versionRes = FlameMod::loadIndexedPackVersion(data);
                    if (!versionRes) {
                        CustomMessageBox::selectable(this, tr("Error"), versionRes.error(), QMessageBox::Critical)->show();
                        return;
                    }
                    version = versionRes.value();
                    auto fileName = version.fileName;

                    // Have to use ensureString then use QUrl to get proper url encoding
                    dl_url = QUrl(version.downloadUrl);
                    if (!dl_url.isValid()) {
                        CustomMessageBox::selectable(
                            this, tr("Error"),
                            tr("The modpack, mod, or resource %1 is blocked for third-parties! Please download it manually.").arg(fileName),
                            QMessageBox::Critical)
                            ->show();
                        return;
                    }
                });

                {  // drop stack
                    ProgressDialog dlUrlDialod(this);
                    dlUrlDialod.showSkipButton();
                    dlUrlDialod.execWithTask(job.get());
                }

            } else if (url.scheme() == BuildConfig.LAUNCHER_APP_BINARY_NAME && !isExternalURLImport) {
                QVariantMap receivedData;
                const QUrlQuery query(url.query());
                const auto items = query.queryItems();
                for (auto it = items.begin(), end = items.end(); it != end; ++it)
                    receivedData.insert(it->first, it->second);
                emit APPLICATION->oauthReplyRecieved(receivedData);
                continue;
            } else if ((url.scheme() == "prismlauncher" || url.scheme() == BuildConfig.LAUNCHER_APP_BINARY_NAME) && isExternalURLImport) {
                // PrismLauncher URL protocol modpack import
                // works for any prism fork
                // preferred import format: prismlauncher://import?url=ENCODED
                const auto host = url.host().toLower();
                const auto path = url.path();

                QString encodedTarget;

                {
                    QUrlQuery query(url);
                    const auto values = query.allQueryItemValues("url");
                    if (!values.isEmpty()) {
                        encodedTarget = values.first();
                    }
                }

                // alternative import format: prismlauncher://import/ENCODED
                if (encodedTarget.isEmpty()) {
                    QString p = path;

                    if (p.startsWith("/import/", Qt::CaseInsensitive)) {
                        p = p.mid(QString("/import/").size());
                    } else if (host == "import" && p.startsWith("/")) {
                        p = p.mid(1);
                    }

                    if (!p.isEmpty() && p != "/import") {
                        encodedTarget = p;
                    }
                }

                if (encodedTarget.isEmpty()) {
                    CustomMessageBox::selectable(this, tr("Error"), tr("Invalid import link: missing 'url' parameter."),
                                                 QMessageBox::Critical)
                        ->show();
                    continue;
                }

                const QString decodedStr = QUrl::fromPercentEncoding(encodedTarget.toUtf8()).trimmed();

                QUrl target = QUrl::fromUserInput(decodedStr);

                // Validate: only allow http(s)
                if (!target.isValid() || (target.scheme() != "https" && target.scheme() != "http")) {
                    CustomMessageBox::selectable(this, tr("Error"), tr("Invalid import link: URL must be http(s)."), QMessageBox::Critical)
                        ->show();
                    continue;
                }

                const auto res = QMessageBox::question(
                    this, tr("Install modpack"),
                    tr("Do you want to download and import a modpack from:\n%1\n\nURL:\n%2").arg(target.host(), target.toString()),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
                if (res != QMessageBox::Yes) {
                    continue;
                }

                dl_url = target;
            } else {
                dl_url = url;
            }

            if (!dl_url.isValid()) {
                continue;  // no valid url to download this resource
            }

            const QString path = dl_url.host() + '/' + dl_url.path();
            auto entry = APPLICATION->metacache()->resolveEntry("general", path);
            entry->setStale(true);
            auto dl_job = unique_qobject_ptr<NetJob>(new NetJob(tr("Modpack download"), APPLICATION->network()));
            dl_job->addNetAction(Net::ApiRequest::makeCached(dl_url, entry));
            auto archivePath = entry->getFullPath();

            bool dl_success = false;
            connect(dl_job.get(), &Task::failed, this,
                    [this](QString reason) { CustomMessageBox::selectable(this, tr("Error"), reason, QMessageBox::Critical)->show(); });
            connect(dl_job.get(), &Task::succeeded, this, [&dl_success] { dl_success = true; });

            {  // drop stack
                ProgressDialog dlUrlDialod(this);
                dlUrlDialod.showSkipButton();
                dlUrlDialod.execWithTask(dl_job.get());
            }

            if (!dl_success) {
                continue;  // no local file to identify
            }
            local_url = QUrl::fromLocalFile(archivePath);

        } else {
            local_url = url;
        }

        auto localFileName = QDir::toNativeSeparators(local_url.toLocalFile());
        QFileInfo localFileInfo(localFileName);

        if (localFileName.isEmpty() || !localFileInfo.exists()) {
            qDebug() << "Ignoring invalid path" << localFileName;
            continue;
        }

        auto type = ResourceUtils::identify(localFileInfo);

        if (ModPlatform::ResourceTypeUtils::g_VALID_RESOURCES.count(type) == 0) {  // probably instance/modpack
            addInstance(localFileName, extra_info);
            continue;
        }

        if (APPLICATION->instances()->count() <= 0) {
            CustomMessageBox::selectable(this, tr("No instance!"),
                                         tr("No instance available to add the resource to.\nPlease create a new instance before "
                                            "attempting to install this resource again."),
                                         QMessageBox::Critical)
                ->show();
            continue;
        }
        ImportResourceDialog dlg(localFileName, type, this);

        if (dlg.exec() != QDialog::Accepted)
            continue;

        qDebug() << "Adding resource" << localFileName << "to" << dlg.selectedInstanceKey;

        auto inst = APPLICATION->instances()->getInstanceById(dlg.selectedInstanceKey);
        auto minecraftInst = inst;

        switch (type) {
            case ModPlatform::ResourceType::ResourcePack:
                minecraftInst->resourcePackList()->installResourceWithFlameMetadata(localFileName, version);
                break;
            case ModPlatform::ResourceType::TexturePack:
                minecraftInst->texturePackList()->installResourceWithFlameMetadata(localFileName, version);
                break;
            case ModPlatform::ResourceType::DataPack:
                qWarning() << "Importing of Data Packs not supported at this time. Ignoring" << localFileName;
                break;
            case ModPlatform::ResourceType::Mod:
                minecraftInst->loaderModList()->installResourceWithFlameMetadata(localFileName, version);
                break;
            case ModPlatform::ResourceType::ShaderPack:
                minecraftInst->shaderPackList()->installResourceWithFlameMetadata(localFileName, version);
                break;
            case ModPlatform::ResourceType::World:
                minecraftInst->worldList()->installWorld(localFileInfo);
                break;
            case ModPlatform::ResourceType::Unknown:
            default:
                qDebug() << "Can't Identify" << localFileName << "Ignoring it.";
                break;
        }
    }
}

void MainWindow::on_actionREDDIT_triggered()
{
    DesktopServices::openUrl(QUrl(BuildConfig.SUBREDDIT_URL));
}

void MainWindow::on_actionDISCORD_triggered()
{
    DesktopServices::openUrl(QUrl(BuildConfig.DISCORD_URL));
}

void MainWindow::on_actionMATRIX_triggered()
{
    DesktopServices::openUrl(QUrl(BuildConfig.MATRIX_URL));
}

void MainWindow::on_actionChangeInstIcon_triggered()
{
    if (!m_selectedInstance)
        return;

    IconPickerDialog dlg(this);
    dlg.execWithSelection(m_selectedInstance->iconKey());
    if (dlg.result() == QDialog::Accepted) {
        m_selectedInstance->setIconKey(dlg.selectedIconKey);
        auto icon = APPLICATION->icons()->getIcon(dlg.selectedIconKey);
        ui->actionChangeInstIcon->setIcon(icon);
        m_changeIconButton->setIcon(icon);
    }
}

void MainWindow::iconUpdated(QString icon)
{
    if (icon == m_currentInstIcon) {
        updateInstanceToolIcon(icon);
    }
}

void MainWindow::updateInstanceToolIcon(QString new_icon)
{
    m_currentInstIcon = new_icon;
    auto icon = APPLICATION->icons()->getIcon(m_currentInstIcon);
    ui->actionChangeInstIcon->setIcon(icon);
    m_changeIconButton->setIcon(icon);
}

void MainWindow::setSelectedInstanceById(const QString& id)
{
    if (id.isNull())
        return;
    const QModelIndex index = APPLICATION->instances()->getInstanceIndexById(id);
    if (index.isValid()) {
        QModelIndex selectionIndex = proxymodel->mapFromSource(index);
        view->selectionModel()->setCurrentIndex(selectionIndex, QItemSelectionModel::ClearAndSelect);
        updateStatusCenter();
    }
}

void MainWindow::on_actionChangeInstGroup_triggered()
{
    if (!m_selectedInstance)
        return;

    InstanceId instId = m_selectedInstance->id();
    QString src(APPLICATION->instances()->getInstanceGroup(instId));

    QStringList groups = APPLICATION->instances()->getGroups();
    groups.prepend("");
    int index = groups.indexOf(src);
    bool ok = false;
    QString dst = QInputDialog::getItem(this, tr("Group name"), tr("Enter a new group name."), groups, index, true, &ok);
    dst = dst.simplified();

    if (ok) {
        APPLICATION->instances()->setInstanceGroup(instId, dst);
    }
}

void MainWindow::deleteGroup(QString group)
{
    Q_ASSERT(!group.isEmpty());

    const int reply = QMessageBox::question(this, tr("Delete group"), tr("Are you sure you want to delete the group '%1'?").arg(group),
                                            QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes)
        APPLICATION->instances()->deleteGroup(group);
}

void MainWindow::setGroupIcon(QString group)
{
    Q_ASSERT(!group.isEmpty());

    InstanceList* instances = APPLICATION->instances();

    IconPickerDialog dlg(this);
    dlg.execWithSelection(instances->getGroupIconKey(group));

    if (dlg.result() == QDialog::Accepted) {
        instances->setGroupIconKey(group, dlg.selectedIconKey);
        view->updateGeometries();
        view->viewport()->update();
    }
}

void MainWindow::removeGroupIcon(QString group)
{
    Q_ASSERT(!group.isEmpty());

    InstanceList* instances = APPLICATION->instances();
    instances->setGroupIconKey(group, "");
    view->updateGeometries();
    view->viewport()->update();
}

void MainWindow::renameGroup(QString group)
{
    Q_ASSERT(!group.isEmpty());

    QString name = QInputDialog::getText(this, tr("Rename group"), tr("Enter a new group name."), QLineEdit::Normal, group);
    name = name.simplified();
    if (name.isNull() || name == group)
        return;

    const bool empty = name.isEmpty();
    const bool duplicate = APPLICATION->instances()->getGroups().contains(name, Qt::CaseInsensitive) && group.toLower() != name.toLower();

    if (empty || duplicate) {
        QMessageBox::warning(this, tr("Cannot rename group"), empty ? tr("Cannot set empty name.") : tr("Group already exists. :/"));
        return;
    }

    APPLICATION->instances()->renameGroup(group, name);
}

void MainWindow::undoTrashInstance()
{
    if (!APPLICATION->instances()->undoTrashInstance())
        QMessageBox::warning(
            this, tr("Failed to undo trashing instance"),
            tr("Some instances and shortcuts could not be restored.\nPlease check your trashbin to manually restore them."));
    ui->actionUndoTrashInstance->setEnabled(APPLICATION->instances()->trashedSomething());
}

void MainWindow::on_actionViewLauncherRootFolder_triggered()
{
    DesktopServices::openPath(".");
}

void MainWindow::on_actionViewInstanceFolder_triggered()
{
    QString str = APPLICATION->settings()->get("InstanceDir").toString();
    DesktopServices::openPath(str);
}

void MainWindow::on_actionViewCentralModsFolder_triggered()
{
    DesktopServices::openPath(APPLICATION->settings()->get("CentralModsDir").toString(), true);
}

void MainWindow::on_actionViewSkinsFolder_triggered()
{
    DesktopServices::openPath(APPLICATION->settings()->get("SkinsDir").toString(), true);
}

void MainWindow::on_actionViewIconThemeFolder_triggered()
{
    DesktopServices::openPath(APPLICATION->themeManager()->getIconThemesFolder().path(), true);
}

void MainWindow::on_actionViewWidgetThemeFolder_triggered()
{
    DesktopServices::openPath(APPLICATION->themeManager()->getApplicationThemesFolder().path(), true);
}

void MainWindow::on_actionViewCatPackFolder_triggered()
{
    DesktopServices::openPath(APPLICATION->themeManager()->getCatPacksFolder().path(), true);
}

void MainWindow::on_actionViewIconsFolder_triggered()
{
    DesktopServices::openPath(APPLICATION->icons()->getDirectory(), true);
}

void MainWindow::on_actionViewLogsFolder_triggered()
{
    DesktopServices::openPath("logs", true);
}

void MainWindow::on_actionViewJavaFolder_triggered()
{
    DesktopServices::openPath(APPLICATION->javaPath(), true);
}

void MainWindow::on_actionViewTemplateFolder_triggered()
{
    DesktopServices::openPath(APPLICATION->settings()->get("TemplateDir").toString(), true);
}

void MainWindow::refreshInstances()
{
    APPLICATION->instances()->loadList();
}

void MainWindow::checkForUpdates()
{
    if (APPLICATION->updaterEnabled()) {
        APPLICATION->triggerUpdateCheck();
    } else {
        qWarning() << "Updater not set up. Cannot check for updates.";
    }
}

void MainWindow::on_actionSettings_triggered()
{
    APPLICATION->ShowGlobalSettings(this, "global-settings");
}

void MainWindow::globalSettingsClosed()
{
    proxymodel->invalidate();
    proxymodel->sort(0);
    m_library->syncSortFromSettings();
    updateMainToolBar();
    updateLaunchButton();
    updateThemeMenu();
    updateStatusCenter();
    updateCatState();
    // This needs to be done to prevent UI elements disappearing in the event the config is changed
    // but Prism Launcher exits abnormally, causing the window state to never be saved:
    APPLICATION->settings()->set("MainWindowState", QString::fromUtf8(saveState().toBase64()));
    update();
}

void MainWindow::on_actionEditInstance_triggered()
{
    editInstance(m_selectedInstance);
}

void MainWindow::editInstance(MinecraftInstance* instance)
{
    if (!instance)
        return;

    if (instance->canEdit()) {
        APPLICATION->showInstanceWindow(instance);
    } else {
        CustomMessageBox::selectable(this, tr("Instance not editable"),
                                     tr("This instance is not editable. It may be broken, invalid, or too old. Check logs for details."),
                                     QMessageBox::Critical)
            ->show();
    }
}

void MainWindow::on_actionManageSkins_triggered()
{
    auto account = APPLICATION->accounts()->defaultAccount();

    if (account && (account->accountType() == AccountType::MSA) && !account->isActive()) {
        SkinManageDialog dialog(this, account);
        dialog.exec();
    }
}

void MainWindow::on_actionManageAccounts_triggered()
{
    APPLICATION->ShowGlobalSettings(this, "accounts");
}

void MainWindow::on_actionReportBug_triggered()
{
    DesktopServices::openUrl(QUrl(BuildConfig.BUG_TRACKER_URL));
}

void MainWindow::on_actionClearMetadata_triggered()
{
    // This if contains side effects!
    if (!APPLICATION->metacache()->evictAll()) {
        CustomMessageBox::selectable(this, tr("Error"),
                                     tr("Metadata cache clear Failed!\nTo clear the metadata cache manually, press Folders -> Launcher "
                                        "Data, and after closing the launcher delete the folder named \"meta\"\n"),
                                     QMessageBox::Warning)
            ->show();
    }

    APPLICATION->metacache()->SaveNow();
}

#ifdef Q_OS_MACOS
void MainWindow::on_actionAddToPATH_triggered()
{
    auto binaryPath = APPLICATION->applicationFilePath();
    auto targetPath = QString("/usr/local/bin/%1").arg(BuildConfig.LAUNCHER_APP_BINARY_NAME);
    qDebug() << "Symlinking" << binaryPath << "to" << targetPath;

    QStringList args;
    args << "-e";
    args << QString("do shell script \"mkdir -p /usr/local/bin && ln -sf '%1' '%2'\" with administrator privileges")
                .arg(binaryPath, targetPath);
    auto outcome = QProcess::execute("/usr/bin/osascript", args);
    if (!outcome) {
        QMessageBox::information(this, tr("Successfully added %1 to PATH").arg(BuildConfig.LAUNCHER_DISPLAYNAME),
                                 tr("%1 was successfully added to your PATH. You can now start it by running `%2`.")
                                     .arg(BuildConfig.LAUNCHER_DISPLAYNAME, BuildConfig.LAUNCHER_APP_BINARY_NAME));
    } else {
        QMessageBox::critical(this, tr("Failed to add %1 to PATH").arg(BuildConfig.LAUNCHER_DISPLAYNAME),
                              tr("An error occurred while trying to add %1 to PATH").arg(BuildConfig.LAUNCHER_DISPLAYNAME));
    }
}
#endif

void MainWindow::on_actionOpenWiki_triggered()
{
    DesktopServices::openUrl(QUrl(BuildConfig.WIKI_URL));
}

void MainWindow::on_actionMoreNews_triggered()
{
    auto entries = m_newsChecker->getNewsEntries();
    NewsDialog news_dialog(entries, this);
    news_dialog.exec();
}

void MainWindow::newsButtonClicked()
{
    auto entries = m_newsChecker->getNewsEntries();
    NewsDialog news_dialog(entries, this);
    news_dialog.toggleArticleList();
    news_dialog.exec();
}

void MainWindow::onCatChanged(int)
{
    setCatBackground(APPLICATION->settings()->get("TheCat").toBool());
}

void MainWindow::on_actionAbout_triggered()
{
    AboutDialog dialog(this);
    dialog.exec();
}

void MainWindow::on_actionDeleteInstance_triggered()
{
    if (!m_selectedInstance) {
        return;
    }

    if (m_selectedInstance->isRunning()) {
        CustomMessageBox::selectable(this, tr("Cannot Delete Running Instance"),
                                     tr("The selected instance is currently running and cannot be deleted. Please stop the instance before "
                                        "attempting to delete it."),
                                     QMessageBox::Warning, QMessageBox::Ok)
            ->exec();
        return;
    }
    auto id = m_selectedInstance->id();
    const QString deletedName = m_selectedInstance->name();

    QString shortcutStr;
    auto shortcuts = m_selectedInstance->shortcuts();
    if (!shortcuts.isEmpty())
        shortcutStr = tr(" and its %n registered shortcut(s)", "", shortcuts.size());
    auto response = CustomMessageBox::selectable(this, tr("Confirm Deletion"),
                                                 tr("You are about to delete \"%1\"%2.\n"
                                                    "This may be permanent and will completely delete the instance.\n\n"
                                                    "Are you sure?")
                                                     .arg(m_selectedInstance->name(), shortcutStr),
                                                 QMessageBox::Warning, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                        ->exec();

    if (response != QMessageBox::Yes)
        return;

    if (!checkLinkedInstances(id, this, tr("Deleting")))
        return;

    if (APPLICATION->instances()->trashInstance(id)) {
        ui->actionUndoTrashInstance->setEnabled(APPLICATION->instances()->trashedSomething());
        Zero::ZeroToast::show(this, tr("Deleted \"%1\"").arg(deletedName), Zero::ZeroToast::Kind::Info, tr("Undo"),
                              [this]() { undoTrashInstance(); });
    } else {
        APPLICATION->instances()->deleteInstance(id);
    }
    APPLICATION->settings()->set("SelectedInstance", QString());
    selectionBad();
}

void MainWindow::on_actionExportInstanceZip_triggered()
{
    if (m_selectedInstance) {
        ExportInstanceDialog dlg(m_selectedInstance, this);
        dlg.exec();
    }
}

void MainWindow::on_actionExportInstanceMrPack_triggered()
{
    if (m_selectedInstance) {
        ExportPackDialog dlg(m_selectedInstance, this);
        dlg.exec();
    }
}

void MainWindow::on_actionExportInstanceFlamePack_triggered()
{
    if (m_selectedInstance) {
        if (auto cmp = m_selectedInstance->getPackProfile()->getComponent("net.minecraft");
            cmp && cmp->getVersionFile() && cmp->getVersionFile()->type == "snapshot") {
            QMessageBox msgBox(this);
            msgBox.setText("Snapshots are currently not supported by CurseForge modpacks.");
            msgBox.exec();
            return;
        }
        ExportPackDialog dlg(m_selectedInstance, this, ModPlatform::ResourceProvider::FLAME);
        dlg.exec();
    }
}

void MainWindow::on_actionRenameInstance_triggered()
{
    if (m_selectedInstance) {
        m_library->beginRename();
    }
}

void MainWindow::on_actionViewSelectedInstFolder_triggered()
{
    if (m_selectedInstance) {
        QString str = m_selectedInstance->instanceRoot();
        DesktopServices::openPath(QFileInfo(str));
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // Save the window state and geometry.
    APPLICATION->settings()->set("MainWindowState", QString::fromUtf8(saveState().toBase64()));
    APPLICATION->settings()->set("MainWindowGeometry", QString::fromUtf8(saveGeometry().toBase64()));
    event->accept();
    emit isClosing();
}

void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::instanceActivated(QModelIndex index)
{
    if (!index.isValid())
        return;
    QString id = index.data(InstanceList::InstanceIDRole).toString();
    MinecraftInstance* inst = APPLICATION->instances()->getInstanceById(id);
    if (!inst)
        return;

    if (APPLICATION->settings()->get("EditInstanceOnDoubleClick").toBool()) {
        if (inst->canEdit()) {
            APPLICATION->showInstanceWindow(inst);
        } else {
            CustomMessageBox::selectable(
                this, tr("Instance not editable"),
                tr("This instance is not editable. It may be broken, invalid, or too old. Check logs for details."), QMessageBox::Critical)
                ->show();
        }
        return;
    }
    APPLICATION->launch(inst);
}

void MainWindow::on_actionLaunchInstance_triggered()
{
    if (m_selectedInstance && !m_selectedInstance->isRunning()) {
        APPLICATION->launch(m_selectedInstance);
    }
}

void MainWindow::on_actionKillInstance_triggered()
{
    if (m_selectedInstance && m_selectedInstance->isRunning()) {
        APPLICATION->kill(m_selectedInstance);
    }
}

void MainWindow::on_actionCreateInstanceShortcut_triggered()
{
    if (!m_selectedInstance)
        return;

    CreateShortcutDialog shortcutDlg(m_selectedInstance, this);
    if (!shortcutDlg.exec())
        return;
    shortcutDlg.createShortcut();
}

void MainWindow::taskEnd()
{
    QObject* sender = QObject::sender();
    if (sender == m_versionLoadTask)
        m_versionLoadTask = NULL;

    sender->deleteLater();
}

void MainWindow::startTask(Task* task)
{
    connect(task, &Task::succeeded, this, &MainWindow::taskEnd);
    connect(task, &Task::failed, this, &MainWindow::taskEnd);
    task->start();
}

void MainWindow::instanceChanged(const QModelIndex& current, [[maybe_unused]] const QModelIndex& previous)
{
    if (!current.isValid()) {
        APPLICATION->settings()->set("SelectedInstance", QString());
        selectionBad();
        return;
    }
    if (m_selectedInstance) {
        disconnect(m_selectedInstance, &BaseInstance::runningStatusChanged, this, &MainWindow::refreshCurrentInstance);
        disconnect(m_selectedInstance, &BaseInstance::profilerChanged, this, &MainWindow::refreshCurrentInstance);
    }
    QString id = current.data(InstanceList::InstanceIDRole).toString();
    m_selectedInstance = APPLICATION->instances()->getInstanceById(id);
    if (m_selectedInstance) {
        ui->instanceToolBar->setEnabled(true);
        setInstanceActionsEnabled(true);
        ui->actionLaunchInstance->setEnabled(m_selectedInstance->canLaunch());

        ui->actionKillInstance->setEnabled(m_selectedInstance->isRunning());
        ui->actionExportInstance->setEnabled(m_selectedInstance->canExport());
        m_nameLabel->setText(m_selectedInstance->name());
        m_statusLeft->setText(m_selectedInstance->getStatusbarDescription());
        updateStatusCenter();
        updateInstanceToolIcon(m_selectedInstance->iconKey());

        updateLaunchButton();

        APPLICATION->settings()->set("SelectedInstance", m_selectedInstance->id());
        if (m_library)
            m_library->setCurrentInstance(m_selectedInstance);

        connect(m_selectedInstance, &BaseInstance::runningStatusChanged, this, &MainWindow::refreshCurrentInstance);
        connect(m_selectedInstance, &BaseInstance::profilerChanged, this, &MainWindow::refreshCurrentInstance);
    } else {
        APPLICATION->settings()->set("SelectedInstance", QString());
        selectionBad();
        return;
    }
}

void MainWindow::instanceSelectRequest(QString id)
{
    setSelectedInstanceById(id);
}

void MainWindow::instanceDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    auto current = view->selectionModel()->currentIndex();
    QItemSelection test(topLeft, bottomRight);
    if (test.contains(current)) {
        instanceChanged(current, current);
    }
}

void MainWindow::selectionBad()
{
    // start by reseting everything...
    m_selectedInstance = nullptr;
    m_statusLeft->setText(tr("No instance selected"));

    statusBar()->clearMessage();
    ui->instanceToolBar->setEnabled(false);
    setInstanceActionsEnabled(false);
    updateLaunchButton();
    m_nameLabel->setText(tr("No Instance Selected"));
    updateInstanceToolIcon("default");
    if (m_library)
        m_library->setCurrentInstance(nullptr);

    // ...and then see if we can enable the previously selected instance
    setSelectedInstanceById(APPLICATION->settings()->get("SelectedInstance").toString());
}

void MainWindow::checkInstancePathForProblems()
{
    QString instanceFolder = APPLICATION->settings()->get("InstanceDir").toString();
    if (FS::checkProblemticPathJava(QDir(instanceFolder))) {
        QMessageBox warning(this);
        warning.setText(tr("Your instance folder contains \'!\' and this is known to cause Java problems!"));
        warning.setInformativeText(tr("You have now two options: <br/>"
                                      " - change the instance folder in the settings <br/>"
                                      " - move this installation of %1 to a different folder")
                                       .arg(BuildConfig.LAUNCHER_DISPLAYNAME));
        warning.setDefaultButton(QMessageBox::Ok);
        warning.exec();
    }
    auto tempFolderText =
        tr("This is a problem: <br/>"
           " - The launcher will likely be deleted without warning by the operating system <br/>"
           " - close the launcher now and extract it to a real location, not a temporary folder");
    QString pathfoldername = QDir(instanceFolder).absolutePath();
    if (pathfoldername.contains("Rar$", Qt::CaseInsensitive)) {
        QMessageBox warning(this);
        warning.setText(tr("Your instance folder contains \'Rar$\' - that means you haven't extracted the launcher archive!"));
        warning.setInformativeText(tempFolderText);
        warning.setDefaultButton(QMessageBox::Ok);
        warning.exec();
    } else if (pathfoldername.startsWith(QDir::tempPath()) || pathfoldername.contains("/TempState/")) {
        QMessageBox warning(this);
        warning.setText(tr("Your instance folder is in a temporary folder: \'%1\'!").arg(QDir::tempPath()));
        warning.setInformativeText(tempFolderText);
        warning.setDefaultButton(QMessageBox::Ok);
        warning.exec();
    }
}

void MainWindow::updateStatusCenter()
{
    m_statusCenter->setVisible(APPLICATION->settings()->get("ShowGlobalGameTime").toBool());
    int64_t timePlayed = APPLICATION->playtimeSettings()->get("TotalPlayTime").toLongLong();
    if (timePlayed > 0) {
        m_statusCenter->setText(
            tr("Total playtime: %1")
                .arg(Time::prettifyDuration(timePlayed, APPLICATION->settings()->get("ShowGameTimeWithoutDays").toBool())));
    }
}
// "Instance actions" are actions that require an instance to be selected (i.e. "new instance" is not here)
// Actions that also require other conditions (e.g. a running instance) won't be changed.
void MainWindow::setInstanceActionsEnabled(bool enabled)
{
    ui->actionEditInstance->setEnabled(enabled);
    ui->actionChangeInstGroup->setEnabled(enabled);
    ui->actionViewSelectedInstFolder->setEnabled(enabled);
    ui->actionExportInstance->setEnabled(enabled);
    ui->actionDeleteInstance->setEnabled(enabled);
    ui->actionCopyInstance->setEnabled(enabled);
    ui->actionCreateInstanceShortcut->setEnabled(enabled);
}

void MainWindow::refreshCurrentInstance()
{
    auto current = view->selectionModel()->currentIndex();
    instanceChanged(current, current);
}

void MainWindow::updateInstanceToolBarOrientation(Qt::Orientation orientation)
{
    ui->instanceToolBar->removeAction(ui->actionRenameInstance);
    ui->instanceToolBar->removeAction(ui->actionChangeInstIcon);

    if (orientation == Qt::Vertical) {
        m_instanceInfoWidget->setVisible(true);
    } else {
        m_instanceInfoWidget->setVisible(false);
        ui->instanceToolBar->insertActionBefore(m_instanceToolBarTopSeparator, ui->actionRenameInstance);
        ui->instanceToolBar->insertActionBefore(m_instanceToolBarTopSeparator, ui->actionChangeInstIcon);
    }
    // HACK: sync size (no, I don't know why this works)
    ui->instanceToolBar->resize(0, 0);
}

void MainWindow::handleInstanceRename(const QString& before, const QString& after)
{
    const auto newRoot = askToUpdateInstanceDirName(m_selectedInstance, before, after, this);
    if (newRoot.isEmpty()) {
        return;
    }

    auto oldID = m_selectedInstance->id();
    auto newID = QFileInfo(newRoot).fileName();
    QString origGroup(APPLICATION->instances()->getInstanceGroup(oldID));
    bool syncGroup = origGroup != GroupId() && oldID != newID;
    if (syncGroup) {
        APPLICATION->instances()->setInstanceGroup(oldID, GroupId());
    }

    refreshInstances();
    setSelectedInstanceById(newID);

    if (syncGroup) {
        APPLICATION->instances()->setInstanceGroup(newID, origGroup);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// ZERO shell. MainWindow stays the controller; the pages only trigger its actions / APIs (docs/zero/SHELL.md).
// ---------------------------------------------------------------------------------------------------------------------

void MainWindow::setupZeroShell()
{
    m_shell = new Zero::ZeroShell(ui->centralWidget);
    ui->horizontalLayout->addWidget(m_shell);

    m_home = new Zero::HomePage(m_shell);
    m_library = new Zero::LibraryPage(view, proxymodel, m_shell);
    m_shell->addPage(QStringLiteral("home"), QStringLiteral("house"), tr("Home"), m_home);
    m_shell->addPage(QStringLiteral("library"), QStringLiteral("library-big"), tr("Library"), m_library);
    m_worlds = new Zero::WorldsPage(m_shell);
    m_shell->addPage(QStringLiteral("worlds"), QStringLiteral("earth"), tr("Worlds"), m_worlds);
    m_screenshots = new Zero::ScreenshotsPage(m_shell);
    m_shell->addPage(QStringLiteral("screenshots"), QStringLiteral("image"), tr("Screenshots"), m_screenshots);
    m_servers = new Zero::ServersPage(m_shell);
    m_shell->addPage(QStringLiteral("servers"), QStringLiteral("compass"), tr("Servers"), m_servers);
    m_tools = new Zero::ToolsPage(m_shell);
    m_shell->addPage(QStringLiteral("tools"), QStringLiteral("wrench"), tr("Tools"), m_tools);
    m_appearance = new Zero::AppearancePage(m_shell);
    m_shell->addPage(QStringLiteral("appearance"), QStringLiteral("palette"), tr("Appearance"), m_appearance,
                     Zero::ZeroShell::Placement::Bottom);  // bottom group: Appearance, More, Settings
    m_shell->finishRail();

    // The toolbars and menu bar are hidden, so their shortcuts would be dead: register every action on the window.
    addActions({ ui->actionAddInstance, ui->actionSettings, ui->actionUndoTrashInstance, ui->actionLaunchInstance,
                 ui->actionKillInstance, ui->actionRenameInstance, ui->actionChangeInstGroup, ui->actionChangeInstIcon,
                 ui->actionEditInstance, ui->actionViewSelectedInstFolder, ui->actionDeleteInstance, ui->actionCopyInstance,
                 ui->actionExportInstance, ui->actionExportInstanceZip, ui->actionExportInstanceMrPack,
                 ui->actionExportInstanceFlamePack, ui->actionCreateInstanceShortcut, ui->actionNoDefaultAccount,
                 ui->actionManageAccounts });

    Zero::LibraryActions actions;
    actions.launch = ui->actionLaunchInstance;
    actions.kill = ui->actionKillInstance;
    actions.edit = ui->actionEditInstance;
    actions.folder = ui->actionViewSelectedInstFolder;
    actions.group = ui->actionChangeInstGroup;
    actions.copy = ui->actionCopyInstance;
    actions.exportInstance = ui->actionExportInstance;
    actions.shortcut = ui->actionCreateInstanceShortcut;
    actions.remove = ui->actionDeleteInstance;
    m_library->setActions(actions);
    m_library->listView()->installEventFilter(this);

    // Library
    connect(m_library, &Zero::LibraryPage::newInstanceRequested, this, &MainWindow::on_actionAddInstance_triggered);
    connect(m_library, &Zero::LibraryPage::importRequested, this, [this]() { addInstance(QString(), {}, QStringLiteral("import")); });
    connect(m_library, &Zero::LibraryPage::instanceActivated, this, &MainWindow::instanceActivated);
    connect(m_library, &Zero::LibraryPage::listContextMenuRequested, this,
            [this](const QPoint& globalPos, const QModelIndex&) { showInstanceContextMenuAt(globalPos, true, QString()); });
    connect(m_library, &Zero::LibraryPage::renameRequested, this, &MainWindow::renameSelectedInstance);
    connect(m_library, &Zero::LibraryPage::changeIconRequested, this, &MainWindow::on_actionChangeInstIcon_triggered);
    connect(m_library, &Zero::LibraryPage::favoriteToggleRequested, this, [this]() {
        if (m_selectedInstance)
            toggleFavorite(m_selectedInstance->id());
    });
    connect(m_library, &Zero::LibraryPage::checkModsRequested, this, [this]() {
        m_tools->openModHealth(m_selectedInstance ? m_selectedInstance->id() : QString());
        m_shell->setCurrentPage(QStringLiteral("tools"));
    });
    connect(m_library, &Zero::LibraryPage::setArtworkRequested, this, &MainWindow::chooseArtworkForSelected);
    connect(m_library, &Zero::LibraryPage::removeArtworkRequested, this, &MainWindow::removeArtworkFromSelected);

    // Worlds (read-only hub): "Show instance in Library" is the only way out
    connect(m_worlds, &Zero::WorldsPage::showInstanceRequested, this, &MainWindow::showInLibrary);
    connect(m_screenshots, &Zero::ScreenshotsPage::showInstanceRequested, this, &MainWindow::showInLibrary);
    connect(m_servers, &Zero::ServersPage::showInstanceRequested, this, &MainWindow::showInLibrary);
    connect(m_shell, &Zero::ZeroShell::pageChanged, this, [this]() { m_screenshots->viewer()->closeViewer(); });  // never left open behind another page
    connect(m_tools, &Zero::ToolsPage::showInstanceRequested, this, &MainWindow::showInLibrary);
    connect(m_tools, &Zero::ToolsPage::javaSettingsRequested, this,
            [this]() { QTimer::singleShot(0, this, [this]() { APPLICATION->ShowGlobalSettings(this, QStringLiteral("java-settings")); }); });

    // Home
    auto withInstance = [](const QString& id, auto&& fn) {
        if (auto* inst = APPLICATION->instances()->getInstanceById(id))
            fn(inst);
    };
    connect(m_home, &Zero::HomePage::playRequested, this, [withInstance](const QString& id) {
        withInstance(id, [](MinecraftInstance* inst) {
            if (!inst->isRunning())
                APPLICATION->launch(inst);
        });
    });
    connect(m_home, &Zero::HomePage::stopRequested, this, [withInstance](const QString& id) {
        withInstance(id, [](MinecraftInstance* inst) {
            if (inst->isRunning())
                APPLICATION->kill(inst);
        });
    });
    connect(m_home, &Zero::HomePage::editRequested, this,
            [this, withInstance](const QString& id) { withInstance(id, [this](MinecraftInstance* inst) { editInstance(inst); }); });
    connect(m_home, &Zero::HomePage::folderRequested, this, [withInstance](const QString& id) {
        withInstance(id, [](MinecraftInstance* inst) { DesktopServices::openPath(QFileInfo(inst->instanceRoot())); });
    });
    connect(m_home, &Zero::HomePage::openInLibraryRequested, this, [this](const QString& id) {
        setSelectedInstanceById(id);
        m_shell->setCurrentPage(QStringLiteral("library"));
        if (view->currentIndex().isValid())
            view->scrollTo(view->currentIndex());
    });
    connect(m_home, &Zero::HomePage::diagnoseRequested, this, [this](const QString& id, std::optional<int> exitCode) {
        m_tools->openCrashDetective(id, true, exitCode);
        m_shell->setCurrentPage(QStringLiteral("tools"));
    });
    connect(m_home, &Zero::HomePage::viewLibraryRequested, this, [this]() { m_shell->setCurrentPage(QStringLiteral("library")); });
    connect(m_home, &Zero::HomePage::newInstanceRequested, this, &MainWindow::on_actionAddInstance_triggered);
    connect(m_home, &Zero::HomePage::importRequested, this, [this]() { addInstance(QString(), {}, QStringLiteral("import")); });
    connect(m_home, &Zero::HomePage::accountsRequested, this, &MainWindow::on_actionManageAccounts_triggered);
    connect(m_home, &Zero::HomePage::instancesFolderRequested, this, &MainWindow::on_actionViewInstanceFolder_triggered);

    // Rail
    connect(m_shell, &Zero::ZeroShell::accountMenuRequested, this, &MainWindow::showAccountMenu);
    connect(m_shell, &Zero::ZeroShell::moreMenuRequested, this, &MainWindow::showMoreMenu);
    connect(m_shell, &Zero::ZeroShell::settingsRequested, ui->actionSettings, &QAction::trigger);
    connect(m_shell, &Zero::ZeroShell::pageChanged, this, [this](const QString& id) {
        if (id == QLatin1String("library"))
            m_library->activeView()->setFocus();
        if (id == QLatin1String("tools") && m_selectedInstance)
            m_tools->setPreferredInstance(m_selectedInstance->id());  // Crash Detective defaults to the Library selection
    });

    // Appearance: the page asks, MainWindow applies and persists (same as the legacy theme menu).
    connect(m_appearance, &Zero::AppearancePage::applyThemeRequested, this, [](const QString& id) {
        if (!APPLICATION->themeManager()->isValidApplicationTheme(id))
            return;
        APPLICATION->themeManager()->setApplicationTheme(id);
        APPLICATION->settings()->set("ApplicationTheme", id);
    });
    connect(m_appearance, &Zero::AppearancePage::moreOptionsRequested, this,
            [this]() { APPLICATION->ShowGlobalSettings(this, QStringLiteral("appearance-settings")); });

    // Tools: Crash Detective opens an instance's settings through the same window the Edit button uses.
    connect(m_tools, &Zero::ToolsPage::instanceSettingsRequested, this, [withInstance](const QString& id) {
        withInstance(id, [](MinecraftInstance* inst) {
            if (inst->canEdit())
                APPLICATION->showInstanceWindow(inst, QStringLiteral("settings"));
            else
                APPLICATION->showInstanceWindow(inst);
        });
    });
    // Automatic world backups: one toast per category (backed up / failed / too large) when the work of an instance ended.
    if (auto* autoBackup = Zero::Backup::AutoBackup::global()) {
        connect(autoBackup, &Zero::Backup::AutoBackup::finished, this,
                [this](const QString&, const QString& instanceName, int backedUp, const QStringList& tooLarge, int failed,
                       const QString& firstError) {
                    if (backedUp > 0)
                        Zero::ZeroToast::show(this, (backedUp == 1 ? tr("Backed up 1 world of %1").arg(instanceName) : tr("Backed up %1 worlds of %2").arg(backedUp).arg(instanceName)),
                                              Zero::ZeroToast::Kind::Success);
                    if (failed > 0)
                        Zero::ZeroToast::show(this, tr("Automatic backup failed for %1: %2").arg(instanceName, firstError),
                                              Zero::ZeroToast::Kind::Error);
                    if (!tooLarge.isEmpty())
                        Zero::ZeroToast::show(this,
                                              tr("%1 is too large for automatic backups. Back it up manually from Worlds.")
                                                  .arg(tooLarge.join(QStringLiteral(", "))),
                                              Zero::ZeroToast::Kind::Warning);
                });
    }
    // Crash hook: the game ended abnormally (crash / non-zero exit, not a user kill) -> offer a diagnosis.
    connect(APPLICATION, &Application::gameStoppedUnexpectedly, this, [this](const QString& id, int exitCode) {
        auto* inst = APPLICATION->instances()->getInstanceById(id);
        if (!inst)
            return;
        Zero::ZeroToast::show(this, tr("%1 stopped unexpectedly").arg(inst->name()), Zero::ZeroToast::Kind::Warning, tr("Diagnose"),
                              [this, id, exitCode]() {
                                  m_tools->openCrashDetective(id, true, exitCode);
                                  m_shell->setCurrentPage(QStringLiteral("tools"));
                                  raise();
                                  activateWindow();
                              });
    });

    setupCommandPalette();
    hideLegacyChrome();
}

void MainWindow::hideLegacyChrome()
{
    ui->mainToolBar->hide();
    ui->newsToolBar->hide();
    ui->instanceToolBar->hide();
    ui->menuBar->hide();
    statusBar()->hide();
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    // restoreState() may bring the legacy toolbars back; they are not part of the ZERO shell.
    hideLegacyChrome();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    // Files / links dropped anywhere outside the instance grid (Home, list mode, empty library) are imported too.
    if (!event->mimeData()->hasUrls())
        return;
    event->acceptProposedAction();
    const auto urls = event->mimeData()->urls();
    QMetaObject::invokeMethod(this, [this, urls]() { processURLs(urls); }, Qt::QueuedConnection);
}

void MainWindow::updateAccountChip()
{
    if (!m_shell)
        return;
    const MinecraftAccountPtr account = APPLICATION->accounts()->defaultAccount();
    if (account && !account->profileName().isEmpty()) {
        m_shell->setAccount(account->displayName(), account->accountType() == AccountType::MSA ? tr("Microsoft account") : tr("Offline"),
                            account->getFace());
    } else {
        m_shell->setAccount(tr("Accounts"), tr("No account"), QPixmap());
    }
}

namespace {
void popupAbove(QMenu* menu, const QPoint& anchor)
{
    menu->popup(QPoint(anchor.x(), anchor.y() - menu->sizeHint().height()));
}
}  // namespace

void MainWindow::showAccountMenu(const QPoint& anchor)
{
    // `anchor` = bottom-right corner of the top bar chip: the menu opens below it, right-aligned.
    if (auto* menu = ui->actionAccountsButton->menu())
        menu->popup(QPoint(anchor.x() - menu->sizeHint().width(), anchor.y() + Zero::Tokens::spaceXs));
}

void MainWindow::showMoreMenu(const QPoint& anchor)
{
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    menu->addMenu(ui->foldersMenu);

    auto* help = menu->addMenu(tr("Help"));
    bool lastWasSeparator = true;
    for (QAction* action : ui->helpMenu->actions()) {
        if (action == ui->actionCheckUpdate || action == ui->actionAbout || action == ui->actionMoreNews)
            continue;
        if (action->isSeparator()) {
            if (!lastWasSeparator)
                help->addSeparator();
            lastWasSeparator = true;
        } else if (action->isVisible() && action->isEnabled()) {
            help->addAction(action);
            lastWasSeparator = false;
        }
    }
    if (!help->actions().isEmpty() && help->actions().last()->isSeparator())
        help->removeAction(help->actions().last());
    help->menuAction()->setVisible(!help->actions().isEmpty());

    if (ui->actionUndoTrashInstance->isEnabled()) {
        menu->addSeparator();
        menu->addAction(ui->actionUndoTrashInstance);
    }
    menu->addSeparator();
    // The shortcut itself is a window-wide QShortcut; the tab-separated text only shows it in the menu.
    menu->addAction(tr("Keyboard shortcuts") + QStringLiteral("\tCtrl+/"), this, &MainWindow::toggleShortcutSheet);
    menu->addAction(ui->actionAbout);
    if (APPLICATION->updaterEnabled())
        menu->addAction(ui->actionCheckUpdate);

    popupAbove(menu, anchor);
}

// ---------------------------------------------------------------------------------------------------------------------
// Command palette (Ctrl+K) and shortcut reference (Ctrl+/). MainWindow only describes the commands: every callback calls an
// existing QAction or API. The list is rebuilt each time the palette opens, so it follows the actions' enabled state.
// ---------------------------------------------------------------------------------------------------------------------

namespace {
QString cleanActionText(QString text)
{
    text.replace(QStringLiteral("&&"), QStringLiteral("\x01"));
    text.remove(QLatin1Char('&'));
    text.replace(QLatin1Char('\x01'), QLatin1Char('&'));
    text = text.trimmed();
    if (text.endsWith(QStringLiteral("...")))
        text.chop(3);
    else if (text.endsWith(QChar(0x2026)))
        text.chop(1);
    return text.trimmed();
}

QString currentMotionValue()
{
    auto* settings = APPLICATION->settings();
    const QString value = settings->get("ZeroMotion").toString();
    if (value == QLatin1String("on") || value == QLatin1String("off"))
        return value;
    return (value.isEmpty() && settings->get("ZeroReduceMotion").toBool()) ? QStringLiteral("off") : QStringLiteral("system");
}
}  // namespace

void MainWindow::setupCommandPalette()
{
    // Ctrl+K opens the palette window-wide: the Kill action's shortcut moved to Ctrl+Shift+K in MainWindow.ui (a shortcut set
    // here would be reset by retranslateUi()).
    m_palette = new Zero::ZeroCommandPalette(this);
    m_palette->setCommandProvider([this]() { return buildPaletteCommands(); });
    m_shortcutSheet = new Zero::ZeroShortcutSheet(this);
    m_shortcutSheet->setGroupsProvider([this]() { return buildShortcutGroups(); });

    auto* pill = new Zero::ZeroSearchPill;
    m_shell->topBar()->setCenterWidget(pill);
    connect(pill, &QAbstractButton::clicked, this, &MainWindow::togglePalette);

    auto* paletteKey = new QShortcut(QKeySequence(QStringLiteral("Ctrl+K")), this);
    connect(paletteKey, &QShortcut::activated, this, &MainWindow::togglePalette);
    auto* sheetKey = new QShortcut(QKeySequence(QStringLiteral("Ctrl+/")), this);
    connect(sheetKey, &QShortcut::activated, this, &MainWindow::toggleShortcutSheet);
    // A page change (Alt+N, rail) must not leave an overlay on top of the new page.
    connect(m_shell, &Zero::ZeroShell::pageChanged, this, [this]() {
        m_palette->closeOverlay();
        m_shortcutSheet->closeOverlay();
    });
}

void MainWindow::togglePalette()
{
    m_shortcutSheet->closeOverlay();
    if (m_palette->isOpen())
        m_palette->closeOverlay();
    else
        m_palette->open();
}

void MainWindow::toggleShortcutSheet()
{
    m_palette->closeOverlay();
    if (m_shortcutSheet->isOpen())
        m_shortcutSheet->closeOverlay();
    else
        m_shortcutSheet->open();
}

void MainWindow::showInLibrary(const QString& instanceId)
{
    setSelectedInstanceById(instanceId);
    m_shell->setCurrentPage(QStringLiteral("library"));
    if (view->currentIndex().isValid())
        view->scrollTo(view->currentIndex());
}

QList<Zero::PaletteCommand> MainWindow::buildPaletteCommands()
{
    using Zero::PaletteCommand;
    using Kind = Zero::Palette::Kind;
    QList<PaletteCommand> out;

    // Instances: Enter = Play (the same launch call Home uses), Ctrl+Enter = show in Library. A running instance cannot be
    // launched again (Prism ignores it), so Enter shows it in the Library instead.
    auto* list = APPLICATION->instances();
    for (int i = 0; i < list->count(); ++i) {
        MinecraftInstance* inst = list->at(i);
        const QString id = inst->id();
        const Zero::InstanceInfo info = Zero::InstanceInfo::of(inst);
        PaletteCommand c;
        c.id = QStringLiteral("instance:") + id;
        c.kind = Kind::Instance;
        c.title = inst->name();
        if (inst->isRunning()) {
            c.subtitle = tr("Running");
        } else {
            QStringList parts;
            if (!info.summary().isEmpty())
                parts << info.summary();
            parts << (inst->lastLaunch() > 0 ? tr("Played %1").arg(Zero::Format::relativeTime(inst->lastLaunch())) : tr("Never played"));
            c.subtitle = parts.join(QStringLiteral(" · "));
        }
        c.keywords = { info.minecraftVersion, info.loader, QStringLiteral("instance") };
        c.icon = APPLICATION->icons()->getIcon(inst->iconKey());
        c.lastUsedMs = inst->lastLaunch();
        c.favorite = Zero::Profile::isFavorite(inst);
        if (c.favorite)
            c.keywords << tr("favorite") << tr("star");
        if (inst->isRunning()) {
            c.hint = tr("Show ↵");
            c.primaryLabel = tr("Show in Library");
            c.run = [this, id]() { showInLibrary(id); };
        } else {
            c.hint = tr("Play ↵");
            c.primaryLabel = tr("Play");
            c.run = [id]() {
                if (auto* instance = APPLICATION->instances()->getInstanceById(id); instance && !instance->isRunning())
                    APPLICATION->launch(instance);
            };
            c.secondaryLabel = tr("Show in Library");
            c.runSecondary = [this, id]() { showInLibrary(id); };
        }
        out.append(c);
    }

    // Navigation.
    struct Nav {
        const char* page;
        QString title;
        const char* icon;
        QStringList keywords;
    };
    const QList<Nav> pages = {
        { "home", tr("Go to Home"), "house", { tr("start"), tr("continue") } },
        { "library", tr("Go to Library"), "library-big", { tr("instances"), tr("games"), tr("packs") } },
        { "worlds", tr("Go to Worlds"), "earth", { tr("saves"), tr("maps") } },
        { "screenshots", tr("Go to Screenshots"), "image", { tr("screenshots"), tr("gallery"), tr("images"), tr("pictures") } },
        { "servers", tr("Go to Servers"), "compass", { tr("servers"), tr("multiplayer"), tr("ping"), tr("join") } },
        { "tools", tr("Go to Tools"), "wrench", { tr("utilities") } },
        { "appearance", tr("Go to Appearance"), "palette", { tr("themes"), tr("look") } },
    };
    for (const Nav& n : pages) {
        const QString page = QString::fromLatin1(n.page);
        if (!m_shell->page(page))
            continue;
        PaletteCommand c;
        c.id = QStringLiteral("nav:") + page;
        c.kind = Kind::Navigation;
        c.title = n.title;
        c.keywords = n.keywords;
        c.iconName = QString::fromLatin1(n.icon);
        c.shortcut = m_shell->pageShortcut(page);
        c.primaryLabel = tr("Go");
        c.run = [this, page]() { m_shell->setCurrentPage(page); };
        out.append(c);
    }
    {
        PaletteCommand c;
        c.id = QStringLiteral("nav:crash-detective");
        c.kind = Kind::Navigation;
        c.title = tr("Open Crash Detective");
        c.keywords = { tr("crash"), tr("diagnose"), tr("log"), tr("error"), tr("fix") };
        c.iconName = QStringLiteral("stethoscope");
        c.primaryLabel = tr("Open");
        c.run = [this]() {
            m_tools->openCrashDetective(m_selectedInstance ? m_selectedInstance->id() : QString(), false);
            m_shell->setCurrentPage(QStringLiteral("tools"));
        };
        out.append(c);
    }
    {
        PaletteCommand c;
        c.id = QStringLiteral("nav:storage");
        c.kind = Kind::Navigation;
        c.title = tr("Open Storage");
        c.keywords = { tr("storage"), tr("disk"), tr("space"), tr("cleanup"), tr("size") };
        c.iconName = QStringLiteral("hard-drive");
        c.primaryLabel = tr("Open");
        c.run = [this]() {
            m_tools->openStorage();
            m_shell->setCurrentPage(QStringLiteral("tools"));
        };
        out.append(c);
    }
    {
        PaletteCommand c;
        c.id = QStringLiteral("nav:java-doctor");
        c.kind = Kind::Navigation;
        c.title = tr("Open Java Doctor");
        c.keywords = { tr("java"), tr("jdk"), tr("jre"), tr("runtime") };
        c.iconName = QStringLiteral("cpu");
        c.primaryLabel = tr("Open");
        c.run = [this]() {
            m_tools->openJavaDoctor();
            m_shell->setCurrentPage(QStringLiteral("tools"));
        };
        out.append(c);
    }
    {
        PaletteCommand c;
        c.id = QStringLiteral("nav:mod-health");
        c.kind = Kind::Navigation;
        c.title = tr("Check mods");
        c.keywords = { tr("mods"), tr("health"), tr("conflicts"), tr("dependencies"), tr("missing") };
        c.iconName = QStringLiteral("package");
        c.primaryLabel = tr("Open");
        c.run = [this]() {
            m_tools->openModHealth(m_selectedInstance ? m_selectedInstance->id() : QString());
            m_shell->setCurrentPage(QStringLiteral("tools"));
        };
        out.append(c);
    }

    // Actions: pulled from the real QActions (shortcut, enabled state) at open time.
    auto addAction = [&out](const QString& id, const QString& title, const QString& icon, QAction* action, const QStringList& keywords,
                            bool suggested = false, const QString& subtitle = QString()) {
        if (!action || !action->isEnabled() || !action->isVisible())
            return;
        PaletteCommand c;
        c.id = id;
        c.kind = Kind::Action;
        c.title = title;
        c.subtitle = subtitle;
        c.keywords = keywords;
        c.iconName = icon;
        c.shortcut = action->shortcut();
        c.suggested = suggested;
        c.primaryLabel = QObject::tr("Run");
        c.run = [action]() { action->trigger(); };
        out.append(c);
    };
    addAction(QStringLiteral("action:new-instance"), tr("New instance"), QStringLiteral("plus"), ui->actionAddInstance,
              { tr("create"), tr("add"), tr("install"), tr("modpack") }, true);
    {
        PaletteCommand c;
        c.id = QStringLiteral("action:import-instance");
        c.kind = Kind::Action;
        c.title = tr("Import instance");
        c.keywords = { tr("modpack"), tr("zip"), tr("mrpack"), tr("curseforge"), tr("file") };
        c.iconName = QStringLiteral("package");
        c.suggested = true;
        c.primaryLabel = tr("Import");
        c.run = [this]() { addInstance(QString(), {}, QStringLiteral("import")); };
        out.append(c);
    }
    addAction(QStringLiteral("action:accounts"), tr("Manage accounts"), QStringLiteral("user"), ui->actionManageAccounts,
              { tr("microsoft"), tr("login"), tr("sign in"), tr("profile"), tr("offline") });
    for (QAction* folder : ui->foldersMenu->actions()) {
        if (folder->isSeparator())
            continue;
        const QString name = cleanActionText(folder->text());
        addAction(QStringLiteral("action:folder:") + folder->objectName(), tr("Open folder: %1").arg(name), QStringLiteral("folder-open"), folder,
                  { tr("directory"), tr("explorer"), tr("open") });
    }
    addAction(QStringLiteral("action:undo-delete"), tr("Undo last instance deletion"), QStringLiteral("arrow-left"), ui->actionUndoTrashInstance,
              { tr("restore"), tr("trash") });
    addAction(QStringLiteral("action:settings"), tr("Settings"), QStringLiteral("settings"), ui->actionSettings, { tr("preferences"), tr("options") });
    addAction(QStringLiteral("action:about"), cleanActionText(ui->actionAbout->text()), QStringLiteral("info"), ui->actionAbout,
              { tr("version"), tr("credits"), tr("license") });
    if (APPLICATION->updaterEnabled())
        addAction(QStringLiteral("action:check-update"), tr("Check for updates"), QStringLiteral("download"), ui->actionCheckUpdate,
                  { tr("update"), tr("upgrade") });
    {
        PaletteCommand c;
        c.id = QStringLiteral("action:keyboard-shortcuts");
        c.kind = Kind::Action;
        c.title = tr("Keyboard shortcuts");
        c.keywords = { tr("keys"), tr("hotkeys"), tr("help"), tr("cheat sheet") };
        c.iconName = QStringLiteral("command");
        c.shortcut = QKeySequence(QStringLiteral("Ctrl+/"));
        c.suggested = true;
        c.primaryLabel = tr("Show");
        c.run = [this]() { QTimer::singleShot(0, this, &MainWindow::toggleShortcutSheet); };
        out.append(c);
    }
    // The selected instance.
    if (m_selectedInstance) {
        const QString name = m_selectedInstance->name();
        addAction(QStringLiteral("action:instance-edit"), tr("Edit instance"), QStringLiteral("pencil"), ui->actionEditInstance,
                  { tr("settings"), tr("mods"), tr("configure") }, false, name);
        addAction(QStringLiteral("action:instance-folder"), tr("Open instance folder"), QStringLiteral("folder-open"), ui->actionViewSelectedInstFolder,
                  { tr("directory"), tr("explorer") }, false, name);
        {
            PaletteCommand c;
            const QString id = m_selectedInstance->id();
            c.id = QStringLiteral("action:instance-favorite");
            c.kind = Kind::Action;
            c.title = tr("Toggle favorite");
            c.subtitle = name;
            c.keywords = { tr("star"), tr("favourite"), tr("pin"), tr("bookmark") };
            c.iconName = QStringLiteral("star");
            c.primaryLabel = Zero::Profile::isFavorite(m_selectedInstance) ? tr("Remove from favorites") : tr("Add to favorites");
            c.run = [this, id]() { toggleFavorite(id); };
            out.append(c);
        }
        if (ui->actionRenameInstance->isEnabled()) {
            PaletteCommand c;
            c.id = QStringLiteral("action:instance-rename");
            c.kind = Kind::Action;
            c.title = tr("Rename instance");
            c.subtitle = name;
            c.iconName = QStringLiteral("pencil");
            c.primaryLabel = tr("Rename");
            c.run = [this]() {
                m_shell->setCurrentPage(QStringLiteral("library"));  // the rename editor lives in the Library
                QTimer::singleShot(0, this, [this]() { ui->actionRenameInstance->trigger(); });
            };
            out.append(c);
        }
        const qsizetype before = out.size();
        addAction(QStringLiteral("action:instance-delete"), tr("Delete instance"), QStringLiteral("trash-2"), ui->actionDeleteInstance,
                  { tr("remove"), tr("trash") }, false, name);
        if (out.size() > before && !ui->actionDeleteInstance->shortcuts().isEmpty())
            out.last().shortcut = ui->actionDeleteInstance->shortcuts().last();  // show "Delete" rather than "Backspace"
    }

    // Settings: one command per global settings page (ids from the page providers in Application.cpp).
    struct SettingsPage {
        const char* id;
        QString title;
        QStringList keywords;
    };
    const QList<SettingsPage> settingsPages = {
        { "launcher-settings", tr("General"), { tr("launcher"), tr("startup"), tr("updates"), tr("console"), tr("logs"), tr("folders"), tr("paths") } },
        { "language-settings", tr("Language"), { tr("translation"), tr("locale") } },
        { "appearance-settings", tr("Appearance"), { tr("theme"), tr("icons"), tr("widgets"), tr("fonts") } },
        { "minecraft-settings", tr("Minecraft"), { tr("window"), tr("resolution"), tr("fullscreen"), tr("game"), tr("performance") } },
        { "java-settings", tr("Java"), { tr("memory"), tr("ram"), tr("jvm"), tr("heap"), tr("allocation"), tr("runtime"), tr("jdk") } },
        { "accounts", tr("Accounts"), { tr("microsoft"), tr("login"), tr("profile"), tr("skin") } },
        { "apis", tr("Services"), { tr("curseforge"), tr("modrinth"), tr("api"), tr("key"), tr("metadata") } },
        { "external-tools", tr("Tools"), { tr("editor"), tr("profiler"), tr("jprofiler"), tr("jvisualvm") } },
        { "proxy-settings", tr("Proxy"), { tr("network"), tr("http"), tr("socks"), tr("connection") } },
    };
    for (const SettingsPage& s : settingsPages) {
        const QString pageId = QString::fromLatin1(s.id);
        if (Zero::Language::kEnglishOnly && pageId == QLatin1String("language-settings"))
            continue;  // the Language page is hidden while ZERO is English only
        PaletteCommand c;
        c.id = QStringLiteral("settings:") + pageId;
        c.kind = Kind::Setting;
        c.title = tr("Settings: %1").arg(s.title);
        c.keywords = s.keywords;
        c.iconName = QStringLiteral("settings");
        c.primaryLabel = tr("Open");
        c.run = [this, pageId]() { QTimer::singleShot(0, this, [this, pageId]() { APPLICATION->ShowGlobalSettings(this, pageId); }); };
        out.append(c);
    }

    // Appearance: ZERO themes and the motion setting.
    {
        QString current = APPLICATION->settings()->get("ApplicationTheme").toString();
        if (current.isEmpty())
            current = QString::fromLatin1(Zero::kDefaultThemeId);
        bool suggestedGiven = false;
        for (ITheme* theme : APPLICATION->themeManager()->getValidApplicationThemes()) {
            if (!Zero::isZeroThemeId(theme->id()))
                continue;
            const QString themeId = theme->id();
            PaletteCommand c;
            c.id = QStringLiteral("theme:") + themeId;
            c.kind = Kind::Appearance;
            c.title = tr("Theme: %1").arg(theme->name());
            c.keywords = { tr("appearance"), tr("colors"), tr("dark"), tr("skin") };
            c.iconName = QStringLiteral("palette");
            c.primaryLabel = tr("Apply");
            if (themeId == current) {
                c.badge = tr("Current");
            } else if (!suggestedGiven) {
                c.suggested = true;  // "switch theme" in the empty-query suggestions
                suggestedGiven = true;
            }
            c.run = [themeId]() {
                if (!APPLICATION->themeManager()->isValidApplicationTheme(themeId))
                    return;
                APPLICATION->themeManager()->setApplicationTheme(themeId);
                APPLICATION->settings()->set("ApplicationTheme", themeId);
            };
            out.append(c);
        }
        const QString motion = currentMotionValue();
        const QList<QPair<QString, QString>> motions = { { QStringLiteral("system"), tr("Animations: Follow Windows") },
                                                         { QStringLiteral("on"), tr("Animations: On") },
                                                         { QStringLiteral("off"), tr("Animations: Off") } };
        for (const auto& m : motions) {
            const QString value = m.first;
            PaletteCommand c;
            c.id = QStringLiteral("motion:") + value;
            c.kind = Kind::Appearance;
            c.title = m.second;
            c.keywords = { tr("motion"), tr("reduce motion"), tr("transitions"), tr("effects") };
            c.iconName = QStringLiteral("clock");
            c.primaryLabel = tr("Apply");
            if (value == motion)
                c.badge = tr("Current");
            c.run = [value]() { APPLICATION->settings()->set("ZeroMotion", value); };
            out.append(c);
        }
    }
    return out;
}

QList<Zero::ShortcutGroup> MainWindow::buildShortcutGroups()
{
    using Zero::ShortcutEntry;
    using Zero::ShortcutGroup;
    auto entry = [](const QString& label, const QKeySequence& keys) { return ShortcutEntry{ label, Zero::keyCaps(keys) }; };

    ShortcutGroup navigation;
    navigation.title = tr("Navigation");
    navigation.entries << entry(tr("Command palette"), QKeySequence(QStringLiteral("Ctrl+K")));
    navigation.entries << entry(tr("Keyboard shortcuts"), QKeySequence(QStringLiteral("Ctrl+/")));
    const QList<QPair<QString, QString>> pages = { { QStringLiteral("home"), tr("Home") },
                                                    { QStringLiteral("library"), tr("Library") },
                                                    { QStringLiteral("worlds"), tr("Worlds") },
                                                    { QStringLiteral("screenshots"), tr("Screenshots") },
                                                    { QStringLiteral("servers"), tr("Servers") },
                                                    { QStringLiteral("tools"), tr("Tools") },
                                                    { QStringLiteral("appearance"), tr("Appearance") } };
    for (const auto& p : pages) {
        const QKeySequence keys = m_shell->pageShortcut(p.first);
        if (!keys.isEmpty())
            navigation.entries << entry(tr("Go to %1").arg(p.second), keys);
    }
    navigation.entries << entry(tr("Search instances / worlds"), QKeySequence(QStringLiteral("Ctrl+F")));
    navigation.entries << ShortcutEntry{ tr("Close palette, clear search"), { QStringLiteral("Esc") } };

    ShortcutGroup instances;
    instances.title = tr("Instances");
    const QList<QAction*> instanceActions = { ui->actionAddInstance,
                                              ui->actionLaunchInstance,
                                              ui->actionKillInstance,
                                              ui->actionEditInstance,
                                              ui->actionRenameInstance,
                                              ui->actionChangeInstGroup,
                                              ui->actionChangeInstIcon,
                                              ui->actionViewSelectedInstFolder,
                                              ui->actionCopyInstance,
                                              ui->actionCreateInstanceShortcut,
                                              ui->actionDeleteInstance,
                                              ui->actionUndoTrashInstance };
    for (QAction* a : instanceActions) {
        if (a->shortcuts().isEmpty())
            continue;
        const QKeySequence keys = a == ui->actionDeleteInstance ? a->shortcuts().last() : a->shortcut();
        instances.entries << entry(a == ui->actionAddInstance ? tr("New instance") : cleanActionText(a->text()), keys);
    }
    instances.entries << ShortcutEntry{ tr("Rename (Library)"), { QStringLiteral("F2") } };
    instances.entries << ShortcutEntry{ tr("Refresh instances (Library)"), { QStringLiteral("F5") } };

    ShortcutGroup app;
    app.title = tr("App");
    if (!ui->actionSettings->shortcut().isEmpty())
        app.entries << entry(tr("Settings"), ui->actionSettings->shortcut());
    if (!m_accountActions.isEmpty())
        app.entries << ShortcutEntry{ tr("Switch account"), { QStringLiteral("Ctrl"), QStringLiteral("1-9") } };
    if (!ui->actionNoDefaultAccount->shortcut().isEmpty())
        app.entries << entry(tr("Clear default account"), ui->actionNoDefaultAccount->shortcut());

    QList<ShortcutGroup> groups = { navigation, instances, app };
    groups.removeIf([](const ShortcutGroup& g) { return g.entries.isEmpty(); });
    return groups;
}

void MainWindow::toggleFavorite(const QString& instanceId)
{
    if (auto* instance = APPLICATION->instances()->getInstanceById(instanceId))
        Zero::Profile::setFavorite(instance, !Zero::Profile::isFavorite(instance));
}

void MainWindow::chooseArtworkForSelected()
{
    if (!m_selectedInstance)
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Choose artwork"), QString(),
                                                      tr("Images (*.png *.jpg *.jpeg *.webp *.bmp);;All files (*)"));
    if (path.isEmpty())
        return;
    const QString error = Zero::Profile::setArtwork(m_selectedInstance, path);
    if (!error.isEmpty())
        Zero::ZeroToast::show(this, error, Zero::ZeroToast::Kind::Error);
}

void MainWindow::removeArtworkFromSelected()
{
    if (m_selectedInstance)
        Zero::Profile::clearArtwork(m_selectedInstance);
}

void MainWindow::renameSelectedInstance(const QString& newName)
{
    if (m_selectedInstance == nullptr)
        return;
    auto* instances = APPLICATION->instances();
    const auto oldName = m_selectedInstance->name();
    const bool ok = instances->setData(instances->index(instances->getInstIndex(m_selectedInstance)), newName, Qt::EditRole);
    if (!ok) {
        m_nameLabel->setText(m_selectedInstance->name());
        return;
    }

    handleInstanceRename(oldName, m_selectedInstance->name());
    m_nameLabel->setText(m_selectedInstance->name());
}

void MainWindow::zeroScreenshot(const QString& path, const QString& page)
{
    // Dev aid: ZERO_SCREENSHOT_THEME=<theme id> (e.g. zero-minecraft) applies that application theme before grabbing.
    if (const QString themeId = qEnvironmentVariable("ZERO_SCREENSHOT_THEME"); !themeId.isEmpty()) {
        if (APPLICATION->themeManager()->isValidApplicationTheme(themeId))
            APPLICATION->themeManager()->setApplicationTheme(themeId, false);
        else
            qWarning() << "ZERO_SCREENSHOT_THEME: unknown theme" << themeId;
    }
    const bool library = page == QLatin1String("library");
    m_shell->setCurrentPage(library                              ? QStringLiteral("library")
                            : page == QLatin1String("worlds")     ? QStringLiteral("worlds")  // ZERO
                            : page == QLatin1String("screenshots") ? QStringLiteral("screenshots")  // ZERO
                            : page == QLatin1String("servers")    ? QStringLiteral("servers")  // ZERO
                            : page == QLatin1String("tools")      ? QStringLiteral("tools")
                            : page == QLatin1String("appearance") ? QStringLiteral("appearance")
                                                                  : QStringLiteral("home"));
    // Dev aid: ZERO_SCREENSHOT_SELECT_THEME=<theme id> selects that card on the Appearance page (nothing is applied).
    if (const QString pick = qEnvironmentVariable("ZERO_SCREENSHOT_SELECT_THEME"); !pick.isEmpty())
        m_appearance->selectTheme(pick);
    QSize size(1280, 800);
    // Dev aid: ZERO_SCREENSHOT_SIZE=<w>x<h> (e.g. 1100x700 to review the collapsed rail).
    const QStringList dims = qEnvironmentVariable("ZERO_SCREENSHOT_SIZE").split(QLatin1Char('x'));
    if (dims.size() == 2 && dims[0].toInt() > 0 && dims[1].toInt() > 0)
        size = QSize(dims[0].toInt(), dims[1].toInt());
    resize(size);
    // Dev aid: ZERO_SCREENSHOT_CHIP=<all|favorites|running|loader:<name>|tag:<name>> selects a Library filter chip.
    if (const QString chip = qEnvironmentVariable("ZERO_SCREENSHOT_CHIP"); !chip.isEmpty()) {
        using Kind = Zero::LibraryFilter::Kind;
        Zero::LibraryFilter::Chip selected;
        if (chip == QLatin1String("favorites"))
            selected = { Kind::Favorites, {} };
        else if (chip == QLatin1String("running"))
            selected = { Kind::Running, {} };
        else if (chip.startsWith(QLatin1String("loader:")))
            selected = { Kind::Loader, chip.mid(7) };
        else if (chip.startsWith(QLatin1String("tag:")))
            selected = { Kind::Tag, chip.mid(4) };
        m_library->setChip(selected);
    }
    // Dev aid: ZERO_SCREENSHOT_SEARCH=<text> types into the Library search field.
    if (const QString search = qEnvironmentVariable("ZERO_SCREENSHOT_SEARCH"); !search.isEmpty())
        m_library->setSearchText(search);
    if (library && !view->currentIndex().isValid() && proxymodel->rowCount() > 0)
        view->selectionModel()->setCurrentIndex(proxymodel->index(0, 0), QItemSelectionModel::ClearAndSelect);

    // Dev aid: ZERO_SCREENSHOT_BACKUP=<instanceId>/<folder> creates (synchronously, before the Worlds page is grabbed) two manual
    // backups, one safety and one auto backup of that world in the normal backups folder, so the Backups block has something to show.
    if (const QString spec = qEnvironmentVariable("ZERO_SCREENSHOT_BACKUP"); !spec.isEmpty()) {
        const int slash = spec.indexOf(QLatin1Char('/'));
        auto* inst = slash > 0 ? APPLICATION->instances()->getInstanceById(spec.left(slash)) : nullptr;
        if (inst) {
            const QString folder = spec.mid(slash + 1);
            const QString dir = Zero::Backup::backupDirFor(APPLICATION->dataRoot(), inst->id(), folder);
            const Zero::Backup::BackupMeta meta{ inst->id(), inst->name(), folder, folder };
            for (const auto kind : { Zero::Backup::Kind::Manual, Zero::Backup::Kind::Safety, Zero::Backup::Kind::Auto, Zero::Backup::Kind::Manual }) {
                const auto r = Zero::Backup::createBackup(QDir(inst->worldDir()).filePath(folder), dir, kind, meta);
                qInfo() << "ZERO_SCREENSHOT_BACKUP:" << (r.ok ? r.zipPath : r.error);
            }
        } else {
            qWarning() << "ZERO_SCREENSHOT_BACKUP: expected <instanceId>/<folder> of a known instance, got" << spec;
        }
    }

    // Dev aid: ZERO_SCREENSHOT_WORLD=<instanceId>/<folder> selects that world on the Worlds page; ZERO_SCREENSHOT_WORLD_SEARCH=<text>
    // types into its search field.
    if (const QString world = qEnvironmentVariable("ZERO_SCREENSHOT_WORLD"); !world.isEmpty())
        m_worlds->selectWorld(world);
    if (const QString worldSearch = qEnvironmentVariable("ZERO_SCREENSHOT_WORLD_SEARCH"); !worldSearch.isEmpty())
        m_worlds->setSearchText(worldSearch);

    // Dev aid: ZERO_SCREENSHOT_RUNNING=<instance id> flips that instance to "running" through the normal signal path
    // (BaseInstance::setRunning -> InstanceList::dataChanged -> pages), so the live running state can be reviewed.
    const QString runningId = qEnvironmentVariable("ZERO_SCREENSHOT_RUNNING");
    if (!runningId.isEmpty()) {
        if (auto* inst = APPLICATION->instances()->getInstanceById(runningId))
            inst->setRunning(true);
    }

    // Dev aid: ZERO_SCREENSHOT_TOAST=1 shows one toast of each kind (info, success, warning, error).
    if (qEnvironmentVariableIntValue("ZERO_SCREENSHOT_TOAST") == 1) {
        QTimer::singleShot(300, this, [this]() {
            Zero::ZeroToast::show(this, tr("Instance copied"), Zero::ZeroToast::Kind::Info);
            Zero::ZeroToast::show(this, tr("Launch finished"), Zero::ZeroToast::Kind::Success);
            Zero::ZeroToast::show(this, tr("Fabulous Survival stopped unexpectedly"), Zero::ZeroToast::Kind::Warning, tr("Diagnose"), []() {});
            Zero::ZeroToast::show(this, tr("Could not start the game"), Zero::ZeroToast::Kind::Error);
        });
    }

    // Dev aid: ZERO_SCREENSHOT_LAUNCH_PHASE=<instance id>:<preparing|running> emits Application::launchPhaseChanged for that
    // instance (the signal the Play buttons listen to), so the Preparing / Running looks can be reviewed without a launch.
    if (const QString spec = qEnvironmentVariable("ZERO_SCREENSHOT_LAUNCH_PHASE"); !spec.isEmpty()) {
        const int colon = spec.lastIndexOf(QLatin1Char(':'));
        const QString id = spec.left(colon), phase = spec.mid(colon + 1);
        if (colon > 0 && (phase == QLatin1String("preparing") || phase == QLatin1String("running"))) {
            QTimer::singleShot(300, this, [id, phase]() {
                emit APPLICATION->launchPhaseChanged(id, phase == QLatin1String("running") ? Application::LaunchPhase::Running
                                                                                          : Application::LaunchPhase::Preparing);
            });
        } else {
            qWarning() << "ZERO_SCREENSHOT_LAUNCH_PHASE: expected <instance id>:<preparing|running>, got" << spec;
        }
    }

    // Dev aid: ZERO_SCREENSHOT_DIAGNOSE=<instance id> opens Tools > Crash Detective for that instance and analyzes it
    // (ZERO_SCREENSHOT_DIAGNOSE_EXIT=<code> passes an exit code, ZERO_SCREENSHOT_EVIDENCE=1 expands the evidence).
    if (const QString diagnoseId = qEnvironmentVariable("ZERO_SCREENSHOT_DIAGNOSE"); !diagnoseId.isEmpty()) {
        std::optional<int> exitCode;
        if (const QString code = qEnvironmentVariable("ZERO_SCREENSHOT_DIAGNOSE_EXIT"); !code.isEmpty())
            exitCode = code.toInt(nullptr, 0);
        m_shell->setCurrentPage(QStringLiteral("tools"));
        m_tools->openCrashDetective(diagnoseId, true, exitCode);
        if (qEnvironmentVariableIntValue("ZERO_SCREENSHOT_EVIDENCE") == 1) {
            connect(m_tools->crashDetective(), &Zero::CrashDetectiveView::analysisFinished, this,
                    [this]() { m_tools->crashDetective()->expandAllEvidence(); });
        }
    }

    // Dev aid: ZERO_SCREENSHOT_TOOL=storage opens Tools > Storage (with --zero-page tools); the grab waits for the scan (5 s max).
    bool waitForStorage = false;
    if (qEnvironmentVariable("ZERO_SCREENSHOT_TOOL") == QLatin1String("storage")) {
        m_tools->openStorage();
        waitForStorage = m_tools->storage()->scanner()->isScanning();
    }

    // Dev aid: ZERO_SCREENSHOT_TOOL=java opens Tools > Java Doctor; the grab waits for the first evaluation (8 s max).
    bool waitForJava = false;
    if (qEnvironmentVariable("ZERO_SCREENSHOT_TOOL") == QLatin1String("java")) {
        m_tools->openJavaDoctor();
        waitForJava = !m_tools->javaDoctor()->hasEvaluated();
    }

    // Dev aid: ZERO_SCREENSHOT_TOOL=mods (+ ZERO_SCREENSHOT_MODS_INSTANCE=<id>) opens Tools > Mod Health; the grab waits for the report (8 s max).
    bool waitForMods = false;
    if (qEnvironmentVariable("ZERO_SCREENSHOT_TOOL") == QLatin1String("mods")) {
        m_tools->openModHealth(qEnvironmentVariable("ZERO_SCREENSHOT_MODS_INSTANCE"));
        waitForMods = m_tools->modHealth()->isScanning();
    }

    // Dev aids: ZERO_SCREENSHOT_PALETTE=<query> opens the command palette with that query ("empty" = no query),
    // ZERO_SCREENSHOT_SHORTCUTS=1 opens the keyboard shortcut sheet.
    if (const QString paletteQuery = qEnvironmentVariable("ZERO_SCREENSHOT_PALETTE"); !paletteQuery.isEmpty()) {
        QTimer::singleShot(200, this, [this, paletteQuery]() {
            m_palette->open();
            if (paletteQuery != QLatin1String("empty"))
                m_palette->setQuery(paletteQuery);
        });
    }
    if (qEnvironmentVariableIntValue("ZERO_SCREENSHOT_SHORTCUTS") == 1)
        QTimer::singleShot(200, this, [this]() { m_shortcutSheet->open(); });

    // Let the layout, icon loading and the page fade settle before grabbing.
    auto grabAndFinish = [this, path]() {
        QApplication::processEvents();
        const bool ok = grab().save(path);
        qInfo() << "ZERO screenshot" << (ok ? "saved to" : "FAILED for") << path;

        // Dev aids (motion evidence): after the normal grab, trigger a page switch (ZERO_SCREENSHOT_TRANSITION=<page id>)
        // and/or a theme switch (ZERO_SCREENSHOT_THEME_TO=<theme id>) and save the window every 40 ms for 320 ms as
        // <file>-t0..t8.png, plus <file>-tfinal.png once everything has settled. Page first, then theme.
        const QString toPage = qEnvironmentVariable("ZERO_SCREENSHOT_TRANSITION");
        const QString toTheme = qEnvironmentVariable("ZERO_SCREENSHOT_THEME_TO");
        if (!ok || (toPage.isEmpty() && toTheme.isEmpty())) {
            APPLICATION->exit(ok ? 0 : 1);
            return;
        }
        auto trigger = [this, toPage, toTheme]() {
            if (!toPage.isEmpty())
                m_shell->setCurrentPage(toPage);
            else if (APPLICATION->themeManager()->isValidApplicationTheme(toTheme))
                APPLICATION->themeManager()->setApplicationTheme(toTheme, false);
            else
                qWarning() << "ZERO_SCREENSHOT_THEME_TO: unknown theme" << toTheme;
        };
        recordTransitionFrames(path, trigger, [this, toPage, toTheme, path]() {
            if (!toPage.isEmpty() && !toTheme.isEmpty()) {
                // second round: the theme switch, on the page we just opened (files get a "-theme" suffix)
                QString themePath = path;
                themePath.insert(themePath.lastIndexOf(QLatin1Char('.')), QStringLiteral("-theme"));
                recordTransitionFrames(themePath,
                                       [this, toTheme]() {
                                           if (APPLICATION->themeManager()->isValidApplicationTheme(toTheme))
                                               APPLICATION->themeManager()->setApplicationTheme(toTheme, false);
                                       },
                                       []() { APPLICATION->exit(0); });
            } else {
                APPLICATION->exit(0);
            }
        });
    };
    // ZERO: the Worlds page scans asynchronously: wait for the scan (sizes included), 3 s at most
    if (page == QLatin1String("worlds"))
        m_worlds->callWhenIdle([this, grabAndFinish]() { QTimer::singleShot(600, this, grabAndFinish); }, 3000);
    else if (page == QLatin1String("screenshots")) {
        // ZERO: the Screenshots page scans and decodes thumbnails on the pool: wait for the scan and the visible thumbnails (4 s at most).
        // Dev aid: ZERO_SCREENSHOT_VIEWER=<file name> opens the viewer on that file and waits for its full image before the grab.
        m_screenshots->callWhenIdle(
            [this, grabAndFinish]() {
                const QString viewerFile = qEnvironmentVariable("ZERO_SCREENSHOT_VIEWER");
                if (!viewerFile.isEmpty() && m_screenshots->openViewerByFileName(viewerFile))
                    m_screenshots->callWhenViewerReady([this, grabAndFinish]() { QTimer::singleShot(600, this, grabAndFinish); }, 4000);
                else
                    QTimer::singleShot(600, this, grabAndFinish);
            },
            4000);
    }
    else if (page == QLatin1String("servers")) {
        // ZERO: the Servers page scans on the pool and pings over the network: wait for the scan and the pings (6 s at most).
        // Dev aid: ZERO_SCREENSHOT_SERVER=<address> selects that server (any spelling of the address).
        if (const QString address = qEnvironmentVariable("ZERO_SCREENSHOT_SERVER"); !address.isEmpty())
            m_servers->selectServer(address);
        m_servers->callWhenIdle([this, grabAndFinish]() { QTimer::singleShot(600, this, grabAndFinish); }, 6000);
    }
    else if (waitForMods) {
        // ZERO: Mod Health reads the mods on a pool thread: grab once the report is on screen (8 s at most)
        auto done = std::make_shared<bool>(false);
        auto fire = [this, grabAndFinish, done]() {
            if (*done)
                return;
            *done = true;
            QTimer::singleShot(600, this, grabAndFinish);
        };
        connect(m_tools->modHealth(), &Zero::ModHealthView::reportShown, this, fire);
        QTimer::singleShot(8000, this, fire);
    } else if (waitForJava) {
        // ZERO: Java Doctor detects runtimes asynchronously: grab once the first evaluation is on screen (8 s at most)
        auto done = std::make_shared<bool>(false);
        auto fire = [this, grabAndFinish, done]() {
            if (*done)
                return;
            *done = true;
            QTimer::singleShot(600, this, grabAndFinish);
        };
        connect(m_tools->javaDoctor(), &Zero::JavaDoctorView::evaluationFinished, this, fire);
        QTimer::singleShot(8000, this, fire);
    } else if (waitForStorage) {
        // ZERO: Storage scans asynchronously: grab once the report is in (5 s at most)
        auto* scanner = m_tools->storage()->scanner();
        auto done = std::make_shared<bool>(false);
        auto fire = [this, grabAndFinish, done]() {
            if (*done)
                return;
            *done = true;
            QTimer::singleShot(600, this, [this, grabAndFinish]() {
                if (qEnvironmentVariableIntValue("ZERO_SCREENSHOT_SCROLL") == 1)
                    m_tools->storage()->scrollToEnd();
                grabAndFinish();
            });
        };
        connect(scanner, &Zero::Storage::StorageScanner::reportReady, this, fire);
        QTimer::singleShot(5000, this, fire);
    } else
        QTimer::singleShot(1500, this, grabAndFinish);
}

void MainWindow::recordTransitionFrames(const QString& path, const std::function<void()>& trigger, const std::function<void()>& done)
{
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    const QString stem = dot > 0 ? path.left(dot) : path;
    const QString ext = dot > 0 ? path.mid(dot) : QStringLiteral(".png");
    constexpr int kFrames = 9, kStepMs = 40;
    trigger();
    for (int i = 0; i < kFrames; ++i) {
        // Frame 0 is taken right after the trigger (same event-loop turn: the overlay's first picture).
        auto save = [this, stem, ext, i]() {
            const QString file = QStringLiteral("%1-t%2%3").arg(stem).arg(i).arg(ext);
            qInfo() << "ZERO transition frame" << file << (grab().save(file) ? "saved" : "FAILED");
        };
        if (i == 0)
            save();
        else
            QTimer::singleShot(i * kStepMs, this, save);
    }
    QTimer::singleShot(900, this, [this, stem, ext, done]() {
        QApplication::processEvents();
        const QString file = QStringLiteral("%1-tfinal%2").arg(stem, ext);
        qInfo() << "ZERO transition frame" << file << (grab().save(file) ? "saved" : "FAILED");
        done();
    });
}
