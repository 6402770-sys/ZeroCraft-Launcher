// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the pure core of "Boost performance" (launcher/zero/perf/PerformanceProfile): the curated table per loader, the
// NeoForge Sodium -> Embeddium fallback, every row state, the memory rule on 4 / 8 / 16 / 32 GB PCs with 0 / 50 / 200 mods, vanilla and modded,
// that it never lowers what is set, the Java line and the summary texts. No widgets, no network.
#include <QTest>

#include "zero/content/ModpackSource.h"
#include "zero/perf/PerformanceProfile.h"

using namespace Zero::Perf;

namespace {

QString S(const char* text)
{
    return QString::fromUtf8(text);
}

constexpr int kGb = 1024;

ModPlatform::IndexedVersion version(const QString& id, const QStringList& mc, ModPlatform::ModLoaderTypes loaders,
                                    ModPlatform::IndexedVersionTypeValue type = ModPlatform::IndexedVersionTypeValue::Release)
{
    ModPlatform::IndexedVersion v;
    v.fileId = id;
    v.version = id;
    v.versionNumber = id;
    v.versionType = ModPlatform::IndexedVersionType(type);
    v.mcVersion = mc;
    v.loaders = loaders;
    v.downloadUrl = S("https://cdn.test/%1.jar").arg(id);
    v.fileName = id + S(".jar");
    return v;
}

/// One catalog answer: the project with a Modrinth id and the given versions.
Found found(const QString& slug, const QList<ModPlatform::IndexedVersion>& versions)
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

/// Everything the loader's list needs, with a fitting version each.
FoundMap everything(Loader loader, const QString& mc, ModPlatform::ModLoaderTypes loaders)
{
    FoundMap map;
    for (const QString& slug : slugsFor(loader)) {
        map.insert(slug, found(slug, { version(slug + S("-1"), { mc }, loaders) }));
    }
    return map;
}

const ModPlatform::ModLoaderTypes kFabric{ ModPlatform::Fabric };
const ModPlatform::ModLoaderTypes kNeo{ ModPlatform::NeoForge };
const ModPlatform::ModLoaderTypes kForge{ ModPlatform::Forge };

const Row* rowOf(const QList<Row>& rows, const QString& slug)
{
    for (const Row& row : rows) {
        if (row.slug == slug) {
            return &row;
        }
    }
    return nullptr;
}

QStringList slugsOf(const QList<Entry>& entries)
{
    QStringList out;
    for (const Entry& entry : entries) {
        out << entry.slug;
    }
    return out;
}

}  // namespace

class PerformanceProfileTest : public QObject {
    Q_OBJECT

   private slots:
    // ---- loaders and the table ----

    void loaderIsChosenByPriority()
    {
        QCOMPARE(loaderOf(kFabric), Loader::Fabric);
        QCOMPARE(loaderOf(kForge), Loader::Forge);
        QCOMPARE(loaderOf(kNeo), Loader::NeoForge);
        QCOMPARE(loaderOf(ModPlatform::ModLoaderTypes(ModPlatform::Quilt)), Loader::Quilt);
        // Prism reports Fabric next to Quilt: Quilt wins, its list is Fabric's anyway
        QCOMPARE(loaderOf(ModPlatform::ModLoaderTypes(ModPlatform::Quilt | ModPlatform::Fabric)), Loader::Quilt);
        QCOMPARE(loaderOf(ModPlatform::ModLoaderTypes()), Loader::None);
        QCOMPARE(loaderOf(ModPlatform::ModLoaderTypes(ModPlatform::LiteLoader)), Loader::None);
        QCOMPARE(loaderName(Loader::NeoForge), S("NeoForge"));
        QVERIFY(loaderName(Loader::None).isEmpty());
    }

