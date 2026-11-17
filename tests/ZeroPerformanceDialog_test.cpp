// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the "Boost performance" dialog (docs/zero/SHELL.md): vanilla, every row state (installed, OptiFine conflict,
// unavailable), loading -> error -> retry with stale answers dropped, the dependency check (one run for the ticked set, debounced, stale
// answers dropped, failure with Retry), the memory section (the rule, "fine", the toggle), the Java line and its Fix, the summary, and Apply:
// the install seam gets the ticked projects plus the dependencies and is not awaited, the memory is written at once, a running instance is refused.
// Offscreen-style with fakes for the catalog, the resolver and every seam: no network, no instances, no APPLICATION.
#include <QApplication>
#include <QCheckBox>
#include <QPushButton>
#include <QTest>

#include <functional>
#include <memory>

#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/content/RemoteImageCache.h"
#include "ui/zero/perf/PerformanceDialog.h"
#include "zero/content/ModInstall.h"

using namespace Zero;
using namespace Zero::Perf;
using Zero::Content::DependencyResolver;
using Zero::Content::PlannedMod;
using Zero::Content::TargetInstance;

namespace {

QString S(const char* text)
{
    return QString::fromUtf8(text);
}

constexpr int kGb = 1024;
const QString kArrow = QString::fromUtf8("\xE2\x86\x92");
const QString kDot = QString::fromUtf8(" \xC2\xB7 ");

ModPlatform::IndexedVersion version(const QString& id, const QString& mc, ModPlatform::ModLoaderTypes loaders)
{
    ModPlatform::IndexedVersion v;
    v.fileId = id;
    v.version = id;
    v.versionNumber = id;
    v.versionType = ModPlatform::IndexedVersionType(ModPlatform::IndexedVersionTypeValue::Release);
    v.mcVersion = { mc };
    v.loaders = loaders;
    v.downloadUrl = S("https://cdn.test/%1.jar").arg(id);
    v.fileName = id + S(".jar");
    return v;
}

Found foundOf(const QString& slug, const QList<ModPlatform::IndexedVersion>& versions)
{
    Found f;
    f.slug = slug;
    f.pack = std::make_shared<ModPlatform::IndexedPack>();
    f.pack->addonId = S("ID-") + slug;
    f.pack->name = projectFor(slug).name;
    f.pack->provider = ModPlatform::ResourceProvider::MODRINTH;
    f.versions = versions;
    return f;
}

FoundMap everything(Loader loader, const QString& mc, ModPlatform::ModLoaderTypes loaders)
{
    FoundMap map;
    for (const QString& slug : slugsFor(loader)) {
        map.insert(slug, foundOf(slug, { version(slug + S("-1"), mc, loaders) }));
    }
    return map;
}

PlannedMod depOf(const QString& id, const QString& name, const QString& requiredBy)
{
    PlannedMod mod;
    mod.pack = std::make_shared<ModPlatform::IndexedPack>();
    mod.pack->addonId = id;
    mod.pack->provider = ModPlatform::ResourceProvider::MODRINTH;
    mod.pack->name = name;
    mod.version = version(S("dep-") + id, S("1.21.1"), ModPlatform::ModLoaderTypes(ModPlatform::Fabric));
    mod.version.addonId = id;
    mod.reason = Content::reasonDependency();
    mod.dependentOn = S("file-of-") + requiredBy;
    mod.requiredBy = { requiredBy };
    return mod;
}

int g_catalogCancels = 0;
int g_resolverCancels = 0;

class FakeCatalog : public PerformanceCatalog {
   public:
    using PerformanceCatalog::PerformanceCatalog;
    void load(quint64 token, const Request& request) override { requests.append({ token, request }); }
    void cancel() override
    {
        ++cancels;
        ++g_catalogCancels;
    }
    void answer(const FoundMap& found, int request = -1)
    {
        const int at = request < 0 ? int(requests.size()) - 1 : request;
        emit loaded(requests.at(at).first, found);
    }
    void fail(const QString& reason = S("offline"), int request = -1)
    {
        const int at = request < 0 ? int(requests.size()) - 1 : request;
        emit failed(requests.at(at).first, reason);
    }
    QList<QPair<quint64, Request>> requests;
    int cancels = 0;
};

class FakeResolver : public DependencyResolver {
   public:
    using DependencyResolver::DependencyResolver;
    void resolve(quint64 token, const Request& request) override { requests.append({ token, request }); }
    void cancel() override
    {
        ++cancels;
        ++g_resolverCancels;
    }
    void answer(const QList<PlannedMod>& deps, int request = -1)
    {
        const int at = request < 0 ? int(requests.size()) - 1 : request;
        emit resolved(requests.at(at).first, deps);
    }
    void fail(const QString& reason = S("boom")) { emit failed(requests.last().first, reason); }
    QList<QPair<quint64, Request>> requests;
    int cancels = 0;
};

TargetInstance fabricTarget()
{
    TargetInstance t;
    t.id = S("fab");
    t.name = S("Fabric Fun");
    t.mcVersion = S("1.21.1");
    t.loaders = ModPlatform::ModLoaderTypes(ModPlatform::Fabric);
    t.loaderText = S("Fabric");
    return t;
}

TargetInstance vanillaTarget()
{
    TargetInstance t;
    t.id = S("van");
    t.name = S("Plain Game");
    t.mcVersion = S("1.21.1");
    return t;
}

/// The environment of a dialog: fakes for every seam, the test decides what they answer and when.
struct Rig {
    RemoteImageCache images{ [](const QString&, std::function<void(const RemoteImageCache::FetchResult&)> done) { done({}); } };
    TargetInstance target = fabricTarget();
    bool managed = false;
    int ramMb = 16 * kGb;
    int currentMax = 2 * kGb;
    bool running = false;
    InstalledSnapshot installed;
    bool deferInstalled = false;
    std::function<void(const InstalledSnapshot&)> pendingInstalled;
    std::optional<QString> javaProblem;
    bool dependencyCheck = true;
    int delayMs = 20;

