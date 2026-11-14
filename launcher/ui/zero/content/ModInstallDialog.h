// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the install review of Discover > Mods (docs/zero/CONTENT-MODS.md, "Mod install").
//
// Top to bottom: the mod header (icon, name, author, provider badge), "Adding to <instance icon> <instance> . 1.21.1 Fabric", a note
// while the target runs, the VERSION (the newest compatible release preselected; type, Minecraft, loader and file size of the chosen
// one) and the DEPENDENCIES block, then Cancel / the primary action ("Add mod", "Add 3 mods", "Update").
//
// The dependency check runs through an injectable `DependencyResolver` (ModInstall.h; the real one is GetModDependenciesTask run
// headless, ModDependencies.h). It starts with the dialog and again on every version change; each run carries a generation token, so
// the answer of a replaced version is dropped. Until the check settled the primary action is disabled; when it failed, the user may
// add the mod without its dependencies ("Add without dependencies", off by default). The resolver is a child of the dialog and is
// cancelled on destruction. Update mode (the target has an older version): the same dialog titled "Update <name>".
//
// The dialog only decides WHAT to install (`plan()`); MainWindow::installMod() builds and runs the download tasks.
#pragma once

#include <QDialog>
#include <QIcon>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>
#include <optional>

#include "zero/content/InstalledMods.h"
#include "zero/content/ModInstall.h"
#include "zero/content/ModTarget.h"
#include "zero/content/ModpackSource.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace Zero {
class RemoteImageCache;
}

namespace Zero::Content {

class DependencySkeleton;
class PackIconWidget;

/// Everything the dialog needs from the application, so tests build it without APPLICATION or network.
struct ModInstallEnvironment {
    ModpackSource* source = nullptr;           ///< asked for the versions when the item does not carry them; may be null
    Zero::RemoteImageCache* images = nullptr;  ///< null = RemoteImageCache::instance()
    /// Creates the dependency check as a child of `parent`; null = no check is possible (shown like "turned off").
    std::function<DependencyResolver*(QObject* parent)> makeResolver;
    /// False = the "ModDependenciesDisabled" setting is on: no check. Null = enabled.
    std::function<bool()> dependencyCheckEnabled;
    /// The installed mods of the target (matched with the version's required dependencies).
    std::function<QList<InstalledMod>()> installedMods;
    /// The instance icon for an icon key; null = the launcher's icon list.
    std::function<QIcon(const QString& iconKey)> instanceIcon;
    /// Whether the instance runs right now; null = TargetInstance::running.
    std::function<bool(const QString& instanceId)> isRunning;
    /// A resource pack (docs/zero/CONTENT-PACKS.md): no Dependencies block and no check, the versions are filtered by the Minecraft
    /// version alone (no loader), the texts say pack, the primary action is "Add pack" / "Update".
    bool packs = false;
    /// A shader (implies `packs`: a folder kind with no dependencies and no loader): the texts say shader, the primary action is
    /// "Add shader" / "Update", there is no "Turn it on" and a note says where the game picks the shader.
    bool shaders = false;
    /// A world map (CurseForge; implies `packs`): every file of the project is offered (no version filtering), the primary action is
    /// "Add world", there is no "Turn it on", a note says how the file's Minecraft version fits the target, and a file without a download
    /// URL turns the primary action into "Open on CurseForge".
    bool worlds = false;

    /// The real thing; `source` is the Discover source of the mod's provider, `installed` reads the Mods section's installed mods.
    /// `packs` / `shaders`: the folder kinds (a shader is also `packs`).
    static ModInstallEnvironment fromApplication(ModpackSource* source, std::function<QList<InstalledMod>()> installed, bool packs = false,
                                                 bool shaders = false, bool worlds = false);
};

class ModInstallDialog : public QDialog {
    Q_OBJECT
   public:
    static constexpr int kWidth = 560;
    static constexpr quint64 kGeneration = 0x7a65726f6d6f6473ULL;  ///< the generation token of this dialog's version request

    /// `installedFileId` non-empty = Update (the target has that file of the mod).
    ModInstallDialog(ModInstallEnvironment env, const ContentItemPtr& item, const TargetInstance& target, const QString& installedFileId,
                     QWidget* parent = nullptr);
    ~ModInstallDialog() override;

    bool isUpdate() const { return m_update; }
    const TargetInstance& target() const { return m_target; }
    /// The version chosen, empty while none is.
    std::optional<ModPlatform::IndexedVersion> selectedVersion() const;
    /// What Add installs now: the mod and the dependencies of the check (none when "Add without dependencies" is on).
    ModInstallPlan plan() const;
    /// The primary action can be pressed (a version, the check settled or skipped, the target is not running).
    bool canInstall() const;
    /// Versions and the dependency check are in (or failed), and the pictures are loaded: the screenshot aid waits for it.
    bool isSettled() const;
    /// Calls `done` once settled, or after `timeoutMs`.
    void callWhenSettled(const std::function<void()>& done, int timeoutMs);

