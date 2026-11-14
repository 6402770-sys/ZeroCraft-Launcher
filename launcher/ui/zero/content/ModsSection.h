// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the Mods section of the Discover page (docs/zero/CONTENT-MODS.md, "Target instance" and "Discover > Mods").
//
// Everything kind-independent is ContentSection. What is Mods here: the TARGET INSTANCE (the instance mods are added to) and what
// follows from it - its Minecraft version and loaders are the section defaults (default chips) and the version filter of the
// Versions tab, CurseForge is offered only when it can serve the target's loaders, the installed mods (packwiz metadata of the
// target's mods folder) give the "Installed" pills, the facets and the primary action (Add to / Installed / Update). The mod card
// delegate is ModDelegate. Pressing the primary action only emits installRequested(): the install review (ModInstallDialog) is the main window's.
//
// Folder kinds: Resource Packs (Kind::ResourcePack) and Shaders (Kind::Shader) are the same section over another folder of the target
// (isFolderKind()): any instance with a Minecraft version is a target, searches carry no loader, the cards are PackDelegate gallery
// cards, the add dialog is the pack mode. Shaders add one thing: the notice above the results while the target has no shader mod.
// Worlds (Kind::World, CurseForge only) are the same section again with no default version chip (an older world opens in a newer game)
// and no cart: a world is a zip that Prism's InstallWorldTask unpacks into the target's saves folder (MainWindow::installWorld).
//
// Without a mod-ready instance the whole body is a ZeroEmptyState with "Create instance" (createInstanceRequested()).
#pragma once

#include <QList>
#include <QPointer>

#include <functional>

#include "ui/zero/content/ContentSection.h"
#include "ui/zero/content/TargetInstanceButton.h"
#include "zero/content/InstallCart.h"
#include "zero/content/InstalledMods.h"
#include "zero/content/ModTarget.h"
#include "zero/content/ShaderMod.h"

namespace Zero {
class ZeroEmptyState;
}

namespace Zero::Content {

class ModDelegate;
class ShaderModNotice;

struct ModsEnvironment : ContentEnvironment {
    /// Kind::Mod (default), Kind::ResourcePack (docs/zero/CONTENT-PACKS.md), Kind::Shader or Kind::World: the same section over another
    /// folder of the target. A pack or shader target needs only a Minecraft version (no loader); the filters send no loader; the cards are
    /// PackDelegate gallery cards.
    Kind kind = Kind::Mod;
    /// The installed mods of the target (not owned; null = nothing is ever installed).
    InstalledModsSource* installedMods = nullptr;
    /// Instance icons for the target control; empty = the launcher's icon list.
    TargetInstanceButton::IconProvider icons;
    /// Shaders: the file names of the target's mods folder (the shader mod check); null = read `TargetInstance::modsDir`.
    std::function<QStringList(const TargetInstance&)> modFiles;
    /// The install cart shared by the Mods, Resource Packs and Shaders sections (the page owns it; not owned here). Null = no cart: no
    /// check boxes, no "Add to cart" in the details panel.
    InstallCartModel* cart = nullptr;
};

class ModsSection : public ContentSection {
    Q_OBJECT
   public:
    static constexpr int kPrimaryNameMax = 160;  ///< px of the instance name in "Add to <name>"

    explicit ModsSection(const ModsEnvironment& env, QWidget* parent = nullptr);
    ~ModsSection() override;

    // ---- target ----
    /// The instances that can be chosen (mod-ready or not: the control's menu counts the others) and the one to use when the current
    /// target is gone or cannot take mods any more. The current target stays while it is still there and mod-ready (its data - name,
    /// running state - is refreshed); otherwise `preferredId` is used, otherwise there is no target.
    void setTargets(const QList<TargetInstance>& all, const QString& preferredId);
    /// Makes the instance `id` of the known targets the target (the user's pick or the page's choice); false when it is not mod-ready.
    bool chooseTarget(const QString& id);
    TargetInstance target() const { return m_target; }
    bool hasTarget() const { return !m_target.id.isEmpty(); }
    QList<TargetInstance> targets() const { return m_targets; }
    TargetInstanceButton* targetButton() const { return m_button; }
    ZeroEmptyState* noTargetState() const { return m_noTarget; }
    /// CurseForge has a source here and can serve the target's loaders (`FlameAPI::validateModLoaders`).
    bool curseForgeAvailable() const;
    /// The instances among the known targets that can take this kind (mod-ready; for packs: any with a Minecraft version).
    QList<TargetInstance> modReadyTargets() const;

    /// A section over a folder of files (Resource Packs, Shaders): same code as Mods, other kind.
    bool isFolderKind() const { return m_modsEnv.kind != Kind::Mod; }
    bool isShaders() const { return m_modsEnv.kind == Kind::Shader; }
    bool isWorlds() const { return m_modsEnv.kind == Kind::World; }
    /// Mods need a loader on the target, resource packs and shaders only a Minecraft version.
    bool needsLoader() const { return !isFolderKind(); }

    // ---- shaders: the target's shader mod ----
    /// What the target's mods folder says about a shader mod (empty facts outside Shaders and without a target).
    ShaderModFacts shaderMod() const { return m_shaderFacts; }
    /// The strip above the results (null outside Shaders).
    ShaderModNotice* shaderNotice() const { return m_notice; }
    /// Reads the target's mods folder again: the notice and the default Platform chip follow it (the section does this when the target
    /// changes and when it is shown).
    void refreshShaderMod();