    void tableFabricAndQuilt()
    {
        const QStringList expected = { S("sodium"), S("lithium"), S("ferrite-core"), S("immediatelyfast"), S("entityculling"), S("modernfix") };
        QCOMPARE(slugsOf(curatedFor(Loader::Fabric)), expected);
        QCOMPARE(slugsOf(curatedFor(Loader::Quilt)), expected);
        QCOMPARE(slugsFor(Loader::Fabric), expected);
    }

    void tableNeoForgeHasTheSodiumFallback()
    {
        const auto list = curatedFor(Loader::NeoForge);
        QCOMPARE(slugsOf(list), (QStringList{ S("sodium"), S("ferrite-core"), S("modernfix"), S("entityculling"), S("immediatelyfast") }));
        QCOMPARE(list.first().fallbackSlug, S("embeddium"));
        // the catalog asks for the fallback as well, once
        QCOMPARE(slugsFor(Loader::NeoForge), (QStringList{ S("sodium"), S("embeddium"), S("ferrite-core"), S("modernfix"), S("entityculling"),
                                                           S("immediatelyfast") }));
    }

    void tableForge()
    {
        QCOMPARE(slugsOf(curatedFor(Loader::Forge)), (QStringList{ S("embeddium"), S("ferrite-core"), S("modernfix"), S("entityculling") }));
        QVERIFY(curatedFor(Loader::None).isEmpty());
        QVERIFY(slugsFor(Loader::None).isEmpty());
    }

    void everySlugOfTheTableHasAProject()
    {
        for (Loader loader : { Loader::Fabric, Loader::Quilt, Loader::NeoForge, Loader::Forge }) {
            for (const QString& slug : slugsFor(loader)) {
                const Project project = projectFor(slug);
                QCOMPARE(project.slug, slug);
                QVERIFY2(!project.name.isEmpty() && !project.benefit.isEmpty() && !project.modIds.isEmpty(), qPrintable(slug));
            }
        }
        QVERIFY(projectFor(S("nope")).slug.isEmpty());
        QVERIFY(projectFor(S("sodium")).rendering);
        QVERIFY(projectFor(S("embeddium")).rendering);
        QVERIFY(!projectFor(S("lithium")).rendering);
        QCOMPARE(projectFor(S("sodium")).alternativeSlug, S("embeddium"));
        QCOMPARE(projectFor(S("embeddium")).alternativeSlug, S("sodium"));
    }

    // ---- rows ----

    void everythingOfferedOnAFreshFabricInstance()
    {
        const auto rows = buildRows(Loader::Fabric, S("1.21.1"), kFabric, everything(Loader::Fabric, S("1.21.1"), kFabric), {});
        QCOMPARE(rows.size(), 6);
        for (const Row& row : rows) {
            QCOMPARE(row.state, RowState::Offered);
            QVERIFY(row.tickedByDefault());
            QVERIFY(row.canTick());
            QVERIFY(row.pack);
            QVERIFY(!row.version.downloadUrl.isEmpty());
            QVERIFY(rowStateText(row, S("1.21.1")).isEmpty());
        }
        QCOMPARE(rows.first().name, S("Sodium"));
        QCOMPARE(rows.first().pack->addonId.toString(), S("ID-sodium"));
    }

    void installedByProjectIdFromMetadata()
    {
        InstalledSnapshot installed;
        installed.withMetadata = { { Zero::Content::managedKey(S("modrinth"), S("ID-lithium")), S("f"), S("Lithium"), S("1.0") } };
        const auto rows = buildRows(Loader::Fabric, S("1.21.1"), kFabric, everything(Loader::Fabric, S("1.21.1"), kFabric), installed);
        const Row* lithium = rowOf(rows, S("lithium"));
        QVERIFY(lithium);
        QCOMPARE(lithium->state, RowState::Installed);
        QVERIFY(!lithium->canTick());
        QVERIFY(!lithium->tickedByDefault());
        QCOMPARE(rowStateText(*lithium, S("1.21.1")), S("Installed"));
        QCOMPARE(rowOf(rows, S("sodium"))->state, RowState::Offered);
    }