    FakeCatalog* catalog = nullptr;
    FakeResolver* resolver = nullptr;
    QList<Content::CartInstallPlan> installs;
    QList<QPair<int, int>> memoryWrites;
    int javaMajor = 0;  // 0 = unknown: no Java arguments section
    int cores = 8;
    QString jvmArgs;
    QStringList jvmWrites;
    JvmArgsPrevious jvmPrevious;

    PerformanceEnvironment environment()
    {
        PerformanceEnvironment env;
        env.target = target;
        env.managedPack = managed;
        env.ramMb = ramMb;
        env.images = &images;
        env.makeCatalog = [this](QObject* parent) -> PerformanceCatalog* {
            catalog = new FakeCatalog(parent);
            return catalog;
        };
        env.makeResolver = [this](QObject* parent) -> DependencyResolver* {
            resolver = new FakeResolver(parent);
            return resolver;
        };
        env.dependencyCheckEnabled = [this]() { return dependencyCheck; };
        env.loadInstalled = [this](QObject*, std::function<void(const InstalledSnapshot&)> done) {
            if (deferInstalled) {
                pendingInstalled = std::move(done);
            } else {
                done(installed);
            }
        };
        env.maxMemoryMb = [this]() { return currentMax; };
        env.checkJava = [this](QObject*, std::function<void(const std::optional<QString>&)> done) { done(javaProblem); };
        env.isRunning = [this]() { return running; };
        env.install = [this](const Content::CartInstallPlan& plan) {
            installs.append(plan);  // returns at once, like the Downloads center: nothing is awaited
        };
        env.writeMemory = [this](int maxMb, int minMb) { memoryWrites.append({ maxMb, minMb }); };
        env.javaMajor = [this]() { return javaMajor; };
        env.cpuCores = [this]() { return cores; };
        env.currentJvmArgs = [this]() { return jvmArgs; };
        env.writeJvmArgs = [this](const QString& args) {
            jvmWrites.append(args);
            return jvmPrevious;
        };
        env.dependencyDelayMs = delayMs;
        return env;
    }

    std::unique_ptr<PerformanceDialog> open() { return std::make_unique<PerformanceDialog>(environment()); }

    /// Opens a Fabric dialog and answers the catalog with a file for every project.
    std::unique_ptr<PerformanceDialog> openReady()
    {
        auto dialog = open();
        catalog->answer(everything(Loader::Fabric, S("1.21.1"), target.loaders));
        return dialog;
    }
};

}  // namespace

class ZeroPerformanceDialogTest : public QObject {
    Q_OBJECT

   private slots:
    void init()
    {
        g_catalogCancels = 0;
        g_resolverCancels = 0;
        ThemeSpec spec = ThemeSpec::obsidian();
        spec.reduceMotion = true;
        Theme::setCurrent(spec);
    }
    void cleanup()
    {
        ThemeSpec spec = ThemeSpec::obsidian();
        spec.reduceMotion = false;
        Theme::setCurrent(spec);
    }

    // ---- vanilla ----

    void vanillaShowsOneLineAndStillOffersMemory()
    {
        Rig rig;
        rig.target = vanillaTarget();
        auto dialog = rig.open();
        QCOMPARE(dialog->modsState(), PerformanceDialog::ModsState::Vanilla);
        QCOMPARE(dialog->titleText(), QString::fromUtf8("Boost performance \xC2\xB7 Plain Game"));
        QCOMPARE(dialog->modsLineText(), vanillaModsText());
        QCOMPARE(dialog->rowCount(), 0);
        QVERIFY2(!rig.catalog, "vanilla never asks Modrinth");
        QCOMPARE(dialog->skeletonRowCount(), 0);
        QVERIFY(dialog->memorySectionVisible());
        QCOMPARE(dialog->ramLineText(), S("Your PC has 16 GB of RAM."));
        QCOMPARE(dialog->memoryText(), S("Give Minecraft 3 GB"));  // vanilla: 3 GB
        QCOMPARE(dialog->memoryNowText(), S("now 2 GB"));
        QVERIFY(dialog->memoryBox()->isChecked());
        QCOMPARE(dialog->summaryText(), S("Memory 2 ") + kArrow + S(" 3 GB"));
        QVERIFY(dialog->applyButton()->isEnabled());

        dialog->applyButton()->click();
        QCOMPARE(dialog->result(), int(QDialog::Accepted));
        QVERIFY(rig.installs.isEmpty());
        QCOMPARE(rig.memoryWrites.size(), 1);
        QCOMPARE(rig.memoryWrites.first(), qMakePair(3 * kGb, 1 * kGb));
        QVERIFY(dialog->outcome().applied);
        QVERIFY(dialog->outcome().memoryWritten);
        QCOMPARE(dialog->outcome().modsQueued, 0);
    }

    void vanillaWithEnoughMemoryHasNothingToApply()
    {
        Rig rig;
        rig.target = vanillaTarget();
        rig.currentMax = 4 * kGb;
        auto dialog = rig.open();
        QCOMPARE(dialog->memoryText(), S("Memory is fine (4 GB)"));
        QVERIFY(dialog->memoryNowText().isEmpty());
        QVERIFY(!dialog->applyButton()->isEnabled());
        QCOMPARE(dialog->summaryText(), S("Nothing to change"));
        dialog->accept();  // Enter does nothing
        QVERIFY(dialog->result() != int(QDialog::Accepted));
        QVERIFY(rig.memoryWrites.isEmpty());
    }

