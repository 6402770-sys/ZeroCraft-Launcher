// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of Discover > Shaders (docs/zero/SHELL.md, "Discover > Shaders"): the section is the packs section over the shader
// kind (any instance with a Minecraft version is a target, no loader is sent), the Platform / Performance facets and what they send per
// provider (CurseForge: disabled with a reason), the default chips (the Minecraft version and the platform of the target's shader mod),
// the shader mod notice (pure decisions and the strip), the card pills, the add dialog in shader mode, the installed shaders (packwiz
// metadata in the shaderpacks folder itself) and the four-section switcher.
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
#include "ui/zero/content/ContentToolbar.h"
#include "ui/zero/content/FilterChipStrip.h"
#include "ui/zero/content/FilterPanel.h"
#include "ui/zero/content/ModInstallDialog.h"
#include "ui/zero/content/ModpackRows.h"
#include "ui/zero/content/ModpacksSection.h"
#include "ui/zero/content/ModsSection.h"
#include "ui/zero/content/PackRows.h"
#include "ui/zero/content/RemoteImageCache.h"
#include "ui/zero/content/ShaderModNotice.h"
#include "ui/zero/pages/DiscoverPage.h"
#include "zero/content/InstalledMods.h"
#include "zero/content/ModInstall.h"
#include "zero/content/ShaderMod.h"

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
TargetInstance forge120() { return makeTarget(S("frg"), S("Forge Land"), S("1.20.1"), ModPlatform::Forge); }
TargetInstance vanilla121() { return makeTarget(S("van"), S("Vanilla"), S("1.21.1"), ModPlatform::ModLoaderTypes()); }
TargetInstance noVersion() { return makeTarget(S("nov"), S("No version"), QString(), ModPlatform::Fabric); }

ModpackItemPtr makeShader(const QString& id, const QString& name, const QStringList& categories = { S("low") },
                          const QStringList& platforms = { S("iris") }, Provider provider = Provider::Modrinth)
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
    pack.loaderNames = platforms;  // a Modrinth shader's loaders are its platforms
    pack.categories = categories;
    pack.categoryIds = categories;
    pack.downloads = 1234567;
    pack.websiteUrl = S("https://example.test/%1").arg(id);
    return itemFromPack(provider, pack);
}

