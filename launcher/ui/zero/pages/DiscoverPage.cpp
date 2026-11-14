// SPDX-License-Identifier: GPL-3.0-only
#include "DiscoverPage.h"

#include <QDebug>
#include <QPainter>
#include <QPixmap>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "Application.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "settings/SettingsObject.h"
#include "ui/zero/content/ContentDetailsPanel.h"
#include "ui/zero/content/FilterPanel.h"
#include "ui/zero/content/ModpackInstallDialog.h"
#include "ui/zero/content/ModpacksSection.h"
#include "ui/zero/content/ModsSection.h"
#include "ui/zero/content/SelectionBar.h"
#include "ui/zero/create/CreateMetaSource.h"
#include "ui/zero/ZeroToast.h"
#include "ui/zero/ZeroTokens.h"
#include "zero/content/InstallCart.h"
#include "zero/content/CreateInstanceModel.h"
#include "zero/content/InstalledMods.h"
#include "zero/content/ModpackSources.h"

namespace Zero {

namespace {
constexpr const char* kModpacks = "modpacks";
constexpr const char* kMods = "mods";
constexpr const char* kPacks = "resourcepacks";
constexpr const char* kShaders = "shaders";
constexpr const char* kWorlds = "worlds";
}  // namespace

DiscoverPage::DiscoverPage(QWidget* parent) : ShellPage(parent)
{
    // ----- Modpacks -----
    Content::ModpacksEnvironment env;
    env.sources.modrinth = new Content::ModrinthSource(this);
    // No CurseForge anywhere without its capability (API key).
    const bool flame = APPLICATION->capabilities() & Application::SupportsFlame;
    if (flame) {
        env.sources.curseforge = new Content::CurseForgeSource(this);
    }
    env.sources.technic = new Content::TechnicSource(this);
    env.installedPacks = &Content::installedPacksFromApplication;
    env.loadFilter = []() { return APPLICATION->settings()->get("ZeroDiscoverModpacksFilter").toString(); };
    env.saveFilter = [](const QString& json) { APPLICATION->settings()->set("ZeroDiscoverModpacksFilter", json); };
    env.loadView = []() { return APPLICATION->settings()->get("ZeroDiscoverModpacksView").toString(); };
    env.saveView = [](const QString& view) { APPLICATION->settings()->set("ZeroDiscoverModpacksView", view); };

    // ----- Mods: no Technic; the installed mods are the target's packwiz metadata -----
    Content::ModsEnvironment mods;
    mods.sources.modrinth = new Content::ModrinthSource(ModPlatform::ResourceType::Mod, this);
    if (flame) {
        mods.sources.curseforge = new Content::CurseForgeSource(ModPlatform::ResourceType::Mod, this);
    }
    mods.installedMods = new Content::InstanceModsSource(this);
    mods.loadFilter = []() { return APPLICATION->settings()->get("ZeroDiscoverModsFilter").toString(); };
    mods.saveFilter = [](const QString& json) { APPLICATION->settings()->set("ZeroDiscoverModsFilter", json); };
    mods.loadView = []() { return APPLICATION->settings()->get("ZeroDiscoverModsView").toString(); };
    mods.saveView = [](const QString& view) { APPLICATION->settings()->set("ZeroDiscoverModsView", view); };

    // ----- Resource Packs: no Technic; the installed packs are the packwiz metadata of the target's resourcepacks folder -----
    Content::ModsEnvironment packs;
    packs.sources.modrinth = new Content::ModrinthSource(ModPlatform::ResourceType::ResourcePack, this);
    if (flame) {
        packs.sources.curseforge = new Content::CurseForgeSource(ModPlatform::ResourceType::ResourcePack, this);
    }
    packs.installedMods = new Content::InstanceModsSource(this, Content::InstalledFolder::ResourcePacks);
    packs.loadFilter = []() { return APPLICATION->settings()->get("ZeroDiscoverResourcePacksFilter").toString(); };
    packs.saveFilter = [](const QString& json) { APPLICATION->settings()->set("ZeroDiscoverResourcePacksFilter", json); };
    packs.loadView = []() { return QStringLiteral("grid"); };  // packs have no list view
    packs.saveView = [](const QString&) {};

    // ----- Shaders: no Technic; the installed shaders are the packwiz metadata of the target's shaderpacks folder -----
    Content::ModsEnvironment shaders;
    shaders.sources.modrinth = new Content::ModrinthSource(ModPlatform::ResourceType::ShaderPack, this);
    if (flame) {
        shaders.sources.curseforge = new Content::CurseForgeSource(ModPlatform::ResourceType::ShaderPack, this);
    }
    shaders.installedMods = new Content::InstanceModsSource(this, Content::InstalledFolder::ShaderPacks);
    shaders.loadFilter = []() { return APPLICATION->settings()->get("ZeroDiscoverShadersFilter").toString(); };
    shaders.saveFilter = [](const QString& json) { APPLICATION->settings()->set("ZeroDiscoverShadersFilter", json); };
    shaders.loadView = []() { return QStringLiteral("grid"); };  // shaders have no list view either
    shaders.saveView = [](const QString&) {};

    // ----- Worlds: CurseForge only (Modrinth hosts no worlds), so there is no section without the capability: no segment, no stub -----
    Content::ModsEnvironment worlds;
    if (flame) {
        worlds.sources.curseforge = new Content::CurseForgeSource(ModPlatform::ResourceType::World, this);
        worlds.loadFilter = []() { return APPLICATION->settings()->get("ZeroDiscoverWorldsFilter").toString(); };
        worlds.saveFilter = [](const QString& json) { APPLICATION->settings()->set("ZeroDiscoverWorldsFilter", json); };
        worlds.loadView = []() { return QStringLiteral("grid"); };  // worlds have no list view either
        worlds.saveView = [](const QString&) {};
    }

    DiscoverHooks hooks;
    hooks.loadSection = []() { return APPLICATION->settings()->get("ZeroDiscoverSection").toString(); };
    hooks.saveSection = [](const QString& id) { APPLICATION->settings()->set("ZeroDiscoverSection", id); };
    hooks.loadTarget = []() { return APPLICATION->settings()->get("ZeroDiscoverTarget").toString(); };
    hooks.saveTarget = [](const QString& id) { APPLICATION->settings()->set("ZeroDiscoverTarget", id); };
    hooks.readTargets = []() { return Content::targetsFromInstanceList(APPLICATION->instances()); };
    build(env, &mods, &packs, &shaders, flame ? &worlds : nullptr, hooks);

    // Minecraft versions for the version filter: Prism's cached meta list, loaded when the page is first shown.
    m_meta = new Create::PrismMetaSource(this);
    connect(m_meta, &Create::MetaSource::statusChanged, this, [this](const QString& uid) {
        if (uid != Create::kMinecraftUid || m_meta->status(uid) != Create::MetaSource::Status::Ready) {
            return;
        }
        QList<Create::MinecraftVersionFact> facts;
        for (const auto& version : m_meta->versions(uid)) {
            facts.append({ version->version(), version->type(), version->time(), version->isRecommended() });
        }
        QStringList ids;
        for (const auto& row : Create::buildVersionRows(facts)) {
            ids.append(row.id);
        }
        setMinecraftVersions(ids);
    });

    // Installed packs and mods and the Mods target change with the instance list (create, delete, rename, update, loader, running).
    m_installedTimer.setSingleShot(true);
    m_installedTimer.setInterval(150);
    connect(&m_installedTimer, &QTimer::timeout, this, [this]() {
        m_modpacks->refreshInstalled();
        refreshTargets();
    });
    auto* instances = APPLICATION->instances();
    auto schedule = [this]() { m_installedTimer.start(); };
    connect(instances, &QAbstractItemModel::rowsInserted, this, schedule);
    connect(instances, &QAbstractItemModel::rowsRemoved, this, schedule);
    connect(instances, &QAbstractItemModel::modelReset, this, schedule);
    connect(instances, &QAbstractItemModel::dataChanged, this, schedule);
}

DiscoverPage::DiscoverPage(const Content::ModpacksEnvironment& env, QWidget* parent) : ShellPage(parent)
{
    build(env, nullptr, nullptr, nullptr, nullptr, {});
}

DiscoverPage::DiscoverPage(const Content::ModpacksEnvironment& modpacks, const Content::ModsEnvironment& mods, const DiscoverHooks& hooks,
                           QWidget* parent)
    : ShellPage(parent)
{
    build(modpacks, &mods, nullptr, nullptr, nullptr, hooks);
}

DiscoverPage::DiscoverPage(const Content::ModpacksEnvironment& modpacks, const Content::ModsEnvironment& mods,
                           const Content::ModsEnvironment& packs, const DiscoverHooks& hooks, QWidget* parent)
    : ShellPage(parent)
{
    build(modpacks, &mods, &packs, nullptr, nullptr, hooks);
}

DiscoverPage::DiscoverPage(const Content::ModpacksEnvironment& modpacks, const Content::ModsEnvironment& mods,
                           const Content::ModsEnvironment& packs, const Content::ModsEnvironment& shaders, const DiscoverHooks& hooks,
                           QWidget* parent)
    : ShellPage(parent)
{
    build(modpacks, &mods, &packs, &shaders, nullptr, hooks);
}

DiscoverPage::DiscoverPage(const Content::ModpacksEnvironment& modpacks, const Content::ModsEnvironment& mods,
                           const Content::ModsEnvironment& packs, const Content::ModsEnvironment& shaders,
                           const Content::ModsEnvironment& worlds, const DiscoverHooks& hooks, QWidget* parent)
    : ShellPage(parent)
{
    build(modpacks, &mods, &packs, &shaders, &worlds, hooks);
}

void DiscoverPage::build(const Content::ModpacksEnvironment& modpacks, const Content::ModsEnvironment* mods,
                         const Content::ModsEnvironment* packs, const Content::ModsEnvironment* shaders,
                         const Content::ModsEnvironment* worlds, const DiscoverHooks& hooks)
{
    m_hooks = hooks;
    m_cart = new Content::InstallCartModel(this);
    // The sections get the page's cart through their environments.
    Content::ModsEnvironment modsEnv;
    Content::ModsEnvironment packsEnv;
    Content::ModsEnvironment shadersEnv;
    if (mods) {
        modsEnv = *mods;
        modsEnv.cart = m_cart;
        mods = &modsEnv;
    }
    if (packs) {
        packsEnv = *packs;
        packsEnv.cart = m_cart;
        packs = &packsEnv;
    }
    if (shaders) {
        shadersEnv = *shaders;
        shadersEnv.cart = m_cart;
        shaders = &shadersEnv;
    }

    // Worlds have no cart (the section drops it): the environment goes in as given.
    // The page content (the sections) keeps its margins; the selection bar is docked under it, edge to edge, so it never covers a card.
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* body = new QWidget(this);
    auto* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl);
    bodyLayout->setSpacing(0);
    m_stack = new QStackedWidget(body);
    bodyLayout->addWidget(m_stack);
    layout->addWidget(body, 1);
    m_bar = new Content::SelectionBar(m_cart, mods ? mods->images : modpacks.images, this);
    layout->addWidget(m_bar);
    connect(m_bar, &Content::SelectionBar::reviewRequested, this, &DiscoverPage::cartReviewRequested);
    connect(m_cart, &Content::InstallCartModel::limitReached, this,
            [this]() { ZeroToast::show(this, tr("The cart holds up to %1 items").arg(Content::InstallCart::kCap), ZeroToast::Kind::Warning); });

