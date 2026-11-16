// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the pack rollback (zero/content/PackRollback, docs/zero/CONTENT-PACKS.md, "Pack rollback"): reading a
// latest.log (rolled back or not, the attempted list, the failing shaders), finding the pack that provides a failing shader (root,
// overlay in / out of range, a higher pack shadowing a lower one, a folder pack), the marker file, the check after a launch (stale log,
// clean load, no packs), the order a restore writes and the question before the next launch. Logs are written here; zips are built in a
// temp dir. No widgets, no network.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include "archive/ArchiveWriter.h"
#include "ui/zero/instance/PackRollbackPrompt.h"
#include "zero/content/PackOrder.h"
#include "zero/content/PackRollback.h"

using namespace Zero::Content::Packs;
using namespace Zero::Content::Packs::Rollback;

namespace {

bool writeFile(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeZip(const QString& path, const QList<std::pair<QString, QByteArray>>& entries)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    MMCZip::ArchiveWriter writer(path);
    if (!writer.open())
        return false;
    for (const auto& entry : entries)
        if (!writer.addFile(entry.first, entry.second))
            return false;
    return bool(writer.close());
}

bool setModified(const QString& path, const QDateTime& when)
{
    QFile file(path);
    return file.open(QIODevice::ReadWrite) && file.setFileTime(when, QFileDevice::FileModificationTime);
}

const QByteArray kMcmeta = R"({"pack":{"pack_format":34,"description":"x"}})";
const QByteArray kShader = "#version 330\nvoid main() {}\n";

QByteArray mcmetaWithOverlay(const QByteArray& entryJson)
{
    return R"({"pack":{"pack_format":34,"description":"x"},"overlays":{"entries":[)" + entryJson + "]}}";
}

/// A log the way the game writes it when the startup reload fails (self-written, no user data).
QString rolledBackLog()
{
    return QStringLiteral(
        "[10:00:01] [main/INFO]: Loading Minecraft\n"
        "[10:00:05] [Render thread/INFO]: Reloading ResourceManager: vanilla, vanilla, somemod, othermod, file/Alpha.zip, file/Beta 2.0.zip, "
        "continuity:glass_pane_culling_fix, a_good_place:default_animations, file/Gamma.zip\n"
        "[10:00:06] [Render thread/WARN]: Could not evaluate condition for assets/x/y.json (pack: file/Alpha.zip)\n"
        "[10:00:06] [Worker-Main-2/ERROR]: Couldn't compile pipeline (minecraft:pipeline/oit_accumulate_entity): \n"
        "com.mojang.renderpearl.util.ShaderCompileException: Couldn't parse GLSL: minecraft:core/entity:3: error: '#' : invalid directive: moj_import\n"
        "[10:00:06] [Worker-Main-3/ERROR]: Couldn't compile pipeline (minecraft:pipeline/block): \n"
        "com.mojang.renderpearl.util.ShaderCompileException: Couldn't parse GLSL: minecraft:core/entity:5: error: again\n"
        "[10:00:07] [Render thread/INFO]: Caught error loading resourcepacks, removing all selected resourcepacks\n"
        "java.util.concurrent.CompletionException: java.lang.RuntimeException: Failed to load required shader programs:\n"
        " - minecraft:pipeline/oit_accumulate_entity\n"
        "[10:00:08] [Render thread/INFO]: Reloading ResourceManager: vanilla, vanilla, somemod, othermod\n");
}

QString cleanLog()
{
    return QStringLiteral("[10:00:05] [Render thread/INFO]: Reloading ResourceManager: vanilla, somemod, file/Alpha.zip\n"
                          "[10:00:09] [Render thread/INFO]: Stopping!\n");
}

struct AskFixture {
    QTemporaryDir dir;
    Zero::PacksUi::RollbackEnv env;
    QString markerPath;
    QString optionsPath;
    QList<QString> titles;
    QList<QStringList> labels;

