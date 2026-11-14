// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of Discover > Resource Packs (docs/zero/CONTENT-PACKS.md, "D1 Discover > Resource Packs"): every instance with a
// Minecraft version is a target (no loader), the default chip is the Minecraft version alone and no loader is ever sent, the resolution
// facet is Modrinth-only (CurseForge disabled with its reason), the installed packs (packwiz `resourcepacks/.index`), the resolution
// pills of the gallery card, the primary action states, the add dialog in pack mode (no dependencies) and the three-section switcher.
// Offscreen, with fake sources and hand-built TargetInstances: no network, no instances, no APPLICATION.
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QListView>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <functional>

#include "ui/zero/ZeroEmptyState.h"
#include "ui/zero/ZeroSkeleton.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/content/ContentDetailsPanel.h"
#include "ui/zero/content/ContentToolbar.h"
#include "ui/zero/content/FilterChipStrip.h"
#include "ui/zero/content/FilterPanel.h"
#include "ui/zero/content/ModInstallDialog.h"
#include "ui/zero/content/ModRows.h"
#include "ui/zero/content/ModpackRows.h"
#include "ui/zero/content/ModpacksSection.h"
#include "ui/zero/content/ModsSection.h"
#include "ui/zero/content/PackRows.h"
#include "ui/zero/content/RemoteImageCache.h"
#include "ui/zero/pages/DiscoverPage.h"
#include "zero/content/InstalledMods.h"
#include "zero/content/ModInstall.h"

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

class FakeInstalled : public InstalledModsSource {
   public:
    void setInstance(const QString& instanceId) override { instance = instanceId; }
    void setActive(bool on) override { active = on; }
    QList<InstalledMod> mods() const override { return list; }
    void set(const QList<InstalledMod>& mods)
    {
        list = mods;
        emit changed();
    }

    QString instance;
    bool active = false;
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

TargetInstance fabric121() { return makeTarget(S("fab"), S("Fabric Fun"), S("1.21.1"), ModPlatform::Fabric); }
TargetInstance vanilla121() { return makeTarget(S("van"), S("Vanilla"), S("1.21.1"), ModPlatform::ModLoaderTypes()); }
TargetInstance noVersion() { return makeTarget(S("nov"), S("No version"), QString(), ModPlatform::Fabric); }

ModpackItemPtr makePack(const QString& id, const QString& name, const QStringList& categories = { S("16x") },
                        Provider provider = Provider::Modrinth)
{
    ModPlatform::IndexedPack pack;
    pack.addonId = id;
    pack.provider = provider == Provider::CurseForge ? ModPlatform::ResourceProvider::FLAME : ModPlatform::ResourceProvider::MODRINTH;
    pack.name = name;
    pack.slug = name.toLower();
    pack.description = S("About %1").arg(name);
    pack.authors = { { S("someone"), QString() } };
    pack.logoUrl = S("https://example.test/icon/%1.png").arg(id);
    pack.gameVersions = { S("1.21.1") };
    pack.loaderNames = { S("minecraft") };  // Modrinth resource packs carry the loader "minecraft"
    pack.categories = categories;
    pack.categoryIds = categories;
    pack.downloads = 1234567;
    pack.websiteUrl = S("https://example.test/%1").arg(id);
    return itemFromPack(provider, pack);
}

QList<ModpackItemPtr> makePacks(int count)
{
    QList<ModpackItemPtr> out;
    for (int i = 1; i <= count; ++i) {
        out.append(makePack(QString::number(i), S("Pack %1").arg(i)));
    }
    return out;
}

ModpackVersion makeVersion(const QString& id, const QString& name, const QString& mc = S("1.21.1"))
{
    ModpackVersion v;
    v.id = id;
    v.name = name;
    v.downloadUrl = S("https://cdn.test/") + id;
    v.type = ModpackVersion::Type::Release;
    v.gameVersions = { mc };
    v.loaders = { S("minecraft") };
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
    FakeInstalled installed;
    RemoteImageCache images{ [](const QString&, std::function<void(const RemoteImageCache::FetchResult&)> done) { done({}); } };
    std::unique_ptr<PacksSection> section;

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
        return env;
    }