    // ---- loading, error, retry ----

    void loadingShowsSkeletonRowsAndDisablesApply()
    {
        Rig rig;
        auto dialog = rig.open();
        QVERIFY(rig.catalog);
        QCOMPARE(dialog->modsState(), PerformanceDialog::ModsState::Loading);
        QCOMPARE(dialog->modsTitleText(), QString::fromUtf8("Mods \xC2\xB7 Fabric 1.21.1"));
        QCOMPARE(dialog->skeletonRowCount(), 6);
        QCOMPARE(dialog->rowCount(), 0);
        QVERIFY(!dialog->applyButton()->isEnabled());
        QCOMPARE(dialog->summaryText(), Content::cartCheckingText());
        QVERIFY2(!dialog->memorySectionVisible(), "the memory advice waits for the mods it depends on");
        // the request: the table's slugs for this version and loader
        QCOMPARE(rig.catalog->requests.size(), 1);
        const auto request = rig.catalog->requests.first().second;
        QCOMPARE(request.slugs, slugsFor(Loader::Fabric));
        QCOMPARE(request.mcVersion, S("1.21.1"));
        QCOMPARE(request.loaders, rig.target.loaders);
    }

    void errorWithRetryThenSuccess()
    {
        Rig rig;
        auto dialog = rig.open();
        rig.catalog->fail(S("no network"));
        QCOMPARE(dialog->modsState(), PerformanceDialog::ModsState::Failed);
        QVERIFY(dialog->errorText().contains(S("Modrinth")));
        QCOMPARE(dialog->skeletonRowCount(), 0);
        QVERIFY(dialog->retryButton()->isVisibleTo(dialog.get()));
        QVERIFY2(dialog->memorySectionVisible(), "memory still works when Modrinth does not answer");
        QVERIFY(!dialog->applyButton()->isEnabled() || dialog->tickedSlugs().isEmpty());

        dialog->retryButton()->click();
        QCOMPARE(dialog->modsState(), PerformanceDialog::ModsState::Loading);
        QCOMPARE(dialog->skeletonRowCount(), 6);
        QCOMPARE(rig.catalog->requests.size(), 2);
        QVERIFY(rig.catalog->requests.at(0).first != rig.catalog->requests.at(1).first);

        rig.catalog->answer(everything(Loader::Fabric, S("1.21.1"), rig.target.loaders));
        QCOMPARE(dialog->modsState(), PerformanceDialog::ModsState::Ready);
        QCOMPARE(dialog->rowCount(), 6);
        QCOMPARE(dialog->skeletonRowCount(), 0);
    }

    void aStaleCatalogAnswerIsDropped()
    {
        Rig rig;
        auto dialog = rig.open();
        rig.catalog->fail(S("no network"));
        dialog->retryButton()->click();
        // the first request answers late: it must not build rows or end the loading state
        rig.catalog->answer(everything(Loader::Fabric, S("1.21.1"), rig.target.loaders), 0);
        QCOMPARE(dialog->modsState(), PerformanceDialog::ModsState::Loading);
        QCOMPARE(dialog->rowCount(), 0);
        rig.catalog->fail(S("late"), 0);
        QCOMPARE(dialog->modsState(), PerformanceDialog::ModsState::Loading);
        rig.catalog->answer(everything(Loader::Fabric, S("1.21.1"), rig.target.loaders), 1);
        QCOMPARE(dialog->modsState(), PerformanceDialog::ModsState::Ready);
    }

    void rowsWaitForTheInstalledMods()
    {
        Rig rig;
        rig.deferInstalled = true;
        auto dialog = rig.open();
        rig.catalog->answer(everything(Loader::Fabric, S("1.21.1"), rig.target.loaders));
        QCOMPARE(dialog->modsState(), PerformanceDialog::ModsState::Loading);  // the folder is not read yet
        InstalledSnapshot installed;
        installed.modIds = { S("lithium") };
        rig.pendingInstalled(installed);
        QCOMPARE(dialog->modsState(), PerformanceDialog::ModsState::Ready);
        QCOMPARE(dialog->rowState(S("lithium")), RowState::Installed);
    }

    // ---- rows ----

    void aFabricInstanceListsTheCuratedModsAllTicked()
    {
        Rig rig;
        auto dialog = rig.openReady();
        QCOMPARE(dialog->modsState(), PerformanceDialog::ModsState::Ready);
        QCOMPARE(dialog->rowSlugs(), slugsFor(Loader::Fabric));
        QCOMPARE(dialog->tickedSlugs(), slugsFor(Loader::Fabric));
        for (const QString& slug : dialog->rowSlugs()) {
            QCOMPARE(dialog->rowState(slug), RowState::Offered);
            QVERIFY(dialog->rowCheck(slug)->isEnabled());
            QVERIFY(dialog->rowCheck(slug)->isChecked());
            QVERIFY(dialog->rowPillText(slug).isEmpty());
        }
        QCOMPARE(dialog->rowName(S("sodium")), S("Sodium"));
        QCOMPARE(dialog->rowName(S("ferrite-core")), S("FerriteCore"));
        QVERIFY(dialog->noteText().isEmpty());
    }

