// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the instance window's Shader packs page (docs/zero/CONTENT-PACKS.md, "Shader packs page"): the page against a REAL
// ShaderPackFolderModel on a temp `shaderpacks` folder (tiny zips, a folder pack, a ".zip.disabled" file) and a temp `mods` folder for the
// shader mod strip. The switch renames to ".disabled" and back, the search filters, the strip shows only without a shader mod, the empty
// and no-match states, the running lock, Remove through an injected trash, Add file. Offscreen, no network, parallel-safe.
// Screenshots (only with ZERO_SHADER_SHOTS=<dir>): the page with two packs and empty, in the Obsidian and Minecraft themes.
#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QImage>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <memory>

#include "archive/ArchiveWriter.h"
#include "minecraft/MinecraftInstance.h"
#include "settings/INISettingsObject.h"
#include "minecraft/mod/ShaderPackFolderModel.h"
#include "modplatform/packwiz/Packwiz.h"
#include "ui/zero/ZeroEmptyState.h"
#include "ui/zero/ZeroSkeleton.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroToast.h"
#include "ui/zero/content/RemoteImageCache.h"
#include "ui/zero/content/ShaderModNotice.h"
#include "ui/zero/instance/InstancePageParts.h"
#include "ui/zero/instance/ShaderPacksPage.h"

using namespace Zero;
using namespace Zero::ShadersUi;
using Zero::ModsUi::ChoicePrompt;

