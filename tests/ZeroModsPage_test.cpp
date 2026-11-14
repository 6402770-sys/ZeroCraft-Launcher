// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the instance window's Mods page (docs/zero/CONTENT-MODS.md, "Installed mods page"): the pure core (filters, search,
// captions, pills, the "other mods are affected" decision, the Remove texts) and the page against a REAL ModFolderModel on a temp folder
// with real (tiny) fabric jars: loading / empty / no-loader states, search, filters, the selection bar, enable / disable (the file is
// renamed to ".disabled", the affected-mods question, the running lock), Remove (an injected trash function: a failing one keeps the
// file and its packwiz metadata, a working one drops both, no relation map is left pointing at a removed mod). Offscreen, no network.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "archive/ArchiveWriter.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/mod/ModFolderModel.h"
#include "modplatform/packwiz/Packwiz.h"
#include "settings/INISettingsObject.h"
#include "settings/SettingsObject.h"
#include "ui/zero/PillTag.h"
#include "ui/zero/ZeroEmptyState.h"
#include "ui/zero/ZeroSkeleton.h"
#include "ui/zero/ZeroToast.h"
#include "ui/zero/content/FilterPanel.h"
#include "ui/zero/instance/ModsPage.h"
#include "ui/zero/instance/ModsUpdates.h"
#include "ui/zero/instance/ModSnapshotNotice.h"
#include "zero/mods/InstalledModsModel.h"
#include "zero/mods/ModSnapshot.h"

using namespace Zero;
using namespace Zero::ModsUi;
using namespace Zero::Mods::Installed;

namespace {

QString S(const char* text)
{
    return QString::fromUtf8(text);
}

Row row(const QString& name, bool enabled = true, const QString& fileName = QString())
{
    Row r;
    r.name = name;
    r.id = fileName.isEmpty() ? name.toLower() + S(".jar") : fileName;
    r.fileName = r.id + (enabled ? QString() : S(".disabled"));
    r.modId = name.toLower().remove(QLatin1Char(' '));
    r.enabled = enabled;
    return r;
}

// ---- real folders ----------------------------------------------------------------------------------------------------------------

bool writeJar(const QString& path, const QString& id, const QString& name, const QString& version, const QString& depends = QString())
{
    MMCZip::ArchiveWriter writer(path);
    if (!writer.open()) {
        return false;
    }
    const QByteArray json = QStringLiteral(R"RAW({"schemaVersion":1,"id":"%1","name":"%2","version":"%3"%4})RAW")
                                .arg(id, name, version, depends.isEmpty() ? QString() : QStringLiteral(R"RAW(,"depends":{"%1":"*"})RAW").arg(depends))
                                .toUtf8();
    if (!writer.addFile(QStringLiteral("fabric.mod.json"), json)) {
        return false;
    }
    return bool(writer.close());
}

/// `<root>/<id>` with an instance.cfg (and the global settings BaseInstance overrides): a real MinecraftInstance without components.
struct InstanceFixture {
    explicit InstanceFixture(const QString& root)
    {
        global = std::make_unique<INISettingsObject>(root + S("/global.cfg"));
        for (const char* name : { "ShowGameTime", "RecordGameTime", "ShowConsole", "AutoCloseConsole", "ShowConsoleOnError", "LogPrePostOutput",
                                  "ConsoleOverflowStop" })
            global->registerSetting(name, false);
        for (const char* name : { "PreLoadCommand", "PreLaunchCommand", "WrapperCommand", "PostExitCommand" })
            global->registerSetting(name, QString());
        global->registerSetting("ConsoleMaxLines", 100000);
        // MinecraftInstance::loadSpecificSettings() overrides these (the pack profile asks for the instance settings on its first turn)
        for (const char* name : { "CloseAfterLaunch", "CustomGLFWPath", "CustomOpenALPath", "CustomSDLPath", "EnableFeralGamemode", "EnableMangoHud",
                                  "Env", "IgnoreJavaCompatibility", "JavaArchitecture", "JavaPath", "JavaRealArchitecture", "JavaSignature",
                                  "JavaVendor", "JavaVersion", "JvmArgs", "LaunchMaximized", "LowMemWarning", "MaxMemAlloc", "MinMemAlloc",
                                  "MinecraftWinHeight", "MinecraftWinWidth", "OnlineFixes", "PermGen", "QuitAfterGameStop", "UseDiscreteGpu",
                                  "UseNativeGLFW", "UseNativeOpenAL", "UseNativeSDL", "UseZink" })
            global->registerSetting(name, QString());
        const QString dir = root + S("/inst");
        QDir().mkpath(dir);
        QFile cfg(dir + S("/instance.cfg"));
        if (cfg.open(QIODevice::WriteOnly)) {
            cfg.write("[General]\nname=Test Instance\n");
        }
        cfg.close();
        instance = std::make_unique<MinecraftInstance>(global.get(), std::make_unique<INISettingsObject>(dir + S("/instance.cfg")), dir);
    }
    std::unique_ptr<SettingsObject> global;
    std::unique_ptr<MinecraftInstance> instance;
};

/// A check that does nothing by itself: the tests answer it by emitting its signals.
class FakeCheck : public ModUpdateCheck {
   public:
    FakeCheck(int* aborts, QObject* parent) : ModUpdateCheck(parent), m_aborts(aborts) {}
    void start() override { started = true; }
    void abort() override
    {
        ++*m_aborts;
        aborted = true;
    }
    QList<ModUpdate> updates() const override { return result; }

    QList<Resource::Ptr> resources;  ///< what the page handed in
    QList<ModUpdate> result;
    bool started = false;
    bool aborted = false;

   private:
    int* m_aborts;
};

ModUpdate update(const QString& key, const QString& name, const QString& version, const QString& type = S("Release"))
{
    ModUpdate u;
    u.key = key;
    u.name = name;
    u.newVersion = version;
    u.versionType = type;
    return u;
}

/// The texts of the toasts of a window.
QStringList toastTexts(QWidget* window)
{
    QStringList out;
    for (const ZeroToast* toast : window->findChildren<ZeroToast*>()) {
        for (const QLabel* label : toast->findChildren<QLabel*>()) {
            if (!label->text().isEmpty()) {
                out.append(label->text());
            }
        }
    }
    return out;
}

struct Rig {
    QTemporaryDir root;
    InstanceFixture fixture{ root.path() };
    QDir mods{ root.path() + S("/mods") };
    std::unique_ptr<ModFolderModel> model;
    std::unique_ptr<ModsPage> page;

    // the fakes
    bool running = false;
    bool loader = true;
    QStringList trashCalls;
    QSet<QString> failTrash;  ///< file names the fake Recycle Bin refuses
    QList<ChoicePrompt> prompts;
    QList<int> answers;  ///< consumed one per question; empty = cancel
    int addModsCalls = 0;
    // the update check
    QPointer<FakeCheck> check;  ///< the last check the page started
    int checkCalls = 0;
    int abortCalls = 0;
    bool curseForge = true;
    QList<ModUpdate> applied;
    int applyCalls = 0;
    bool applyOk = true;
    std::function<void()> onApply;
    // the Dependency Inspector
    bool fakeScan = false;  ///< the scan is answered by the test (scanDone), not by the thread pool
    int scanDelay = -1;     ///< ms between a reload and the scan; -1 = the page's default (300)
    QList<Zero::Mods::ScanRequest> scanRequests;
    QList<Zero::Mods::ScanDone> scanDone;
    QStringList findCalls;
    // safe update
    QString snapshotsPath;  ///< `snapshotsDir` of the page; empty = no snapshots (the other tests)
    Zero::Mods::Snapshot::Hooks snapshotHooks;  ///< the page's `snapshotHooks` (the trash one stays the page's `trash`)


    Rig()
    {
        QDir().mkpath(mods.absolutePath());
    }

    void addJar(const QString& fileName, const QString& id, const QString& name, const QString& version, const QString& depends = QString())
    {
        QVERIFY(writeJar(mods.filePath(fileName), id, name, version, depends));
    }

    /// The three jars of most tests: Sodium, Iris Shaders (needs Sodium), Lithium (disabled).
    void addTrio()
    {
        addJar(S("sodium.jar"), S("sodium"), S("Sodium"), S("0.5.8"));
        addJar(S("iris.jar"), S("iris"), S("Iris Shaders"), S("1.7.0"), S("sodium"));
        addJar(S("lithium.jar.disabled"), S("lithium"), S("Lithium"), S("0.12.0"));
    }

    /// Alpha and Beta have provider info (projects PA / PB, hashes h1 / h2), Gamma is a plain jar.
    void addMetaMods()
    {
        addJar(S("alpha.jar"), S("alpha"), S("Alpha"), S("1.0"));
        addJar(S("beta.jar"), S("beta"), S("Beta"), S("1.0"));
        addJar(S("gamma.jar"), S("gamma"), S("Gamma"), S("1.0"));
        addMetadata(S("alpha"), S("Alpha"), S("alpha.jar"), S("1.20.1"), S("PA"), S("h1"));
        addMetadata(S("beta"), S("Beta"), S("beta.jar"), S("1.20.1"), S("PB"), S("h2"));
    }

    /// Answers the running check: the updates it found, the mods it could not check, then `finished`.
    void finishCheck(const QList<ModUpdate>& updates, const QList<QPair<QString, QString>>& failures = {}, bool ok = true,
                     const QString& error = QString())
    {
        QVERIFY(check);
        check->result = updates;
        for (const auto& failure : failures) {
            emit check->modFailed(failure.first, failure.second);
        }
        emit check->finished(ok, error);
    }

    void addMetadata(const QString& slug, const QString& name, const QString& fileName, const QString& mc = S("1.20.1"),
                     const QString& projectId = S("PROJ1"), const QString& hash = S("0000"),
                     ModPlatform::ResourceProvider provider = ModPlatform::ResourceProvider::MODRINTH, bool lockUpdate = false)
    {
        Packwiz::V1::Mod meta;
        meta.slug = slug;
        meta.name = name;
        meta.filename = fileName;
        meta.side = ModPlatform::SideType::ClientSide;
        meta.loaders = ModPlatform::ModLoaderTypes(ModPlatform::Fabric);
        meta.mcVersions = { mc };
        meta.mode = S("url");
        meta.url = QUrl(S("https://cdn.test/") + fileName);
        meta.hashFormat = S("sha1");
        meta.hash = hash;
        meta.provider = provider;
        meta.fileId = provider == ModPlatform::ResourceProvider::FLAME ? QVariant(777) : QVariant(S("FILE1"));  // Flame ids are numbers
        meta.projectId = projectId;
        meta.versionNumber = S("1.0");
        meta.lockUpdate = lockUpdate;
        QDir index(mods.filePath(S(".index")));
        QDir().mkpath(index.absolutePath());
        Packwiz::V1::updateModIndex(index, meta);
    }

    /// A scan answer for one file of the folder: fill its lists, then hand it to inspection().
    Zero::Mods::ModInspection inspectionOf(const QString& file, const QString& name, bool enabled = true) const
    {
        Zero::Mods::ModInspection m;
        m.path = mods.filePath(file);
        m.fileName = file;
        m.name = name;
        m.enabled = enabled;
        return m;
    }
    static Zero::Mods::Inspection inspection(const QList<Zero::Mods::ModInspection>& mods)
    {
        Zero::Mods::Inspection out;
        out.mods = mods;
        for (int i = 0; i < out.mods.size(); ++i) {
            out.byPath.insert(out.mods.at(i).path, i);
        }
        return out;
    }
    /// Waits until the page asked for `count` scans (fake scan only).
    bool waitScans(int count)
    {
        for (int i = 0; i < 200 && scanDone.size() < count; ++i) {
            QTest::qWait(25);
        }
        return scanDone.size() >= count;
    }
    /// Answers the newest scan and waits until the page took it.
    bool answerScan(const Zero::Mods::Inspection& answer)
    {
        if (scanDone.isEmpty()) {
            return false;
        }
        scanDone.last()(answer);
        for (int i = 0; i < 200 && page->isScanning(); ++i) {
            QTest::qWait(25);
        }
        page->rebuildNow();
        return !page->isScanning() && page->hasScan();
    }

    ModsPageEnvironment environment()
    {
        ModsPageEnvironment env;
        env.target = [this]() {
            Content::TargetInstance target;
            target.id = S("inst");
            target.name = S("Test Instance");
            target.mcVersion = S("1.21.1");
            if (loader) {
                target.loaders = ModPlatform::ModLoaderTypes(ModPlatform::Fabric);
                target.loaderText = S("Fabric");
            }
            return target;
        };
        env.isRunning = [this]() { return running; };
        env.trash = [this](const QString& path) {
            trashCalls.append(QFileInfo(path).fileName());
            if (failTrash.contains(QFileInfo(path).fileName())) {
                return false;  // the Recycle Bin refused: nothing is touched
            }
            return QFile::remove(path);  // stands in for the Recycle Bin: the file goes
        };
        env.ask = [this](QWidget*, const ChoicePrompt& prompt) {
            prompts.append(prompt);
            return answers.isEmpty() ? -1 : answers.takeFirst();
        };
        env.addMods = [this]() { ++addModsCalls; };
        env.checkUpdates = [this](const QList<Resource::Ptr>& resources, QObject* parent) -> ModUpdateCheck* {
            auto* fake = new FakeCheck(&abortCalls, parent);
            fake->resources = resources;
            check = fake;
            ++checkCalls;
            return fake;
        };
        env.applyUpdates = [this](QWidget*, const QList<ModUpdate>& updates) {
            applied = updates;
            ++applyCalls;
            if (onApply) {
                onApply();
            }
            return applyOk;
        };
        env.curseForgeAvailable = [this]() { return curseForge; };
        if (scanDelay >= 0) {
            env.scanDelayMs = scanDelay;
        }
        if (fakeScan) {
            env.scanRunner = [this](const Zero::Mods::ScanRequest& request, Zero::Mods::ScanDone done) {
                scanRequests.append(request);
                scanDone.append(std::move(done));
            };
        }
        env.findMod = [this](const QString& query) { findCalls.append(query); };
        if (!snapshotsPath.isEmpty()) {
            env.snapshotsDir = [this]() { return snapshotsPath; };
        }
        env.snapshotHooks = snapshotHooks;
        return env;
    }

    /// The model and the page, not yet opened.
    void build()
    {
        model = std::make_unique<ModFolderModel>(mods.absolutePath(), fixture.instance.get(), true, true);
        page = std::make_unique<ModsPage>(environment(), model.get());
        page->resize(1000, 640);
    }

    /// Opens the page and waits until `count` mods are listed and every jar was read.
    bool open(int count)
    {
        page->opened();
        page->show();
        for (int i = 0; i < 200; ++i) {
            QTest::qWait(50);
            page->rebuildNow();
            if (page->isLoaded() && page->listModel()->rowCount() == count && !model->hasPendingParseTasks()) {
                QTest::qWait(60);
                page->rebuildNow();
                return true;
            }
        }
        return false;
    }

    int rowOf(const QString& name) const
    {
        for (int i = 0; i < page->listModel()->rowCount(); ++i) {
            if (page->listModel()->rowAt(i).name == name) {
                return i;
            }
        }
        return -1;
    }
    /// The row of that mod, or an empty row while it is not listed (a folder read in the middle of a roll back may not have it yet):
    /// asking the list for row -1 would stop the whole test binary on Qt's index check instead of failing (or retrying) this test.
    const Row& rowNamed(const QString& name) const
    {
        static const Row none;
        const int row = rowOf(name);
        return row < 0 ? none : page->listModel()->rowAt(row);
    }

    QStringList names() const
    {
        QStringList out;
        for (int i = 0; i < page->listModel()->rowCount(); ++i) {
            out.append(page->listModel()->rowAt(i).name);
        }
        return out;
    }

    /// Reads the folder again (a file watcher is not something a test can count on) and waits until that update was applied.
    bool settle()
    {
        QSignalSpy spy(model.get(), &ResourceFolderModel::updateFinished);
        model->update();
        if (!spy.count() && !spy.wait(10000)) {
            return false;
        }
        QTest::qWait(120);
        page->rebuildNow();
        return true;
    }
};

}  // namespace

class ZeroModsPageTest : public QObject {
    Q_OBJECT

   private slots:
    // =============================== the pure core ==========================================================================

    void filterIds()
    {
        for (const Filter filter : allFilters()) {
            QCOMPARE(filterFromId(filterId(filter)), filter);
        }
        QCOMPARE(filterFromId(S("nonsense")), Filter::All);
        QCOMPARE(filterFromId(QString()), Filter::All);
        QCOMPARE(filterLabel(Filter::Issues), S("Issues"));
        QCOMPARE(allFilters().size(), 4);  // no "Updates" yet
    }

    void filterPredicates()
    {
        Row on = row(S("A"));
        Row off = row(S("B"), false);
        Row bad = row(S("C"));
        bad.issues = { S("Not marked as compatible with the instance's game version.") };
        QVERIFY(matchesFilter(on, Filter::All) && matchesFilter(off, Filter::All) && matchesFilter(bad, Filter::All));
        QVERIFY(matchesFilter(on, Filter::Enabled) && !matchesFilter(off, Filter::Enabled) && matchesFilter(bad, Filter::Enabled));
        QVERIFY(!matchesFilter(on, Filter::Disabled) && matchesFilter(off, Filter::Disabled) && !matchesFilter(bad, Filter::Disabled));
        QVERIFY(!matchesFilter(on, Filter::Issues) && !matchesFilter(off, Filter::Issues) && matchesFilter(bad, Filter::Issues));
    }

    void searchMatchesNameIdAndFileName()
    {
        Row r = row(S("Iris Shaders"));
        r.modId = S("iris");
        r.fileName = S("iris-fabric-1.7.0+mc1.21.jar");
        QVERIFY(matchesSearch(r, QString()));
        QVERIFY(matchesSearch(r, S("   ")));
        QVERIFY(matchesSearch(r, S("shaders")));
        QVERIFY(matchesSearch(r, S("IRIS")));
        QVERIFY(matchesSearch(r, S("1.7.0")));       // file name
        QVERIFY(matchesSearch(r, S("shaders iris")));  // every word, any order
        QVERIFY(matchesSearch(r, S("iris  fabric")));  // extra spaces
        QVERIFY(!matchesSearch(r, S("sodium")));
        QVERIFY(!matchesSearch(r, S("iris sodium")));  // one word missing
    }

    void visibleRowsOrderFilterAndSearch()
    {
        const QList<Row> rows = { row(S("zeta")), row(S("Alpha"), false), row(S("beta")), row(S("alpha two")) };
        QCOMPARE(visibleRows(rows, Filter::All, QString()), (QList<int>{ 1, 3, 2, 0 }));  // case-insensitive by name
        QCOMPARE(visibleRows(rows, Filter::Disabled, QString()), (QList<int>{ 1 }));
        QCOMPARE(visibleRows(rows, Filter::Enabled, S("alpha")), (QList<int>{ 3 }));
        QCOMPARE(visibleRows(rows, Filter::Issues, QString()), QList<int>());
        QVERIFY(visibleRows({}, Filter::All, S("x")).isEmpty());
    }