    void allInstalledLeavesNothingToTick()
    {
        Rig rig;
        rig.currentMax = 6 * kGb;
        for (const QString& slug : slugsFor(Loader::Fabric)) {
            rig.installed.modIds << projectFor(slug).modIds;
        }
        rig.installed.count = 6;
        auto dialog = rig.openReady();
        QCOMPARE(dialog->tickedSlugs(), QStringList());
        for (const QString& slug : dialog->rowSlugs()) {
            QCOMPARE(dialog->rowState(slug), RowState::Installed);
            QCOMPARE(dialog->rowPillText(slug), S("Installed"));
            QVERIFY(!dialog->rowCheck(slug)->isEnabled());
            QVERIFY(!dialog->rowCheck(slug)->isChecked());
        }
        QCOMPARE(dialog->memoryText(), S("Memory is fine (6 GB)"));
        QCOMPARE(dialog->summaryText(), S("Nothing to change"));
        QVERIFY(!dialog->applyButton()->isEnabled());
        QVERIFY2(rig.resolver->requests.isEmpty(), "nothing ticked: nothing to resolve");
    }

    void optiFineConflictRowIsUntickedWithAWarningPill()
    {
        Rig rig;
        rig.installed.modIds = { S("optifine") };
        rig.installed.count = 1;
        auto dialog = rig.openReady();
        QCOMPARE(dialog->rowState(S("sodium")), RowState::ConflictOptiFine);
        QCOMPARE(dialog->rowPillText(S("sodium")), S("Conflicts with OptiFine"));
        QVERIFY(!dialog->rowCheck(S("sodium"))->isChecked());
        QVERIFY(!dialog->rowCheck(S("sodium"))->isEnabled());
        QCOMPARE(dialog->tickedSlugs().size(), 5);
        QVERIFY(!dialog->tickedSlugs().contains(S("sodium")));
    }

    void unavailableRowSaysForWhichVersion()
    {
        Rig rig;
        auto dialog = rig.open();
        FoundMap map = everything(Loader::Fabric, S("1.21.1"), rig.target.loaders);
        map.insert(S("lithium"), foundOf(S("lithium"), { version(S("old"), S("1.20.1"), rig.target.loaders) }));
        rig.catalog->answer(map);
        QCOMPARE(dialog->rowState(S("lithium")), RowState::Unavailable);
        QCOMPARE(dialog->rowPillText(S("lithium")), S("Not available for 1.21.1"));
        QVERIFY(!dialog->rowCheck(S("lithium"))->isEnabled());
        QVERIFY(!dialog->rowCheck(S("lithium"))->isChecked());
        QCOMPARE(dialog->tickedSlugs().size(), 5);
    }

    void neoForgeListsSodiumOrItsEmbeddiumFallback()
    {
        Rig rig;
        rig.target.loaders = ModPlatform::ModLoaderTypes(ModPlatform::NeoForge);
        rig.target.loaderText = S("NeoForge");
        auto dialog = rig.open();
        QCOMPARE(dialog->modsTitleText(), QString::fromUtf8("Mods \xC2\xB7 NeoForge 1.21.1"));
        FoundMap map = everything(Loader::NeoForge, S("1.21.1"), rig.target.loaders);
        map.insert(S("sodium"), foundOf(S("sodium"), {}));  // no NeoForge build of Sodium for this version
        rig.catalog->answer(map);
        QCOMPARE(dialog->rowSlugs().first(), S("embeddium"));
        QCOMPARE(dialog->rowName(S("embeddium")), S("Embeddium"));
        QCOMPARE(dialog->rowState(S("embeddium")), RowState::Offered);
    }

    void aManagedModpackGetsTheNote()
    {
        Rig rig;
        rig.managed = true;
        auto dialog = rig.openReady();
        QCOMPARE(dialog->noteText(), modpackNote());
        Rig loading;
        loading.managed = true;
        auto other = loading.open();
        QVERIFY2(other->noteText().isEmpty(), "no note while there is no list");
    }

    void clickingARowFlipsItsBox()
    {
        Rig rig;
        auto dialog = rig.openReady();
        QCheckBox* box = dialog->rowCheck(S("lithium"));
        QVERIFY(box->isChecked());
        box->click();
        QVERIFY(!box->isChecked());
        QVERIFY(!dialog->tickedSlugs().contains(S("lithium")));
        box->click();
        QVERIFY(dialog->tickedSlugs().contains(S("lithium")));
    }

    // ---- dependencies ----

    void oneDependencyRunCoversTheTickedModsAndBlocksApplyMeanwhile()
    {
        Rig rig;
        auto dialog = rig.openReady();
        QVERIFY(dialog->dependencyCheckPending());
        QVERIFY(!dialog->applyButton()->isEnabled());
        QCOMPARE(dialog->summaryText(), Content::cartCheckingText());
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        const auto request = rig.resolver->requests.first().second;
        QCOMPARE(request.instanceId, S("fab"));
        QCOMPARE(request.all().size(), 6);
        QVERIFY(!request.pack);  // all of them travel in `also`
        QVERIFY(!dialog->applyButton()->isEnabled());

        rig.resolver->answer({ depOf(S("FAPI"), S("Fabric API"), S("Sodium")) });
        QVERIFY(!dialog->dependencyCheckPending());
        QCOMPARE(dialog->dependencyNames(), QStringList{ S("Fabric API") });
        QCOMPARE(dialog->summaryText(), S("Adds 6 mods (+1 they need)") + kDot + S("Memory 2 ") + kArrow + S(" 5 GB"));
        QVERIFY(dialog->applyButton()->isEnabled());
    }