namespace {

QString S(const char* text)
{
    return QString::fromUtf8(text);
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

/// A tiny shader pack: a zip with shaders/final.fsh.
bool writeZip(const QString& path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    MMCZip::ArchiveWriter writer(path);
    if (!writer.open()) {
        return false;
    }
    if (!writer.addFile(QStringLiteral("shaders/final.fsh"), QByteArray("void main() {}\n"))) {
        return false;
    }
    return bool(writer.close());
}

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

struct Rig {
    QTemporaryDir root;
    QDir shaders{ root.path() + S("/.minecraft/shaderpacks") };
    QDir mods{ root.path() + S("/.minecraft/mods") };
    std::unique_ptr<InstanceFixture> fixture;  ///< only when a test needs metadata: Resource::updateIssues() asks the instance
    std::unique_ptr<ShaderPackFolderModel> model;
    std::unique_ptr<ShaderPacksPage> page;

    // the fakes
    bool running = false;
    QStringList trashCalls;
    QSet<QString> failTrash;
    QList<ChoicePrompt> prompts;
    QList<int> answers;  ///< consumed one per question; empty = cancel
    int addCalls = 0;
    QStringList findCalls;
    bool withAdd = true;
    ModPlatform::ModLoaderTypes loaders = ModPlatform::Fabric;
    Zero::ShaderIcons::Lookup iconLookup;     ///< empty = no icons
    Zero::RemoteImageCache* images = nullptr;

    /// The packwiz entry of `fileName` (shaderpacks/.index), as a pack installed from a platform has it.
    void addMetadata(const QString& fileName, const QString& projectId, ModPlatform::ResourceProvider provider)
    {
        Packwiz::V1::Mod meta;
        meta.slug = QFileInfo(fileName).completeBaseName().toLower();
        meta.name = meta.slug;
        meta.filename = fileName;
        meta.mode = S("url");
        meta.url = QUrl(S("https://cdn.test/") + fileName);
        meta.hashFormat = S("sha1");
        meta.hash = S("0000");
        meta.provider = provider;
        meta.fileId = provider == ModPlatform::ResourceProvider::FLAME ? QVariant(777) : QVariant(S("FILE1"));
        meta.projectId = projectId;
        meta.versionNumber = S("1.0");
        meta.lockUpdate = false;
        QDir index(shaders.filePath(S(".index")));
        QDir().mkpath(index.absolutePath());
        Packwiz::V1::updateModIndex(index, meta);
    }

    Rig()
    {
        QDir().mkpath(shaders.absolutePath());
        QDir().mkpath(mods.absolutePath());
    }

    void addZip(const QString& name) { QVERIFY(writeZip(shaders.filePath(name))); }
    void addFolder(const QString& name)
    {
        QVERIFY(writeFile(shaders.filePath(name + S("/shaders/final.fsh")), QByteArray("void main() {}\n")));
    }
    void addSet()
    {
        addZip(S("Complementary.zip"));
        addZip(S("BSL.zip"));
    }
    void addShaderMod() { QVERIFY(writeFile(mods.filePath(S("iris-fabric-1.7.jar")), QByteArray("x"))); }

    ShadersPageEnvironment environment()
    {
        ShadersPageEnvironment env;
        env.target = [this]() {
            Content::TargetInstance target;
            target.id = S("inst");
            target.name = S("Test Instance");
            target.mcVersion = S("1.21.1");
            target.loaders = loaders;
            target.modsDir = mods.absolutePath();
            return target;
        };
        env.isRunning = [this]() { return running; };
        env.iconLookup = iconLookup;
        env.images = images;
        env.trash = [this](const QString& path) {
            trashCalls.append(QFileInfo(path).fileName());
            if (failTrash.contains(QFileInfo(path).fileName())) {
                return false;
            }
            return QFileInfo(path).isDir() ? QDir(path).removeRecursively() : QFile::remove(path);  // stands in for the Recycle Bin
        };
        env.ask = [this](QWidget*, const ChoicePrompt& prompt) {
            prompts.append(prompt);
            return answers.isEmpty() ? -1 : answers.takeFirst();
        };
        if (withAdd) {
            env.addShaders = [this]() { ++addCalls; };
            env.findMod = [this](const QString& query) { findCalls.append(query); };
        }
        return env;
    }

    void build()
    {
        model = std::make_unique<ShaderPackFolderModel>(shaders.absolutePath(), fixture ? fixture->instance.get() : nullptr, true, true);
        page = std::make_unique<ShaderPacksPage>(environment(), model.get());
        page->resize(1000, 640);
    }

    /// Opens the page and waits until `count` packs are known and read.
    bool open(int count)
    {
        page->opened();
        page->show();
        for (int i = 0; i < 200; ++i) {
            QTest::qWait(50);
            page->rebuildNow();
            if (page->isLoaded() && model->size() == count && !model->hasPendingParseTasks()) {
                QTest::qWait(60);
                page->rebuildNow();
                return true;
            }
        }
        return false;
    }

    QStringList names() const
    {
        QStringList out;
        for (int i = 0; i < page->listModel()->rowCount(); ++i) {
            out.append(page->listModel()->rowAt(i).name);
        }
        return out;
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
    bool enabled(const QString& name) const { return page->listModel()->rowAt(rowOf(name)).enabled; }
};

}  // namespace

class ZeroShaderPacksPageTest : public QObject {
    Q_OBJECT

   private slots:
    void rowsComeFromTheFolder()
    {
        Rig rig;
        rig.addSet();
        rig.addFolder(S("Folder Pack"));
        QVERIFY(writeFile(rig.shaders.filePath(S("readme.txt")), QByteArray("not a shader pack")));
        rig.build();
        QVERIFY(rig.open(3));
        QCOMPARE(rig.page->view(), ShaderPacksPage::View::List);
        QCOMPARE(rig.names(), QStringList({ S("BSL"), S("Complementary"), S("Folder Pack") }));  // by name; the .txt is no pack
        QCOMPARE(rig.page->captionLabel()->text(), S("3 installed · 3 on"));
        QVERIFY(rig.page->subtitleLabel()->text().contains(S("Iris or OptiFine")));
        const auto& row = rig.page->listModel()->rowAt(rig.rowOf(S("BSL")));
        QVERIFY(row.version.contains(S("·")));  // size and date: only real data, no "Unknown"
        QVERIFY(!row.version.contains(S("Unknown")));
        QCOMPARE(row.source, Mods::Installed::Source::Local);
        QCOMPARE(rig.page->id(), S("shaderpacks"));
    }

    void toggleRenamesToDisabledAndBack()
    {
        Rig rig;
        rig.addSet();
        rig.build();
        QVERIFY(rig.open(2));
        rig.page->requestToggle({ rig.rowOf(S("BSL")) });
        QVERIFY(!QFile::exists(rig.shaders.filePath(S("BSL.zip"))));
        QVERIFY(QFile::exists(rig.shaders.filePath(S("BSL.zip.disabled"))));
        QVERIFY(!rig.enabled(S("BSL")));
        QVERIFY(rig.enabled(S("Complementary")));
        QCOMPARE(rig.page->captionLabel()->text(), S("2 installed · 1 on"));
        rig.page->requestToggle({ rig.rowOf(S("BSL")) });
        QVERIFY(QFile::exists(rig.shaders.filePath(S("BSL.zip"))));
        QVERIFY(!QFile::exists(rig.shaders.filePath(S("BSL.zip.disabled"))));
        QVERIFY(rig.enabled(S("BSL")));
    }

    void spaceTogglesTheSelectedRow()
    {
        Rig rig;
        rig.addSet();
        rig.build();
        QVERIFY(rig.open(2));
        rig.page->selectRow(rig.rowOf(S("Complementary")));
        rig.page->listView()->setFocus();
        QTest::keyClick(rig.page->listView(), Qt::Key_Space);
        QVERIFY(QFile::exists(rig.shaders.filePath(S("Complementary.zip.disabled"))));
    }

    void aFolderPackCannotBeTurnedOff()
    {
        Rig rig;
        rig.addFolder(S("Folder Pack"));
        rig.build();
        QVERIFY(rig.open(1));
        rig.page->requestToggle({ 0 });
        QVERIFY(QDir(rig.shaders.filePath(S("Folder Pack"))).exists());
        QVERIFY(rig.enabled(S("Folder Pack")));
        QVERIFY(toastTexts(rig.page->window()).join(QLatin1Char('|')).contains(S("folders can't be turned off")));
    }

    void searchFiltersAndNoMatchesState()
    {
        Rig rig;
        rig.addSet();
        rig.build();
        QVERIFY(rig.open(2));
        rig.page->setSearchText(S("bsl"));
        QCOMPARE(rig.names(), QStringList({ S("BSL") }));
        QCOMPARE(rig.page->captionLabel()->text(), S("1 of 2 shown"));
        QCOMPARE(rig.page->view(), ShaderPacksPage::View::List);
        rig.page->setSearchText(S("zzz"));
        QCOMPARE(rig.page->view(), ShaderPacksPage::View::NoMatches);
        QVERIFY(rig.page->emptyState()->isVisibleTo(rig.page.get()));
        rig.page->setSearchText(QString());
        QCOMPARE(rig.page->view(), ShaderPacksPage::View::List);
        QCOMPARE(rig.names().size(), 2);
    }

    void noShaderModShowsTheStripAndAShaderModHidesIt()
    {
        Rig rig;
        rig.addSet();
        rig.build();
        QVERIFY(rig.open(2));
        QVERIFY(rig.page->noticeShown());
        QVERIFY(rig.page->shaderModNotice()->label()->text().contains(S("no shader mod")));
        QVERIFY(rig.page->shaderModNotice()->label()->text().contains(S("Iris")));
        // its button opens Discover > Mods for Iris
        QTest::mouseClick(rig.page->shaderModNotice()->button(), Qt::LeftButton);
        QCOMPARE(rig.findCalls, QStringList({ S("Iris") }));
        // a shader mod in <game root>/mods: no strip
        rig.addShaderMod();
        rig.page->rebuildNow();
        QVERIFY(!rig.page->noticeShown());
        // a disabled one does not count
        QVERIFY(QFile::rename(rig.mods.filePath(S("iris-fabric-1.7.jar")), rig.mods.filePath(S("iris-fabric-1.7.jar.disabled"))));
        rig.page->rebuildNow();
        QVERIFY(rig.page->noticeShown());
    }

    void noStripWhenTheNavigationDoesNotExist()
    {
        Rig rig;
        rig.withAdd = false;
        rig.addSet();
        rig.build();
        QVERIFY(rig.open(2));
        QVERIFY(rig.page->noticeShown());
        QVERIFY(!rig.page->shaderModNotice()->button()->isVisibleTo(rig.page.get()));  // text only
        QVERIFY(!rig.page->downloadButton()->isVisibleTo(rig.page.get()));
    }

    void emptyStateOffersDownload()
    {
        Rig rig;
        rig.build();
        QVERIFY(rig.open(0));
        QCOMPARE(rig.page->view(), ShaderPacksPage::View::Empty);
        QVERIFY(rig.page->emptyState()->isVisibleTo(rig.page.get()));
        QVERIFY(!rig.page->searchField()->isVisibleTo(rig.page.get()));
        QVERIFY(rig.page->addFileButton()->isVisibleTo(rig.page.get()));
        emit rig.page->emptyState()->actionTriggered();
        QCOMPARE(rig.addCalls, 1);
    }

    void skeletonWhileLoading()
    {
        Rig rig;
        rig.addSet();
        rig.build();
        QCOMPARE(rig.page->view(), ShaderPacksPage::View::Loading);
        QVERIFY(!rig.page->isLoaded());
    }

    void downloadButtonCallsTheEnvironment()
    {
        Rig rig;
        rig.addSet();
        rig.build();
        QVERIFY(rig.open(2));
        QVERIFY(rig.page->downloadButton()->isVisibleTo(rig.page.get()));
        QCOMPARE(rig.page->downloadButton()->text(), S("Download shaders"));
        QTest::mouseClick(rig.page->downloadButton(), Qt::LeftButton);
        QCOMPARE(rig.addCalls, 1);
    }

    void runningLocksEverything()
    {
        Rig rig;
        rig.addSet();
        rig.build();
        QVERIFY(rig.open(2));
        QVERIFY(!rig.page->banner()->isVisibleTo(rig.page.get()));
        rig.running = true;
        rig.page->refreshRunning();
        QVERIFY(rig.page->isLocked());
        QVERIFY(rig.page->banner()->isVisibleTo(rig.page.get()));
        QCOMPARE(rig.page->banner()->accessibleName(), S("Close the game to change shaders"));
        QVERIFY(rig.page->listView()->rowDelegate()->locked());
        rig.page->requestToggle({ rig.rowOf(S("BSL")) });
        rig.answers = { 1 };
        rig.page->requestRemove({ rig.rowOf(S("BSL")) });
        rig.page->addPaths({ rig.root.filePath(S("x.zip")) });
        QVERIFY(rig.prompts.isEmpty());
        QVERIFY(rig.trashCalls.isEmpty());
        QVERIFY(QFile::exists(rig.shaders.filePath(S("BSL.zip"))));
        rig.running = false;
        rig.page->refreshRunning();
        QVERIFY(!rig.page->isLocked());
        QVERIFY(!rig.page->banner()->isVisibleTo(rig.page.get()));
        rig.page->requestToggle({ rig.rowOf(S("BSL")) });
        QVERIFY(QFile::exists(rig.shaders.filePath(S("BSL.zip.disabled"))));
    }

    void removeAsksThenTrashes()
    {
        Rig rig;
        rig.addSet();
        rig.build();
        QVERIFY(rig.open(2));
        // Cancel: nothing happens
        rig.answers = { 0 };
        rig.page->requestRemove({ rig.rowOf(S("BSL")) });
        QCOMPARE(rig.prompts.size(), 1);
        QCOMPARE(rig.prompts.first().title, S("Remove BSL?"));
        QVERIFY(rig.trashCalls.isEmpty());
        QVERIFY(QFile::exists(rig.shaders.filePath(S("BSL.zip"))));
        // Remove
        rig.answers = { 1 };
        rig.page->requestRemove({ rig.rowOf(S("BSL")) });
        QCOMPARE(rig.trashCalls, QStringList({ S("BSL.zip") }));
        QVERIFY(!QFile::exists(rig.shaders.filePath(S("BSL.zip"))));
        QVERIFY(toastTexts(rig.page->window()).contains(S("Removed BSL")));
        QVERIFY(rig.open(1));
        QCOMPARE(rig.names(), QStringList({ S("Complementary") }));
    }

    void removeThatCannotGoToTheRecycleBinKeepsTheFile()
    {
        Rig rig;
        rig.addSet();
        rig.build();
        QVERIFY(rig.open(2));
        rig.failTrash.insert(S("BSL.zip"));
        rig.answers = { 1 };
        rig.page->requestRemove({ rig.rowOf(S("BSL")) });
        QVERIFY(QFile::exists(rig.shaders.filePath(S("BSL.zip"))));  // never deleted for good
        QVERIFY(toastTexts(rig.page->window()).contains(S("Couldn't move BSL to the Recycle Bin")));
    }

    void deleteKeyRemovesTheSelection()
    {
        Rig rig;
        rig.addSet();
        rig.build();
        QVERIFY(rig.open(2));
        rig.answers = { 1 };
        rig.page->selectRow(rig.rowOf(S("BSL")));
        rig.page->listView()->setFocus();
        QTest::keyClick(rig.page->listView(), Qt::Key_Delete);
        QCOMPARE(rig.prompts.size(), 1);
        QCOMPARE(rig.trashCalls, QStringList({ S("BSL.zip") }));
    }

    void addFileCopiesAndAsksBeforeReplacing()
    {
        Rig rig;
        rig.addSet();
        rig.build();
        QVERIFY(rig.open(2));
        QTemporaryDir outside;
        const QString fresh = outside.filePath(S("Sildurs.zip"));
        QVERIFY(writeZip(fresh));
        rig.page->addPaths({ fresh, outside.filePath(S("notes.txt")) });
        QVERIFY(QFile::exists(rig.shaders.filePath(S("Sildurs.zip"))));
        QVERIFY(rig.prompts.isEmpty());
        // a pack of the same name: Cancel keeps it, Replace sends the old one to the Recycle Bin first
        const QString again = outside.filePath(S("BSL.zip"));
        QVERIFY(writeZip(again));
        rig.answers = { 0 };
        rig.page->addPaths({ again });
        QCOMPARE(rig.prompts.size(), 1);
        QVERIFY(rig.trashCalls.isEmpty());
        rig.answers = { 1 };
        rig.page->addPaths({ again });
        QCOMPARE(rig.trashCalls, QStringList({ S("BSL.zip") }));
        QVERIFY(QFile::exists(rig.shaders.filePath(S("BSL.zip"))));
    }

    void rowMenuHasTheRealActionsOnly()
    {
        Rig rig;
        rig.addSet();
        rig.build();
        QVERIFY(rig.open(2));
        QMenu menu;
        rig.page->fillRowMenu(&menu, rig.rowOf(S("BSL")));
        QStringList texts;
        for (const QAction* action : menu.actions()) {
            if (!action->isSeparator()) {
                texts.append(action->text());
            }
        }
        QCOMPARE(texts, QStringList({ S("Turn off"), S("Show in folder"), S("Remove") }));  // no homepage without metadata, no change version
    }

    void overflowMenuEntries()
    {
        Rig rig;
        rig.addSet();
        rig.build();
        QVERIFY(rig.open(2));
        QStringList texts;
        for (const QAction* action : rig.page->overflowButton()->menu()->actions()) {
            if (!action->isSeparator()) {
                texts.append(action->text());
            }
        }
        // the test environment has no update checker and no classic page: those entries are hidden, not stubbed
        QCOMPARE(texts, QStringList({ S("Add folder…"), S("Open shader packs folder") }));
    }

    void sharedRemoveTexts()
    {
        using namespace Zero::Mods::Installed;
        QCOMPARE(removeConfirm({ S("a"), S("b") }, Noun::Shaders).title, S("Remove 2 shader packs?"));
        QCOMPARE(removeConfirm({ S("a"), S("b") }, Noun::Packs).title, S("Remove 2 packs?"));
        QCOMPARE(removeResult({ S("a"), S("b") }, {}, Noun::Shaders).text, S("Removed 2 shader packs"));
        QCOMPARE(removeResult({}, { S("a"), S("b") }, Noun::Shaders).text, S("Couldn't move 2 shader packs to the Recycle Bin"));
    }

    // ---- project icons ---------------------------------------------------------------------------------------------------------

    void platformPacksShowTheirProjectIconAndLocalOnesKeepTheSun()
    {
        Zero::ShaderIcons::sessionUrls().clear();
        Zero::ShaderIcons::sessionAsked().clear();
        const QColor mark(255, 0, 255);
        QImage picture(64, 64, QImage::Format_ARGB32);
        picture.fill(mark);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        picture.save(&buffer, "PNG");
        QStringList fetched;
        Zero::RemoteImageCache images([&](const QString& url, std::function<void(const Zero::RemoteImageCache::FetchResult&)> done) {
            fetched.append(url);
            Zero::RemoteImageCache::FetchResult result;
            result.ok = true;
            result.data = png;
            done(result);
        });
        int lookups = 0;
        Zero::ShaderIcons::Ids asked;
        Rig rig;
        rig.images = &images;
        rig.fixture = std::make_unique<InstanceFixture>(rig.root.path());
        rig.iconLookup = [&](const Zero::ShaderIcons::Ids& ids, std::function<void(const QHash<QString, QString>&)> done) {
            ++lookups;
            asked = ids;
            done({ { S("modrinth:PROJ1"), S("https://cdn.test/icon.png") } });
        };
        rig.addZip(S("Platform.zip"));
        rig.addZip(S("Local.zip"));
        rig.addMetadata(S("Platform.zip"), S("PROJ1"), ModPlatform::ResourceProvider::MODRINTH);
        rig.build();
        QVERIFY(rig.open(2));
        QTRY_VERIFY_WITH_TIMEOUT(rig.page->listModel()->rowCount() == 2 && (rig.page->listModel()->rowAt(0).hasMetadata ||
                                                                              rig.page->listModel()->rowAt(1).hasMetadata), 5000);
        const int platformRow = rig.page->listModel()->rowAt(0).hasMetadata ? 0 : 1;
        const int localRow = 1 - platformRow;
        QVERIFY(rig.page->listModel()->rowAt(platformRow).hasIcon);
        QVERIFY(!rig.page->listModel()->rowAt(localRow).hasIcon);
        auto* list = rig.page->listView();
        const auto markedPixels = [&](int rowIndex) {
            const QImage shot = list->viewport()->grab().toImage().convertToFormat(QImage::Format_ARGB32);
            const QRect row = list->visualRect(rig.page->listModel()->index(rowIndex));
            int count = 0;
            for (int y = row.top(); y < row.bottom() && y < shot.height(); ++y) {
                for (int x = row.left(); x < row.left() + 80 && x < shot.width(); ++x) {
                    count += shot.pixelColor(x, y) == mark ? 1 : 0;
                }
            }
            return count;
        };
        QTRY_VERIFY_WITH_TIMEOUT(markedPixels(platformRow) > 100, 5000);
        QCOMPARE(markedPixels(localRow), 0);
        QCOMPARE(asked.modrinth, QStringList({ S("PROJ1") }));  // one batch, only the pack with metadata
        QVERIFY(asked.curseforge.isEmpty());
        QCOMPARE(fetched, QStringList({ S("https://cdn.test/icon.png") }));

        // reopening in the same session asks nobody again
        rig.page->rebuildNow();
        rig.page->rebuildNow();
        QCOMPARE(lookups, 1);
    }

    // ---- Screenshots (only with ZERO_SHADER_SHOTS=<dir>) ---------------------------------------------------------------------

    void screenshots()
    {
        const QString dir = qEnvironmentVariable("ZERO_SHADER_SHOTS");
        if (dir.isEmpty()) {
            return;
        }
        QDir().mkpath(dir);
        for (const QString& themeId : { QStringLiteral("obsidian"), QStringLiteral("minecraft") }) {
            ThemeSpec spec = themeId == QLatin1String("obsidian") ? ThemeSpec::obsidian() : ThemeSpec::minecraft();
            spec.reduceMotion = true;
            Theme::setCurrent(spec);
            qApp->setPalette(Style::palette(spec));
            qApp->setStyleSheet(Style::styleSheet(spec));
            for (const bool filled : { true, false }) {
                Rig rig;
                if (filled) {
                    rig.addSet();
                }
                rig.build();
                QVERIFY(rig.open(filled ? 2 : 0));
                QTest::qWait(100);
                rig.page->grab().save(dir + QLatin1Char('/') + themeId + (filled ? QStringLiteral("-shaders-2-packs.png") : QStringLiteral("-shaders-empty.png")));
            }
        }
    }
};

QTEST_MAIN(ZeroShaderPacksPageTest)

#include "ZeroShaderPacksPage_test.moc"
