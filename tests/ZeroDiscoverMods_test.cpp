// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the Discover > Mods section and the section switcher (docs/zero/CONTENT-MODS.md): the target's version and
// loaders as default chips, a target change = new defaults + new search + new version filter, CurseForge only for loaders it serves,
// the installed mods (Installed pill, Hide installed, Add to / Installed / Update, the running target), the empty state without a
// mod-ready instance, the "Not for" fact, the client-side version filter, and the page's remembered section and target rule.
// Offscreen, with fake sources and hand-built TargetInstances: no network, no instances, no APPLICATION.
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <functional>

#include "ui/zero/ZeroEmptyState.h"
#include "ui/zero/ZeroSkeleton.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/content/ContentDetailsPanel.h"
#include "ui/zero/content/ContentToolbar.h"
#include "ui/zero/content/FilterChipStrip.h"
#include "ui/zero/content/FilterPanel.h"
#include "ui/zero/content/ModRows.h"
#include "ui/zero/content/ModpackRows.h"
#include "ui/zero/content/ModpacksSection.h"
#include "ui/zero/content/ModsSection.h"
#include "ui/zero/content/RemoteImageCache.h"
#include "ui/zero/pages/DiscoverPage.h"
#include "zero/content/InstalledMods.h"

using namespace Zero;
using namespace Zero::Content;

namespace {

void setMotion(bool on)
{
    ThemeSpec spec = ThemeSpec::obsidian();
    spec.reduceMotion = !on;
    Theme::setCurrent(spec);
}

class FakeSource : public ModpackSource {
   public:
    struct SearchCall {
        ResourceAPI::SearchArgs args;
        quint64 generation = 0;
    };
    struct ItemCall {
        ModpackItemPtr item;
        quint64 generation = 0;
    };

    explicit FakeSource(Provider provider) : m_provider(provider) {}

    Provider provider() const override { return m_provider; }
    void search(const ResourceAPI::SearchArgs& args, quint64 generation) override { searches.append({ args, generation }); }
    void details(const ModpackItemPtr& item, quint64 generation) override { detailCalls.append({ item, generation }); }
    void versions(const ModpackItemPtr& item, quint64 generation) override { versionCalls.append({ item, generation }); }
    void changelog(const ModpackItemPtr&, const QString&, quint64) override {}

    void answerSearch(int call, const QList<ModpackItemPtr>& items)
    {
        emit searchFinished(searches.at(call).generation, searches.at(call).args.offset, items, int(items.size()));
    }
    void answerVersions(int call, const QList<ModpackVersion>& versions)
    {
        emit versionsFinished(versionCalls.at(call).generation, versionCalls.at(call).item, versions);
    }

    QList<SearchCall> searches;
    QList<ItemCall> detailCalls;
    QList<ItemCall> versionCalls;

   private:
    Provider m_provider;
};

class FakeInstalledMods : public InstalledModsSource {
   public:
    void setInstance(const QString& instanceId) override { instance = instanceId; }
    void setActive(bool on) override
    {
        active = on;
        ++activations;
    }
    QList<InstalledMod> mods() const override { return list; }
    void set(const QList<InstalledMod>& mods)
    {
        list = mods;
        emit changed();
    }

    QString instance;
    bool active = false;
    int activations = 0;
    QList<InstalledMod> list;
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

TargetInstance fabric121() { return makeTarget(QStringLiteral("fab"), QStringLiteral("Fabric Fun"), QStringLiteral("1.21.1"), ModPlatform::Fabric); }
TargetInstance forge1201() { return makeTarget(QStringLiteral("forge"), QStringLiteral("Forge Run"), QStringLiteral("1.20.1"), ModPlatform::Forge); }
TargetInstance vanilla() { return makeTarget(QStringLiteral("van"), QStringLiteral("Vanilla"), QStringLiteral("1.21.1"), ModPlatform::ModLoaderTypes()); }

ModpackItemPtr makeMod(const QString& id, const QString& name, const QStringList& versions = { QStringLiteral("1.21.1") },
                       const QStringList& loaders = { QStringLiteral("fabric") }, Provider provider = Provider::Modrinth)
{
    ModPlatform::IndexedPack pack;
    pack.addonId = id;
    pack.provider = provider == Provider::CurseForge ? ModPlatform::ResourceProvider::FLAME : ModPlatform::ResourceProvider::MODRINTH;
    pack.name = name;
    pack.slug = name.toLower();
    pack.description = QStringLiteral("About %1").arg(name);
    pack.authors = { { QStringLiteral("someone"), QString() } };
    pack.logoUrl = QStringLiteral("https://example.test/icon/%1.png").arg(id);
    pack.gameVersions = versions;
    pack.loaderNames = loaders;
    pack.downloads = 1234567;
    pack.websiteUrl = QStringLiteral("https://example.test/%1").arg(id);
    return itemFromPack(provider, pack);
}

QList<ModpackItemPtr> makeMods(int count)
{
    QList<ModpackItemPtr> out;
    for (int i = 1; i <= count; ++i) {
        out.append(makeMod(QString::number(i), QStringLiteral("Mod %1").arg(i)));
    }
    return out;
}

ModpackVersion makeVersion(const QString& id, const QString& name, const QString& mc = QStringLiteral("1.21.1"),
                           const QString& loader = QStringLiteral("fabric"), ModpackVersion::Type type = ModpackVersion::Type::Release)
{
    ModpackVersion v;
    v.id = id;
    v.name = name;
    v.downloadUrl = QStringLiteral("https://cdn.test/") + id;
    v.type = type;
    v.gameVersions = { mc };
    v.loaders = { loader };
    v.date = QDateTime(QDate(2024, 3, 12), QTime(10, 0), Qt::UTC);
    return v;
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

struct Rig {
    FakeSource modrinth{ Provider::Modrinth };
    FakeSource curseforge{ Provider::CurseForge };
    FakeInstalledMods installed;
    RemoteImageCache images{ [](const QString&, std::function<void(const RemoteImageCache::FetchResult&)> done) { done({}); } };
    QString savedFilter;
    QString loadFilter;
    std::unique_ptr<ModsSection> section;

    ModsEnvironment environment(bool withCurseForge = true)
    {
        ModsEnvironment env;
        env.sources.modrinth = &modrinth;
        env.sources.curseforge = withCurseForge ? &curseforge : nullptr;
        env.images = &images;
        env.installedMods = &installed;
        env.icons = [](const QString&) {
            QPixmap pm(20, 20);
            pm.fill(Qt::darkGreen);
            return QIcon(pm);
        };
        env.loadFilter = [this]() { return loadFilter; };
        env.saveFilter = [this](const QString& json) { savedFilter = json; };
        return env;
    }

    void open(bool withCurseForge = true, const QSize& size = QSize(1280, 720))
    {
        section = std::make_unique<ModsSection>(environment(withCurseForge));
        section->resize(size);
        section->show();
        QVERIFY2(QTest::qWaitForWindowExposed(section.get()), "section not exposed");
    }

    /// Opens with `targets`, `preferred` chosen, and answers the first search with `items`.
    void openWith(const QList<TargetInstance>& targets, const QString& preferred, const QList<ModpackItemPtr>& items = {})
    {
        open();
        section->setTargets(targets, preferred);
        if (!items.isEmpty()) {
            QVERIFY(!modrinth.searches.isEmpty());
            modrinth.answerSearch(int(modrinth.searches.size()) - 1, items);
        }
    }
};

}  // namespace

class ZeroDiscoverModsTest : public QObject {
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