    void changingTheTicksStartsOneNewRunAndDropsTheStaleAnswer()
    {
        Rig rig;
        auto dialog = rig.openReady();
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        dialog->rowCheck(S("lithium"))->click();
        dialog->rowCheck(S("modernfix"))->click();  // two quick changes: one run
        QTRY_COMPARE(rig.resolver->requests.size(), 2);
        QTest::qWait(rig.delayMs * 3);
        QCOMPARE(rig.resolver->requests.size(), 2);
        QCOMPARE(rig.resolver->requests.last().second.all().size(), 4);

        // the answer of the first run arrives late: dropped
        rig.resolver->answer({ depOf(S("STALE"), S("Stale Lib"), S("Sodium")) }, 0);
        QVERIFY(dialog->dependencyCheckPending());
        QVERIFY(dialog->dependencyNames().isEmpty());
        QVERIFY(!dialog->applyButton()->isEnabled());

        rig.resolver->answer({}, 1);
        QVERIFY(!dialog->dependencyCheckPending());
        QCOMPARE(dialog->summaryText(), S("Adds 4 mods") + kDot + S("Memory 2 ") + kArrow + S(" 5 GB"));
    }

    void aDependencyThatIsItselfTickedIsNotCountedTwice()
    {
        Rig rig;
        auto dialog = rig.openReady();
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        // the resolver reports Lithium (ticked) as needed by Sodium, plus a real extra
        rig.resolver->answer({ depOf(S("ID-lithium"), S("Lithium"), S("Sodium")), depOf(S("LIB"), S("Some Lib"), S("Sodium")) });
        QCOMPARE(dialog->dependencyNames(), QStringList{ S("Some Lib") });
        QVERIFY(dialog->summaryText().startsWith(S("Adds 6 mods (+1 they need)")));
    }

    void dependencyFailureKeepsApplyDisabledUntilRetry()
    {
        Rig rig;
        auto dialog = rig.openReady();
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        rig.resolver->fail();
        QVERIFY(dialog->dependencyCheckFailed());
        QVERIFY(dialog->dependencyRetryButton()->isVisibleTo(dialog.get()));
        QVERIFY(!dialog->applyButton()->isEnabled());
        dialog->dependencyRetryButton()->click();
        QTRY_COMPARE(rig.resolver->requests.size(), 2);
        rig.resolver->answer({});
        QVERIFY(dialog->applyButton()->isEnabled());
    }

    void dependencyCheckTurnedOffAddsNothingExtra()
    {
        Rig rig;
        rig.dependencyCheck = false;
        auto dialog = rig.openReady();
        QTRY_VERIFY(!dialog->dependencyCheckPending());
        QVERIFY(rig.resolver->requests.isEmpty());
        QVERIFY(dialog->dependencyNames().isEmpty());
        QVERIFY(dialog->applyButton()->isEnabled());
    }

    // ---- memory ----

    void memoryFollowsInstalledPlusTickedMods()
    {
        Rig rig;
        rig.installed.count = 69;  // + 6 ticked = 75: one block
        auto dialog = rig.openReady();
        QCOMPARE(dialog->memoryText(), S("Give Minecraft 5 GB"));
        dialog->rowCheck(S("lithium"))->click();  // 74
        QCOMPARE(dialog->memoryText(), S("Give Minecraft 5 GB"));

        Rig more;
        more.installed.count = 70;  // + 6 = 76: a second block
        auto other = more.openReady();
        QCOMPARE(other->memoryText(), S("Give Minecraft 6 GB"));
        other->rowCheck(S("lithium"))->click();  // 75: back to one block
        QCOMPARE(other->memoryText(), S("Give Minecraft 5 GB"));
    }

    void theRecommendationNeverLowersWhatIsSet()
    {
        Rig rig;
        rig.currentMax = 8 * kGb;
        auto dialog = rig.openReady();
        QCOMPARE(dialog->memoryText(), S("Memory is fine (8 GB)"));
        QVERIFY(dialog->memoryNowText().isEmpty());
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        rig.resolver->answer({});
        QCOMPARE(dialog->summaryText(), S("Adds 6 mods"));
        dialog->applyButton()->click();
        QVERIFY(rig.memoryWrites.isEmpty());
        QCOMPARE(rig.installs.size(), 1);
    }

    void aSmallPcGetsAtMostHalfOfItsRam()
    {
        Rig rig;
        rig.ramMb = 4 * kGb;
        rig.currentMax = 1 * kGb;
        auto dialog = rig.openReady();
        QCOMPARE(dialog->ramLineText(), S("Your PC has 4 GB of RAM."));
        QCOMPARE(dialog->memoryText(), S("Give Minecraft 2 GB"));
        QCOMPARE(dialog->memoryNowText(), S("now 1 GB"));
    }

    void anUnknownRamHidesTheMemorySection()
    {
        Rig rig;
        rig.ramMb = 0;
        auto dialog = rig.openReady();
        QVERIFY(!dialog->memorySectionVisible());
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        rig.resolver->answer({});
        QCOMPARE(dialog->summaryText(), S("Adds 6 mods"));
        dialog->applyButton()->click();
        QVERIFY(rig.memoryWrites.isEmpty());
    }

    void turningTheMemoryToggleOffKeepsItOutOfTheSummaryAndTheWrite()
    {
        Rig rig;
        auto dialog = rig.openReady();
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        rig.resolver->answer({});
        QVERIFY(dialog->memoryBox()->isChecked());
        dialog->memoryBox()->click();
        QVERIFY(!dialog->memoryBox()->isChecked());
        QCOMPARE(dialog->summaryText(), S("Adds 6 mods"));
        dialog->applyButton()->click();
        QVERIFY(rig.memoryWrites.isEmpty());
        QCOMPARE(rig.installs.size(), 1);
        QVERIFY(!dialog->outcome().memoryWritten);
    }