QList<ModpackItemPtr> makeShaders(int count)
{
    QList<ModpackItemPtr> out;
    for (int i = 1; i <= count; ++i) {
        out.append(makeShader(QString::number(i), S("Shader %1").arg(i)));
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
    v.loaders = { S("iris") };
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
    std::unique_ptr<ShadersSection> section;
    QStringList modFiles;  ///< what the target's mods folder holds (the section's shader mod check reads this)
    QStringList savedFilters;

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
        env.modFiles = [this](const TargetInstance&) { return modFiles; };
        env.saveFilter = [this](const QString& json) { savedFilters << json; };
        return env;
    }

    void open(bool withCurseForge = true)
    {
        section = std::make_unique<ShadersSection>(environment(withCurseForge));
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

ModPlatform::IndexedVersion indexedVersion(const QString& fileId, const QString& name, const QStringList& mc)
{
    ModPlatform::IndexedVersion v;
    v.addonId = S("PROJ");
    v.fileId = fileId;
    v.version = name;
    v.versionNumber = name;
    v.versionType = ModPlatform::IndexedVersionType(ModPlatform::IndexedVersionTypeValue::Release);
    v.mcVersion = mc;
    v.loaders = ModPlatform::ModLoaderTypes();
    v.downloadUrl = S("https://cdn.test/shader.zip");
    v.fileName = name + S(".zip");
    v.date = S("2024-03-12T10:00:00Z");
    v.fileSize = 2 * 1024 * 1024;
    return v;
}

ModpackItemPtr shaderWithVersions(const QList<ModPlatform::IndexedVersion>& versions)
{
    auto item = makeShader(S("PROJ"), S("Complementary"));
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

class ZeroDiscoverShadersTest : public QObject {
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

    // ---- section kind and target ----

    void sectionKindAndTargetRule()
    {
        Rig rig;
        rig.open();
        QVERIFY(rig.section->isShaders());
        QVERIFY(rig.section->isFolderKind());
        QVERIFY(!rig.section->needsLoader());
        QVERIFY(rig.section->shaderNotice() != nullptr);
        QVERIFY(rig.section->view()->findChild<PackDelegate*>());  // gallery cards, like the packs
        QVERIFY(rig.section->delegate() == nullptr);

        // any instance with a Minecraft version (no loader) is a target, one without a version never
        rig.section->setTargets({ noVersion(), vanilla121() }, S("van"));
        QVERIFY(rig.section->hasTarget());
        QCOMPARE(rig.section->target().id, S("van"));
        QVERIFY(!rig.section->chooseTarget(S("nov")));
        QCOMPARE(rig.section->modReadyTargets().size(), 1);
        QVERIFY(!rig.section->targetButton()->needsLoader());
        QCOMPARE(rig.section->target().summaryFor(rig.section->needsLoader()), S("1.21.1"));

        // a Mods section of the same environment still needs a loader
        ModsEnvironment modsEnv = rig.environment();
        modsEnv.kind = Kind::Mod;
        ModsSection mods(modsEnv);
        QVERIFY(!mods.isFolderKind());
        QVERIFY(mods.needsLoader());
        QVERIFY(mods.shaderNotice() == nullptr);
    }

    void emptyStateWithoutAnyInstance()
    {
        Rig rig;
        rig.open();
        rig.section->setTargets({ noVersion() }, QString());
        QVERIFY(!rig.section->hasTarget());
        QVERIFY(rig.section->noTargetState()->isVisibleTo(rig.section.get()));
        QVERIFY(!rig.section->shaderNotice()->isVisibleTo(rig.section.get()));
        QVERIFY(rig.modrinth.searches.isEmpty());
        QSignalSpy create(rig.section.get(), &ModsSection::createInstanceRequested);
        emit rig.section->noTargetState()->actionTriggered();
        QCOMPARE(create.size(), 1);
    }

    // ---- search arguments ----

    void modrinthSearchSendsTypeVersionAndNoLoader()
    {
        Rig rig;
        rig.openWith({ fabric121() }, S("fab"));
        QCOMPARE(rig.modrinth.searches.size(), 1);
        const auto& args = rig.modrinth.searches[0].args;
        QCOMPARE(args.type, ModPlatform::ResourceType::ShaderPack);
        QCOMPARE(int(args.versions->size()), 1);
        QCOMPARE(args.versions->front().toString(), S("1.21.1"));
        QVERIFY(!args.loaders.has_value());  // a shader's loaders are its platforms: never a mod loader
        QCOMPARE(rig.modrinth.versionFilter().mcVersions, QStringList{ S("1.21.1") });
        QCOMPARE(rig.modrinth.versionFilter().loaders, ModPlatform::ModLoaderTypes());
        QVERIFY(rig.installed.active);
        QCOMPARE(rig.installed.instance, S("fab"));
    }

    void platformAndPerformanceGoAsModrinthCategoryGroups()
    {
        Rig rig;
        rig.openWith({ vanilla121() }, S("van"));
        FilterState s = rig.section->state();
        s.selected.insert(FacetId::platform, { S("iris"), S("optifine") });
        s.selected.insert(FacetId::performance, { S("potato"), S("low") });
        rig.section->applyState(s);
        const auto& args = rig.modrinth.searches.last().args;
        QCOMPARE(args.type, ModPlatform::ResourceType::ShaderPack);
        QVERIFY(args.categoryGroups.has_value());
        QCOMPARE(args.categoryGroups.value(), (QList<QStringList>{ { S("iris"), S("optifine") }, { S("potato"), S("low") } }));
        QVERIFY(!args.loaders.has_value());
    }

    void facetsFollowTheSpec()
    {
        Rig rig;
        rig.openWith({ vanilla121() }, S("van"));
        QStringList ids;
        for (const auto& f : rig.section->facets()) {
            ids << f.id;
        }
        // The provider choice is offered because CurseForge is in the build; the rest is the filter core's shader set.
        QCOMPARE(ids, (QStringList{ FacetId::provider, FacetId::mcVersion, FacetId::platform, FacetId::performance, FacetId::feature,
                                    FacetId::category, FacetId::installed }));
        QVERIFY(facetOf(rig.section->facets(), FacetId::platform).isEnabled());
        QVERIFY(facetOf(rig.section->facets(), FacetId::performance).isEnabled());
        QVERIFY(rig.section->curseForgeAvailable());  // any loader: CurseForge always can

        Rig modrinthOnly;
        modrinthOnly.open(false);
        modrinthOnly.section->setTargets({ vanilla121() }, S("van"));
        QVERIFY(facetOf(modrinthOnly.section->facets(), FacetId::provider).id.isEmpty());  // nothing to choose
        QVERIFY(!modrinthOnly.section->curseForgeAvailable());
    }

    void curseForgeSendsVersionAndCategoriesAndDisablesPlatformAndPerformance()
    {
        Rig rig;
        rig.openWith({ fabric121() }, S("fab"));
        FilterState s = rig.section->state();
        s.selected.insert(FacetId::platform, { S("iris") });  // chosen on Modrinth
        rig.section->applyState(s);
        rig.section->setProvider(Provider::CurseForge);
        QCOMPARE(rig.section->provider(), Provider::CurseForge);
        QVERIFY(!rig.curseforge.searches.isEmpty());
        const auto& args = rig.curseforge.searches.last().args;
        QCOMPARE(args.type, ModPlatform::ResourceType::ShaderPack);
        QVERIFY(!args.loaders.has_value());
        QCOMPARE(int(args.versions->size()), 1);
        QCOMPARE(args.versions->front().toString(), S("1.21.1"));
        QVERIFY(!args.categoryGroups.has_value());  // the platform is never sent to CurseForge
        const auto platform = facetOf(rig.section->facets(), FacetId::platform);
        QVERIFY(!platform.isEnabled());
        QCOMPARE(platform.disabledReason, S("CurseForge has no shader platform data"));
        const auto performance = facetOf(rig.section->facets(), FacetId::performance);
        QVERIFY(!performance.isEnabled());
        QVERIFY(!performance.disabledReason.isEmpty());
        QVERIFY(facetOf(rig.section->facets(), FacetId::mcVersion).isEnabled());
        QVERIFY(facetOf(rig.section->facets(), FacetId::category).isEnabled());
    }

    // ---- default chips ----

    void defaultChipIsTheMinecraftVersionWithoutAShaderMod()
    {
        Rig rig;
        rig.open();
        rig.section->setTargets({ fabric121() }, S("fab"));
        QCOMPARE(rig.section->state().values(FacetId::mcVersion), QStringList{ S("1.21.1") });
        QVERIFY(rig.section->state().values(FacetId::platform).isEmpty());
        QCOMPARE(rig.section->chipStrip()->chipLabels(), QStringList{ S("Minecraft version: 1.21.1") });
    }

    void platformChipFollowsTheTargetsShaderMod()
    {
        Rig rig;
        rig.modFiles = { S("sodium-fabric-0.6.jar"), S("Iris-1.8.0.jar") };
        rig.open();
        rig.section->setTargets({ fabric121() }, S("fab"));
        QCOMPARE(rig.section->state().values(FacetId::platform), QStringList{ S("iris") });
        QCOMPARE(rig.section->defaults().values(FacetId::platform), QStringList{ S("iris") });
        QCOMPARE(rig.section->chipStrip()->chipLabels(), (QStringList{ S("Minecraft version: 1.21.1"), S("Platform: Iris") }));
        QCOMPARE(rig.modrinth.searches.last().args.categoryGroups.value(), (QList<QStringList>{ { S("iris") } }));

        // "Show everything" removes both defaults
        rig.section->applyState(showEverything(rig.section->state()));
        QVERIFY(rig.section->chipStrip()->chipLabels().isEmpty());
        QVERIFY(!rig.modrinth.searches.last().args.versions.has_value());
        QVERIFY(!rig.modrinth.searches.last().args.categoryGroups.has_value());

        // OptiFine; Oculus counts as Iris
        Rig optifine;
        optifine.modFiles = { S("OptiFine_1.20.1_HD_U_I6.jar") };
        optifine.open();
        optifine.section->setTargets({ forge120() }, S("frg"));
        QCOMPARE(optifine.section->state().values(FacetId::platform), QStringList{ S("optifine") });
        Rig oculus;
        oculus.modFiles = { S("oculus-mc1.20.1-1.6.9.jar") };
        oculus.open();
        oculus.section->setTargets({ forge120() }, S("frg"));
        QCOMPARE(oculus.section->state().values(FacetId::platform), QStringList{ S("iris") });
    }

    void aDisabledShaderModGivesNoPlatformChip()
    {
        Rig rig;
        rig.modFiles = { S("iris-1.8.0.jar.disabled") };
        rig.open();
        rig.section->setTargets({ fabric121() }, S("fab"));
        QVERIFY(rig.section->state().values(FacetId::platform).isEmpty());
        QVERIFY(rig.section->shaderNotice()->isVisibleTo(rig.section.get()));
    }

    void showingTheSectionReadsTheModsFolderAgain()
    {
        Rig rig;
        rig.open();
        rig.section->setTargets({ fabric121() }, S("fab"));
        QVERIFY(rig.section->shaderNotice()->isVisibleTo(rig.section.get()));
        const int searches = int(rig.modrinth.searches.size());
        rig.modFiles = { S("iris-1.8.0.jar") };  // the user added Iris meanwhile
        rig.section->refreshShaderMod();
        QVERIFY(!rig.section->shaderNotice()->isVisibleTo(rig.section.get()));
        QVERIFY(rig.section->shaderMod().hasShaderMod);
        QCOMPARE(rig.section->state().values(FacetId::platform), QStringList{ S("iris") });
        QCOMPARE(int(rig.modrinth.searches.size()), searches + 1);  // the platform is a server facet
        // nothing changed: nothing happens
        rig.section->refreshShaderMod();
        QCOMPARE(int(rig.modrinth.searches.size()), searches + 1);
        // hide and show again re-evaluates
        rig.modFiles = {};
        rig.section->hide();
        rig.section->show();
        QVERIFY(rig.section->shaderNotice()->isVisibleTo(rig.section.get()));
        QVERIFY(rig.section->state().values(FacetId::platform).isEmpty());
    }

    void targetChangeReevaluatesTheNotice()
    {
        Rig rig;
        rig.open();
        rig.section->setTargets({ fabric121(), forge120(), vanilla121() }, S("fab"));
        QCOMPARE(rig.section->shaderNotice()->label()->text(), S("Fabric Fun has no shader mod. Shaders need Iris."));
        QVERIFY(rig.section->chooseTarget(S("frg")));
        QCOMPARE(rig.section->shaderNotice()->label()->text(), S("Forge Land has no shader mod. Shaders need Oculus."));
        QCOMPARE(rig.section->shaderNotice()->button()->text(), S("Find Oculus"));
        QVERIFY(rig.section->chooseTarget(S("van")));
        QCOMPARE(rig.section->shaderNotice()->label()->text(), S("Shaders need Iris or OptiFine, which need a modded instance."));
        QVERIFY(!rig.section->shaderNotice()->button()->isVisibleTo(rig.section->shaderNotice()));
    }

    // ---- the shader mod notice ----

    void shaderModFactsReadFileNames()
    {
        QVERIFY(!shaderModFacts({}).hasShaderMod);
        QVERIFY(!shaderModFacts({ S("sodium-0.6.jar"), S("lithium.jar") }).hasShaderMod);
        QCOMPARE(shaderModFacts({ S("iris-1.8.jar") }).platforms, QStringList{ S("iris") });
        QCOMPARE(shaderModFacts({ S("IRIS-fabric.JAR") }).platforms, QStringList{ S("iris") });  // case-insensitive
        QCOMPARE(shaderModFacts({ S("Oculus-1.6.jar") }).platforms, QStringList{ S("iris") });
        QCOMPARE(shaderModFacts({ S("OptiFine_1.20.1.jar") }).platforms, QStringList{ S("optifine") });
        QCOMPARE(shaderModFacts({ S("optifine.jar"), S("iris.jar") }).platforms, (QStringList{ S("iris"), S("optifine") }));
        // a disabled file does not count
        QVERIFY(!shaderModFacts({ S("iris-1.8.jar.disabled") }).hasShaderMod);
        QVERIFY(shaderModFacts({ S("iris-1.8.jar.disabled"), S("optifine.jar") }).hasShaderMod);
        QVERIFY(!shaderModFacts({ S("OptiFine.jar.DISABLED") }).hasShaderMod);
    }

    void modFileNamesReadsTheFolder()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QDir dir(temp.path());
        QVERIFY(dir.mkpath(S("sub")));
        QFile(dir.absoluteFilePath(S("iris.jar"))).open(QIODevice::WriteOnly);
        QFile(dir.absoluteFilePath(S("sodium.jar.disabled"))).open(QIODevice::WriteOnly);
        QCOMPARE(modFileNamesIn(temp.path()), (QStringList{ S("iris.jar"), S("sodium.jar.disabled") }));  // files only
        QVERIFY(modFileNamesIn(dir.absoluteFilePath(S("missing"))).isEmpty());
        QVERIFY(modFileNamesIn(QString()).isEmpty());
    }

    void noticeDecisions()
    {
        const ShaderModFacts none;
        ShaderModFacts has;
        has.hasShaderMod = true;
        has.platforms = { S("iris") };

        for (const auto loader : { ModPlatform::Fabric, ModPlatform::Quilt, ModPlatform::NeoForge }) {
            const ShaderNotice n = shaderNoticeFor(S("Fun"), ModPlatform::ModLoaderTypes(loader), none);
            QVERIFY(n.show);
            QCOMPARE(n.text, S("Fun has no shader mod. Shaders need Iris."));
            QCOMPARE(n.buttonText, S("Find Iris"));
            QCOMPARE(n.searchQuery, S("Iris"));
        }
        const ShaderNotice forge = shaderNoticeFor(S("Old"), ModPlatform::ModLoaderTypes(ModPlatform::Forge), none);
        QVERIFY(forge.show);
        QCOMPARE(forge.text, S("Old has no shader mod. Shaders need Oculus."));
        QCOMPARE(forge.buttonText, S("Find Oculus"));
        QCOMPARE(forge.searchQuery, S("Oculus"));
        const ShaderNotice vanilla = shaderNoticeFor(S("Plain"), ModPlatform::ModLoaderTypes(), none);
        QVERIFY(vanilla.show);
        QCOMPARE(vanilla.text, S("Shaders need Iris or OptiFine, which need a modded instance."));
        QVERIFY(vanilla.buttonText.isEmpty());
        QVERIFY(vanilla.searchQuery.isEmpty());
        // a shader mod in the folder: no notice at all
        for (const auto loaders : { ModPlatform::ModLoaderTypes(ModPlatform::Fabric), ModPlatform::ModLoaderTypes(ModPlatform::Forge),
                                    ModPlatform::ModLoaderTypes() }) {
            QVERIFY(!shaderNoticeFor(S("X"), loaders, has).show);
        }
        // a loader ZERO knows no shader mod for: nothing to say
        QVERIFY(!shaderNoticeFor(S("X"), ModPlatform::ModLoaderTypes(ModPlatform::LegacyFabric), none).show);
    }

    void noticeStripAndFindButton()
    {
        Rig rig;
        rig.open();
        rig.section->setTargets({ fabric121() }, S("fab"));
        auto* notice = rig.section->shaderNotice();
        QVERIFY(notice->isVisibleTo(rig.section.get()));
        QVERIFY(notice->button()->isVisibleTo(notice));
        QCOMPARE(notice->button()->text(), S("Find Iris"));
        QVERIFY(!notice->grab().toImage().isNull());
        QSignalSpy find(rig.section.get(), &ModsSection::findModRequested);
        notice->button()->click();
        QCOMPARE(find.size(), 1);
        QCOMPARE(find.first().at(0).toString(), S("fab"));
        QCOMPARE(find.first().at(1).toString(), S("Iris"));
    }

    // ---- the card ----

    void cardPills()
    {
        // performance: the label, a range for several tags, never a number
        QCOMPARE(PackDelegate::shaderPills(*makeShader(S("1"), S("A"), { S("low") }, { S("iris") })), (QStringList{ S("Lightweight"), S("Iris") }));
        QCOMPARE(PackDelegate::shaderPills(*makeShader(S("1"), S("A"), { S("medium") }, { S("iris") })).first(), S("Balanced"));
        QCOMPARE(PackDelegate::shaderPills(*makeShader(S("1"), S("A"), { S("high") }, { S("iris") })).first(), S("High quality"));
        QCOMPARE(PackDelegate::shaderPills(*makeShader(S("1"), S("A"), { S("potato"), S("high") }, { S("iris") })).first(),
                 QString::fromUtf8("Lightweight \xE2\x80\x93 High quality"));
        // platforms in a stable order; "vanilla" is no pill
        QCOMPARE(PackDelegate::shaderPills(*makeShader(S("1"), S("A"), {}, { S("vanilla"), S("canvas"), S("optifine"), S("iris") })),
                 (QStringList{ S("Iris"), S("OptiFine"), S("Canvas") }));
        QVERIFY(PackDelegate::shaderPills(*makeShader(S("1"), S("A"), {}, { S("vanilla") })).isEmpty());
        QVERIFY(PackDelegate::shaderPills(*makeShader(S("1"), S("A"), { S("cartoon") }, { S("iris") })).first() == S("Iris"));  // other tags: no label
        // CurseForge has no performance data: its ids are never read as tags
        QVERIFY(PackDelegate::shaderPills(*makeShader(S("1"), S("A"), { S("low") }, {}, Provider::CurseForge)).isEmpty());
    }

    void galleryCardsPaint()
    {
        Rig rig;
        rig.installed.list = { { S("modrinth:1"), S("f"), S("Shader 1"), QString() } };
        rig.openWith({ vanilla121() }, S("van"),
                     { makeShaders(1).first(), makeShader(S("9"), S("Many"), { S("potato"), S("high") }, { S("iris"), S("optifine"), S("canvas") }) });
        QApplication::processEvents();
        QVERIFY(!rig.section->grab().toImage().isNull());
        auto* model = rig.section->model();
        QVERIFY(model->data(model->index(0), ModpackListModel::InstalledRole).toBool());
        QVERIFY(!model->data(model->index(1), ModpackListModel::InstalledRole).toBool());
    }

    // ---- details and the primary action ----

    void headerPillsAreTheRangeAndTheShaderFacts()
    {
        Rig rig;
        rig.openWith({ vanilla121() }, S("van"), { makeShader(S("1"), S("A"), { S("medium") }, { S("iris"), S("optifine") }) });
        rig.section->selectRow(0);
        QTRY_COMPARE(rig.modrinth.versionCalls.size(), 1);
        const QStringList pills = rig.section->panel()->pillTexts();
        QVERIFY(pills.contains(S("Balanced")));
        QVERIFY(pills.contains(S("Iris")));
        QVERIFY(pills.contains(S("OptiFine")));
    }

    void primaryActionStates()
    {
        Rig rig;
        rig.installed.list = { { S("modrinth:1"), S("v2"), S("Shader 1"), S("2.0") }, { S("modrinth:2"), S("v0"), S("Shader 2"), S("0.5") } };
        rig.openWith({ vanilla121() }, S("van"), makeShaders(3));
        auto* panel = rig.section->panel();
        const QList<ModpackVersion> versions = { makeVersion(S("v2"), S("2.0")), makeVersion(S("v1"), S("1.0")) };
        auto open = [&](int row, int call) {
            rig.section->selectRow(row);
            QTRY_COMPARE(rig.modrinth.versionCalls.size(), call + 1);
            rig.modrinth.answerVersions(call, versions);
        };
        open(0, 0);
        QCOMPARE(panel->primaryButton()->text(), S("Installed"));
        QVERIFY(panel->noteText().contains(S("newest version of this shader")));
        open(1, 1);
        QCOMPARE(panel->primaryButton()->text(), S("Update"));
        QVERIFY(panel->primaryButton()->isEnabled());
        QSignalSpy requests(rig.section.get(), &ModsSection::installRequested);
        panel->primaryButton()->click();
        QCOMPARE(requests.size(), 1);
        QCOMPARE(requests.first().at(1).toString(), S("van"));
        QCOMPARE(requests.first().at(2).toString(), S("v0"));
        open(2, 2);
        QCOMPARE(panel->primaryButton()->text(), S("Add to Vanilla"));
        QVERIFY(panel->primaryButton()->isEnabled());
    }

    void runningTargetAndNoFittingVersion()
    {
        Rig rig;
        rig.openWith({ makeTarget(S("van"), S("Vanilla"), S("1.21.1"), ModPlatform::ModLoaderTypes(), true) }, S("van"), makeShaders(1));
        rig.section->selectRow(0);
        QTRY_COMPARE(rig.modrinth.versionCalls.size(), 1);
        rig.modrinth.answerVersions(0, { makeVersion(S("v1"), S("1.0")) });
        QVERIFY(!rig.section->panel()->primaryButton()->isEnabled());
        QCOMPARE(rig.section->panel()->noteText(), S("Close Vanilla to add shaders"));
        rig.section->setTargets({ vanilla121() }, S("van"));
        QVERIFY(rig.section->panel()->primaryButton()->isEnabled());
        rig.section->selectRow(0);
        rig.modrinth.answerVersions(0, { makeVersion(S("old"), S("Old"), S("1.8.9")) });
        QVERIFY(!rig.section->panel()->primaryButton()->isEnabled());
        QCOMPARE(rig.section->panel()->noteText(), S("No version of this shader fits 1.21.1."));
    }

    // ---- persistence ----

    void savedFilterKeepsFacetsButNeverTheVersionOrThePlatform()
    {
        Rig rig;
        rig.modFiles = { S("iris.jar") };
        rig.openWith({ fabric121() }, S("fab"));
        FilterState s = rig.section->state();
        s.selected.insert(FacetId::performance, { S("potato") });
        rig.section->applyState(s);
        QVERIFY(!rig.savedFilters.isEmpty());
        const QString json = rig.savedFilters.last();
        QVERIFY2(json.contains(S("potato")), qPrintable(json));
        QVERIFY2(!json.contains(S("1.21.1")), qPrintable(json));
        QVERIFY2(!json.contains(S("iris")), qPrintable(json));
    }

    // ---- the add dialog in shader mode ----

    void dialogInShaderMode()
    {
        RemoteImageCache images([](const QString&, std::function<void(const RemoteImageCache::FetchResult&)> done) { done({}); });
        ModInstallEnvironment env;
        env.images = &images;
        env.packs = true;
        env.shaders = true;
        env.instanceIcon = [](const QString&) { return QIcon(); };
        bool running = false;
        env.isRunning = [&running](const QString&) { return running; };
        const auto versions = QList<ModPlatform::IndexedVersion>{ indexedVersion(S("f2"), S("2.0"), { S("1.21.1") }),
                                                                  indexedVersion(S("f1"), S("1.0"), { S("1.21.1") }),
                                                                  indexedVersion(S("f0"), S("0.9"), { S("1.20.1") }) };
        ModInstallDialog dialog(env, shaderWithVersions(versions), fabric121(), QString());
        QCOMPARE(dialog.versionBox()->count(), 2);  // the 1.20.1 one does not fit
        QCOMPARE(dialog.primaryButton()->text(), S("Add shader"));
        QVERIFY(dialog.canInstall());
        QCOMPARE(dialog.targetLine(), S("Adding to Fabric Fun · 1.21.1"));
        QVERIFY(!dialog.turnOnBox()->isVisibleTo(&dialog));  // the game picks the shader, nothing is written to its settings
        QVERIFY(!dialog.skipDependenciesBox()->isVisibleTo(&dialog));
        QVERIFY(dialog.shaderNote()->isVisibleTo(&dialog));
        QCOMPARE(dialog.shaderNote()->text(), S("Then pick it in the game's shader settings."));
        const ModInstallPlan plan = dialog.plan();
        QCOMPARE(plan.count(), 1);
        QCOMPARE(plan.mods.first().reason, reasonStandalone());
        QCOMPARE(plan.mods.first().version.fileId.toString(), S("f2"));

        ModInstallDialog update(env, shaderWithVersions(versions), vanilla121(), S("f1"));
        QCOMPARE(update.primaryButton()->text(), S("Update"));
        QCOMPARE(update.plan().mods.first().reason, reasonUpdate());
        running = true;
        ModInstallDialog busy(env, shaderWithVersions(versions), vanilla121(), QString());
        QVERIFY(!busy.canInstall());
        QCOMPARE(busy.noteText(), S("Close Vanilla to add shaders"));

        // a resource pack dialog is unchanged: no shader note
        ModInstallEnvironment packEnv = env;
        packEnv.shaders = false;
        running = false;
        ModInstallDialog pack(packEnv, shaderWithVersions(versions), fabric121(), QString());
        QCOMPARE(pack.primaryButton()->text(), S("Add pack"));
        QVERIFY(!pack.shaderNote()->isVisibleTo(&pack));
    }

    void installPlanOfAShader()
    {
        QCOMPARE(shaderPrimaryActionText(false), S("Add shader"));
        QCOMPARE(shaderPrimaryActionText(true), S("Update"));
        const auto versions = QList<ModPlatform::IndexedVersion>{ indexedVersion(S("f2"), S("2.0"), { S("1.21.1") }) };
        const ModInstallPlan plan = buildPackInstallPlan(shaderWithVersions(versions)->pack, versions.first(), false);
        QCOMPARE(plan.count(), 1);  // the shader alone, no dependencies
        QCOMPARE(plan.dependencyCount(), 0);
        QCOMPARE(plan.mods.first().reason, reasonStandalone());
        QCOMPARE(buildPackInstallPlan(shaderWithVersions(versions)->pack, versions.first(), true).mods.first().reason, reasonUpdate());
    }

    // ---- installed ----

    void installedFromTheShaderpacksFolderItself()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QDir dir(temp.path());  // the shaderpacks folder: files and packwiz metadata side by side
        writeIndexEntry(dir, S("complementary"), S("Complementary.zip"), S("P1"), S("V1"));
        writeIndexEntry(dir, S("gone"), S("Gone.zip"), S("P2"), S("V2"));  // the file was deleted outside the launcher
        writeIndexEntry(dir, S("hidden"), S("Hidden.zip"), S("P3"), S("V3"));
        QFile(dir.absoluteFilePath(S("Complementary.zip"))).open(QIODevice::WriteOnly);
        QFile(dir.absoluteFilePath(S("Hidden.zip.disabled"))).open(QIODevice::WriteOnly);
        QFile(dir.absoluteFilePath(S("Local Shader.zip"))).open(QIODevice::WriteOnly);  // no metadata: not listed

        const QList<InstalledMod> found = installedFromIndex(dir, dir);
        QCOMPARE(found.size(), 2);  // the .pw.toml files are metadata, never entries of their own
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
            }
        }
    }

    // ---- the page ----

    void switcherHasFourSegmentsAndRestoresShaders()
    {
        Rig modsRig;
        Rig packsRig;
        Rig shadersRig;
        FakeSource modpackSource(Provider::Modrinth);
        ModpacksEnvironment modpacks;
        modpacks.sources.modrinth = &modpackSource;
        modpacks.images = &shadersRig.images;
        QString section = S("shaders");
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
        DiscoverPage page(modpacks, modsRig.environment(), packsRig.environment(), shadersRig.environment(), hooks, nullptr);
        page.setLibrarySelection([]() { return QString(); });
        page.resize(1280, 720);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));

        QCOMPARE(page.sections(), (QStringList{ S("modpacks"), S("mods"), S("resourcepacks"), S("shaders") }));
        QCOMPARE(page.switcher()->count(), 4);
        QCOMPARE(page.switcher()->valueAt(3), S("shaders"));
        QCOMPARE(page.currentSection(), S("shaders"));  // restored on start
        QVERIFY(written.isEmpty());
        QCOMPARE(page.switcher()->selected(), S("shaders"));
        QVERIFY(page.current() == page.shaders());
        QCOMPARE(page.shaders()->target().id, S("fab"));
        QVERIFY(!shadersRig.modrinth.searches.isEmpty());
        QCOMPARE(shadersRig.modrinth.searches.first().args.type, ModPlatform::ResourceType::ShaderPack);
        QVERIFY(packsRig.modrinth.searches.isEmpty());  // the other sections wait until they are shown

        page.switcher()->onActivated(S("resourcepacks"));
        QCOMPARE(written.last(), S("resourcepacks"));
        page.switcher()->onActivated(S("shaders"));
        QCOMPARE(page.currentSection(), S("shaders"));
        QCOMPARE(written.last(), S("shaders"));  // remembered
        page.showSection(S("modpacks"));
        QCOMPARE(page.currentSection(), S("modpacks"));
        page.showSection(S("shaders"), S("curseforge"));
        QCOMPARE(page.currentSection(), S("shaders"));
        QCOMPARE(page.shaders()->provider(), Provider::CurseForge);

        // showShadersFor: the target is chosen and remembered; a query becomes the search text
        QVERIFY(page.showShadersFor(S("van"), S("complementary")));
        QCOMPARE(page.shaders()->target().id, S("van"));
        QCOMPARE(target, S("van"));
        QCOMPARE(page.shaders()->state().query, S("complementary"));
        QVERIFY(!page.showShadersFor(S("nov")));  // no Minecraft version: not a target
        QCOMPARE(page.shaders()->target().id, S("van"));

        // signals
        QSignalSpy install(&page, &DiscoverPage::shaderInstallRequested);
        QSignalSpy packInstall(&page, &DiscoverPage::packInstallRequested);
        QSignalSpy createVanilla(&page, &DiscoverPage::createVanillaInstanceRequested);
        QSignalSpy find(&page, &DiscoverPage::findShaderModRequested);
        emit page.shaders()->installRequested(makeShader(S("1"), S("A")), S("van"), QString());
        QCOMPARE(install.size(), 1);
        QCOMPARE(packInstall.size(), 0);
        emit page.shaders()->createInstanceRequested();
        QCOMPARE(createVanilla.size(), 1);
        emit page.shaders()->findModRequested(S("van"), S("Iris"));
        QCOMPARE(find.size(), 1);
        QCOMPARE(find.first().at(0).toString(), S("van"));
        QCOMPARE(find.first().at(1).toString(), S("Iris"));
    }

    void pageWithoutShadersKeepsItsSections()
    {
        Rig rig;
        Rig packsRig;
        FakeSource modpackSource(Provider::Modrinth);
        ModpacksEnvironment modpacks;
        modpacks.sources.modrinth = &modpackSource;
        modpacks.images = &rig.images;
        DiscoverHooks hooks;
        hooks.loadSection = []() { return S("shaders"); };  // not built: starts on Modpacks
        hooks.readTargets = []() { return QList<TargetInstance>{ vanilla121() }; };
        DiscoverPage page(modpacks, rig.environment(), packsRig.environment(), hooks, nullptr);
        QCOMPARE(page.switcher()->count(), 3);
        QCOMPARE(page.sections(), (QStringList{ S("modpacks"), S("mods"), S("resourcepacks") }));
        QVERIFY(page.shaders() == nullptr);
        QCOMPARE(page.currentSection(), S("modpacks"));
        QVERIFY(!page.showShadersFor(S("van")));
    }
};

QTEST_MAIN(ZeroDiscoverShadersTest)

#include "ZeroDiscoverShaders_test.moc"
