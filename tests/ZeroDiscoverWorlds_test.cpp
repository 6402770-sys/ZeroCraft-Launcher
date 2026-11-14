// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of Discover > Worlds (docs/zero/SHELL.md, "Discover > Worlds - as built"): the section is the packs / shaders
// section over CurseForge's world maps (facets: Minecraft version and Category, no provider, no loader, no default version chip), the
// card pills ("Made for"), the add dialog in world mode (every file, the version-fit note, no download URL = "Open on CurseForge"), the
// pure install pieces (version-fit rules, temp path, cleanup, the folder InstallWorldTask made), worlds staying out of the install
// cart, the fifth switcher segment and its fit in the top bar at 1100 and 1280 px.
// Offscreen, with fake sources and hand-built TargetInstances: no network, no instances, no APPLICATION.
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QListView>
#include <QPixmap>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <functional>

#include "ui/zero/ZeroEmptyState.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/content/ContentDetailsPanel.h"
#include "ui/zero/content/FilterPanel.h"
#include "ui/zero/content/ModInstallDialog.h"
#include "ui/zero/content/ModpacksSection.h"
#include "ui/zero/content/ModsSection.h"
#include "ui/zero/content/PackRows.h"
#include "ui/zero/content/RemoteImageCache.h"
#include "ui/zero/pages/DiscoverPage.h"
#include "ui/zero/shell/AccountChip.h"
#include "ui/zero/shell/ZeroSearchPill.h"
#include "ui/zero/shell/ZeroShell.h"
#include "ui/zero/shell/ZeroTopBar.h"
#include "zero/content/InstallCart.h"
#include "zero/content/WorldInstall.h"

using namespace Zero;
using namespace Zero::Content;

namespace {

void setMotion(bool on)
{
    ThemeSpec spec = ThemeSpec::obsidian();
    spec.reduceMotion = !on;
    Theme::setCurrent(spec);
}

QString S(const char* text)
{
    return QString::fromUtf8(text);
}

class FakeSource : public ModpackSource {
   public:
    struct SearchCall {
        ResourceAPI::SearchArgs args;
        quint64 generation = 0;
    };

    explicit FakeSource(Provider provider) : m_provider(provider) {}

    Provider provider() const override { return m_provider; }
    void search(const ResourceAPI::SearchArgs& args, quint64 generation) override { searches.append({ args, generation }); }
    void details(const ModpackItemPtr&, quint64) override {}
    void versions(const ModpackItemPtr&, quint64) override { ++versionCalls; }
    void changelog(const ModpackItemPtr&, const QString&, quint64) override {}

    void setCategories(const QList<ModPlatform::Category>& categories)
    {
        m_categories = categories;
        emit categoriesReady();
    }
    void answerSearch(int call, const QList<ModpackItemPtr>& items)
    {
        emit searchFinished(searches.at(call).generation, searches.at(call).args.offset, items, int(items.size()));
    }

    QList<SearchCall> searches;
    int versionCalls = 0;

   private:
    Provider m_provider;
};

TargetInstance makeTarget(const QString& id, const QString& name, const QString& mc, ModPlatform::ModLoaderTypes loaders, bool running = false)
{
    TargetInstance t;
    t.id = id;
    t.name = name;
    t.iconKey = QStringLiteral("default");
    t.mcVersion = mc;
    t.loaders = loaders;
    t.loaderText = loaderDisplayNames(loaders).join(QStringLiteral(", "));
    t.running = running;
    return t;
}

TargetInstance fabric121() { return makeTarget(S("fab"), S("Fabric Fun"), S("1.21.1"), ModPlatform::Fabric); }
TargetInstance forge120() { return makeTarget(S("frg"), S("Forge Land"), S("1.20.1"), ModPlatform::Forge); }
TargetInstance vanilla121() { return makeTarget(S("van"), S("Vanilla"), S("1.21.1"), ModPlatform::ModLoaderTypes()); }
TargetInstance noVersion() { return makeTarget(S("nov"), S("No version"), QString(), ModPlatform::Fabric); }

ModpackItemPtr makeWorld(const QString& id, const QString& name, const QStringList& categories = { S("Adventure"), S("Parkour") },
                         const QStringList& gameVersions = { S("1.20.1"), S("1.19.4") })
{
    ModPlatform::IndexedPack pack;
    pack.addonId = id;
    pack.provider = ModPlatform::ResourceProvider::FLAME;
    pack.name = name;
    pack.slug = name.toLower().replace(QLatin1Char(' '), QLatin1Char('-'));
    pack.description = S("About %1").arg(name);
    pack.authors = { { S("mapmaker"), QString() } };
    pack.logoUrl = S("https://example.test/icon/%1.png").arg(id);
    pack.gameVersions = gameVersions;
    pack.categories = categories;
    for (int i = 0; i < categories.size(); ++i) {
        pack.categoryIds << QString::number(1000 + i);
    }
    pack.downloads = 4321;
    pack.websiteUrl = S("https://www.curseforge.com/minecraft/worlds/%1").arg(pack.slug);
    return itemFromPack(Provider::CurseForge, pack);
}

ModPlatform::IndexedVersion indexedVersion(const QString& fileId, const QString& name, const QStringList& mc, bool downloadable = true)
{
    ModPlatform::IndexedVersion v;
    v.addonId = S("PROJ");
    v.fileId = fileId;
    v.version = name;
    v.versionNumber = name;
    v.versionType = ModPlatform::IndexedVersionType(ModPlatform::IndexedVersionTypeValue::Release);
    v.mcVersion = mc;
    v.loaders = ModPlatform::ModLoaderTypes();
    v.downloadUrl = downloadable ? S("https://edge.forgecdn.test/%1.zip").arg(fileId) : QString();
    v.fileName = name + S(".zip");
    v.date = S("2024-03-12T10:00:00Z");
    v.fileSize = 3 * 1024 * 1024;
    return v;
}

ModpackItemPtr worldWithVersions(const QList<ModPlatform::IndexedVersion>& versions, const QString& website = {})
{
    auto item = makeWorld(S("PROJ"), S("Skyblock Remix"));
    item->pack->versions = versions;
    item->pack->versionsLoaded = true;
    if (!website.isNull()) {
        item->websiteUrl = website;
    }
    return item;
}

Facet facetOf(const QList<Facet>& facets, const QString& id)
{
    for (const auto& f : facets) {
        if (f.id == id) {
            return f;
        }
    }
    return {};
}

QList<ModPlatform::Category> worldCategories()
{
    return { { S("Adventure"), S("1001"), QString() }, { S("Parkour"), S("1002"), QString() }, { S("Survival"), S("1003"), QString() } };
}

struct Rig {
    FakeSource curseforge{ Provider::CurseForge };
    InstallCartModel cart;
    RemoteImageCache images{ [](const QString&, std::function<void(const RemoteImageCache::FetchResult&)> done) { done({}); } };
    std::unique_ptr<WorldsSection> section;
    QStringList savedFilters;