    // ---- target: defaults, search, version filter ----

    void defaultsFromTarget()
    {
        Rig rig;
        rig.open();
        QVERIFY(rig.modrinth.searches.isEmpty());  // no target: nothing is searched
        rig.section->setTargets({ fabric121(), forge1201() }, QStringLiteral("fab"));
        QCOMPARE(rig.section->target().id, QStringLiteral("fab"));
        QCOMPARE(rig.section->state().values(FacetId::mcVersion), QStringList{ QStringLiteral("1.21.1") });
        QCOMPARE(rig.section->state().values(FacetId::loader), QStringList{ QStringLiteral("fabric") });
        QCOMPARE(rig.section->defaults().values(FacetId::mcVersion), QStringList{ QStringLiteral("1.21.1") });
        QCOMPARE(rig.section->chipStrip()->chipLabels(), (QStringList{ QStringLiteral("Minecraft version: 1.21.1"), QStringLiteral("Loader: Fabric") }));
        // exactly the arguments of ModModel::createSearchArguments
        QCOMPARE(rig.modrinth.searches.size(), 1);
        const auto& args = rig.modrinth.searches[0].args;
        QCOMPARE(args.type, ModPlatform::ResourceType::Mod);
        QVERIFY(args.versions.has_value());
        QCOMPARE(int(args.versions->size()), 1);
        QCOMPARE(args.versions->front().toString(), QStringLiteral("1.21.1"));
        QVERIFY(args.loaders.has_value());
        QCOMPARE(*args.loaders, ModPlatform::ModLoaderTypes(ModPlatform::Fabric));
        // the section's own work: versions requests carry the target
        QCOMPARE(rig.modrinth.versionFilter().mcVersions, QStringList{ QStringLiteral("1.21.1") });
        QCOMPARE(rig.modrinth.versionFilter().loaders, ModPlatform::ModLoaderTypes(ModPlatform::Fabric));
        // the section is shown: the installed mods are watched for the target
        QCOMPARE(rig.installed.instance, QStringLiteral("fab"));
        QVERIFY(rig.installed.active);
    }

    void quiltTargetAcceptsFabricMods()
    {
        Rig rig;
        rig.open();
        rig.section->setTargets({ makeTarget(QStringLiteral("q"), QStringLiteral("Quilty"), QStringLiteral("1.20.1"), ModPlatform::Quilt | ModPlatform::Fabric) },
                                QStringLiteral("q"));
        QCOMPARE(rig.section->state().values(FacetId::loader), (QStringList{ QStringLiteral("fabric"), QStringLiteral("quilt") }));
        QVERIFY(rig.modrinth.searches.last().args.loaders.has_value());
        QCOMPARE(*rig.modrinth.searches.last().args.loaders, ModPlatform::ModLoaderTypes(ModPlatform::Fabric | ModPlatform::Quilt));
    }