    // ---- parts (tests, dev aids) ----
    QComboBox* versionBox() const { return m_versionBox; }
    QPushButton* primaryButton() const { return m_primary; }
    QPushButton* cancelButton() const { return m_cancel; }
    QPushButton* retryButton() const { return m_retry; }
    QCheckBox* skipDependenciesBox() const { return m_skip; }
    /// Resource packs: "Turn it on" (checked; only for Minecraft 1.13 and newer, where options.txt can say which packs are on).
    QCheckBox* turnOnBox() const { return m_turnOn; }
    QLabel* shaderNote() const { return m_shaderNote; }  ///< "Then pick it in the game's shader settings." (shaders only)
    /// Worlds: the line under the version ("Made for 1.21.4, newer than ...", "... upgrades it the first time you open it.", or "This
    /// world can only be downloaded on CurseForge."); empty = hidden.
    QString worldNoteText() const;
    QLabel* worldNote() const { return m_worldNote; }
    /// Worlds: the chosen file has no download URL: the primary action opens the project's page instead of adding.
    bool opensWebsite() const;
    /// Worlds: the project's page (http / https only; empty when it has none).
    QUrl websiteUrl() const;
    /// The pack is to be put on top of the game's pack list once it is added (the box is there and checked).
    bool turnItOn() const;
    QString modName() const;
    QString modAuthor() const;
    QStringList headerPills() const;
    /// Texts of the pills under the version box (type, Minecraft, loaders, file size).
    QStringList versionPills() const;
    /// "Adding to <instance> . 1.21.1 Fabric" as one string.
    QString targetLine() const;
    /// The note under the target line; empty = hidden.
    QString noteText() const;
    QString versionStatus() const;
    /// True while the dependency check runs (the skeleton rows and "Checking dependencies..." show).
    bool isChecking() const;
    /// The names of the dependency rows, in order.
    QStringList dependencyNames() const;
    /// The "Required by ..." text of each dependency row.
    QStringList dependencyRequiredBy() const;
    /// The status line of the block: "Checking dependencies...", "No other mods needed", the failure, "Dependency check is turned off in Settings".
    QString dependencyStatus() const;
    /// "Already installed: A, B"; empty = hidden.
    QString alreadyInstalledText() const;
    /// The dependency check failed (the Retry button and the checkbox show).
    bool dependencyCheckFailed() const { return m_depState == DepState::Failed; }

   public slots:
    void accept() override;

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    enum class VersionState { Loading, Ready, Failed };
    enum class DepState { Idle, Checking, Done, Failed, Disabled };

    void buildUi();
    void requestVersions();
    void setVersions(const QList<ModPlatform::IndexedVersion>& all);
    void setVersionState(VersionState state, const QString& message = {}, bool retryable = false);
    void onVersionChanged();
    void startDependencyCheck();
    void setDependencyState(DepState state, const QString& message = {});
    void onResolved(quint64 token, const QList<PlannedMod>& dependencies);
    void onResolveFailed(quint64 token, const QString& reason);
    void refreshState();
    void refreshWorldNote();
    void refreshIcons();
    bool targetRunning() const;

    ModInstallEnvironment m_env;
    ContentItemPtr m_item;
    TargetInstance m_target;
    bool m_update = false;
    QList<ModPlatform::IndexedVersion> m_versions;  ///< the compatible ones, in the provider's order
    VersionState m_versionState = VersionState::Loading;
    DepState m_depState = DepState::Idle;
    quint64 m_token = 0;  ///< generation of the dependency check: an answer for another token is dropped
    DependencyResolver* m_resolver = nullptr;
    QList<PlannedMod> m_deps;        ///< the dependencies that would be added
    QStringList m_alreadyInstalled;  ///< required dependencies the target already has

    PackIconWidget* m_icon = nullptr;
    QLabel* m_name = nullptr;
    QLabel* m_author = nullptr;
    QWidget* m_headerPills = nullptr;
    QLabel* m_targetIcon = nullptr;
    QLabel* m_targetCaption = nullptr;
    QLabel* m_targetName = nullptr;
    QLabel* m_targetVersion = nullptr;
    QWidget* m_noteRow = nullptr;
    QLabel* m_noteIcon = nullptr;
    QLabel* m_note = nullptr;
    QComboBox* m_versionBox = nullptr;
    QWidget* m_versionPills = nullptr;
    QLabel* m_versionStatus = nullptr;
    QLabel* m_shaderNote = nullptr;
    QLabel* m_worldNote = nullptr;
    QPushButton* m_versionRetry = nullptr;
    QWidget* m_depBlock = nullptr;
    QLabel* m_depStatus = nullptr;
    DependencySkeleton* m_depSkeleton = nullptr;
    QWidget* m_depRows = nullptr;
    QVBoxLayout* m_depRowsLayout = nullptr;
    QLabel* m_depInstalled = nullptr;
    QWidget* m_depFailure = nullptr;
    QPushButton* m_retry = nullptr;
    QCheckBox* m_skip = nullptr;
    QCheckBox* m_turnOn = nullptr;
    QWidget* m_footer = nullptr;
    QPushButton* m_cancel = nullptr;
    QPushButton* m_primary = nullptr;
};

}  // namespace Zero::Content