    void installedByModIdWhenThereIsNoMetadata()
    {
        InstalledSnapshot installed;
        installed.modIds = { S("FerriteCore"), S("modernfix") };  // any case
        const auto rows = buildRows(Loader::Fabric, S("1.21.1"), kFabric, everything(Loader::Fabric, S("1.21.1"), kFabric), installed);
        QCOMPARE(rowOf(rows, S("ferrite-core"))->state, RowState::Installed);
        QCOMPARE(rowOf(rows, S("modernfix"))->state, RowState::Installed);
        QCOMPARE(rowOf(rows, S("lithium"))->state, RowState::Offered);
    }

    void aMetadataEntryOfAnotherProjectIsNotAMatch()
    {
        InstalledSnapshot installed;
        installed.withMetadata = { { Zero::Content::managedKey(S("modrinth"), S("SOMETHING-ELSE")), S("f"), S("Other"), S("1") },
                                   { Zero::Content::managedKey(S("flame"), S("ID-lithium")), S("f"), S("Lithium"), S("1") } };  // another provider
        const auto rows = buildRows(Loader::Fabric, S("1.21.1"), kFabric, everything(Loader::Fabric, S("1.21.1"), kFabric), installed);
        QCOMPARE(rowOf(rows, S("lithium"))->state, RowState::Offered);
    }

    void unavailableWhenThereIsNoFileForTheVersionOrLoader()
    {
        FoundMap map = everything(Loader::Fabric, S("1.21.1"), kFabric);
        map.insert(S("lithium"), found(S("lithium"), { version(S("old"), { S("1.20.1") }, kFabric) }));          // another Minecraft version
        map.insert(S("modernfix"), found(S("modernfix"), { version(S("forge"), { S("1.21.1") }, kForge) }));       // another loader
        map.insert(S("entityculling"), found(S("entityculling"), {}));                                              // no version at all
        map.remove(S("immediatelyfast"));                                                                           // the catalog knows nothing of it
        const auto rows = buildRows(Loader::Fabric, S("1.21.1"), kFabric, map, {});
        for (const QString& slug : { S("lithium"), S("modernfix"), S("entityculling"), S("immediatelyfast") }) {
            const Row* row = rowOf(rows, slug);
            QVERIFY2(row, qPrintable(slug));
            QCOMPARE(row->state, RowState::Unavailable);
            QVERIFY(!row->canTick());
            QCOMPARE(rowStateText(*row, S("1.21.1")), S("Not available for 1.21.1"));
        }
        QCOMPARE(rowOf(rows, S("sodium"))->state, RowState::Offered);
        QCOMPARE(rowStateText(*rowOf(rows, S("lithium")), QString()), S("Not available"));
    }

    void releasePreferredOverNewerBeta()
    {
        FoundMap map = everything(Loader::Fabric, S("1.21.1"), kFabric);
        map.insert(S("lithium"), found(S("lithium"), { version(S("beta"), { S("1.21.1") }, kFabric, ModPlatform::IndexedVersionTypeValue::Beta),
                                                       version(S("release"), { S("1.21.1") }, kFabric) }));
        const auto rows = buildRows(Loader::Fabric, S("1.21.1"), kFabric, map, {});
        QCOMPARE(rowOf(rows, S("lithium"))->version.fileId.toString(), S("release"));
    }

    void neoForgeUsesSodiumWhenItHasAFile()
    {
        const auto rows = buildRows(Loader::NeoForge, S("1.21.1"), kNeo, everything(Loader::NeoForge, S("1.21.1"), kNeo), {});
        QCOMPARE(rows.size(), 5);
        const Row& first = rows.first();
        QCOMPARE(first.slug, S("sodium"));
        QCOMPARE(first.name, S("Sodium"));
        QCOMPARE(first.state, RowState::Offered);
        QCOMPARE(first.pack->addonId.toString(), S("ID-sodium"));
    }