    m_modpacks = new Content::ModpacksSection(modpacks, m_stack);
    m_stack->addWidget(m_modpacks);
    connect(m_modpacks, &Content::ModpacksSection::resultsCaptionChanged, this, [this](const QString&) { updateChrome(); });
    connect(m_modpacks, &Content::ModpacksSection::installRequested, this, &DiscoverPage::installRequested);
    connect(m_modpacks, &Content::ModpacksSection::reloaded, this, &DiscoverPage::requestMinecraftVersions);

    if (mods) {
        m_mods = new Content::ModsSection(*mods, m_stack);
        m_stack->addWidget(m_mods);
        connect(m_mods, &Content::ModsSection::resultsCaptionChanged, this, [this](const QString&) { updateChrome(); });
        connect(m_mods, &Content::ModsSection::installRequested, this, &DiscoverPage::modInstallRequested);
        connect(m_mods, &Content::ModsSection::createInstanceRequested, this, &DiscoverPage::createInstanceRequested);
        connect(m_mods, &Content::ModsSection::targetChanged, this, &DiscoverPage::updateCartBar);
        connect(m_mods, &Content::ModsSection::reloaded, this, &DiscoverPage::requestMinecraftVersions);
        connect(m_mods, &Content::ModsSection::targetPicked, this, [this](const QString& id) {
            if (m_hooks.saveTarget) {
                m_hooks.saveTarget(id);
            }
            if (m_librarySelection) {
                m_lastLibrarySelection = m_librarySelection();  // a pick is not undone by the Library selection the user already left alone
            }
        });

        if (packs) {
            m_packs = new Content::PacksSection(*packs, m_stack);
            m_stack->addWidget(m_packs);
            connect(m_packs, &Content::PacksSection::resultsCaptionChanged, this, [this](const QString&) { updateChrome(); });
            connect(m_packs, &Content::PacksSection::installRequested, this, &DiscoverPage::packInstallRequested);
            connect(m_packs, &Content::PacksSection::createInstanceRequested, this, &DiscoverPage::createVanillaInstanceRequested);
            connect(m_packs, &Content::PacksSection::targetChanged, this, &DiscoverPage::updateCartBar);
            connect(m_packs, &Content::PacksSection::reloaded, this, &DiscoverPage::requestMinecraftVersions);
            connect(m_packs, &Content::PacksSection::targetPicked, this, [this](const QString& id) {
                if (m_hooks.saveTarget) {
                    m_hooks.saveTarget(id);
                }
                if (m_librarySelection) {
                    m_lastLibrarySelection = m_librarySelection();
                }
            });
        }

        if (shaders) {
            m_shaders = new Content::ShadersSection(*shaders, m_stack);
            m_stack->addWidget(m_shaders);
            connect(m_shaders, &Content::ShadersSection::resultsCaptionChanged, this, [this](const QString&) { updateChrome(); });
            connect(m_shaders, &Content::ShadersSection::installRequested, this, &DiscoverPage::shaderInstallRequested);
            connect(m_shaders, &Content::ShadersSection::createInstanceRequested, this, &DiscoverPage::createVanillaInstanceRequested);
            connect(m_shaders, &Content::ShadersSection::targetChanged, this, &DiscoverPage::updateCartBar);
            connect(m_shaders, &Content::ShadersSection::findModRequested, this, &DiscoverPage::findShaderModRequested);
            connect(m_shaders, &Content::ShadersSection::reloaded, this, &DiscoverPage::requestMinecraftVersions);
            connect(m_shaders, &Content::ShadersSection::targetPicked, this, [this](const QString& id) {
                if (m_hooks.saveTarget) {
                    m_hooks.saveTarget(id);
                }
                if (m_librarySelection) {
                    m_lastLibrarySelection = m_librarySelection();
                }
            });
        }

        if (worlds) {
            m_worlds = new Content::WorldsSection(*worlds, m_stack);
            m_stack->addWidget(m_worlds);
            connect(m_worlds, &Content::WorldsSection::resultsCaptionChanged, this, [this](const QString&) { updateChrome(); });
            connect(m_worlds, &Content::WorldsSection::installRequested, this, &DiscoverPage::worldInstallRequested);
            connect(m_worlds, &Content::WorldsSection::createInstanceRequested, this, &DiscoverPage::createVanillaInstanceRequested);
            connect(m_worlds, &Content::WorldsSection::reloaded, this, &DiscoverPage::requestMinecraftVersions);
            connect(m_worlds, &Content::WorldsSection::targetPicked, this, [this](const QString& id) {
                if (m_hooks.saveTarget) {
                    m_hooks.saveTarget(id);
                }
                if (m_librarySelection) {
                    m_lastLibrarySelection = m_librarySelection();
                }
            });
        }

        // The segmented control of the top bar. Sections that are not built do not appear (no stubs).
        QList<Content::FacetOption> options;
        options.append({ QString::fromLatin1(kModpacks), tr("Modpacks"), QString() });
        options.append({ QString::fromLatin1(kMods), tr("Mods"), QString() });
        if (m_packs) {
            options.append({ QString::fromLatin1(kPacks), tr("Resource Packs"), QString() });
        }
        if (m_shaders) {
            options.append({ QString::fromLatin1(kShaders), tr("Shaders"), QString() });
        }
        if (m_worlds) {
            options.append({ QString::fromLatin1(kWorlds), tr("Worlds"), QString() });
        }
        m_switcher = new Content::SegmentedControl(options, nullptr);
        m_switcher->setObjectName(QStringLiteral("zeroDiscoverSwitcher"));
        m_switcher->setAccessibleName(tr("Section"));
        m_switcher->setNaturalWidths(true);  // up to five sections: equal widths (the widest label) did not fit the top bar at 1100-1280 px
        m_switcher->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        m_switcher->setFixedWidth(m_switcher->sizeHint().width() + Tokens::spaceLg);
        m_switcher->onActivated = [this](const QString& id) { setCurrentSection(id, true); };
        setTopBarActions(m_switcher);

        // The remembered section (read once, on start); the target of Mods.
        const QString saved = m_hooks.loadSection ? m_hooks.loadSection() : QString();
        QWidget* start = m_modpacks;
        if (saved == QLatin1String(kMods)) {
            start = m_mods;
        } else if (saved == QLatin1String(kPacks) && m_packs) {
            start = m_packs;
        } else if (saved == QLatin1String(kShaders) && m_shaders) {
            start = m_shaders;
        } else if (saved == QLatin1String(kWorlds) && m_worlds) {
            start = m_worlds;
        }
        m_stack->setCurrentWidget(start);
        m_switcher->setSelected(currentSection());
        refreshTargets(true);
    }
    updateChrome();
    updateCartBar();
}