    ModsEnvironment environment()
    {
        ModsEnvironment env;
        env.sources.curseforge = &curseforge;  // Modrinth hosts no worlds: there is no other source
        env.images = &images;
        env.cart = &cart;  // the section drops it: worlds are not cart-eligible
        env.icons = [](const QString&) {
            QPixmap pm(20, 20);
            pm.fill(Qt::darkGreen);
            return QIcon(pm);
        };
        env.saveFilter = [this](const QString& json) { savedFilters << json; };
        return env;
    }

    void open()
    {
        section = std::make_unique<WorldsSection>(environment());
        section->resize(1280, 720);
        section->show();
        QVERIFY2(QTest::qWaitForWindowExposed(section.get()), "section not exposed");
    }
};

}  // namespace

class ZeroDiscoverWorldsTest : public QObject {
    Q_OBJECT

   private slots:
    void initTestCase()
    {
        qRegisterMetaType<Zero::Content::ModpackItemPtr>("Zero::Content::ModpackItemPtr");
        qRegisterMetaType<Zero::Content::ContentItemPtr>("Zero::Content::ContentItemPtr");
        qRegisterMetaType<Zero::Content::ContentDetailsPanel::Tab>("Zero::Content::ContentDetailsPanel::Tab");
    }
    void init() { setMotion(false); }
    void cleanup() { setMotion(true); }

    // ---- filter core ----

    void facetsAreVersionAndCategoryOnly()
    {
        FacetContext ctx;
        ctx.provider = Provider::CurseForge;
        ctx.mcVersions = { S("1.21.1"), S("1.20.1") };
        ctx.categories = worldCategories();
        const QList<Facet> facets = facetsFor(Kind::World, ctx);
        QStringList ids;
        for (const Facet& f : facets) {
            ids << f.id;
            QVERIFY(f.isEnabled());
        }
        QCOMPARE(ids, (QStringList{ FacetId::mcVersion, FacetId::category }));  // no provider, no loader, no installed
        QCOMPARE(facetOf(facets, FacetId::mcVersion).options.size(), 2);
        const Facet category = facetOf(facets, FacetId::category);
        QCOMPARE(category.options.size(), 3);
        QCOMPARE(category.options.first().label, S("Adventure"));  // CurseForge's own names, never hard-coded here
        QCOMPARE(category.options.first().value, S("1001"));
        QVERIFY(!sortOptions(Kind::World, Provider::CurseForge).isEmpty());
        QCOMPARE(sortOptions(Kind::World, Provider::CurseForge).first().id, S("featured"));
    }