    void neoForgeFallsBackToEmbeddiumWhenSodiumHasNoNeoForgeFile()
    {
        FoundMap map = everything(Loader::NeoForge, S("1.20.1"), kNeo);
        map.insert(S("sodium"), found(S("sodium"), { version(S("fabric-only"), { S("1.20.1") }, kFabric) }));  // no NeoForge build
        const auto rows = buildRows(Loader::NeoForge, S("1.20.1"), kNeo, map, {});
        const Row& first = rows.first();
        QCOMPARE(first.slug, S("embeddium"));
        QCOMPARE(first.name, S("Embeddium"));
        QCOMPARE(first.state, RowState::Offered);
        QCOMPARE(first.pack->addonId.toString(), S("ID-embeddium"));
        QCOMPARE(first.version.fileId.toString(), S("embeddium-1"));
    }

    void neoForgeSodiumRowIsUnavailableWhenNeitherHasAFile()
    {
        FoundMap map = everything(Loader::NeoForge, S("1.21.1"), kNeo);
        map.insert(S("sodium"), found(S("sodium"), {}));
        map.insert(S("embeddium"), found(S("embeddium"), {}));
        const auto rows = buildRows(Loader::NeoForge, S("1.21.1"), kNeo, map, {});
        QCOMPARE(rows.first().slug, S("sodium"));
        QCOMPARE(rows.first().state, RowState::Unavailable);
    }

    void embeddiumInstalledMakesTheSodiumRowAnAlternative()
    {
        InstalledSnapshot installed;
        installed.modIds = { S("embeddium") };
        const auto rows = buildRows(Loader::NeoForge, S("1.21.1"), kNeo, everything(Loader::NeoForge, S("1.21.1"), kNeo), installed);
        const Row& first = rows.first();
        QCOMPARE(first.slug, S("sodium"));
        QCOMPARE(first.state, RowState::InstalledAlternative);
        QCOMPARE(first.alternativeName, S("Embeddium"));
        QCOMPARE(rowStateText(first, S("1.21.1")), S("Installed (Embeddium)"));
        QVERIFY(!first.canTick());
    }

    void embeddiumInstalledOnFabricToo()
    {
        InstalledSnapshot installed;
        installed.withMetadata = { { Zero::Content::managedKey(S("modrinth"), S("sk9rgfiA")), S("f"), S("Embeddium"), S("1") } };
        installed.modIds = { S("embeddium") };
        const auto rows = buildRows(Loader::Fabric, S("1.21.1"), kFabric, everything(Loader::Fabric, S("1.21.1"), kFabric), installed);
        QCOMPARE(rowOf(rows, S("sodium"))->state, RowState::InstalledAlternative);
    }

    void sodiumInstalledMakesTheForgeEmbeddiumRowAnAlternative()
    {
        InstalledSnapshot installed;
        installed.modIds = { S("sodium") };
        const auto rows = buildRows(Loader::Forge, S("1.20.1"), kForge, everything(Loader::Forge, S("1.20.1"), kForge), installed);
        const Row* embeddium = rowOf(rows, S("embeddium"));
        QVERIFY(embeddium);
        QCOMPARE(embeddium->state, RowState::InstalledAlternative);
        QCOMPARE(embeddium->alternativeName, S("Sodium"));
        QCOMPARE(rowStateText(*embeddium, S("1.20.1")), S("Installed (Sodium)"));
    }

    void embeddiumInstalledOnForgeIsInstalled()
    {
        InstalledSnapshot installed;
        installed.modIds = { S("embeddium") };
        const auto rows = buildRows(Loader::Forge, S("1.20.1"), kForge, everything(Loader::Forge, S("1.20.1"), kForge), installed);
        QCOMPARE(rowOf(rows, S("embeddium"))->state, RowState::Installed);
    }