    void captions()
    {
        QCOMPARE(captionText({ 48, 45, 3, 0 }, 48, false), S("48 mods · 3 disabled"));
        QCOMPARE(captionText({ 48, 48, 0, 0 }, 48, false), S("48 mods"));
        QCOMPARE(captionText({ 1, 1, 0, 0 }, 1, false), S("1 mod"));
        QCOMPARE(captionText({ 1, 0, 1, 0 }, 1, false), S("1 mod · 1 disabled"));
        QCOMPARE(captionText({ 48, 45, 3, 0 }, 5, true), S("5 of 48 mods"));
        QCOMPARE(captionText({ 1, 1, 0, 0 }, 0, true), S("0 of 1 mod"));
        QCOMPARE(selectionText(2), S("2 selected"));
        const QList<Row> rows = { row(S("a")), row(S("b"), false), row(S("c"), false) };
        const Counts counts = countOf(rows);
        QCOMPARE(counts.total, 3);
        QCOMPARE(counts.enabled, 1);
        QCOMPARE(counts.disabled, 2);
        QCOMPARE(counts.issues, 0);
    }

    void pillsOfARow()
    {
        Row plain = row(S("Sodium"));
        plain.source = Source::Modrinth;
        QList<PillSpec> pills = pillsFor(plain, S("1.21.1"));
        QCOMPARE(pills.size(), 1);
        QCOMPARE(pills.first().text, S("Modrinth"));

        Row local = row(S("Local"));
        QCOMPARE(pillsFor(local, S("1.21.1")).first().text, S("Local file"));

        Row all = row(S("Iris"), false);
        all.source = Source::CurseForge;
        all.environment = S("Client");
        all.requiredBy = 2;
        all.issues = { S("x") };
        pills = pillsFor(all, S("1.21.1"));
        QStringList texts;
        for (const PillSpec& pill : pills) {
            texts.append(pill.text);
        }
        QCOMPARE(texts, (QStringList{ S("Disabled"), S("Not for 1.21.1"), S("Required by 2"), S("Client"), S("CurseForge") }));
        QCOMPARE(pills.at(1).tone, PillTone::Warning);  // "Not for" is the warning
        QCOMPARE(pills.at(0).tone, PillTone::Plain);
        QCOMPARE(notForText(QString()), S("Not for this version"));
        QCOMPARE(sourceLabel(Source::Modrinth), S("Modrinth"));
    }

    void pillsThatDoNotFitDropTheLeastImportantFirst()
    {
        Row all = row(S("Iris"), false);
        all.source = Source::CurseForge;
        all.environment = S("Client");
        all.requiredBy = 2;
        all.issues = { S("x") };
        const QList<PillSpec> pills = pillsFor(all, S("1.21.1"));
        // most important first: the warning, "Required by", "Disabled", the provider, the environment
        const QStringList order = { S("Not for 1.21.1"), S("Required by 2"), S("Disabled"), S("CurseForge"), S("Client") };
        QCOMPARE(ModDelegate::fitPills(pills, 100000).size(), 5);
        QVERIFY(ModDelegate::fitPills(pills, 0).isEmpty());
        for (int budget = 600; budget >= 0; budget -= 3) {
            QSet<QString> now;
            for (const auto& placed : ModDelegate::fitPills(pills, budget)) {
                now.insert(placed.spec.text);
            }
            const QStringList expected = order.mid(0, int(now.size()));
            QCOMPARE(now, QSet<QString>(expected.begin(), expected.end()));
        }
    }

    void nextToggleOnASelection()
    {
        QCOMPARE(nextToggle({ true }), ToggleAction::Disable);
        QCOMPARE(nextToggle({ false }), ToggleAction::Enable);
        QCOMPARE(nextToggle({ true, true, true }), ToggleAction::Disable);
        QCOMPARE(nextToggle({ true, false }), ToggleAction::Enable);
        QCOMPARE(nextToggle({}), ToggleAction::Enable);
    }

    void togglePromptWhenNothingElseIsAffected()
    {
        QVERIFY(!togglePrompt(ToggleAction::Disable, { S("Sodium") }, {}).needed);
        QVERIFY(!togglePrompt(ToggleAction::Enable, { S("Sodium") }, {}).needed);
        QVERIFY(!togglePrompt(ToggleAction::Enable, {}, { S("x") }).needed);
    }

    void togglePromptDisable()
    {
        TogglePrompt p = togglePrompt(ToggleAction::Disable, { S("Sodium") }, { S("Iris Shaders") });
        QVERIFY(p.needed);
        QCOMPARE(p.body, S("Disabling Sodium affects Iris Shaders, which requires it."));
        QCOMPARE(p.onlyLabel, S("Disable only Sodium"));
        QCOMPARE(p.allLabel, S("Disable all 2"));
        QCOMPARE(p.allCount, 2);
        QCOMPARE(p.title, S("Disable Iris Shaders too?"));

        p = togglePrompt(ToggleAction::Disable, { S("Sodium") }, { S("Iris"), S("Indium") });
        QCOMPARE(p.body, S("Disabling Sodium affects Iris and Indium, which require it."));
        QCOMPARE(p.allLabel, S("Disable all 3"));
        QCOMPARE(p.title, S("Disable 2 other mods too?"));

        p = togglePrompt(ToggleAction::Disable, { S("A"), S("B"), S("C") }, { S("Iris") });
        QCOMPARE(p.body, S("Disabling 3 mods affects Iris, which requires them."));
        QCOMPARE(p.onlyLabel, S("Disable only the 3 selected"));
        QCOMPARE(p.allLabel, S("Disable all 4"));
    }

    void togglePromptEnable()
    {
        TogglePrompt p = togglePrompt(ToggleAction::Enable, { S("Iris Shaders") }, { S("Fabric API") });
        QVERIFY(p.needed);
        QCOMPARE(p.title, S("Enable Fabric API too?"));
        QCOMPARE(p.body, S("Iris Shaders requires Fabric API, which is disabled."));
        QCOMPARE(p.onlyLabel, S("Enable only Iris Shaders"));
        QCOMPARE(p.allLabel, S("Enable all 2"));

        p = togglePrompt(ToggleAction::Enable, { S("Iris") }, { S("A"), S("B") });
        QCOMPARE(p.title, S("Enable 2 required mods too?"));
        QCOMPARE(p.body, S("Iris requires A and B, which are disabled."));

        p = togglePrompt(ToggleAction::Enable, { S("X"), S("Y") }, { S("A") });
        QCOMPARE(p.body, S("The 2 selected mods require A, which is disabled."));
        QCOMPARE(p.onlyLabel, S("Enable only the 2 selected"));
    }

    void longNamesAreShortenedInButtonsOnly()
    {
        const QString name = S("An Extremely Long Mod Name That Would Not Fit In A Button");
        const TogglePrompt p = togglePrompt(ToggleAction::Disable, { name }, { S("Iris") });
        QVERIFY(p.onlyLabel.size() < name.size());
        QVERIFY(p.onlyLabel.endsWith(QChar(0x2026)));
        QVERIFY(p.body.contains(name));
    }

    void joinNamesLists()
    {
        QCOMPARE(joinNames({}), QString());
        QCOMPARE(joinNames({ S("A") }), S("A"));
        QCOMPARE(joinNames({ S("A"), S("B") }), S("A and B"));
        QCOMPARE(joinNames({ S("A"), S("B"), S("C") }), S("A, B and C"));
        QCOMPARE(joinNames({ S("A"), S("B"), S("C"), S("D"), S("E") }), S("A, B, C and 2 more"));
    }

    void toggleFailureTexts()
    {
        QCOMPARE(toggleFailureText(ToggleAction::Enable, { S("Sodium") }), S("Couldn't enable Sodium"));
        QCOMPARE(toggleFailureText(ToggleAction::Disable, { S("Sodium"), S("Iris") }), S("Couldn't disable 2 mods"));
    }

    void removeConfirmTexts()
    {
        RemoveConfirm c = removeConfirm({ S("Sodium") });
        QCOMPARE(c.title, S("Remove Sodium?"));
        QCOMPARE(c.listed, QStringList{ S("Sodium") });
        QVERIFY(c.more.isEmpty());
        QCOMPARE(c.note, S("It goes to the Recycle Bin."));
        QCOMPARE(c.confirmLabel, S("Remove"));

        QStringList eight;
        for (int i = 1; i <= 8; ++i) {
            eight.append(S("Mod %1").arg(i));
        }
        c = removeConfirm(eight);
        QCOMPARE(c.title, S("Remove 8 mods?"));
        QCOMPARE(c.listed.size(), 8);
        QVERIFY(c.more.isEmpty());
        QCOMPARE(c.note, S("They go to the Recycle Bin."));

        QStringList twelve = eight;
        for (int i = 9; i <= 12; ++i) {
            twelve.append(S("Mod %1").arg(i));
        }
        c = removeConfirm(twelve);
        QCOMPARE(c.title, S("Remove 12 mods?"));
        QCOMPARE(c.listed, eight);
        QCOMPARE(c.more, S("and 4 more"));
    }

    void removeResultTexts()
    {
        RemoveResult r = removeResult({ S("A"), S("B"), S("C") }, {});
        QCOMPARE(r.text, S("Removed 3 mods"));
        QCOMPARE(r.outcome, Outcome::Success);
        r = removeResult({ S("Sodium") }, {});
        QCOMPARE(r.text, S("Removed Sodium"));
        r = removeResult({}, { S("A"), S("B") });
        QCOMPARE(r.text, S("Couldn't move 2 mods to the Recycle Bin"));
        QCOMPARE(r.outcome, Outcome::Error);
        r = removeResult({}, { S("Sodium") });
        QCOMPARE(r.text, S("Couldn't move Sodium to the Recycle Bin"));
        r = removeResult({ S("A"), S("B"), S("C") }, { S("D"), S("E") });
        QCOMPARE(r.text, S("Removed 3 mods. Couldn't move 2 mods to the Recycle Bin"));
        QCOMPARE(r.outcome, Outcome::Warning);
    }

    void tooltipHasFileAndIssues()
    {
        Row r = row(S("Sodium"));
        r.fileName = S("sodium.jar");
        QCOMPARE(rowToolTip(r), S("sodium.jar"));
        r.issues = { S("Not marked as compatible with the instance's game version.") };
        QVERIFY(rowToolTip(r).contains(QLatin1Char('\n')));
        QVERIFY(rowToolTip(r).contains(S("compatible")));
    }

    // =============================== the list model =========================================================================

    void listModelUpdatesInPlaceWhenTheModsAreTheSame()
    {
        ModListModel model;
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        QSignalSpy changes(&model, &QAbstractItemModel::dataChanged);
        QVERIFY(model.setRows({ row(S("A")), row(S("B")) }, { 1, 2 }, { 0, 1 }));
        QCOMPARE(resets.count(), 1);
        QVERIFY(!model.setRows({ row(S("A")), row(S("B"), false) }, { 1, 2 }, { 0, 1 }));
        QCOMPARE(resets.count(), 1);
        QCOMPARE(changes.count(), 1);
        QCOMPARE(model.data(model.index(1), ModListModel::EnabledRole).toBool(), false);
        QVERIFY(model.setRows({ row(S("B")) }, { 2 }, { 1 }));
        QCOMPARE(resets.count(), 2);
        QCOMPARE(model.rowOfToken(2), 0);
        QCOMPARE(model.rowOfToken(1), -1);
        QCOMPARE(model.sourceRowAt(0), 1);
        QCOMPARE(model.rowOfId(S("b.jar")), 0);
    }

    // =============================== the page on real folders ===============================================================

    void startsInTheLoadingState()
    {
        Rig rig;
        rig.addTrio();
        rig.build();
        QCOMPARE(rig.page->view(), ModsPage::View::Loading);
        QVERIFY(!rig.page->isLoaded());
        QCOMPARE(rig.page->id(), S("mods"));
        QCOMPARE(rig.page->displayName(), S("Mods"));
        rig.page->show();
        QVERIFY(rig.page->skeleton()->isVisibleTo(rig.page.get()));
        QVERIFY(!rig.page->searchField()->isVisibleTo(rig.page.get()));  // no toolbar content before there is something to search
        QVERIFY(!rig.page->addButton()->isVisibleTo(rig.page.get()));
    }

    void listsTheModsSortedWithTheirFacts()
    {
        Rig rig;
        rig.addTrio();
        rig.build();
        QVERIFY(rig.open(3));
        QCOMPARE(rig.page->view(), ModsPage::View::List);
        QCOMPARE(rig.names(), (QStringList{ S("Iris Shaders"), S("Lithium"), S("Sodium") }));
        const Row& sodium = rig.rowNamed(S("Sodium"));
        QCOMPARE(sodium.version, S("0.5.8"));
        QCOMPARE(sodium.modId, S("sodium"));
        QVERIFY(sodium.enabled);
        QCOMPARE(sodium.source, Source::Local);
        QCOMPARE(sodium.fileName, S("sodium.jar"));
        QVERIFY(!rig.rowNamed(S("Lithium")).enabled);
        QCOMPARE(rig.rowNamed(S("Lithium")).fileName, S("lithium.jar.disabled"));
        QTRY_COMPARE_WITH_TIMEOUT(rig.rowNamed(S("Sodium")).requiredBy, 1, 5000);  // Iris needs it
        QCOMPARE(rig.page->captionLabel()->text(), S("3 mods · 1 disabled"));
        QVERIFY(rig.page->searchField()->isVisibleTo(rig.page.get()));
        QVERIFY(rig.page->addButton()->isVisibleTo(rig.page.get()));
        QVERIFY(!rig.page->banner()->isVisibleTo(rig.page.get()));
        // painting it must not trip over anything
        QVERIFY(!rig.page->grab().isNull());
    }

    void filtersAndSearch()
    {
        Rig rig;
        rig.addTrio();
        rig.build();
        QVERIFY(rig.open(3));

        rig.page->setFilter(Filter::Disabled);
        QCOMPARE(rig.names(), QStringList{ S("Lithium") });
        QCOMPARE(rig.page->captionLabel()->text(), S("1 of 3 mods"));
        QCOMPARE(rig.page->filterControl()->selected(), S("disabled"));
        rig.page->setFilter(Filter::Enabled);
        QCOMPARE(rig.names(), (QStringList{ S("Iris Shaders"), S("Sodium") }));
        rig.page->setFilter(Filter::Issues);
        QCOMPARE(rig.page->view(), ModsPage::View::NoMatches);
        QCOMPARE(rig.page->emptyState()->findChildren<QLabel*>().isEmpty(), false);
        rig.page->setFilter(Filter::All);
        QCOMPARE(rig.page->view(), ModsPage::View::List);
        QCOMPARE(rig.names().size(), 3);

        rig.page->setSearchText(S("iris"));
        QCOMPARE(rig.names(), QStringList{ S("Iris Shaders") });
        rig.page->setSearchText(S("lithium.jar"));  // file name
        QCOMPARE(rig.names(), QStringList{ S("Lithium") });
        rig.page->setSearchText(S("sodium"));  // name and mod id; Iris only mentions it as a dependency
        QCOMPARE(rig.names(), QStringList{ S("Sodium") });
        rig.page->setSearchText(S("zzz"));
        QCOMPARE(rig.page->view(), ModsPage::View::NoMatches);
        QCOMPARE(rig.page->captionLabel()->text(), S("0 of 3 mods"));
        QVERIFY(rig.page->emptyState()->isVisibleTo(rig.page.get()));
        // the empty state's action clears both
        rig.page->setFilter(Filter::Disabled);
        QMetaObject::invokeMethod(rig.page->emptyState(), "actionTriggered");
        QCOMPARE(rig.page->view(), ModsPage::View::List);
        QCOMPARE(rig.page->filter(), Filter::All);
        QVERIFY(rig.page->searchText().isEmpty());
        QCOMPARE(rig.names().size(), 3);
    }

    void emptyFolderAndNoLoader()
    {
        {
            Rig rig;
            rig.build();
            QVERIFY(rig.open(0));
            QCOMPARE(rig.page->view(), ModsPage::View::Empty);
            QVERIFY(rig.page->emptyState()->isVisibleTo(rig.page.get()));
            bool found = false;
            for (const QLabel* label : rig.page->emptyState()->findChildren<QLabel*>()) {
                found = found || label->text() == S("No mods yet");
                if (label->text().contains(S("Discover"))) {
                    QCOMPARE(label->text(), S("Find mods for Test Instance in Discover."));
                }
            }
            QVERIFY(found);
            QPushButton* action = rig.page->emptyState()->findChild<QPushButton*>();
            QVERIFY(action && action->isVisibleTo(rig.page.get()));
            QCOMPARE(action->text(), S("Add mods"));
            action->click();
            QCOMPARE(rig.addModsCalls, 1);
            QVERIFY(!rig.page->addButton()->isVisibleTo(rig.page.get()));  // the empty state has the action
        }
        {
            Rig rig;
            rig.loader = false;
            rig.build();
            QVERIFY(rig.open(0));
            QCOMPARE(rig.page->view(), ModsPage::View::NoLoader);
            bool found = false;
            for (const QLabel* label : rig.page->emptyState()->findChildren<QLabel*>()) {
                found = found || label->text() == S("This instance has no mod loader");
            }
            QVERIFY(found);
            QPushButton* action = rig.page->emptyState()->findChild<QPushButton*>();
            QVERIFY(!action || !action->isVisibleTo(rig.page.get()));  // no Add without a loader
        }
        {
            // mods without a loader: the list is shown, but there is nothing to add them with
            Rig rig;
            rig.loader = false;
            rig.addTrio();
            rig.build();
            QVERIFY(rig.open(3));
            QCOMPARE(rig.page->view(), ModsPage::View::List);
            QVERIFY(!rig.page->addButton()->isVisibleTo(rig.page.get()));
        }
    }

    void theSelectionBarReplacesTheCaption()
    {
        Rig rig;
        rig.addTrio();
        rig.build();
        QVERIFY(rig.open(3));
        QVERIFY(rig.page->captionLabel()->isVisibleTo(rig.page.get()));
        QVERIFY(!rig.page->selectionBar()->isVisibleTo(rig.page.get()));
        rig.page->selectRows({ 0 });
        QVERIFY(!rig.page->selectionBar()->isVisibleTo(rig.page.get()));  // one is not a selection bar
        rig.page->selectRows({ rig.rowOf(S("Sodium")), rig.rowOf(S("Lithium")) });
        QVERIFY(rig.page->selectionBar()->isVisibleTo(rig.page.get()));
        QVERIFY(!rig.page->captionLabel()->isVisibleTo(rig.page.get()));
        QCOMPARE(rig.page->selectionLabel()->text(), S("2 selected"));
        QVERIFY(rig.page->selectionEnableButton()->isEnabled());   // Lithium is off
        QVERIFY(rig.page->selectionDisableButton()->isEnabled());  // Sodium is on
        QVERIFY(rig.page->selectionRemoveButton()->isEnabled());
        rig.page->selectRows({ rig.rowOf(S("Sodium")), rig.rowOf(S("Iris Shaders")) });
        QVERIFY(!rig.page->selectionEnableButton()->isEnabled());  // both are on
        rig.page->selectRows({ 0 });
        QVERIFY(rig.page->captionLabel()->isVisibleTo(rig.page.get()));
    }

    void selectionSurvivesAFilterChange()
    {
        Rig rig;
        rig.addTrio();
        rig.build();
        QVERIFY(rig.open(3));
        rig.page->selectRows({ rig.rowOf(S("Sodium")), rig.rowOf(S("Iris Shaders")) });
        rig.page->setFilter(Filter::Enabled);  // the same two are still listed
        QCOMPARE(rig.page->selectedRows().size(), 2);
        rig.page->setFilter(Filter::Disabled);  // gone
        QCOMPARE(rig.page->selectedRows().size(), 0);
        rig.page->setFilter(Filter::All);
        QCOMPARE(rig.page->selectedRows().size(), 0);
    }