QWidget* DiscoverPage::topBarActions() const
{
    return m_switcher ? m_switcher : ShellPage::topBarActions();
}

Content::ContentSection* DiscoverPage::current() const
{
    if (m_mods && m_stack->currentWidget() == m_mods) {
        return m_mods;
    }
    if (m_packs && m_stack->currentWidget() == m_packs) {
        return m_packs;
    }
    if (m_shaders && m_stack->currentWidget() == m_shaders) {
        return m_shaders;
    }
    if (m_worlds && m_stack->currentWidget() == m_worlds) {
        return m_worlds;
    }
    return m_modpacks;
}

QList<Content::ModsSection*> DiscoverPage::targetedSections() const
{
    QList<Content::ModsSection*> out;
    for (Content::ModsSection* section : { m_mods, static_cast<Content::ModsSection*>(m_packs), static_cast<Content::ModsSection*>(m_shaders),
                                           static_cast<Content::ModsSection*>(m_worlds) }) {
        if (section) {
            out.append(section);
        }
    }
    return out;
}

Content::ModsSection* DiscoverPage::targeted(const QString& id) const
{
    if (id == QLatin1String(kMods)) {
        return m_mods;
    }
    if (id == QLatin1String(kPacks)) {
        return m_packs;
    }
    if (id == QLatin1String(kShaders)) {
        return m_shaders;
    }
    if (id == QLatin1String(kWorlds)) {
        return m_worlds;
    }
    return nullptr;
}