    void targetChangeStartsNewSearch()
    {
        Rig rig;
        rig.openWith({ fabric121(), forge1201() }, QStringLiteral("fab"), makeMods(3));
        // another facet the user chose stays; the version and loader chips follow the target
        FilterState user = rig.section->state();
        user.selected.insert(FacetId::category, { QStringLiteral("magic") });
        rig.section->applyState(user);
        const int before = int(rig.modrinth.searches.size());

        QVERIFY(rig.section->chooseTarget(QStringLiteral("forge")));
        QCOMPARE(int(rig.modrinth.searches.size()), before + 1);
        const auto& args = rig.modrinth.searches.last().args;
        QCOMPARE(args.versions->front().toString(), QStringLiteral("1.20.1"));
        QCOMPARE(*args.loaders, ModPlatform::ModLoaderTypes(ModPlatform::Forge));
        QCOMPARE(rig.section->state().values(FacetId::mcVersion), QStringList{ QStringLiteral("1.20.1") });
        QCOMPARE(rig.section->state().values(FacetId::loader), QStringList{ QStringLiteral("forge") });
        QCOMPARE(rig.section->state().values(FacetId::category), QStringList{ QStringLiteral("magic") });
        QCOMPARE(rig.section->defaults().values(FacetId::mcVersion), QStringList{ QStringLiteral("1.20.1") });
        QCOMPARE(rig.modrinth.versionFilter().mcVersions, QStringList{ QStringLiteral("1.20.1") });
        QCOMPARE(rig.modrinth.versionFilter().loaders, ModPlatform::ModLoaderTypes(ModPlatform::Forge));
        QCOMPARE(rig.installed.instance, QStringLiteral("forge"));
        QCOMPARE(rig.section->targetButton()->currentId(), QStringLiteral("forge"));
        // the old results' answer is stale now
        rig.modrinth.answerSearch(before - 1, makeMods(5));
        QVERIFY(rig.section->model()->loadedCount() != 5);
    }

    void sameContextTargetDoesNotSearchAgain()
    {
        Rig rig;
        rig.openWith({ fabric121() }, QStringLiteral("fab"), makeMods(2));
        const int before = int(rig.modrinth.searches.size());
        // renamed, running: the section keeps its results and only the action changes
        auto renamed = fabric121();
        renamed.name = QStringLiteral("Renamed");
        renamed.running = true;
        rig.section->setTargets({ renamed }, QStringLiteral("fab"));
        QCOMPARE(int(rig.modrinth.searches.size()), before);
        QCOMPARE(rig.section->target().name, QStringLiteral("Renamed"));
        QVERIFY(rig.section->target().running);
    }

    void currentTargetStaysWhilePreferredChanges()
    {
        Rig rig;
        rig.openWith({ fabric121(), forge1201() }, QStringLiteral("fab"));
        QVERIFY(rig.section->chooseTarget(QStringLiteral("forge")));
        rig.section->setTargets({ fabric121(), forge1201() }, QStringLiteral("fab"));  // a list refresh: the user's pick stays
        QCOMPARE(rig.section->target().id, QStringLiteral("forge"));
        // the picked instance loses its loader: back to the preferred one
        rig.section->setTargets({ fabric121(), makeTarget(QStringLiteral("forge"), QStringLiteral("Forge Run"), QStringLiteral("1.20.1"), ModPlatform::ModLoaderTypes()) },
                                QStringLiteral("fab"));
        QCOMPARE(rig.section->target().id, QStringLiteral("fab"));
        // and it is gone: no target, the empty state
        rig.section->setTargets({ vanilla() }, QStringLiteral("van"));
        QVERIFY(!rig.section->hasTarget());
    }

    void userPickIsAnnounced()
    {
        Rig rig;
        rig.openWith({ fabric121(), forge1201() }, QStringLiteral("fab"));
        QSignalSpy picked(rig.section.get(), &ModsSection::targetPicked);
        emit rig.section->targetButton()->targetChosen(QStringLiteral("forge"));
        QCOMPARE(picked.size(), 1);
        QCOMPARE(picked.first().first().toString(), QStringLiteral("forge"));
        QCOMPARE(rig.section->target().id, QStringLiteral("forge"));
        // choosing programmatically is not a pick
        QVERIFY(rig.section->chooseTarget(QStringLiteral("fab")));
        QCOMPARE(picked.size(), 1);
    }

    void versionFilterReachesAnOpenItem()
    {
        Rig rig;
        rig.openWith({ fabric121(), forge1201() }, QStringLiteral("fab"), makeMods(2));
        rig.section->selectItem(rig.section->model()->item(0));
        QTRY_COMPARE(rig.modrinth.versionCalls.size(), 1);
        rig.modrinth.answerVersions(0, { makeVersion(QStringLiteral("a"), QStringLiteral("1.0")) });
        QCOMPARE(rig.section->panel()->versionRowCount(), 1);
        // a target change refreshes the open item: its versions are asked again under the new filter
        QVERIFY(rig.section->chooseTarget(QStringLiteral("forge")));
        QCOMPARE(rig.section->panel()->versionsState(), LoadState::Loading);
        QTRY_COMPARE(rig.modrinth.versionCalls.size(), 2);
        QCOMPARE(rig.modrinth.versionFilter().mcVersions, QStringList{ QStringLiteral("1.20.1") });
    }

    // ---- CurseForge ----