    void memoryOnlyApplyWhenEveryModIsUnticked()
    {
        Rig rig;
        auto dialog = rig.openReady();
        for (const QString& slug : dialog->rowSlugs()) {
            dialog->rowCheck(slug)->click();
        }
        QVERIFY(dialog->tickedSlugs().isEmpty());
        QVERIFY(!dialog->dependencyCheckPending());
        QCOMPARE(dialog->memoryText(), S("Give Minecraft 4 GB"));  // modded, 0 mods
        QCOMPARE(dialog->summaryText(), S("Memory 2 ") + kArrow + S(" 4 GB"));
        QVERIFY(dialog->applyButton()->isEnabled());
        dialog->applyButton()->click();
        QVERIFY(rig.installs.isEmpty());
        QCOMPARE(rig.memoryWrites.first(), qMakePair(4 * kGb, 1 * kGb));
    }

    void memoryStillWorksWhenModrinthFailed()
    {
        Rig rig;
        auto dialog = rig.open();
        rig.catalog->fail();
        QCOMPARE(dialog->memoryText(), S("Give Minecraft 4 GB"));
        QVERIFY(dialog->applyButton()->isEnabled());
        dialog->applyButton()->click();
        QCOMPARE(rig.memoryWrites.size(), 1);
        QVERIFY(rig.installs.isEmpty());
    }

    // ---- Java ----

    void theJavaLineAppearsOnlyForAProblemAndFixClosesTheDialog()
    {
        Rig fine;
        auto noProblem = fine.openReady();
        QVERIFY(!noProblem->javaSectionVisible());

        Rig rig;
        rig.javaProblem = S("Java 17 is set; this version needs Java 21.");
        auto dialog = rig.openReady();
        QVERIFY(dialog->javaSectionVisible());
        QCOMPARE(dialog->javaText(), S("Java 17 is set; this version needs Java 21."));
        QVERIFY(!dialog->javaFixRequested());
        dialog->javaFixButton()->click();
        QVERIFY(dialog->javaFixRequested());
        QCOMPARE(dialog->result(), int(QDialog::Rejected));
        QVERIFY(rig.installs.isEmpty());
        QVERIFY(rig.memoryWrites.isEmpty());
    }

    // ---- Apply ----

    void applyInstallsTheTickedModsAndTheDependenciesThenWritesTheMemory()
    {
        Rig rig;
        auto dialog = rig.openReady();
        dialog->rowCheck(S("immediatelyfast"))->click();  // five ticked (before the first run started: it is one run)
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        QCOMPARE(rig.resolver->requests.last().second.all().size(), 5);
        rig.resolver->answer({ depOf(S("FAPI"), S("Fabric API"), S("Sodium")) });
        QCOMPARE(dialog->summaryText(), S("Adds 5 mods (+1 they need)") + kDot + S("Memory 2 ") + kArrow + S(" 5 GB"));
        dialog->applyButton()->click();

        QCOMPARE(dialog->result(), int(QDialog::Accepted));
        QCOMPARE(rig.installs.size(), 1);
        const Content::CartInstallPlan plan = rig.installs.first();
        QCOMPARE(plan.count(), 6);
        QCOMPARE(plan.dependencyCount(), 1);
        QStringList projects;
        for (const auto& download : plan.downloads) {
            QCOMPARE(download.kind, Content::Kind::Mod);
            projects << download.mod.pack->addonId.toString();
            QVERIFY(!download.mod.version.downloadUrl.isEmpty());
        }
        QCOMPARE(projects, (QStringList{ S("ID-sodium"), S("ID-lithium"), S("ID-ferrite-core"), S("ID-entityculling"), S("ID-modernfix"), S("FAPI") }));
        QCOMPARE(plan.downloads.first().mod.reason, Content::reasonStandalone());
        QCOMPARE(plan.downloads.last().mod.reason, Content::reasonDependency());
        QCOMPARE(plan.downloads.last().mod.dependentOn, S("file-of-Sodium"));

        QCOMPARE(rig.memoryWrites.size(), 1);
        QCOMPARE(rig.memoryWrites.first(), qMakePair(5 * kGb, 1 * kGb));
        QVERIFY(dialog->outcome().applied);
        QCOMPARE(dialog->outcome().modsQueued, 6);
        QVERIFY(dialog->outcome().memoryWritten);
        QVERIFY(!dialog->outcome().refusedRunning);
    }

    void aRunningInstanceIsRefused()
    {
        Rig rig;
        auto dialog = rig.openReady();
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        rig.resolver->answer({});
        rig.running = true;  // the game started while the dialog was open
        dialog->applyButton()->click();
        QVERIFY(dialog->outcome().refusedRunning);
        QVERIFY(!dialog->outcome().applied);
        QCOMPARE(dialog->result(), int(QDialog::Rejected));
        QVERIFY(rig.installs.isEmpty());
        QVERIFY(rig.memoryWrites.isEmpty());
    }

    void applyWritesTheMemoryWithoutWaitingForTheDownloads()
    {
        // The seam only queues the job (nothing is awaited): the memory is written and the dialog closes whatever the downloads do later.
        Rig rig;
        auto dialog = rig.openReady();
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        rig.resolver->answer({});
        dialog->applyButton()->click();
        QCOMPARE(rig.installs.size(), 1);
        QCOMPARE(rig.memoryWrites.size(), 1);
        QCOMPARE(dialog->result(), int(QDialog::Accepted));
        QVERIFY(dialog->outcome().applied);
        QVERIFY(dialog->outcome().modsQueued > 0);
    }