QStringList DiscoverPage::sections() const
{
    QStringList out{ QString::fromLatin1(kModpacks) };
    if (m_mods) {
        out.append(QString::fromLatin1(kMods));
    }
    if (m_packs) {
        out.append(QString::fromLatin1(kPacks));
    }
    if (m_shaders) {
        out.append(QString::fromLatin1(kShaders));
    }
    if (m_worlds) {
        out.append(QString::fromLatin1(kWorlds));
    }
    return out;
}

QString DiscoverPage::currentSection() const
{
    if (m_packs && current() == m_packs) {
        return QString::fromLatin1(kPacks);
    }
    if (m_shaders && current() == m_shaders) {
        return QString::fromLatin1(kShaders);
    }
    if (m_worlds && current() == m_worlds) {
        return QString::fromLatin1(kWorlds);
    }
    return QString::fromLatin1(current() == m_mods && m_mods ? kMods : kModpacks);
}

Content::ModsSection* DiscoverPage::cartTargetSection() const
{
    // Worlds are not in the cart: their target never decides where the cart adds.
    if (auto* shown = qobject_cast<Content::ModsSection*>(current()); shown && shown != m_worlds && shown->hasTarget()) {
        return shown;
    }
    for (Content::ModsSection* section : targetedSections()) {
        if (section != m_worlds && section->hasTarget()) {
            return section;
        }
    }
    return nullptr;
}