    void curseForgeNeedsASupportedLoader()
    {
        {
            Rig rig;
            rig.openWith({ fabric121() }, QStringLiteral("fab"));
            const auto provider = facetOf(rig.section->facets(), FacetId::provider);
            QVERIFY(provider.isEnabled());
            QCOMPARE(provider.options.size(), 2);
            QVERIFY(rig.section->curseForgeAvailable());
        }
        {
            // LiteLoader: Modrinth knows it, CurseForge does not
            Rig rig;
            rig.openWith({ makeTarget(QStringLiteral("lite"), QStringLiteral("Lite"), QStringLiteral("1.12.2"), ModPlatform::LiteLoader) }, QStringLiteral("lite"));
            QVERIFY(rig.section->hasTarget());
            QVERIFY(!rig.section->curseForgeAvailable());
            const auto provider = facetOf(rig.section->facets(), FacetId::provider);
            QVERIFY(!provider.isEnabled());
            QVERIFY(provider.disabledReason.contains(QStringLiteral("CurseForge")));
        }
        {
            // no CurseForge source (no API key): the option does not exist, the facet is not "disabled"
            Rig rig;
            rig.open(false);
            rig.section->setTargets({ fabric121() }, QStringLiteral("fab"));
            const auto provider = facetOf(rig.section->facets(), FacetId::provider);
            QVERIFY(provider.isEnabled());
            QCOMPARE(provider.options.size(), 1);
            QVERIFY(!rig.section->curseForgeAvailable());
        }
    }

    void curseForgeSelectionFallsBackWithTheTarget()
    {
        Rig rig;
        rig.openWith({ fabric121(), makeTarget(QStringLiteral("lite"), QStringLiteral("Lite"), QStringLiteral("1.12.2"), ModPlatform::LiteLoader) },
                     QStringLiteral("fab"));
        rig.section->setProvider(Provider::CurseForge);
        QCOMPARE(rig.section->provider(), Provider::CurseForge);
        QVERIFY(!rig.curseforge.searches.isEmpty());
        QCOMPARE(rig.curseforge.searches.last().args.type, ModPlatform::ResourceType::Mod);
        QVERIFY(rig.section->chooseTarget(QStringLiteral("lite")));
        QCOMPARE(rig.section->provider(), Provider::Modrinth);
    }

    void environmentFacetPerProvider()
    {
        Rig rig;
        rig.openWith({ fabric121() }, QStringLiteral("fab"));
        auto env = facetOf(rig.section->facets(), FacetId::environment);
        QVERIFY(env.isEnabled());
        rig.section->setProvider(Provider::CurseForge);
        env = facetOf(rig.section->facets(), FacetId::environment);
        QCOMPARE(env.disabledReason, QStringLiteral("CurseForge has no environment filter"));
    }

    // ---- facets ----

    void facetsFollowTheSpec()
    {
        Rig rig;
        rig.openWith({ fabric121() }, QStringLiteral("fab"));
        QStringList ids;
        for (const auto& f : rig.section->facets()) {
            ids << f.id;
            if (f.side == FacetSide::Client) {
                QVERIFY2(f.isEnabled(), qPrintable(f.id));  // a target exists: the installed facets work
            }
        }
        // "Update available" is hidden until the installed manager provides update data
        QCOMPARE(ids, (QStringList{ FacetId::provider, FacetId::mcVersion, FacetId::loader, FacetId::category, FacetId::environment, FacetId::installed,
                                    FacetId::hideInstalled }));
    }

    void filterPersistenceLeavesTheTargetOut()
    {
        Rig rig;
        rig.openWith({ fabric121() }, QStringLiteral("fab"));
        FilterState s = rig.section->state();
        s.selected.insert(FacetId::environment, { QStringLiteral("client") });
        rig.section->applyState(s);
        const auto saved = QJsonDocument::fromJson(rig.savedFilter.toUtf8()).object().value(QStringLiteral("selected")).toObject();
        QVERIFY(saved.contains(FacetId::environment));
        QVERIFY(!saved.contains(FacetId::mcVersion));  // they follow the target, not the last session
        QVERIFY(!saved.contains(FacetId::loader));
    }

    // ---- installed ----

    void installedKeysShowPillAndFilter()
    {
        Rig rig;
        rig.installed.list = { { QStringLiteral("modrinth:2"), QStringLiteral("f2"), QStringLiteral("Mod 2"), QStringLiteral("2.0") } };
        rig.openWith({ fabric121() }, QStringLiteral("fab"), makeMods(4));
        auto* model = rig.section->model();
        QCOMPARE(model->visibleCount(), 4);
        QVERIFY(model->data(model->index(1), ModpackListModel::InstalledRole).toBool());
        QVERIFY(!model->data(model->index(0), ModpackListModel::InstalledRole).toBool());

        // Hide installed
        FilterState s = rig.section->state();
        s.selected.insert(FacetId::hideInstalled, { kToggleOn });
        rig.section->applyState(s);
        QCOMPARE(model->visibleCount(), 3);
        // Installed in target
        s.selected.remove(FacetId::hideInstalled);
        s.selected.insert(FacetId::installed, { kToggleOn });
        rig.section->applyState(s);
        QCOMPARE(model->visibleCount(), 1);
        QCOMPARE(model->item(0)->id, QStringLiteral("2"));

        // The installed folder finished loading later: the pill appears without a new search
        s.selected.remove(FacetId::installed);
        rig.section->applyState(s);
        const int searches = int(rig.modrinth.searches.size());
        rig.installed.set({ { QStringLiteral("modrinth:3"), QStringLiteral("f3"), QStringLiteral("Mod 3"), QString() } });
        QVERIFY(model->data(model->index(2), ModpackListModel::InstalledRole).toBool());
        QVERIFY(!model->data(model->index(1), ModpackListModel::InstalledRole).toBool());
        QCOMPARE(int(rig.modrinth.searches.size()), searches);
    }