    /// The mod card delegate; null in a Resource Packs / Shaders section (its delegate is a PackDelegate).
    ModDelegate* delegate() const;
    /// The installed mod of the item (provider + project id), if the target has it.
    std::optional<InstalledMod> installedFor(const ContentItem& item) const;
    /// Every installed mod of the target that has provider metadata (what the "Installed" states are read from).
    QList<InstalledMod> installedMods() const { return m_installed; }
    // ---- install cart ----
    InstallCartModel* cart() const { return m_modsEnv.cart; }
    /// The kind this section's items have in the cart (Mod, ResourcePack or Shader).
    Kind cartKind() const { return m_modsEnv.kind; }
    /// The item may be ticked: the cart exists, a target is known, and the item is not exactly installed. An installed item whose
    /// versions are loaded and whose newest installable version is not the installed one ("Update available") is eligible; an installed
    /// item whose versions are not known yet is not (its primary state is not known either).
    bool cartEligible(const ContentItem& item) const;
    bool inCart(const ContentItem& item) const;
    /// Ticks / unticks `item` (a ticked item can always be unticked); false when it is not eligible or the cart refused it (full).
    bool toggleCart(const ContentItemPtr& item);
    /// The same for the card in `row` of the shown results (dev aids, tests).
    bool toggleCartRow(int row);
    /// The delegate that paints the cards (a ModDelegate or a PackDelegate): its checkRect() is the check box's hit area.
    ContentDelegate* cardDelegate() const { return contentDelegate(); }
    /// "Not for 1.20.1 Fabric" when the item's data says it does not fit the target (and the filters did not already apply it); else empty.
    QString notForText(const ContentItem& item) const;
    /// Whether a version fits the target (Minecraft version and loader), the client-side rules of ModModel::checkVersionFilters.
    bool versionFits(const ContentVersion& version) const;

   signals:
    /// The primary action of the open mod: add it to `targetInstanceId`, or update `installedFileId` (empty = a new mod).
    void installRequested(const Zero::Content::ContentItemPtr& item, const QString& targetInstanceId, const QString& installedFileId);
    /// "Create instance" of the empty state.
    void createInstanceRequested();
    /// The user chose another instance in the control (the page remembers it in ZeroDiscoverTarget).
    void targetPicked(const QString& instanceId);
    /// The target (or its name / running state) was set again by setTargets() / chooseTarget(): the page's selection bar follows it.
    void targetChanged();
    /// Shaders: "Find Iris" / "Find Oculus" of the notice - the page opens Discover > Mods for `targetInstanceId` with `query`.
    void findModRequested(const QString& targetInstanceId, const QString& query);

   protected:
    FilterState sectionDefaults() const override;
    QList<Facet> sectionFacets(const FacetContext& ctx) const override;
    QSet<QString> readInstalledKeys() override;
    QList<ContentVersion> panelVersions(const ContentItem& item, const QList<ContentVersion>& versions) const override;
    DetailsHeader headerFor(const ContentItem& item) const override;
    void updatePrimaryAction() override;
    void onPrimaryAction() override;
    bool canSearch() const override { return hasTarget(); }
    void onSelectionChanged(const ContentItemPtr& item) override;

    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    static ContentSectionSpec makeSpec(Kind kind);
    static ModsEnvironment wrapped(const ModsEnvironment& env);
    void applyTarget(const TargetInstance& next);
    void updateEmptyState();
    ShaderModFacts readShaderMod(const TargetInstance& target) const;
    void updateShaderNotice();
    void applyPlatformDefault();
    VersionFilter versionFilter() const;
    bool updateAvailable(const ContentItem& item) const;
    void updateCartAction();
    void onCartChanged();
    bool cardHit(const QPoint& pos, QModelIndex* index, QRect* cell) const;

    ModsEnvironment m_modsEnv;
    QPointer<InstalledModsSource> m_source;
    TargetInstanceButton* m_button;
    ZeroEmptyState* m_noTarget;
    TargetInstance m_target;
    QList<TargetInstance> m_targets;
    QList<InstalledMod> m_installed;
    ShaderModNotice* m_notice = nullptr;
    ShaderModFacts m_shaderFacts;
    bool m_keyboardFocus = false;   ///< the last input on the results was a key (the focus ring is the keyboard's)
    bool m_swallowRelease = false;  ///< the press that toggled the cart is answered by a release nobody else should see
};

/// The Resource Packs section of the Discover page: a ModsSection over the target's `resourcepacks` folder.
class PacksSection : public ModsSection {
    Q_OBJECT
   public:
    explicit PacksSection(ModsEnvironment env, QWidget* parent = nullptr) : ModsSection(withPacks(std::move(env)), parent) {}

   private:
    static ModsEnvironment withPacks(ModsEnvironment env)
    {
        env.kind = Kind::ResourcePack;
        return env;
    }
};

/// The Shaders section of the Discover page: a ModsSection over the target's `shaderpacks` folder.
class ShadersSection : public ModsSection {
    Q_OBJECT
   public:
    explicit ShadersSection(ModsEnvironment env, QWidget* parent = nullptr) : ModsSection(withShaders(std::move(env)), parent) {}

   private:
    static ModsEnvironment withShaders(ModsEnvironment env)
    {
        env.kind = Kind::Shader;
        return env;
    }
};

/// The Worlds section of the Discover page: a ModsSection over CurseForge's world maps (no cart, no default version chip).
class WorldsSection : public ModsSection {
    Q_OBJECT
   public:
    explicit WorldsSection(ModsEnvironment env, QWidget* parent = nullptr) : ModsSection(withWorlds(std::move(env)), parent) {}

   private:
    static ModsEnvironment withWorlds(ModsEnvironment env)
    {
        env.kind = Kind::World;
        env.cart = nullptr;  // worlds are not cart-eligible
        return env;
    }
};

}  // namespace Zero::Content
