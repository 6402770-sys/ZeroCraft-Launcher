// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the install review of Discover > Modpacks (docs/zero/CONTENT.md, "Modpacks" > "Install").
//
// Top to bottom: the pack header (icon, name, author, provider badge), the first decision "What do you want to do?" (two cards: New
// instance / Update an instance; Update is disabled while no instance manages the pack or the provider cannot update in place),
// the list of the instances that manage the pack (Update only), the version (newest release preselected; type pill, Minecraft and
// loader of the chosen one), "Keep my game settings" (Update only), the instance name, group, location (only with several instance
// folders) and the template switch (New only), then Cancel / Install (Update). In Update mode only the version is asked: the name,
// group and icon are the chosen instance's own.
//
// The choice is made here, once: the provider's task never asks again (ModpackInstall.h).
//
// The dialog builds the provider's task through `planInstall()` / `createInstallTask()` (ModpackInstall.h), exactly as Prism's
// classic pages do. The pack logo becomes the instance icon only when Install is pressed: nothing is downloaded or installed
// into the icon list before that. MainWindow::installModpack() runs the task while this dialog is alive (provider tasks parent
// their own dialogs to it).
#pragma once

#include <QDialog>
#include <QList>
#include <QPointer>
#include <QString>

#include <functional>
#include <optional>

#include "ui/zero/content/ModpacksSection.h"
#include "ui/zero/create/CreateInstanceDialog.h"
#include "zero/content/ModpackSource.h"

class InstanceTask;
class QCheckBox;
class QScrollArea;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QPushButton;

namespace Zero {
class RemoteImageCache;
}

namespace Zero::Content {

class PackIconWidget;
class InstallChoiceCard;

/// The instances that manage a modpack (BaseInstance::isManagedPack() with a provider id), as the installed state of the cards.
QList<InstalledPack> installedPacksFromApplication();

/// Everything the dialog needs from the application, so tests build it without APPLICATION or network.
struct ModpackInstallEnvironment {
    /// Groups, instance folders, the last used folder, the template switch and the settings (the Create Instance environment).
    Create::CreateInstanceEnvironment create;
    ModpackSource* source = nullptr;           ///< asked for the versions when the item does not carry them; may be null
    Zero::RemoteImageCache* images = nullptr;  ///< null = RemoteImageCache::instance()
    std::function<QList<InstalledPack>()> installedPacks;
    std::function<QString(const QString& instanceId)> instanceIconKey;  ///< Update: the instance keeps its icon
    std::function<QString(const QString& instanceId)> instanceGroup;    ///< Update: and its group
    /// Downloads the logo at `url` into a file called `fileName` (it must keep its image suffix) and calls `done` exactly once with
    /// the file's path, or an empty path on failure. Only called when Install is pressed.
    std::function<void(const QString& url, const QString& fileName, std::function<void(const QString& path)> done)> fetchLogo;
    /// IconList::installIcon(path, iconName).
    std::function<void(const QString& path, const QString& iconName)> installIcon;
    std::function<QNetworkAccessManager*()> network;  ///< Technic Solder installs

    /// The real thing; `source` is the Discover source of the pack's provider.
    static ModpackInstallEnvironment fromApplication(ModpackSource* source);
};

class ModpackInstallDialog : public QDialog {
    Q_OBJECT
   public:
    static constexpr int kWidth = 560;
    static constexpr quint64 kGeneration = 0x7a65726f696e7374ULL;  ///< the generation token of this dialog's version request
    static constexpr int kLogoTimeoutMs = 8000;                    ///< Install does not wait longer than this for the logo

    /// `updateInstanceId` non-empty = Update starts selected with that instance first in the list (ignored when no installed pack has
    /// that id). Without it Update is still preselected whenever an instance manages the pack; the first row is then the one played
    /// most recently. `initialGroup` is the group a new instance starts in (LastUsedGroupForNewInstance).
    ModpackInstallDialog(ModpackInstallEnvironment env, const ModpackItemPtr& item, const QString& updateInstanceId,
                         const QString& initialGroup, QWidget* parent = nullptr);
    ~ModpackInstallDialog() override;