    void applyWithoutModsStillWritesTheMemory()
    {
        Rig rig;
        auto dialog = rig.openReady();
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        rig.resolver->answer({});
        for (const QString& slug : dialog->rowSlugs()) {
            if (auto* check = dialog->rowCheck(slug); check && check->isEnabled() && check->isChecked())
                check->click();
        }
        QTRY_VERIFY(dialog->applyButton()->isEnabled());  // the memory is still worth applying
        dialog->applyButton()->click();
        QVERIFY(rig.installs.isEmpty());
        QCOMPARE(rig.memoryWrites.size(), 1);
        QCOMPARE(dialog->outcome().modsQueued, 0);
        QVERIFY(dialog->outcome().applied);
    }

    void cancelChangesNothing()
    {
        Rig rig;
        auto dialog = rig.openReady();
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        rig.resolver->answer({});
        dialog->cancelButton()->click();
        QCOMPARE(dialog->result(), int(QDialog::Rejected));
        QVERIFY(rig.installs.isEmpty());
        QVERIFY(rig.memoryWrites.isEmpty());
        QVERIFY(!dialog->outcome().applied);
    }

    void applyIsRefusedWhileTheCheckIsRunning()
    {
        Rig rig;
        auto dialog = rig.openReady();
        QVERIFY(dialog->dependencyCheckPending());
        dialog->accept();  // Enter while "Checking…"
        QVERIFY(rig.installs.isEmpty());
        QVERIFY(dialog->result() != int(QDialog::Accepted));
    }

    // ---- Java arguments ----

    void javaArgumentsAreHiddenWhenTheJavaIsUnknownOrNotSupported()
    {
        Rig unknown;
        auto a = unknown.openReady();
        QVERIFY(!a->jvmArgsSectionVisible());
        QCOMPARE(a->jvmArgsState(), JvmArgsState::NotSupported);
        QVERIFY(!a->summaryText().contains(S("Java arguments")));

        Rig single;
        single.javaMajor = 21;
        single.cores = 1;
        auto b = single.openReady();
        QVERIFY(!b->jvmArgsSectionVisible());
    }

    void noCustomArgumentsOffersATickedRowWithTheExplanations()
    {
        Rig rig;
        rig.javaMajor = 21;
        auto dialog = rig.openReady();
        QVERIFY(dialog->jvmArgsSectionVisible());
        QCOMPARE(dialog->jvmArgsState(), JvmArgsState::NoCustomArgs);
        QCOMPARE(dialog->jvmArgsBoxText(), S("Use recommended Java arguments"));
        QVERIFY(dialog->jvmArgsBox()->isChecked());
        QVERIFY(dialog->jvmArgsCurrentText().isEmpty());
        QVERIFY(dialog->jvmArgsFineText().isEmpty());
        QVERIFY(!dialog->jvmArgsDetailsVisible());
        dialog->jvmArgsDetailsButton()->click();
        QVERIFY(dialog->jvmArgsDetailsVisible());
        QCOMPARE(dialog->jvmArgsDetailsButton()->text(), S("Hide details"));
        const QStringList lines = dialog->jvmArgsExplanations();
        QCOMPARE(lines.size(), 17);
        QVERIFY(lines.first().startsWith(S("-XX:+UseG1GC ")));
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        rig.resolver->answer({});  // the summary says "Checking…" until the dependencies are in
        QVERIFY(dialog->summaryText().endsWith(kDot + S("Java arguments tuned")));
        QVERIFY(dialog->applyButton()->isEnabled());
    }

    void javaArgumentsAloneEnableApplyAndUnticking()
    {
        Rig rig;
        rig.target = vanillaTarget();
        rig.ramMb = 16 * kGb;
        rig.currentMax = 8 * kGb;  // memory is fine
        rig.javaMajor = 17;
        auto dialog = rig.open();
        QCOMPARE(dialog->summaryText(), S("Java arguments tuned"));
        QVERIFY(dialog->applyButton()->isEnabled());
        dialog->jvmArgsBox()->click();
        QVERIFY(!dialog->jvmArgsBox()->isChecked());
        QCOMPARE(dialog->summaryText(), S("Nothing to change"));
        QVERIFY(!dialog->applyButton()->isEnabled());
        QVERIFY(!dialog->jvmArgsToWrite().has_value());
    }

    void customArgumentsAreAnUntickedReplaceRowWithTheCurrentOnesElided()
    {
        Rig rig;
        rig.javaMajor = 21;
        rig.jvmArgs = S("-XX:+UseZGC -Dfoo=bar");
        auto dialog = rig.openReady();
        QCOMPARE(dialog->jvmArgsState(), JvmArgsState::HasCustomArgs);
        QCOMPARE(dialog->jvmArgsBoxText(), S("Replace your Java arguments with the recommended ones"));
        QVERIFY(!dialog->jvmArgsBox()->isChecked());
        QCOMPARE(dialog->jvmArgsCurrentText(), S("Currently: -XX:+UseZGC -Dfoo=bar"));
        QCOMPARE(dialog->jvmArgsCurrentToolTip(), S("-XX:+UseZGC -Dfoo=bar"));
        QVERIFY(!dialog->summaryText().contains(S("Java arguments")));
        QVERIFY(!dialog->jvmArgsToWrite().has_value());

        // A very long own set is elided, the whole is in the tooltip.
        Rig longRig;
        longRig.javaMajor = 21;
        longRig.jvmArgs = QString(S("-Dsome.very.long.property.name=value ")).repeated(30).trimmed();
        auto longDialog = longRig.openReady();
        QVERIFY(longDialog->jvmArgsCurrentText().endsWith(QChar(0x2026)));
        QCOMPARE(longDialog->jvmArgsCurrentToolTip(), longRig.jvmArgs);
    }

