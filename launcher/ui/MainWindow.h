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

#pragma once

#include <functional>
#include <memory>

#include <QMainWindow>
#include <QPointer>
#include <QProcess>
#include <QTimer>

#include "minecraft/auth/MinecraftAccount.h"
#include "zero/content/CartInstall.h"

class EditableLabel;
class LaunchController;
class NewsChecker;
class QLabel;
class QTextEdit;
class QToolButton;
class InstanceProxyModel;
class MinecraftInstance;
class MinecraftLauncher;
class BaseProfilerFactory;
class InstanceView;
class KonamiCode;
class InstanceTask;
class QMenu;

namespace Zero {
class ExportChooserDialog;  // ZERO
class HomePage;
class LibraryPage;
class DiscoverPage;  // ZERO
namespace Content {
struct ModpackItem;  // ZERO
enum class Kind;  // ZERO (zero/content/ContentFilter.h)
class ModInstallDialog;  // ZERO
class CartReviewDialog;  // ZERO
class ModpackInstallDialog;  // ZERO
}  // namespace Content
class WorldsPage;  // ZERO
class ScreenshotsPage;  // ZERO
class ServersPage;  // ZERO
class ToolsPage;
class AppearancePage;
class ZeroSettingsPage;
class ZeroShell;
class ZeroCommandPalette;
class ZeroShortcutSheet;
namespace Onboarding {
class IntroOverlay;  // ZERO
class TourOverlay;   // ZERO
}
struct PaletteCommand;
struct ShortcutGroup;
namespace Perf {
class PerformanceDialog;  // ZERO
}
namespace Create {
class CreateInstanceDialog;  // ZERO
}
}  // namespace Zero

namespace Ui {
class MainWindow;
}
class MainWindow : public QMainWindow {
    Q_OBJECT

   public:
    /// ZERO: shows Settings as a page of this window and selects `pageId` (empty = last section). False when unavailable.
    bool showSettingsPage(const QString& pageId = QString());
    explicit MainWindow(QWidget* parent = 0);
    ~MainWindow();

    bool eventFilter(QObject* obj, QEvent* ev) override;
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

    void checkInstancePathForProblems();

    void updatesAllowedChanged(bool allowed);

    void processURLs(QList<QUrl> urls);