    void optiFineMakesTheRenderersConflict()
    {
        for (const QString& hint : { S("optifine"), S("OptiFine_1.21.1_HD_U_J1.jar"), S("OptiFabric") }) {
            InstalledSnapshot installed;
            (hint.contains(S("jar")) ? installed.names : installed.modIds) << hint;
            QVERIFY2(hasOptiFine(installed), qPrintable(hint));
            const auto rows = buildRows(Loader::Fabric, S("1.21.1"), kFabric, everything(Loader::Fabric, S("1.21.1"), kFabric), installed);
            const Row* sodium = rowOf(rows, S("sodium"));
            QVERIFY(sodium);
            QCOMPARE(sodium->state, RowState::ConflictOptiFine);
            QVERIFY(!sodium->tickedByDefault());
            QVERIFY(!sodium->canTick());
            QCOMPARE(rowStateText(*sodium, S("1.21.1")), S("Conflicts with OptiFine"));
            // the others are not renderers
            QCOMPARE(rowOf(rows, S("lithium"))->state, RowState::Offered);
            QCOMPARE(rowOf(rows, S("modernfix"))->state, RowState::Offered);
        }
        QVERIFY(!hasOptiFine({}));
    }

    void optiFineConflictAlsoHitsEmbeddium()
    {
        InstalledSnapshot installed;
        installed.modIds = { S("optifine") };
        const auto rows = buildRows(Loader::Forge, S("1.20.1"), kForge, everything(Loader::Forge, S("1.20.1"), kForge), installed);
        QCOMPARE(rowOf(rows, S("embeddium"))->state, RowState::ConflictOptiFine);
        QCOMPARE(rowOf(rows, S("ferrite-core"))->state, RowState::Offered);
    }

    void installedWinsOverTheOptiFineConflict()
    {
        InstalledSnapshot installed;
        installed.modIds = { S("optifine"), S("sodium") };
        const auto rows = buildRows(Loader::Fabric, S("1.21.1"), kFabric, everything(Loader::Fabric, S("1.21.1"), kFabric), installed);
        QCOMPARE(rowOf(rows, S("sodium"))->state, RowState::Installed);
    }

    void unavailableWinsOverTheOptiFineConflict()
    {
        FoundMap map = everything(Loader::Fabric, S("1.21.1"), kFabric);
        map.insert(S("sodium"), found(S("sodium"), {}));
        InstalledSnapshot installed;
        installed.modIds = { S("optifine") };
        const auto rows = buildRows(Loader::Fabric, S("1.21.1"), kFabric, map, installed);
        QCOMPARE(rowOf(rows, S("sodium"))->state, RowState::Unavailable);
    }

    void vanillaHasNoRows()
    {
        QVERIFY(buildRows(Loader::None, S("1.21.1"), ModPlatform::ModLoaderTypes(), {}, {}).isEmpty());
    }

    // ---- memory ----

    void memoryRuleOnFourEightSixteenThirtyTwoGbPcs()
    {
        struct Case {
            int ramGb;
            int mods;
            bool modded;
            int expectedGb;
        };
        const QList<Case> cases = {
            // 4 GB PC: half = 2 GB caps everything
            { 4, 0, false, 2 }, { 4, 0, true, 2 }, { 4, 50, true, 2 }, { 4, 200, true, 2 },
            // 8 GB PC: half = 4 GB
            { 8, 0, false, 3 }, { 8, 0, true, 4 }, { 8, 50, true, 4 }, { 8, 200, true, 4 },
            // 16 GB PC: half = 8 GB (the cap too)
            { 16, 0, false, 3 }, { 16, 0, true, 4 }, { 16, 50, true, 5 }, { 16, 200, true, 7 },
            // 32 GB PC: the 8 GB cap
            { 32, 0, false, 3 }, { 32, 0, true, 4 }, { 32, 50, true, 5 }, { 32, 200, true, 7 },
        };
        for (const Case& c : cases) {
            const int got = recommendedMaxMb(c.ramGb * kGb, c.mods, c.modded);
            QVERIFY2(got == c.expectedGb * kGb, qPrintable(QStringLiteral("%1 GB PC, %2 mods, %3: got %4 MB, expected %5 GB")
                                                                .arg(c.ramGb).arg(c.mods).arg(c.modded ? S("modded") : S("vanilla")).arg(got).arg(c.expectedGb)));
        }
    }