    void alreadyTunedShowsACalmLineAndNothingToApply()
    {
        Rig rig;
        rig.target = vanillaTarget();
        rig.currentMax = 8 * kGb;
        rig.javaMajor = 21;
        rig.jvmArgs = adviseJvmArgs(21, 8 * kGb, 8, QString()).args().join(S("   "));
        auto dialog = rig.open();
        QCOMPARE(dialog->jvmArgsState(), JvmArgsState::AlreadyRecommended);
        QVERIFY(dialog->jvmArgsSectionVisible());
        QCOMPARE(dialog->jvmArgsFineText(), S("Java arguments are already tuned."));
        QVERIFY(dialog->jvmArgsBoxText().isEmpty());
        QCOMPARE(dialog->summaryText(), S("Nothing to change"));
        QVERIFY(!dialog->applyButton()->isEnabled());
    }

    void theMemoryChangeDecidesTheTier()
    {
        Rig rig;
        rig.ramMb = 32 * kGb;
        rig.currentMax = 2 * kGb;
        rig.javaMajor = 21;
        auto dialog = rig.openReady();
        // Fabric with five mods on 32 GB: the memory section proposes 5 GB -> the small tier.
        QVERIFY(dialog->jvmArgsExplanations().join(QLatin1Char(' ')).contains(S("G1HeapRegionSize=8M")));
        // The heap is already 16 GB: the roomier tier.
        Rig big;
        big.ramMb = 32 * kGb;
        big.currentMax = 16 * kGb;
        big.javaMajor = 21;
        auto bigDialog = big.openReady();
        QVERIFY(bigDialog->jvmArgsExplanations().join(QLatin1Char(' ')).contains(S("G1HeapRegionSize=16M")));
    }

    void applyWritesTheArgumentsAndHandsBackThePreviousState()
    {
        Rig rig;
        rig.javaMajor = 21;
        rig.jvmArgs = S("-Dold=1");
        rig.jvmPrevious = { true, S("-Dold=1") };
        auto dialog = rig.openReady();
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        rig.resolver->answer({});  // the summary says "Checking…" until the dependencies are in
        dialog->jvmArgsBox()->click();  // replace
        QVERIFY(dialog->jvmArgsBox()->isChecked());
        QVERIFY(dialog->summaryText().contains(S("Java arguments tuned")));
        for (const QString& slug : dialog->rowSlugs())
            if (dialog->rowCheck(slug)->isChecked())
                dialog->rowCheck(slug)->click();
        QVERIFY(dialog->tickedSlugs().isEmpty());

        dialog->applyButton()->click();

        QCOMPARE(dialog->result(), int(QDialog::Accepted));
        QCOMPARE(rig.jvmWrites.size(), 1);
        QCOMPARE(rig.jvmWrites.first(), adviseJvmArgs(21, 5 * kGb, 8, QString()).argsString());
        QVERIFY(!rig.jvmWrites.first().contains(S("-Xmx")));
        QVERIFY(dialog->outcome().javaArgsWritten);
        QVERIFY(dialog->outcome().applied);
        QVERIFY(dialog->outcome().previousJvmArgs.overridden);
        QCOMPARE(dialog->outcome().previousJvmArgs.args, S("-Dold=1"));
    }

    void anUntickedJavaRowWritesNothing()
    {
        Rig rig;
        rig.javaMajor = 21;
        rig.jvmArgs = S("-Dold=1");
        auto dialog = rig.openReady();
        QVERIFY(!dialog->jvmArgsBox()->isChecked());
        dialog->applyButton()->click();
        QVERIFY(rig.jvmWrites.isEmpty());
        QVERIFY(!dialog->outcome().javaArgsWritten);
    }

    void javaArgumentsAreWrittenAtApplyWithoutWaitingForTheDownloads()
    {
        Rig rig;
        rig.javaMajor = 21;
        auto dialog = rig.openReady();
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        rig.resolver->answer({});
        dialog->applyButton()->click();
        QCOMPARE(rig.installs.size(), 1);
        QCOMPARE(rig.jvmWrites.size(), 1);
        QVERIFY(dialog->outcome().javaArgsWritten);
        QCOMPARE(dialog->result(), int(QDialog::Accepted));
    }

    void closingTheDialogCancelsTheRuns()
    {
        Rig rig;
        auto dialog = rig.openReady();
        QTRY_COMPARE(rig.resolver->requests.size(), 1);  // a run is in flight
        const int resolverCancels = g_resolverCancels;
        const int catalogCancels = g_catalogCancels;
        dialog.reset();
        QVERIFY2(g_resolverCancels > resolverCancels, "the dependency run is cancelled with the dialog");
        QVERIFY2(g_catalogCancels > catalogCancels, "the Modrinth lookup is cancelled with the dialog");
    }

    void everythingIsSettledOnceTheAnswersAreIn()
    {
        Rig rig;
        auto dialog = rig.open();
        QVERIFY(!dialog->isSettled());
        rig.catalog->answer(everything(Loader::Fabric, S("1.21.1"), rig.target.loaders));
        QVERIFY(!dialog->isSettled());  // the dependency check is on its way
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        rig.resolver->answer({});
        QVERIFY(dialog->isSettled());
    }
};

QTEST_MAIN(ZeroPerformanceDialogTest)

#include "ZeroPerformanceDialog_test.moc"