    /// Dev aid (`--zero-screenshot`): shows `page` ("home", "library" or "tools"), grabs the window to `path` and quits.
    void zeroScreenshot(const QString& path, const QString& page);
    /// ZERO: opens the Discover page on `section` ("modpacks" | "mods" | "resourcepacks" | "shaders" | "worlds"; empty = the remembered one) with `provider` ("modrinth" | "curseforge" | "technic" | empty) preselected.
    void showDiscover(const QString& section, const QString& provider = {});
    /// Discover > Mods with this instance as the target (the instance window's "Add mods"); raises the main window.
    /// `query` (optional) becomes the search text of Discover > Mods (the Mods page's "Find" for a missing dependency).
    void showDiscoverMods(const QString& instanceId, const QString& query = QString());
    /// Discover > Resource Packs with this instance as the target (the packs page of the instance window); raises the main window.
    void showDiscoverPacks(const QString& instanceId, const QString& query = QString());
    /// Discover > Shaders with this instance as the target; raises the main window.
    void showDiscoverShaders(const QString& instanceId, const QString& query = QString());
    /// Discover > Worlds with this instance as the target (only when the build has a CurseForge key); raises the main window.
    void showDiscoverWorlds(const QString& instanceId, const QString& query = QString());
    /// ZERO: the Install / Update of a Discover modpack. Shows the install review, then runs the provider's task while the dialog is
    /// alive (its tasks parent their own dialogs to it); a toast with "Show in Library" follows a successful install.
    void installModpack(const std::shared_ptr<Zero::Content::ModpackItem>& item, const QString& updateInstanceId);
    /// ZERO: the Add / Update of a Discover mod (`installedFileId` non-empty = update that file). Shows the review dialog (version, dependencies),
    /// downloads the chosen mods like ModFolderPage does, reads the mods folder again and toasts "Added ... to <instance>" with "Show mods".
    void installMod(const std::shared_ptr<Zero::Content::ModpackItem>& item, const QString& targetInstanceId, const QString& installedFileId);
    /// ZERO: the Add / Update of a Discover resource pack: the review dialog (version only), one ResourceDownloadTask into the instance's
    /// `resourcePackList()`, the folder read again, a toast "Added ... to <instance>". Packs are never turned on here (that is the packs page).
    void installResourcePack(const std::shared_ptr<Zero::Content::ModpackItem>& item, const QString& targetInstanceId, const QString& installedFileId);
    /// ZERO: the Add / Update of a Discover shader: the same flow as a resource pack over `shaderPackList()` (no "Turn it on": the game
    /// picks the shader), toast "Added ... to <instance>" with "Show shaders".
    void installShaderPack(const std::shared_ptr<Zero::Content::ModpackItem>& item, const QString& targetInstanceId, const QString& installedFileId);
    /// ZERO: the Add of a Discover world (CurseForge). The review dialog (file, version note), then ONE task under a ProgressDialog: a NetJob
    /// downloads the zip into `<instance>/.zero/world-download/` (CurseForge's hash checked), Prism's `worldList()->createInstallWorldTask`
    /// unpacks the folder with level.dat into `saves/`; the zip is deleted whatever happens. Refused while the target runs. Toast
    /// "Added ... to <instance>" with "Show worlds".
    void installWorld(const std::shared_ptr<Zero::Content::ModpackItem>& item, const QString& targetInstanceId);
    /// ZERO: "Review and add to <instance>" of Discover's selection bar. Shows the cart review (versions, dependencies of all the mods together),
    /// downloads everything in one task into the mods / resourcepacks / shaderpacks folders of the target, turns the resource packs on (one
    /// options.txt write) and toasts "Added 3 mods and 1 shader to <instance>" with "Show". Added entries leave the cart; failed ones stay.
    void installCart();
    /// ZERO: what runInstallPlan() did: `done` = the downloads whose task succeeded (a failed or never-run one is not in it), `packFiles` = the
    /// resource pack files to turn on, `instanceGone` = the instance disappeared meanwhile (nothing else is valid).
    struct PlanRun {
        Zero::Content::CartInstallPlan done;
        QStringList packFiles;
        bool instanceGone = false;
    };
    /// ZERO: the download half of installCart() (one ConcurrentTask under a ProgressDialog, folders read again), shared with Boost performance.
    PlanRun runInstallPlan(const QString& targetInstanceId, const Zero::Content::CartInstallPlan& plan);
    /// ZERO: "Boost performance" of an instance (Library details button, command palette): the dialog, the mods through runInstallPlan(), the
    /// memory settings, the toast "Boosted <instance>" with "Show mods"; "Fix" of its Java line opens Tools > Java Doctor.
    void boostPerformance(const QString& instanceId);
    /// ZERO: the Library Export button. Asks Full copy / Modrinth pack / CurseForge pack, then opens that Prism export dialog.
    void exportInstanceZero(const QString& instanceId);
    void recordTransitionFrames(const QString& path, const std::function<void()>& trigger, const std::function<void()>& done);
   signals:
    void isClosing();