    void memoryRuleStartedBlocksOfSeventyFiveMods()
    {
        const int ram = 64 * kGb;
        QCOMPARE(recommendedMaxMb(ram, 0, true), 4 * kGb);
        QCOMPARE(recommendedMaxMb(ram, 1, true), 5 * kGb);    // a started block counts
        QCOMPARE(recommendedMaxMb(ram, 75, true), 5 * kGb);
        QCOMPARE(recommendedMaxMb(ram, 76, true), 6 * kGb);
        QCOMPARE(recommendedMaxMb(ram, 150, true), 6 * kGb);
        QCOMPARE(recommendedMaxMb(ram, 151, true), 7 * kGb);
        QCOMPARE(recommendedMaxMb(ram, 226, true), 8 * kGb);
        QCOMPARE(recommendedMaxMb(ram, 5000, true), 8 * kGb);  // the cap
        QCOMPARE(recommendedMaxMb(ram, -3, true), 4 * kGb);    // nonsense counts as none
    }

    void memoryRuleNeverAboveHalfTheRamInWholeGb()
    {
        QCOMPARE(recommendedMaxMb(6 * kGb, 200, true), 3 * kGb);
        QCOMPARE(recommendedMaxMb(7 * kGb, 200, true), 3 * kGb);   // half = 3.5 GB -> 3 GB
        QCOMPARE(recommendedMaxMb(15 * kGb + 800, 200, true), 7 * kGb);  // a PC that reports 15.8 GB: half = 7.9 GB -> 7 GB
        QCOMPARE(recommendedMaxMb(16 * kGb - 100, 200, true), 7 * kGb);
    }

    void memoryRuleOnTinyPcs()
    {
        QCOMPARE(recommendedMaxMb(4 * kGb, 0, false), 2 * kGb);  // half is exactly 2 GB
        QCOMPARE(recommendedMaxMb(3 * kGb, 50, true), 1 * kGb);  // half = 1.5 GB -> 1 GB
        QCOMPARE(recommendedMaxMb(2 * kGb, 50, true), 1 * kGb);
        QCOMPARE(recommendedMaxMb(1 * kGb, 50, true), 1 * kGb);  // at least 1 GB
        QCOMPARE(recommendedMaxMb(512, 50, true), 1 * kGb);
        QCOMPARE(recommendedMaxMb(0, 50, true), 0);              // unknown RAM: no advice
        QCOMPARE(recommendedMaxMb(-1, 50, true), 0);
    }

    void minimumHeapIsOneGbOrHalfTheMax()
    {
        QCOMPARE(recommendedMinMb(6 * kGb), 1 * kGb);
        QCOMPARE(recommendedMinMb(2 * kGb), 1 * kGb);
        QCOMPARE(recommendedMinMb(1 * kGb), 512);
        QCOMPARE(recommendedMinMb(0), 0);
    }