    /// The task to run (the caller owns it), with name, original name + version, group, icon, target folder and template set as
    /// NewInstanceDialog::extractTask() sets them; nullptr when nothing can be installed.
    InstanceTask* extractTask();
    QString instName() const;
    QString instGroup() const;
    QString instDir() const;
    QString iconKey() const;
    bool copyTemplateDir() const;
    bool isUpdate() const { return m_updateMode; }
    /// The instance an Update changes (empty for a new instance).
    QString updateInstanceId() const;
    /// Update only: "Keep my game settings" is on (MainWindow saves and restores the files of ModpackKeep.h around the task).
    bool keepGameSettings() const;
    /// The version chosen, empty while none is.
    std::optional<ModpackVersion> selectedVersion() const;
    /// Install can be pressed: a version that can be installed, a valid name and a location.
    bool canInstall() const;
    /// Versions are in (or failed): the screenshot aid waits for it.
    bool isSettled() const;
    /// Calls `done` once the versions and the pack icon settled, or after `timeoutMs`.
    void callWhenSettled(const std::function<void()>& done, int timeoutMs);

    // ---- parts (tests, dev aids) ----
    QComboBox* versionBox() const { return m_versionBox; }
    QLineEdit* nameField() const { return m_name; }
    QComboBox* groupBox() const { return m_group; }
    QComboBox* locationBox() const { return m_location; }
    QCheckBox* templateBox() const { return m_template; }
    QPushButton* installButton() const { return m_install; }
    QPushButton* cancelButton() const { return m_cancel; }
    QPushButton* retryButton() const { return m_retry; }
    InstallChoiceCard* newCard() const { return m_newCard; }
    InstallChoiceCard* updateCard() const { return m_updateCard; }
    /// The rows of the instances an Update can target, in the order shown (empty when none manages the pack).
    QList<InstallChoiceCard*> instanceRows() const { return m_instanceRows; }
    /// The "Instance to update" list is on screen.
    bool instanceListVisible() const;
    QCheckBox* keepBox() const { return m_keep; }
    bool keepVisible() const;
    QString nameError() const;
    QString versionStatus() const;
    /// Texts of the pills under the version box (type, Minecraft, loaders).
    QStringList versionPills() const;
    QStringList headerPills() const;
    QString packName() const;
    QString packAuthor() const;
    bool isInstalling() const { return m_busy; }

   public slots:
    void accept() override;

   protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    enum class VersionState { Loading, Ready, Failed };

    void buildUi();
    void requestVersions();
    void setVersions(const QList<ModpackVersion>& versions);
    void setVersionState(VersionState state, const QString& error = {});
    void onVersionChanged();
    void refreshState();
    void applyMode();
    void buildInstanceRows();
    void finishAccept(const QString& logoPath);

    ModpackInstallEnvironment m_env;
    ModpackItemPtr m_item;
    QList<InstalledPack> m_managed;  ///< the instances that manage this pack, the one to start with first
    int m_selected = 0;              ///< index into m_managed of the instance an Update changes
    bool m_updateMode = false;
    QString m_iconKey = QStringLiteral("default");
    QList<ModpackVersion> m_versions;
    VersionState m_versionState = VersionState::Loading;
    bool m_nameEdited = false;
    bool m_nameSelectedOnce = false;
    bool m_busy = false;
    bool m_logoDone = false;

    PackIconWidget* m_packIcon = nullptr;
    QLabel* m_packName = nullptr;
    QLabel* m_packAuthor = nullptr;
    QWidget* m_headerPills = nullptr;
    InstallChoiceCard* m_newCard = nullptr;
    InstallChoiceCard* m_updateCard = nullptr;
    QWidget* m_instanceSection = nullptr;
    QWidget* m_instanceHost = nullptr;
    QScrollArea* m_instanceScroll = nullptr;
    QList<InstallChoiceCard*> m_instanceRows;
    QWidget* m_keepSection = nullptr;
    QCheckBox* m_keep = nullptr;
    QComboBox* m_versionBox = nullptr;
    QWidget* m_versionPills = nullptr;
    QLabel* m_versionStatus = nullptr;
    QPushButton* m_retry = nullptr;
    QWidget* m_nameColumn = nullptr;
    QLineEdit* m_name = nullptr;
    QLabel* m_nameError = nullptr;
    QWidget* m_groupRow = nullptr;
    QComboBox* m_group = nullptr;
    QWidget* m_locationColumn = nullptr;
    QComboBox* m_location = nullptr;
    QCheckBox* m_template = nullptr;
    QWidget* m_footer = nullptr;
    QPushButton* m_cancel = nullptr;
    QPushButton* m_install = nullptr;
};

}  // namespace Zero::Content