void DiscoverPage::updateCartBar()
{
    if (m_bar) {
        const Content::ModsSection* section = cartTargetSection();
        m_bar->setTargetName(section ? section->target().name : QString());
    }
}

void DiscoverPage::updateChrome()
{
    const QString next = current()->resultsCaption();
    m_subtitle = next;
    emit chromeChanged();
}

void DiscoverPage::setCurrentSection(const QString& id, bool remember)
{
    QWidget* target = m_modpacks;
    if (id == QLatin1String(kMods) && m_mods) {
        target = m_mods;
    } else if (id == QLatin1String(kPacks) && m_packs) {
        target = m_packs;
    } else if (id == QLatin1String(kShaders) && m_shaders) {
        target = m_shaders;
    } else if (id == QLatin1String(kWorlds) && m_worlds) {
        target = m_worlds;
    }
    if (m_stack->currentWidget() != target) {
        m_stack->setCurrentWidget(target);
        if (m_switcher) {
            m_switcher->setSelected(currentSection());
        }
        if (target != m_modpacks) {
            refreshTargets();
        }
        if (isVisible()) {
            current()->start();
        }
        updateChrome();
        updateCartBar();
    }
    if (remember && m_hooks.saveSection) {
        m_hooks.saveSection(currentSection());
    }
}