    void primaryActionStates()
    {
        Rig rig;
        rig.installed.list = { { QStringLiteral("modrinth:1"), QStringLiteral("v2"), QStringLiteral("Mod 1"), QStringLiteral("2.0") },
                               { QStringLiteral("modrinth:2"), QStringLiteral("v0"), QStringLiteral("Mod 2"), QStringLiteral("0.5") } };
        rig.openWith({ fabric121() }, QStringLiteral("fab"), makeMods(3));
        auto* panel = rig.section->panel();
        const QList<ModpackVersion> versions = { makeVersion(QStringLiteral("v2"), QStringLiteral("2.0")), makeVersion(QStringLiteral("v1"), QStringLiteral("1.0")) };

        auto open = [&](int row, int call) {
            rig.section->selectRow(row);
            QTRY_COMPARE(rig.modrinth.versionCalls.size(), call + 1);
            QVERIFY(!panel->primaryButton()->isEnabled());  // not before its versions are known
            rig.modrinth.answerVersions(call, versions);
        };
        // newest version installed: "Installed", disabled
        open(0, 0);
        QCOMPARE(panel->primaryButton()->text(), QStringLiteral("Installed"));
        QVERIFY(!panel->primaryButton()->isEnabled());
        QCOMPARE(panel->primaryButton()->property("zeroInstalled").toBool(), true);
        // an older version installed: "Update"
        open(1, 1);
        QCOMPARE(panel->primaryButton()->text(), QStringLiteral("Update"));
        QVERIFY(panel->primaryButton()->isEnabled());
        QVERIFY(panel->noteText().contains(QStringLiteral("0.5")));
        QCOMPARE(panel->primaryButton()->property("zeroInstalled").toBool(), false);
        QSignalSpy requests(rig.section.get(), &ModsSection::installRequested);
        panel->primaryButton()->click();
        QCOMPARE(requests.size(), 1);
        QCOMPARE(requests.first().at(0).value<ContentItemPtr>()->id, QStringLiteral("2"));
        QCOMPARE(requests.first().at(1).toString(), QStringLiteral("fab"));
        QCOMPARE(requests.first().at(2).toString(), QStringLiteral("v0"));
        // not installed: "Add to <instance>"
        open(2, 2);
        QCOMPARE(panel->primaryButton()->text(), QStringLiteral("Add to Fabric Fun"));
        QVERIFY(panel->primaryButton()->isEnabled());
        panel->primaryButton()->click();
        QCOMPARE(requests.size(), 2);
        QCOMPARE(requests.last().at(2).toString(), QString());
        QCOMPARE(requests.last().at(1).toString(), QStringLiteral("fab"));
    }

    void runningTargetRefusesTheAction()
    {
        Rig rig;
        rig.openWith({ makeTarget(QStringLiteral("fab"), QStringLiteral("Fabric Fun"), QStringLiteral("1.21.1"), ModPlatform::Fabric, true) },
                     QStringLiteral("fab"), makeMods(1));
        rig.section->selectRow(0);
        QTRY_COMPARE(rig.modrinth.versionCalls.size(), 1);
        rig.modrinth.answerVersions(0, { makeVersion(QStringLiteral("v1"), QStringLiteral("1.0")) });
        auto* panel = rig.section->panel();
        QVERIFY(!panel->primaryButton()->isEnabled());
        QCOMPARE(panel->noteText(), QStringLiteral("Close Fabric Fun to add mods"));
        QCOMPARE(panel->primaryButton()->text(), QStringLiteral("Add to Fabric Fun"));
        // closed again: the action works
        auto closed = fabric121();
        rig.section->setTargets({ closed }, QStringLiteral("fab"));
        QVERIFY(panel->primaryButton()->isEnabled());
        QVERIFY(panel->noteText().isEmpty());
    }

    void longInstanceNameIsElided()
    {
        Rig rig;
        rig.openWith({ makeTarget(QStringLiteral("fab"), QStringLiteral("A very long instance name that cannot fit a button of this size at all"),
                                  QStringLiteral("1.21.1"), ModPlatform::Fabric) },
                     QStringLiteral("fab"), makeMods(1));
        rig.section->selectRow(0);
        QTRY_COMPARE(rig.modrinth.versionCalls.size(), 1);
        rig.modrinth.answerVersions(0, { makeVersion(QStringLiteral("v1"), QStringLiteral("1.0")) });
        const QString text = rig.section->panel()->primaryButton()->text();
        QVERIFY(text.startsWith(QStringLiteral("Add to ")));
        QVERIFY(text.endsWith(QChar(0x2026)));
        QVERIFY(text.size() < 60);
    }

    void noFittingVersionDisablesTheAction()
    {
        Rig rig;
        rig.openWith({ fabric121() }, QStringLiteral("fab"), makeMods(1));
        rig.section->selectRow(0);
        QTRY_COMPARE(rig.modrinth.versionCalls.size(), 1);
        // the provider (or CurseForge's partial filter) sent a version for another game version and loader
        rig.modrinth.answerVersions(0, { makeVersion(QStringLiteral("x"), QStringLiteral("old"), QStringLiteral("1.19.2"), QStringLiteral("forge")) });
        QCOMPARE(rig.section->panel()->versionRowCount(), 0);
        QVERIFY(!rig.section->panel()->primaryButton()->isEnabled());
        QVERIFY(rig.section->panel()->noteText().contains(QStringLiteral("1.21.1 Fabric")));
    }

