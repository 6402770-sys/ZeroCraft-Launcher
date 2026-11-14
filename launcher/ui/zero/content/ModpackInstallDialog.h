// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the install review of Discover > Modpacks (docs/zero/CONTENT.md, "Modpacks" > "Install").
//
// Top to bottom: the pack header (icon, name, author, provider badge), a note when an instance already manages the pack, the
// version (newest release preselected; type pill, Minecraft and loader of the chosen one), the instance name, group, location
// (only with several instance folders) and the template switch, then Cancel / Install. In Update mode (an instance manages the
// pack and the user pressed Update) only the version is asked: the name, group and icon are the instance's own.
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

    /// `updateInstanceId` non-empty = Update that instance (ignored when no installed pack has that id). `initialGroup` is the group a
    /// new instance starts in (LastUsedGroupForNewInstance).
    ModpackInstallDialog(ModpackInstallEnvironment env, const ModpackItemPtr& item, const QString& updateInstanceId,
                         const QString& initialGroup, QWidget* parent = nullptr);
    ~ModpackInstallDialog() override;

    /// The task to run (the caller owns it), with name, original name + version, group, icon, target folder and template set as
    /// NewInstanceDialog::extractTask() sets them; nullptr when nothing can be installed.
    InstanceTask* extractTask();
    QString instName() const;
    QString instGroup() const;
    QString instDir() const;
    QString iconKey() const { return m_iconKey; }
    bool copyTemplateDir() const;
    bool isUpdate() const { return m_updateMode; }
    /// The instance an Update changes (empty for a new instance).
    QString updateInstanceId() const;
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
    /// The "already installed" / "updates ..." note; empty text = hidden.
    QString noteText() const;
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
    void refreshIcons();
    void finishAccept(const QString& logoPath);

    ModpackInstallEnvironment m_env;
    ModpackItemPtr m_item;
    std::optional<InstalledPack> m_installed;  ///< the instance that manages this pack (the note, Update)
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
    QWidget* m_noteRow = nullptr;
    QLabel* m_noteIcon = nullptr;
    QLabel* m_note = nullptr;
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