    void open(bool withCurseForge = true)
    {
        section = std::make_unique<PacksSection>(environment(withCurseForge));
        section->resize(1280, 720);
        section->show();
        QVERIFY2(QTest::qWaitForWindowExposed(section.get()), "section not exposed");
    }

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

ModPlatform::IndexedVersion indexedVersion(const QString& fileId, const QString& name, const QStringList& mc, ModPlatform::ModLoaderTypes loaders)
{
    ModPlatform::IndexedVersion v;
    v.addonId = S("PROJ");
    v.fileId = fileId;
    v.version = name;
    v.versionNumber = name;
    v.versionType = ModPlatform::IndexedVersionType(ModPlatform::IndexedVersionTypeValue::Release);
    v.mcVersion = mc;
    v.loaders = loaders;
    v.downloadUrl = S("https://cdn.test/pack.zip");
    v.fileName = name + S(".zip");
    v.date = S("2024-03-12T10:00:00Z");
    v.fileSize = 2 * 1024 * 1024;
    return v;
}

ModpackItemPtr itemWithVersions(const QList<ModPlatform::IndexedVersion>& versions)
{
    auto item = makePack(S("PROJ"), S("Faithful"));
    item->pack->versions = versions;
    item->pack->versionsLoaded = true;
    return item;
}

void writeIndexEntry(const QDir& index, const QString& slug, const QString& file, const QString& projectId, const QString& versionId)
{
    QFile out(index.absoluteFilePath(slug + S(".pw.toml")));
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(QStringLiteral("name = \"%1\"\nfilename = \"%2\"\nside = \"client\"\n\n[download]\nurl = \"https://cdn.test/%2\"\n"
                             "hash-format = \"sha512\"\nhash = \"abc\"\n\n[update]\n[update.modrinth]\n"
                             "mod-id = \"%3\"\nversion = \"%4\"\n")
                  .arg(slug, file, projectId, versionId)
                  .toUtf8());
}

}  // namespace

class ZeroDiscoverPacksTest : public QObject {
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

    // ---- target ----

    void targetAcceptsVanillaAndRejectsNoVersion()
    {
        QVERIFY(vanilla121().packReady());
        QVERIFY(!vanilla121().modReady());  // mods still need a loader
        QVERIFY(fabric121().packReady());
        QVERIFY(!noVersion().packReady());
        QCOMPARE(vanilla121().summaryFor(false), S("1.21.1"));
        QCOMPARE(fabric121().summaryFor(false), S("1.21.1"));  // no loader in the pack summary
        QCOMPARE(fabric121().summaryFor(true), S("1.21.1 Fabric"));
        // the default target rule with packs: a vanilla instance can be chosen, one without a version never
        QCOMPARE(chooseTarget({ noVersion(), vanilla121() }, QString(), QString(), false), S("van"));
        QCOMPARE(chooseTarget({ noVersion(), vanilla121() }, QString(), QString(), true), QString());  // mods: unchanged
        QCOMPARE(chooseTarget({ noVersion() }, S("nov"), S("nov"), false), QString());

        Rig rig;
        rig.open();
        rig.section->setTargets({ noVersion(), vanilla121() }, S("van"));
        QVERIFY(rig.section->hasTarget());
        QCOMPARE(rig.section->target().id, S("van"));
        QVERIFY(!rig.section->chooseTarget(S("nov")));
        QCOMPARE(rig.section->modReadyTargets().size(), 1);
        QCOMPARE(rig.section->targetButton()->currentId(), S("van"));
        QVERIFY(!rig.section->targetButton()->needsLoader());
        // the menu lists the vanilla instance and counts the one without a version
        QMenu* menu = rig.section->targetButton()->buildMenu(rig.section.get());
        QCOMPARE(menu->actions().size(), 3);  // the instance, the separator, the note
        QVERIFY(menu->actions().last()->text().contains(S("Minecraft version")));
        delete menu;
    }