    void versionsTabListsOnlyCompatibleVersions()
    {
        Rig rig;
        rig.openWith({ makeTarget(QStringLiteral("q"), QStringLiteral("Quilty"), QStringLiteral("1.21.1"), ModPlatform::Quilt | ModPlatform::Fabric) },
                     QStringLiteral("q"), makeMods(1));
        rig.section->selectRow(0);
        QTRY_COMPARE(rig.modrinth.versionCalls.size(), 1);
        rig.modrinth.answerVersions(0, { makeVersion(QStringLiteral("a"), QStringLiteral("fabric ok")),
                                         makeVersion(QStringLiteral("b"), QStringLiteral("quilt ok"), QStringLiteral("1.21.1"), QStringLiteral("quilt")),
                                         makeVersion(QStringLiteral("c"), QStringLiteral("forge"), QStringLiteral("1.21.1"), QStringLiteral("forge")),
                                         makeVersion(QStringLiteral("d"), QStringLiteral("old mc"), QStringLiteral("1.20.1"), QStringLiteral("fabric")) });
        QCOMPARE(rig.section->panel()->versionRowCount(), 2);
        ModpackVersion noData = makeVersion(QStringLiteral("n"), QStringLiteral("no data"));
        noData.gameVersions.clear();
        noData.loaders.clear();
        QVERIFY(rig.section->versionFits(noData));  // missing data never excludes
    }

    void staleVersionsAnswerAfterATargetChangeIsDropped()
    {
        Rig rig;
        rig.openWith({ fabric121(), forge1201() }, QStringLiteral("fab"), makeMods(1));
        rig.section->selectRow(0);
        QTRY_COMPARE(rig.modrinth.versionCalls.size(), 1);  // asked under 1.21.1 Fabric, still on its way
        QVERIFY(rig.section->chooseTarget(QStringLiteral("forge")));
        rig.modrinth.answerVersions(0, { makeVersion(QStringLiteral("old"), QStringLiteral("1.21.1 build")) });
        QCOMPARE(rig.section->panel()->versionRowCount(), 0);
        QTRY_COMPARE(rig.modrinth.versionCalls.size(), 2);
        rig.modrinth.answerVersions(1, { makeVersion(QStringLiteral("new"), QStringLiteral("1.20.1 build"), QStringLiteral("1.20.1"), QStringLiteral("forge")) });
        QCOMPARE(rig.section->panel()->versionRowCount(), 1);
    }

    // ---- empty state ----

    void emptyStateWithoutModReadyInstance()
    {
        Rig rig;
        rig.open();
        rig.section->setTargets({ vanilla() }, QString());
        QVERIFY(!rig.section->hasTarget());
        QVERIFY(rig.section->noTargetState()->isVisibleTo(rig.section.get()));
        QVERIFY(!rig.section->toolbar()->isVisibleTo(rig.section.get()));
        QVERIFY(rig.modrinth.searches.isEmpty());
        QSignalSpy create(rig.section.get(), &ModsSection::createInstanceRequested);
        emit rig.section->noTargetState()->actionTriggered();
        QCOMPARE(create.size(), 1);
        // an instance with a loader appears (the instance list changed): the section starts
        rig.section->setTargets({ vanilla(), fabric121() }, QStringLiteral("fab"));
        QVERIFY(!rig.section->noTargetState()->isVisibleTo(rig.section.get()));
        QVERIFY(rig.section->toolbar()->isVisibleTo(rig.section.get()));
        QVERIFY(rig.section->hasTarget());
        QCOMPARE(rig.modrinth.searches.size(), 1);
    }

    // ---- "Not for" ----

    void notForTextAfterShowEverything()
    {
        Rig rig;
        rig.openWith({ fabric121() }, QStringLiteral("fab"),
                     { makeMod(QStringLiteral("1"), QStringLiteral("Fits")),
                       makeMod(QStringLiteral("2"), QStringLiteral("Forge only"), { QStringLiteral("1.21.1") }, { QStringLiteral("forge") }),
                       makeMod(QStringLiteral("3"), QStringLiteral("Old"), { QStringLiteral("1.20.1") }, { QStringLiteral("fabric") }),
                       makeMod(QStringLiteral("4"), QStringLiteral("No data"), {}, {}) });
        // with the default chips the provider already filtered: nothing is "not for"
        for (const auto& item : rig.section->model()->loaded()) {
            QCOMPARE(rig.section->notForText(*item), QString());
        }
        rig.section->applyState(showEverything(rig.section->state()));
        QVERIFY(rig.section->chipStrip()->chipLabels().isEmpty());
        const auto items = rig.section->model()->loaded();
        QCOMPARE(rig.section->notForText(*items[0]), QString());
        QCOMPARE(rig.section->notForText(*items[1]), QStringLiteral("Not for 1.21.1 Fabric"));
        QCOMPARE(rig.section->notForText(*items[2]), QStringLiteral("Not for 1.21.1 Fabric"));
        QCOMPARE(rig.section->notForText(*items[3]), QString());  // unknown data never says "not for"
        // The empty state offers "Show everything" because the section has defaults
        QVERIFY(rig.section->noMatchState());
    }

    // ---- painting ----