    bool setup(const QString& options, bool withSuspect = true)
    {
        env.instanceRoot = dir.path();
        env.gameRoot = dir.path() + "/minecraft";
        env.isRunning = []() { return false; };
        optionsPath = env.gameRoot + "/options.txt";
        markerPath = markerPathFor(env.instanceRoot);
        if (!writeFile(optionsPath, options.toUtf8()) || !writeZip(env.gameRoot + "/resourcepacks/A.zip", { { "pack.mcmeta", kMcmeta } }) ||
            !writeZip(env.gameRoot + "/resourcepacks/Bad 1.0.zip", { { "pack.mcmeta", kMcmeta } })) {
            return false;
        }
        Marker marker;
        marker.valid = true;
        marker.attempted = { "vanilla", "file/A.zip", "file/Bad 1.0.zip" };
        if (withSuspect) {
            marker.suspects = { { "Bad 1.0.zip", "minecraft:core/entity" } };
        }
        return writeMarker(markerPath, marker);
    }
    Zero::ModsUi::AskFunction answering(int index)
    {
        return [this, index](QWidget*, const Zero::ModsUi::ChoicePrompt& prompt) {
            titles.append(prompt.title);
            QStringList names;
            for (const auto& button : prompt.buttons)
                names.append(button.label);
            labels.append(names);
            return index;
        };
    }
};

}  // namespace

class PackRollbackTest : public QObject {
    Q_OBJECT
   private:
    QString root(QTemporaryDir& dir) { return dir.path(); }

   private slots:
    // ---- the log ------------------------------------------------------------------------------------------------------------
    void format_fromVersionJson()
    {
        QCOMPARE(formatFromVersionJson(R"({"pack_version":{"resource_major":97,"resource_minor":1,"data_major":121}})"), std::optional<int>(97));
        QCOMPARE(formatFromVersionJson(R"({"pack_version":{"resource":34,"data":41}})"), std::optional<int>(34));
        QCOMPARE(formatFromVersionJson(R"({"pack_version":15})"), std::optional<int>(15));
        QCOMPARE(formatFromVersionJson(R"({"id":"1.8"})"), std::optional<int>());
        QCOMPARE(formatFromVersionJson("not json"), std::optional<int>());
        QCOMPARE(formatFromClientJar(QStringLiteral("does/not/exist.jar")), std::optional<int>());
    }

    void log_rolledBack()
    {
        const LogAnalysis analysis = analyzeLog(rolledBackLog());
        QVERIFY(analysis.rolledBack);
        // bare mod ids dropped, the duplicate vanilla dropped, `ns:id` and `file/` kept in order
        QCOMPARE(analysis.attempted, (QStringList{ "vanilla", "file/Alpha.zip", "file/Beta 2.0.zip", "continuity:glass_pane_culling_fix",
                                                   "a_good_place:default_animations", "file/Gamma.zip" }));
        QCOMPARE(analysis.failingShaders, QStringList{ "minecraft:core/entity" });  // listed once; the noise line is no suspect
    }
    void log_crlf()
    {
        QString text = rolledBackLog();
        text.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
        const LogAnalysis analysis = analyzeLog(text);
        QVERIFY(analysis.rolledBack);
        QCOMPARE(analysis.attempted.last(), QStringLiteral("file/Gamma.zip"));
        QCOMPARE(analysis.failingShaders, QStringList{ "minecraft:core/entity" });
    }
    void log_noRollbackLine()
    {
        const LogAnalysis analysis = analyzeLog(cleanLog());
        QVERIFY(!analysis.rolledBack);
        QCOMPARE(analysis.attempted, (QStringList{ "vanilla", "file/Alpha.zip" }));
        QVERIFY(analysis.failingShaders.isEmpty());
        QVERIFY(!analyzeLog(QString()).rolledBack);
    }
    void log_lastReloadBeforeTheRollbackCounts()
    {
        const QString text = QStringLiteral(
            "[1] Reloading ResourceManager: vanilla, file/Old.zip\n"
            "[2] Reloading ResourceManager: vanilla, file/New.zip\n"
            "[3] Worker/ERROR: Couldn't parse GLSL: minecraft:core/terrain:1: boom\n"
            "[4] Caught error loading resourcepacks, removing all selected resourcepacks\n");
        const LogAnalysis analysis = analyzeLog(text);
        QVERIFY(analysis.rolledBack);
        QCOMPARE(analysis.attempted, (QStringList{ "vanilla", "file/New.zip" }));
        QCOMPARE(analysis.failingShaders, QStringList{ "minecraft:core/terrain" });
    }
    void log_shaderErrorOfAnEarlierReloadIsForgotten()
    {
        const QString text = QStringLiteral(
            "[1] Reloading ResourceManager: vanilla, file/A.zip\n"
            "[2] Worker/ERROR: Couldn't parse GLSL: minecraft:core/old:1: boom\n"
            "[3] Reloading ResourceManager: vanilla, file/A.zip\n"
            "[4] Caught error loading resourcepacks, removing all selected resourcepacks\n");
        QVERIFY(analyzeLog(text).failingShaders.isEmpty());
    }
    void log_namesWithCommas()
    {
        QCOMPARE(splitReloadList(QStringLiteral("vanilla, file/Faithful, Extra 32x.zip, file/B.zip, mod:id")),
                 (QStringList{ "vanilla", "file/Faithful, Extra 32x.zip", "file/B.zip", "mod:id" }));
        QCOMPARE(splitReloadList(QString()), QStringList());
    }