   protected:
    QMenu* createPopupMenu() override;
    void showEvent(QShowEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

   private slots:
    void onCatToggled(bool);

    void onCatChanged(int);

    void on_actionAbout_triggered();

    void on_actionAddInstance_triggered();

    void on_actionREDDIT_triggered();

    void on_actionMATRIX_triggered();

    void on_actionDISCORD_triggered();

    void on_actionCopyInstance_triggered();

    void on_actionChangeInstGroup_triggered();

    void on_actionChangeInstIcon_triggered();

    void on_actionViewLauncherRootFolder_triggered();

    void on_actionViewInstanceFolder_triggered();
    void on_actionViewCentralModsFolder_triggered();

    void on_actionViewIconThemeFolder_triggered();
    void on_actionViewWidgetThemeFolder_triggered();
    void on_actionViewCatPackFolder_triggered();
    void on_actionViewIconsFolder_triggered();
    void on_actionViewLogsFolder_triggered();
    void on_actionViewJavaFolder_triggered();
    void on_actionViewTemplateFolder_triggered();

    void on_actionViewSkinsFolder_triggered();

    void on_actionViewSelectedInstFolder_triggered();

    void refreshInstances();

    void checkForUpdates();

    void on_actionSettings_triggered();

    void on_actionManageSkins_triggered();

    void on_actionManageAccounts_triggered();
    void addAccountZero();  // ZERO: the Add account dialog
    /// ZERO: plays the first-run intro over the window (queued by Application::performMainStartupAction); does nothing when one is up.
    void startOnboardingIntro();
    /// ZERO: the intro is over ("Get started"). The single place the next onboarding steps hang on: the Add account dialog now (when
    /// there is no valid account), the guided tour later.
    void onboardingIntroFinished();
    /// ZERO: the guided tour (Home, rail, New instance, Search ...). Starts from step 1 on Home, whatever ZeroTourDone says; does nothing
    /// when one is up. Called after the intro, by the "Take the tour" palette entry and by ZERO_FORCE_TOUR.
    void startGuidedTour();

    void on_actionReportBug_triggered();

    void on_actionClearMetadata_triggered();

#ifdef Q_OS_MACOS
    void on_actionAddToPATH_triggered();
#endif

    void on_actionOpenWiki_triggered();

    void on_actionMoreNews_triggered();

    void newsButtonClicked();

    void on_actionLaunchInstance_triggered();

    void on_actionKillInstance_triggered();

    void on_actionDeleteInstance_triggered();

    void setGroupIcon(QString group);
    void removeGroupIcon(QString group);

    void deleteGroup(QString group);
    void renameGroup(QString group);
    void undoTrashInstance();

    inline void on_actionExportInstance_triggered() { on_actionExportInstanceZip_triggered(); }
    void on_actionExportInstanceZip_triggered();
    void on_actionExportInstanceMrPack_triggered();
    void on_actionExportInstanceFlamePack_triggered();

    void on_actionRenameInstance_triggered();

    void on_actionEditInstance_triggered();

    void on_actionCreateInstanceShortcut_triggered();

    void taskEnd();

    /**
     * called when an icon is changed in the icon model.
     */
    void iconUpdated(QString);

    void showInstanceContextMenu(const QPoint&);

    void updateMainToolBar();

    void updateLaunchButton();

    void updateThemeMenu();

    void instanceActivated(QModelIndex);

    void instanceChanged(const QModelIndex& current, const QModelIndex& previous);

    void instanceSelectRequest(QString id);

    void instanceDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight);

    void selectionBad();

    void startTask(Task* task);

    void defaultAccountChanged();

    void changeActiveAccount();

    void repopulateAccountsMenu();

    void updateNewsLabel();

    void konamiTriggered();

    void globalSettingsClosed();

    void setStatusBarVisibility(bool);

    void lockToolbars(bool);

#ifndef Q_OS_MACOS
    void keyReleaseEvent(QKeyEvent* event) override;
#endif

    void refreshCurrentInstance();

   private:
    /// The one body of installResourcePack / installShaderPack (`kind` = Kind::ResourcePack or Kind::Shader).
    void installFolderContent(Zero::Content::Kind kind, const std::shared_ptr<Zero::Content::ModpackItem>& item, const QString& targetInstanceId,
                              const QString& installedFileId);
    void retranslateUi();

    void updateInstanceToolBarOrientation(Qt::Orientation orientation);

    void addInstance(const QString& url = QString(), const QMap<QString, QString>& extra_info = {}, const QString& initialPage = QString());
    // ZERO: the classic New Instance dialog (deep links, classic pages) and the ZERO Create Instance dialog
    void addInstanceClassic(const QString& groupName, const QString& url, const QMap<QString, QString>& extra_info, const QString& initialPage);
    void addInstanceZero(const QString& groupName, bool import);
    void zeroScreenshotCreate(const QString& path, const QString& kind);
    void zeroScreenshotInstance(const QString& path, const QString& spec);
    void zeroScreenshotRollback(const QString& path, const QString& instanceId);
    // ZERO: safe modpack update (docs/zero/ARCHITECTURE.md, "Modpack snapshots")
    bool saveCopyBeforeModpackUpdate(const QString& instanceId, const QString& newVersion, QString* zipPath);
    void rollBackModpackUpdate(const QString& instanceId);
    void setCatBackground(bool enabled);
    void updateCatState();
    void updateInstanceToolIcon(QString new_icon);
    void setSelectedInstanceById(const QString& id);
    void updateStatusCenter();
    void setInstanceActionsEnabled(bool enabled);

    void runModalTask(Task* task);
    void instanceFromInstanceTask(InstanceTask* task);

    void handleInstanceRename(const QString& before, const QString& after);