    void emptyStateWithoutAnyInstance()
    {
        Rig rig;
        rig.open();
        rig.section->setTargets({ noVersion() }, QString());
        QVERIFY(!rig.section->hasTarget());
        QVERIFY(rig.section->noTargetState()->isVisibleTo(rig.section.get()));
        QVERIFY(rig.modrinth.searches.isEmpty());
        QSignalSpy create(rig.section.get(), &ModsSection::createInstanceRequested);
        emit rig.section->noTargetState()->actionTriggered();
        QCOMPARE(create.size(), 1);
        rig.section->setTargets({ noVersion(), vanilla121() }, S("van"));
        QVERIFY(!rig.section->noTargetState()->isVisibleTo(rig.section.get()));
        QCOMPARE(rig.modrinth.searches.size(), 1);
    }

    // ---- defaults and search arguments ----

    void defaultChipIsTheMinecraftVersionOnly()
    {
        Rig rig;
        rig.open();
        QVERIFY(rig.modrinth.searches.isEmpty());
        rig.section->setTargets({ fabric121() }, S("fab"));  // a Fabric instance: the loader is still not a chip
        QCOMPARE(rig.section->state().values(FacetId::mcVersion), QStringList{ S("1.21.1") });
        QVERIFY(rig.section->state().values(FacetId::loader).isEmpty());
        QCOMPARE(rig.section->chipStrip()->chipLabels(), QStringList{ S("Minecraft version: 1.21.1") });
        QCOMPARE(rig.section->defaults().values(FacetId::mcVersion), QStringList{ S("1.21.1") });

        QCOMPARE(rig.modrinth.searches.size(), 1);
        const auto& args = rig.modrinth.searches[0].args;
        QCOMPARE(args.type, ModPlatform::ResourceType::ResourcePack);
        QVERIFY(args.versions.has_value());
        QCOMPARE(int(args.versions->size()), 1);
        QCOMPARE(args.versions->front().toString(), S("1.21.1"));
        QVERIFY(!args.loaders.has_value());  // Modrinth packs carry the loader "minecraft": never a mod loader
        // versions requests carry the Minecraft version and no loader
        QCOMPARE(rig.modrinth.versionFilter().mcVersions, QStringList{ S("1.21.1") });
        QCOMPARE(rig.modrinth.versionFilter().loaders, ModPlatform::ModLoaderTypes());
        QVERIFY(rig.installed.active);
        QCOMPARE(rig.installed.instance, S("fab"));
    }

    void showEverythingRemovesTheChip()
    {
        Rig rig;
        rig.openWith({ vanilla121() }, S("van"));
        rig.section->applyState(showEverything(rig.section->state()));
        QVERIFY(rig.section->chipStrip()->chipLabels().isEmpty());
        QVERIFY(!rig.modrinth.searches.last().args.versions.has_value());
    }

    void changingLoaderOnlyDoesNotSearchAgain()
    {
        Rig rig;
        rig.openWith({ vanilla121() }, S("van"), makePacks(2));
        const int before = int(rig.modrinth.searches.size());
        auto changed = vanilla121();
        changed.loaders = ModPlatform::Fabric;  // the loader does not matter for packs
        changed.loaderText = S("Fabric");
        rig.section->setTargets({ changed }, S("van"));
        QCOMPARE(int(rig.modrinth.searches.size()), before);
        // another Minecraft version does
        changed.mcVersion = S("1.20.1");
        rig.section->setTargets({ changed }, S("van"));
        QCOMPARE(int(rig.modrinth.searches.size()), before + 1);
        QCOMPARE(rig.modrinth.searches.last().args.versions->front().toString(), S("1.20.1"));
    }

    // ---- facets ----

    void facetsFollowTheSpec()
    {
        Rig rig;
        rig.openWith({ vanilla121() }, S("van"));
        QStringList ids;
        for (const auto& f : rig.section->facets()) {
            ids << f.id;
        }
        QCOMPARE(ids, (QStringList{ FacetId::provider, FacetId::mcVersion, FacetId::resolution, FacetId::category, FacetId::feature, FacetId::installed }));
        QVERIFY(facetOf(rig.section->facets(), FacetId::resolution).isEnabled());
        QVERIFY(rig.section->curseForgeAvailable());
        QVERIFY(facetOf(rig.section->facets(), FacetId::provider).isEnabled());  // any loader: CurseForge always can
    }