    void togglingRenamesTheFile()
    {
        Rig rig;
        rig.addJar(S("solo.jar"), S("solo"), S("Solo"), S("1.0"));
        rig.addJar(S("lithium.jar.disabled"), S("lithium"), S("Lithium"), S("0.12.0"));
        rig.build();
        QVERIFY(rig.open(2));

        rig.page->requestToggle({ rig.rowOf(S("Solo")) }, ToggleAction::Disable);
        QVERIFY(QFile::exists(rig.mods.filePath(S("solo.jar.disabled"))));
        QVERIFY(!QFile::exists(rig.mods.filePath(S("solo.jar"))));
        QVERIFY(rig.prompts.isEmpty());  // nothing depends on it: no question
        QTRY_VERIFY(!rig.rowNamed(S("Solo")).enabled);
        QCOMPARE(rig.rowNamed(S("Solo")).fileName, S("solo.jar.disabled"));
        QCOMPARE(rig.page->captionLabel()->text(), S("2 mods · 2 disabled"));

        rig.page->requestToggle({ rig.rowOf(S("Lithium")) }, ToggleAction::Enable);
        QVERIFY(QFile::exists(rig.mods.filePath(S("lithium.jar"))));
        QVERIFY(!QFile::exists(rig.mods.filePath(S("lithium.jar.disabled"))));
        QTRY_VERIFY(rig.rowNamed(S("Lithium")).enabled);

        // turning on what is on does nothing
        rig.page->requestToggle({ rig.rowOf(S("Lithium")) }, ToggleAction::Enable);
        QVERIFY(QFile::exists(rig.mods.filePath(S("lithium.jar"))));
        QVERIFY(rig.settle());
        QCOMPARE(rig.names().size(), 2);
    }

    void theSwitchAndTheSpaceKeyToggle()
    {
        Rig rig;
        rig.addJar(S("solo.jar"), S("solo"), S("Solo"), S("1.0"));
        rig.build();
        QVERIFY(rig.open(1));
        auto* view = rig.page->listView();
        const QModelIndex index = rig.page->listModel()->index(0);
        const QRect card = ModDelegate::cardRect(view->visualRect(index));
        QVERIFY(card.isValid() && card.height() == ModDelegate::kRowHeight);
        const QPoint center = ModDelegate::switchRect(card).center();
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, center);
        QVERIFY(QFile::exists(rig.mods.filePath(S("solo.jar.disabled"))));
        QVERIFY(rig.page->selectedRows().isEmpty());  // the switch does not select the row
        QTRY_VERIFY(!rig.rowNamed(S("Solo")).enabled);