    void searchArgsForWorlds()
    {
        FacetContext ctx;
        ctx.provider = Provider::CurseForge;
        ctx.mcVersions = { S("1.21.1"), S("1.20.1") };
        ctx.categories = worldCategories();
        const QList<Facet> facets = facetsFor(Kind::World, ctx);

        // nothing picked: no version is sent (a world of any version is listed)
        FilterState none;
        auto args = toSearchArgs(none, Kind::World, Provider::CurseForge, 0, {}, facets);
        QCOMPARE(args.type, ModPlatform::ResourceType::World);
        QVERIFY(!args.versions.has_value());
        QVERIFY(!args.loaders.has_value());
        QVERIFY(!args.categoryIds.has_value());

        FilterState picked;
        picked.selected.insert(FacetId::mcVersion, { S("1.20.1") });
        picked.selected.insert(FacetId::category, { S("1002"), S("1003") });
        args = toSearchArgs(picked, Kind::World, Provider::CurseForge, 25, {}, facets);
        QCOMPARE(int(args.versions->size()), 1);  // CurseForge takes one version per search
        QCOMPARE(args.versions->front().toString(), S("1.20.1"));
        QCOMPARE(*args.categoryIds, (QStringList{ S("1002"), S("1003") }));
        QCOMPARE(args.offset, 25);
        QVERIFY(!args.loaders.has_value());
    }

    // ---- the section ----

    void sectionKindTargetAndNoDefaultVersionChip()
    {
        Rig rig;
        rig.open();
        QVERIFY(rig.section->isWorlds());
        QVERIFY(rig.section->isFolderKind());
        QVERIFY(!rig.section->needsLoader());
        QVERIFY(rig.section->view()->findChild<PackDelegate*>());  // gallery cards, like the packs and shaders
        QVERIFY(rig.section->cart() == nullptr);                   // the cart of the environment is dropped
        QCOMPARE(rig.section->provider(), Provider::CurseForge);    // the only provider, whatever the state says

        rig.section->setTargets({ noVersion(), vanilla121(), forge120() }, S("van"));
        QVERIFY(rig.section->hasTarget());
        QCOMPARE(rig.section->target().id, S("van"));
        QVERIFY(!rig.section->chooseTarget(S("nov")));  // an instance without a Minecraft version is never a target
        QCOMPARE(rig.section->modReadyTargets().size(), 2);

        // NO default version chip: an older world opens in a newer game
        QVERIFY(rig.section->defaults().values(FacetId::mcVersion).isEmpty());
        QVERIFY(rig.section->state().values(FacetId::mcVersion).isEmpty());
        QCOMPARE(rig.curseforge.searches.size(), 1);
        const auto& args = rig.curseforge.searches[0].args;
        QCOMPARE(args.type, ModPlatform::ResourceType::World);
        QVERIFY(!args.versions.has_value());
        QVERIFY(!args.loaders.has_value());
        // the Versions tab and the add dialog ask for every file: no version filter on the source
        QVERIFY(rig.curseforge.versionFilter().isEmpty());

        // facets: Minecraft version and Category only (no Provider)
        rig.curseforge.setCategories(worldCategories());
        QStringList ids;
        for (const Facet& f : rig.section->facets()) {
            ids << f.id;
        }
        QCOMPARE(ids, (QStringList{ FacetId::mcVersion, FacetId::category }));
        QCOMPARE(facetOf(rig.section->facets(), FacetId::category).options.size(), 3);
    }

    void userVersionChipSurvivesATargetChangeAndIsSaved()
    {
        Rig rig;
        rig.open();
        rig.section->setTargets({ vanilla121(), forge120() }, S("van"));
        rig.curseforge.setCategories(worldCategories());
        QCOMPARE(rig.curseforge.searches.size(), 1);

        FilterState state = rig.section->state();
        state.selected.insert(FacetId::mcVersion, { S("1.19.4") });
        state.selected.insert(FacetId::category, { S("1002") });
        rig.section->applyState(state);
        QCOMPARE(rig.curseforge.searches.size(), 2);
        QCOMPARE(rig.curseforge.searches.last().args.versions->front().toString(), S("1.19.4"));
        QCOMPARE(*rig.curseforge.searches.last().args.categoryIds, QStringList{ S("1002") });

        // another target with another Minecraft version: the user's chips are not the target's, they stay
        QVERIFY(rig.section->chooseTarget(S("frg")));
        QCOMPARE(rig.section->state().values(FacetId::mcVersion), QStringList{ S("1.19.4") });
        QCOMPARE(rig.section->state().values(FacetId::category), QStringList{ S("1002") });
        QVERIFY(rig.section->defaults().values(FacetId::mcVersion).isEmpty());

        // the chosen version is part of what is remembered (ZeroDiscoverWorldsFilter keeps the user's chips)
        QVERIFY(!rig.savedFilters.isEmpty());
        QVERIFY(rig.savedFilters.last().contains(S("1.19.4")));

        // "Show everything" removes the user's chips and keeps the provider
        const FilterState everything = showEverything(rig.section->state());
        QVERIFY(everything.values(FacetId::mcVersion).isEmpty());
        QVERIFY(everything.values(FacetId::category).isEmpty());
        QCOMPARE(everything.provider(), Provider::CurseForge);
        rig.section->applyState(everything);
        QVERIFY(rig.section->state().values(FacetId::mcVersion).isEmpty());
        QCOMPARE(rig.section->provider(), Provider::CurseForge);
        // a saved filter without a provider (or with Modrinth) is still CurseForge's
        FilterState odd = rig.section->state();
        odd.selected.insert(FacetId::provider, { providerId(Provider::Modrinth) });
        rig.section->applyState(odd);
        QCOMPARE(rig.section->provider(), Provider::CurseForge);
    }

