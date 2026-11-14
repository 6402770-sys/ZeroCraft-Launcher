// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the Discover page (rail item after Library, docs/zero/CONTENT.md "Discover page" and docs/zero/CONTENT-MODS.md):
// browse content from Modrinth, CurseForge and Technic. Sections: Modpacks, Mods, Resource Packs and Shaders, chosen with the segmented
// control in the top bar (a section appears when the page was built with its environment); the choice is remembered in `ZeroDiscoverSection`. Top bar: title
// "Discover", subtitle = the current section's results caption.
//
// The page builds the production environments (provider sources over Prism's APIs, the installed packs of the instance list, the
// installed mods of the target, settings) and forwards the sections' requests to the main window. For Mods it also owns the TARGET
// instance rule: the Library's selection (a function the main window sets), else the remembered `ZeroDiscoverTarget`, else the most
// recently played mod-ready instance; instance list changes re-validate it.
#pragma once

#include <QList>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <functional>

#include "ui/zero/shell/ShellPage.h"
#include "zero/content/ModTarget.h"
#include "zero/content/ModpackSource.h"

class QPixmap;
class QStackedWidget;

namespace Zero {

namespace Create {
class MetaSource;
}

namespace Content {
class ContentSection;
class ModsSection;
class PacksSection;
class ShadersSection;
class WorldsSection;
class InstallCartModel;
class ModpacksSection;
class SegmentedControl;
class SelectionBar;
struct ModpacksEnvironment;
struct ModsEnvironment;
}  // namespace Content

/// What the page takes from the application besides the sections' own environments (tests inject fakes; the application page
/// fills them from settings and the instance list).
struct DiscoverHooks {
    std::function<QString()> loadSection;            ///< "modpacks" | "mods" | "resourcepacks" | "shaders" | "worlds" (ZeroDiscoverSection)
    std::function<void(const QString&)> saveSection;
    std::function<QString()> loadTarget;             ///< the instance id the mod browser targets (ZeroDiscoverTarget)
    std::function<void(const QString&)> saveTarget;
    std::function<QList<Content::TargetInstance>()> readTargets;  ///< the instances as targets (hand-built in tests)
};

class DiscoverPage : public ShellPage {
    Q_OBJECT
   public:
    /// The application page: Modrinth, CurseForge (only with `Application::SupportsFlame`), Technic, the instance list, settings.
    explicit DiscoverPage(QWidget* parent = nullptr);
    /// Modpacks only, with an explicit environment (tests); no Minecraft version loading, no switcher.
    DiscoverPage(const Content::ModpacksEnvironment& env, QWidget* parent);
    /// Modpacks and Mods with explicit environments and hooks (tests); no Minecraft version loading.
    DiscoverPage(const Content::ModpacksEnvironment& modpacks, const Content::ModsEnvironment& mods, const DiscoverHooks& hooks, QWidget* parent);
    /// Modpacks, Mods and Resource Packs (tests); `packs` is the environment of the Resource Packs section.
    DiscoverPage(const Content::ModpacksEnvironment& modpacks, const Content::ModsEnvironment& mods, const Content::ModsEnvironment& packs,
                 const DiscoverHooks& hooks, QWidget* parent);
    /// Modpacks, Mods, Resource Packs and Shaders (tests); `shaders` is the environment of the Shaders section.
    DiscoverPage(const Content::ModpacksEnvironment& modpacks, const Content::ModsEnvironment& mods, const Content::ModsEnvironment& packs,
                 const Content::ModsEnvironment& shaders, const DiscoverHooks& hooks, QWidget* parent);

    /// Modpacks, Mods, Resource Packs, Shaders and Worlds (tests); `worlds` is the environment of the Worlds section (CurseForge only).
    DiscoverPage(const Content::ModpacksEnvironment& modpacks, const Content::ModsEnvironment& mods, const Content::ModsEnvironment& packs,
                 const Content::ModsEnvironment& shaders, const Content::ModsEnvironment& worlds, const DiscoverHooks& hooks, QWidget* parent);

    QString title() const override { return tr("Discover"); }
    QString subtitle() const override { return m_subtitle; }
    QWidget* topBarActions() const override;

    /// The sections the page offers.
    static QStringList sectionIds() { return { QStringLiteral("modpacks"), QStringLiteral("mods") }; }
    /// The sections this page was built with (Resource Packs only when a packs environment was given), in switcher order.
    QStringList sections() const;
    QString currentSection() const;
    /// Shows `section` ("modpacks" | "mods" | "resourcepacks" | "shaders"; empty = the current one) and preselects `provider` ("modrinth" | "curseforge" | "technic" |
    /// empty) in it. The section is remembered.
    void showSection(const QString& section, const QString& provider = {});
    /// Shows the Mods section with the instance `instanceId` as its target (remembered in ZeroDiscoverTarget); an instance that cannot
    /// take mods (no loader) leaves the target as it was. Returns whether the instance is the target now.
    /// Mods section for `instanceId`; a non-empty `query` becomes its search text (after the target was chosen).
    bool showModsFor(const QString& instanceId, const QString& query = QString());
    /// The same for the Resource Packs section: `instanceId` becomes its target when it has a Minecraft version.
    bool showPacksFor(const QString& instanceId, const QString& query = QString());
    /// The same for the Shaders section.
    bool showShadersFor(const QString& instanceId, const QString& query = QString());
    /// The same for the Worlds section (false when the page has none).
    bool showWorldsFor(const QString& instanceId, const QString& query = QString());
    Content::ModpacksSection* modpacks() const { return m_modpacks; }
    Content::ModsSection* mods() const { return m_mods; }
    Content::PacksSection* packs() const { return m_packs; }
    Content::ShadersSection* shaders() const { return m_shaders; }
    /// Null unless the page was built with a Worlds environment (the application page: only with a CurseForge key).
    Content::WorldsSection* worlds() const { return m_worlds; }
    Content::ContentSection* current() const;
    Content::SegmentedControl* switcher() const { return m_switcher; }
    /// The install cart shared by the Mods, Resource Packs and Shaders sections (lives as long as the page) and the bar that shows it.
    Content::InstallCartModel* cart() const { return m_cart; }
    Content::SelectionBar* selectionBar() const { return m_bar; }
    /// The section whose target the cart's review adds to: the shown one when it has a target, else the first section that has one
    /// (null when no section does).
    Content::ModsSection* cartTargetSection() const;

