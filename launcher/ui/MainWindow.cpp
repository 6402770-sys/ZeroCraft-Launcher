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

#include <QColorDialog>
#include <QComboBox>
#include <QScreen>
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
#include <QPainter>
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
#include "ui/zero/accounts/AddAccountDialog.h"      // ZERO
#include "ui/zero/accounts/AddAccountScreenshot.h"  // ZERO
#include "ui/zero/create/CreateInstanceDialog.h"  // ZERO
#include "ui/zero/create/ImportPanel.h"           // ZERO
#include "ui/dialogs/NewsDialog.h"
#include "ui/dialogs/ProgressDialog.h"
#include "ui/dialogs/skins/SkinManageDialog.h"
#include "ui/instanceview/InstanceDelegate.h"
#include "ui/zero/ZeroToast.h"
#include "zero/ZeroLanguage.h"  // ZERO
#include "zero/backup/AutoBackup.h"  // ZERO
#include "zero/backup/PackSnapshotTasks.h"  // ZERO
#include "ui/zero/instance/ModsDialogs.h"  // ZERO
#include "zero/backup/WorldBackup.h"  // ZERO
#include "ui/zero/ZeroNavRail.h"
#include "ui/zero/ZeroTheme.h"
#include "ui/zero/pages/ZeroSettingsPage.h"
#include "ui/zero/ZeroFormat.h"
#include "ui/zero/palette/ZeroCommandPalette.h"
#include "ui/zero/palette/ZeroShortcutSheet.h"
#include "ui/zero/pages/InstanceInfo.h"
#include "ui/zero/shell/ZeroSearchPill.h"
#include "ui/zero/shell/ZeroTopBar.h"
#include "ui/zero/pages/HomePage.h"
#include "ui/zero/pages/LibraryPage.h"
#include "ui/zero/pages/WorldsPage.h"  // ZERO
#include "ui/zero/pages/DiscoverPage.h"  // ZERO
#include "ui/zero/instance/ModsPage.h"  // ZERO
#include "ui/zero/instance/ResourcePacksPage.h"  // ZERO
#include "ui/zero/content/CartReviewDialog.h"  // ZERO
#include "ui/zero/perf/PerformanceDialog.h"  // ZERO
#include "ui/zero/content/ModInstallDialog.h"  // ZERO
#include "ui/zero/content/SelectionBar.h"  // ZERO
#include "ui/zero/content/ModsSection.h"  // ZERO
#include "ui/zero/content/ModpackInstallDialog.h"  // ZERO
#include "ui/zero/pages/ExportChooserDialog.h"  // ZERO
#include "ui/zero/content/ModpacksSection.h"  // ZERO
#include "ui/zero/pages/ScreenshotsPage.h"  // ZERO
#include "ui/zero/pages/ServersPage.h"  // ZERO
#include "ui/zero/pages/ScreenshotViewer.h"  // ZERO
#include "zero/profiles/InstanceProfile.h"
#include "ui/zero/pages/CrashDetectiveView.h"
#include "ui/zero/pages/AppearancePage.h"
#include "ui/zero/pages/ThemeEditorPanel.h"
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
#include "ResourceDownloadTask.h"  // ZERO
#include "tasks/ConcurrentTask.h"  // ZERO
#include "tasks/SequentialTask.h"  // ZERO
#include "net/ChecksumValidator.h"  // ZERO
#include "modplatform/helpers/HashUtils.h"  // ZERO
#include "zero/content/WorldInstall.h"  // ZERO
#include "zero/content/CartInstall.h"  // ZERO
#include "zero/content/InstallCart.h"  // ZERO
#include "zero/content/ModInstall.h"  // ZERO
#include "zero/content/ModTarget.h"  // ZERO
#include "zero/content/PackOrder.h"  // ZERO
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
    // ZERO: "Add account..." sits above "Manage accounts" (one dialog for Microsoft, Ely.by and offline accounts)
    if (!m_actionAddAccount) {
        m_actionAddAccount = new QAction(tr("Add account..."), this);
        connect(m_actionAddAccount, &QAction::triggered, this, &MainWindow::addAccountZero);
    }
    ui->accountsMenu->addAction(m_actionAddAccount);
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

    ui->actionCAT->setVisible(false);  // ZERO: no cat background (was: catEnabled)
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

    // ZERO: "New instance" and "Import" open the ZERO Create Instance dialog; deep links (a url) and the classic
    // provider pages keep the classic NewInstanceDialog.
    if (url.isEmpty() && (initialPage.isEmpty() || initialPage == QLatin1String("import"))) {
        addInstanceZero(groupName, initialPage == QLatin1String("import"));
        return;
    }
    addInstanceClassic(groupName, url, extra_info, initialPage);
}