    void resolutionFacetIsDisabledForCurseForge()
    {
        Rig rig;
        rig.openWith({ fabric121() }, S("fab"));
        rig.section->setProvider(Provider::CurseForge);
        QCOMPARE(rig.section->provider(), Provider::CurseForge);
        QVERIFY(!rig.curseforge.searches.isEmpty());
        const auto& args = rig.curseforge.searches.last().args;
        QCOMPARE(args.type, ModPlatform::ResourceType::ResourcePack);
        QVERIFY(!args.loaders.has_value());
        QCOMPARE(args.versions->front().toString(), S("1.21.1"));
        const auto resolution = facetOf(rig.section->facets(), FacetId::resolution);
        QVERIFY(!resolution.isEnabled());
        QCOMPARE(resolution.disabledReason, S("CurseForge has no resolution data"));
        // a resolution chosen on Modrinth is not sent to CurseForge
        rig.section->setProvider(Provider::Modrinth);
        FilterState s = rig.section->state();
        s.selected.insert(FacetId::resolution, { S("16x") });
        rig.section->applyState(s);
        QVERIFY(rig.modrinth.searches.last().args.categoryGroups.has_value());
    }

    // ---- installed ----

    void installedFromAPackwizIndex()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QDir files(temp.path());
        QVERIFY(files.mkpath(S(".index")));
        QDir index(files.absoluteFilePath(S(".index")));
        writeIndexEntry(index, S("faithful"), S("Faithful 32x.zip"), S("P1"), S("V1"));
        writeIndexEntry(index, S("gone"), S("Gone.zip"), S("P2"), S("V2"));  // the file was deleted outside the launcher
        writeIndexEntry(index, S("hidden"), S("Hidden.zip"), S("P3"), S("V3"));
        QFile(files.absoluteFilePath(S("Faithful 32x.zip"))).open(QIODevice::WriteOnly);
        QFile(files.absoluteFilePath(S("Hidden.zip.disabled"))).open(QIODevice::WriteOnly);