    /// The id of the instance selected in the Library (empty = none); the main window sets it.
    void setLibrarySelection(const std::function<QString()>& selected) { m_librarySelection = selected; }
    /// Reads the instances again and re-validates the Mods target (the page does this on every instance list change). With
    /// `followLibrary` a Library selection that changed since the last look becomes the target.
    void refreshTargets(bool followLibrary = false);

    /// Dev aids (docs/zero/SHELL.md "Dev aids"): applies ZERO_SCREENSHOT_DISCOVER_SECTION / _QUERY / _PROVIDER / _SELECT / _FILTERS, waits
    /// until the first page and the pictures settled (12 s at most) and calls `ready`.
    void prepareScreenshot(const std::function<void()>& ready);
    /// Paints the popups that are separate windows (the filter panel) into a grab of `window`.
    void paintOverlays(QPixmap* shot, QWidget* window) const;

   signals:
    /// The Install / Update action of a pack: `updateInstanceId` empty = install as a new instance. The install dialog is wired by the
    /// main window.
    void installRequested(const Zero::Content::ModpackItemPtr& item, const QString& updateInstanceId);
    /// The Add to / Update action of a mod (the review dialog is the next task, nothing listens yet).
    void modInstallRequested(const Zero::Content::ContentItemPtr& item, const QString& targetInstanceId, const QString& installedFileId);
    /// "Create instance" of the Mods empty state: the main window opens Create Instance on Modded.
    /// The Add to / Update action of a resource pack (the review dialog is the main window's).
    void packInstallRequested(const Zero::Content::ContentItemPtr& item, const QString& targetInstanceId, const QString& installedFileId);
    void createInstanceRequested();
    /// "Create instance" of the Resource Packs empty state: the main window opens Create Instance on Vanilla.
    void createVanillaInstanceRequested();
    /// The Add to / Update action of a shader (the review dialog is the main window's).
    void shaderInstallRequested(const Zero::Content::ContentItemPtr& item, const QString& targetInstanceId, const QString& installedFileId);
    /// The Add action of a world (the dialog, the download and the install are the main window's; worlds have no "installed" state, so
    /// `installedFileId` is always empty).
    void worldInstallRequested(const Zero::Content::ContentItemPtr& item, const QString& targetInstanceId, const QString& installedFileId);
    /// "Find Iris" / "Find Oculus" of the shader notice: the main window opens Discover > Mods for `targetInstanceId` with `query`.
    void findShaderModRequested(const QString& targetInstanceId, const QString& query);
    /// "Review and add to <instance>" of the selection bar (the review dialog is the main window's).
    void cartReviewRequested();

   protected:
    void showEvent(QShowEvent* event) override;

   private:
    void build(const Content::ModpacksEnvironment& modpacks, const Content::ModsEnvironment* mods, const Content::ModsEnvironment* packs,
               const Content::ModsEnvironment* shaders, const Content::ModsEnvironment* worlds, const DiscoverHooks& hooks);
    Content::ModsSection* targeted(const QString& id) const;  // the Mods, Resource Packs or Shaders section of a section id
    QList<Content::ModsSection*> targetedSections() const;     // every section that has a target instance, in switcher order
    bool showFolderFor(const QString& sectionId, const QString& instanceId, const QString& query);
    void setCurrentSection(const QString& id, bool remember);
    void updateChrome();
    void updateCartBar();
    void setMinecraftVersions(const QStringList& ids);
    void loadMinecraftVersions();
    void requestMinecraftVersions();

    Content::ModpacksSection* m_modpacks = nullptr;
    Content::ModsSection* m_mods = nullptr;
    Content::PacksSection* m_packs = nullptr;
    Content::ShadersSection* m_shaders = nullptr;
    Content::WorldsSection* m_worlds = nullptr;
    Content::SegmentedControl* m_switcher = nullptr;
    Content::InstallCartModel* m_cart = nullptr;
    Content::SelectionBar* m_bar = nullptr;
    QStackedWidget* m_stack = nullptr;
    Create::MetaSource* m_meta = nullptr;
    DiscoverHooks m_hooks;
    std::function<QString()> m_librarySelection;
    QString m_lastLibrarySelection;
    QString m_subtitle;
    QTimer m_installedTimer;
    bool m_versionsRequested = false;
};

}  // namespace Zero