void DiscoverPage::setMinecraftVersions(const QStringList& ids)
{
    m_modpacks->setMinecraftVersions(ids);
    if (m_mods) {
        m_mods->setMinecraftVersions(ids);
    }
    if (m_packs) {
        m_packs->setMinecraftVersions(ids);
    }
    if (m_shaders) {
        m_shaders->setMinecraftVersions(ids);
    }
    if (m_worlds) {
        m_worlds->setMinecraftVersions(ids);
    }
}

void DiscoverPage::refreshTargets(bool followLibrary)
{
    if ((!m_mods && !m_packs && !m_shaders) || !m_hooks.readTargets) {
        return;
    }
    const QList<Content::TargetInstance> targets = m_hooks.readTargets();
    const QString library = m_librarySelection ? m_librarySelection() : QString();
    const QString saved = m_hooks.loadTarget ? m_hooks.loadTarget() : QString();
    // Each section has its own target (a vanilla instance can be the target of packs and not of mods).
    for (Content::ModsSection* section : targetedSections()) {
        section->setTargets(targets, Content::chooseTarget(targets, library, saved, section->needsLoader()));
        // Showing the page after the Library selection changed: the selected instance is the one the user is working on.
        if (followLibrary && !library.isEmpty() && library != m_lastLibrarySelection) {
            section->chooseTarget(library);
        }
    }
    m_lastLibrarySelection = library;
}

void DiscoverPage::requestMinecraftVersions()
{
    // Once; asked again only after a failure (MetaSource::request ignores Ready and Loading).
    if (m_meta && (!m_versionsRequested || m_meta->status(Create::kMinecraftUid) == Create::MetaSource::Status::Failed)) {
        m_versionsRequested = true;
        m_meta->request(Create::kMinecraftUid);
    }
}