        rig.page->selectRows({ 0 });
        QTest::keyClick(view, Qt::Key_Space);
        QVERIFY(QFile::exists(rig.mods.filePath(S("solo.jar"))));
        QVERIFY(!QFile::exists(rig.mods.filePath(S("solo.jar.disabled"))));
    }

    void disablingARequiredModAsksAndTheAnswerDecides()
    {
        Rig rig;
        rig.addTrio();
        rig.build();
        QVERIFY(rig.open(3));
        QTRY_COMPARE_WITH_TIMEOUT(rig.rowNamed(S("Sodium")).requiredBy, 1, 5000);

        // Cancel: nothing changes
        rig.answers = { 0 };
        rig.page->requestToggle({ rig.rowOf(S("Sodium")) }, ToggleAction::Disable);
        QCOMPARE(rig.prompts.size(), 1);
        QCOMPARE(rig.prompts.first().body, S("Disabling Sodium affects Iris Shaders, which requires it."));
        QCOMPARE(rig.prompts.first().buttons.size(), 3);
        QCOMPARE(rig.prompts.first().buttons.at(1).label, S("Disable only Sodium"));
        QCOMPARE(rig.prompts.first().buttons.at(2).label, S("Disable all 2"));
        QVERIFY(QFile::exists(rig.mods.filePath(S("sodium.jar"))));
        QVERIFY(QFile::exists(rig.mods.filePath(S("iris.jar"))));

        // "only": Sodium goes, Iris stays on
        rig.answers = { 1 };
        rig.page->requestToggle({ rig.rowOf(S("Sodium")) }, ToggleAction::Disable);
        QVERIFY(QFile::exists(rig.mods.filePath(S("sodium.jar.disabled"))));
        QVERIFY(QFile::exists(rig.mods.filePath(S("iris.jar"))));
        QTRY_VERIFY(!rig.rowNamed(S("Sodium")).enabled);

        // enabling Sodium again needs nothing; then "all" takes Iris with it
        rig.page->requestToggle({ rig.rowOf(S("Sodium")) }, ToggleAction::Enable);
        QVERIFY(QFile::exists(rig.mods.filePath(S("sodium.jar"))));
        QTRY_VERIFY(rig.rowNamed(S("Sodium")).enabled);
        rig.answers = { 2 };
        rig.page->requestToggle({ rig.rowOf(S("Sodium")) }, ToggleAction::Disable);
        QVERIFY(QFile::exists(rig.mods.filePath(S("sodium.jar.disabled"))));
        QVERIFY(QFile::exists(rig.mods.filePath(S("iris.jar.disabled"))));
        QCOMPARE(rig.prompts.size(), 3);
    }

    void enablingAModWhoseRequirementIsOffAsks()
    {
        Rig rig;
        rig.addJar(S("sodium.jar.disabled"), S("sodium"), S("Sodium"), S("0.5.8"));
        rig.addJar(S("iris.jar.disabled"), S("iris"), S("Iris Shaders"), S("1.7.0"), S("sodium"));
        rig.build();
        QVERIFY(rig.open(2));
        QTRY_COMPARE_WITH_TIMEOUT(rig.rowNamed(S("Sodium")).requiredBy, 1, 5000);

        rig.answers = { 2 };
        rig.page->requestToggle({ rig.rowOf(S("Iris Shaders")) }, ToggleAction::Enable);
        QCOMPARE(rig.prompts.size(), 1);
        QCOMPARE(rig.prompts.first().title, S("Enable Sodium too?"));
        QCOMPARE(rig.prompts.first().body, S("Iris Shaders requires Sodium, which is disabled."));
        QVERIFY(QFile::exists(rig.mods.filePath(S("iris.jar"))));
        QVERIFY(QFile::exists(rig.mods.filePath(S("sodium.jar"))));
    }

    void selectionBarTogglesTheWholeSelection()
    {
        Rig rig;
        rig.addJar(S("a.jar"), S("a"), S("Alpha"), S("1"));
        rig.addJar(S("b.jar"), S("b"), S("Beta"), S("1"));
        rig.addJar(S("c.jar"), S("c"), S("Gamma"), S("1"));
        rig.build();
        QVERIFY(rig.open(3));
        rig.page->selectRows({ 0, 1 });
        rig.page->selectionDisableButton()->click();
        QVERIFY(QFile::exists(rig.mods.filePath(S("a.jar.disabled"))));
        QVERIFY(QFile::exists(rig.mods.filePath(S("b.jar.disabled"))));
        QVERIFY(QFile::exists(rig.mods.filePath(S("c.jar"))));
        QTRY_VERIFY(!rig.page->selectionDisableButton()->isEnabled());  // both are off now; the selection stayed
        QCOMPARE(rig.page->selectedRows().size(), 2);
        rig.page->selectionEnableButton()->click();
        QVERIFY(QFile::exists(rig.mods.filePath(S("a.jar"))));
        QVERIFY(QFile::exists(rig.mods.filePath(S("b.jar"))));
    }

    void nothingChangesWhileTheGameRuns()
    {
        Rig rig;
        rig.addTrio();
        rig.build();
        QVERIFY(rig.open(3));
        QVERIFY(!rig.page->banner()->isVisibleTo(rig.page.get()));

        rig.running = true;
        rig.page->refreshRunning();
        QVERIFY(rig.page->isLocked());
        QVERIFY(rig.page->banner()->isVisibleTo(rig.page.get()));
        QVERIFY(rig.page->listView()->rowDelegate()->locked());

        rig.answers = { 2, 1, 2 };
        rig.page->requestToggle({ rig.rowOf(S("Sodium")) }, ToggleAction::Disable);
        rig.page->requestToggle({ rig.rowOf(S("Lithium")) }, ToggleAction::Enable);
        rig.page->requestRemove({ rig.rowOf(S("Sodium")) });
        QVERIFY(rig.prompts.isEmpty());      // not even a question
        QVERIFY(rig.trashCalls.isEmpty());
        QVERIFY(QFile::exists(rig.mods.filePath(S("sodium.jar"))));
        QVERIFY(QFile::exists(rig.mods.filePath(S("lithium.jar.disabled"))));

        rig.page->selectRows({ 0, 1 });
        QVERIFY(!rig.page->selectionEnableButton()->isEnabled());
        QVERIFY(!rig.page->selectionDisableButton()->isEnabled());
        QVERIFY(!rig.page->selectionRemoveButton()->isEnabled());

        rig.running = false;
        rig.page->refreshRunning();
        QVERIFY(!rig.page->isLocked());
        QVERIFY(!rig.page->banner()->isVisibleTo(rig.page.get()));
        rig.answers.clear();
        rig.page->requestToggle({ rig.rowOf(S("Lithium")) }, ToggleAction::Enable);
        QVERIFY(QFile::exists(rig.mods.filePath(S("lithium.jar"))));
    }

    void removeAsksAndTrashesOnlyTheSelectedFiles()
    {
        Rig rig;
        rig.addTrio();
        rig.build();
        QVERIFY(rig.open(3));

        // Cancel
        rig.answers = { 0 };
        rig.page->requestRemove({ rig.rowOf(S("Lithium")) });
        QCOMPARE(rig.prompts.size(), 1);
        QCOMPARE(rig.prompts.first().title, S("Remove Lithium?"));
        QCOMPARE(rig.prompts.first().note, S("It goes to the Recycle Bin."));
        QCOMPARE(rig.prompts.first().defaultButton, 0);  // Enter cancels
        QVERIFY(rig.trashCalls.isEmpty());
        QVERIFY(QFile::exists(rig.mods.filePath(S("lithium.jar.disabled"))));

        // Remove the disabled one, then Sodium and Iris together
        rig.answers = { 1 };
        rig.page->requestRemove({ rig.rowOf(S("Lithium")) });
        QCOMPARE(rig.trashCalls, QStringList{ S("lithium.jar.disabled") });
        QVERIFY(!QFile::exists(rig.mods.filePath(S("lithium.jar.disabled"))));
        QVERIFY(rig.settle());
        QTRY_COMPARE(rig.page->listModel()->rowCount(), 2);
        QCOMPARE(ZeroToast::count(rig.page->window()), 1);

        rig.answers = { 1 };
        rig.page->selectRows({ 0, 1 });
        rig.page->selectionRemoveButton()->click();
        QCOMPARE(rig.prompts.last().title, S("Remove 2 mods?"));
        QCOMPARE(rig.prompts.last().list.size(), 2);
        QVERIFY(rig.settle());
        QTRY_COMPARE(rig.page->listModel()->rowCount(), 0);
        QVERIFY(rig.mods.entryList(QDir::Files).isEmpty());
        QTRY_COMPARE(rig.page->view(), ModsPage::View::Empty);
    }

    void deleteKeyAsksToRemove()
    {
        Rig rig;
        rig.addJar(S("solo.jar"), S("solo"), S("Solo"), S("1.0"));
        rig.build();
        QVERIFY(rig.open(1));
        rig.page->selectRows({ 0 });
        rig.answers = { 0 };
        QTest::keyClick(rig.page->listView(), Qt::Key_Delete);
        QCOMPARE(rig.prompts.size(), 1);
        QVERIFY(QFile::exists(rig.mods.filePath(S("solo.jar"))));
    }

    void aFailingTrashKeepsTheFileAndItsMetadata()
    {
        Rig rig;
        rig.addJar(S("rare.jar"), S("rare"), S("Rare Mod"), S("1.0"));
        rig.addJar(S("fine.jar"), S("fine"), S("Fine Mod"), S("1.0"));
        rig.addMetadata(S("rare"), S("Rare Mod"), S("rare.jar"));
        rig.addMetadata(S("fine"), S("Fine Mod"), S("fine.jar"));
        rig.build();
        QVERIFY(rig.open(2));
        const QString rareToml = rig.mods.filePath(S(".index/rare.pw.toml"));
        const QString fineToml = rig.mods.filePath(S(".index/fine.pw.toml"));
        QTRY_VERIFY_WITH_TIMEOUT(rig.rowNamed(S("Rare Mod")).hasMetadata, 5000);
        QCOMPARE(rig.rowNamed(S("Rare Mod")).source, Source::Modrinth);
        QCOMPARE(rig.rowNamed(S("Rare Mod")).environment, S("Client"));
        QVERIFY(QFile::exists(rareToml) && QFile::exists(fineToml));

        rig.failTrash = { S("rare.jar") };
        rig.answers = { 1 };
        rig.page->selectRows({ 0, 1 });  // Fine Mod, Rare Mod
        rig.page->requestRemove(rig.page->selectedRows());
        QCOMPARE(rig.trashCalls.size(), 2);
        // the refused one stays, with its metadata; the other went with its metadata
        QVERIFY(QFile::exists(rig.mods.filePath(S("rare.jar"))));
        QVERIFY(QFile::exists(rareToml));
        QVERIFY(!QFile::exists(rig.mods.filePath(S("fine.jar"))));
        QVERIFY(!QFile::exists(fineToml));
        QVERIFY(rig.settle());
        QTRY_COMPARE(rig.page->listModel()->rowCount(), 1);
        QCOMPARE(rig.names(), QStringList{ S("Rare Mod") });
        QCOMPARE(ZeroToast::count(rig.page->window()), 1);

        // everything refused: nothing is removed at all, and still no permanent delete
        rig.answers = { 1 };
        rig.page->requestRemove({ 0 });
        QVERIFY(QFile::exists(rig.mods.filePath(S("rare.jar"))));
        QVERIFY(QFile::exists(rareToml));
    }

    void issuesComeFromThePrismCheckAndFilter()
    {
        Rig rig;
        rig.addJar(S("old.jar"), S("old"), S("Old Mod"), S("1.0"));
        rig.addJar(S("plain.jar"), S("plain"), S("Plain Mod"), S("1.0"));
        rig.addMetadata(S("old"), S("Old Mod"), S("old.jar"), S("1.20.1"));  // the instance has no Minecraft component: not compatible
        rig.build();
        QVERIFY(rig.open(2));
        QTRY_VERIFY_WITH_TIMEOUT(!rig.rowNamed(S("Old Mod")).issues.isEmpty(), 5000);
        QVERIFY(rig.rowNamed(S("Plain Mod")).issues.isEmpty());
        rig.page->setFilter(Filter::Issues);
        QCOMPARE(rig.names(), QStringList{ S("Old Mod") });
        const QList<PillSpec> pills = pillsFor(rig.rowNamed(S("Old Mod")), S("1.21.1"));
        QCOMPARE(pills.first().text, S("Not for 1.21.1"));
    }

    void removingAModLeavesNoDanglingRelations()
    {
        Rig rig;
        rig.addTrio();
        rig.build();
        QVERIFY(rig.open(3));
        QTRY_COMPARE_WITH_TIMEOUT(rig.rowNamed(S("Sodium")).requiredBy, 1, 5000);

        // Remove Iris (the one that needs Sodium); once the folder was read again Sodium is needed by nobody and disabling it asks nothing
        rig.answers = { 1 };
        rig.page->requestRemove({ rig.rowOf(S("Iris Shaders")) });
        QVERIFY(rig.settle());
        QTRY_COMPARE(rig.page->listModel()->rowCount(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(rig.rowNamed(S("Sodium")).requiredBy, 0, 5000);
        rig.prompts.clear();
        rig.answers.clear();
        rig.page->requestToggle({ rig.rowOf(S("Sodium")) }, ToggleAction::Disable);
        QVERIFY(rig.prompts.isEmpty());
        QVERIFY(QFile::exists(rig.mods.filePath(S("sodium.jar.disabled"))));
    }

    void addModsButtonAndOverflow()
    {
        Rig rig;
        rig.addTrio();
        rig.build();
        QVERIFY(rig.open(3));
        QSignalSpy spy(rig.page.get(), &ModsPage::addModsRequested);
        rig.page->addButton()->click();
        QCOMPARE(rig.addModsCalls, 1);
        QCOMPARE(spy.count(), 1);
        QVERIFY(rig.page->overflowButton()->menu());
        QStringList entries;
        for (const QAction* action : rig.page->overflowButton()->menu()->actions()) {
            entries.append(action->text());
        }
        // the classic page needs a real instance: this rig has none, so only the folder entry is there
        QCOMPARE(entries, QStringList{ S("Open mods folder") });
    }

    void closingStopsOnlyTheWatchingItStarted()
    {
        Rig rig;
        rig.addJar(S("solo.jar"), S("solo"), S("Solo"), S("1.0"));
        rig.build();
        QVERIFY(rig.model->startWatching());  // somebody else watches first
        QVERIFY(rig.open(1));
        rig.page->closed();
        // still watched: asking to start returns false (already watching), so the watching was never toggled off
        QVERIFY(!rig.model->startWatching());
        QVERIFY(rig.model->stopWatching());
    }

    void reopeningAfterClosingStillLoads()
    {
        Rig rig;
        rig.addJar(S("solo.jar"), S("solo"), S("Solo"), S("1.0"));
        rig.build();
        QVERIFY(rig.open(1));
        rig.page->closed();
        rig.addJar(S("two.jar"), S("two"), S("Two"), S("1.0"));
        rig.page->opened();
        QTRY_COMPARE_WITH_TIMEOUT(rig.page->listModel()->rowCount(), 2, 10000);
        rig.page->closed();
    }

    // ---- C5b: the update check, the pure part ---------------------------------------------------------------------------------------

    void updatesFilterAndCounts()
    {
        QCOMPARE(filterId(Filter::Updates), S("updates"));
        QCOMPARE(filterFromId(S("updates")), Filter::Updates);
        QCOMPARE(filterLabel(Filter::Updates), S("Updates"));
        QCOMPARE(filtersFor(false), allFilters());
        QCOMPARE(filtersFor(true).size(), 5);
        QCOMPARE(filtersFor(true).last(), Filter::Updates);

        Row up = row(S("Sodium"));
        up.updateVersion = S("0.6.14");
        const Row none = row(S("Lithium"));
        QVERIFY(matchesFilter(up, Filter::Updates));
        QVERIFY(!matchesFilter(none, Filter::Updates));
        QCOMPARE(visibleRows({ up, none }, Filter::Updates, QString()), QList<int>{ 0 });
        QCOMPARE(countOf({ up, none }).updates, 1);
    }

    void captionsAndTextsOfTheCheck()
    {
        Counts counts;
        counts.total = 8;
        counts.updates = 3;
        QCOMPARE(captionText(counts, 8, false), S("8 mods \u00b7 3 updates"));
        counts.updates = 1;
        QCOMPARE(captionText(counts, 8, false), S("8 mods \u00b7 1 update"));
        counts.disabled = 2;
        QCOMPARE(captionText(counts, 8, false), S("8 mods \u00b7 2 disabled \u00b7 1 update"));
        QCOMPARE(captionText(counts, 3, true), S("3 of 8 mods"));

        QCOMPARE(checkingCaption(41), S("Checking 41 mods\u2026"));
        QCOMPARE(checkingCaption(1), S("Checking 1 mod\u2026"));
        QCOMPARE(checkButtonText(), S("Check for updates"));
        QCOMPARE(checkingButtonText(), S("Checking\u2026"));
        QCOMPARE(updateAllText(3), S("Update all 3"));
        QCOMPARE(updateAllText(1), S("Update 1 mod"));
        QCOMPARE(updatePillText(S("0.6.14")), S("Update \u2192 0.6.14"));
        QCOMPARE(updateToText(S("0.6.14")), S("Update to 0.6.14"));
        QCOMPARE(updateSelectedText(3), S("Update 3 mods"));
        QCOMPARE(checkAgainText(), S("Check for updates again"));
        QCOMPARE(unverifiableText(0), QString());
        QCOMPARE(unverifiableText(1), S("1 mod has no provider info and can't be checked."));
        QCOMPARE(unverifiableText(7), S("7 mods have no provider info and can't be checked."));
        QCOMPARE(upToDateText(), S("All mods are up to date"));
        QCOMPARE(checkFailedText(S("offline")), S("Couldn't check for updates: offline"));
        QCOMPARE(updatedText({ S("Sodium") }, S("0.6.14")), S("Updated Sodium to 0.6.14"));
        QCOMPARE(updatedText({ S("A"), S("B"), S("C") }, QString()), S("Updated 3 mods"));
        QCOMPARE(versionTypeLabel(S("Beta")), S("Beta"));
        QCOMPARE(versionTypeLabel(S("Unknown")), QString());
    }

    void theUpdatePillIsTheLastAndNeverTheFirstToGo()
    {
        Row r = row(S("Sodium"));
        r.environment = S("Client");
        r.requiredBy = 2;
        r.updateVersion = S("0.6.14");
        r.updateType = S("Beta");
        const QList<PillSpec> pills = pillsFor(r, S("1.21.1"));
        QCOMPARE(pills.last().text, S("Update \u2192 0.6.14"));
        QCOMPARE(pills.last().tone, PillTone::Accent);
        QCOMPARE(pills.last().maxWidth, kUpdatePillMaxWidth);
        for (int i = 0; i < pills.size() - 1; ++i) {
            QVERIFY(pills.at(i).priority < pills.last().priority);
        }
        // a row too narrow for everything keeps the update pill
        const int alone = PillTag::widthForText(pills.last().text);
        const QList<ModDelegate::Placed> placed = ModDelegate::fitPills(pills, alone);
        QCOMPARE(placed.size(), 1);
        QCOMPARE(placed.first().spec.tone, PillTone::Accent);

        // a very long version is elided to 140 px
        r.updateVersion = QString(90, QLatin1Char('9'));
        const QList<ModDelegate::Placed> longPlaced = ModDelegate::fitPills(pillsFor(r, S("1.21.1")), 100000);
        QCOMPARE(longPlaced.last().spec.tone, PillTone::Accent);
        QVERIFY(longPlaced.last().width <= kUpdatePillMaxWidth);
        QVERIFY(longPlaced.last().text.endsWith(QChar(0x2026)));
        QVERIFY(longPlaced.last().text != longPlaced.last().spec.text);

        // the tooltip tells the type, a failed check says why
        QVERIFY(rowToolTip(r).contains(S("Update to ") + r.updateVersion + S(" (Beta)")));
        r.updateType.clear();
        QVERIFY(rowToolTip(r).contains(S("Update to ")));
        QVERIFY(!rowToolTip(r).contains(S("(")));
        Row failed = row(S("Iris"));
        failed.checkError = S("No valid version found");
        QVERIFY(rowToolTip(failed).contains(S("Couldn't check for updates: No valid version found")));
    }

    void resultsFollowTheFileTheyWereAbout()
    {
        QCOMPARE(updateKeyOf(S("sodium.jar.disabled")), S("sodium.jar"));
        QCOMPARE(updateKeyOf(S("sodium.jar")), S("sodium.jar"));

        Row a = row(S("A"));
        a.updateKey = S("a.jar");
        a.metadataHash = S("h1");
        a.fileStamp = S("10/1");
        Row b = row(S("B"));
        b.updateKey = S("b.jar");
        b.metadataHash = S("h2");
        b.fileStamp = S("20/2");
        UpdateResults res;
        res.checked = true;
        res.entries = { CheckEntry{ S("a.jar"), S("h1"), S("10/1"), S("1.2"), S("Beta"), QString() },
                        CheckEntry{ S("b.jar"), S("h2"), S("20/2"), QString(), QString(), S("offline") },
                        CheckEntry{ S("gone.jar"), S("h3"), S("30/3"), S("2.0"), QString(), QString() } };
        QCOMPARE(updateCount(res), 2);
        QCOMPARE(failureCount(res), 1);

        QList<Row> rows{ a, b };
        QVERIFY(pruneResults(res, rows));  // the mod that is gone leaves
        QCOMPARE(res.entries.size(), 2);
        QVERIFY(!pruneResults(res, rows));
        overlayResults(rows, res);
        QCOMPARE(rows[0].updateVersion, S("1.2"));
        QCOMPARE(rows[0].updateType, S("Beta"));
        QVERIFY(rows[0].checkError.isEmpty());
        QVERIFY(!rows[1].hasUpdate());
        QCOMPARE(rows[1].checkError, S("offline"));

        // the same mod switched off keeps its answer (the key ignores ".disabled")
        Row off = a;
        off.id = S("a.jar.disabled");
        off.updateKey = updateKeyOf(off.id);
        rows = { off, b };
        QVERIFY(!pruneResults(res, rows));

        // another packwiz hash, another file: the answer does not fit any more
        rows = { a, b };
        rows[0].metadataHash = S("h9");
        QVERIFY(pruneResults(res, rows));
        QCOMPARE(res.entries.size(), 1);
        rows = { a, b };
        rows[1].fileStamp = S("21/3");
        QVERIFY(pruneResults(res, rows));
        QVERIFY(res.entries.isEmpty());

        res.entries = { CheckEntry{ S("a.jar"), S("h1"), S("10/1"), S("1.2"), QString(), QString() },
                        CheckEntry{ S("b.jar"), S("h2"), S("20/2"), S("3.0"), QString(), QString() } };
        dropEntries(res, { S("a.jar") });
        QCOMPARE(res.entries.size(), 1);
        QCOMPARE(res.entries.first().key, S("b.jar"));
    }

    void whichModsAreChecked()
    {
        Row ok = row(S("A"));
        ok.checkable = true;
        Row flame = ok;
        flame.source = Source::CurseForge;
        Row noInfo = row(S("B"));
        Row locked = row(S("C"));
        locked.updateLocked = true;
        QVERIFY(canCheck(ok, false));
        QVERIFY(canCheck(flame, true));
        QVERIFY(!canCheck(flame, false));
        QVERIFY(!canCheck(noInfo, true));
        QVERIFY(!canCheck(locked, true));
        // a locked mod is on purpose: it is not "unchecked"; CurseForge without CurseForge is
        QCOMPARE(unverifiableCount({ ok, flame, noInfo, locked }, true), 1);
        QCOMPARE(unverifiableCount({ ok, flame, noInfo, locked }, false), 2);

        UpdateResults res;
        res.entries = { CheckEntry{ S("a"), QString(), QString(), QString(), QString(), S("x") },
                        CheckEntry{ S("b"), QString(), QString(), QString(), QString(), S("y") } };
        QVERIFY(everyCheckFailed(2, res));
        QVERIFY(!everyCheckFailed(3, res));  // one was checked and has no update: not "everything failed"
        QVERIFY(!everyCheckFailed(0, UpdateResults()));
        res.entries.first().newVersion = S("1");
        QVERIFY(!everyCheckFailed(2, res));
    }

    void anUpdateBelongsToTheRightMod()
    {
        const QList<UpdateCandidate> c = { { Source::Modrinth, S("P1"), S("h1"), S("1.0") },
                                           { Source::Modrinth, S("P2"), S("h2"), S("2.0") },
                                           { Source::Modrinth, S("P1"), S("h3"), S("1.1") },
                                           { Source::CurseForge, S("P1"), S("h4"), S("9") } };
        QList<bool> taken(4, false);
        QCOMPARE(matchUpdate(c, taken, { Source::Modrinth, S("P2"), S("zz"), QString() }), 1);
        QCOMPARE(matchUpdate(c, taken, { Source::Modrinth, S("P1"), S("h3"), QString() }), 2);   // the hash decides
        QCOMPARE(matchUpdate(c, taken, { Source::Modrinth, S("P1"), S("zz"), S("1.1") }), 2);    // then the version
        QCOMPARE(matchUpdate(c, taken, { Source::Modrinth, S("P1"), S("zz"), S("zz") }), 0);     // then the first
        taken[0] = true;
        QCOMPARE(matchUpdate(c, taken, { Source::Modrinth, S("P1"), S("zz"), S("zz") }), 2);     // not twice
        QCOMPARE(matchUpdate(c, taken, { Source::CurseForge, S("P1"), QString(), QString() }), 3);  // the provider counts
        QCOMPARE(matchUpdate(c, taken, { Source::Modrinth, S("P3"), S("h1"), S("1.0") }), -1);
        QCOMPARE(matchUpdate({}, {}, { Source::Modrinth, S("P1"), S("h1"), S("1.0") }), -1);
    }

    // ---- C5b: the page --------------------------------------------------------------------------------------------------------------

    void rowsCarryWhatTheCheckNeeds()
    {
        Rig rig;
        rig.addMetaMods();
        rig.build();
        QVERIFY(rig.open(3));
        const Row& alpha = rig.rowNamed(S("Alpha"));
        QVERIFY(alpha.checkable);
        QCOMPARE(alpha.updateKey, S("alpha.jar"));
        QCOMPARE(alpha.metadataHash, S("h1"));
        QCOMPARE(alpha.projectId, S("PA"));
        QVERIFY(!alpha.fileStamp.isEmpty());
        QVERIFY(!rig.rowNamed(S("Gamma")).checkable);
        QVERIFY(!rig.rowNamed(S("Gamma")).updateLocked);
        // before any check there is nothing about updates on the page
        QVERIFY(rig.page->checkButton()->isVisibleTo(rig.page.get()));
        QCOMPARE(rig.page->checkButton()->text(), S("Check for updates"));
        QVERIFY(!rig.page->infoLine()->isVisibleTo(rig.page.get()));
        QCOMPARE(rig.page->filterControl()->count(), 4);
        QCOMPARE(rig.page->updateCount(), 0);
    }

    void checkingLocksTheRowsAndHandsOnlyModsWithMetadata()
    {
        Rig rig;
        rig.addMetaMods();
        rig.build();
        QVERIFY(rig.open(3));
        rig.page->checkButton()->click();
        QVERIFY(rig.page->isChecking());
        QCOMPARE(rig.checkCalls, 1);
        QVERIFY(rig.check->started);
        // only the two mods with provider info, and each has its metadata (the checkers dereference it)
        QCOMPARE(rig.check->resources.size(), 2);
        for (const Resource::Ptr& resource : rig.check->resources) {
            QVERIFY(resource->metadata());
        }
        QCOMPARE(rig.page->checkButton()->text(), S("Checking\u2026"));
        QVERIFY(!rig.page->checkButton()->isEnabled());
        QCOMPARE(rig.page->captionLabel()->text(), S("Checking 2 mods\u2026"));
        QVERIFY(rig.page->listView()->rowDelegate()->locked());
        QVERIFY(!rig.page->banner()->isVisibleTo(rig.page.get()));  // the lock of a check has no banner
        QVERIFY(!rig.page->isLocked());

        // nothing changes: no question, no file touched, not by the switch, the keys, the selection bar
        rig.answers = { 2, 1, 2 };
        rig.page->requestToggle({ rig.rowOf(S("Alpha")) }, ToggleAction::Disable);
        rig.page->requestRemove({ rig.rowOf(S("Beta")) });
        rig.page->selectRows({ 0 });
        QTest::keyClick(rig.page->listView(), Qt::Key_Space);
        QTest::keyClick(rig.page->listView(), Qt::Key_Delete);
        rig.page->selectRows({ 0, 1 });
        QVERIFY(!rig.page->selectionEnableButton()->isEnabled());
        QVERIFY(!rig.page->selectionDisableButton()->isEnabled());
        QVERIFY(!rig.page->selectionRemoveButton()->isEnabled());
        QVERIFY(rig.prompts.isEmpty());
        QVERIFY(rig.trashCalls.isEmpty());
        QVERIFY(QFile::exists(rig.mods.filePath(S("alpha.jar"))));
        QVERIFY(QFile::exists(rig.mods.filePath(S("beta.jar"))));
        // a second check does not start
        rig.page->startCheck();
        QCOMPARE(rig.checkCalls, 1);

        // the answer unlocks
        rig.finishCheck({});
        QVERIFY(!rig.page->isChecking());
        QVERIFY(!rig.page->listView()->rowDelegate()->locked());
        rig.answers.clear();
        rig.page->requestToggle({ rig.rowOf(S("Alpha")) }, ToggleAction::Disable);
        QVERIFY(QFile::exists(rig.mods.filePath(S("alpha.jar.disabled"))));
    }

    void resultsShowPillsSegmentCaptionAndTheInfoLine()
    {
        Rig rig;
        rig.addMetaMods();
        rig.build();
        QVERIFY(rig.open(3));
        rig.page->startCheck();
        rig.finishCheck({ update(S("alpha.jar"), S("Alpha"), S("0.6.14"), S("Beta")) },
                        { { S("beta.jar"), S("No valid version found for this resource.") } });
        QVERIFY(!rig.page->isChecking());
        QCOMPARE(rig.page->updateCount(), 1);

        // the button, the caption, the segment, the overflow entry
        QCOMPARE(rig.page->checkButton()->text(), S("Update 1 mod"));
        QVERIFY(rig.page->checkButton()->isEnabled());
        QCOMPARE(rig.page->captionLabel()->text(), S("3 mods \u00b7 1 update"));
        QCOMPARE(rig.page->filterControl()->count(), 5);
        QCOMPARE(rig.page->filterControl()->valueAt(4), S("updates"));
        QStringList menu;
        for (const QAction* action : rig.page->overflowButton()->menu()->actions()) {
            menu.append(action->text());
        }
        QVERIFY(menu.contains(S("Check for updates again")));

        // the pill of the row, the failure in the tooltip of the other
        const Row& alpha = rig.rowNamed(S("Alpha"));
        QCOMPARE(alpha.updateVersion, S("0.6.14"));
        const QList<PillSpec> pills = pillsFor(alpha, S("1.21.1"));
        QCOMPARE(pills.last().text, S("Update \u2192 0.6.14"));
        QCOMPARE(pills.last().tone, PillTone::Accent);
        QVERIFY(rowToolTip(alpha).contains(S("(Beta)")));
        QVERIFY(rig.rowNamed(S("Beta")).checkError.contains(S("No valid version")));
        QVERIFY(rowToolTip(rig.rowNamed(S("Beta"))).contains(S("Couldn't check for updates: No valid version")));
        QVERIFY(!rig.rowNamed(S("Gamma")).hasUpdate());

        // the mod without provider info
        QVERIFY(rig.page->infoLine()->isVisibleTo(rig.page.get()));
        QCOMPARE(rig.page->infoLabel()->text(), S("1 mod has no provider info and can't be checked."));
        QCOMPARE(ZeroToast::count(rig.page->window()), 0);  // an update was found: no toast

        // the Updates segment lists only the mod with an update
        rig.page->setFilter(Filter::Updates);
        QCOMPARE(rig.names(), QStringList{ S("Alpha") });
        QCOMPARE(rig.page->captionLabel()->text(), S("1 of 3 mods"));
        // another check starts again from the menu entry: results go, the filter falls back
        rig.page->checkAgainAction()->trigger();
        QCOMPARE(rig.checkCalls, 2);
        QCOMPARE(rig.page->filter(), Filter::All);
        QCOMPARE(rig.page->filterControl()->count(), 4);
        QVERIFY(!rig.page->infoLine()->isVisibleTo(rig.page.get()));  // gone until the next answer
        rig.finishCheck({});
        QVERIFY(rig.page->infoLine()->isVisibleTo(rig.page.get()));
    }

    void noUpdatesIsAToastAndTheButtonStays()
    {
        Rig rig;
        rig.addMetaMods();
        rig.build();
        QVERIFY(rig.open(3));
        rig.page->startCheck();
        rig.finishCheck({});
        QCOMPARE(toastTexts(rig.page->window()), QStringList{ S("All mods are up to date") });
        QCOMPARE(rig.page->checkButton()->text(), S("Check for updates"));
        QCOMPARE(rig.page->filterControl()->count(), 4);
        QVERIFY(!rig.page->overflowButton()->menu()->actions().contains(rig.page->checkAgainAction()));
        QCOMPARE(rig.page->captionLabel()->text(), S("3 mods"));
        // still told about the mod nobody can check
        QVERIFY(rig.page->infoLine()->isVisibleTo(rig.page.get()));
    }

    void infoLineSaysHowManyAndGoesWhenThereAreNone()
    {
        Rig rig;
        rig.addJar(S("a.jar"), S("a"), S("A"), S("1.0"));
        rig.addJar(S("b.jar"), S("b"), S("B"), S("1.0"));
        rig.addJar(S("c.jar"), S("c"), S("C"), S("1.0"));
        rig.addJar(S("d.jar"), S("d"), S("D"), S("1.0"));
        rig.addMetadata(S("d"), S("D"), S("d.jar"), S("1.20.1"), S("PD"), S("hd"));
        rig.build();
        QVERIFY(rig.open(4));
        rig.page->startCheck();
        rig.finishCheck({});
        QCOMPARE(rig.page->infoLabel()->text(), S("3 mods have no provider info and can't be checked."));
        // the metadata of one mod is removed ... and the folder holds only mods with provider info next time: simulated by removing the jars
        QFile::remove(rig.mods.filePath(S("a.jar")));
        QFile::remove(rig.mods.filePath(S("b.jar")));
        QFile::remove(rig.mods.filePath(S("c.jar")));
        QVERIFY(rig.settle());
        QTRY_COMPARE(rig.page->listModel()->rowCount(), 1);
        rig.page->startCheck();
        QVERIFY(!rig.page->infoLine()->isVisibleTo(rig.page.get()));  // hidden while the next check runs
        rig.finishCheck({});
        QVERIFY(!rig.page->infoLine()->isVisibleTo(rig.page.get()));  // and 0 mods without info: it stays away
    }

    void nothingToCheckStartsNoCheck()
    {
        Rig rig;
        rig.addJar(S("a.jar"), S("a"), S("A"), S("1.0"));
        rig.addJar(S("b.jar"), S("b"), S("B"), S("1.0"));
        rig.build();
        QVERIFY(rig.open(2));
        rig.page->checkButton()->click();
        QCOMPARE(rig.checkCalls, 0);  // never a check without a single mod to ask about
        QVERIFY(!rig.page->isChecking());
        QCOMPARE(rig.page->infoLabel()->text(), S("2 mods have no provider info and can't be checked."));
        QCOMPARE(toastTexts(rig.page->window()), QStringList{ S("None of these mods can be checked for updates") });
    }

    void lockedAndCurseForgeModsAreHandledOnPurpose()
    {
        Rig rig;
        rig.addJar(S("a.jar"), S("a"), S("A"), S("1.0"));
        rig.addJar(S("b.jar"), S("b"), S("B"), S("1.0"));
        rig.addJar(S("c.jar"), S("c"), S("C"), S("1.0"));
        rig.addJar(S("d.jar"), S("d"), S("D"), S("1.0"));
        rig.addMetadata(S("a"), S("A"), S("a.jar"), S("1.20.1"), S("PA"), S("ha"));
        rig.addMetadata(S("b"), S("B"), S("b.jar"), S("1.20.1"), S("PB"), S("hb"), ModPlatform::ResourceProvider::MODRINTH, true);  // locked
        rig.addMetadata(S("c"), S("C"), S("c.jar"), S("1.20.1"), S("12345"), S("hc"), ModPlatform::ResourceProvider::FLAME);
        rig.addMetadata(S("d"), S("D"), S("d.jar"), S("1.20.1"), S("not-a-number"), S("hd"), ModPlatform::ResourceProvider::FLAME);
        rig.build();
        QVERIFY(rig.open(4));
        QVERIFY(rig.rowNamed(S("B")).updateLocked);
        QVERIFY(!rig.rowNamed(S("B")).checkable);
        QVERIFY(rig.rowNamed(S("C")).checkable);
        QVERIFY(!rig.rowNamed(S("D")).checkable);  // packwiz cannot even store a CurseForge entry without a numeric project id

        // with CurseForge: A and C are asked; B is locked on purpose, D has no usable id
        rig.page->startCheck();
        QCOMPARE(rig.check->resources.size(), 2);
        rig.finishCheck({});
        QCOMPARE(rig.page->infoLabel()->text(), S("1 mod has no provider info and can't be checked."));

        // without it only A
        rig.curseForge = false;
        rig.page->startCheck();
        QCOMPARE(rig.check->resources.size(), 1);
        rig.finishCheck({});
        QCOMPARE(rig.page->infoLabel()->text(), S("2 mods have no provider info and can't be checked."));
    }

    void everyModFailingIsAnErrorWithoutASegment()
    {
        Rig rig;
        rig.addMetaMods();
        rig.build();
        QVERIFY(rig.open(3));
        rig.page->startCheck();
        rig.finishCheck({}, { { S("alpha.jar"), S("Offline") }, { S("beta.jar"), S("Other") } });
        QCOMPARE(toastTexts(rig.page->window()), QStringList{ S("Couldn't check for updates: Offline") });
        QCOMPARE(rig.page->filterControl()->count(), 4);
        QCOMPARE(rig.page->updateCount(), 0);
        QCOMPARE(rig.page->checkButton()->text(), S("Check for updates"));
        QCOMPARE(rig.rowNamed(S("Alpha")).checkError, S("Offline"));  // each row still says why

        // some failed, some are simply up to date: no "up to date" claim
        rig.page->startCheck();
        rig.finishCheck({}, { { S("alpha.jar"), S("Offline") } });
        QVERIFY(!toastTexts(rig.page->window()).contains(S("All mods are up to date")));
    }

    void aCheckThatFailsAsAWholeIsAnError()
    {
        Rig rig;
        rig.addMetaMods();
        rig.build();
        QVERIFY(rig.open(3));
        rig.page->startCheck();
        rig.finishCheck({}, {}, false, S("The network is down"));
        QVERIFY(!rig.page->isChecking());
        QVERIFY(!rig.page->listView()->rowDelegate()->locked());
        QCOMPARE(toastTexts(rig.page->window()), QStringList{ S("Couldn't check for updates: The network is down") });
        QCOMPARE(rig.page->filterControl()->count(), 4);
        QCOMPARE(rig.page->checkButton()->text(), S("Check for updates"));
    }

    void aReloadDuringTheCheckAbortsItAndLateAnswersAreIgnored()
    {
        Rig rig;
        rig.addMetaMods();
        rig.build();
        QVERIFY(rig.open(3));

        // a reload that removes a file: the check goes BEFORE the model drops the row (rowsAboutToBeRemoved), and whatever the aborted
        // check still says, even from inside that very signal (our slot runs after the page's), is ignored
        rig.page->startCheck();
        QCOMPARE(rig.checkCalls, 1);
        FakeCheck* late = rig.check.data();
        bool abortedBeforeTheRowWentAway = false;
        int abortCallsThen = -1;
        bool fired = false;
        connect(rig.model.get(), &QAbstractItemModel::rowsAboutToBeRemoved, rig.model.get(), [&]() {
            if (fired) {
                return;
            }
            fired = true;
            abortedBeforeTheRowWentAway = !rig.page->isChecking() && late->aborted;
            abortCallsThen = rig.abortCalls;
            late->result = { update(S("alpha.jar"), S("Alpha"), S("9.9")) };
            emit late->modFailed(S("beta.jar"), S("late"));
            emit late->finished(true, QString());
        });
        QFile::remove(rig.mods.filePath(S("gamma.jar")));
        QVERIFY(rig.settle());
        QVERIFY(fired);
        QVERIFY(abortedBeforeTheRowWentAway);
        QCOMPARE(abortCallsThen, 1);
        QVERIFY(!rig.page->isChecking());
        QCOMPARE(rig.abortCalls, 1);
        QVERIFY(!rig.page->listView()->rowDelegate()->locked());
        QCOMPARE(rig.page->updateCount(), 0);
        QCOMPARE(ZeroToast::count(rig.page->window()), 0);
        QCOMPARE(rig.page->checkButton()->text(), S("Check for updates"));
        QVERIFY(!rig.rowNamed(S("Alpha")).hasUpdate());
        QVERIFY(rig.rowNamed(S("Beta")).checkError.isEmpty());

        // a real reload that replaces a checked file (the model swaps its object: no "about to" signal, only dataChanged)
        rig.page->startCheck();
        QCOMPARE(rig.checkCalls, 2);
        const QString alphaPath = rig.mods.filePath(S("alpha.jar"));
        QVERIFY(QFile::remove(alphaPath));
        rig.addJar(S("alpha.jar"), S("alpha"), S("Alpha"), S("1.0.1-a-longer-version"));
        {
            QFile file(alphaPath);
            QVERIFY(file.open(QIODevice::ReadWrite));
            QVERIFY(file.setFileTime(QDateTime::currentDateTime().addSecs(30), QFileDevice::FileModificationTime));
        }
        // add_metadata is untouched: the jar under the same name keeps its packwiz entry
        QVERIFY(rig.settle());
        QTRY_VERIFY_WITH_TIMEOUT(!rig.page->isChecking(), 5000);
        QCOMPARE(rig.abortCalls, 2);
    }

    void closingTheWindowDuringTheCheckAbortsIt()
    {
        Rig rig;
        rig.addMetaMods();
        rig.build();
        QVERIFY(rig.open(3));
        rig.page->startCheck();
        FakeCheck* late = rig.check.data();
        rig.page->closed();
        QCOMPARE(rig.abortCalls, 1);
        QVERIFY(!rig.page->isChecking());
        late->result = { update(S("alpha.jar"), S("Alpha"), S("9.9")) };
        emit late->finished(true, QString());
        QCOMPARE(rig.page->updateCount(), 0);
        QCOMPARE(ZeroToast::count(rig.page->window()), 0);
    }

    void destroyingThePageDuringTheCheckAbortsIt()
    {
        Rig rig;
        rig.addMetaMods();
        rig.build();
        QVERIFY(rig.open(3));
        rig.page->startCheck();
        QVERIFY(rig.page->isChecking());
        rig.page.reset();
        QCOMPARE(rig.abortCalls, 1);
    }

    void theGameStartingStopsTheCheckAndHidesTheButton()
    {
        Rig rig;
        rig.addMetaMods();
        rig.build();
        QVERIFY(rig.open(3));
        rig.page->startCheck();
        rig.running = true;
        rig.page->refreshRunning();
        QVERIFY(!rig.page->isChecking());
        QCOMPARE(rig.abortCalls, 1);
        QVERIFY(!rig.page->checkButton()->isVisibleTo(rig.page.get()));  // the running lock wins: hidden
        QVERIFY(rig.page->banner()->isVisibleTo(rig.page.get()));
        rig.page->startCheck();  // refused
        QCOMPARE(rig.checkCalls, 1);
        rig.running = false;
        rig.page->refreshRunning();
        QVERIFY(rig.page->checkButton()->isVisibleTo(rig.page.get()));
    }

    void resultsSurviveAReloadUntilTheFileChanges()
    {
        Rig rig;
        rig.addMetaMods();
        rig.build();
        QVERIFY(rig.open(3));
        rig.page->startCheck();
        rig.finishCheck({ update(S("alpha.jar"), S("Alpha"), S("0.6.14")) });
        QVERIFY(rig.rowNamed(S("Alpha")).hasUpdate());

        // a reload that changes nothing keeps the pill
        QVERIFY(rig.settle());
        QVERIFY(rig.rowNamed(S("Alpha")).hasUpdate());
        QCOMPARE(rig.page->updateCount(), 1);

        // switching the mod off renames its file: the answer is about the mod, not the name
        rig.page->requestToggle({ rig.rowOf(S("Alpha")) }, ToggleAction::Disable);
        QVERIFY(rig.settle());
        QVERIFY(QFile::exists(rig.mods.filePath(S("alpha.jar.disabled"))));
        QTRY_VERIFY_WITH_TIMEOUT(rig.rowNamed(S("Alpha")).hasUpdate(), 5000);
        QCOMPARE(rig.page->filterControl()->count(), 5);

        // another file under that name: the answer is dropped, the segment goes, the filter falls back
        rig.page->setFilter(Filter::Updates);
        QCOMPARE(rig.names(), QStringList{ S("Alpha") });
        const QString path = rig.mods.filePath(S("alpha.jar.disabled"));
        QVERIFY(QFile::remove(path));
        rig.addJar(S("alpha.jar.disabled"), S("alpha"), S("Alpha"), S("1.0.1-a-longer-version"));
        QVERIFY(rig.settle());
        QTRY_COMPARE_WITH_TIMEOUT(rig.page->updateCount(), 0, 5000);
        QCOMPARE(rig.page->filterControl()->count(), 4);
        QCOMPARE(rig.page->filter(), Filter::All);
        QCOMPARE(rig.page->checkButton()->text(), S("Check for updates"));
    }

    void applyingAllRunsTheDownloadsAndReadsTheFolderAgain()
    {
        Rig rig;
        rig.addMetaMods();
        rig.build();
        QVERIFY(rig.open(3));
        rig.page->startCheck();
        rig.finishCheck({ update(S("alpha.jar"), S("Alpha"), S("0.6.14")), update(S("beta.jar"), S("Beta"), S("2.0")) });
        QCOMPARE(rig.page->checkButton()->text(), S("Update all 2"));

        bool lockedWhileApplying = false;
        bool toggledWhileApplying = false;
        rig.onApply = [&]() {
            lockedWhileApplying = rig.page->listView()->rowDelegate()->locked() && !rig.page->checkButton()->isEnabled();
            rig.answers = { 2 };
            rig.page->requestToggle({ rig.rowOf(S("Alpha")) }, ToggleAction::Disable);
            toggledWhileApplying = QFile::exists(rig.mods.filePath(S("alpha.jar.disabled")));
            rig.page->requestUpdateAll();  // not re-entrant
        };
        QSignalSpy reads(rig.model.get(), &ResourceFolderModel::updateFinished);
        rig.page->checkButton()->click();
        QCOMPARE(rig.applyCalls, 1);
        QCOMPARE(rig.applied.size(), 2);
        QVERIFY(lockedWhileApplying);
        QVERIFY(!toggledWhileApplying);
        QVERIFY(reads.count() > 0 || reads.wait(10000));  // the folder is read again
        QCOMPARE(rig.page->updateCount(), 0);              // what was applied leaves the results
        QCOMPARE(rig.page->filterControl()->count(), 4);
        QCOMPARE(rig.page->checkButton()->text(), S("Check for updates"));
        QVERIFY(!rig.page->listView()->rowDelegate()->locked());
        QCOMPARE(toastTexts(rig.page->window()), QStringList{ S("Updated 2 mods") });
        QVERIFY(QFile::exists(rig.mods.filePath(S("alpha.jar"))));  // this page deleted nothing
    }

    void applyingOneOrASelectionAppliesOnlyThose()
    {
        Rig rig;
        rig.addMetaMods();
        rig.build();
        QVERIFY(rig.open(3));
        rig.page->startCheck();
        rig.finishCheck({ update(S("alpha.jar"), S("Alpha"), S("0.6.14")), update(S("beta.jar"), S("Beta"), S("2.0")) });

        // the selection bar offers Update when a selected row has one; it applies those rows only
        rig.page->selectRows({ rig.rowOf(S("Alpha")), rig.rowOf(S("Gamma")) });
        QVERIFY(rig.page->selectionUpdateButton()->isVisibleTo(rig.page.get()));
        rig.page->selectRows({ rig.rowOf(S("Alpha")), rig.rowOf(S("Gamma")) });
        rig.page->selectionUpdateButton()->click();
        QCOMPARE(rig.applyCalls, 1);
        QCOMPARE(rig.applied.size(), 1);
        QCOMPARE(rig.applied.first().key, S("alpha.jar"));
        QCOMPARE(toastTexts(rig.page->window()), QStringList{ S("Updated Alpha to 0.6.14") });
        QTRY_COMPARE_WITH_TIMEOUT(rig.page->updateCount(), 1, 5000);  // Beta is still there
        QCOMPARE(rig.page->checkButton()->text(), S("Update 1 mod"));
        QVERIFY(!rig.rowNamed(S("Alpha")).hasUpdate());
        QVERIFY(rig.rowNamed(S("Beta")).hasUpdate());

        // a selection with nothing to update has no Update button
        rig.page->selectRows({ rig.rowOf(S("Alpha")), rig.rowOf(S("Gamma")) });
        QVERIFY(!rig.page->selectionUpdateButton()->isVisibleTo(rig.page.get()));

        // a single row
        rig.page->requestUpdate({ rig.rowOf(S("Beta")) });
        QCOMPARE(rig.applyCalls, 2);
        QCOMPARE(rig.applied.first().key, S("beta.jar"));
        QTRY_COMPARE_WITH_TIMEOUT(rig.page->updateCount(), 0, 5000);
        rig.page->requestUpdate({ 0 });  // nothing left: nothing runs
        QCOMPARE(rig.applyCalls, 2);
    }

    void aFailedUpdateReportsNoSuccessAndLeavesNoStaleEntry()
    {
        Rig rig;
        rig.addMetaMods();
        rig.build();
        QVERIFY(rig.open(3));
        rig.page->startCheck();
        rig.finishCheck({ update(S("alpha.jar"), S("Alpha"), S("0.6.14")) });
        rig.applyOk = false;
        QSignalSpy reads(rig.model.get(), &ResourceFolderModel::updateFinished);
        rig.page->requestUpdateAll();
        QCOMPARE(rig.applyCalls, 1);
        QVERIFY(reads.count() > 0 || reads.wait(10000));  // read again after a failure too: whatever arrived shows up
        QVERIFY(toastTexts(rig.page->window()).isEmpty());  // no "Updated" for a failure (Prism reports the error itself)
        QCOMPARE(rig.page->updateCount(), 0);               // the used-up download is not offered again: check again
        QVERIFY(QFile::exists(rig.mods.filePath(S("alpha.jar"))));
    }

    void theRunningLockRefusesUpdates()
    {
        Rig rig;
        rig.addMetaMods();
        rig.build();
        QVERIFY(rig.open(3));
        rig.page->startCheck();
        rig.finishCheck({ update(S("alpha.jar"), S("Alpha"), S("0.6.14")) });
        QCOMPARE(rig.page->updateCount(), 1);

        rig.running = true;
        rig.page->refreshRunning();
        QVERIFY(!rig.page->checkButton()->isVisibleTo(rig.page.get()));
        rig.page->requestUpdateAll();
        rig.page->requestUpdate({ rig.rowOf(S("Alpha")) });
        rig.page->selectRows({ rig.rowOf(S("Alpha")), rig.rowOf(S("Beta")) });
        QVERIFY(!rig.page->selectionUpdateButton()->isEnabled());
        QCOMPARE(rig.applyCalls, 0);
        QVERIFY(!rig.page->overflowButton()->menu()->actions().contains(rig.page->checkAgainAction()));

        rig.running = false;
        rig.page->refreshRunning();
        QVERIFY(rig.page->checkButton()->isVisibleTo(rig.page.get()));
        rig.page->requestUpdateAll();
        QCOMPARE(rig.applyCalls, 1);
    }

    // ---- safe update (zero/mods/ModSnapshot) ---------------------------------------------------------------------------------------

    /// A page over Alpha / Beta / Gamma with snapshots in `<root>/snaps`; the fake apply "updates" Alpha the way Prism does (the old jar
    /// goes, the new one arrives, the entry is rewritten). Leaves the update found by a check.
    static void prepareSafeUpdate(Rig& rig)
    {
        rig.addMetaMods();
        rig.snapshotsPath = rig.root.path() + S("/snaps");
        rig.onApply = [&rig]() {
            QFile::remove(rig.mods.filePath(S("alpha.jar")));
            rig.addJar(S("alpha-0.6.14.jar"), S("alpha"), S("Alpha"), S("0.6.14"));
            rig.addMetadata(S("alpha"), S("Alpha"), S("alpha-0.6.14.jar"), S("1.20.1"), S("PA"), S("h9"));
        };
        rig.build();
    }

    static QPushButton* toastAction(QWidget* window, const QString& text)
    {
        for (ZeroToast* toast : window->findChildren<ZeroToast*>()) {
            for (QPushButton* button : toast->findChildren<QPushButton*>()) {
                if (button->text() == text) {
                    return button;
                }
            }
        }
        return nullptr;
    }

    /// Opens the page and runs the update of Alpha (and Beta when `withBeta`) through the page.
    static void runUpdate(Rig& rig, bool withBeta = false)
    {
        QVERIFY(rig.open(3));
        rig.page->startCheck();
        QList<ModUpdate> found = { update(S("alpha.jar"), S("Alpha"), S("0.6.14")) };
        if (withBeta) {
            found.append(update(S("beta.jar"), S("Beta"), S("2.0")));
        }
        rig.finishCheck(found);
        rig.page->requestUpdateAll();
    }

    void anUpdateSavesACopyFirstAndOffersUndo()
    {
        Rig rig;
        prepareSafeUpdate(rig);
        runUpdate(rig);
        QCOMPARE(rig.applyCalls, 1);

        // the copy: Alpha's jar and its entry, nothing else; the update is recorded
        const QList<Zero::Mods::Snapshot::Info> all = Zero::Mods::Snapshot::listSnapshots(rig.snapshotsPath);
        QCOMPARE(all.size(), 1);
        const auto& info = all.first();
        QVERIFY2(info.valid, qPrintable(info.error));
        QCOMPARE(info.outcome, S("ok"));
        QSet<QString> paths;
        for (const auto& f : info.files) {
            paths.insert(f.path);
        }
        QCOMPARE(paths, QSet<QString>({ S("alpha.jar"), S(".index/alpha.pw.toml") }));
        QCOMPARE(info.mods.first().oldVersion, S("1.0"));
        QCOMPARE(info.mods.first().newVersion, S("0.6.14"));
        QVERIFY(info.added.contains(S("alpha-0.6.14.jar")));
        QVERIFY(info.mods.first().changed);

        // the toast with Undo, and the strip
        QVERIFY(toastAction(rig.page.get(), S("Undo")));
        QVERIFY(toastTexts(rig.page.get()).join(QLatin1Char('|')).contains(S("Updated Alpha to 0.6.14")));
        QVERIFY(rig.page->snapshotNotice()->isVisibleTo(rig.page.get()));
        QVERIFY(rig.page->snapshotNotice()->label()->text().startsWith(S("Updated 1 mod")));
        QVERIFY(rig.page->snapshotNotice()->rollbackButton()->isEnabled());
    }

    void undoRollsTheModBackAfterAskingAndPutsTheNewFileInTheBin()
    {
        Rig rig;
        prepareSafeUpdate(rig);
        runUpdate(rig);
        QPushButton* undo = toastAction(rig.page.get(), S("Undo"));
        QVERIFY(undo);

        rig.answers = { 1 };
        undo->click();
        QCOMPARE(rig.prompts.size(), 1);
        QVERIFY(rig.prompts.first().title.contains(S("Roll back Alpha to the version you had before?")));
        QCOMPARE(rig.prompts.first().list, QStringList({ S("Alpha   1.0 ← 0.6.14") }));
        QVERIFY(rig.prompts.first().note.contains(S("Recycle Bin")));
        QVERIFY(QFile::exists(rig.mods.filePath(S("alpha.jar"))));
        QVERIFY(!QFile::exists(rig.mods.filePath(S("alpha-0.6.14.jar"))));
        QVERIFY(rig.trashCalls.contains(S("alpha-0.6.14.jar")));
        QVERIFY(toastTexts(rig.page.get()).join(QLatin1Char('|')).contains(S("Rolled back Alpha")));
        QVERIFY(!rig.page->snapshotNotice()->isVisibleTo(rig.page.get()));
        QVERIFY(Zero::Mods::Snapshot::readSnapshot(Zero::Mods::Snapshot::listSnapshots(rig.snapshotsPath).first().dir).rolledBack);
        // the folder is read again: Alpha 1.0 is listed, not 0.6.14
        QTRY_VERIFY_WITH_TIMEOUT(rig.rowNamed(S("Alpha")).version == S("1.0"), 8000);
    }

    /// Alpha as the model holds it: its jar, and the provider info the update check needs.
    static Resource::Ptr alphaOf(Rig& rig)
    {
        const int row = rig.rowOf(S("Alpha"));
        return row < 0 ? Resource::Ptr() : rig.model->find(rig.page->listModel()->rowAt(row).id);
    }

    /// The bug of owner QA: after a roll back the files were all back, but the model had read the folder with the jar there and its entry
    /// not yet (the jars go back first) and kept that reading, so the mods "had no provider info" and the check skipped them. The roll back
    /// is slowed after every jar so the model certainly reads the folder in between (on a real disk and Recycle Bin that is what happens).
    void aRollBackReadMidwayKeepsTheProviderInfoOfTheMods()
    {
        Rig rig;
        rig.snapshotHooks.rename = [](const QString& from, const QString& to) {
            const bool ok = QFile::rename(from, to);
            if (to.endsWith(S(".jar"))) {
                QThread::msleep(900);
            }
            return ok;
        };
        prepareSafeUpdate(rig);
        runUpdate(rig);
        QVERIFY(alphaOf(rig));
        QVERIFY(alphaOf(rig)->metadata());  // the update wrote the entry
        rig.answers = { 1 };
        rig.page->requestRollback();

        // the files: the old jar and the old entry (h1), not the new ones
        QVERIFY(QFile::exists(rig.mods.filePath(S("alpha.jar"))));
        QVERIFY(QFile::exists(rig.mods.filePath(S(".index/alpha.pw.toml"))));
        QVERIFY(!QFile::exists(rig.mods.filePath(S("alpha-0.6.14.jar"))));
        // the model: read again, with the entry
        QVERIFY(rig.settle());
        QVERIFY(alphaOf(rig));
        QVERIFY2(alphaOf(rig)->metadata(), "Alpha lost its provider info after the roll back");
        QCOMPARE(alphaOf(rig)->metadata()->hash, S("h1"));
        QVERIFY(alphaOf(rig)->metadata()->projectId == QVariant(S("PA")));
        QVERIFY(rig.rowNamed(S("Alpha")).version == S("1.0"));
    }

    void afterARollBackCheckingForUpdatesFindsTheUpdateAgain()
    {
        Rig rig;
        rig.snapshotHooks.rename = [](const QString& from, const QString& to) {
            const bool ok = QFile::rename(from, to);
            if (to.endsWith(S(".jar"))) {
                QThread::msleep(900);
            }
            return ok;
        };
        prepareSafeUpdate(rig);
        runUpdate(rig, true);
        rig.answers = { 1 };
        rig.page->requestRollback();
        QVERIFY(rig.settle());

        const int before = rig.checkCalls;
        rig.page->startCheck();
        QCOMPARE(rig.checkCalls, before + 1);  // there is something to check: the mods have their provider info
        QVERIFY(rig.check);
        QSet<QString> checked;
        for (const Resource::Ptr& resource : rig.check->resources) {
            QVERIFY(resource->metadata());
            checked.insert(resource->name());
        }
        QVERIFY(checked.contains(S("Alpha")));
        QVERIFY(checked.contains(S("Beta")));
        rig.finishCheck({ update(S("alpha.jar"), S("Alpha"), S("0.6.14")), update(S("beta.jar"), S("Beta"), S("2.0")) });
        QCOMPARE(rig.page->updateCount(), 2);
        QCOMPARE(checked.size(), 2);  // Gamma is a plain jar: the only mod without provider info
    }

    /// The update may write the entry under another file name (Prism does when the slug differs): the old entry is in the snapshot, the
    /// new one is trashed, and the mod ends with the entry it had.
    void aRollBackRestoresTheEntryWhenTheUpdateWroteItUnderAnotherName()
    {
        Rig rig;
        prepareSafeUpdate(rig);
        rig.onApply = [&rig]() {
            QFile::remove(rig.mods.filePath(S("alpha.jar")));
            QFile::remove(rig.mods.filePath(S(".index/alpha.pw.toml")));
            rig.addJar(S("alpha-0.6.14.jar"), S("alpha"), S("Alpha"), S("0.6.14"));
            rig.addMetadata(S("alpha-fabric"), S("Alpha"), S("alpha-0.6.14.jar"), S("1.20.1"), S("PA"), S("h9"));
        };
        runUpdate(rig);
        const auto info = Zero::Mods::Snapshot::listSnapshots(rig.snapshotsPath).first();
        QSet<QString> paths;
        for (const auto& f : info.files) {
            paths.insert(f.path);
        }
        QVERIFY(paths.contains(S(".index/alpha.pw.toml")));              // the old entry was copied
        QVERIFY(info.added.contains(S(".index/alpha-fabric.pw.toml")));  // the new one is recorded

        rig.answers = { 1 };
        rig.page->requestRollback();
        QVERIFY(QFile::exists(rig.mods.filePath(S(".index/alpha.pw.toml"))));
        QVERIFY(!QFile::exists(rig.mods.filePath(S(".index/alpha-fabric.pw.toml"))));
        QVERIFY(rig.trashCalls.contains(S("alpha-fabric.pw.toml")));
        QVERIFY(rig.settle());
        QVERIFY(alphaOf(rig));
        QVERIFY(alphaOf(rig)->metadata());
        QCOMPARE(alphaOf(rig)->metadata()->hash, S("h1"));
    }

    void theStripRollsBackAndCancelChangesNothing()
    {
        Rig rig;
        prepareSafeUpdate(rig);
        runUpdate(rig);
        QVERIFY(rig.page->snapshotNotice()->isVisibleTo(rig.page.get()));
        rig.trashCalls.clear();
        rig.answers = { 0 };  // Cancel
        rig.page->snapshotNotice()->rollbackButton()->click();
        QCOMPARE(rig.prompts.size(), 1);
        QVERIFY(rig.trashCalls.isEmpty());
        QVERIFY(QFile::exists(rig.mods.filePath(S("alpha-0.6.14.jar"))));
        QVERIFY(rig.page->snapshotNotice()->isVisibleTo(rig.page.get()));  // still offered
        rig.answers = { 1 };
        rig.page->snapshotNotice()->rollbackButton()->click();
        QVERIFY(QFile::exists(rig.mods.filePath(S("alpha.jar"))));
    }

    void whenTheCopyCannotBeMadeNothingIsUpdated()
    {
        Rig rig;
        prepareSafeUpdate(rig);
        QFile blocker(rig.root.path() + S("/blocker"));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        rig.snapshotsPath = rig.root.path() + S("/blocker/snaps");  // a file where the folder should go
        runUpdate(rig);
        QCOMPARE(rig.applyCalls, 0);  // the update did not run
        QVERIFY(QFile::exists(rig.mods.filePath(S("alpha.jar"))));
        QVERIFY(toastTexts(rig.page.get()).join(QLatin1Char('|')).contains(S("Could not save a copy of your mods first, so nothing was updated")));
        QVERIFY(!rig.page->snapshotNotice()->isVisibleTo(rig.page.get()));
        QCOMPARE(rig.page->updateCount(), 1);  // still offered: nothing was used up
        rig.page->requestUpdateAll();          // and the page is not stuck
        QCOMPARE(rig.applyCalls, 0);
    }

    void anUpdateThatChangedNothingLeavesNoSnapshot()
    {
        Rig rig;
        prepareSafeUpdate(rig);
        rig.onApply = nullptr;  // the downloads fail before any file moves
        rig.applyOk = false;
        runUpdate(rig);
        QCOMPARE(rig.applyCalls, 1);
        QVERIFY(Zero::Mods::Snapshot::listSnapshots(rig.snapshotsPath).isEmpty());
        QVERIFY(!rig.page->snapshotNotice()->isVisibleTo(rig.page.get()));
        QVERIFY(toastTexts(rig.page.get()).isEmpty());
    }

    void aPartialUpdateStillOffersTheWayBack()
    {
        Rig rig;
        prepareSafeUpdate(rig);
        rig.applyOk = false;  // Prism reports the failure itself, but Alpha did arrive
        runUpdate(rig, true);
        QVERIFY(toastAction(rig.page.get(), S("Undo")));
        QVERIFY(rig.page->snapshotNotice()->isVisibleTo(rig.page.get()));
        const auto info = Zero::Mods::Snapshot::listSnapshots(rig.snapshotsPath).first();
        QCOMPARE(info.outcome, S("failed"));
        QCOMPARE(info.changedCount(), 1);  // only Alpha changed
        QCOMPARE(info.mods.size(), 2);     // Beta was copied too
    }

    void theRunningGameBlocksRollBack()
    {
        Rig rig;
        prepareSafeUpdate(rig);
        runUpdate(rig);
        QVERIFY(rig.page->snapshotNotice()->rollbackButton()->isEnabled());

        rig.running = true;
        rig.page->refreshRunning();
        QVERIFY(rig.page->snapshotNotice()->isVisibleTo(rig.page.get()));  // shown like the other locked controls, disabled
        QVERIFY(!rig.page->snapshotNotice()->rollbackButton()->isEnabled());
        QCOMPARE(rig.page->snapshotNotice()->rollbackButton()->toolTip(), S("Close the game first"));
        rig.answers = { 1 };
        rig.page->requestRollback();
        QVERIFY(rig.prompts.isEmpty());
        QVERIFY(QFile::exists(rig.mods.filePath(S("alpha-0.6.14.jar"))));  // nothing touched

        rig.running = false;
        rig.page->refreshRunning();
        QVERIFY(rig.page->snapshotNotice()->rollbackButton()->isEnabled());
    }

    void dismissHidesTheStripButKeepsTheSnapshot()
    {
        Rig rig;
        prepareSafeUpdate(rig);
        runUpdate(rig);
        const QString dir = rig.page->snapshot().dir;
        rig.page->snapshotNotice()->dismissButton()->click();
        QVERIFY(!rig.page->snapshotNotice()->isVisibleTo(rig.page.get()));
        QVERIFY(Zero::Mods::Snapshot::readSnapshot(dir).dismissed);
        QVERIFY(QFile::exists(dir + S("/files/alpha.jar")));  // kept for the retention
        rig.page->refreshSnapshot();
        QVERIFY(!rig.page->snapshotNotice()->isVisibleTo(rig.page.get()));
    }

    void aFailedRollBackSaysWhatHappenedAndKeepsTheSnapshot()
    {
        Rig rig;
        prepareSafeUpdate(rig);
        runUpdate(rig);
        rig.failTrash.insert(S("alpha-0.6.14.jar"));
        rig.answers = { 1, 0 };
        rig.page->requestRollback();
        QCOMPARE(rig.prompts.size(), 2);  // the question, then what happened
        QVERIFY(rig.prompts.last().body.contains(S("alpha-0.6.14.jar")));
        QVERIFY(rig.prompts.last().body.contains(S("Roll back again")));
        QVERIFY(QFile::exists(rig.mods.filePath(S("alpha-0.6.14.jar"))));  // the file the bin refused stays
        QVERIFY(rig.page->snapshotNotice()->isVisibleTo(rig.page.get()));   // still offered
        QVERIFY(rig.page->snapshotNotice()->rollbackButton()->isEnabled());  // and the page is not stuck
    }

    void theStripAppearsOnOpenForAnExistingSnapshot()
    {
        Rig rig;
        prepareSafeUpdate(rig);
        runUpdate(rig);
        rig.page.reset();
        rig.model.reset();
        rig.build();
        QVERIFY(rig.open(3));
        QVERIFY(rig.page->snapshotNotice()->isVisibleTo(rig.page.get()));
        QVERIFY(rig.page->snapshotNotice()->label()->text().contains(S("Updated 1 mod")));
    }

    void withoutASnapshotsFolderTheOldBehaviourStays()
    {
        Rig rig;  // snapshotsPath empty
        rig.addMetaMods();
        rig.build();
        QVERIFY(rig.open(3));
        rig.page->startCheck();
        rig.finishCheck({ update(S("alpha.jar"), S("Alpha"), S("0.6.14")) });
        rig.page->requestUpdateAll();
        QCOMPARE(rig.applyCalls, 1);
        QVERIFY(!toastAction(rig.page.get(), S("Undo")));
        QVERIFY(!rig.page->snapshotNotice()->isVisibleTo(rig.page.get()));
    }

    void theRowMenuOffersTheUpdateFirst()
    {
        Rig rig;
        rig.addMetaMods();
        rig.build();
        QVERIFY(rig.open(3));
        rig.page->startCheck();
        rig.finishCheck({ update(S("alpha.jar"), S("Alpha"), S("0.6.14")) });
        // The menu is modal (exec), so it is looked at from a timer: the first entry reads "Update to 0.6.14"
        // The entries are read from the menu the page fills (showing it with exec() is flaky under parallel GUI tests: a popup
        // closes as soon as another test's window takes the activation).
        QMenu menu;
        rig.page->fillRowMenu(&menu, rig.rowOf(S("Alpha")));
        QVERIFY(!menu.actions().isEmpty());
        QCOMPARE(menu.actions().first()->text(), S("Update to 0.6.14"));
        QVERIFY(menu.actions().first()->isEnabled());
    }

    // ---- the Dependency Inspector (docs/zero/CONTENT-MODS.md) ----------------------------------------------------------------------

    void dependencyProblemsBecomePillsInTheirOwnOrder()
    {
        Row r = row(S("Iris"));
        r.depProblems = { { DepKind::Missing, { S("Indium") } },
                          { DepKind::Disabled, { S("Sodium") } },
                          { DepKind::WrongVersion, { S("Lithium") } },
                          { DepKind::Conflict, { S("OptiFabric") } } };
        r.source = Source::Modrinth;
        r.environment = S("Client");
        r.requiredBy = 2;
        const QList<PillSpec> pills = pillsFor(r, S("1.21.1"));
        QCOMPARE(pills.at(0).text, S("Needs Indium"));
        QCOMPARE(pills.at(0).tone, PillTone::Danger);
        QCOMPARE(pills.at(1).text, S("Needs Sodium \u00b7 disabled"));
        QCOMPARE(pills.at(1).tone, PillTone::Warning);
        QCOMPARE(pills.at(2).text, S("Wrong Lithium version"));
        QCOMPARE(pills.at(2).tone, PillTone::Warning);
        QCOMPARE(pills.at(3).text, S("Conflicts with OptiFabric"));
        QCOMPARE(pills.at(3).tone, PillTone::Danger);
        for (int i = 0; i < 4; ++i) {
            QVERIFY(pills.at(i).priority >= kKeepPriority);  // they outrank provider / environment / Required by
            QCOMPARE(pills.at(i).maxWidth, kDepPillMaxWidth);
        }
        for (int i = 4; i < pills.size(); ++i) {
            QVERIFY(pills.at(i).priority < kKeepPriority);
        }
        // several names: the first one and how many more
        QCOMPARE(depPillText({ DepKind::Missing, { S("Indium"), S("Sodium"), S("Lithium") } }), S("Needs Indium +2"));
        QCOMPARE(depPillText({ DepKind::Disabled, { S("Sodium"), S("Lithium") } }), S("Needs Sodium \u00b7 disabled +1"));
        QCOMPARE(depPillText({ DepKind::WrongVersion, { S("A"), S("B") } }), S("Wrong A version +1"));
        QCOMPARE(depPillText({ DepKind::Conflict, { S("A"), S("B") } }), S("Conflicts with A +1"));
        QCOMPARE(depFullText({ DepKind::Missing, { S("Indium"), S("Sodium") } }), S("Needs Indium, Sodium"));
        QVERIFY(rowToolTip(r).contains(S("Needs Indium")));
        QVERIFY(rowToolTip(r).contains(S("Conflicts with OptiFabric")));
    }

    void dependencyPillsNeverDropButTheOthersDoFirst()
    {
        Row r = row(S("Iris"));
        r.source = Source::CurseForge;
        r.environment = S("Client");
        r.requiredBy = 2;
        r.updateVersion = S("0.6.14");
        r.depProblems = { { DepKind::Missing, { S("Indium") } } };
        const QList<PillSpec> pills = pillsFor(r, S("1.21.1"));
        // no room at all: what must stay stays (the danger and the update), everything else goes
        const QList<ModDelegate::Placed> none = ModDelegate::fitPills(pills, 0);
        QStringList kept;
        for (const auto& placed : none) {
            kept.append(placed.spec.text);
        }
        QCOMPARE(kept, (QStringList{ S("Needs Indium"), S("Update \u2192 0.6.14") }));
        // the least important go first as the room shrinks
        int everything = 0;
        for (const auto& placed : ModDelegate::fitPills(pills, 100000)) {
            everything += placed.width + ModDelegate::kPillGap;
        }
        QCOMPARE(ModDelegate::fitPills(pills, everything).size(), 5);
        const QList<ModDelegate::Placed> some = ModDelegate::fitPills(pills, everything - 60);
        QVERIFY(some.size() < 5 && some.size() >= 2);
        QCOMPARE(some.first().spec.text, S("Needs Indium"));
        // a row that still cannot hold them is shortened, never pushed over the name
        QList<ModDelegate::Placed> squeeze = ModDelegate::fitPills(pills, 0);
        int sum = ModDelegate::kPillGap * int(squeeze.size() - 1);
        for (const auto& placed : squeeze) {
            sum += placed.width;
        }
        ModDelegate::shrinkToFit(squeeze, sum - 40);
        int after = ModDelegate::kPillGap * int(squeeze.size() - 1);
        for (const auto& placed : squeeze) {
            after += placed.width;
        }
        QVERIFY(after <= sum - 40);
        QCOMPARE(squeeze.size(), 2);
    }

    void overlayPutsTheScanIntoTheRowsOfEnabledModsOnly()
    {
        QList<Row> rows = { row(S("Iris")), row(S("Lithium"), false), row(S("Sodium")) };
        for (Row& r : rows) {
            r.filePath = S("C:/mods/") + r.fileName;
            r.fileStamp = S("10/20");
        }
        Mods::Inspection scan;
        const auto add = [&scan](const Row& r, const QString& stamp, const QString& missing) {
            Mods::ModInspection m;
            m.path = r.filePath;
            m.fileName = r.fileName;
            m.stamp = stamp;
            m.enabled = r.enabled;
            Mods::DepEntry d;
            d.id = missing.toLower();
            d.name = missing;
            d.state = Mods::DepState::Missing;
            m.needs = { d };
            scan.byPath.insert(m.path, int(scan.mods.size()));
            scan.mods.append(m);
        };
        add(rows.at(0), S("10/20"), S("Indium"));
        add(rows.at(1), S("10/20"), S("Indium"));  // disabled: its needs do not matter until it is enabled
        add(rows.at(2), S("99/99"), S("Indium"));  // the file changed since the scan read it
        overlayDependencies(rows, scan);
        QCOMPARE(rows.at(0).depProblems.size(), 1);
        QCOMPARE(rows.at(0).depProblems.first().kind, DepKind::Missing);
        QCOMPARE(rows.at(0).depProblems.first().names, QStringList{ S("Indium") });
        QVERIFY(rows.at(1).depProblems.isEmpty());
        QVERIFY(rows.at(2).depProblems.isEmpty());
        // an empty answer clears everything
        overlayDependencies(rows, Mods::Inspection());
        QVERIFY(!rows.at(0).hasDepProblems());

        // Issues + caption: "2 need attention" / "1 needs attention"
        rows = { row(S("A")), row(S("B")), row(S("C")) };
        rows[0].depProblems = { { DepKind::Missing, { S("X") } } };
        rows[1].depProblems = { { DepKind::Conflict, { S("Y") } } };
        rows[2].issues = { S("Not for 1.21.1") };  // Prism's own issue: in Issues, but not a dependency problem
        const Counts counts = countOf(rows);
        QCOMPARE(counts.issues, 3);
        QCOMPARE(counts.attention, 2);
        QCOMPARE(captionText(counts, 3, false), S("3 mods \u00b7 2 need attention"));
        rows[1].depProblems.clear();
        QCOMPARE(captionText(countOf(rows), 3, false), S("3 mods \u00b7 1 needs attention"));
        QCOMPARE(attentionText(1), S("1 needs attention"));
        QCOMPARE(attentionText(2), S("2 need attention"));
        QVERIFY(matchesFilter(rows[0], Filter::Issues));
        QVERIFY(matchesFilter(rows[2], Filter::Issues));
        QVERIFY(!matchesFilter(rows[1], Filter::Issues));
        // with disabled mods and updates the order is disabled, attention, updates
        rows[2].enabled = false;
        rows[1].updateVersion = S("1.1");
        QCOMPARE(captionText(countOf(rows), 3, false), S("3 mods \u00b7 1 disabled \u00b7 1 needs attention \u00b7 1 update"));
    }

    void pillsIssuesAndCaptionFollowAnInjectedScan()
    {
        Rig rig;
        rig.fakeScan = true;
        rig.scanDelay = 10;
        rig.addTrio();  // Sodium, Iris Shaders (needs Sodium), Lithium (disabled)
        rig.build();
        QVERIFY(rig.open(3));
        QVERIFY(rig.waitScans(1));
        QVERIFY(!rig.page->hasScan());
        QVERIFY(rig.page->isScanning());
        // what the scan was asked: every file of the list with its on / off state, and the Fabric / 1.21.1 context
        const Zero::Mods::ScanRequest request = rig.scanRequests.first();
        QCOMPARE(request.files.size(), 3);
        QSet<QString> paths;
        for (const Zero::Mods::ScanFile& file : request.files) {
            paths.insert(QFileInfo(file.path).fileName());
            QCOMPARE(file.enabled, !file.path.endsWith(S(".disabled")));
        }
        QCOMPARE(paths, (QSet<QString>{ S("sodium.jar"), S("iris.jar"), S("lithium.jar.disabled") }));
        QCOMPARE(request.context.mcVersion, S("1.21.1"));
        QCOMPARE(request.context.loader, Zero::Mods::InstanceLoader::Fabric);
        // nothing is claimed before the answer
        QVERIFY(!rig.rowNamed(S("Iris Shaders")).hasDepProblems());
        QCOMPARE(rig.page->captionLabel()->text(), S("3 mods \u00b7 1 disabled"));

        Zero::Mods::ModInspection iris = rig.inspectionOf(S("iris.jar"), S("Iris Shaders"));
        Zero::Mods::DepEntry indium;
        indium.id = S("indium");
        indium.name = S("Indium");
        indium.state = Zero::Mods::DepState::Missing;
        iris.needs = { indium };
        Zero::Mods::ModInspection sodium = rig.inspectionOf(S("sodium.jar"), S("Sodium"));
        Zero::Mods::DepEntry opti;
        opti.id = S("optifabric");
        opti.name = S("OptiFabric");
        opti.state = Zero::Mods::DepState::Installed;
        sodium.conflicts = { opti };
        Zero::Mods::ModInspection lithium = rig.inspectionOf(S("lithium.jar.disabled"), S("Lithium"), false);
        lithium.needs = { indium };  // disabled: it does not count
        QVERIFY(rig.answerScan(Rig::inspection({ iris, sodium, lithium })));

        QCOMPARE(rig.rowNamed(S("Iris Shaders")).depProblems.size(), 1);
        QCOMPARE(rig.rowNamed(S("Iris Shaders")).depProblems.first().kind, DepKind::Missing);
        QCOMPARE(rig.rowNamed(S("Sodium")).depProblems.first().kind, DepKind::Conflict);
        QVERIFY(!rig.rowNamed(S("Lithium")).hasDepProblems());
        QCOMPARE(rig.page->captionLabel()->text(), S("3 mods \u00b7 1 disabled \u00b7 2 need attention"));
        const QList<PillSpec> irisPills = pillsFor(rig.rowNamed(S("Iris Shaders")), S("1.21.1"));
        QCOMPARE(irisPills.first().text, S("Needs Indium"));
        QCOMPARE(irisPills.first().tone, PillTone::Danger);
        QVERIFY(rowToolTip(rig.rowNamed(S("Iris Shaders"))).contains(S("Needs Indium")));
        // Issues lists exactly those two
        rig.page->setFilter(Filter::Issues);
        QCOMPARE(rig.names(), (QStringList{ S("Iris Shaders"), S("Sodium") }));
        rig.page->setFilter(Filter::All);

        // a newer scan says the conflict is gone: singular caption
        rig.page->rescanNow();
        QVERIFY(rig.waitScans(2));
        QVERIFY(rig.answerScan(Rig::inspection({ iris, rig.inspectionOf(S("sodium.jar"), S("Sodium")), lithium })));
        QCOMPARE(rig.page->captionLabel()->text(), S("3 mods \u00b7 1 disabled \u00b7 1 needs attention"));

        // an answer about a file that changed since is not shown for that file
        Zero::Mods::ModInspection stale = iris;
        stale.stamp = S("1/1");
        rig.page->rescanNow();
        QVERIFY(rig.waitScans(3));
        QVERIFY(rig.answerScan(Rig::inspection({ stale })));
        QVERIFY(!rig.rowNamed(S("Iris Shaders")).hasDepProblems());
        QCOMPARE(rig.page->captionLabel()->text(), S("3 mods \u00b7 1 disabled"));
    }

    void theScanWaitsForTheReloadToSettle()
    {
        Rig rig;
        rig.fakeScan = true;
        rig.scanDelay = 4000;  // far longer than this test
        rig.addTrio();
        rig.build();
        QVERIFY(rig.open(3));
        QTest::qWait(200);
        QVERIFY(rig.scanRequests.isEmpty());  // the reload did not start it yet
        QVERIFY(!rig.page->isScanning());
        rig.page->rescanNow();  // ... but it can be asked for
        QCOMPARE(rig.scanRequests.size(), 1);
        QVERIFY(rig.page->isScanning());
        // closing the page drops an answer that is still on its way
        rig.page->closed();
        rig.scanDone.last()(Rig::inspection({}));
        QTest::qWait(60);
        QVERIFY(!rig.page->hasScan());
    }

    void aStaleScanAnswerIsIgnored()
    {
        Rig rig;
        rig.fakeScan = true;
        rig.scanDelay = 10;
        rig.addTrio();
        rig.build();
        QVERIFY(rig.open(3));
        QVERIFY(rig.waitScans(1));
        rig.page->rescanNow();
        rig.page->rescanNow();
        QVERIFY(rig.scanDone.size() >= 3);
        const int newest = int(rig.scanDone.size()) - 1;

        Zero::Mods::DepEntry indium;
        indium.id = S("indium");
        indium.name = S("Indium");
        indium.state = Zero::Mods::DepState::Missing;
        Zero::Mods::ModInspection old = rig.inspectionOf(S("iris.jar"), S("Iris Shaders"));
        old.needs = { indium };
        Zero::Mods::ModInspection current = rig.inspectionOf(S("iris.jar"), S("Iris Shaders"));  // nothing wrong any more

        // the answer of an older scan arrives first: nothing happens
        rig.scanDone.at(newest - 1)(Rig::inspection({ old }));
        QTest::qWait(80);
        QVERIFY(!rig.page->hasScan());
        QVERIFY(!rig.rowNamed(S("Iris Shaders")).hasDepProblems());
        // the newest one is taken, a late older one afterwards is not
        rig.scanDone.at(newest)(Rig::inspection({ current }));
        QTRY_VERIFY(rig.page->hasScan());
        rig.scanDone.at(newest - 1)(Rig::inspection({ old }));
        rig.scanDone.at(0)(Rig::inspection({ old }));
        QTest::qWait(80);
        rig.page->rebuildNow();
        QVERIFY(!rig.rowNamed(S("Iris Shaders")).hasDepProblems());
        QVERIFY(rig.page->inspection().find(rig.mods.filePath(S("iris.jar")))->needs.isEmpty());
    }

    void thePanelOpensOnARowClickAndEnterButNotOnTheSwitch()
    {
        Rig rig;
        rig.fakeScan = true;
        rig.scanDelay = 10;
        rig.addTrio();
        rig.build();
        QVERIFY(rig.open(3));
        QVERIFY(rig.waitScans(1));
        QVERIFY(rig.answerScan(Rig::inspection({ rig.inspectionOf(S("iris.jar"), S("Iris Shaders")), rig.inspectionOf(S("sodium.jar"), S("Sodium")) })));
        QVERIFY(!rig.page->isPanelOpen());
        QVERIFY(!rig.page->panel()->isVisible());
        auto* view = rig.page->listView();

        // the switch does not open it
        const QRect card = ModDelegate::cardRect(view->visualRect(rig.page->listModel()->index(rig.rowOf(S("Iris Shaders")))));
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, ModDelegate::switchRect(card).center());
        QVERIFY(!rig.page->isPanelOpen());
        QTRY_VERIFY(!rig.rowNamed(S("Iris Shaders")).enabled);  // it toggled instead
        rig.page->requestToggle({ rig.rowOf(S("Iris Shaders")) }, ToggleAction::Enable);
        QTRY_VERIFY(rig.rowNamed(S("Iris Shaders")).enabled);
        rig.page->rebuildNow();

        // a plain click on the body does
        const QRect irisCard = ModDelegate::cardRect(view->visualRect(rig.page->listModel()->index(rig.rowOf(S("Iris Shaders")))));
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, irisCard.center());
        QVERIFY(rig.page->isPanelOpen());
        QVERIFY(rig.page->panel()->isVisible());
        QCOMPARE(rig.page->panel()->width(), ModDependencyPanel::kWidth);
        QCOMPARE(rig.page->panel()->nameLabel()->text(), S("Iris Shaders"));
        QVERIFY(rig.page->panel()->captionLabel()->text().contains(S("1.7.0")));
        QVERIFY(rig.page->panel()->captionLabel()->text().contains(S("iris.jar")));
        QVERIFY(rig.page->panel()->captionLabel()->toolTip().endsWith(S("iris.jar")));  // the full path

        // a Ctrl click makes a multi-selection: the panel closes
        const QRect sodiumCard = ModDelegate::cardRect(view->visualRect(rig.page->listModel()->index(rig.rowOf(S("Sodium")))));
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::ControlModifier, sodiumCard.center());
        QCOMPARE(rig.page->selectedRows().size(), 2);
        QVERIFY(!rig.page->isPanelOpen());
        QVERIFY(!rig.page->panel()->isVisible());

        // Esc closes it, Enter opens it again on the keyboard row
        rig.page->selectRows({ rig.rowOf(S("Sodium")) });
        view->setFocus();
        QTest::keyClick(view, Qt::Key_Return);
        QVERIFY(rig.page->isPanelOpen());
        QCOMPARE(rig.page->panel()->nameLabel()->text(), S("Sodium"));
        QTest::keyClick(view, Qt::Key_Escape);
        QVERIFY(!rig.page->isPanelOpen());
        QVERIFY(!rig.page->panel()->isVisible());
        QTest::keyClick(view, Qt::Key_Escape);  // with nothing open Esc is not taken
        QVERIFY(!rig.page->isPanelOpen());

        // the cross closes it
        QTest::keyClick(view, Qt::Key_Return);
        QVERIFY(rig.page->isPanelOpen());
        rig.page->panel()->closeButton()->click();
        QVERIFY(!rig.page->isPanelOpen());

        // open, the panel follows a single selection
        QTest::keyClick(view, Qt::Key_Return);
        rig.page->selectRows({ rig.rowOf(S("Iris Shaders")) });
        QVERIFY(rig.page->isPanelOpen());
        QCOMPARE(rig.page->panel()->nameLabel()->text(), S("Iris Shaders"));
    }

    void thePanelClosesWhenItsModDisappears()
    {
        Rig rig;
        rig.fakeScan = true;
        rig.scanDelay = 10;
        rig.addTrio();
        rig.build();
        QVERIFY(rig.open(3));
        QVERIFY(rig.waitScans(1));
        QVERIFY(rig.answerScan(Rig::inspection({})));
        QVERIFY(rig.page->openPanelFor(S("Sodium")));
        QVERIFY(rig.page->isPanelOpen());
        QVERIFY(!rig.page->openPanelFor(S("No such mod")));
        // switching it off keeps it open: the file is renamed, the mod is the same
        rig.answers = { 1 };
        rig.page->requestToggle({ rig.rowOf(S("Sodium")) }, ToggleAction::Disable);
        QVERIFY(QFile::exists(rig.mods.filePath(S("sodium.jar.disabled"))));
        QTRY_VERIFY(!rig.rowNamed(S("Sodium")).enabled);
        QVERIFY(rig.page->isPanelOpen());
        QCOMPARE(rig.page->panel()->nameLabel()->text(), S("Sodium"));
        // the file goes: the panel goes with it
        QVERIFY(QFile::remove(rig.mods.filePath(S("sodium.jar.disabled"))));
        QVERIFY(rig.settle());
        QTRY_VERIFY(rig.rowOf(S("Sodium")) < 0);
        QVERIFY(!rig.page->isPanelOpen());
        QVERIFY(!rig.page->panel()->isVisible());
    }

    void thePanelShowsTheSectionsOfTheScan()
    {
        Rig rig;
        rig.fakeScan = true;
        rig.scanDelay = 10;
        rig.addTrio();
        rig.build();
        QVERIFY(rig.open(3));
        QTRY_COMPARE_WITH_TIMEOUT(rig.rowNamed(S("Sodium")).requiredBy, 1, 5000);
        QVERIFY(rig.waitScans(1));

        using Zero::Mods::DepEntry;
        using Zero::Mods::DepState;
        const auto entry = [](const QString& id, const QString& name, DepState state, const QString& predicate = QString(),
                              const QString& version = QString(), const QString& file = QString()) {
            DepEntry d;
            d.id = id;
            d.name = name;
            d.state = state;
            d.predicate = predicate;
            d.version = version;
            d.fileName = file;
            return d;
        };
        Zero::Mods::ModInspection iris = rig.inspectionOf(S("iris.jar"), S("Iris Shaders"));
        iris.needs = { entry(S("sodium"), S("Sodium"), DepState::Installed, S("0.5 or newer"), S("0.116.18"), S("sodium.jar")),
                       entry(S("lib"), S("Lib"), DepState::Bundled, QString(), S("1.0"), S("sodium.jar")),
                       entry(S("lithium"), S("Lithium"), DepState::Disabled, S("any"), S("0.12.0"), S("lithium.jar.disabled")),
                       entry(S("beta"), S("Beta"), DepState::WrongVersion, S("1.0 or newer"), S("0.5.1"), S("beta.jar")),
                       entry(S("indium"), S("Indium"), DepState::Missing),
                       entry(S("odd"), S("Odd"), DepState::Unknown, S("?")) };
        iris.needs[2].path = rig.mods.filePath(S("lithium.jar.disabled"));
        Zero::Mods::GameEntry mc;
        mc.kind = Zero::Mods::GameKind::Minecraft;
        mc.id = S("minecraft");
        mc.name = S("Minecraft");
        mc.predicate = S("1.21.1");
        mc.state = Zero::Mods::GameState::Ok;
        mc.version = S("1.21.1");
        Zero::Mods::GameEntry loader = mc;
        loader.kind = Zero::Mods::GameKind::Loader;
        loader.id = S("fabricloader");
        loader.name = S("Fabric Loader");
        loader.predicate = S("0.17 or newer");
        loader.state = Zero::Mods::GameState::Mismatch;
        loader.version = S("0.16.5");
        Zero::Mods::GameEntry java = mc;
        java.kind = Zero::Mods::GameKind::Java;
        java.id = S("java");
        java.name = S("Java");
        java.predicate = S("21 or newer");
        java.state = Zero::Mods::GameState::Unknown;
        iris.game = { mc, loader, java };
        iris.optional = { entry(S("indium2"), S("Indium Extra"), DepState::NotInstalled), entry(S("lithium"), S("Lithium"), DepState::Installed) };
        iris.conflicts = { entry(S("optifabric"), S("OptiFabric"), DepState::Installed), entry(S("other"), S("Other"), DepState::NotInstalled) };
        QVERIFY(rig.answerScan(Rig::inspection({ iris })));

        QVERIFY(rig.page->openPanelFor(S("Iris Shaders")));
        ModDependencyPanel* panel = rig.page->panel();
        QCOMPARE(panel->sectionTitles(), (QStringList{ S("Needs"), S("Game"), S("Optional"), S("Needed by"), S("Conflicts") }));
        QVERIFY(!panel->skeleton());
        QCOMPARE(panel->rowTexts(S("Needs")).size(), 6);
        QVERIFY(panel->rowTexts(S("Needs")).first().contains(S("0.5 or newer")));
        QVERIFY(panel->rowTexts(S("Needs")).at(1).endsWith(S("any version")));  // no rule = any version
        QVERIFY(panel->rowTexts(S("Needs")).last().endsWith(S("version rule unknown")));
        const QStringList pills = panel->pillTexts();
        for (const QString& expected : { S("0.116.18"), S("Bundled"), S("Disabled"), S("Has 0.5.1"), S("Missing"), S("Can't tell"), S("Not checked"),
                                         S("Needs 0.17 or newer"), S("Installed"), S("Not installed") }) {
            QVERIFY2(pills.contains(expected), qPrintable(expected));
        }
        QVERIFY(pills.contains(S("1.21.1")));  // the instance's version: success
        QCOMPARE(panel->enableButtons().size(), 1);
        QCOMPARE(panel->findButtons().size(), 1);
        QCOMPARE(panel->rowTexts(S("Game")).size(), 3);
        QCOMPARE(panel->rowTexts(S("Optional")).size(), 2);
        QCOMPARE(panel->rowTexts(S("Conflicts")).size(), 2);
        QCOMPARE(panel->emptyTexts(), QStringList{ S("No installed mod needs Iris Shaders.") });
        QCOMPARE(panel->nameLabel()->text(), S("Iris Shaders"));

        // Sodium: nothing needed (empty states), but Iris needs it: from Prism's relations although the scan has no entry
        Zero::Mods::ModInspection sodium = rig.inspectionOf(S("sodium.jar"), S("Sodium"));
        rig.page->rescanNow();
        QVERIFY(rig.waitScans(2));
        const Zero::Mods::ModInspection lithiumOff = rig.inspectionOf(S("lithium.jar.disabled"), S("Lithium"), false);
        QVERIFY(rig.answerScan(Rig::inspection({ iris, sodium, lithiumOff })));
        QVERIFY(rig.page->openPanelFor(S("Sodium")));
        QCOMPARE(panel->sectionTitles(), (QStringList{ S("Needs"), S("Needed by") }));  // empty ones are hidden except these two
        QVERIFY(panel->emptyTexts().contains(S("Needs no other mods.")));
        QCOMPARE(panel->neededByButtons().size(), 1);
        QCOMPARE(panel->neededByButtons().first()->text(), S("Iris Shaders"));
        QVERIFY(rig.page->openPanelFor(S("Lithium")));
        QVERIFY(panel->emptyTexts().contains(S("No installed mod needs Lithium.")));
        QVERIFY(panel->neededByButtons().isEmpty());

        // a mod ZERO could not read: one sentence and Needed by
        Zero::Mods::ModInspection broken = rig.inspectionOf(S("sodium.jar"), S("Sodium"));
        broken.readable = false;
        rig.page->rescanNow();
        QVERIFY(rig.waitScans(3));
        QVERIFY(rig.answerScan(Rig::inspection({ iris, broken })));
        QVERIFY(rig.page->openPanelFor(S("Sodium")));
        QCOMPARE(panel->sectionTitles(), QStringList{ S("Needed by") });
        bool sentence = false;
        for (const QLabel* label : panel->findChildren<QLabel*>()) {
            sentence = sentence || label->text() == S("ZERO couldn't read this mod's details.");
        }
        QVERIFY(sentence);
    }

    void aDisabledModKeepsItsNeededByEntryWithTheWordDisabled()
    {
        Rig rig;
        rig.fakeScan = true;
        rig.scanDelay = 10;
        rig.addTrio();
        rig.build();
        QVERIFY(rig.open(3));
        QVERIFY(rig.waitScans(1));
        Zero::Mods::ModInspection sodium = rig.inspectionOf(S("sodium.jar"), S("Sodium"));
        Zero::Mods::NeededBy off;
        off.fileName = S("lithium.jar.disabled");
        off.path = rig.mods.filePath(S("lithium.jar.disabled"));
        off.name = S("Lithium");
        off.enabled = false;
        sodium.neededBy = { off };
        QVERIFY(rig.answerScan(Rig::inspection({ sodium })));
        QVERIFY(rig.page->openPanelFor(S("Sodium")));
        // the scan's entry and Prism's Iris Shaders (it requires Sodium), by name
        QStringList texts;
        for (const QPushButton* b : rig.page->panel()->neededByButtons()) {
            texts.append(b->text());
        }
        QCOMPARE(texts, (QStringList{ S("Iris Shaders"), S("Lithium \u00b7 disabled") }));
    }

    void neededByClickSelectsThatModAndShowsItsPanel()
    {
        Rig rig;
        rig.fakeScan = true;
        rig.scanDelay = 10;
        rig.addTrio();
        rig.build();
        QVERIFY(rig.open(3));
        QTRY_COMPARE_WITH_TIMEOUT(rig.rowNamed(S("Sodium")).requiredBy, 1, 5000);
        QVERIFY(rig.waitScans(1));
        QVERIFY(rig.answerScan(Rig::inspection({ rig.inspectionOf(S("sodium.jar"), S("Sodium")), rig.inspectionOf(S("iris.jar"), S("Iris Shaders")),
                                                 rig.inspectionOf(S("lithium.jar.disabled"), S("Lithium"), false) })));
        QVERIFY(rig.page->openPanelFor(S("Sodium")));
        QCOMPARE(rig.page->selectedRows(), QList<int>{ rig.rowOf(S("Sodium")) });
        QCOMPARE(rig.page->panel()->neededByButtons().size(), 1);
        rig.page->panel()->neededByButtons().first()->click();
        QCOMPARE(rig.page->panel()->nameLabel()->text(), S("Iris Shaders"));
        QCOMPARE(rig.page->selectedRows(), QList<int>{ rig.rowOf(S("Iris Shaders")) });

        // a filter that hides the mod is lifted: it was asked for
        rig.page->setFilter(Filter::Disabled);
        QCOMPARE(rig.names(), QStringList{ S("Lithium") });
        QVERIFY(rig.page->openPanelFor(S("Iris Shaders")));
        QCOMPARE(rig.page->filter(), Filter::All);
        QCOMPARE(rig.page->selectedRows(), QList<int>{ rig.rowOf(S("Iris Shaders")) });
    }

    void theEnableButtonUsesTheNormalEnableFlow()
    {
        Rig rig;
        rig.fakeScan = true;
        rig.scanDelay = 10;
        rig.addJar(S("sodium.jar.disabled"), S("sodium"), S("Sodium"), S("0.5.8"), S("indium"));  // needs Indium, which is off too
        rig.addJar(S("indium.jar.disabled"), S("indium"), S("Indium"), S("1.0"));
        rig.addJar(S("iris.jar"), S("iris"), S("Iris Shaders"), S("1.7.0"), S("sodium"));
        rig.build();
        QVERIFY(rig.open(3));
        QTRY_COMPARE_WITH_TIMEOUT(rig.rowNamed(S("Sodium")).requiredBy, 1, 5000);
        QVERIFY(rig.waitScans(1));
        Zero::Mods::ModInspection iris = rig.inspectionOf(S("iris.jar"), S("Iris Shaders"));
        Zero::Mods::DepEntry sodium;
        sodium.id = S("sodium");
        sodium.name = S("Sodium");
        sodium.state = Zero::Mods::DepState::Disabled;
        sodium.fileName = S("sodium.jar.disabled");
        sodium.path = rig.mods.filePath(S("sodium.jar.disabled"));
        iris.needs = { sodium };
        QVERIFY(rig.answerScan(Rig::inspection({ iris })));
        QCOMPARE(rig.rowNamed(S("Iris Shaders")).depProblems.first().kind, DepKind::Disabled);
        QCOMPARE(pillsFor(rig.rowNamed(S("Iris Shaders")), S("1.21.1")).first().text, S("Needs Sodium \u00b7 disabled"));
        QVERIFY(rig.page->openPanelFor(S("Iris Shaders")));
        QCOMPARE(rig.page->panel()->enableButtons().size(), 1);
        QVERIFY(rig.page->panel()->enableButtons().first()->isEnabled());

        // the running lock: the button is dimmed and a request that still comes does nothing
        rig.running = true;
        rig.page->refreshRunning();
        QVERIFY(!rig.page->panel()->enableButtons().first()->isEnabled());
        emit rig.page->panel()->enableRequested(sodium.path);
        QVERIFY(QFile::exists(rig.mods.filePath(S("sodium.jar.disabled"))));
        QVERIFY(rig.prompts.isEmpty());
        rig.running = false;
        rig.page->refreshRunning();
        QVERIFY(rig.page->panel()->enableButtons().first()->isEnabled());

        // the normal flow: Sodium needs Indium, which is off, so the page asks the affected-mods question first; Cancel leaves everything
        rig.answers = { 0 };
        rig.page->panel()->enableButtons().first()->click();
        QCOMPARE(rig.prompts.size(), 1);
        QCOMPARE(rig.prompts.first().title, S("Enable Indium too?"));
        QVERIFY(QFile::exists(rig.mods.filePath(S("sodium.jar.disabled"))));
        QVERIFY(!QFile::exists(rig.mods.filePath(S("sodium.jar"))));
        // "Enable only Sodium"
        rig.answers = { 1 };
        rig.page->panel()->enableButtons().first()->click();
        QCOMPARE(rig.prompts.size(), 2);
        QVERIFY(QFile::exists(rig.mods.filePath(S("sodium.jar"))));
        QVERIFY(!QFile::exists(rig.mods.filePath(S("sodium.jar.disabled"))));
        QVERIFY(QFile::exists(rig.mods.filePath(S("indium.jar.disabled"))));  // only what was chosen
        // a request for a file that is not listed (any more) does nothing
        emit rig.page->panel()->enableRequested(sodium.path);
        emit rig.page->panel()->enableRequested(rig.mods.filePath(S("gone.jar")));
        QCOMPARE(rig.prompts.size(), 2);
    }

    void findAsksTheEnvironmentForTheDependencyId()
    {
        Rig rig;
        rig.fakeScan = true;
        rig.scanDelay = 10;
        rig.addTrio();
        rig.build();
        QVERIFY(rig.open(3));
        QVERIFY(rig.waitScans(1));
        Zero::Mods::ModInspection iris = rig.inspectionOf(S("iris.jar"), S("Iris Shaders"));
        Zero::Mods::DepEntry indium;
        indium.id = S("indium");
        indium.name = S("indium");
        indium.state = Zero::Mods::DepState::Missing;
        iris.needs = { indium };
        QVERIFY(rig.answerScan(Rig::inspection({ iris })));
        QVERIFY(rig.page->openPanelFor(S("Iris Shaders")));
        QCOMPARE(rig.page->panel()->findButtons().size(), 1);
        QVERIFY(rig.page->panel()->enableButtons().isEmpty());
        rig.page->panel()->findButtons().first()->click();
        QCOMPARE(rig.findCalls, QStringList{ S("indium") });
    }

    void theFirstScanShowsSkeletonLines()
    {
        Rig rig;
        rig.fakeScan = true;
        rig.scanDelay = 10;
        rig.addTrio();
        rig.build();
        QVERIFY(rig.open(3));
        QVERIFY(rig.waitScans(1));
        QVERIFY(rig.page->openPanelFor(S("Iris Shaders")));
        ZeroSkeleton* skeleton = rig.page->panel()->skeleton();
        QVERIFY(skeleton);
        QVERIFY(skeleton->isVisible());
        QCOMPARE(skeleton->shape(), ZeroSkeleton::Shape::Rows);
        QVERIFY(skeleton->height() >= 4 * 60);  // four lines
        QVERIFY(rig.page->panel()->sectionTitles().isEmpty());
        QVERIFY(rig.page->panel()->enableButtons().isEmpty());
        QCOMPARE(rig.page->panel()->nameLabel()->text(), S("Iris Shaders"));  // the header is already there
        // the answer replaces them
        QVERIFY(rig.answerScan(Rig::inspection({ rig.inspectionOf(S("iris.jar"), S("Iris Shaders")) })));
        QVERIFY(!rig.page->panel()->skeleton());
        QCOMPARE(rig.page->panel()->sectionTitles(), (QStringList{ S("Needs"), S("Needed by") }));
    }

    void aRealScanReadsTheJarsAndFollowsTheFolder()
    {
        Rig rig;
        rig.addJar(S("sodium.jar"), S("sodium"), S("Sodium"), S("0.5.8"));
        rig.addJar(S("iris.jar"), S("iris"), S("Iris Shaders"), S("1.7.0"), S("indium"));
        rig.build();
        QVERIFY(rig.open(2));
        // the real pool scan (300 ms after the reload) finds that Iris needs Indium
        QTRY_VERIFY_WITH_TIMEOUT(rig.page->hasScan(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(rig.rowNamed(S("Iris Shaders")).hasDepProblems(), 5000);
        QCOMPARE(rig.rowNamed(S("Iris Shaders")).depProblems.first().kind, DepKind::Missing);
        QCOMPARE(rig.rowNamed(S("Iris Shaders")).depProblems.first().names, QStringList{ S("indium") });
        QVERIFY(!rig.rowNamed(S("Sodium")).hasDepProblems());
        QCOMPARE(rig.page->captionLabel()->text(), S("2 mods \u00b7 1 needs attention"));
        QVERIFY(rig.page->openPanelFor(S("Iris Shaders")));
        QCOMPARE(rig.page->panel()->findButtons().size(), 1);
        QVERIFY(rig.page->scanner()->cache());
        QVERIFY(rig.page->scanner()->cache()->parseCount() >= 2);

        // Indium arrives: the next scan clears the pill and the panel follows
        rig.addJar(S("indium.jar"), S("indium"), S("Indium"), S("1.0"));
        QVERIFY(rig.settle());
        QTRY_VERIFY_WITH_TIMEOUT(!rig.rowNamed(S("Iris Shaders")).hasDepProblems(), 15000);
        QCOMPARE(rig.page->captionLabel()->text(), S("3 mods"));
        QTRY_VERIFY_WITH_TIMEOUT(rig.page->panel()->findButtons().isEmpty(), 5000);
        // ... and switching it off brings the problem back as "disabled" (a file that was parsed before is not parsed again)
        const int parsed = rig.page->scanner()->cache()->parseCount();
        rig.answers = { 1 };  // Iris needs it: "disable only Indium"
        rig.page->requestToggle({ rig.rowOf(S("Indium")) }, ToggleAction::Disable);
        QTRY_VERIFY_WITH_TIMEOUT(!rig.rowNamed(S("Indium")).enabled, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rig.rowNamed(S("Iris Shaders")).hasDepProblems(), 15000);
        QCOMPARE(rig.rowNamed(S("Iris Shaders")).depProblems.first().kind, DepKind::Disabled);
        QCOMPARE(pillsFor(rig.rowNamed(S("Iris Shaders")), S("1.21.1")).first().text, S("Needs Indium \u00b7 disabled"));
        QVERIFY(rig.page->scanner()->cache()->parseCount() <= parsed + 1);  // only the renamed file was read again
    }

    void bundledNeedsAndConflictsAreSummarisedInThePanel()
    {
        Rig rig;
        rig.fakeScan = true;
        rig.scanDelay = 10;
        rig.addTrio();
        rig.build();
        QVERIFY(rig.open(3));
        QVERIFY(rig.waitScans(1));
        Zero::Mods::ModInspection iris = rig.inspectionOf(S("iris.jar"), S("Iris Shaders"));
        Zero::Mods::DepEntry bundled;
        bundled.id = S("lib");
        bundled.name = S("lib");
        bundled.state = Zero::Mods::DepState::Missing;
        bundled.forBundled = S("Lib Base");
        iris.needs = { bundled };
        Zero::Mods::DepEntry opti;
        opti.id = S("optifabric");
        opti.name = S("OptiFabric");
        opti.state = Zero::Mods::DepState::Installed;
        iris.conflicts = { opti };
        for (int i = 0; i < 12; ++i) {
            iris.otherConflicts.append(S("other%1").arg(i));
        }
        QVERIFY(rig.answerScan(Rig::inspection({ iris })));
        QVERIFY(rig.page->openPanelFor(S("Iris Shaders")));
        ModDependencyPanel* panel = rig.page->panel();
        QCOMPARE(panel->rowTexts(S("Needs")), QStringList{ S("lib | for bundled Lib Base") });
        QCOMPARE(panel->rowTexts(S("Conflicts")), QStringList{ S("OptiFabric") });
        const auto more = [panel]() { return panel->findChild<QLabel*>(S("zeroDepMoreConflicts")); };
        QVERIFY(more());
        QCOMPARE(more()->text(), S("Declares 12 more conflicts with mods that aren't installed."));
        QVERIFY(more()->toolTip().contains(S("other0")) && more()->toolTip().contains(S("other11")));

        // one left over: singular; none of either: no section
        iris.otherConflicts = { S("only") };
        rig.page->rescanNow();
        QVERIFY(rig.waitScans(2));
        QVERIFY(rig.answerScan(Rig::inspection({ iris })));
        QCOMPARE(more()->text(), S("Declares 1 more conflict with a mod that isn't installed."));
        iris.otherConflicts.clear();
        iris.conflicts.clear();
        rig.page->rescanNow();
        QVERIFY(rig.waitScans(3));
        QVERIFY(rig.answerScan(Rig::inspection({ iris })));
        QVERIFY(!panel->sectionTitles().contains(S("Conflicts")));
        // only the caption, no rows: the section stays
        iris.otherConflicts = { S("a"), S("b") };
        rig.page->rescanNow();
        QVERIFY(rig.waitScans(4));
        QVERIFY(rig.answerScan(Rig::inspection({ iris })));
        QVERIFY(panel->sectionTitles().contains(S("Conflicts")));
        QVERIFY(panel->rowTexts(S("Conflicts")).isEmpty());
    }

    void thePanelPaintsNoBlackBoxAndTheCloseButtonHasItsIcon()
    {
        Rig rig;
        rig.fakeScan = true;
        rig.scanDelay = 10;
        rig.addTrio();
        rig.build();
        QVERIFY(rig.open(3));
        QVERIFY(rig.waitScans(1));
        QVERIFY(rig.answerScan(Rig::inspection({ rig.inspectionOf(S("iris.jar"), S("Iris Shaders")) })));
        QVERIFY(rig.page->openPanelFor(S("Iris Shaders")));
        ModDependencyPanel* panel = rig.page->panel();
        // the scroll area and its content let the card show through
        QVERIFY(!panel->findChild<QScrollArea*>()->viewport()->autoFillBackground());
        QVERIFY(!panel->findChild<QWidget*>(S("zeroDepContent"))->autoFillBackground());
        // the close button is an icon button, not a letter
        QVERIFY(panel->closeButton()->text().isEmpty());
        QVERIFY(!panel->closeButton()->icon().isNull());
        QCOMPARE(panel->closeButton()->width(), 28);
        QCOMPARE(panel->closeButton()->toolTip(), S("Close"));
        QCOMPARE(panel->closeButton()->accessibleName(), S("Close"));
    }

    void theWholePageFitsAWindowOf1100x700WithThePanelOpen()
    {
        Rig rig;
        rig.fakeScan = true;
        rig.scanDelay = 10;
        rig.addTrio();
        rig.build();
        rig.page->resize(1100 - 220, 700 - 120);  // the instance window minus its sidebar and title area
        QVERIFY(rig.open(3));
        QVERIFY(rig.waitScans(1));
        QVERIFY(rig.answerScan(Rig::inspection({ rig.inspectionOf(S("iris.jar"), S("Iris Shaders")) })));
        QVERIFY(rig.page->openPanelFor(S("Iris Shaders")));
        QTest::qWait(30);
        const ModDependencyPanel* panel = rig.page->panel();
        QVERIFY(panel->isVisible());
        QCOMPARE(panel->width(), 320);
        const QRect pageRect = rig.page->rect();
        QVERIFY(pageRect.contains(panel->geometry().translated(panel->parentWidget()->mapTo(rig.page.get(), QPoint(0, 0)))));
        // the list keeps room for the name and a danger pill; the toolbar and the status row still fit
        QVERIFY(rig.page->listView()->width() >= 360);
        QVERIFY(rig.page->listView()->width() + panel->width() <= pageRect.width());
        QVERIFY(rig.page->searchField()->isVisible());
        QVERIFY(rig.page->addButton()->isVisible());
        QVERIFY(rig.page->addButton()->geometry().right() <= pageRect.right());
        QVERIFY(rig.page->captionLabel()->isVisible());
        QVERIFY(rig.page->minimumSizeHint().width() <= pageRect.width());
        QVERIFY(rig.page->minimumSizeHint().height() <= pageRect.height());
    }
};

QTEST_MAIN(ZeroModsPageTest)

#include "ZeroModsPage_test.moc"