    void environmentLabels()
    {
        auto item = makeMod(QStringLiteral("1"), QStringLiteral("A"));
        QCOMPARE(environmentLabel(*item), QString());  // the provider sent nothing: no pill
        item->pack->side = ModPlatform::SideType::ClientSide;
        QCOMPARE(environmentLabel(*item), QStringLiteral("Client"));
        item->pack->side = ModPlatform::SideType::ServerSide;
        QCOMPARE(environmentLabel(*item), QStringLiteral("Server"));
        item->pack->side = ModPlatform::SideType::UniversalSide;
        QCOMPARE(environmentLabel(*item), QStringLiteral("Client & server"));
    }

    void footerPillsDropInPriorityOrder()
    {
        auto item = makeMod(QStringLiteral("1"), QStringLiteral("A"), { QStringLiteral("1.21.1") }, { QStringLiteral("fabric"), QStringLiteral("quilt") });
        item->pack->side = ModPlatform::SideType::UniversalSide;
        auto kinds = [](const QList<ModDelegate::Pill>& pills) {
            QList<int> out;
            for (const auto& p : pills) {
                out << int(p.kind);
            }
            return out;
        };
        using K = ModDelegate::Pill::Kind;
        QCOMPARE(kinds(ModDelegate::pillsFor(*item, true, 1000)),
                 (QList<int>{ int(K::Loader), int(K::Loader), int(K::Environment), int(K::Downloads), int(K::Installed) }));
        // narrower: the environment goes first, then the second loader; Installed and the first loader stay longest
        const auto all = ModDelegate::pillsFor(*item, true, 1000);
        int total = -ModDelegate::kPillGap;
        for (const auto& p : all) {
            total += p.width + ModDelegate::kPillGap;
        }
        const auto noEnv = ModDelegate::pillsFor(*item, true, total - 1);
        QVERIFY(!kinds(noEnv).contains(int(K::Environment)));
        const QList<int> noEnvKinds = kinds(noEnv);
        QCOMPARE(std::count(noEnvKinds.begin(), noEnvKinds.end(), int(K::Loader)), 2);
        const auto tiny = ModDelegate::pillsFor(*item, true, 10);
        QCOMPARE(kinds(tiny), QList<int>{ int(K::Installed) });
        QVERIFY(ModDelegate::pillsFor(*makeMod(QStringLiteral("2"), QStringLiteral("B"), {}, {}), false, 1000).size() == 1);  // downloads only
    }

    void gridCardGeometry()
    {
        RemoteImageCache images([](const QString&, std::function<void(const RemoteImageCache::FetchResult&)> done) { done({}); });
        ModDelegate delegate(&images);
        delegate.setViewportWidth(1000);
        QCOMPARE(delegate.itemSize().height(), 132);
        QVERIFY(delegate.itemSize().width() >= 300 && delegate.itemSize().width() <= 420);
        QCOMPARE(delegate.columns(), 3);  // (1000 + 16) / 316
        delegate.setViewportWidth(1400);
        QCOMPARE(delegate.columns(), 4);  // the columns fill the width: 4 cards of 338 px
        QCOMPARE(delegate.itemSize().width(), 338);
        delegate.setViewportWidth(500);
        QCOMPARE(delegate.columns(), 1);
        QCOMPARE(delegate.itemSize().width(), 500);  // a single column (the details panel is open) takes the room, up to 560
        delegate.setViewportWidth(600);
        QCOMPARE(delegate.columns(), 1);
        QCOMPARE(delegate.itemSize().width(), 560);
        delegate.setViewportWidth(900);
        QCOMPARE(delegate.columns(), 2);
        QCOMPARE(delegate.itemSize().width(), 420);
        delegate.setMode(ContentDelegate::Mode::List);
        delegate.setViewportWidth(900);
        QCOMPARE(delegate.itemSize(), QSize(900, 56));
        QCOMPARE(delegate.columns(), 1);
    }

    void cardsPaintInBothModesAndStates()
    {
        Rig rig;
        rig.installed.list = { { QStringLiteral("modrinth:1"), QStringLiteral("f"), QStringLiteral("Mod 1"), QString() } };
        auto misfit = makeMod(QStringLiteral("9"), QStringLiteral("Misfit"), { QStringLiteral("1.12") }, { QStringLiteral("forge") });
        misfit->pack->side = ModPlatform::SideType::ClientSide;
        rig.openWith({ fabric121() }, QStringLiteral("fab"), { makeMods(1).first(), misfit });
        rig.section->applyState(showEverything(rig.section->state()));
        for (const auto mode : { ContentToolbar::ViewMode::Grid, ContentToolbar::ViewMode::List }) {
            emit rig.section->toolbar()->viewModeChanged(mode);
            QApplication::processEvents();
            const QImage shot = rig.section->grab().toImage();
            QVERIFY(!shot.isNull());
        }
        auto* skeleton = rig.section->findChild<Zero::ZeroSkeleton*>();
        QVERIFY(skeleton);
        QCOMPARE(skeleton->shape(), Zero::ZeroSkeleton::Shape::Cards);
        QVERIFY(skeleton->compactCards());  // the mod card shape, not the modpack card
        QVERIFY(!skeleton->grab().toImage().isNull());
    }

    // ---- the page: switcher, remembered section, target rule ----