    void emptyStateWithoutAnyInstance()
    {
        Rig rig;
        rig.open();
        rig.section->setTargets({ noVersion() }, QString());
        QVERIFY(!rig.section->hasTarget());
        QVERIFY(rig.section->noTargetState()->isVisibleTo(rig.section.get()));
        QVERIFY(rig.curseforge.searches.isEmpty());
        QSignalSpy create(rig.section.get(), &ModsSection::createInstanceRequested);
        emit rig.section->noTargetState()->actionTriggered();
        QCOMPARE(create.size(), 1);
    }

    void primaryActionHasNoInstalledState()
    {
        Rig rig;
        rig.open();
        rig.section->setTargets({ vanilla121() }, S("van"));
        auto item = makeWorld(S("7"), S("Seven"));
        rig.curseforge.answerSearch(0, { item });
        rig.section->selectItem(item);
        auto* button = rig.section->panel()->primaryButton();
        QCOMPARE(button->text(), S("Add to Vanilla"));
        QVERIFY(button->isEnabled());  // before the versions are known too: the dialog loads the files itself
        QSignalSpy install(rig.section.get(), &ModsSection::installRequested);
        button->click();
        QCOMPARE(install.size(), 1);
        QCOMPARE(install.first().at(1).toString(), S("van"));
        QCOMPARE(install.first().at(2).toString(), QString());  // never an installed file: adding twice makes two copies

        // the target runs: the action waits, with the reason
        rig.section->setTargets({ makeTarget(S("van"), S("Vanilla"), S("1.21.1"), ModPlatform::ModLoaderTypes(), true) }, S("van"));
        QVERIFY(!rig.section->panel()->primaryButton()->isEnabled());
        QVERIFY(rig.section->panel()->noteText().contains(S("Close Vanilla to add worlds")));
    }

    // ---- not in the cart ----

    void worldsAreNotCartEligible()
    {
        QVERIFY(!InstallCart::takesKind(Kind::World));
        QVERIFY(InstallCart::takesKind(Kind::Mod));
        QVERIFY(InstallCart::takesKind(Kind::ResourcePack));
        QVERIFY(InstallCart::takesKind(Kind::Shader));
        InstallCartModel cart;
        QVERIFY(cart.toggle(Kind::World, makeWorld(S("1"), S("A"))) == InstallCart::ToggleResult::Invalid);
        QVERIFY(cart.isEmpty());

        Rig rig;
        rig.open();
        rig.section->setTargets({ vanilla121() }, S("van"));
        auto item = makeWorld(S("2"), S("B"));
        rig.curseforge.answerSearch(0, { item });
        QVERIFY(!rig.section->cartEligible(*item));
        QVERIFY(!rig.section->toggleCartRow(0));
        QVERIFY(rig.cart.isEmpty());
        QVERIFY(rig.section->cardDelegate()->checkRect(QRect(0, 0, 300, 300)).isEmpty());  // no check box
    }

    // ---- cards ----

    void cardPills()
    {
        // up to two categories, then "Made for <newest release>"
        auto world = makeWorld(S("1"), S("A"), { S("Adventure"), S("Parkour"), S("Puzzle") }, { S("1.19.4"), S("1.20.1"), S("23w31a") });
        QCOMPARE(worldPills(*world), (QStringList{ S("Adventure"), S("Parkour"), S("Made for 1.20.1") }));
        RemoteImageCache images([](const QString&, std::function<void(const RemoteImageCache::FetchResult&)> done) { done({}); });
        PackDelegate delegate(&images, Kind::World);
        QCOMPARE(delegate.pillsFor(*world), worldPills(*world));

        // unknown versions: no "Made for" pill (never a guess)
        auto unknown = makeWorld(S("2"), S("B"), { S("Survival") }, {});
        QCOMPARE(worldPills(*unknown), QStringList{ S("Survival") });
        auto snapshotsOnly = makeWorld(S("3"), S("C"), {}, { S("23w31a"), S("1.20-pre1") });
        QVERIFY(worldPills(*snapshotsOnly).isEmpty());
        QCOMPARE(madeForText({ S("1.8.9"), S("1.21.4"), S("1.9") }), S("Made for 1.21.4"));  // numbers compare as numbers, not text
        QCOMPARE(madeForText({}), QString());
    }

    // ---- version fit ----