// ZERO: the classic dialog part of addInstance(), unchanged.
void MainWindow::addInstanceClassic(const QString& groupName, const QString& url, const QMap<QString, QString>& extra_info,
                                    const QString& initialPage)
{
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

// ZERO: Create Instance (docs/zero/CONTENT.md). Same settings and the same run-while-the-dialog-lives behaviour as the
// classic path; modpack sources, "More sources" and curseforge:// links close it and open the classic dialog instead.
void MainWindow::addInstanceZero(const QString& groupName, bool import)
{
    using Zero::Create::CreateInstanceDialog;
    const auto env = Zero::Create::CreateInstanceEnvironment::fromApplication();
    const auto kind = import ? CreateInstanceDialog::Kind::Import : CreateInstanceDialog::rememberedKind(env);
    CreateInstanceDialog dialog(env, groupName, kind, this);

    // A hand-over runs after the ZERO dialog closed, with the group the user had chosen in it.
    std::function<void()> handOver;
    auto classic = [this, &dialog, &handOver](const QString& url, const QString& page) {
        const QString group = dialog.instGroup();
        handOver = [this, group, url, page]() { addInstanceClassic(group, url, {}, page); };
        dialog.reject();
    };
    // Modrinth / CurseForge / Technic and "Browse all modpacks" open Discover › Modpacks (after the dialog closed);
    // "More sources" (ATLauncher, FTB, ...) keep the classic dialog.
    auto discover = [this, &dialog, &handOver](const QString& provider) {
        handOver = [this, provider]() { showDiscover(QStringLiteral("modpacks"), provider); };
        dialog.reject();
    };
    connect(&dialog, &CreateInstanceDialog::classicPageRequested, this, [classic, discover](const QString& page) {
        if (page == QLatin1String("modrinth") || page == QLatin1String("technic"))
            discover(page);
        else if (page == QLatin1String("flame"))
            discover(QStringLiteral("curseforge"));
        else
            classic({}, page);
    });
    connect(&dialog, &CreateInstanceDialog::classicImportRequested, this, [classic](const QString& url) { classic(url, {}); });
    connect(&dialog, &CreateInstanceDialog::browseModpacksRequested, this, [discover]() { discover({}); });

    if (m_zeroCreateHook)
        m_zeroCreateHook(&dialog);
    const int result = dialog.exec();
    if (handOver) {
        handOver();
        return;
    }
    if (result != QDialog::Accepted)
        return;

    APPLICATION->settings()->set("LastUsedGroupForNewInstance", dialog.instGroup());
    APPLICATION->settings()->set("LastUsedInstDirForNewInstance", dialog.instDir());

    // Run while the dialog is alive: import tasks parent their own dialogs to it.
    InstanceTask* creationTask = dialog.extractTask();
    if (creationTask) {
        instanceFromInstanceTask(creationTask);
    }
}

// ZERO: Install / Update of a Discover modpack (docs/zero/CONTENT.md, "Modpacks" > "Install"). The review dialog builds the provider's
// task exactly as Prism's classic pages do; like addInstanceZero() the task runs while the dialog is alive, because provider tasks
// parent their own dialogs (askIfShouldUpdate, optional files) to it. Prism's error reporting (runModalTask) is unchanged.
void MainWindow::installModpack(const std::shared_ptr<Zero::Content::ModpackItem>& item, const QString& updateInstanceId)
{
    using Zero::Content::ModpackInstallDialog;
    if (!item)
        return;
    const auto env = Zero::Content::ModpackInstallEnvironment::fromApplication(m_discover->modpacks()->source(item->provider));
    ModpackInstallDialog dialog(env, item, updateInstanceId, APPLICATION->settings()->get("LastUsedGroupForNewInstance").toString(), this);
    if (m_zeroInstallHook)
        m_zeroInstallHook(&dialog);
    if (dialog.exec() != QDialog::Accepted)
        return;

    if (!dialog.isUpdate()) {
        APPLICATION->settings()->set("LastUsedGroupForNewInstance", dialog.instGroup());
        APPLICATION->settings()->set("LastUsedInstDirForNewInstance", dialog.instDir());
    }

    // An update is Prism's: it removes the old pack files inside the instance while its task runs. So the copy of the instance is taken
    // first, and when that fails the update does not start.
    const bool update = dialog.isUpdate();
    const QString updateId = update ? dialog.updateInstanceId() : QString();
    QString snapshotZip;
    if (update) {
        const auto version = dialog.selectedVersion();
        if (!saveCopyBeforeModpackUpdate(updateId, version ? version->name : QString(), &snapshotZip))
            return;
    }

    InstanceTask* creationTask = dialog.extractTask();
    if (!creationTask) {
        if (!snapshotZip.isEmpty())
            Zero::Backup::Pack::removeSnapshot(snapshotZip, Zero::Backup::Pack::snapshotsDirFor(APPLICATION->dataRoot(), updateId));
        return;
    }
    const QString requestedName = dialog.instName();

    // The instance the task commits is announced through instanceSelectRequest (a new one and an updated one alike).
    QString createdId;
    const auto connection = connect(APPLICATION->instances(), &InstanceList::instanceSelectRequest, this,
                                    [&createdId](const QString& id) { createdId = id; });
    unique_qobject_ptr<Task> task(APPLICATION->instances()->wrapInstanceTask(creationTask));
    runModalTask(task.get());
    disconnect(connection);

    // The copy stays after a success and after a failure (a failed update is when it is needed): only its outcome is recorded. When the
    // provider made a new instance instead of updating this one, the copy has nothing to bring back and goes.
    if (!snapshotZip.isEmpty()) {
        const QString snapshotsDir = Zero::Backup::Pack::snapshotsDirFor(APPLICATION->dataRoot(), updateId);
        if (task->wasSuccessful() && createdId != updateId) {
            Zero::Backup::Pack::removeSnapshot(snapshotZip, snapshotsDir);
            snapshotZip.clear();
        } else {
            Zero::Backup::Pack::markOutcome(
                snapshotZip, QString::fromLatin1(task->wasSuccessful() ? Zero::Backup::Pack::kOutcomeOk : Zero::Backup::Pack::kOutcomeFailed));
        }
        m_library->detailsPanel()->refreshRollback();
    }

    if (!task->wasSuccessful())
        return;
    QString name = requestedName;
    if (!createdId.isEmpty()) {
        if (auto* inst = APPLICATION->instances()->getInstanceById(createdId))
            name = inst->name();
    }
    const QString text = (update ? tr("Updated %1") : tr("Installed %1")).arg(name);
    if (createdId.isEmpty())
        Zero::ZeroToast::show(this, text, Zero::ZeroToast::Kind::Success);
    else if (update && !snapshotZip.isEmpty())  // one action only: Undo takes the place of "Show in Library" (the update selects the instance anyway)
        Zero::ZeroToast::show(this, text, Zero::ZeroToast::Kind::Success, tr("Undo"), [this, createdId]() { rollBackModpackUpdate(createdId); });
    else
        Zero::ZeroToast::show(this, text, Zero::ZeroToast::Kind::Success, tr("Show in Library"), [this, createdId]() { showInLibrary(createdId); });
}

namespace {
/// Why a modpack update / roll back of this instance cannot start now (empty = it can).
QString packBusyReason(const QString& instanceId)
{
    auto* inst = APPLICATION->instances()->getInstanceById(instanceId);
    if (APPLICATION->launchPhase(instanceId) != Application::LaunchPhase::Idle || (inst && inst->isRunning()))
        return QCoreApplication::translate("MainWindow", "Close the game first.");
    return {};
}

/// The question of the roll back (the same text in the real flow and in the screenshot aid).
Zero::ModsUi::ChoicePrompt rollbackPrompt(const QString& name, const Zero::Backup::Pack::Info& snapshot)
{
    Zero::ModsUi::ChoicePrompt ask;
    const QString back =
        snapshot.meta.oldVersion.isEmpty() ? QCoreApplication::translate("MainWindow", "the previous version") : snapshot.meta.oldVersion;
    ask.title = QCoreApplication::translate("MainWindow", "Roll back %1 to %2?").arg(name, back);
    ask.body = QCoreApplication::translate("MainWindow", "Your worlds, screenshots and play time stay. The updated files go to the Recycle Bin.");
    ask.buttons = { { QCoreApplication::translate("MainWindow", "Cancel"), Zero::ModsUi::ChoiceButton::Kind::Ghost },
                    { QCoreApplication::translate("MainWindow", "Roll back"), Zero::ModsUi::ChoiceButton::Kind::Primary } };
    ask.defaultButton = 0;  // Enter cancels
    return ask;
}
}  // namespace

// ZERO: the copy of the instance before a modpack update, under a modal progress. Cancelling it cancels the update. True = the copy exists
// (`zipPath`); false = nothing was changed and the reason was shown.
bool MainWindow::saveCopyBeforeModpackUpdate(const QString& instanceId, const QString& newVersion, QString* zipPath)
{
    auto* inst = APPLICATION->instances()->getInstanceById(instanceId);
    if (!inst) {
        Zero::ZeroToast::show(this, tr("That instance is no longer available."), Zero::ZeroToast::Kind::Error);
        return false;
    }
    const QString name = inst->name();
    if (!packBusyReason(instanceId).isEmpty()) {
        Zero::ZeroToast::show(this, tr("Close %1 to update it").arg(name), Zero::ZeroToast::Kind::Warning);
        return false;
    }
    Zero::Backup::Pack::Meta meta;
    meta.instanceId = inst->id();
    meta.instanceName = name;
    meta.packId = inst->getManagedPackID();
    meta.packName = inst->getManagedPackName().isEmpty() ? name : inst->getManagedPackName();
    meta.oldVersion = inst->getManagedPackVersionName();
    meta.newVersion = newVersion;
    meta.gameDir = Zero::Backup::Pack::relativeGameDir(inst->instanceRoot(), inst->gameRoot());

    unique_qobject_ptr<Zero::Backup::PackSnapshotTask> task(
        new Zero::Backup::PackSnapshotTask(inst->instanceRoot(), Zero::Backup::Pack::snapshotsDirFor(APPLICATION->dataRoot(), instanceId), meta, {},
                                           [instanceId]() { return packBusyReason(instanceId); }));
    ProgressDialog dialog(this);
    dialog.showSkipButton(tr("Cancel"));
    dialog.execWithTask(task.get());
    if (task->wasSuccessful()) {
        *zipPath = task->zipPath();
        return true;
    }
    if (task->getState() == Task::State::AbortedByUser)
        Zero::ZeroToast::show(this, tr("Update cancelled. %1 was not changed.").arg(name), Zero::ZeroToast::Kind::Info);
    else
        Zero::ZeroToast::show(this, tr("Could not save a copy of %1 first, so it was not updated: %2").arg(name, task->failReason()),
                              Zero::ZeroToast::Kind::Error);
    return false;
}

// ZERO: roll back the last modpack update of an instance (button in the Library details, "Undo" of the update toast). The question comes
// first; the work is PackRollbackTask (verify the copy, swap the instance folder, the replaced one to the Recycle Bin).
void MainWindow::rollBackModpackUpdate(const QString& instanceId)
{
    auto* inst = APPLICATION->instances()->getInstanceById(instanceId);
    if (!inst) {
        Zero::ZeroToast::show(this, tr("That instance is no longer available."), Zero::ZeroToast::Kind::Error);
        return;
    }
    const QString name = inst->name();
    const QString root = inst->instanceRoot();  // the question below runs an event loop: nothing of `inst` is used after it
    const QString dir = Zero::Backup::Pack::snapshotsDirFor(APPLICATION->dataRoot(), instanceId);
    const QString packId = inst->getManagedPackID();
    const Zero::Backup::Pack::Info snapshot = Zero::Backup::Pack::newestActive(Zero::Backup::Pack::listSnapshots(dir), packId);
    if (!snapshot.valid) {
        m_library->detailsPanel()->refreshRollback();
        Zero::ZeroToast::show(this, tr("There is no update of %1 to roll back.").arg(name), Zero::ZeroToast::Kind::Info);
        return;
    }
    if (!packBusyReason(instanceId).isEmpty()) {
        Zero::ZeroToast::show(this, tr("Close %1 to roll it back").arg(name), Zero::ZeroToast::Kind::Warning);
        return;
    }
    if (Zero::ModsUi::askWithDialog(this, rollbackPrompt(name, snapshot)) != 1)
        return;
    // the question ran an event loop: the instance may have started meanwhile
    if (!packBusyReason(instanceId).isEmpty()) {
        Zero::ZeroToast::show(this, tr("Close %1 to roll it back").arg(name), Zero::ZeroToast::Kind::Warning);
        return;
    }

    unique_qobject_ptr<Zero::Backup::PackRollbackTask> task(new Zero::Backup::PackRollbackTask(
        root, snapshot.zipPath, dir, packId, {}, [instanceId]() { return packBusyReason(instanceId); }));
    ProgressDialog dialog(this);
    dialog.execWithTask(task.get());
    if (!task->wasSuccessful()) {
        Zero::ZeroToast::show(this, tr("Could not roll back %1: %2").arg(name, task->failReason()), Zero::ZeroToast::Kind::Error);
        m_library->detailsPanel()->refreshRollback();
        return;
    }
    // the folder was replaced on disk: the instance in memory is stale (settings, components, mod lists)
    APPLICATION->instances()->reloadInstance(instanceId);
    m_library->detailsPanel()->refreshRollback();
    const QString leftover = task->result().leftoverPath;
    if (leftover.isEmpty())
        Zero::ZeroToast::show(this, tr("Rolled back %1").arg(name), Zero::ZeroToast::Kind::Success);
    else
        Zero::ZeroToast::show(this,
                              tr("Rolled back %1. The updated files could not go to the Recycle Bin, they are in %2")
                                  .arg(name, QDir::toNativeSeparators(leftover)),
                              Zero::ZeroToast::Kind::Warning);
}

// ZERO: Add / Update of a Discover mod (docs/zero/CONTENT-MODS.md, "Mod install"). The review dialog decides what to install (the mod and
// its dependencies); this builds one ResourceDownloadTask per mod, exactly as the classic ResourceDownloadDialog does (isIndexed =
// packwiz metadata, the reason and the file id of the mod that requires it), and runs them like ModFolderPage::downloadDialogFinished:
// one ConcurrentTask under a ProgressDialog, Prism's error and warning boxes (non-blocking), then the mods folder is read again.
// ResourceDownloadDialog / ResourceUpdateDialog (their modal boxes) are never used.
void MainWindow::installMod(const std::shared_ptr<Zero::Content::ModpackItem>& item, const QString& targetInstanceId, const QString& installedFileId)
{
    using namespace Zero::Content;
    if (!item || !item->pack)
        return;
    auto* inst = APPLICATION->instances()->getInstanceById(targetInstanceId);
    if (!inst) {
        Zero::ZeroToast::show(this, tr("That instance is no longer available."), Zero::ZeroToast::Kind::Error);
        return;
    }
    const TargetInstance target = targetFromInstance(inst);
    if (target.running) {
        Zero::ZeroToast::show(this, tr("Close %1 to add mods").arg(target.name), Zero::ZeroToast::Kind::Warning);
        return;
    }

    auto* section = m_discover->mods();
    const auto env = ModInstallEnvironment::fromApplication(section->source(item->provider), [section]() { return section->installedMods(); });
    ModInstallDialog dialog(env, item, target, installedFileId, this);
    if (m_zeroModInstallHook)
        m_zeroModInstallHook(&dialog);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const ModInstallPlan plan = dialog.plan();
    const bool update = dialog.isUpdate();
    if (plan.isEmpty())
        return;
    // The instance may have started or been removed while the dialog was open.
    inst = APPLICATION->instances()->getInstanceById(targetInstanceId);
    if (!inst || inst->isRunning()) {
        Zero::ZeroToast::show(this, inst ? tr("Close %1 to add mods").arg(inst->name()) : tr("That instance is no longer available."),
                              inst ? Zero::ZeroToast::Kind::Warning : Zero::ZeroToast::Kind::Error);
        return;
    }
    ModFolderModel* model = inst->loaderModList();
    const bool indexed = !APPLICATION->settings()->get("ModMetadataDisabled").toBool();

    ConcurrentTask tasks(tr("Add mods"), APPLICATION->settings()->get("NumberOfConcurrentDownloads").toInt());
    for (const auto& mod : plan.mods)
        tasks.addTask(makeShared<ResourceDownloadTask>(mod.pack, mod.version, model, indexed, mod.reason, mod.dependentOn));
    connect(&tasks, &Task::failed, this,
            [this](const QString& reason) { CustomMessageBox::selectable(this, tr("Error"), reason, QMessageBox::Critical)->show(); });
    connect(&tasks, &Task::succeeded, this, [this, &tasks]() {
        QStringList warnings = tasks.warnings();
        if (warnings.count())
            CustomMessageBox::selectable(this, tr("Warnings"), warnings.join('\n'), QMessageBox::Warning)->show();
    });
    ProgressDialog loadDialog(this);
    loadDialog.showSkipButton();
    loadDialog.execWithTask(&tasks);

    // Whatever arrived (also after a failure or an abort) is read again: the Mods section's Installed / Update states follow
    // the model's updateFinished.
    inst = APPLICATION->instances()->getInstanceById(targetInstanceId);
    if (!inst)
        return;
    inst->loaderModList()->update();
    if (!tasks.wasSuccessful())
        return;

    const QString name = item->name;
    const QString text = update ? updatedToastText(name, inst->name(), plan.dependencyCount()) : addedToastText(name, inst->name(), plan.dependencyCount());
    Zero::ZeroToast::show(this, text, Zero::ZeroToast::Kind::Success, tr("Show mods"), [targetInstanceId]() {
        if (auto* shown = APPLICATION->instances()->getInstanceById(targetInstanceId))
            APPLICATION->showInstanceWindow(shown, QStringLiteral("mods"));
    });
}

// ZERO: Add / Update of a Discover resource pack or shader (docs/zero/CONTENT-PACKS.md, "D1 Discover > Resource Packs"; SHELL.md, "Discover >
// Shaders"). Like installMod, but the review dialog is the pack mode (version only, no dependencies) and the one ResourceDownloadTask writes
// into the instance's resourcePackList() / shaderPackList() (packwiz metadata unless ModMetadataDisabled). "Turn it on" of the dialog
// (resource packs only, 1.13+) puts `file/<file name>` on top of `resourcePacks` in options.txt through Packs::turnOnAtTop (the page's own
// write: one line, atomic, with its history for Undo; refused while the instance runs). Shaders never write the game's settings.
void MainWindow::installResourcePack(const std::shared_ptr<Zero::Content::ModpackItem>& item, const QString& targetInstanceId,
                                     const QString& installedFileId)
{
    installFolderContent(Zero::Content::Kind::ResourcePack, item, targetInstanceId, installedFileId);
}

void MainWindow::installShaderPack(const std::shared_ptr<Zero::Content::ModpackItem>& item, const QString& targetInstanceId,
                                   const QString& installedFileId)
{
    installFolderContent(Zero::Content::Kind::Shader, item, targetInstanceId, installedFileId);
}

void MainWindow::installFolderContent(Zero::Content::Kind kind, const std::shared_ptr<Zero::Content::ModpackItem>& item,
                                      const QString& targetInstanceId, const QString& installedFileId)
{
    using namespace Zero::Content;
    if (!item || !item->pack)
        return;
    const bool shaders = kind == Kind::Shader;
    auto* inst = APPLICATION->instances()->getInstanceById(targetInstanceId);
    if (!inst) {
        Zero::ZeroToast::show(this, tr("That instance is no longer available."), Zero::ZeroToast::Kind::Error);
        return;
    }
    const TargetInstance target = targetFromInstance(inst);
    const auto closeText = [shaders](const QString& name) { return shaders ? tr("Close %1 to add shaders").arg(name) : tr("Close %1 to add packs").arg(name); };
    if (target.running) {
        Zero::ZeroToast::show(this, closeText(target.name), Zero::ZeroToast::Kind::Warning);
        return;
    }

    ModsSection* section = shaders ? static_cast<ModsSection*>(m_discover->shaders()) : static_cast<ModsSection*>(m_discover->packs());
    const auto env = ModInstallEnvironment::fromApplication(section->source(item->provider), [section]() { return section->installedMods(); }, true, shaders);
    ModInstallDialog dialog(env, item, target, installedFileId, this);
    if (m_zeroModInstallHook)
        m_zeroModInstallHook(&dialog);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const ModInstallPlan plan = dialog.plan();
    const bool update = dialog.isUpdate();
    if (plan.isEmpty())
        return;
    // The instance may have started or been removed while the dialog was open.
    inst = APPLICATION->instances()->getInstanceById(targetInstanceId);
    if (!inst || inst->isRunning()) {
        Zero::ZeroToast::show(this, inst ? closeText(inst->name()) : tr("That instance is no longer available."),
                              inst ? Zero::ZeroToast::Kind::Warning : Zero::ZeroToast::Kind::Error);
        return;
    }
    ResourceFolderModel* model = shaders ? static_cast<ResourceFolderModel*>(inst->shaderPackList()) : static_cast<ResourceFolderModel*>(inst->resourcePackList());
    const bool indexed = !APPLICATION->settings()->get("ModMetadataDisabled").toBool();

    const bool turnOn = !shaders && dialog.turnItOn() && !update;
    QString addedFile;
    ConcurrentTask tasks(shaders ? tr("Add shaders") : tr("Add resource packs"), APPLICATION->settings()->get("NumberOfConcurrentDownloads").toInt());
    for (const auto& pack : plan.mods) {
        auto download = makeShared<ResourceDownloadTask>(pack.pack, pack.version, model, indexed, pack.reason, pack.dependentOn);
        if (addedFile.isEmpty())
            addedFile = download->getFilename();
        tasks.addTask(download);
    }
    connect(&tasks, &Task::failed, this,
            [this](const QString& reason) { CustomMessageBox::selectable(this, tr("Error"), reason, QMessageBox::Critical)->show(); });
    connect(&tasks, &Task::succeeded, this, [this, &tasks]() {
        QStringList warnings = tasks.warnings();
        if (warnings.count())
            CustomMessageBox::selectable(this, tr("Warnings"), warnings.join('\n'), QMessageBox::Warning)->show();
    });
    ProgressDialog loadDialog(this);
    loadDialog.showSkipButton();
    loadDialog.execWithTask(&tasks);

    // Whatever arrived (also after a failure or an abort) is read again: the section's Installed / Update states follow the model.
    inst = APPLICATION->instances()->getInstanceById(targetInstanceId);
    if (!inst)
        return;
    if (shaders)
        inst->shaderPackList()->update();
    else
        inst->resourcePackList()->update();
    if (!tasks.wasSuccessful())
        return;

    QString text = update ? updatedToastText(item->name, inst->name(), 0) : addedToastText(item->name, inst->name(), 0);
    if (turnOn && !addedFile.isEmpty()) {
        // The instance may have started during the download: the write is refused then and the pack is just added.
        MinecraftInstance* started = inst;
        const auto turned = Zero::Content::Packs::turnOnAtTop(Zero::Content::Packs::optionsPathFor(inst->gameRoot()),
                                                              Zero::Content::Packs::historyPathFor(inst->instanceRoot()),
                                                              model->dir().absolutePath(), addedFile,
                                                              [started]() { return started && started->isRunning(); });
        if (turned.result == Zero::Content::Packs::Result::Ok || turned.result == Zero::Content::Packs::Result::NoChange)
            text = Zero::Content::Packs::addedAndOnText(item->name, inst->name());
    }
    const QString page = shaders ? QStringLiteral("shaderpacks") : QStringLiteral("resourcepacks");
    Zero::ZeroToast::show(this, text, Zero::ZeroToast::Kind::Success, shaders ? tr("Show shaders") : tr("Show packs"), [targetInstanceId, page]() {
        if (auto* shown = APPLICATION->instances()->getInstanceById(targetInstanceId))
            APPLICATION->showInstanceWindow(shown, page);
    });
}

// ZERO: the Add of a Discover world (docs/zero/SHELL.md, "Discover > Worlds - as built"). Worlds are not a ResourceFolderModel (no packwiz
// metadata, no ResourceDownloadTask): one SequentialTask = a NetJob that downloads the zip into <instance>/.zero/world-download/ (CurseForge's
// hash checked when it sent one) + Prism's InstallWorldTask (`worldList()->createInstallWorldTask`: the folder with level.dat is extracted
// into saves/<unique name>). The zip is deleted afterwards whatever the outcome (installed, failed, skipped).
void MainWindow::installWorld(const std::shared_ptr<Zero::Content::ModpackItem>& item, const QString& targetInstanceId)
{
    using namespace Zero::Content;
    if (!item || !item->pack || !m_discover->worlds())
        return;
    auto* inst = APPLICATION->instances()->getInstanceById(targetInstanceId);
    if (!inst) {
        Zero::ZeroToast::show(this, tr("That instance is no longer available."), Zero::ZeroToast::Kind::Error);
        return;
    }
    const TargetInstance target = targetFromInstance(inst);
    if (target.running) {
        Zero::ZeroToast::show(this, tr("Close %1 to add worlds").arg(target.name), Zero::ZeroToast::Kind::Warning);
        return;
    }

    ModsSection* section = m_discover->worlds();
    const auto env = ModInstallEnvironment::fromApplication(
        section->source(item->provider), []() { return QList<InstalledMod>(); }, true, false, true);
    ModInstallDialog dialog(env, item, target, QString(), this);
    if (m_zeroModInstallHook)
        m_zeroModInstallHook(&dialog);
    if (dialog.exec() != QDialog::Accepted)
        return;  // also "Open on CurseForge": the dialog opened the page and closed itself, nothing is added
    const auto version = dialog.selectedVersion();
    if (!version || !worldFileDownloadable(*version))
        return;

    // The instance may have started or been removed while the dialog was open.
    inst = APPLICATION->instances()->getInstanceById(targetInstanceId);
    if (!inst || inst->isRunning()) {
        Zero::ZeroToast::show(this, inst ? tr("Close %1 to add worlds").arg(inst->name()) : tr("That instance is no longer available."),
                              inst ? Zero::ZeroToast::Kind::Warning : Zero::ZeroToast::Kind::Error);
        return;
    }
    const QString instanceRoot = inst->instanceRoot();
    const QString zipPath = worldDownloadPath(instanceRoot, version->fileId.toString(), version->fileName);
    if (!FS::ensureFolderPathExists(QFileInfo(zipPath).absolutePath())) {
        Zero::ZeroToast::show(this, tr("Could not prepare the download folder of %1.").arg(inst->name()), Zero::ZeroToast::Kind::Error);
        return;
    }
    const QStringList before = QDir(inst->worldDir()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    SequentialTask tasks(tr("Add world"));
    NetJob::Ptr download{ new NetJob(tr("World download"), APPLICATION->network()) };
    download->setStatus(tr("Downloading %1").arg(item->name));
    auto action = Net::ApiRequest::makeFile(QUrl(version->downloadUrl), zipPath, Net::Request::Option::NoOptions);
    if (!version->hashType.isEmpty() && !version->hash.isEmpty()) {
        switch (Hashing::algorithmFromString(version->hashType)) {
            case Hashing::Algorithm::Md4:
                action->addValidator(new Net::ChecksumValidator(QCryptographicHash::Algorithm::Md4, version->hash));
                break;
            case Hashing::Algorithm::Md5:
                action->addValidator(new Net::ChecksumValidator(QCryptographicHash::Algorithm::Md5, version->hash));
                break;
            case Hashing::Algorithm::Sha1:
                action->addValidator(new Net::ChecksumValidator(QCryptographicHash::Algorithm::Sha1, version->hash));
                break;
            case Hashing::Algorithm::Sha256:
                action->addValidator(new Net::ChecksumValidator(QCryptographicHash::Algorithm::Sha256, version->hash));
                break;
            case Hashing::Algorithm::Sha512:
                action->addValidator(new Net::ChecksumValidator(QCryptographicHash::Algorithm::Sha512, version->hash));
                break;
            default:
                break;
        }
    }
    download->addNetAction(action);
    tasks.addTask(download);
    Task::Ptr unpack{ inst->worldList()->createInstallWorldTask(QFileInfo(zipPath)).release() };
    tasks.addTask(unpack);
    connect(&tasks, &Task::failed, this,
            [this](const QString& reason) { CustomMessageBox::selectable(this, tr("Error"), reason, QMessageBox::Critical)->show(); });
    ProgressDialog loadDialog(this);
    loadDialog.showSkipButton();
    loadDialog.execWithTask(&tasks);

    // Whatever happened, the zip is not kept.
    const bool added = tasks.wasSuccessful();
    removeWorldDownload(zipPath, instanceRoot);
    inst = APPLICATION->instances()->getInstanceById(targetInstanceId);
    if (!inst)
        return;
    inst->worldList()->update();
    if (!added)
        return;

    const QStringList after = QDir(inst->worldDir()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    const QString folder = newWorldFolder(before, after);
    const QString key = folder.isEmpty() ? QString() : targetInstanceId + QLatin1Char('/') + folder;
    Zero::ZeroToast::show(this, addedToastText(item->name, inst->name(), 0), Zero::ZeroToast::Kind::Success, tr("Show worlds"),
                          [this, targetInstanceId, key]() {
                              if (!key.isEmpty() && m_worlds) {
                                  // The ZERO Worlds page, with the new world selected once it is listed.
                                  m_worlds->rescan();
                                  m_worlds->selectWorld(key);
                                  m_shell->setCurrentPage(QStringLiteral("worlds"));
                              } else if (auto* shown = APPLICATION->instances()->getInstanceById(targetInstanceId)) {
                                  APPLICATION->showInstanceWindow(shown, QStringLiteral("worlds"));
                              }
                          });
}

// ZERO: the download half of installCart(), shared with "Boost performance": one ResourceDownloadTask per download of `plan` (same arguments as
// installMod / installFolderContent, into loaderModList() / resourcePackList() / shaderPackList()), run as ONE ConcurrentTask under a
// ProgressDialog, then the touched folders are read again. Returns what arrived (`done`: the downloads whose task succeeded, a failed or
// never-run one is not in it) and the file names of the resource packs to turn on. Error and warning boxes are Prism's, as before.
MainWindow::PlanRun MainWindow::runInstallPlan(const QString& targetInstanceId, const Zero::Content::CartInstallPlan& plan)
{
    using namespace Zero::Content;
    PlanRun run;
    auto* inst = APPLICATION->instances()->getInstanceById(targetInstanceId);
    if (!inst) {
        run.instanceGone = true;
        return run;
    }
    const bool indexed = !APPLICATION->settings()->get("ModMetadataDisabled").toBool();
    ResourceFolderModel* mods = inst->loaderModList();
    ResourceFolderModel* packs = inst->resourcePackList();
    ResourceFolderModel* shaders = inst->shaderPackList();

    struct Started {
        CartDownload download;
        shared_qobject_ptr<ResourceDownloadTask> task;
    };
    QList<Started> started;
    ConcurrentTask tasks(tr("Add content"), APPLICATION->settings()->get("NumberOfConcurrentDownloads").toInt());
    for (const CartDownload& download : plan.downloads) {
        ResourceFolderModel* model = download.kind == Kind::Mod ? mods : download.kind == Kind::ResourcePack ? packs : shaders;
        auto task = makeShared<ResourceDownloadTask>(download.mod.pack, download.mod.version, model, indexed, download.mod.reason,
                                                     download.mod.dependentOn);
        started.append({ download, task });
        tasks.addTask(task);
    }
    connect(&tasks, &Task::failed, this,
            [this](const QString& reason) { CustomMessageBox::selectable(this, tr("Error"), reason, QMessageBox::Critical)->show(); });
    connect(&tasks, &Task::succeeded, this, [this, &tasks]() {
        QStringList warnings = tasks.warnings();
        if (warnings.count())
            CustomMessageBox::selectable(this, tr("Warnings"), warnings.join('\n'), QMessageBox::Warning)->show();
    });
    ProgressDialog loadDialog(this);
    loadDialog.showSkipButton();
    loadDialog.execWithTask(&tasks);

    // Whatever arrived (also after a failure or an abort) is read again: the sections' Installed / Update states follow the models.
    inst = APPLICATION->instances()->getInstanceById(targetInstanceId);
    if (!inst) {
        run.instanceGone = true;
        return run;
    }
    if (plan.countOf(Kind::Mod) > 0)
        inst->loaderModList()->update();
    if (plan.countOf(Kind::ResourcePack) > 0)
        inst->resourcePackList()->update();
    if (plan.countOf(Kind::Shader) > 0)
        inst->shaderPackList()->update();

    for (const Started& one : started) {
        if (!one.task->wasSuccessful())
            continue;
        run.done.downloads.append(one.download);
        if (one.download.turnOn)
            run.packFiles.append(one.task->getFilename());
    }
    return run;
}

// ZERO: the combined install of Discover's install cart (docs/zero/SHELL.md, "Discover > Install cart"). The review dialog decides WHAT to
// install (every cart entry with its version, plus the dependencies of all the mods); this builds one ResourceDownloadTask per download
// exactly like installMod / installFolderContent (same arguments, into loaderModList() / resourcePackList() / shaderPackList()), runs them as
// ONE ConcurrentTask under a ProgressDialog, reads the touched folders again, puts the new resource packs on top of the game's list with ONE
// options.txt write (Packs::turnOnManyAtTop: one history entry, Undo works) and toasts what arrived. A download that succeeded takes its
// entry out of the cart; one that failed (or never ran) stays, and so does an entry the review skipped. The single-item flows are untouched.
void MainWindow::installCart()
{
    using namespace Zero::Content;
    auto* cart = m_discover->cart();
    auto* targetSection = m_discover->cartTargetSection();
    if (!cart || cart->isEmpty())
        return;
    const QString targetInstanceId = targetSection ? targetSection->target().id : QString();
    auto* inst = APPLICATION->instances()->getInstanceById(targetInstanceId);
    if (!inst) {
        Zero::ZeroToast::show(this, tr("That instance is no longer available."), Zero::ZeroToast::Kind::Error);
        return;
    }
    const TargetInstance target = targetFromInstance(inst);
    if (target.running) {
        Zero::ZeroToast::show(this, cartCloseText(target.name), Zero::ZeroToast::Kind::Warning);
        return;
    }

    const auto sectionOf = [this](Kind kind) -> ModsSection* {
        return kind == Kind::Mod ? static_cast<ModsSection*>(m_discover->mods())
               : kind == Kind::ResourcePack ? static_cast<ModsSection*>(m_discover->packs())
                                            : static_cast<ModsSection*>(m_discover->shaders());
    };
    // What the review reads about the target comes from the sections' installed lists (the same data as the cards' "Installed" and
    // "Update" states); a section that looks at another instance knows nothing about this target.
    const auto env = CartReviewEnvironment::fromApplication(
        [sectionOf](const CartEntry& entry) -> ModpackSource* {
            auto* section = sectionOf(entry.kind);
            return section ? section->source(entry.provider) : nullptr;
        },
        [this, targetInstanceId]() {
            auto* section = m_discover->mods();
            return section && section->target().id == targetInstanceId ? section->installedMods() : QList<InstalledMod>();
        },
        [sectionOf, targetInstanceId](const CartEntry& entry) -> QString {
            auto* section = sectionOf(entry.kind);
            if (!section || section->target().id != targetInstanceId || !entry.item)
                return {};
            const auto installed = section->installedFor(*entry.item);
            return installed ? installed->fileId : QString();
        });
    CartReviewDialog dialog(env, cart, target, this);
    if (m_zeroCartHook)
        m_zeroCartHook(&dialog);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const CartInstallPlan plan = dialog.plan();
    if (plan.isEmpty())
        return;
    // The instance may have started or been removed while the dialog was open.
    inst = APPLICATION->instances()->getInstanceById(targetInstanceId);
    if (!inst || inst->isRunning()) {
        Zero::ZeroToast::show(this, inst ? cartCloseText(inst->name()) : tr("That instance is no longer available."),
                              inst ? Zero::ZeroToast::Kind::Warning : Zero::ZeroToast::Kind::Error);
        return;
    }
    // What succeeded leaves the cart; what failed stays (the error box above says why).
    const PlanRun run = runInstallPlan(targetInstanceId, plan);
    if (run.instanceGone)
        return;
    inst = APPLICATION->instances()->getInstanceById(targetInstanceId);
    const CartInstallPlan& done = run.done;
    const QStringList& packFiles = run.packFiles;
    for (const CartDownload& one : done.downloads) {
        if (!one.cartKey.isEmpty())
            cart->remove(one.cartKey);
    }
    if (done.isEmpty())
        return;

    if (!packFiles.isEmpty()) {
        // The instance may have started during the download: the write is refused then and the packs are just added.
        MinecraftInstance* watched = inst;
        Zero::Content::Packs::turnOnManyAtTop(Zero::Content::Packs::optionsPathFor(inst->gameRoot()),
                                              Zero::Content::Packs::historyPathFor(inst->instanceRoot()),
                                              inst->resourcePackList()->dir().absolutePath(), packFiles,
                                              [watched]() { return watched && watched->isRunning(); });
    }
    const QString text = cartToastText(done.countOf(Kind::Mod), done.countOf(Kind::ResourcePack), done.countOf(Kind::Shader), inst->name(),
                                       done.allUpdates());
    const QString page = done.firstPageId();
    Zero::ZeroToast::show(this, text, Zero::ZeroToast::Kind::Success, tr("Show"), [targetInstanceId, page]() {
        if (auto* shown = APPLICATION->instances()->getInstanceById(targetInstanceId))
            APPLICATION->showInstanceWindow(shown, page);
    });
}

// ZERO: "Boost performance" (docs/zero/SHELL.md). The dialog decides WHAT to change (the curated mods for the instance's version + loader, the
// dependencies they need, the memory); its Apply calls the install below, which is the cart's download half (runInstallPlan: one task, one
// ProgressDialog, Prism's own error boxes), and then writes the memory settings. Refused while the instance runs. "Fix" of the Java line closes
// the dialog and opens Tools > Java Doctor.
void MainWindow::boostPerformance(const QString& instanceId)
{
    using namespace Zero::Perf;
    auto* inst = APPLICATION->instances()->getInstanceById(instanceId);
    if (!inst) {
        Zero::ZeroToast::show(this, tr("That instance is no longer available."), Zero::ZeroToast::Kind::Error);
        return;
    }
    if (inst->isRunning()) {
        Zero::ZeroToast::show(this, closeFirstText(inst->name()), Zero::ZeroToast::Kind::Warning);
        return;
    }
    const QString name = inst->name();
    auto env = PerformanceEnvironment::fromApplication(instanceId, [this, instanceId](const Zero::Content::CartInstallPlan& plan) {
        InstallResult result;
        const PlanRun run = runInstallPlan(instanceId, plan);
        result.installed = run.done.count();
        result.failedAsWhole = run.instanceGone || run.done.isEmpty();  // nothing arrived (a failure, or the user skipped everything)
        return result;
    });
    PerformanceDialog dialog(std::move(env), this);
    if (m_zeroBoostHook)
        m_zeroBoostHook(&dialog);
    dialog.exec();

    const PerformanceDialog::Outcome outcome = dialog.outcome();
    if (outcome.refusedRunning) {
        Zero::ZeroToast::show(this, closeFirstText(name), Zero::ZeroToast::Kind::Warning);
        return;
    }
    if (dialog.javaFixRequested()) {
        m_tools->setPreferredInstance(instanceId);
        m_tools->openJavaDoctor();
        m_shell->setCurrentPage(QStringLiteral("tools"));
        return;
    }
    if (!outcome.applied)
        return;
    if (outcome.modsAdded > 0) {
        Zero::ZeroToast::show(this, boostedToastText(name), Zero::ZeroToast::Kind::Success, tr("Show mods"), [instanceId]() {
            if (auto* shown = APPLICATION->instances()->getInstanceById(instanceId))
                APPLICATION->showInstanceWindow(shown, QStringLiteral("mods"));
        });
    } else {
        Zero::ZeroToast::show(this, boostedToastText(name), Zero::ZeroToast::Kind::Success);
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
    APPLICATION->OpenSettings(this);
}

bool MainWindow::showSettingsPage(const QString& pageId)
{
    if (!m_settingsPage || !m_shell)
        return false;
    m_settingsPage->selectSection(pageId);
    m_shell->setCurrentPage(QStringLiteral("settings"));
    setWindowState(windowState() & ~Qt::WindowMinimized);
    raise();
    activateWindow();
    return true;
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
    APPLICATION->OpenSettings(this, "accounts");
}

void MainWindow::addAccountZero()
{
    // ZERO: the dialog returns the account; adding it (and making it the default when it is the first) is ours
    const MinecraftAccountPtr account = Zero::Accounts::AddAccountDialog::run(this);
    if (!account)
        return;
    auto* accounts = APPLICATION->accounts();
    accounts->addAccount(account);
    if (accounts->count() == 1)
        accounts->setDefaultAccount(account);
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

void MainWindow::exportInstanceZero(const QString& instanceId)
{
    auto* inst = APPLICATION->instances()->getInstanceById(instanceId);
    if (!inst || !inst->canExport())
        return;
    bool snapshot = false;
    if (auto cmp = inst->getPackProfile()->getComponent("net.minecraft"); cmp && cmp->getVersionFile() && cmp->getVersionFile()->type == "snapshot")
        snapshot = true;
    const auto remembered = Zero::exportFormatFromKey(APPLICATION->settings()->get("ZeroExportFormat").toString());
    Zero::ExportChooserDialog chooser(inst->name(), snapshot, remembered, this);
    if (m_zeroExportHook)
        m_zeroExportHook(&chooser);
    if (chooser.exec() != QDialog::Accepted)
        return;
    const auto format = chooser.selected();
    APPLICATION->settings()->set("ZeroExportFormat", Zero::exportFormatKey(format));
    // Prism's own dialogs stay the second step (file tree, names, save path).
    switch (format) {
        case Zero::ExportFormat::Zip: {
            ExportInstanceDialog dlg(inst, this);
            dlg.exec();
            break;
        }
        case Zero::ExportFormat::MrPack: {
            ExportPackDialog dlg(inst, this);
            dlg.exec();
            break;
        }
        case Zero::ExportFormat::CurseForge: {
            ExportPackDialog dlg(inst, this, ModPlatform::ResourceProvider::FLAME);
            dlg.exec();
            break;
        }
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
    if (m_settingsPage)
        m_settingsPage->applyAll();  // Settings saves when you leave it; quitting while it is open counts as leaving
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
    m_discover = new Zero::DiscoverPage(m_shell);  // Alt+3: later pages shift by one
    connect(m_discover, &Zero::DiscoverPage::installRequested, this,
            [this](const Zero::Content::ModpackItemPtr& item, const QString& updateInstanceId) { installModpack(item, updateInstanceId); });
    // Mods: the target defaults to the Library's selected instance; "Create instance" of the empty state opens Create Instance on Modded.
    connect(m_discover, &Zero::DiscoverPage::modInstallRequested, this,
            [this](const Zero::Content::ContentItemPtr& item, const QString& targetInstanceId, const QString& installedFileId) {
                installMod(item, targetInstanceId, installedFileId);
            });
    // Resource Packs: the same target rule (every instance with a Minecraft version qualifies); "Create instance" opens Vanilla.
    connect(m_discover, &Zero::DiscoverPage::packInstallRequested, this,
            [this](const Zero::Content::ContentItemPtr& item, const QString& targetInstanceId, const QString& installedFileId) {
                installResourcePack(item, targetInstanceId, installedFileId);
            });
    // Shaders: the same rule; "Find Iris" / "Find Oculus" of the notice opens Discover > Mods for the same instance.
    connect(m_discover, &Zero::DiscoverPage::shaderInstallRequested, this,
            [this](const Zero::Content::ContentItemPtr& item, const QString& targetInstanceId, const QString& installedFileId) {
                installShaderPack(item, targetInstanceId, installedFileId);
            });
    // Worlds (CurseForge, only with the capability): the dialog, the download, Prism's world install.
    connect(m_discover, &Zero::DiscoverPage::worldInstallRequested, this,
            [this](const Zero::Content::ContentItemPtr& item, const QString& targetInstanceId, const QString&) { installWorld(item, targetInstanceId); });
    // The install cart: "Review and add to <instance>" of the selection bar.
    connect(m_discover, &Zero::DiscoverPage::cartReviewRequested, this, [this]() { installCart(); });
    connect(m_discover, &Zero::DiscoverPage::findShaderModRequested, this,
            [this](const QString& targetInstanceId, const QString& query) { showDiscoverMods(targetInstanceId, query); });
    connect(m_discover, &Zero::DiscoverPage::createVanillaInstanceRequested, this, [this]() {
        m_zeroCreateHook = [](Zero::Create::CreateInstanceDialog* dialog) { dialog->setKind(Zero::Create::CreateInstanceDialog::Kind::Vanilla); };
        addInstance();
        m_zeroCreateHook = {};
    });
    m_discover->setLibrarySelection([this]() { return m_selectedInstance ? m_selectedInstance->id() : QString(); });
    connect(m_discover, &Zero::DiscoverPage::createInstanceRequested, this, [this]() {
        m_zeroCreateHook = [](Zero::Create::CreateInstanceDialog* dialog) { dialog->setKind(Zero::Create::CreateInstanceDialog::Kind::Modded); };
        addInstance();
        m_zeroCreateHook = {};
    });
    m_shell->addPage(QStringLiteral("discover"), QStringLiteral("sparkles"), tr("Discover"), m_discover);
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
    m_settingsPage = new Zero::ZeroSettingsPage(APPLICATION->globalSettingsPages(), m_shell);
    m_shell->setSettingsPage(m_settingsPage);
    m_shell->finishRail();
    // What each section is for, in the rail tooltips (the labels alone do not say it).
    {
        const QList<QPair<QString, QString>> descriptions = {
            { "home", tr("Pick up where you left off: your last instance, favorites and recent activity.") },
            { "library", tr("All your instances. Create, edit, tag, favorite and play them.") },
            { "discover", tr("Browse modpacks from Modrinth, CurseForge and Technic, with filters and details.") },
            { "worlds", tr("Every singleplayer world of every instance, with verified backups.") },
            { "screenshots", tr("One gallery with the screenshots of all your instances.") },
            { "servers", tr("The multiplayer servers saved in your instances, with their status.") },
            { "tools", tr("Fixers and checkers: crashes, disk space, Java and mods.") },
            { "appearance", tr("ZERO themes and animations.") },
            { "more", tr("Help, about, folders and less common actions.") },
            { "settings", tr("Everything else: folders, Java and memory, accounts, game window...") },
        };
        for (const auto& [id, text] : descriptions)
            m_shell->rail()->setItemDescription(id, text);
    }

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
    connect(m_library, &Zero::LibraryPage::exportRequested, this, &MainWindow::exportInstanceZero);
    connect(m_library, &Zero::LibraryPage::boostPerformanceRequested, this, [this]() {
        if (m_selectedInstance)
            boostPerformance(m_selectedInstance->id());
    });
    connect(m_library, &Zero::LibraryPage::rollbackRequested, this, [this]() {
        if (m_selectedInstance)
            rollBackModpackUpdate(m_selectedInstance->id());
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
            [this]() { QTimer::singleShot(0, this, [this]() { APPLICATION->OpenSettings(this, QStringLiteral("java-settings")); }); });

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
    // A running game pauses the video background (updateAllowedChanged(false) = first instance started, true = last one stopped).
    m_shell->setGameRunning(!APPLICATION->updatesAreAllowed());
    connect(APPLICATION, &Application::updateAllowedChanged, m_shell, [this](bool allowed) { m_shell->setGameRunning(!allowed); });
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
            [this]() { APPLICATION->OpenSettings(this, QStringLiteral("appearance-settings")); });

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

QString MainWindow::accountChipSubtitle(AccountType type)
{
    switch (type) {
        case AccountType::MSA:
            return tr("Microsoft account");
        case AccountType::Ely:
            return tr("Ely.by account");
        case AccountType::Offline:
            break;
    }
    return tr("Offline");
}

void MainWindow::updateAccountChip()
{
    if (!m_shell)
        return;
    const MinecraftAccountPtr account = APPLICATION->accounts()->defaultAccount();
    if (account && !account->profileName().isEmpty()) {
        m_shell->setAccount(account->displayName(), accountChipSubtitle(account->accountType()), account->getFace());
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
    if (value == QLatin1String("on") || value == QLatin1String("off") || value == QLatin1String("system"))
        return value;
    return settings->get("ZeroReduceMotion").toBool() ? QStringLiteral("off") : QStringLiteral("on");  // never chosen: on
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

void MainWindow::showDiscover(const QString& section, const QString& provider)
{
    // ZERO: the palette uses this now; Create Instance will use it to hand over to the full browser. `section`: "modpacks" | "mods" | "resourcepacks" | "shaders".
    m_discover->showSection(section, provider);
    m_shell->setCurrentPage(QStringLiteral("discover"));
}

void MainWindow::showDiscoverMods(const QString& instanceId, const QString& query)
{
    // ZERO: the Mods page of the instance window ("Add mods"): the mod browser for this instance.
    m_discover->showModsFor(instanceId, query);
    m_shell->setCurrentPage(QStringLiteral("discover"));
    setWindowState(windowState() & ~Qt::WindowMinimized);
    raise();
    activateWindow();
}

void MainWindow::showDiscoverPacks(const QString& instanceId, const QString& query)
{
    // ZERO: the packs page of the instance window ("Add packs"): the resource pack browser for this instance.
    m_discover->showPacksFor(instanceId, query);
    m_shell->setCurrentPage(QStringLiteral("discover"));
    setWindowState(windowState() & ~Qt::WindowMinimized);
    raise();
    activateWindow();
}

void MainWindow::showDiscoverShaders(const QString& instanceId, const QString& query)
{
    // ZERO: Discover > Shaders for this instance (the shaders page of the instance window, a palette command).
    m_discover->showShadersFor(instanceId, query);
    m_shell->setCurrentPage(QStringLiteral("discover"));
    setWindowState(windowState() & ~Qt::WindowMinimized);
    raise();
    activateWindow();
}

void MainWindow::showDiscoverWorlds(const QString& instanceId, const QString& query)
{
    // ZERO: Discover > Worlds for this instance (a palette command; only when the build has a CurseForge key).
    m_discover->showWorldsFor(instanceId, query);
    m_shell->setCurrentPage(QStringLiteral("discover"));
    setWindowState(windowState() & ~Qt::WindowMinimized);
    raise();
    activateWindow();
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

        // Boost performance of this instance (not while it runs: nothing in its folders may change then).
        if (!inst->isRunning()) {
            PaletteCommand boost;
            boost.id = QStringLiteral("action:boost-performance:") + id;
            boost.kind = Kind::Action;
            boost.title = tr("Boost performance of %1").arg(inst->name());
            boost.keywords = { tr("optimize"), tr("optimise"), tr("fps"),    tr("memory"), tr("ram"),
                               tr("sodium"),   tr("lithium"),  tr("speed"),  tr("faster"), tr("lag") };
            boost.iconName = QStringLiteral("gauge");
            boost.primaryLabel = tr("Boost");
            boost.run = [this, id]() { QTimer::singleShot(0, this, [this, id]() { boostPerformance(id); }); };
            out.append(boost);
        }
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
        { "discover", tr("Go to Discover"), "sparkles", { tr("browse"), tr("modpacks"), tr("download"), tr("content") } },
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
    if (m_shell->page(QStringLiteral("discover"))) {
        PaletteCommand c;
        c.id = QStringLiteral("action:browse-mods");
        c.kind = Kind::Action;
        c.title = tr("Browse mods");
        c.keywords = { tr("discover"), tr("download"), tr("install"), tr("modrinth"), tr("curseforge"), tr("content"), tr("fabric"), tr("forge") };
        c.iconName = QStringLiteral("sparkles");
        c.primaryLabel = tr("Browse");
        c.run = [this]() { showDiscover(QStringLiteral("mods")); };
        out.append(c);
    }
    if (m_shell->page(QStringLiteral("discover"))) {
        PaletteCommand c;
        c.id = QStringLiteral("action:browse-resourcepacks");
        c.kind = Kind::Action;
        c.title = tr("Browse resource packs");
        c.keywords = { tr("discover"), tr("download"), tr("install"), tr("modrinth"), tr("curseforge"), tr("content"), tr("textures"), tr("texture packs") };
        c.iconName = QStringLiteral("sparkles");
        c.primaryLabel = tr("Browse");
        c.run = [this]() { showDiscover(QStringLiteral("resourcepacks")); };
        out.append(c);
    }
    if (m_shell->page(QStringLiteral("discover"))) {
        PaletteCommand c;
        c.id = QStringLiteral("action:browse-shaders");
        c.kind = Kind::Action;
        c.title = tr("Browse shaders");
        c.keywords = { tr("discover"), tr("download"), tr("install"), tr("modrinth"), tr("curseforge"), tr("content"), tr("shader packs"), tr("iris"), tr("optifine") };
        c.iconName = QStringLiteral("sparkles");
        c.primaryLabel = tr("Browse");
        c.run = [this]() { showDiscover(QStringLiteral("shaders")); };
        out.append(c);
    }
    if (m_shell->page(QStringLiteral("discover")) && m_discover->worlds()) {  // only with a CurseForge key
        PaletteCommand c;
        c.id = QStringLiteral("action:browse-worlds");
        c.kind = Kind::Action;
        c.title = tr("Browse worlds");
        c.keywords = { tr("discover"), tr("download"), tr("install"), tr("curseforge"), tr("content"), tr("maps"), tr("saves"), tr("adventure"), tr("parkour") };
        c.iconName = QStringLiteral("sparkles");
        c.primaryLabel = tr("Browse");
        c.run = [this]() { showDiscover(QStringLiteral("worlds")); };
        out.append(c);
    }
    if (m_shell->page(QStringLiteral("discover"))) {
        PaletteCommand c;
        c.id = QStringLiteral("action:browse-modpacks");
        c.kind = Kind::Action;
        c.title = tr("Browse modpacks");
        c.keywords = { tr("discover"), tr("download"), tr("install"), tr("modrinth"), tr("curseforge"), tr("technic"), tr("content") };
        c.iconName = QStringLiteral("sparkles");
        c.primaryLabel = tr("Browse");
        c.run = [this]() { showDiscover(QStringLiteral("modpacks")); };
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
        c.run = [this, pageId]() { QTimer::singleShot(0, this, [this, pageId]() { APPLICATION->OpenSettings(this, pageId); }); };
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
                                                    { QStringLiteral("discover"), tr("Discover") },
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
    // ZERO: Dev aid ZERO_SCREENSHOT_INSTANCE_PAGE=<instance id>:<page id> grabs that page of the instance window instead (docs/zero/SHELL.md).
    if (const QString instancePage = qEnvironmentVariable("ZERO_SCREENSHOT_INSTANCE_PAGE"); !instancePage.isEmpty()) {
        zeroScreenshotInstance(path, instancePage);
        return;
    }
    const bool library = page == QLatin1String("library");
    m_shell->setCurrentPage(library                              ? QStringLiteral("library")
                            : page == QLatin1String("discover")   ? QStringLiteral("discover")  // ZERO
                            : page == QLatin1String("worlds")     ? QStringLiteral("worlds")  // ZERO
                            : page == QLatin1String("screenshots") ? QStringLiteral("screenshots")  // ZERO
                            : page == QLatin1String("servers")    ? QStringLiteral("servers")  // ZERO
                            : page == QLatin1String("tools")      ? QStringLiteral("tools")
                            : page == QLatin1String("appearance") ? QStringLiteral("appearance")
                            : page == QLatin1String("settings")   ? QStringLiteral("settings")  // ZERO
                                                                  : QStringLiteral("home"));
    // Dev aid: ZERO_SCREENSHOT_SETTINGS=<page id> selects that Settings section (with --zero-page settings).
    if (const QString section = qEnvironmentVariable("ZERO_SCREENSHOT_SETTINGS"); !section.isEmpty() && m_settingsPage)
        m_settingsPage->selectSection(section);
    // Dev aid: ZERO_SCREENSHOT_SELECT_THEME=<theme id> selects that card on the Appearance page (nothing is applied).
    if (const QString pick = qEnvironmentVariable("ZERO_SCREENSHOT_SELECT_THEME"); !pick.isEmpty())
        m_appearance->selectTheme(pick);
    // Dev aid: ZERO_SCREENSHOT_THEME_EDITOR=new|<custom theme id>[,minecraft][,hard][,all] opens the theme editor panel on the Appearance
    // page (",minecraft" starts from the Minecraft base, ",hard" picks a text color that is hard to read, ",all" expands All colors).
    if (const QString editor = qEnvironmentVariable("ZERO_SCREENSHOT_THEME_EDITOR"); !editor.isEmpty())
        m_appearance->openThemeEditor(editor);
    QSize size(1280, 800);
    // Dev aid: ZERO_SCREENSHOT_SIZE=<w>x<h> (e.g. 1100x700 to review the collapsed rail).
    const QStringList dims = qEnvironmentVariable("ZERO_SCREENSHOT_SIZE").split(QLatin1Char('x'));
    if (dims.size() == 2 && dims[0].toInt() > 0 && dims[1].toInt() > 0)
        size = QSize(dims[0].toInt(), dims[1].toInt());
    resize(size);
    // ZERO: Dev aid ZERO_SCREENSHOT_ADD_ACCOUNT=<choose|microsoft|ely|offline|error|success|ely-password|ely-2fa|ely-wrong> grabs the Add account dialog in that state instead.
    if (const QString addAccount = qEnvironmentVariable("ZERO_SCREENSHOT_ADD_ACCOUNT"); !addAccount.isEmpty()) {
        QTimer::singleShot(300, this, [this, path, addAccount]() {
            if (!Zero::Accounts::screenshotAddAccount(this, addAccount, path))
                APPLICATION->exit(1);
        });
        return;
    }
    // ZERO: Dev aid ZERO_SCREENSHOT_CREATE=<vanilla|modded|modpack|import> grabs the Create Instance dialog instead.
    if (const QString createKind = qEnvironmentVariable("ZERO_SCREENSHOT_CREATE"); !createKind.isEmpty()) {
        zeroScreenshotCreate(path, createKind);
        return;
    }
    // ZERO: Dev aid ZERO_SCREENSHOT_EXPORT=<instance id> opens the export chooser for that instance and saves it (the dialog alone) to <file>.
    if (const QString exportId = qEnvironmentVariable("ZERO_SCREENSHOT_EXPORT"); !exportId.isEmpty()) {
        m_zeroExportHook = [path](Zero::ExportChooserDialog* dialog) {
            QTimer::singleShot(500, dialog, [dialog, path]() {
                QApplication::processEvents();
                const bool ok = dialog->grab().save(path);
                qInfo() << "ZERO screenshot" << (ok ? "saved to" : "FAILED for") << path;
                dialog->reject();
            });
        };
        QTimer::singleShot(0, this, [this, exportId]() {
            exportInstanceZero(exportId);
            m_zeroExportHook = {};
            APPLICATION->exit(0);
        });
        return;
    }
    // ZERO: Dev aid ZERO_SCREENSHOT_BOOST=<instance id> opens "Boost performance" for that instance, waits until Modrinth, the mods folder and
    // Java answered (20 s at most) and saves the dialog over the window to <file> (and the dialog alone to <file>-dialog.png) instead of the
    // page; ZERO_SCREENSHOT_BOOST_APPLY=1 then presses Apply (into the -d data dir) and saves the window again 3 s later as <file>-after.png.
    if (const QString boostId = qEnvironmentVariable("ZERO_SCREENSHOT_BOOST"); !boostId.isEmpty()) {
        const bool apply = qEnvironmentVariableIntValue("ZERO_SCREENSHOT_BOOST_APPLY") == 1;
        m_zeroBoostHook = [this, path, apply](Zero::Perf::PerformanceDialog* dialog) {
            dialog->callWhenSettled(
                [this, dialog, path, apply]() {
                    QTimer::singleShot(500, dialog, [this, dialog, path, apply]() {
                        QApplication::processEvents();
                        QPixmap window = grab();
                        const QPixmap shot = dialog->grab();
                        QString dialogPath = path;
                        dialogPath.insert(dialogPath.lastIndexOf(QLatin1Char('.')), QStringLiteral("-dialog"));
                        bool ok = shot.save(dialogPath);
                        {
                            QPainter painter(&window);
                            painter.fillRect(window.rect(), QColor(0, 0, 0, 140));
                            const qreal ratio = shot.devicePixelRatio() > 0 ? shot.devicePixelRatio() : 1.0;
                            painter.drawPixmap(int((window.width() / window.devicePixelRatio() - shot.width() / ratio) / 2),
                                               int((window.height() / window.devicePixelRatio() - shot.height() / ratio) / 2), shot);
                        }
                        ok = window.save(path) && ok;
                        qInfo() << "ZERO screenshot" << (ok ? "saved to" : "FAILED for") << path << "and" << dialogPath;
                        if (apply && dialog->canApply()) {
                            qInfo() << "ZERO boost: applying" << dialog->plan().count() << "download(s), summary" << dialog->summaryText();
                            dialog->accept();
                        } else {
                            if (apply)
                                qWarning() << "ZERO boost: Apply is disabled, nothing to change";
                            dialog->reject();
                        }
                    });
                },
                20000);
        };
        QTimer::singleShot(0, this, [this, boostId, path, apply]() {
            boostPerformance(boostId);  // returns when the dialog (and the install) is over
            m_zeroBoostHook = {};
            if (!apply) {
                APPLICATION->exit(0);
                return;
            }
            QTimer::singleShot(3000, this, [this, path]() {
                QString afterPath = path;
                afterPath.insert(afterPath.lastIndexOf(QLatin1Char('.')), QStringLiteral("-after"));
                const bool ok = grab().save(afterPath);
                qInfo() << "ZERO screenshot" << (ok ? "saved to" : "FAILED for") << afterPath;
                APPLICATION->exit(0);
            });
        });
        return;
    }
    // ZERO: Dev aid ZERO_SCREENSHOT_PACK_SNAPSHOT=<instance id> takes a modpack snapshot of that instance now (outcome ok, in the normal
    // snapshots folder of the -d data dir), so the Library details offer "Roll back update". ZERO_SCREENSHOT_PACK_ROLLBACK=<instance id>
    // grabs the roll back question of that instance instead of the page.
    if (const QString snapId = qEnvironmentVariable("ZERO_SCREENSHOT_PACK_SNAPSHOT"); !snapId.isEmpty()) {
        if (auto* inst = APPLICATION->instances()->getInstanceById(snapId)) {
            Zero::Backup::Pack::Meta meta;
            meta.instanceId = inst->id();
            meta.instanceName = inst->name();
            meta.packId = inst->getManagedPackID();
            meta.packName = inst->getManagedPackName();
            meta.oldVersion = inst->getManagedPackVersionName();
            meta.newVersion = qEnvironmentVariable("ZERO_SCREENSHOT_PACK_NEW_VERSION");
            meta.gameDir = Zero::Backup::Pack::relativeGameDir(inst->instanceRoot(), inst->gameRoot());
            const auto made = Zero::Backup::Pack::createSnapshot(inst->instanceRoot(), Zero::Backup::Pack::snapshotsDirFor(APPLICATION->dataRoot(), snapId), meta);
            if (made.ok)
                Zero::Backup::Pack::markOutcome(made.zipPath, QString::fromLatin1(Zero::Backup::Pack::kOutcomeOk));
            qInfo() << "ZERO_SCREENSHOT_PACK_SNAPSHOT:" << (made.ok ? made.zipPath : made.error);
            m_library->detailsPanel()->refreshRollback();
        } else {
            qWarning() << "ZERO_SCREENSHOT_PACK_SNAPSHOT: unknown instance" << snapId;
        }
    }
    if (const QString rollbackId = qEnvironmentVariable("ZERO_SCREENSHOT_PACK_ROLLBACK"); !rollbackId.isEmpty()) {
        zeroScreenshotRollback(path, rollbackId);
        return;
    }
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
        QPixmap shot = grab();
        m_discover->paintOverlays(&shot, this);  // ZERO: the Discover filter panel is a popup window of its own
        // Dev aid: ZERO_SCREENSHOT_THEME_MENU=<custom theme id> paints that card's options menu where it would open.
        if (const QString menuTheme = qEnvironmentVariable("ZERO_SCREENSHOT_THEME_MENU"); !menuTheme.isEmpty())
            m_appearance->paintThemeMenu(&shot, this, menuTheme);
        const bool ok = shot.save(path);
        qInfo() << "ZERO screenshot" << (ok ? "saved to" : "FAILED for") << path;
        // Dev aid: ZERO_SCREENSHOT_SCREEN=1 also saves the real screen around the window (native title bar with the window icon, drawn by Windows)
        // as <file>-screen.png.
        if (qEnvironmentVariableIntValue("ZERO_SCREENSHOT_SCREEN") == 1 && ok && qEnvironmentVariableIntValue("ZERO_SCREENSHOT_COLOR_DIALOG") != 1) {
            QTimer::singleShot(800, this, [this, path]() {
                QApplication::processEvents();
                QString screenPath = path;
                screenPath.insert(screenPath.lastIndexOf(QLatin1Char('.')), QStringLiteral("-screen"));
                const QRect area = frameGeometry();
                const bool saved = QGuiApplication::primaryScreen()->grabWindow(0, area.x(), area.y(), area.width(), 120).save(screenPath);
                qInfo() << "ZERO screenshot" << (saved ? "saved to" : "FAILED for") << screenPath;
                APPLICATION->exit(saved ? 0 : 1);
            });
            return;
        }
        // Dev aid: ZERO_SCREENSHOT_COLOR_DIALOG=1 (with ZERO_SCREENSHOT_THEME_EDITOR) opens the editor's color dialog on the accent, picks two
        // colors the way a drag in it would, then saves the real screen around the window (what the user sees: window opacity included) as
        // <file>-dialog.png and the dialog's own grab as <file>-dialog-widget.png.
        if (qEnvironmentVariableIntValue("ZERO_SCREENSHOT_COLOR_DIALOG") == 1 && ok && m_appearance->themeEditor()->isOpen()) {
            QColorDialog* dialog = m_appearance->themeEditor()->pickColor(QStringLiteral("accent"));
            QTimer::singleShot(500, dialog, [dialog]() { dialog->setCurrentColor(QColor(0xC0, 0x39, 0x2B)); });
            QTimer::singleShot(900, dialog, [dialog]() { dialog->setCurrentColor(QColor(0x2E, 0x86, 0xDE)); });
            QTimer::singleShot(1800, this, [this, dialog, path]() {
                QApplication::processEvents();
                QString screenPath = path, widgetPath = path;
                screenPath.insert(screenPath.lastIndexOf(QLatin1Char('.')), QStringLiteral("-dialog"));
                widgetPath.insert(widgetPath.lastIndexOf(QLatin1Char('.')), QStringLiteral("-dialog-widget"));
                const QRect area = frameGeometry().united(dialog->frameGeometry());
                const QPixmap screen = QGuiApplication::primaryScreen()->grabWindow(0, area.x(), area.y(), area.width(), area.height());
                const bool saved = screen.save(screenPath) && dialog->grab().save(widgetPath);
                qInfo() << "ZERO screenshot" << (saved ? "saved to" : "FAILED for") << screenPath << "dialog" << dialog->size()
                        << "minimum" << dialog->minimumSizeHint() << "hint" << dialog->sizeHint() << "opacity" << dialog->windowOpacity();
                dialog->reject();
                APPLICATION->exit(saved ? 0 : 1);
            });
            return;
        }

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
            if (toPage == QLatin1String("discover-cards"))
                m_discover->modpacks()->replayEntrance();  // ZERO: the Modpacks cards enter again (dev aid)
            else if (!toPage.isEmpty())
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
    else if (page == QLatin1String("discover")) {
        // ZERO: Discover loads over the network: wait for the first page and the visible pictures (12 s at most).
        // Dev aids: ZERO_SCREENSHOT_DISCOVER_QUERY / _PROVIDER / _SELECT=<row> / _FILTERS=1 (docs/zero/SHELL.md).
        // ZERO_SCREENSHOT_DISCOVER_INSTALL=1 grabs the install dialog of the selected pack (or, with ZERO_SCREENSHOT_DISCOVER_SECTION=mods|resourcepacks|shaders,
        // of the selected mod: its primary action is pressed, the dialog is grabbed once the dependency check settled) instead of the
        // page, then rejects it; =accept presses Install / Add after the grab (installed into the -d data dir).
        m_discover->prepareScreenshot([this, grabAndFinish, path]() {
            // ZERO_SCREENSHOT_DISCOVER_CART_REVIEW=1 (with ZERO_SCREENSHOT_DISCOVER_CART=<rows>) presses "Review and add" of the selection bar, waits
            // until the versions and the dependency check settled (20 s at most), grabs the review instead of the page and rejects it;
            // =accept adds everything after the grab (into the -d data dir) and grabs the page 3 s later as <file>-after.png.
            const QString reviewAid = qEnvironmentVariable("ZERO_SCREENSHOT_DISCOVER_CART_REVIEW");
            if (!reviewAid.isEmpty()) {
                auto* bar = m_discover->selectionBar();
                if (m_discover->cart()->isEmpty() || !bar->reviewButton()->isEnabled()) {
                    qWarning() << "ZERO_SCREENSHOT_DISCOVER_CART_REVIEW needs ZERO_SCREENSHOT_DISCOVER_CART=<rows> to fill the cart";
                    QTimer::singleShot(600, this, grabAndFinish);
                    return;
                }
                const bool accept = reviewAid == QLatin1String("accept");
                m_zeroCartHook = [path, accept](Zero::Content::CartReviewDialog* dialog) {
                    dialog->callWhenSettled(
                        [dialog, path, accept]() {
                            QTimer::singleShot(500, dialog, [dialog, path, accept]() {
                                QApplication::processEvents();
                                const bool ok = dialog->grab().save(path);
                                qInfo() << "ZERO screenshot" << (ok ? "saved to" : "FAILED for") << path;
                                if (accept && dialog->canInstall()) {
                                    qInfo() << "ZERO install: adding" << dialog->plan().count() << "download(s) from the cart";
                                    dialog->accept();
                                } else {
                                    if (accept)
                                        qWarning() << "ZERO install: Add is disabled, nothing to install";
                                    dialog->reject();
                                }
                            });
                        },
                        20000);
                };
                QTimer::singleShot(0, this, [this, path, accept]() {
                    m_discover->selectionBar()->reviewButton()->click();  // cartReviewRequested -> installCart(), returns when the dialog (and the install) is over
                    m_zeroCartHook = {};
                    if (!accept) {
                        APPLICATION->exit(0);
                        return;
                    }
                    QTimer::singleShot(3000, this, [this, path]() {
                        QString afterPath = path;
                        afterPath.insert(afterPath.lastIndexOf(QLatin1Char('.')), QStringLiteral("-after"));
                        const bool ok = grab().save(afterPath);
                        qInfo() << "ZERO screenshot" << (ok ? "saved to" : "FAILED for") << afterPath;
                        APPLICATION->exit(0);
                    });
                });
                return;
            }
            const QString installAid = qEnvironmentVariable("ZERO_SCREENSHOT_DISCOVER_INSTALL");
            // Mods, Resource Packs and Shaders share the dialog path (the pack dialog has no dependency check).
            const bool modsShown = m_discover->current() == m_discover->mods() || m_discover->current() == m_discover->packs() ||
                                   m_discover->current() == m_discover->shaders() || (m_discover->worlds() && m_discover->current() == m_discover->worlds());
            const auto item = m_discover->current()->selectedItem();
            if (installAid.isEmpty() || !item) {
                if (!installAid.isEmpty())
                    qWarning() << "ZERO_SCREENSHOT_DISCOVER_INSTALL needs ZERO_SCREENSHOT_DISCOVER_SELECT=<row> to open a pack";
                QTimer::singleShot(600, this, grabAndFinish);
                return;
            }
            const bool accept = installAid == QLatin1String("accept");
            if (modsShown) {
                m_zeroModInstallHook = [path, accept](Zero::Content::ModInstallDialog* dialog) {
                    dialog->callWhenSettled(
                        [dialog, path, accept]() {
                            QTimer::singleShot(500, dialog, [dialog, path, accept]() {
                                QApplication::processEvents();
                                const bool ok = dialog->grab().save(path);
                                qInfo() << "ZERO screenshot" << (ok ? "saved to" : "FAILED for") << path;
                                if (accept && dialog->canInstall()) {
                                    qInfo() << "ZERO install: adding" << dialog->plan().count() << "mod(s), first version" << dialog->versionBox()->currentText();
                                    dialog->accept();
                                } else {
                                    if (accept)
                                        qWarning() << "ZERO install: Add is disabled, nothing to install";
                                    dialog->reject();
                                }
                            });
                        },
                        20000);
                };
                QTimer::singleShot(0, this, [this, path, accept]() {
                    // The same path as a click on the details panel's primary action: installRequested -> installMod().
                    auto* button = m_discover->current()->panel()->primaryButton();
                    const bool enabled = button->isEnabled();
                    if (enabled)
                        button->click();
                    else
                        qWarning() << "ZERO_SCREENSHOT_DISCOVER_INSTALL: the primary action of the selected mod is disabled:" << button->text();
                    m_zeroModInstallHook = {};
                    if (!accept || !enabled) {
                        APPLICATION->exit(0);
                        return;
                    }
                    // After an install the section reads the mods folder again: grab the page (as <file>-after.png) to show its state.
                    QTimer::singleShot(3000, this, [this, path, button]() {
                        QString afterPath = path;
                        afterPath.insert(afterPath.lastIndexOf(QLatin1Char('.')), QStringLiteral("-after"));
                        qInfo() << "ZERO install: the primary action of the mod now reads" << button->text();
                        const bool ok = grab().save(afterPath);
                        qInfo() << "ZERO screenshot" << (ok ? "saved to" : "FAILED for") << afterPath;
                        APPLICATION->exit(0);
                    });
                });
                return;
            }
            m_zeroInstallHook = [path, accept](Zero::Content::ModpackInstallDialog* dialog) {
                dialog->callWhenSettled(
                    [dialog, path, accept]() {
                        QTimer::singleShot(500, dialog, [dialog, path, accept]() {
                            QApplication::processEvents();
                            const bool ok = dialog->grab().save(path);
                            qInfo() << "ZERO screenshot" << (ok ? "saved to" : "FAILED for") << path;
                            if (accept && dialog->canInstall()) {
                                qInfo() << "ZERO install: installing" << dialog->instName() << "version" << dialog->versionBox()->currentText();
                                dialog->accept();
                            } else {
                                if (accept)
                                    qWarning() << "ZERO install: Install is disabled, nothing to install";
                                dialog->reject();
                            }
                        });
                    },
                    15000);
            };
            QTimer::singleShot(0, this, [this, item]() {
                installModpack(item, QString());
                m_zeroInstallHook = {};
                APPLICATION->exit(0);
            });
        });
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

// ZERO: Dev aid ZERO_SCREENSHOT_INSTANCE_PAGE=<instance id>:<page id>: opens that instance's window on that page and grabs the window to
// `path` (ZERO_SCREENSHOT_SIZE=<w>x<h> sizes it, default 1280x800; ZERO_SCREENSHOT_RUNNING=<instance id> flips the instance to running first).
// Page "mods" waits until the mods are listed (8 s at most); ZERO_SCREENSHOT_MODS_SEARCH=<text>, ZERO_SCREENSHOT_MODS_FILTER=<all|enabled|
// disabled|issues|updates> and ZERO_SCREENSHOT_MODS_SELECT=<n> (the first n rows) shape the list before the grab; ZERO_SCREENSHOT_MODS_CHECK=1
// first runs a real update check and waits until it settled (25 s at most), so "updates" has something to show.
// ZERO_SCREENSHOT_MODS_INSPECT=<mod name> opens the Dependency Inspector panel of that mod once the scan answered (8 s at most).
// The strip of the last update (Roll back) shows by itself when the instance has a snapshot in the data dir; ZERO_SCREENSHOT_MODS_SNAPSHOT=dialog
// grabs the "Roll back N mods?" question (the dialog only, to `path`) instead of the window and rejects it.
// Page "resourcepacks" waits until the packs are listed (8 s at most); ZERO_SCREENSHOT_PACKS_SEARCH=<text> and
// ZERO_SCREENSHOT_PACKS_FILTER=<all|on|off> shape the list before the grab.
void MainWindow::zeroScreenshotInstance(const QString& path, const QString& spec)
{
    const int colon = spec.indexOf(QLatin1Char(':'));
    auto* inst = colon > 0 ? APPLICATION->instances()->getInstanceById(spec.left(colon)) : nullptr;
    if (!inst) {
        qWarning() << "ZERO_SCREENSHOT_INSTANCE_PAGE: expected <instance id>:<page id> of a known instance, got" << spec;
        APPLICATION->exit(1);
        return;
    }
    const QString pageId = spec.mid(colon + 1);
    if (qEnvironmentVariable("ZERO_SCREENSHOT_RUNNING") == inst->id())
        inst->setRunning(true);
    QSize size(1280, 800);
    const QStringList dims = qEnvironmentVariable("ZERO_SCREENSHOT_SIZE").split(QLatin1Char('x'));
    if (dims.size() == 2 && dims[0].toInt() > 0 && dims[1].toInt() > 0)
        size = QSize(dims[0].toInt(), dims[1].toInt());
    InstanceWindow* window = APPLICATION->showInstanceWindow(inst, pageId);
    if (!window) {
        APPLICATION->exit(1);
        return;
    }
    window->resize(size);
    auto grab = [window, path]() {
        QApplication::processEvents();
        const bool ok = window->grab().save(path);
        qInfo() << "ZERO screenshot" << (ok ? "saved to" : "FAILED for") << path;
        APPLICATION->exit(ok ? 0 : 1);
    };
    if (auto* packsPage = dynamic_cast<Zero::PacksUi::ResourcePacksPage*>(window->selectedPage())) {
        auto* packPoll = new QTimer(window);
        auto packWaited = std::make_shared<int>(0);
        connect(packPoll, &QTimer::timeout, window, [packPoll, packWaited, packsPage, grab]() {
            *packWaited += 100;
            if (!packsPage->isLoaded() && *packWaited < 8000)
                return;
            packPoll->stop();
            QTimer::singleShot(800, packsPage, [packsPage, grab]() {
                if (const QString search = qEnvironmentVariable("ZERO_SCREENSHOT_PACKS_SEARCH"); !search.isEmpty())
                    packsPage->setSearchText(search);
                if (const QString filter = qEnvironmentVariable("ZERO_SCREENSHOT_PACKS_FILTER"); !filter.isEmpty())
                    packsPage->setFilter(Zero::Content::Packs::filterFromId(filter));
                QTimer::singleShot(400, packsPage, grab);
            });
        });
        packPoll->start(100);
        return;
    }
    auto* modsPage = dynamic_cast<Zero::ModsUi::ModsPage*>(window->selectedPage());
    if (!modsPage) {
        QTimer::singleShot(1500, window, grab);
        return;
    }
    auto* poll = new QTimer(window);
    auto waited = std::make_shared<int>(0);
    connect(poll, &QTimer::timeout, window, [this, poll, waited, modsPage, grab, path]() {
        *waited += 100;
        if (!modsPage->isLoaded() && *waited < 8000)
            return;
        poll->stop();
        QTimer::singleShot(800, modsPage, [modsPage, grab, path]() {
            auto shape = [modsPage, grab, path]() {
                if (const QString search = qEnvironmentVariable("ZERO_SCREENSHOT_MODS_SEARCH"); !search.isEmpty())
                    modsPage->setSearchText(search);
                if (const QString filter = qEnvironmentVariable("ZERO_SCREENSHOT_MODS_FILTER"); !filter.isEmpty())
                    modsPage->setFilter(Zero::Mods::Installed::filterFromId(filter));
                if (const int count = qEnvironmentVariableIntValue("ZERO_SCREENSHOT_MODS_SELECT"); count > 0) {
                    QList<int> rows;
                    for (int i = 0; i < count && i < modsPage->listModel()->rowCount(); ++i)
                        rows.append(i);
                    modsPage->selectRows(rows);
                }
                if (qEnvironmentVariable("ZERO_SCREENSHOT_MODS_SNAPSHOT") == QLatin1String("dialog")) {
                    // requestRollback() blocks in the question: the timer grabs the modal dialog, rejects it and ends the run.
                    QTimer::singleShot(700, modsPage, [path]() {
                        QWidget* dialog = QApplication::activeModalWidget();
                        const bool ok = dialog && dialog->grab().save(path);
                        qInfo() << "ZERO screenshot" << (ok ? "saved to" : "FAILED for") << path;
                        if (auto* d = qobject_cast<QDialog*>(dialog))
                            d->reject();
                        APPLICATION->exit(ok ? 0 : 1);
                    });
                    modsPage->requestRollback();
                    return;
                }
                // The Dependency Inspector panel of that mod: waits for the scan (8 s at most) so the sections are filled.
                if (const QString inspect = qEnvironmentVariable("ZERO_SCREENSHOT_MODS_INSPECT"); !inspect.isEmpty()) {
                    auto* scanPoll = new QTimer(modsPage);
                    auto scanWaited = std::make_shared<int>(0);
                    connect(scanPoll, &QTimer::timeout, modsPage, [modsPage, scanPoll, scanWaited, inspect, grab]() {
                        *scanWaited += 100;
                        if (!modsPage->hasScan() && *scanWaited < 8000)
                            return;
                        scanPoll->stop();
                        if (!modsPage->openPanelFor(inspect))
                            qWarning() << "ZERO_SCREENSHOT_MODS_INSPECT: no mod named" << inspect;
                        QTimer::singleShot(400, modsPage, grab);
                    });
                    scanPoll->start(100);
                    return;
                }
                QTimer::singleShot(400, modsPage, grab);
            };
            if (qEnvironmentVariableIntValue("ZERO_SCREENSHOT_MODS_CHECK") != 1) {
                shape();
                return;
            }
            // A real update check (network): shaped and grabbed once it settled, 25 s at most.
            modsPage->startCheck();
            auto* settle = new QTimer(modsPage);
            auto waited = std::make_shared<int>(0);
            connect(settle, &QTimer::timeout, modsPage, [modsPage, settle, waited, shape]() {
                *waited += 200;
                if (modsPage->isChecking() && *waited < 25000)
                    return;
                settle->stop();
                QTimer::singleShot(300, modsPage, shape);
            });
            settle->start(200);
        });
    });
    poll->start(100);
}

// ZERO: Dev aid ZERO_SCREENSHOT_CREATE=<vanilla|modded|modpack|import>: opens Create Instance through the normal addInstance()
// path in that kind, waits until its version lists loaded or failed (10 s at most) and grabs the dialog to `path`.
// Optional: ZERO_SCREENSHOT_SIZE=<w>x<h> sizes the dialog, ZERO_SCREENSHOT_CREATE_MC=<version> and
// ZERO_SCREENSHOT_CREATE_LOADER=<fabric|forge|neoforge|quilt|liteloader> preselect, ZERO_SCREENSHOT_CREATE_FILE=<path> /
// ZERO_SCREENSHOT_CREATE_URL=<link> fill the Import source, ZERO_SCREENSHOT_CREATE_ADVANCED=1 expands Advanced,
// ZERO_SCREENSHOT_CREATE_ACCEPT=1 presses Create after the grab (the instance is created in the -d data dir).
void MainWindow::zeroScreenshotCreate(const QString& path, const QString& kindKey)
{
    using Zero::Create::CreateInstanceDialog;
    const auto kind = CreateInstanceDialog::kindFromKey(kindKey);
    if (!kind)
        qWarning() << "ZERO_SCREENSHOT_CREATE: unknown kind" << kindKey << "(vanilla, modded, modpack, import)";
    QSize size;
    const QStringList dims = qEnvironmentVariable("ZERO_SCREENSHOT_SIZE").split(QLatin1Char('x'));
    if (dims.size() == 2 && dims[0].toInt() > 0 && dims[1].toInt() > 0)
        size = QSize(dims[0].toInt(), dims[1].toInt());
    const bool accept = qEnvironmentVariableIntValue("ZERO_SCREENSHOT_CREATE_ACCEPT") == 1;

    m_zeroCreateHook = [path, kind, size, accept](CreateInstanceDialog* dialog) {
        dialog->setKind(kind.value_or(CreateInstanceDialog::Kind::Vanilla));
        if (size.isValid())
            dialog->resize(size.expandedTo(dialog->minimumSize()));
        const QString mc = qEnvironmentVariable("ZERO_SCREENSHOT_CREATE_MC");
        const auto loader = Zero::Create::loaderFromName(qEnvironmentVariable("ZERO_SCREENSHOT_CREATE_LOADER"));
        if (!mc.isEmpty() || loader)
            dialog->preselect(mc, loader);
        if (const QString file = qEnvironmentVariable("ZERO_SCREENSHOT_CREATE_FILE"); !file.isEmpty())
            dialog->importPanel()->setLocalFile(file);
        if (const QString link = qEnvironmentVariable("ZERO_SCREENSHOT_CREATE_URL"); !link.isEmpty())
            dialog->importPanel()->setUrlText(link);
        if (qEnvironmentVariableIntValue("ZERO_SCREENSHOT_CREATE_ADVANCED") == 1 && dialog->advancedButton()->isVisibleTo(dialog))
            dialog->advancedButton()->setChecked(true);
        dialog->callWhenSettled(
            [dialog, path, accept]() {
                QTimer::singleShot(600, dialog, [dialog, path, accept]() {
                    QApplication::processEvents();
                    const bool ok = dialog->grab().save(path);
                    qInfo() << "ZERO screenshot" << (ok ? "saved to" : "FAILED for") << path;
                    if (accept && dialog->canCreate()) {
                        qInfo() << "ZERO create: creating" << dialog->instName() << "in" << dialog->instDir();
                        dialog->accept();
                    } else {
                        if (accept)
                            qWarning() << "ZERO create: Create is disabled, nothing to create";
                        dialog->reject();
                    }
                });
            },
            10000);
    };
    QTimer::singleShot(300, this, [this, kind, accept]() {
        const int before = APPLICATION->instances()->count();
        addInstance(QString(), {}, kind == CreateInstanceDialog::Kind::Import ? QStringLiteral("import") : QString());
        m_zeroCreateHook = {};
        if (accept)
            qInfo() << "ZERO create: instances" << before << "->" << APPLICATION->instances()->count();
        APPLICATION->exit(0);
    });
}

// ZERO: Dev aid ZERO_SCREENSHOT_PACK_ROLLBACK=<instance id>: shows the question of "Roll back update" (the same prompt as the real flow, never
// answered) and grabs it to `path`.
void MainWindow::zeroScreenshotRollback(const QString& path, const QString& instanceId)
{
    auto* inst = APPLICATION->instances()->getInstanceById(instanceId);
    const auto snapshot = inst ? Zero::Backup::Pack::newestActive(
                                     Zero::Backup::Pack::listSnapshots(Zero::Backup::Pack::snapshotsDirFor(APPLICATION->dataRoot(), instanceId)),
                                     inst->getManagedPackID())
                               : Zero::Backup::Pack::Info();
    if (!snapshot.valid) {
        qWarning() << "ZERO_SCREENSHOT_PACK_ROLLBACK: no snapshot of instance" << instanceId << "(ZERO_SCREENSHOT_PACK_SNAPSHOT makes one)";
        APPLICATION->exit(1);
        return;
    }
    auto* dialog = new Zero::ModsUi::ChoiceDialog(rollbackPrompt(inst->name(), snapshot), this);
    dialog->show();
    QTimer::singleShot(600, this, [dialog, path]() {
        QApplication::processEvents();
        const bool ok = dialog->grab().save(path);
        qInfo() << "ZERO screenshot" << (ok ? "saved to" : "FAILED for") << path;
        dialog->deleteLater();
        APPLICATION->exit(0);
    });
}

void MainWindow::recordTransitionFrames(const QString& path, const std::function<void()>& trigger, const std::function<void()>& done)
{
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    const QString stem = dot > 0 ? path.left(dot) : path;
    const QString ext = dot > 0 ? path.mid(dot) : QStringLiteral(".png");
    constexpr int kFrames = 9;
    // ZERO: ZERO_SCREENSHOT_TRANSITION_STEP=<ms> spaces the frames wider (the Discover card entrance lasts ~700 ms); default 40.
    const int kStepMs = qEnvironmentVariableIntValue("ZERO_SCREENSHOT_TRANSITION_STEP") > 0 ? qEnvironmentVariableIntValue("ZERO_SCREENSHOT_TRANSITION_STEP") : 40;
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