    void adviceNeverLowers()
    {
        // current < recommended: offered
        MemoryAdvice low = adviseMemory(16 * kGb, 50, true, 2 * kGb);
        QVERIFY(low.known);
        QCOMPARE(low.recommendedMaxMb, 5 * kGb);
        QCOMPARE(low.recommendedMinMb, 1 * kGb);
        QVERIFY(low.needsChange());
        // current == recommended: fine
        QVERIFY(!adviseMemory(16 * kGb, 50, true, 5 * kGb).needsChange());
        // current > recommended: fine, and it stays what it is
        const MemoryAdvice high = adviseMemory(16 * kGb, 50, true, 6 * kGb);
        QVERIFY(!high.needsChange());
        QCOMPARE(high.currentMaxMb, 6 * kGb);
        // every combination of the rule: needsChange() is exactly "current < recommended"
        for (int ramGb : { 4, 8, 16, 32 }) {
            for (int mods : { 0, 50, 200 }) {
                for (bool modded : { false, true }) {
                    const int recommended = recommendedMaxMb(ramGb * kGb, mods, modded);
                    for (int current : { 512, 1 * kGb, 2 * kGb, 3 * kGb, 4 * kGb, 6 * kGb, 8 * kGb, 12 * kGb }) {
                        const MemoryAdvice advice = adviseMemory(ramGb * kGb, mods, modded, current);
                        QCOMPARE(advice.needsChange(), current < recommended);
                    }
                }
            }
        }
        // unknown RAM: no advice at all
        QVERIFY(!adviseMemory(0, 50, true, 2 * kGb).known);
        QVERIFY(!adviseMemory(0, 50, true, 2 * kGb).needsChange());
    }

    void currentValueIsTheInstanceOverrideElseTheGlobal()
    {
        QCOMPARE(currentMaxMb(true, 6 * kGb, 2 * kGb), 6 * kGb);
        QCOMPARE(currentMaxMb(false, 6 * kGb, 2 * kGb), 2 * kGb);
    }

    // ---- Java ----

    void javaProblemOnlyForWarningAndWillFail()
    {
        Zero::Java::InstanceJava instance;
        instance.compatibleMajors = { 21 };
        Zero::Java::Diagnosis diagnosis;
        diagnosis.effective = Zero::Java::Runtime{ S("java"), S("17.0.8"), 17, true, false };

        diagnosis.status = Zero::Java::Status::Ready;
        diagnosis.reason = Zero::Java::Reason::Ok;
        QVERIFY(!javaProblemText(instance, diagnosis));
        diagnosis.reason = Zero::Java::Reason::WillDownload;
        QVERIFY(!javaProblemText(instance, diagnosis));
        diagnosis.status = Zero::Java::Status::Unknown;
        diagnosis.reason = Zero::Java::Reason::RequirementUnknown;
        QVERIFY(!javaProblemText(instance, diagnosis));

        diagnosis.status = Zero::Java::Status::WillFail;
        diagnosis.reason = Zero::Java::Reason::IncompatibleMajor;
        QCOMPARE(*javaProblemText(instance, diagnosis), S("Java 17 is set; this version needs Java 21."));
        instance.compatibleMajors = { 17, 21 };
        QCOMPARE(*javaProblemText(instance, diagnosis), S("Java 17 is set; this version needs Java 17 or 21."));

        diagnosis.status = Zero::Java::Status::Warning;
        diagnosis.reason = Zero::Java::Reason::ConfiguredPathMissing;
        instance.overrideLocation = true;
        QCOMPARE(*javaProblemText(instance, diagnosis), S("The Java set for this instance no longer exists."));
        instance.overrideLocation = false;
        QCOMPARE(*javaProblemText(instance, diagnosis), S("The default Java does not exist."));
        diagnosis.reason = Zero::Java::Reason::NoCompatibleInstalledDownloadOff;
        QVERIFY(javaProblemText(instance, diagnosis)->contains(S("17 or 21")));
        diagnosis.reason = Zero::Java::Reason::ThirtyTwoBitMemory;
        QVERIFY(javaProblemText(instance, diagnosis)->contains(S("32-bit")));
    }

    // ---- texts ----