    void versionFitRules()
    {
        // newer than the target: a warning
        WorldFit fit = worldFit({ S("1.21.4") }, S("1.21.1"), S("Vanilla"));
        QCOMPARE(int(fit.tone), int(WorldFit::Tone::Warning));
        QCOMPARE(fit.text, S("Made for 1.21.4, newer than Vanilla (1.21.1): the game may not open it."));
        // older: Minecraft upgrades it
        fit = worldFit({ S("1.20.1") }, S("1.21.1"), S("Vanilla"));
        QCOMPARE(int(fit.tone), int(WorldFit::Tone::Info));
        QCOMPARE(fit.text, S("Made for 1.20.1. Minecraft upgrades it the first time you open it."));
        // the same: nothing to say ("1.21" equals "1.21.0")
        QCOMPARE(int(worldFit({ S("1.21.1") }, S("1.21.1"), S("V")).tone), int(WorldFit::Tone::None));
        QCOMPARE(int(worldFit({ S("1.21") }, S("1.21.0"), S("V")).tone), int(WorldFit::Tone::None));
        // unknown on either side: nothing to say
        QCOMPARE(int(worldFit({}, S("1.21.1"), S("V")).tone), int(WorldFit::Tone::None));
        QCOMPARE(int(worldFit({ S("1.20.1") }, QString(), S("V")).tone), int(WorldFit::Tone::None));
        // snapshots are not ordered against releases (Versions answers "unknown"): no claim
        QCOMPARE(int(worldFit({ S("24w10a") }, S("1.21.1"), S("V")).tone), int(WorldFit::Tone::None));
        QCOMPARE(int(worldFit({ S("1.21.1") }, S("24w10a"), S("V")).tone), int(WorldFit::Tone::None));
        // several versions: the newest one at or below the target counts
        QCOMPARE(worldFit({ S("1.19.4"), S("1.20.6") }, S("1.21.1"), S("V")).text,
                 S("Made for 1.20.6. Minecraft upgrades it the first time you open it."));
        // a file that lists the target and a newer version fits: no warning
        QCOMPARE(int(worldFit({ S("1.21.1"), S("1.21.4") }, S("1.21.1"), S("V")).tone), int(WorldFit::Tone::None));
        // an older and a newer version: the older one (the game upgrades it), never the warning
        QCOMPARE(worldFit({ S("1.20.1"), S("1.21.4") }, S("1.21.1"), S("V")).text,
                 S("Made for 1.20.1. Minecraft upgrades it the first time you open it."));
        // only newer versions: the closest one is named
        QCOMPARE(worldFit({ S("1.21.5"), S("1.21.4") }, S("1.21.1"), S("Vanilla")).text,
                 S("Made for 1.21.4, newer than Vanilla (1.21.1): the game may not open it."));
        QCOMPARE(newestReleaseVersion({ S("1.9"), S("1.10"), S("Java 17"), S("Fabric") }), S("1.10"));

        // the preselected file: the newest one the target can open, else the newest
        QCOMPARE(defaultWorldFileIndex({ { S("26.3") }, { S("1.21.4") }, { S("1.21.1") }, { S("1.20.1") } }, S("1.21.1")), 2);
        QCOMPARE(defaultWorldFileIndex({ { S("26.3") }, { S("1.20.1") } }, S("1.21.1")), 1);
        QCOMPARE(defaultWorldFileIndex({ { S("26.3") }, { S("1.21.4") } }, S("1.21.1")), 0);
        QCOMPARE(defaultWorldFileIndex({ { S("24w10a") } }, S("1.21.1")), 0);
        QCOMPARE(defaultWorldFileIndex({}, S("1.21.1")), -1);
    }

    // ---- the add dialog ----

    void dialogInWorldMode()
    {
        RemoteImageCache images([](const QString&, std::function<void(const RemoteImageCache::FetchResult&)> done) { done({}); });
        ModInstallEnvironment env;
        env.images = &images;
        env.packs = true;
        env.worlds = true;
        env.instanceIcon = [](const QString&) { return QIcon(); };
        bool running = false;
        env.isRunning = [&running](const QString&) { return running; };
        // newest first, all of them: nothing is filtered by the target's version
        const auto versions = QList<ModPlatform::IndexedVersion>{ indexedVersion(S("f3"), S("Remix 3"), { S("1.21.4") }),
                                                                  indexedVersion(S("f2"), S("Remix 2"), { S("1.21.1") }),
                                                                  indexedVersion(S("f1"), S("Remix 1"), { S("1.20.1") }),
                                                                  indexedVersion(S("f0"), S("Remix 0"), {}) };
        ModInstallDialog dialog(env, worldWithVersions(versions), vanilla121(), QString());
        QCOMPARE(dialog.versionBox()->count(), 4);
        // preselected: the newest file the instance can open (Remix 2, 1.21.1), not the newest file (1.21.4)
        QCOMPARE(dialog.versionBox()->currentIndex(), 1);
        QCOMPARE(dialog.selectedVersion()->fileId.toString(), S("f2"));
        QCOMPARE(dialog.worldNoteText(), QString());
        dialog.versionBox()->setCurrentIndex(0);
        QCOMPARE(dialog.primaryButton()->text(), S("Add world"));
        QVERIFY(dialog.canInstall());
        QVERIFY(!dialog.opensWebsite());
        QCOMPARE(dialog.targetLine(), S("Adding to Vanilla · 1.21.1"));
        QVERIFY(!dialog.turnOnBox()->isVisibleTo(&dialog));
        QVERIFY(!dialog.skipDependenciesBox()->isVisibleTo(&dialog));
        QVERIFY(!dialog.shaderNote()->isVisibleTo(&dialog));
        // the newest file is made for a newer game than the instance
        QCOMPARE(dialog.worldNoteText(), S("Made for 1.21.4, newer than Vanilla (1.21.1): the game may not open it."));
        QVERIFY(dialog.worldNote()->property("zeroTone").toString() == S("warning"));

        dialog.versionBox()->setCurrentIndex(1);  // made for the instance's own version
        QCOMPARE(dialog.worldNoteText(), QString());
        dialog.versionBox()->setCurrentIndex(2);  // older
        QCOMPARE(dialog.worldNoteText(), S("Made for 1.20.1. Minecraft upgrades it the first time you open it."));
        dialog.versionBox()->setCurrentIndex(3);  // unknown
        QCOMPARE(dialog.worldNoteText(), QString());
        QVERIFY(dialog.canInstall());

        // the target runs: refused, with the reason
        running = true;
        ModInstallDialog busy(env, worldWithVersions(versions), vanilla121(), QString());
        QVERIFY(!busy.canInstall());
        QCOMPARE(busy.noteText(), S("Close Vanilla to add worlds"));
    }