        const QList<InstalledMod> found = installedFromIndex(index, files);
        QCOMPARE(found.size(), 2);
        QStringList keys;
        for (const auto& mod : found) {
            keys << mod.key;
        }
        QVERIFY(keys.contains(S("modrinth:P1")));
        QVERIFY(keys.contains(S("modrinth:P3")));
        QVERIFY(!keys.contains(S("modrinth:P2")));
        for (const auto& mod : found) {
            if (mod.key == S("modrinth:P1")) {
                QCOMPARE(mod.fileId, S("V1"));
                QCOMPARE(mod.name, S("faithful"));
            }
        }
        QVERIFY(installedFromIndex(QDir(files.absoluteFilePath(S("nothing"))), files).isEmpty());
    }

    void installedPillAndFilter()
    {
        Rig rig;
        rig.installed.list = { { S("modrinth:2"), S("f2"), S("Pack 2"), S("2.0") } };
        rig.openWith({ vanilla121() }, S("van"), makePacks(4));
        auto* model = rig.section->model();
        QCOMPARE(model->visibleCount(), 4);
        QVERIFY(model->data(model->index(1), ModpackListModel::InstalledRole).toBool());
        QVERIFY(!model->data(model->index(0), ModpackListModel::InstalledRole).toBool());
        FilterState s = rig.section->state();
        s.selected.insert(FacetId::installed, { kToggleOn });
        rig.section->applyState(s);
        QCOMPARE(model->visibleCount(), 1);
        QCOMPARE(model->item(0)->id, S("2"));
        // the folder finished loading later: no new search
        s.selected.remove(FacetId::installed);
        rig.section->applyState(s);
        const int searches = int(rig.modrinth.searches.size());
        rig.installed.set({ { S("modrinth:3"), S("f3"), S("Pack 3"), QString() } });
        QVERIFY(model->data(model->index(2), ModpackListModel::InstalledRole).toBool());
        QCOMPARE(int(rig.modrinth.searches.size()), searches);
    }

    // ---- the gallery card ----

    void resolutionPills()
    {
        QCOMPARE(PackDelegate::resolutionPills(*makePack(S("1"), S("A"), { S("16x") })), QStringList{ S("16x") });
        QCOMPARE(PackDelegate::resolutionPills(*makePack(S("1"), S("A"), { S("32x"), S("16x") })), (QStringList{ S("16x"), S("32x") }));
        QCOMPARE(PackDelegate::resolutionPills(*makePack(S("1"), S("A"), { S("64x"), S("16x"), S("32x") })),
                 (QStringList{ S("16x"), S("32x"), S("+1") }));
        QCOMPARE(PackDelegate::resolutionPills(*makePack(S("1"), S("A"), { S("16x"), S("32x"), S("64x"), S("128x") })),
                 (QStringList{ S("16x"), S("32x"), S("+2") }));
        QVERIFY(PackDelegate::resolutionPills(*makePack(S("1"), S("A"), { S("fantasy"), S("combat") })).isEmpty());  // other categories are no pills
        QVERIFY(PackDelegate::resolutionPills(*makePack(S("1"), S("A"), {}, Provider::CurseForge)).isEmpty());  // CurseForge has no resolution data
    }

    void galleryGeometryAndPainting()
    {
        RemoteImageCache images([](const QString&, std::function<void(const RemoteImageCache::FetchResult&)> done) { done({}); });
        PackDelegate delegate(&images);
        delegate.setViewportWidth(1000);
        QVERIFY(delegate.itemSize().width() >= PackDelegate::kCardMin && delegate.itemSize().width() <= PackDelegate::kCardMax);
        QCOMPARE(delegate.itemSize().height(), delegate.itemSize().width() * 9 / 16 + PackDelegate::kBodyHeight);
        QCOMPARE(delegate.columns(), 3);  // (1000 + 16) / 276
        delegate.setViewportWidth(1400);
        QCOMPARE(delegate.columns(), 5);  // the columns fill the width
        delegate.setViewportWidth(500);
        QCOMPARE(delegate.columns(), 1);
        QCOMPARE(delegate.itemSize().width(), 440);  // a single column takes the room, up to 440
        delegate.setMode(ContentDelegate::Mode::List);  // grid only: the request is ignored
        QCOMPARE(delegate.mode(), ContentDelegate::Mode::Grid);

        Rig rig;
        rig.installed.list = { { S("modrinth:1"), S("f"), S("Pack 1"), QString() } };
        rig.openWith({ vanilla121() }, S("van"), { makePacks(1).first(), makePack(S("9"), S("Many"), { S("16x"), S("32x"), S("64x") }) });
        QApplication::processEvents();
        QVERIFY(!rig.section->grab().toImage().isNull());
        // grid only: no view switch, the Gallery skeleton, the galleries' reveal numbers
        auto* skeleton = rig.section->findChild<Zero::ZeroSkeleton*>();
        QVERIFY(skeleton);
        QCOMPARE(skeleton->shape(), Zero::ZeroSkeleton::Shape::Gallery);
        QVERIFY(!skeleton->grab().toImage().isNull());
        QVERIFY(rig.section->view()->findChild<PackDelegate*>());  // the section's delegate (the reveal may wrap the view's own)
        QVERIFY(rig.section->delegate() == nullptr);  // no mod delegate in this section
        QCOMPARE(rig.section->reveal()->spec().offsetPx, 20);  // the galleries' numbers
        QCOMPARE(rig.section->reveal()->spec().durationMs, 300);
        QCOMPARE(rig.section->reveal()->spec().staggerMs, 28);
    }

    // ---- primary action ----

    void primaryActionStates()
    {
        Rig rig;
        rig.installed.list = { { S("modrinth:1"), S("v2"), S("Pack 1"), S("2.0") }, { S("modrinth:2"), S("v0"), S("Pack 2"), S("0.5") } };
        rig.openWith({ vanilla121() }, S("van"), makePacks(3));
        auto* panel = rig.section->panel();
        const QList<ModpackVersion> versions = { makeVersion(S("v2"), S("2.0")), makeVersion(S("v1"), S("1.0")) };
        auto open = [&](int row, int call) {
            rig.section->selectRow(row);
            QTRY_COMPARE(rig.modrinth.versionCalls.size(), call + 1);
            QVERIFY(!panel->primaryButton()->isEnabled());
            rig.modrinth.answerVersions(call, versions);
        };
        open(0, 0);
        QCOMPARE(panel->primaryButton()->text(), S("Installed"));
        QVERIFY(!panel->primaryButton()->isEnabled());
        QVERIFY(panel->noteText().contains(S("newest version of this pack")));
        open(1, 1);
        QCOMPARE(panel->primaryButton()->text(), S("Update"));
        QVERIFY(panel->primaryButton()->isEnabled());
        QVERIFY(panel->noteText().contains(S("0.5")));
        QSignalSpy requests(rig.section.get(), &ModsSection::installRequested);
        panel->primaryButton()->click();
        QCOMPARE(requests.size(), 1);
        QCOMPARE(requests.first().at(0).value<ContentItemPtr>()->id, S("2"));
        QCOMPARE(requests.first().at(1).toString(), S("van"));
        QCOMPARE(requests.first().at(2).toString(), S("v0"));
        open(2, 2);
        QCOMPARE(panel->primaryButton()->text(), S("Add to Vanilla"));
        QVERIFY(panel->primaryButton()->isEnabled());
        panel->primaryButton()->click();
        QCOMPARE(requests.size(), 2);
        QCOMPARE(requests.last().at(2).toString(), QString());
    }

    void runningTargetNote()
    {
        Rig rig;
        rig.openWith({ makeTarget(S("van"), S("Vanilla"), S("1.21.1"), ModPlatform::ModLoaderTypes(), true) }, S("van"), makePacks(1));
        rig.section->selectRow(0);
        QTRY_COMPARE(rig.modrinth.versionCalls.size(), 1);
        rig.modrinth.answerVersions(0, { makeVersion(S("v1"), S("1.0")) });
        auto* panel = rig.section->panel();
        QVERIFY(!panel->primaryButton()->isEnabled());
        QCOMPARE(panel->noteText(), S("Close Vanilla to add packs"));
        rig.section->setTargets({ vanilla121() }, S("van"));
        QVERIFY(panel->primaryButton()->isEnabled());
        QVERIFY(panel->noteText().isEmpty());
    }

    void versionsTabAndFitIgnoreTheLoader()
    {
        Rig rig;
        rig.openWith({ fabric121() }, S("fab"), makePacks(1));
        rig.section->selectRow(0);
        QTRY_COMPARE(rig.modrinth.versionCalls.size(), 1);
        // a Modrinth pack version has the loader "minecraft" (or any tag): it fits a Fabric instance by its Minecraft version alone
        ModpackVersion other = makeVersion(S("a"), S("A"));
        other.loaders = { S("forge") };
        rig.modrinth.answerVersions(0, { other, makeVersion(S("b"), S("B"), S("1.20.1")) });
        QCOMPARE(rig.section->panel()->versionRowCount(), 1);  // the 1.20.1 one does not fit
        QVERIFY(rig.section->versionFits(other));
        QVERIFY(!rig.section->versionFits(makeVersion(S("b"), S("B"), S("1.20.1"))));
        QCOMPARE(rig.section->panel()->primaryButton()->text(), S("Add to Fabric Fun"));
    }

    void noFittingVersionDisablesTheAction()
    {
        Rig rig;
        rig.openWith({ vanilla121() }, S("van"), makePacks(1));
        rig.section->selectRow(0);
        QTRY_COMPARE(rig.modrinth.versionCalls.size(), 1);
        rig.modrinth.answerVersions(0, { makeVersion(S("old"), S("Old"), S("1.8.9")) });
        QVERIFY(!rig.section->panel()->primaryButton()->isEnabled());
        QCOMPARE(rig.section->panel()->noteText(), S("No version of this pack fits 1.21.1."));
    }

    // ---- the add dialog in pack mode ----

    void dialogHasNoDependencies()
    {
        RemoteImageCache images([](const QString&, std::function<void(const RemoteImageCache::FetchResult&)> done) { done({}); });
        ModInstallEnvironment env;
        env.images = &images;
        env.packs = true;
        env.instanceIcon = [](const QString&) { return QIcon(); };
        // a Forge-tagged and a "minecraft" version: both fit a Fabric instance, loaders do not matter for packs
        const auto versions = QList<ModPlatform::IndexedVersion>{ indexedVersion(S("f2"), S("2.0"), { S("1.21.1") }, ModPlatform::Forge),
                                                                  indexedVersion(S("f1"), S("1.0"), { S("1.21.1") }, ModPlatform::ModLoaderTypes()),
                                                                  indexedVersion(S("f0"), S("0.9"), { S("1.20.1") }, ModPlatform::ModLoaderTypes()) };
        ModInstallDialog dialog(env, itemWithVersions(versions), fabric121(), QString());
        QCOMPARE(dialog.versionBox()->count(), 2);  // the 1.20.1 one does not fit
        QVERIFY(dialog.selectedVersion().has_value());
        QCOMPARE(dialog.selectedVersion()->fileId.toString(), S("f2"));  // the newest release
        QCOMPARE(dialog.primaryButton()->text(), S("Add pack"));
        QVERIFY(dialog.primaryButton()->isEnabled());
        QVERIFY(dialog.canInstall());
        QCOMPARE(dialog.targetLine(), S("Adding to Fabric Fun · 1.21.1"));
        // no Dependencies block, no check
        QVERIFY(dialog.dependencyStatus().isEmpty());
        QVERIFY(dialog.dependencyNames().isEmpty());
        QVERIFY(!dialog.isChecking());
        QVERIFY(!dialog.skipDependenciesBox()->isVisibleTo(&dialog));
        // the plan is the pack alone
        const ModInstallPlan plan = dialog.plan();
        QCOMPARE(plan.count(), 1);
        QCOMPARE(plan.dependencyCount(), 0);
        QCOMPARE(plan.mods.first().reason, reasonStandalone());
        QCOMPARE(plan.mods.first().version.fileId.toString(), S("f2"));
        QVERIFY(plan.mods.first().dependentOn.isEmpty());
    }

    void dialogUpdateAndRunning()
    {
        RemoteImageCache images([](const QString&, std::function<void(const RemoteImageCache::FetchResult&)> done) { done({}); });
        ModInstallEnvironment env;
        env.images = &images;
        env.packs = true;
        env.instanceIcon = [](const QString&) { return QIcon(); };
        bool running = false;
        env.isRunning = [&running](const QString&) { return running; };
        const auto versions = QList<ModPlatform::IndexedVersion>{ indexedVersion(S("f2"), S("2.0"), { S("1.21.1") }, ModPlatform::ModLoaderTypes()) };
        ModInstallDialog update(env, itemWithVersions(versions), vanilla121(), S("f1"));
        QVERIFY(update.isUpdate());
        QCOMPARE(update.primaryButton()->text(), S("Update"));
        QCOMPARE(update.plan().mods.first().reason, reasonUpdate());
        QCOMPARE(update.plan().count(), 1);
        running = true;
        ModInstallDialog busy(env, itemWithVersions(versions), vanilla121(), QString());
        QVERIFY(!busy.canInstall());
        QCOMPARE(busy.noteText(), S("Close Vanilla to add packs"));
        QCOMPARE(packPrimaryActionText(false), S("Add pack"));
        QCOMPARE(buildPackInstallPlan(itemWithVersions(versions)->pack, versions.first(), false).count(), 1);
    }

    // ---- the page ----

    void switcherHasThreeSegmentsAndRemembersPacks()
    {
        Rig rig;
        FakeSource modpackSource(Provider::Modrinth);
        FakeSource modsSource(Provider::Modrinth);
        FakeInstalled modsInstalled;
        ModpacksEnvironment modpacks;
        modpacks.sources.modrinth = &modpackSource;
        modpacks.images = &rig.images;
        ModsEnvironment mods;
        mods.sources.modrinth = &modsSource;
        mods.images = &rig.images;
        mods.installedMods = &modsInstalled;
        QString section = S("resourcepacks");
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
        hooks.readTargets = []() { return QList<TargetInstance>{ noVersion(), vanilla121(), fabric121() }; };
        DiscoverPage page(modpacks, mods, rig.environment(), hooks, nullptr);
        page.setLibrarySelection([]() { return QString(); });
        page.resize(1280, 720);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));

        QCOMPARE(page.sections(), (QStringList{ S("modpacks"), S("mods"), S("resourcepacks") }));
        QCOMPARE(page.switcher()->count(), 3);
        QCOMPARE(page.switcher()->valueAt(2), S("resourcepacks"));
        QCOMPARE(page.currentSection(), S("resourcepacks"));  // read on start
        QVERIFY(written.isEmpty());
        QCOMPARE(page.switcher()->selected(), S("resourcepacks"));
        QVERIFY(page.current() == page.packs());
        // no saved target, no Library selection: the first ready instance by name (both sections)
        QCOMPARE(page.packs()->target().id, S("fab"));
        QCOMPARE(page.mods()->target().id, S("fab"));
        QVERIFY(!rig.modrinth.searches.isEmpty());
        QCOMPARE(rig.modrinth.searches.first().args.type, ModPlatform::ResourceType::ResourcePack);

        page.switcher()->onActivated(S("mods"));
        QCOMPARE(written, QStringList{ S("mods") });
        page.switcher()->onActivated(S("resourcepacks"));
        QCOMPARE(page.currentSection(), S("resourcepacks"));
        QCOMPARE(written.last(), S("resourcepacks"));  // remembered
        page.showSection(S("modpacks"));
        QCOMPARE(page.currentSection(), S("modpacks"));
        page.showSection(S("resourcepacks"));
        QCOMPARE(page.currentSection(), S("resourcepacks"));

        // showPacksFor: the target is chosen and remembered; a query becomes the search text
        QVERIFY(page.showPacksFor(S("van"), S("faithful")));
        QCOMPARE(page.packs()->target().id, S("van"));
        QCOMPARE(target, S("van"));
        QCOMPARE(page.packs()->state().query, S("faithful"));
        QVERIFY(!page.showPacksFor(S("nov")));  // no Minecraft version: not a target
        QCOMPARE(page.packs()->target().id, S("van"));

        // signals
        QSignalSpy install(&page, &DiscoverPage::packInstallRequested);
        QSignalSpy createVanilla(&page, &DiscoverPage::createVanillaInstanceRequested);
        QSignalSpy createModded(&page, &DiscoverPage::createInstanceRequested);
        emit page.packs()->createInstanceRequested();
        QCOMPARE(createVanilla.size(), 1);
        QCOMPARE(createModded.size(), 0);
        emit page.packs()->installRequested(makePack(S("1"), S("A")), S("van"), QString());
        QCOMPARE(install.size(), 1);
    }

    void pageWithoutPacksKeepsTwoSections()
    {
        Rig rig;
        FakeSource modpackSource(Provider::Modrinth);
        ModpacksEnvironment modpacks;
        modpacks.sources.modrinth = &modpackSource;
        modpacks.images = &rig.images;
        DiscoverHooks hooks;
        hooks.loadSection = []() { return S("resourcepacks"); };  // not built: starts on Modpacks
        hooks.readTargets = []() { return QList<TargetInstance>{ vanilla121() }; };
        DiscoverPage page(modpacks, rig.environment(), hooks, nullptr);
        QCOMPARE(page.switcher()->count(), 2);
        QCOMPARE(page.sections(), (QStringList{ S("modpacks"), S("mods") }));
        QVERIFY(page.packs() == nullptr);
        QCOMPARE(page.currentSection(), S("modpacks"));
        QVERIFY(!page.showPacksFor(S("van")));
    }
};

QTEST_MAIN(ZeroDiscoverPacksTest)

#include "ZeroDiscoverPacks_test.moc"