void DiscoverPage::showEvent(QShowEvent* event)
{
    ShellPage::showEvent(event);
    requestMinecraftVersions();
    if (m_mods || m_packs || m_shaders) {
        refreshTargets(true);
    }
    current()->start();
}

void DiscoverPage::showSection(const QString& section, const QString& provider)
{
    if (!section.isEmpty() && section != QLatin1String(kModpacks) && section != QLatin1String(kMods) && section != QLatin1String(kPacks) &&
        section != QLatin1String(kShaders) && section != QLatin1String(kWorlds)) {
        qWarning() << "Discover: unknown section" << section;
    } else if (section == QLatin1String(kMods) && !m_mods) {
        qWarning() << "Discover: no Mods section in this page";
    } else if (section == QLatin1String(kPacks) && !m_packs) {
        qWarning() << "Discover: no Resource Packs section in this page";
    } else if (section == QLatin1String(kShaders) && !m_shaders) {
        qWarning() << "Discover: no Shaders section in this page";
    } else if (section == QLatin1String(kWorlds) && !m_worlds) {
        qWarning() << "Discover: no Worlds section in this page";
    }
    setCurrentSection(section.isEmpty() ? currentSection() : section, true);
    if (!provider.isEmpty()) {
        if (const auto p = Content::providerFromId(provider)) {
            current()->setProvider(*p);
        } else {
            qWarning() << "Discover: unknown provider" << provider;
        }
    }
}

bool DiscoverPage::showModsFor(const QString& instanceId, const QString& query)
{
    showSection(QLatin1String(kMods));
    if (!m_mods) {
        return false;
    }
    // Read the instances and the Library selection first: the page follows a Library selection that changed since it was last
    // shown, and this choice must not be undone by that when the page appears.
    refreshTargets();
    const bool chosen = m_mods->chooseTarget(instanceId);
    if (chosen && m_hooks.saveTarget) {
        m_hooks.saveTarget(instanceId);
    }
    if (chosen && !query.trimmed().isEmpty()) {
        m_mods->setSearchText(query);  // after the target: choosing it may start a search of its own
    }
    return chosen;
}

bool DiscoverPage::showFolderFor(const QString& sectionId, const QString& instanceId, const QString& query)
{
    showSection(sectionId);
    Content::ModsSection* section = targeted(sectionId);
    if (!section) {
        return false;
    }
    refreshTargets();
    const bool chosen = section->chooseTarget(instanceId);
    if (chosen && m_hooks.saveTarget) {
        m_hooks.saveTarget(instanceId);
    }
    if (chosen && !query.trimmed().isEmpty()) {
        section->setSearchText(query);
    }
    return chosen;
}

bool DiscoverPage::showPacksFor(const QString& instanceId, const QString& query)
{
    return showFolderFor(QString::fromLatin1(kPacks), instanceId, query);
}

bool DiscoverPage::showShadersFor(const QString& instanceId, const QString& query)
{
    return showFolderFor(QString::fromLatin1(kShaders), instanceId, query);
}

bool DiscoverPage::showWorldsFor(const QString& instanceId, const QString& query)
{
    return showFolderFor(QString::fromLatin1(kWorlds), instanceId, query);
}