    // ZERO shell
    void setupZeroShell();
    void hideLegacyChrome();
    void updateAccountChip();
    static QString accountChipSubtitle(AccountType type);  // ZERO: "Microsoft account" / "Ely.by account" / "Offline"
    void showAccountMenu(const QPoint& anchor);
    void showMoreMenu(const QPoint& anchor);
    void setupCommandPalette();
    QList<Zero::PaletteCommand> buildPaletteCommands();
    QList<Zero::ShortcutGroup> buildShortcutGroups();
    void showInLibrary(const QString& instanceId);
    void togglePalette();
    void toggleShortcutSheet();
    void showInstanceContextMenuAt(const QPoint& globalPos, bool onInstance, const QString& group);
    void renameSelectedInstance(const QString& newName);
    // instance profile (favorite, artwork): the pages emit requests, these apply them
    void toggleFavorite(const QString& instanceId);
    void chooseArtworkForSelected();
    void removeArtworkFromSelected();
    void editInstance(MinecraftInstance* instance);

   private:
    Ui::MainWindow* ui;
    // these are managed by Qt's memory management model!
    InstanceView* view = nullptr;
    InstanceProxyModel* proxymodel = nullptr;
    QToolButton* newsLabel = nullptr;
    QLabel* m_statusLeft = nullptr;
    QLabel* m_statusCenter = nullptr;
    QToolButton* m_changeIconButton = nullptr;
    QAction* m_instanceToolBarTopSeparator = nullptr;
    QAction* m_instanceInfoWidget = nullptr;
    EditableLabel* m_nameLabel = nullptr;
    QToolButton* helpMenuButton = nullptr;
    KonamiCode* secretEventFilter = nullptr;

    Zero::ZeroShell* m_shell = nullptr;
    Zero::HomePage* m_home = nullptr;
    Zero::LibraryPage* m_library = nullptr;
    Zero::DiscoverPage* m_discover = nullptr;  // ZERO
    Zero::WorldsPage* m_worlds = nullptr;  // ZERO
    Zero::ScreenshotsPage* m_screenshots = nullptr;  // ZERO
    Zero::ServersPage* m_servers = nullptr;  // ZERO
    Zero::ToolsPage* m_tools = nullptr;
    Zero::AppearancePage* m_appearance = nullptr;
    Zero::ZeroSettingsPage* m_settingsPage = nullptr;
    Zero::ZeroCommandPalette* m_palette = nullptr;
    QPointer<Zero::Onboarding::IntroOverlay> m_intro;  // ZERO: the first-run intro while it plays
    QPointer<Zero::Onboarding::TourOverlay> m_tour;    // ZERO: the guided tour while it runs
    Zero::ZeroShortcutSheet* m_shortcutSheet = nullptr;
    QAction* m_actionAddAccount = nullptr;  // ZERO: "Add account..." in the account menu
    QList<QAction*> m_accountActions;  // per-account actions (they carry the Ctrl+N shortcuts), recreated on repopulate
    // ZERO: dev screenshot aid only (ZERO_SCREENSHOT_CREATE): called with the Create Instance dialog before it is shown
    std::function<void(Zero::Create::CreateInstanceDialog*)> m_zeroCreateHook;
    // ZERO: dev screenshot aid only (ZERO_SCREENSHOT_DISCOVER_INSTALL): called with the modpack install dialog before it is shown
    std::function<void(Zero::Content::ModpackInstallDialog*)> m_zeroInstallHook;
    // ZERO: dev screenshot aid only (ZERO_SCREENSHOT_DISCOVER_INSTALL with the Mods section): called with the mod install dialog before it is shown
    std::function<void(Zero::Content::ModInstallDialog*)> m_zeroModInstallHook;
    // ZERO: dev screenshot aid only (ZERO_SCREENSHOT_DISCOVER_CART_REVIEW): called with the cart review before it is shown
    std::function<void(Zero::Content::CartReviewDialog*)> m_zeroCartHook;
    // ZERO: dev screenshot aid only (ZERO_SCREENSHOT_BOOST): called with the Boost performance dialog before it is shown
    std::function<void(Zero::Perf::PerformanceDialog*)> m_zeroBoostHook;
    // ZERO: dev screenshot aid only (ZERO_SCREENSHOT_EXPORT): called with the export chooser before it is shown
    std::function<void(Zero::ExportChooserDialog*)> m_zeroExportHook;

    unique_qobject_ptr<NewsChecker> m_newsChecker;

    MinecraftInstance* m_selectedInstance = nullptr;
    QString m_currentInstIcon;

    // managed by the application object
    Task* m_versionLoadTask = nullptr;
};
