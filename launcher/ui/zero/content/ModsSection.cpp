// SPDX-License-Identifier: GPL-3.0-only
#include "ModsSection.h"

#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QListView>
#include <QMouseEvent>
#include <QPushButton>

#include <algorithm>

#include "ModRows.h"
#include "ModpackRows.h"
#include "PackRows.h"
#include "ShaderModNotice.h"
#include "ui/zero/ZeroEmptyState.h"
#include "ui/zero/ZeroFormat.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/content/ContentToolbar.h"
#include "modplatform/flame/FlameAPI.h"
#include "zero/content/ModpackInstall.h"
#include "zero/content/WorldInstall.h"

namespace Zero::Content {

namespace {

/// The loader facet values ("fabric", ...) of the target's loaders: the four the filter knows.
QStringList loaderFacetValues(ModPlatform::ModLoaderTypes loaders)
{
    QStringList out;
    for (const auto& [flag, name] : { std::pair{ ModPlatform::Fabric, "fabric" }, std::pair{ ModPlatform::Forge, "forge" },
                                      std::pair{ ModPlatform::NeoForge, "neoforge" }, std::pair{ ModPlatform::Quilt, "quilt" } }) {
        if (loaders.testFlag(flag)) {
            out.append(QString::fromLatin1(name));
        }
    }
    return out;
}

/// The loaders named by lower-case loader names ("fabric", "quilt"); names Prism does not know are ignored.
ModPlatform::ModLoaderTypes loadersOfNames(const QStringList& names)
{
    ModPlatform::ModLoaderTypes loaders;
    for (const auto& name : names) {
        loaders |= ModPlatform::getModLoaderFromString(name);
    }
    return loaders;
}

}  // namespace

ContentSectionSpec ModsSection::makeSpec(Kind kind)
{
    ContentSectionSpec spec;
    spec.kind = kind;
    if (kind == Kind::ResourcePack || kind == Kind::Shader || kind == Kind::World) {
        // Resource Packs (docs/zero/CONTENT-PACKS.md), Shaders and Worlds: gallery cards, grid only, the galleries' reveal numbers.
        const bool shaders = kind == Kind::Shader;
        const bool worlds = kind == Kind::World;
        spec.reveal = RevealSpec::gallery();
        spec.objectName = worlds ? QStringLiteral("zeroWorldsSection") : shaders ? QStringLiteral("zeroShadersSection") : QStringLiteral("zeroPacksSection");
        spec.searchPlaceholder = worlds ? tr("Search worlds") : shaders ? tr("Search shaders") : tr("Search resource packs");
        spec.accessibleName = worlds ? tr("Worlds") : shaders ? tr("Shaders") : tr("Resource packs");
        spec.skeletonText = worlds ? tr("Loading worlds…") : shaders ? tr("Loading shaders…") : tr("Loading resource packs…");
        spec.skeletonShape = Zero::ZeroSkeleton::Shape::Gallery;
        spec.viewModes = false;
        spec.makeDelegate = [kind](Zero::RemoteImageCache* images, QObject* parent) { return new PackDelegate(images, kind, parent); };
        if (shaders) {
            spec.makeAccessory = [](Zero::RemoteImageCache*, QWidget* parent) { return new ShaderModNotice(parent); };
        }
        return spec;
    }
    spec.reveal = RevealSpec::mods();
    spec.objectName = QStringLiteral("zeroModsSection");
    spec.searchPlaceholder = tr("Search mods");
    spec.accessibleName = tr("Mods");
    spec.skeletonText = tr("Loading mods…");
    spec.compactCards = true;
    spec.makeDelegate = [](Zero::RemoteImageCache* images, QObject* parent) { return new ModDelegate(images, parent); };
    return spec;
}

ModsEnvironment ModsSection::wrapped(const ModsEnvironment& env)
{
    // The target's Minecraft version and loaders are the section defaults and follow the target: they are never persisted (a saved
    // 1.20.1 would be wrong for the next session's target).
    ModsEnvironment out = env;
    if (env.saveFilter) {
        auto save = env.saveFilter;
        out.saveFilter = [save, kind = env.kind](const QString& json) {
            auto doc = QJsonDocument::fromJson(json.toUtf8());
            if (!doc.isObject()) {
                save(json);
                return;
            }
            QJsonObject root = doc.object();
            QJsonObject selected = root.value(QStringLiteral("selected")).toObject();
            if (kind != Kind::World) {  // worlds have no default version: what the user picked stays
                selected.remove(FacetId::mcVersion);
            }
            selected.remove(FacetId::loader);
            if (kind == Kind::Shader) {
                selected.remove(FacetId::platform);  // the default Platform chip follows the target's shader mod, like the loader of Mods
            }
            root.insert(QStringLiteral("selected"), selected);
            save(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
        };
    }
    return out;
}

ModsSection::ModsSection(const ModsEnvironment& env, QWidget* parent)
    : ContentSection(wrapped(env), makeSpec(env.kind), parent), m_modsEnv(env), m_source(env.installedMods)
{
    m_button = new TargetInstanceButton(this, env.icons);
    m_button->setNeedsLoader(needsLoader());
    toolbar()->setLeadingWidget(m_button);
    connect(m_button, &TargetInstanceButton::targetChosen, this, [this](const QString& id) {
        if (id != m_target.id && chooseTarget(id)) {
            emit targetPicked(id);
        }
    });

    if (isFolderKind()) {
        m_noTarget = new ZeroEmptyState(QStringLiteral("blocks"), tr("No instance yet"),
                                        isWorlds()    ? tr("Worlds need an instance.")
                                        : isShaders() ? tr("Shaders need an instance.")
                                                      : tr("Resource packs need an instance."),
                                        tr("Create instance"), this);
    } else {
        m_noTarget = new ZeroEmptyState(QStringLiteral("blocks"), tr("No instance can take mods yet"),
                                        tr("Mods need an instance with Fabric, Forge, NeoForge or Quilt."), tr("Create instance"), this);
    }
    m_noTarget->hide();
    connect(m_noTarget, &ZeroEmptyState::actionTriggered, this, &ModsSection::createInstanceRequested);
    if (auto* row = qobject_cast<QHBoxLayout*>(layout())) {
        row->insertWidget(0, m_noTarget, 1);
    }

    if (m_source) {
        connect(m_source, &InstalledModsSource::changed, this, [this]() { refreshInstalled(); });
    }
    m_notice = qobject_cast<ShaderModNotice*>(accessory());
    if (m_notice) {
        connect(m_notice, &ShaderModNotice::findRequested, this, [this](const QString& query) { emit findModRequested(m_target.id, query); });
    }
    if (auto* mods = delegate()) {
        mods->setFitCheck([this](const ContentItem& item) { return notForText(item); });
    }
    if (InstallCartModel* installCart = m_modsEnv.cart) {
        CartHooks hooks;
        hooks.eligible = [this](const ContentItem& item) { return cartEligible(item); };
        hooks.inCart = [this](const ContentItem& item) { return inCart(item); };
        hooks.anyInCart = [installCart]() { return !installCart->isEmpty(); };
        hooks.keyboardFocus = [this]() { return m_keyboardFocus; };
        contentDelegate()->setCartHooks(hooks);
        connect(installCart, &InstallCartModel::changed, this, &ModsSection::onCartChanged);
        connect(panel(), &ContentDetailsPanel::cartToggleRequested, this, [this]() { toggleCart(selectedItem()); });
        view()->viewport()->installEventFilter(this);
        view()->installEventFilter(this);
    }
    init();
    updateEmptyState();
}

ModsSection::~ModsSection()
{
    if (m_source) {
        m_source->setActive(false);
    }
}

ModDelegate* ModsSection::delegate() const
{
    return qobject_cast<ModDelegate*>(contentDelegate());
}

// ---- target ----

bool ModsSection::curseForgeAvailable() const
{
    return environment().sources.has(Provider::CurseForge) && hasTarget() && (!needsLoader() || FlameAPI::validateModLoaders(m_target.loaders));
}

QList<TargetInstance> ModsSection::modReadyTargets() const
{
    QList<TargetInstance> out;
    for (const auto& t : m_targets) {
        if (t.readyFor(needsLoader())) {
            out.append(t);
        }
    }
    return out;
}

void ModsSection::setTargets(const QList<TargetInstance>& all, const QString& preferredId)
{
    m_targets = all;
    m_button->setTargets(all);
    const auto find = [&all, this](const QString& id) -> const TargetInstance* {
        if (id.isEmpty()) {
            return nullptr;
        }
        for (const auto& t : all) {
            if (t.id == id && t.readyFor(needsLoader())) {
                return &t;
            }
        }
        return nullptr;
    };
    const TargetInstance* next = find(m_target.id);
    if (!next) {
        next = find(preferredId);
    }
    applyTarget(next ? *next : TargetInstance());
}

bool ModsSection::chooseTarget(const QString& id)
{
    for (const auto& t : m_targets) {
        if (t.id == id && t.readyFor(needsLoader())) {
            applyTarget(t);
            return true;
        }
    }
    return false;
}

VersionFilter ModsSection::versionFilter() const
{
    VersionFilter filter;
    if (hasTarget() && !isWorlds()) {  // a world of any version can be added: the Versions tab and the add dialog list every file
        filter.mcVersions = { m_target.mcVersion };
        if (needsLoader()) {
            filter.loaders = m_target.loaders;  // Modrinth resource packs carry the loader "minecraft": never send a mod loader
        }
    }
    return filter;
}

void ModsSection::applyTarget(const TargetInstance& next)
{
    const TargetInstance previous = m_target;
    m_target = next;
    m_button->setCurrentId(next.id);
    updateEmptyState();

    const bool contextChanged =
        previous.id != next.id || previous.mcVersion != next.mcVersion || (needsLoader() && previous.loaders != next.loaders);
    if (!contextChanged) {
        refreshShaderMod();     // the target's mods may have changed (a rename or the running state reach this too)
        updateShaderNotice();   // ... and so may its name
        updatePrimaryAction();  // a rename or the running state: the action's text and note
        emit targetChanged();
        return;
    }
    if (isShaders()) {
        m_shaderFacts = hasTarget() ? readShaderMod(next) : ShaderModFacts();  // before the defaults, which hold its Platform
        updateShaderNotice();
    }

    if (m_source) {
        m_source->setInstance(next.id);
        m_source->setActive(isVisible() && next.id.size() > 0);
    }
    setVersionFilter(versionFilter());
    refreshDefaults();  // the target's version and loaders are the defaults; the Provider facet follows its loaders

    // New defaults replace the old ones: the version and loader chips are the target's again, the other facets stay.
    FilterState state = this->state();
    if (!isWorlds()) {
        state.selected.remove(FacetId::mcVersion);  // worlds: the user's version chip is not the target's
    }
    state.selected.remove(FacetId::loader);
    state.selected.remove(FacetId::updates);
    const FilterState defaults = this->defaults();
    if (isShaders()) {
        state.selected.remove(FacetId::platform);
    }
    for (const QString& id : { FacetId::mcVersion, FacetId::loader, FacetId::platform }) {
        if (const QStringList values = defaults.values(id); !values.isEmpty()) {
            state.selected.insert(id, values);
        }
    }
    if (state.provider() == Provider::CurseForge && !curseForgeAvailable()) {
        state.selected.insert(FacetId::provider, { providerId(Provider::Modrinth) });
    }
    applyState(state);  // searches when a server facet changed (and the section is running)
    if (isVisible()) {
        start();  // the first target of a section that waited for one
    }
    refreshInstalled();
    if (ContentItemPtr open = selectedItem()) {
        selectItem(open);  // its versions are asked again under the new filter
    }
    view()->viewport()->update();  // the cart's check boxes follow the target
    emit targetChanged();
}

ShaderModFacts ModsSection::readShaderMod(const TargetInstance& target) const
{
    return shaderModFacts(m_modsEnv.modFiles ? m_modsEnv.modFiles(target) : modFileNamesIn(target.modsDir));
}

void ModsSection::updateShaderNotice()
{
    if (m_notice) {
        m_notice->setNotice(hasTarget() ? shaderNoticeFor(m_target.name, m_target.loaders, m_shaderFacts) : ShaderNotice());
    }
}

void ModsSection::applyPlatformDefault()
{
    refreshDefaults();
    FilterState state = this->state();
    state.selected.remove(FacetId::platform);
    if (const QStringList platforms = defaults().values(FacetId::platform); !platforms.isEmpty()) {
        state.selected.insert(FacetId::platform, platforms);
    }
    applyState(state);
}

void ModsSection::refreshShaderMod()
{
    if (!isShaders()) {
        return;
    }
    const ShaderModFacts next = hasTarget() ? readShaderMod(m_target) : ShaderModFacts();
    const bool platformsChanged = next.platforms != m_shaderFacts.platforms;
    m_shaderFacts = next;
    updateShaderNotice();
    if (platformsChanged) {
        applyPlatformDefault();
    }
}

void ModsSection::updateEmptyState()
{
    const bool none = !hasTarget();
    m_noTarget->setVisible(none);
    column()->setVisible(!none);
    if (none) {
        selectItem(nullptr);
    }
}

// ---- visibility: the target's mods folder is watched only while the section is shown ----

void ModsSection::showEvent(QShowEvent* event)
{
    ContentSection::showEvent(event);
    if (m_source) {
        m_source->setInstance(m_target.id);
        m_source->setActive(hasTarget());
    }
    refreshShaderMod();  // the user may have added or removed a shader mod since the section was last shown
}

void ModsSection::hideEvent(QHideEvent* event)
{
    ContentSection::hideEvent(event);
    if (m_source) {
        m_source->setActive(false);
    }
}

// ---- hooks of the core ----

void ModsSection::onSelectionChanged(const ContentItemPtr& item)
{
    m_button->setCompact(item != nullptr);  // the details panel narrows the toolbar: icon, name, chevron
}

FilterState ModsSection::sectionDefaults() const
{
    FilterState defaults;
    // Worlds are CurseForge only; every other kind starts on Modrinth.
    defaults.selected.insert(FacetId::provider, { providerId(isWorlds() ? Provider::CurseForge : Provider::Modrinth) });
    if (hasTarget() && !isWorlds()) {  // no default version chip on worlds: an older world opens in a newer game
        if (!m_target.mcVersion.isEmpty()) {
            defaults.selected.insert(FacetId::mcVersion, { m_target.mcVersion });
        }
        if (const QStringList loaders = loaderFacetValues(m_target.loaders); needsLoader() && !loaders.isEmpty()) {
            defaults.selected.insert(FacetId::loader, loaders);
        }
        if (isShaders() && !m_shaderFacts.platforms.isEmpty()) {
            defaults.selected.insert(FacetId::platform, m_shaderFacts.platforms);  // the platform of the target's shader mod
        }
    }
    return defaults;
}

QList<Facet> ModsSection::sectionFacets(const FacetContext& ctx) const
{
    FacetContext c = ctx;
    c.hasTarget = hasTarget();
    QList<Facet> facets = facetsFor(m_modsEnv.kind, c);
    const ModpackSourceSet& sources = environment().sources;
    if (isShaders() && sources.has(Provider::CurseForge) &&
        std::none_of(facets.cbegin(), facets.cend(), [](const Facet& f) { return f.id == FacetId::provider; })) {
        // The filter core leaves the Provider facet out of shaders (the platform data exists on Modrinth only); with CurseForge in
        // the build the choice is offered, and CurseForge's missing platform / performance data is told on those facets.
        Facet provider;
        provider.id = FacetId::provider;
        provider.label = tr("Provider");
        provider.kind = FacetKind::Single;
        provider.side = FacetSide::Server;
        provider.options = { { providerId(Provider::Modrinth), providerName(Provider::Modrinth), QString() },
                             { providerId(Provider::CurseForge), providerName(Provider::CurseForge), QString() } };
        facets.prepend(provider);
    }
    for (auto it = facets.begin(); it != facets.end();) {
        if (it->id == FacetId::updates) {
            it = facets.erase(it);  // "Update available" waits for the installed manager's update data: hidden, not stubbed
            continue;
        }
        if (it->id == FacetId::environment && ctx.provider == Provider::CurseForge) {
            it->disabledReason = tr("CurseForge has no environment filter");  // CONTENT-MODS.md's wording
        }
        if (it->id == FacetId::provider) {
            QList<FacetOption> kept;
            for (const auto& option : std::as_const(it->options)) {
                const auto p = providerFromId(option.value);
                if (p && sources.has(*p)) {
                    kept.append(option);
                }
            }
            it->options = kept;
            // CurseForge is there but cannot serve the target's loaders: the choice is shown disabled, with the reason.
            if (needsLoader() && sources.has(Provider::CurseForge) && hasTarget() && !curseForgeAvailable()) {
                it->disabledReason = tr("CurseForge has no mods for %1").arg(m_target.loaderText.isEmpty() ? tr("this loader") : m_target.loaderText);
            }
        }
        ++it;
    }
    return facets;
}

QSet<QString> ModsSection::readInstalledKeys()
{
    m_installed = m_source ? m_source->mods() : QList<InstalledMod>();
    QSet<QString> keys;
    for (const auto& mod : std::as_const(m_installed)) {
        keys.insert(mod.key);
    }
    return keys;
}

std::optional<InstalledMod> ModsSection::installedFor(const ContentItem& item) const
{
    const QString key = installedKey(item);
    for (const auto& mod : m_installed) {
        if (mod.key == key) {
            return mod;
        }
    }
    return std::nullopt;
}

bool ModsSection::versionFits(const ContentVersion& version) const
{
    if (!hasTarget() || isWorlds()) {
        return true;
    }
    if (!m_target.mcVersion.isEmpty() && !version.gameVersions.isEmpty() && !version.gameVersions.contains(m_target.mcVersion)) {
        return false;
    }
    if (!needsLoader()) {
        return true;  // resource packs: the Minecraft version alone decides
    }
    const ModPlatform::ModLoaderTypes loaders = loadersOfNames(version.loaders);
    if (m_target.loaders != ModPlatform::ModLoaderTypes() && loaders != ModPlatform::ModLoaderTypes() &&
        !(loaders & m_target.loaders)) {
        return false;
    }
    return true;
}

QList<ContentVersion> ModsSection::panelVersions(const ContentItem&, const QList<ContentVersion>& versions) const
{
    QList<ContentVersion> out;
    for (const auto& version : versions) {
        if (versionFits(version)) {
            out.append(version);
        }
    }
    return out;
}

QString ModsSection::notForText(const ContentItem& item) const
{
    if (!hasTarget() || isWorlds()) {
        return {};
    }
    const FilterState s = state();
    // A filter that holds the target's value was applied by the provider: the item fits (CurseForge's lists are partial).
    const bool versionApplied = s.isSelected(FacetId::mcVersion, m_target.mcVersion);
    bool loaderApplied = false;
    for (const auto& name : loaderFacetValues(m_target.loaders)) {
        loaderApplied = loaderApplied || s.isSelected(FacetId::loader, name);
    }
    bool misfit = false;
    if (!versionApplied && !m_target.mcVersion.isEmpty() && !item.gameVersions.isEmpty() && !item.gameVersions.contains(m_target.mcVersion)) {
        misfit = true;
    }
    const ModPlatform::ModLoaderTypes loaders = loadersOfNames(item.loaderNames);
    if (needsLoader() && !loaderApplied && m_target.loaders != ModPlatform::ModLoaderTypes() && loaders != ModPlatform::ModLoaderTypes() &&
        !(loaders & m_target.loaders)) {
        misfit = true;
    }
    return misfit ? tr("Not for %1").arg(m_target.summaryFor(needsLoader())) : QString();
}

DetailsHeader ModsSection::headerFor(const ContentItem& item) const
{
    DetailsHeader h;
    h.key = item.key();
    h.name = item.name;
    h.author = item.author;
    h.iconUrl = item.iconUrl;
    h.placeholderIcon = isWorlds() ? QStringLiteral("earth") : isFolderKind() ? QStringLiteral("package") : QStringLiteral("blocks");
    // No hero picture unless the project has a featured gallery image (a pack: its featured image, else its first one).
    if (item.pack) {
        QString featured = item.pack->featuredImageUrl;
        for (const auto& image : item.pack->gallery) {
            if (featured.isEmpty() && image.featured) {
                featured = image.url;
            }
        }
        h.artworkUrl = featured;
    }
    if (isFolderKind() && h.artworkUrl.isEmpty()) {
        h.artworkUrl = item.artworkUrl;
    }
    h.compactHero = h.artworkUrl.isEmpty();
    h.providerLabel = providerBadge(item.provider);
    h.websiteUrl = item.websiteUrl;
    if (isWorlds()) {
        h.pills << worldPills(item);  // categories and "Made for 1.20.1"; no loaders, no environment, no resolutions
    } else if (const QString range = minecraftRangeText(item.gameVersions); !range.isEmpty()) {
        h.pills << range;
    }
    if (isWorlds()) {
        // nothing more
    } else if (isShaders()) {
        h.pills << PackDelegate::shaderPills(item);  // no loaders, no environment: the performance label and the platforms
    } else if (isFolderKind()) {
        h.pills << resolutionsOf(item.categoryIds);  // no loaders, no environment: the resolutions
    } else {
        h.pills << loaderLabels(item.loaderNames);
        if (const QString env = environmentLabel(item); !env.isEmpty()) {
            h.pills << env;
        }
    }
    if (item.downloads >= 0) {
        h.stats.append({ QStringLiteral("download"), tr("%1 downloads").arg(formatCount(item.downloads)), tr("Downloads") });
    }
    if (item.pack && item.pack->follows >= 0) {
        h.stats.append({ QStringLiteral("heart"), tr("%1 follows").arg(formatCount(item.pack->follows)), tr("Follows") });
    }
    if (item.updated.isValid()) {
        h.stats.append({ QStringLiteral("clock"), tr("Updated %1").arg(Format::relativeTime(item.updated.toMSecsSinceEpoch()).toLower()),
                         tr("Last updated") });
    }
    if (item.pack) {
        if (const QString license = licenseText(item.pack->license); !license.isEmpty()) {
            h.stats.append({ QStringLiteral("file-text"), license, tr("License") });
        }
    }
    return h;
}

void ModsSection::onPrimaryAction()
{
    const ContentItemPtr item = selectedItem();
    if (!item || !hasTarget()) {
        return;
    }
    const auto installed = installedFor(*item);
    emit installRequested(item, m_target.id, installed ? installed->fileId : QString());
}

void ModsSection::updatePrimaryAction()
{
    const ContentItemPtr selected = selectedItem();
    if (!selected) {
        return;
    }
    const ContentItem& item = *selected;
    QPushButton* button = panel()->primaryButton();
    const QFontMetrics metrics(button->font());
    const QString name = metrics.elidedText(m_target.name, Qt::ElideRight, kPrimaryNameMax);
    QString text = hasTarget() ? tr("Add to %1").arg(name) : tr("Add");
    QString note;
    bool enabled = false;
    bool upToDate = false;

    if (isWorlds()) {
        // A world has no "installed" state (adding twice makes two copies, by design) and the dialog reads the file list itself: the
        // action is there as soon as there is a target that does not run.
        if (!hasTarget()) {
            note = tr("Choose an instance to add worlds to.");
        } else if (m_target.running) {
            note = tr("Close %1 to add worlds").arg(m_target.name);
        } else {
            enabled = true;
        }
        Style::setProperty(button, "zeroInstalled", false);
        panel()->setPrimaryAction(text, enabled, note);
        return;
    }

    // The nouns of the three kinds.
    const auto kindText = [this](const QString& mod, const QString& pack, const QString& shader) {
        return isShaders() ? shader : isFolderKind() ? pack : mod;
    };
    const bool versionsKnown = hasVersions(item.key());
    const QList<ContentVersion> versions = panelVersions(item, versionsOf(item.key()));
    const auto installed = installedFor(item);
    int newest = -1;  // the newest installable version
    for (int i = 0; i < versions.size(); ++i) {
        if (isInstallable(versions.at(i), item.provider)) {
            newest = i;
            break;
        }
    }
    const int release = versionsKnown ? defaultVersionIndex(versions, item.provider) : -1;

    if (!hasTarget()) {
        note = kindText(tr("Choose an instance to add mods to."), tr("Choose an instance to add resource packs to."),
                        tr("Choose an instance to add shaders to."));
    } else if (versionsKnown && newest < 0) {
        note = kindText(tr("No version of this mod fits %1."), tr("No version of this pack fits %1."), tr("No version of this shader fits %1."))
                   .arg(m_target.summaryFor(needsLoader()));
    } else if (versionsKnown) {
        const QString installedId = installed ? installed->fileId : QString();
        if (installed && !installedId.isEmpty() && (installedId == versions.at(newest).id || (release >= 0 && installedId == versions.at(release).id))) {
            text = tr("Installed");
            upToDate = true;
            note = kindText(tr("%1 has the newest version of this mod."), tr("%1 has the newest version of this pack."),
                            tr("%1 has the newest version of this shader."))
                       .arg(m_target.name);
        } else if (m_target.running) {
            note = kindText(tr("Close %1 to add mods"), tr("Close %1 to add packs"), tr("Close %1 to add shaders")).arg(m_target.name);
            if (installed) {
                text = tr("Update");
            }
        } else if (installed) {
            text = tr("Update");
            enabled = true;
            note = tr("%1 has %2 installed. The newest version is %3.")
                       .arg(m_target.name, installed->version.isEmpty() ? tr("an older version") : installed->version, versions.at(newest).name);
        } else {
            enabled = true;
        }
    } else if (m_target.running) {
        note = kindText(tr("Close %1 to add mods"), tr("Close %1 to add packs"), tr("Close %1 to add shaders")).arg(m_target.name);
    }
    Style::setProperty(button, "zeroInstalled", upToDate);
    panel()->setPrimaryAction(text, enabled, note);
    updateCartAction();
}

// ---- install cart ----

bool ModsSection::updateAvailable(const ContentItem& item) const
{
    // The same rules as the primary action's "Update": installed, versions loaded, an installable version exists and the installed
    // file is neither the newest nor the default release.
    const auto installed = installedFor(item);
    if (!installed || !hasVersions(item.key())) {
        return false;
    }
    const QList<ContentVersion> versions = panelVersions(item, versionsOf(item.key()));
    int newest = -1;
    for (int i = 0; i < versions.size(); ++i) {
        if (isInstallable(versions.at(i), item.provider)) {
            newest = i;
            break;
        }
    }
    if (newest < 0) {
        return false;
    }
    const int release = defaultVersionIndex(versions, item.provider);
    const QString installedId = installed->fileId;
    const bool upToDate =
        !installedId.isEmpty() && (installedId == versions.at(newest).id || (release >= 0 && installedId == versions.at(release).id));
    return !upToDate;
}

bool ModsSection::cartEligible(const ContentItem& item) const
{
    if (!m_modsEnv.cart || !hasTarget() || !InstallCart::takesKind(m_modsEnv.kind) || item.id.isEmpty()) {
        return false;
    }
    return !installedFor(item) || updateAvailable(item);
}

bool ModsSection::inCart(const ContentItem& item) const
{
    return m_modsEnv.cart && m_modsEnv.cart->contains(m_modsEnv.kind, item);
}

bool ModsSection::toggleCart(const ContentItemPtr& item)
{
    if (!item || !m_modsEnv.cart || (!inCart(*item) && !cartEligible(*item))) {
        return false;
    }
    const auto result = m_modsEnv.cart->toggle(m_modsEnv.kind, item);
    return result == InstallCart::ToggleResult::Added || result == InstallCart::ToggleResult::Removed;
}

bool ModsSection::toggleCartRow(int row)
{
    return toggleCart(model()->item(row));
}

void ModsSection::updateCartAction()
{
    const ContentItemPtr selected = selectedItem();
    if (!selected || !m_modsEnv.cart) {
        return;
    }
    const bool in = inCart(*selected);
    panel()->setCartAction(in || cartEligible(*selected), in);
}

void ModsSection::onCartChanged()
{
    view()->viewport()->update();
    updateCartAction();
}

bool ModsSection::cardHit(const QPoint& pos, QModelIndex* index, QRect* cell) const
{
    const QModelIndex hit = view()->indexAt(pos);
    if (!hit.isValid()) {
        return false;
    }
    const QRect visual = view()->visualRect(hit);
    if (!contentDelegate()->cardRect(visual).contains(pos)) {
        return false;  // the gap under a card is not the card
    }
    *index = hit;
    *cell = visual;
    return true;
}

bool ModsSection::eventFilter(QObject* watched, QEvent* event)
{
    if (!m_modsEnv.cart || (watched != view() && watched != view()->viewport())) {
        return ContentSection::eventFilter(watched, event);
    }
    switch (event->type()) {
        case QEvent::KeyPress: {
            const auto* key = static_cast<QKeyEvent*>(event);
            if (watched != view() || key->key() == Qt::Key_Shift || key->key() == Qt::Key_Control || key->key() == Qt::Key_Alt) {
                break;
            }
            if (!m_keyboardFocus) {
                m_keyboardFocus = true;
                view()->viewport()->update();
            }
            // Space ticks the focused card (the view's own Space would only select it again).
            if (key->key() == Qt::Key_Space && (key->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
                const QModelIndex current = view()->currentIndex();
                if (current.isValid()) {
                    if (!key->isAutoRepeat()) {
                        toggleCart(model()->item(current.row()));
                    }
                    return true;
                }
            }
            break;
        }
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick: {
            if (watched != view()->viewport()) {
                break;
            }
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if (m_keyboardFocus) {
                m_keyboardFocus = false;
                view()->viewport()->update();
            }
            if (mouse->button() != Qt::LeftButton) {
                break;
            }
            QModelIndex index;
            QRect cell;
            const QPoint pos = mouse->position().toPoint();
            if (!cardHit(pos, &index, &cell)) {
                break;
            }
            const ContentItemPtr item = model()->item(index.row());
            if (!item || (!inCart(*item) && !cartEligible(*item))) {
                break;  // nothing to tick here: a plain click, the details open as always
            }
            const bool onCheck = contentDelegate()->checkRect(cell).contains(pos);
            if (!onCheck && !(mouse->modifiers() & Qt::ControlModifier)) {
                break;
            }
            toggleCart(item);  // a quick second click (a double click event) ticks again, like a check box
            m_swallowRelease = true;
            return true;
        }
        case QEvent::MouseButtonRelease: {
            if (watched == view()->viewport() && m_swallowRelease && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
                m_swallowRelease = false;
                return true;
            }
            break;
        }
        default:
            break;
    }
    return ContentSection::eventFilter(watched, event);
}

}  // namespace Zero::Content