    void dialogWithoutDownloadUrlOpensTheProjectPage()
    {
        RemoteImageCache images([](const QString&, std::function<void(const RemoteImageCache::FetchResult&)> done) { done({}); });
        ModInstallEnvironment env;
        env.images = &images;
        env.packs = true;
        env.worlds = true;
        env.instanceIcon = [](const QString&) { return QIcon(); };
        bool running = false;
        env.isRunning = [&running](const QString&) { return running; };
        const auto versions = QList<ModPlatform::IndexedVersion>{ indexedVersion(S("f2"), S("Locked"), { S("1.21.1") }, false) };

        ModInstallDialog locked(env, worldWithVersions(versions), vanilla121(), QString());
        QCOMPARE(locked.versionBox()->count(), 1);  // listed all the same
        QVERIFY(locked.opensWebsite());
        QCOMPARE(locked.worldNoteText(), S("This world can only be downloaded on CurseForge."));
        QCOMPARE(locked.primaryButton()->text(), S("Open on CurseForge"));
        QVERIFY(locked.canInstall());
        QCOMPARE(locked.websiteUrl().toString(), S("https://www.curseforge.com/minecraft/worlds/skyblock-remix"));

        // the primary action works while the target runs (nothing is added), but only with a web address
        running = true;
        ModInstallDialog runningLocked(env, worldWithVersions(versions), vanilla121(), QString());
        QVERIFY(runningLocked.canInstall());
        ModInstallDialog noPage(env, worldWithVersions(versions, S("javascript:alert(1)")), vanilla121(), QString());
        QVERIFY(noPage.opensWebsite());
        QVERIFY(!noPage.websiteUrl().isValid());  // http / https only
        QVERIFY(!noPage.canInstall());
        ModInstallDialog noUrl(env, worldWithVersions(versions, S("")), vanilla121(), QString());
        QVERIFY(!noUrl.canInstall());

        QVERIFY(!worldFileDownloadable(versions.first()));
        QVERIFY(worldFileDownloadable(indexedVersion(S("f"), S("Ok"), {})));
        ModPlatform::IndexedVersion noName = indexedVersion(S("g"), S("Ok"), {});
        noName.fileName.clear();
        QVERIFY(!worldFileDownloadable(noName));
    }

    void noFilesAtAll()
    {
        RemoteImageCache images([](const QString&, std::function<void(const RemoteImageCache::FetchResult&)> done) { done({}); });
        ModInstallEnvironment env;
        env.images = &images;
        env.packs = true;
        env.worlds = true;
        env.instanceIcon = [](const QString&) { return QIcon(); };
        ModInstallDialog dialog(env, worldWithVersions({}), vanilla121(), QString());
        QVERIFY(!dialog.canInstall());
        QCOMPARE(dialog.versionStatus(), S("This world has no files to add."));
    }

    // ---- the pure install pieces ----

    void webUrlOnlyAllowsHttp()
    {
        QCOMPARE(webUrl(S("https://www.curseforge.com/minecraft/worlds/x")).toString(), S("https://www.curseforge.com/minecraft/worlds/x"));
        QVERIFY(webUrl(S("http://example.test/a")).isValid());
        QVERIFY(!webUrl(S("file:///C:/Windows/System32/calc.exe")).isValid());
        QVERIFY(!webUrl(S("javascript:alert(1)")).isValid());
        QVERIFY(!webUrl(S("ftp://example.test/a")).isValid());
        QVERIFY(!webUrl(QString()).isValid());
    }

    void downloadPathAndCleanup()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString instanceRoot = root.path();
        QCOMPARE(worldDownloadDir(instanceRoot), QDir(instanceRoot).filePath(S(".zero/world-download")));