    void switcherRemembersTheSection()
    {
        Rig rig;
        FakeSource packSource(Provider::Modrinth);
        ModpacksEnvironment packs;
        packs.sources.modrinth = &packSource;
        packs.images = &rig.images;
        QString section = QStringLiteral("mods");
        QString target;
        QStringList written;
        DiscoverHooks hooks;
        hooks.loadSection = [&]() { return section; };
        hooks.saveSection = [&](const QString& id) {
            section = id;
            written << id;
        };
        hooks.loadTarget = [&]() { return target; };
        hooks.saveTarget = [&](const QString& id) { target = id; };
        hooks.readTargets = []() { return QList<TargetInstance>{ vanilla(), fabric121(), forge1201() }; };
        DiscoverPage page(packs, rig.environment(), hooks, nullptr);
        QString library;
        page.setLibrarySelection([&]() { return library; });
        page.resize(1280, 720);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));

        QCOMPARE(page.currentSection(), QStringLiteral("mods"));  // read on start
        QVERIFY(written.isEmpty());                                // reading does not write
        QCOMPARE(DiscoverPage::sectionIds(), (QStringList{ QStringLiteral("modpacks"), QStringLiteral("mods") }));
        QVERIFY(page.topBarActions() == page.switcher());
        QCOMPARE(page.switcher()->selected(), QStringLiteral("mods"));
        QCOMPARE(page.mods()->target().id, QStringLiteral("fab"));  // no Library selection, no saved target: first mod-ready by name
        QCOMPARE(page.subtitle(), QString());
        rig.modrinth.answerSearch(0, makeMods(3));
        QCOMPARE(page.subtitle(), QStringLiteral("3 results"));  // the current section's caption (a short page ends the list)

        QSignalSpy chrome(&page, &ShellPage::chromeChanged);
        page.switcher()->onActivated(QStringLiteral("modpacks"));  // the user clicks Modpacks
        QCOMPARE(page.currentSection(), QStringLiteral("modpacks"));
        QCOMPARE(written, QStringList{ QStringLiteral("modpacks") });
        QCOMPARE(page.switcher()->selected(), QStringLiteral("modpacks"));
        QVERIFY(chrome.size() >= 1);
        QCOMPARE(page.subtitle(), QString());  // the Modpacks caption (nothing loaded yet)
        page.showSection(QStringLiteral("mods"));
        QCOMPARE(page.currentSection(), QStringLiteral("mods"));
        QCOMPARE(written.last(), QStringLiteral("mods"));
        QCOMPARE(page.subtitle(), QStringLiteral("3 results"));

        // a pick is remembered
        emit page.mods()->targetButton()->targetChosen(QStringLiteral("forge"));
        QCOMPARE(target, QStringLiteral("forge"));
        // an instance list change keeps the pick
        page.refreshTargets();
        QCOMPARE(page.mods()->target().id, QStringLiteral("forge"));
        // the Library selection changed since the last look: following it
        library = QStringLiteral("fab");
        page.refreshTargets(true);
        QCOMPARE(page.mods()->target().id, QStringLiteral("fab"));
        // an unchanged Library selection does not undo a later pick
        QVERIFY(page.mods()->chooseTarget(QStringLiteral("forge")));
        page.refreshTargets(true);
        QCOMPARE(page.mods()->target().id, QStringLiteral("forge"));

        // the modpack side does not know the Mods section's "Create instance"
        QSignalSpy create(&page, &DiscoverPage::createInstanceRequested);
        emit page.mods()->createInstanceRequested();
        QCOMPARE(create.size(), 1);
    }

    void pageWithoutModsHasNoSwitcher()
    {
        FakeSource source(Provider::Modrinth);
        RemoteImageCache images([](const QString&, std::function<void(const RemoteImageCache::FetchResult&)> done) { done({}); });
        ModpacksEnvironment packs;
        packs.sources.modrinth = &source;
        packs.images = &images;
        DiscoverPage page(packs, nullptr);
        QVERIFY(page.switcher() == nullptr);
        QVERIFY(page.mods() == nullptr);
        QCOMPARE(page.currentSection(), QStringLiteral("modpacks"));
        page.showSection(QStringLiteral("mods"));  // nothing to show: stays on Modpacks
        QCOMPARE(page.currentSection(), QStringLiteral("modpacks"));
    }

    void targetFromLibrarySelectionAtStart()
    {
        Rig rig;
        FakeSource packSource(Provider::Modrinth);
        ModpacksEnvironment packs;
        packs.sources.modrinth = &packSource;
        packs.images = &rig.images;
        DiscoverHooks hooks;
        hooks.loadSection = []() { return QStringLiteral("mods"); };
        hooks.loadTarget = []() { return QStringLiteral("fab"); };
        hooks.readTargets = []() { return QList<TargetInstance>{ fabric121(), forge1201() }; };
        DiscoverPage page(packs, rig.environment(), hooks, nullptr);
        page.setLibrarySelection([]() { return QStringLiteral("forge"); });  // the Library selection wins over the saved target
        page.resize(1280, 720);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        QCOMPARE(page.mods()->target().id, QStringLiteral("forge"));
    }

    // ---- watching ----

    void installedModsAreWatchedOnlyWhileShown()
    {
        Rig rig;
        rig.openWith({ fabric121() }, QStringLiteral("fab"));
        QVERIFY(rig.installed.active);
        rig.section->hide();
        QVERIFY(!rig.installed.active);
        rig.section->show();
        QVERIFY(rig.installed.active);
        rig.section.reset();
        QVERIFY(!rig.installed.active);
    }
};

QTEST_MAIN(ZeroDiscoverModsTest)

#include "ZeroDiscoverMods_test.moc"