    void gbTexts()
    {
        QCOMPARE(gbText(6 * kGb), S("6 GB"));
        QCOMPARE(gbText(5 * kGb + 512), S("5.5 GB"));
        QCOMPARE(gbNumber(2 * kGb), S("2"));
        QCOMPARE(gbNumber(512), S("0.5"));
        QCOMPARE(ramText(16 * kGb), S("Your PC has 16 GB of RAM."));
        QCOMPARE(ramText(16 * kGb - 150), S("Your PC has 16 GB of RAM."));  // the system reports a little less
        QCOMPARE(ramText(8 * kGb), S("Your PC has 8 GB of RAM."));
        QCOMPARE(memoryGiveText(6 * kGb), S("Give Minecraft 6 GB"));
        QCOMPARE(memoryNowText(2 * kGb), S("now 2 GB"));
        QCOMPARE(memoryFineText(6 * kGb), S("Memory is fine (6 GB)"));
    }

    void titleAndLines()
    {
        QCOMPARE(dialogTitle(S("Fabric Fun")), QString::fromUtf8("Boost performance \xC2\xB7 Fabric Fun"));
        QCOMPARE(modsTitle(Loader::Fabric, S("1.21.1")), QString::fromUtf8("Mods \xC2\xB7 Fabric 1.21.1"));
        QCOMPARE(modsTitle(Loader::NeoForge, S("1.20.4")), QString::fromUtf8("Mods \xC2\xB7 NeoForge 1.20.4"));
        QCOMPARE(modsTitle(Loader::None, S("1.21.1")), QString::fromUtf8("Mods \xC2\xB7 1.21.1"));
        QCOMPARE(modsTitle(Loader::None, QString()), S("Mods"));
        QCOMPARE(vanillaModsText(), QString::fromUtf8("Performance mods need a mod loader such as Fabric. Make a Fabric copy from Library \xE2\x80\xBA Copy."));
        QCOMPARE(modpackNote(), S("This is a modpack: a pack update may remove mods you add."));
        QCOMPARE(boostedToastText(S("Fabric Fun")), S("Boosted Fabric Fun"));
        QCOMPARE(closeFirstText(S("Fabric Fun")), S("Close Fabric Fun first"));
    }

    void summaryTexts()
    {
        const QString arrow = QString::fromUtf8("\xE2\x86\x92");
        const QString dot = QString::fromUtf8(" \xC2\xB7 ");
        Summary s;
        QVERIFY(s.isEmpty());
        QCOMPARE(summaryText(s), S("Nothing to change"));

        s.mods = 4;
        s.dependencies = 1;
        s.memory = true;
        s.memoryFromMb = 2 * kGb;
        s.memoryToMb = 6 * kGb;
        QCOMPARE(summaryText(s), S("Adds 4 mods (+1 they need)") + dot + S("Memory 2 ") + arrow + S(" 6 GB"));

        s.dependencies = 0;
        QCOMPARE(summaryText(s), S("Adds 4 mods") + dot + S("Memory 2 ") + arrow + S(" 6 GB"));

        s.mods = 1;
        QCOMPARE(summaryText(s), S("Adds 1 mod") + dot + S("Memory 2 ") + arrow + S(" 6 GB"));

        s.memory = false;
        QCOMPARE(summaryText(s), S("Adds 1 mod"));
        s.dependencies = 3;
        QCOMPARE(summaryText(s), S("Adds 1 mod (+3 they need)"));

        // memory only
        Summary memoryOnly;
        memoryOnly.memory = true;
        memoryOnly.memoryFromMb = 2 * kGb + 512;
        memoryOnly.memoryToMb = 4 * kGb;
        QVERIFY(!memoryOnly.isEmpty());
        QCOMPARE(summaryText(memoryOnly), S("Memory 2.5 ") + arrow + S(" 4 GB"));

        // dependencies alone never make a change
        Summary depsOnly;
        depsOnly.dependencies = 2;
        QVERIFY(depsOnly.isEmpty());
        QCOMPARE(summaryText(depsOnly), S("Nothing to change"));
    }
};

QTEST_GUILESS_MAIN(PerformanceProfileTest)

#include "PerformanceProfile_test.moc"