        // a plain file name inside the download folder, whatever the provider sent
        const QString path = worldDownloadPath(instanceRoot, S("12345"), S("My Map 1.20.zip"));
        QCOMPARE(path, QDir(worldDownloadDir(instanceRoot)).filePath(S("12345-My Map 1.20.zip")));
        const QString sneaky = worldDownloadPath(instanceRoot, S("../9"), S("..\\..\\evil:name?.zip"));
        QVERIFY(QDir::cleanPath(sneaky).startsWith(QDir::cleanPath(worldDownloadDir(instanceRoot)) + QLatin1Char('/')));
        QCOMPARE(QFileInfo(sneaky).dir().absolutePath(), QFileInfo(path).dir().absolutePath());
        QVERIFY(!QFileInfo(sneaky).fileName().contains(QLatin1Char(':')));
        QVERIFY(!QFileInfo(sneaky).fileName().contains(QLatin1Char('?')));
        QVERIFY(worldDownloadPath(instanceRoot, QString(), QString()).endsWith(S("/world.zip")));

        // cleanup: the file goes, and the folders it made when they are empty now; whatever the outcome was
        QVERIFY(QDir().mkpath(worldDownloadDir(instanceRoot)));
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("zip");
        }
        QVERIFY(removeWorldDownload(path, instanceRoot));
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(!QFileInfo::exists(worldDownloadDir(instanceRoot)));
        QVERIFY(!QFileInfo::exists(QDir(instanceRoot).filePath(S(".zero"))));
        QVERIFY(removeWorldDownload(path, instanceRoot));  // already gone (an aborted download that never started): still fine

        // somebody else's file in .zero stays
        QVERIFY(QDir().mkpath(worldDownloadDir(instanceRoot)));
        QFile other(QDir(instanceRoot).filePath(S(".zero/keep.txt")));
        QVERIFY(other.open(QIODevice::WriteOnly));
        other.close();
        QFile partial(path);
        QVERIFY(partial.open(QIODevice::WriteOnly));
        partial.close();  // a download that stopped half way
        QVERIFY(removeWorldDownload(path, instanceRoot));
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(!QFileInfo::exists(worldDownloadDir(instanceRoot)));
        QVERIFY(QFileInfo::exists(other.fileName()));
    }

    void theFolderTheInstallMade()
    {
        QCOMPARE(newWorldFolder({ S("A"), S("B") }, { S("A"), S("B"), S("Skyblock Remix") }), S("Skyblock Remix"));
        QCOMPARE(newWorldFolder({}, { S("Only") }), S("Only"));
        QCOMPARE(newWorldFolder({ S("A") }, { S("A") }), QString());                      // nothing new
        QCOMPARE(newWorldFolder({ S("A") }, { S("A"), S("B"), S("C") }), QString());      // ambiguous: no guess
    }

    // ---- the switcher and the page ----

    void switcherHasAFifthSegmentOnlyWithCurseForge()
    {
        Rig modsRig, packsRig, shadersRig, worldsRig;
        FakeSource modpackSource(Provider::Modrinth);
        ModpacksEnvironment modpacks;
        modpacks.sources.modrinth = &modpackSource;
        modpacks.images = &worldsRig.images;
        QString section = S("worlds");
        QString target;
        DiscoverHooks plainHooks;
        QStringList written;
        DiscoverHooks hooks;
        hooks.loadSection = [&]() { return section; };
        hooks.saveSection = [&](const QString& id) {
            section = id;
            written << id;
        };
        hooks.loadTarget = [&]() { return target; };
        hooks.saveTarget = [&](const QString& id) { target = id; };
        hooks.readTargets = []() { return QList<TargetInstance>{ noVersion(), vanilla121(), fabric121() }; };

        // no CurseForge key: the application page builds no worlds environment: four segments, no stub
        {
            plainHooks = hooks;
            plainHooks.saveSection = nullptr;  // the two pages must not share what is remembered
            DiscoverPage plain(modpacks, modsRig.environment(), packsRig.environment(), shadersRig.environment(), plainHooks, nullptr);
            QCOMPARE(plain.switcher()->count(), 4);
            QVERIFY(plain.worlds() == nullptr);
            QCOMPARE(plain.sections(), (QStringList{ S("modpacks"), S("mods"), S("resourcepacks"), S("shaders") }));
            QCOMPARE(plain.currentSection(), S("modpacks"));  // "worlds" is remembered but not built
            QVERIFY(!plain.showWorldsFor(S("van")));
        }

        DiscoverPage page(modpacks, modsRig.environment(), packsRig.environment(), shadersRig.environment(), worldsRig.environment(), hooks,
                          nullptr);
        page.setLibrarySelection([]() { return QString(); });
        page.resize(1280, 720);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        QCOMPARE(page.switcher()->count(), 5);
        QCOMPARE(page.switcher()->valueAt(4), S("worlds"));
        QCOMPARE(page.sections(), (QStringList{ S("modpacks"), S("mods"), S("resourcepacks"), S("shaders"), S("worlds") }));
        QCOMPARE(page.currentSection(), S("worlds"));  // restored on start
        QVERIFY(page.current() == page.worlds());
        QCOMPARE(page.worlds()->target().id, S("fab"));
        QVERIFY(!worldsRig.curseforge.searches.isEmpty());
        QCOMPARE(worldsRig.curseforge.searches.first().args.type, ModPlatform::ResourceType::World);
        QVERIFY(shadersRig.curseforge.searches.isEmpty());  // the other sections wait until they are shown
        QVERIFY(written.isEmpty());

        page.switcher()->onActivated(S("shaders"));
        QCOMPARE(written.last(), S("shaders"));
        page.switcher()->onActivated(S("worlds"));
        QCOMPARE(page.currentSection(), S("worlds"));
        QCOMPARE(written.last(), S("worlds"));
        // a provider that worlds do not have changes nothing
        page.showSection(S("worlds"), S("modrinth"));
        QCOMPARE(page.worlds()->provider(), Provider::CurseForge);

        // showWorldsFor: the target is chosen and remembered; a query becomes the search text
        QVERIFY(page.showWorldsFor(S("van"), S("skyblock")));
        QCOMPARE(page.worlds()->target().id, S("van"));
        QCOMPARE(target, S("van"));
        QCOMPARE(page.worlds()->state().query, S("skyblock"));
        QVERIFY(!page.showWorldsFor(S("nov")));  // no Minecraft version: not a target

        // signals
        QSignalSpy install(&page, &DiscoverPage::worldInstallRequested);
        QSignalSpy shaderInstall(&page, &DiscoverPage::shaderInstallRequested);
        QSignalSpy createVanilla(&page, &DiscoverPage::createVanillaInstanceRequested);
        emit page.worlds()->installRequested(makeWorld(S("1"), S("A")), S("fab"), QString());
        QCOMPARE(install.size(), 1);
        QCOMPARE(shaderInstall.size(), 0);
        emit page.worlds()->createInstanceRequested();
        QCOMPARE(createVanilla.size(), 1);

        // the cart's target is never the worlds section: it stays on the mod-ready sections
        QVERIFY(page.cartTargetSection() != page.worlds());
    }

    void fiveSegmentsFitTheTopBar()
    {
        Rig modsRig, packsRig, shadersRig, worldsRig;
        FakeSource modpackSource(Provider::Modrinth);
        ModpacksEnvironment modpacks;
        modpacks.sources.modrinth = &modpackSource;
        modpacks.images = &worldsRig.images;
        DiscoverHooks hooks;
        hooks.loadSection = []() { return S("worlds"); };
        hooks.readTargets = []() { return QList<TargetInstance>{ vanilla121() }; };

        for (const QSize size : { QSize(1100, 700), QSize(1280, 800) }) {
            ZeroShell shell;
            auto* page = new DiscoverPage(modpacks, modsRig.environment(), packsRig.environment(), shadersRig.environment(),
                                          worldsRig.environment(), hooks, nullptr);
            shell.addPage(S("discover"), S("compass"), S("Discover"), page);
            shell.finishRail();
            shell.topBar()->setCenterWidget(new ZeroSearchPill);
            shell.resize(1400, size.height());
            shell.show();
            QVERIFY(QTest::qWaitForWindowExposed(&shell));
            shell.setCurrentPage(S("discover"));
            // Like the user shrinking the window: the first resize below 1200 px collapses the nav rail (which frees the room the next
            // resize needs); a window never needs more than its own width.
            shell.resize(1190, size.height());
            QApplication::processEvents();
            shell.resize(size);
            QApplication::processEvents();

            ZeroTopBar* bar = shell.topBar();
            QCOMPARE(shell.size(), size);  // the layout did not need more room than the window has
            QWidget* actions = bar->actionsWidget();
            QVERIFY(actions == page->switcher());
            QVERIFY(actions->isVisible());
            QWidget* pill = bar->centerWidget();
            QVERIFY(pill->isVisible());
            // the switcher is whole: its natural width, inside the bar, left of the account chip, right of the search pill
            QVERIFY2(actions->width() >= actions->sizeHint().width(), qPrintable(S("%1 < %2").arg(actions->width()).arg(actions->sizeHint().width())));
            const QRect actionsRect(actions->mapTo(bar, QPoint(0, 0)), actions->size());
            const QRect pillRect(pill->mapTo(bar, QPoint(0, 0)), pill->size());
            const QRect chipRect(bar->accountChip()->mapTo(bar, QPoint(0, 0)), bar->accountChip()->size());
            QVERIFY2(actionsRect.right() < chipRect.left(), qPrintable(S("switcher %1 chip %2").arg(actionsRect.right()).arg(chipRect.left())));
            QVERIFY2(pillRect.right() < actionsRect.left(), qPrintable(S("pill %1 switcher %2").arg(pillRect.right()).arg(actionsRect.left())));
            QVERIFY2(chipRect.right() <= bar->width(), "the account chip is cut off");
            // the search pill gives way down to 200 px and never below
            QVERIFY2(pillRect.width() >= ZeroSearchPill::kMinWidth,
                     qPrintable(S("pill is %1 px wide at %2 px").arg(pillRect.width()).arg(size.width())));
            QCOMPARE(int(ZeroSearchPill::kMinWidth), 200);
            // the title keeps its room too
            QVERIFY(bar->titleBlock()->width() >= bar->titleBlock()->minimumSizeHint().width());
        }
    }
};

QTEST_MAIN(ZeroDiscoverWorldsTest)

#include "ZeroDiscoverWorlds_test.moc"