void DiscoverPage::prepareScreenshot(const std::function<void()>& ready)
{
    const QString section = qEnvironmentVariable("ZERO_SCREENSHOT_DISCOVER_SECTION");
    if (!section.isEmpty()) {
        setCurrentSection(section, false);
    }
    Content::ContentSection* shown = current();
    const QString provider = qEnvironmentVariable("ZERO_SCREENSHOT_DISCOVER_PROVIDER");
    const QString query = qEnvironmentVariable("ZERO_SCREENSHOT_DISCOVER_QUERY");
    bool selectOk = false;
    int select = qEnvironmentVariable("ZERO_SCREENSHOT_DISCOVER_SELECT").toInt(&selectOk);
    if (!selectOk && ((m_packs && shown == m_packs) || (m_shaders && shown == m_shaders) || (m_worlds && shown == m_worlds)) &&
        !qEnvironmentVariable("ZERO_SCREENSHOT_DISCOVER_INSTALL").isEmpty()) {
        selectOk = true;  // ZERO_SCREENSHOT_DISCOVER_INSTALL on Resource Packs / Shaders / Worlds opens the add dialog of the first result
        select = 0;
    }
    const bool filters = qEnvironmentVariableIntValue("ZERO_SCREENSHOT_DISCOVER_FILTERS") == 1;
    const QString cartRows = qEnvironmentVariable("ZERO_SCREENSHOT_DISCOVER_CART");  // 0-based rows to tick, e.g. "0,2,3"
    if (!provider.isEmpty()) {
        if (const auto p = Content::providerFromId(provider)) {
            shown->setProvider(*p);
        } else {
            qWarning() << "ZERO_SCREENSHOT_DISCOVER_PROVIDER: unknown provider" << provider << "(modrinth, curseforge, technic)";
        }
    }
    if (!query.isEmpty()) {
        shown->setSearchText(query);
    }
    if (auto* targetedSection = qobject_cast<Content::ModsSection*>(shown); targetedSection && !targetedSection->hasTarget()) {
        // No ready instance: the empty state, nothing to wait for.
        QTimer::singleShot(600, this, [ready]() { ready(); });
        return;
    }
    shown->start();
    // The first page and the visible pictures first; then the details of the chosen row; then the filter panel.
    shown->callWhenIdle(
        [shown, selectOk, select, filters, cartRows, ready]() {
            auto finish = [shown, filters, cartRows, ready]() {
                // Optional: the tab of the details panel (overview | gallery | versions | whatsnew) and, for the gallery, the viewer.
                using Tab = Content::ContentDetailsPanel::Tab;
                const QString tab = qEnvironmentVariable("ZERO_SCREENSHOT_DISCOVER_TAB");
                auto* panel = shown->panel();
                if (tab == QLatin1String("gallery")) {
                    panel->setCurrentTab(Tab::Gallery);
                } else if (tab == QLatin1String("versions")) {
                    panel->setCurrentTab(Tab::Versions);
                    bool expandOk = false;
                    const int expand = qEnvironmentVariable("ZERO_SCREENSHOT_DISCOVER_EXPAND").toInt(&expandOk);
                    if (expandOk) {
                        panel->toggleVersion(expand);
                    }
                } else if (tab == QLatin1String("whatsnew")) {
                    panel->setCurrentTab(Tab::WhatsNew);
                }
                bool viewerOk = false;
                const int viewer = qEnvironmentVariable("ZERO_SCREENSHOT_DISCOVER_VIEWER").toInt(&viewerOk);
                if (viewerOk) {
                    panel->openGallery(viewer);
                }
                if (filters) {
                    shown->openFilters();
                }
                if (auto* targetedSection = qobject_cast<Content::ModsSection*>(shown)) {
                    for (const QString& part : cartRows.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                        bool rowOk = false;
                        const int row = part.trimmed().toInt(&rowOk);
                        if (rowOk) {
                            targetedSection->toggleCartRow(row);
                        }
                    }
                }
                shown->callWhenIdle(ready, 12000);  // the pictures of the chosen tab / viewer too (and the selection bar's slide)
            };
            if (selectOk) {
                shown->selectRow(select);
                shown->callWhenIdle(finish, 12000);
            } else {
                finish();
            }
        },
        12000);
}

void DiscoverPage::paintOverlays(QPixmap* shot, QWidget* window) const
{
    auto* panel = current()->filterPanel();
    if (!shot || !panel || !panel->isVisible()) {
        return;
    }
    // A Qt::Popup is its own top-level window: grab() of the main window does not contain it.
    const QPixmap popup = panel->grab();
    const QPoint at = window->mapFromGlobal(panel->mapToGlobal(QPoint(0, 0)));
    QPainter p(shot);
    p.drawPixmap(at, popup);
}

}  // namespace Zero