    // ---- overlays -----------------------------------------------------------------------------------------------------------
    void overlays_shapes()
    {
        const QByteArray json = R"({"pack":{},"overlays":{"entries":[
            {"directory":"a","formats":34},
            {"directory":"b","formats":[10,20]},
            {"directory":"c","formats":{"min_inclusive":76,"max_inclusive":99}},
            {"directory":"d","min_format":[70,1],"max_format":90},
            {"directory":"e","min_format":50},
            {"directory":"f"},
            {"formats":1},
            {"directory":"../evil","formats":1}]}})";
        const QList<Overlay> overlays = parseOverlays(json);
        QCOMPARE(overlays.size(), 6);
        QCOMPARE(overlays[0].minFormat, 34);
        QCOMPARE(overlays[0].maxFormat, 34);
        QCOMPARE(overlays[1].minFormat, 10);
        QCOMPARE(overlays[1].maxFormat, 20);
        QCOMPARE(overlays[2].minFormat, 76);
        QCOMPARE(overlays[2].maxFormat, 99);
        QCOMPARE(overlays[3].minFormat, 70);
        QCOMPARE(overlays[3].maxFormat, 90);
        QCOMPARE(overlays[4].minFormat, 50);
        QCOMPARE(overlays[4].maxFormat, -1);
        QCOMPARE(overlays[5].directory, QStringLiteral("f"));

        QVERIFY(overlayApplies(overlays[2], 97));
        QVERIFY(!overlayApplies(overlays[2], 75));
        QVERIFY(!overlayApplies(overlays[2], 100));
        QVERIFY(overlayApplies(overlays[4], 1000));
        QVERIFY(!overlayApplies(overlays[4], 49));
        QVERIFY(overlayApplies(overlays[5], 1));
        QVERIFY(overlayApplies(overlays[2], std::nullopt));  // format unknown: counts as applicable
        QVERIFY(parseOverlays(QByteArray("not json")).isEmpty());
        QVERIFY(parseOverlays(QByteArray("\xEF\xBB\xBF") + mcmetaWithOverlay(R"({"directory":"o","formats":1})")).size() == 1);
    }
    void format_forVersion()
    {
        FormatMap map;
        map.insert({ 34, 0 }, { Version("1.20.2"), Version("1.20.2") });
        map.insert({ 46, 0 }, { Version("1.21"), Version("1.21.1") });
        QCOMPARE(formatForVersion(map, "1.21.1").value_or(-1), 46);
        QCOMPARE(formatForVersion(map, "1.20.2").value_or(-1), 34);
        QVERIFY(!formatForVersion(map, "26.3"));  // newer than the table
        QVERIFY(!formatForVersion(map, QString()));
        QVERIFY(!formatForVersion(map, "23w13a"));
    }

    // ---- the suspects -------------------------------------------------------------------------------------------------------
    void suspects_rootShader()
    {
        QTemporaryDir dir;
        const QString packs = dir.path();
        QVERIFY(writeZip(packs + "/Plain.zip", { { "pack.mcmeta", kMcmeta }, { "assets/minecraft/textures/a.png", "x" } }));
        QVERIFY(writeZip(packs + "/Shaders.zip", { { "pack.mcmeta", kMcmeta }, { "assets/minecraft/shaders/core/entity.vsh", kShader } }));
        const QList<Suspect> suspects =
            findSuspects({ "vanilla", "file/Shaders.zip", "file/Plain.zip" }, { "minecraft:core/entity" }, packs, 97);
        QCOMPARE(suspects.size(), 1);
        QCOMPARE(suspects[0].file, QStringLiteral("Shaders.zip"));
        QCOMPARE(suspects[0].shader, QStringLiteral("minecraft:core/entity"));
    }
    void suspects_overlayInRange()
    {
        QTemporaryDir dir;
        const QString packs = dir.path();
        QVERIFY(writeZip(packs + "/Buckets.zip",
                         { { "pack.mcmeta", mcmetaWithOverlay(R"({"directory":"26-1","formats":[76,99]})") },
                           { "26-1/assets/minecraft/shaders/core/entity.fsh", kShader } }));
        const QList<Suspect> suspects = findSuspects({ "vanilla", "file/Buckets.zip" }, { "minecraft:core/entity" }, packs, 97);
        QCOMPARE(suspects.size(), 1);
        QCOMPARE(suspects[0].file, QStringLiteral("Buckets.zip"));
        // the same pack with the format unknown: every overlay counts
        QCOMPARE(findSuspects({ "file/Buckets.zip" }, { "minecraft:core/entity" }, packs, std::nullopt).size(), 1);
    }
    void suspects_overlayOutOfRange()
    {
        QTemporaryDir dir;
        const QString packs = dir.path();
        QVERIFY(writeZip(packs + "/Old.zip",
                         { { "pack.mcmeta", mcmetaWithOverlay(R"({"directory":"old","min_format":[10,0],"max_format":[40,2]})") },
                           { "old/assets/minecraft/shaders/core/entity.vsh", kShader } }));
        QVERIFY(findSuspects({ "file/Old.zip" }, { "minecraft:core/entity" }, packs, 97).isEmpty());
        // an overlay directory that is not declared never counts
        QVERIFY(writeZip(packs + "/Undeclared.zip",
                         { { "pack.mcmeta", kMcmeta }, { "hidden/assets/minecraft/shaders/core/entity.vsh", kShader } }));
        QVERIFY(findSuspects({ "file/Undeclared.zip" }, { "minecraft:core/entity" }, packs, std::nullopt).isEmpty());
    }
    void suspects_higherPackShadowsLower()
    {
        QTemporaryDir dir;
        const QString packs = dir.path();
        const QList<std::pair<QString, QByteArray>> withShader = { { "pack.mcmeta", kMcmeta },
                                                                   { "assets/minecraft/shaders/core/entity.vsh", kShader } };
        QVERIFY(writeZip(packs + "/Low.zip", withShader));
        QVERIFY(writeZip(packs + "/High.zip", withShader));
        // the attempted list runs bottom to top: the last entry wins
        const QList<Suspect> suspects = findSuspects({ "vanilla", "file/Low.zip", "file/High.zip" }, { "minecraft:core/entity" }, packs, 97);
        QCOMPARE(suspects.size(), 1);
        QCOMPARE(suspects[0].file, QStringLiteral("High.zip"));
    }
    void suspects_severalShadersSeveralPacks()
    {
        QTemporaryDir dir;
        const QString packs = dir.path();
        QVERIFY(writeZip(packs + "/A.zip", { { "pack.mcmeta", kMcmeta }, { "assets/minecraft/shaders/core/entity.vsh", kShader } }));
        QVERIFY(writeZip(packs + "/B.zip", { { "pack.mcmeta", kMcmeta }, { "assets/minecraft/shaders/include/fog.glsl", kShader } }));
        const QList<Suspect> suspects =
            findSuspects({ "file/A.zip", "file/B.zip" }, { "minecraft:core/entity", "minecraft:include/fog" }, packs, 97);
        QCOMPARE(suspects.size(), 2);
        QCOMPARE(suspectFiles(suspects), (QStringList{ "A.zip", "B.zip" }));
    }
    void suspects_folderPack()
    {
        QTemporaryDir dir;
        const QString packs = dir.path();
        QVERIFY(writeFile(packs + "/FolderPack/pack.mcmeta", mcmetaWithOverlay(R"({"directory":"ov","formats":[90,99]})")));
        QVERIFY(writeFile(packs + "/FolderPack/ov/assets/minecraft/shaders/core/entity.fsh", kShader));
        QVERIFY(writeFile(packs + "/Plain/pack.mcmeta", kMcmeta));
        const QList<Suspect> suspects = findSuspects({ "file/FolderPack", "file/Plain" }, { "minecraft:core/entity" }, packs, 97);
        QCOMPARE(suspects.size(), 1);
        QCOMPARE(suspects[0].file, QStringLiteral("FolderPack"));
        QVERIFY(findSuspects({ "file/FolderPack" }, { "minecraft:core/entity" }, packs, 50).isEmpty());  // overlay out of range
    }
    void suspects_missingOrBrokenPackIsSkipped()
    {
        QTemporaryDir dir;
        const QString packs = dir.path();
        QVERIFY(writeFile(packs + "/Broken.zip", "this is not a zip"));
        QVERIFY(writeZip(packs + "/Ok.zip", { { "pack.mcmeta", kMcmeta }, { "assets/minecraft/shaders/core/entity.vsh", kShader } }));
        const QList<Suspect> suspects =
            findSuspects({ "file/Ok.zip", "file/Gone.zip", "file/Broken.zip" }, { "minecraft:core/entity" }, packs, 97);
        QCOMPARE(suspects.size(), 1);
        QCOMPARE(suspects[0].file, QStringLiteral("Ok.zip"));
        QVERIFY(findSuspects({ "file/Ok.zip" }, {}, packs, 97).isEmpty());
        QVERIFY(findSuspects({ "file/Ok.zip" }, { "garbage" }, packs, 97).isEmpty());
    }

    // ---- the marker ---------------------------------------------------------------------------------------------------------
    void marker_roundTrip()
    {
        QTemporaryDir dir;
        const QString path = markerPathFor(dir.path());
        QVERIFY(path.endsWith(QStringLiteral(".zero/pack-rollback.json")));
        QVERIFY(!readMarker(path).valid);
        Marker marker;
        marker.valid = true;
        marker.time = QDateTime::currentDateTimeUtc();
        marker.attempted = { "vanilla", "file/A.zip", "a:b" };
        marker.suspects = { { "A.zip", "minecraft:core/entity" } };
        QVERIFY(writeMarker(path, marker));
        Marker back = readMarker(path);
        QVERIFY(back.valid);
        QCOMPARE(back.attempted, marker.attempted);
        QCOMPARE(back.suspects.size(), 1);
        QCOMPARE(back.suspects[0].shader, QStringLiteral("minecraft:core/entity"));
        QVERIFY(!back.asked);
        QVERIFY(markAsked(path));
        QVERIFY(readMarker(path).asked);
        QVERIFY(clearMarker(path));
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(clearMarker(path));  // nothing there is fine
        QVERIFY(!markAsked(path));
        QVERIFY(writeFile(path, "{ damaged"));
        QVERIFY(!readMarker(path).valid);
    }
    void marker_writeOrderClearsIt()
    {
        QTemporaryDir dir;
        const QString gameRoot = dir.path() + "/minecraft";
        const QString instanceRoot = dir.path();
        QVERIFY(writeFile(gameRoot + "/options.txt", "resourcePacks:[\"vanilla\"]\n"));
        QVERIFY(writeFile(gameRoot + "/resourcepacks/A.zip", "x"));
        Marker marker;
        marker.valid = true;
        marker.attempted = { "vanilla", "file/A.zip" };
        const QString path = markerPathFor(instanceRoot);
        QVERIFY(writeMarker(path, marker));
        // no change: the marker stays (nothing was decided)
        QCOMPARE(writeOrder(optionsPathFor(gameRoot), historyPathFor(instanceRoot), {}, { "A.zip" }, nullptr).result, Zero::Content::Packs::Result::NoChange);
        QVERIFY(QFileInfo::exists(path));
        QCOMPARE(writeOrder(optionsPathFor(gameRoot), historyPathFor(instanceRoot), { "A.zip" }, { "A.zip" }, nullptr).result, Zero::Content::Packs::Result::Ok);
        QVERIFY(!QFileInfo::exists(path));
        QCOMPARE(markerPathForHistory(historyPathFor(instanceRoot)), path);
    }

    // ---- after the game stopped ---------------------------------------------------------------------------------------------
    void detect_recordsTheRollback()
    {
        QTemporaryDir dir;
        const QString gameRoot = dir.path() + "/minecraft";
        QVERIFY(writeFile(gameRoot + "/logs/latest.log", rolledBackLog().toUtf8()));
        QVERIFY(writeZip(gameRoot + "/resourcepacks/Beta 2.0.zip",
                         { { "pack.mcmeta", mcmetaWithOverlay(R"({"directory":"26-1","formats":[76,99]})") },
                           { "26-1/assets/minecraft/shaders/core/entity.vsh", kShader } }));
        QVERIFY(writeZip(gameRoot + "/resourcepacks/Alpha.zip", { { "pack.mcmeta", kMcmeta } }));
        DetectInput input;
        input.instanceRoot = dir.path();
        input.gameRoot = gameRoot;
        input.launchStart = QDateTime::currentDateTime().addSecs(-60);
        input.format = 97;
        QCOMPARE(detectAfterLaunch(input), DetectResult::Recorded);
        const Marker marker = readMarker(markerPathFor(dir.path()));
        QVERIFY(marker.valid);
        QVERIFY(!marker.asked);
        QCOMPARE(attemptedFilesTopFirst(marker.attempted), (QStringList{ "Gamma.zip", "Beta 2.0.zip", "Alpha.zip" }));
        QCOMPARE(marker.suspects.size(), 1);
        QCOMPARE(marker.suspects[0].file, QStringLiteral("Beta 2.0.zip"));
        QCOMPARE(marker.suspects[0].shader, QStringLiteral("minecraft:core/entity"));
    }
    void detect_rolledBackWithoutSuspects()
    {
        QTemporaryDir dir;
        const QString gameRoot = dir.path() + "/minecraft";
        QVERIFY(writeFile(gameRoot + "/logs/latest.log", rolledBackLog().toUtf8()));
        DetectInput input;
        input.instanceRoot = dir.path();
        input.gameRoot = gameRoot;
        QCOMPARE(detectAfterLaunch(input), DetectResult::Recorded);  // the packs are gone from disk: still recorded, no suspect
        QVERIFY(readMarker(markerPathFor(dir.path())).suspects.isEmpty());
    }
    void detect_staleLogIsIgnored()
    {
        QTemporaryDir dir;
        const QString gameRoot = dir.path() + "/minecraft";
        const QString log = gameRoot + "/logs/latest.log";
        QVERIFY(writeFile(log, rolledBackLog().toUtf8()));
        QVERIFY(setModified(log, QDateTime::currentDateTime().addSecs(-3600)));
        DetectInput input;
        input.instanceRoot = dir.path();
        input.gameRoot = gameRoot;
        input.launchStart = QDateTime::currentDateTime().addSecs(-60);
        QCOMPARE(detectAfterLaunch(input), DetectResult::Stale);
        QVERIFY(!QFileInfo::exists(markerPathFor(dir.path())));
        // a log from after the launch start counts
        QVERIFY(setModified(log, QDateTime::currentDateTime()));
        QCOMPARE(detectAfterLaunch(input), DetectResult::Recorded);
    }
    void detect_noLog()
    {
        QTemporaryDir dir;
        DetectInput input;
        input.instanceRoot = dir.path();
        input.gameRoot = dir.path() + "/minecraft";
        QCOMPARE(detectAfterLaunch(input), DetectResult::NoLog);
    }
    void detect_cleanLoadRemovesAnOlderMarker()
    {
        QTemporaryDir dir;
        const QString gameRoot = dir.path() + "/minecraft";
        QVERIFY(writeFile(gameRoot + "/logs/latest.log", cleanLog().toUtf8()));
        Marker old;
        old.valid = true;
        old.attempted = { "vanilla", "file/A.zip" };
        QVERIFY(writeMarker(markerPathFor(dir.path()), old));
        DetectInput input;
        input.instanceRoot = dir.path();
        input.gameRoot = gameRoot;
        QCOMPARE(detectAfterLaunch(input), DetectResult::LoadedFine);
        QVERIFY(!QFileInfo::exists(markerPathFor(dir.path())));
    }
    void detect_noPacksKeepsAnOlderMarker()
    {
        QTemporaryDir dir;
        const QString gameRoot = dir.path() + "/minecraft";
        QVERIFY(writeFile(gameRoot + "/logs/latest.log", "[1] Reloading ResourceManager: vanilla, somemod\n"));
        Marker old;
        old.valid = true;
        old.attempted = { "vanilla", "file/A.zip" };
        QVERIFY(writeMarker(markerPathFor(dir.path()), old));
        DetectInput input;
        input.instanceRoot = dir.path();
        input.gameRoot = gameRoot;
        QCOMPARE(detectAfterLaunch(input), DetectResult::NoPacks);  // "Play without them": the way back stays
        QVERIFY(readMarker(markerPathFor(dir.path())).valid);
    }
    void detect_newRollbackResetsAsked()
    {
        QTemporaryDir dir;
        const QString gameRoot = dir.path() + "/minecraft";
        QVERIFY(writeFile(gameRoot + "/logs/latest.log", rolledBackLog().toUtf8()));
        Marker old;
        old.valid = true;
        old.asked = true;
        old.attempted = { "vanilla", "file/Alpha.zip" };
        QVERIFY(writeMarker(markerPathFor(dir.path()), old));
        DetectInput input;
        input.instanceRoot = dir.path();
        input.gameRoot = gameRoot;
        QCOMPARE(detectAfterLaunch(input), DetectResult::Recorded);
        QVERIFY(!readMarker(markerPathFor(dir.path())).asked);
    }

    // ---- what a restore writes, the texts -----------------------------------------------------------------------------------
    void restore_order()
    {
        Marker marker;
        marker.valid = true;
        marker.attempted = { "vanilla", "file/A.zip", "mod:id", "file/B.zip", "file/C.zip" };
        marker.suspects = { { "B.zip", "minecraft:core/entity" } };
        const QSet<QString> existing = { "A.zip", "B.zip", "C.zip" };
        QCOMPARE(restoreOrder(marker, false, {}, existing), (QStringList{ "C.zip", "B.zip", "A.zip" }));
        QCOMPARE(restoreOrder(marker, true, {}, existing), (QStringList{ "C.zip", "A.zip" }));
        // a pack that is gone is left out; a pack that is on now and not in the marker stays below
        QCOMPARE(restoreOrder(marker, false, { "New.zip" }, { "A.zip", "C.zip", "New.zip" }), (QStringList{ "C.zip", "A.zip", "New.zip" }));
    }
    void restore_askRule()
    {
        Marker marker;
        marker.valid = true;
        marker.attempted = { "vanilla", "file/A.zip" };
        QVERIFY(shouldAskBeforeLaunch(marker, {}));
        QVERIFY(!shouldAskBeforeLaunch(marker, { "A.zip" }));  // packs are on again
        marker.asked = true;
        QVERIFY(!shouldAskBeforeLaunch(marker, {}));
        marker.asked = false;
        marker.attempted = { "vanilla" };
        QVERIFY(!shouldAskBeforeLaunch(marker, {}));  // nothing to restore
        QVERIFY(!shouldAskBeforeLaunch(Marker(), {}));
    }
    void texts()
    {
        QCOMPARE(namesText({ "Refined Buckets 2.4.1.zip" }), QStringLiteral("Refined Buckets 2.4.1"));
        QCOMPARE(namesText({ "A.zip", "B.zip" }), QStringLiteral("A and B"));
        QCOMPARE(namesText({ "A.zip", "B", "C.zip" }), QStringLiteral("A, B and C"));
        QCOMPARE(noticeBody({ "Refined Buckets 2.4.1.zip" }),
                 QStringLiteral("When the game started, Refined Buckets 2.4.1 failed to load (it changes the game's shaders), so Minecraft "
                                "turned every pack off."));
        QVERIFY(noticeBody({}).contains(QStringLiteral("one of your packs")));
        QCOMPARE(restoreWithoutText({ "X.zip" }), QStringLiteral("Restore without X"));
        QCOMPARE(promptRestoreAndPlayText({ "X.zip" }), QStringLiteral("Restore without X and play"));
        QCOMPARE(promptRestoreAndPlayText({}), QStringLiteral("Restore and play"));
        QVERIFY(brokeTooltip(QStringLiteral("minecraft:core/entity")).contains(QStringLiteral("minecraft:core/entity")));
    }
    void rowPillAndTooltip()
    {
        PackRow row;
        row.fileName = "Buckets.zip";
        row.name = "Buckets";
        row.brokeLastLaunch = true;
        row.brokeShader = "minecraft:core/entity";
        bool found = false;
        for (const auto& pill : pillsFor(row)) {
            if (pill.text == QStringLiteral("Broke the last launch")) {
                found = true;
                QCOMPARE(int(pill.tone), int(Zero::Mods::Installed::PillTone::Warning));
            }
        }
        QVERIFY(found);
        QVERIFY(rowToolTip(row).contains(QStringLiteral("minecraft:core/entity")));
        row.brokeLastLaunch = false;
        for (const auto& pill : pillsFor(row)) {
            QVERIFY(pill.text != QStringLiteral("Broke the last launch"));
        }
    }

    // ---- the question before Play -------------------------------------------------------------------------------------------
    void ask_restoreWithoutSuspectAndPlay()
    {
        AskFixture f;
        QVERIFY(f.setup("resourcePacks:[\"vanilla\"]\nfoo:bar\n"));
        QVERIFY(Zero::PacksUi::askBeforeLaunch(f.env, nullptr, f.answering(0)));
        QCOMPARE(f.titles, QList<QString>{ "Minecraft turned off your resource packs last time" });
        QCOMPARE(f.labels[0], (QStringList{ "Restore without Bad 1.0 and play", "Play without them", "Cancel" }));
        QCOMPARE(readOptions(f.optionsPath).entries, (QStringList{ "vanilla", "file/A.zip" }));
        QVERIFY(readFile(f.optionsPath).contains("foo:bar"));  // the rest of options.txt stays
        QVERIFY(!QFileInfo::exists(f.markerPath));
        QCOMPARE(readHistory(historyPathFor(f.env.instanceRoot)).size(), 1);  // and it can be undone
    }
    void ask_withoutSuspectsOffersRestoreAll()
    {
        AskFixture f;
        QVERIFY(f.setup("resourcePacks:[\"vanilla\"]\n", false));
        QVERIFY(Zero::PacksUi::askBeforeLaunch(f.env, nullptr, f.answering(0)));
        QCOMPARE(f.labels[0].first(), QStringLiteral("Restore and play"));
        QCOMPARE(readOptions(f.optionsPath).entries, (QStringList{ "vanilla", "file/A.zip", "file/Bad 1.0.zip" }));
    }
    void ask_playWithoutThemAsksOnlyOnce()
    {
        AskFixture f;
        const QByteArray options = "resourcePacks:[\"vanilla\"]\n";
        QVERIFY(f.setup(options));
        QVERIFY(Zero::PacksUi::askBeforeLaunch(f.env, nullptr, f.answering(1)));
        QCOMPARE(readFile(f.optionsPath), options);  // untouched
        QVERIFY(readMarker(f.markerPath).asked);
        QVERIFY(Zero::PacksUi::askBeforeLaunch(f.env, nullptr, f.answering(0)));
        QCOMPARE(f.titles.size(), 1);  // not asked again
        QCOMPARE(readFile(f.optionsPath), options);
    }
    void ask_cancelStopsTheLaunchAndAsksAgain()
    {
        AskFixture f;
        const QByteArray options = "resourcePacks:[\"vanilla\"]\n";
        QVERIFY(f.setup(options));
        QVERIFY(!Zero::PacksUi::askBeforeLaunch(f.env, nullptr, f.answering(2)));
        QVERIFY(!Zero::PacksUi::askBeforeLaunch(f.env, nullptr, f.answering(-1)));
        QCOMPARE(f.titles.size(), 2);
        QCOMPARE(readFile(f.optionsPath), options);
        QVERIFY(!readMarker(f.markerPath).asked);
    }
    void ask_noQuestionWhenPacksAreOnOrNothingToRestore()
    {
        {
            AskFixture f;
            QVERIFY(f.setup("resourcePacks:[\"vanilla\",\"file/A.zip\"]\n"));
            QVERIFY(Zero::PacksUi::askBeforeLaunch(f.env, nullptr, f.answering(2)));
            QVERIFY(f.titles.isEmpty());
        }
        {
            AskFixture f;
            QVERIFY(f.setup("resourcePacks:[\"vanilla\"]\n"));
            QVERIFY(QFile::remove(f.env.gameRoot + "/resourcepacks/A.zip"));
            QVERIFY(QFile::remove(f.env.gameRoot + "/resourcepacks/Bad 1.0.zip"));
            QVERIFY(Zero::PacksUi::askBeforeLaunch(f.env, nullptr, f.answering(2)));
            QVERIFY(f.titles.isEmpty());
        }
        {
            AskFixture f;
            QVERIFY(f.setup("resourcePacks:[\"vanilla\"]\n"));
            QVERIFY(QFile::remove(f.markerPath));
            QVERIFY(Zero::PacksUi::askBeforeLaunch(f.env, nullptr, f.answering(2)));
            QVERIFY(f.titles.isEmpty());
        }
    }
    void ask_gameRunningDoesNotWrite()
    {
        AskFixture f;
        const QByteArray options = "resourcePacks:[\"vanilla\"]\n";
        QVERIFY(f.setup(options));
        f.env.isRunning = []() { return true; };
        // the restore cannot be written: a second question offers to play anyway; options.txt stays as it is
        int calls = 0;
        const auto ask = [&calls](QWidget*, const Zero::ModsUi::ChoicePrompt&) {
            ++calls;
            return 0;
        };
        QVERIFY(Zero::PacksUi::askBeforeLaunch(f.env, nullptr, ask));
        QCOMPARE(calls, 2);
        QCOMPARE(readFile(f.optionsPath), options);
    }
};

QTEST_GUILESS_MAIN(PackRollbackTest)

#include "PackRollback_test.moc"
