// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the instance window's Resource packs page (docs/zero/CONTENT-PACKS.md, "D2 Installed resource packs"): the page
// against a REAL ResourcePackFolderModel on a temp folder with tiny zips (pack.mcmeta + pack.png), a folder pack and a ".zip.disabled" file,
// and a temp options.txt. Groups and order come from options.txt; turning a pack on / off and moving it write exactly one line (everything
// else stays byte for byte, the slots of `vanilla` / `fabric` stay); Undo restores; Remove goes through an injected trash and drops the
// entry; the running lock refuses everything and shows the banner; options.txt is read again when the page is shown and after the game
// stops; Minecraft 1.12 and an unreadable pack list show a note and no switches; the empty state; the shared widget parts. Offscreen, no
// network, parallel-safe (every test owns its temp folder).
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QBuffer>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <memory>

#include "archive/ArchiveWriter.h"
#include "minecraft/mod/ResourcePackFolderModel.h"
#include "ui/zero/ZeroEmptyState.h"
#include "ui/zero/ZeroSkeleton.h"
#include "ui/zero/ZeroToast.h"
#include "ui/zero/content/FilterPanel.h"
#include "ui/zero/instance/InstancePageParts.h"
#include "ui/zero/instance/PackRollbackNotice.h"
#include "ui/zero/instance/ResourcePacksPage.h"
#include "zero/content/PackRollback.h"
#include "zero/mods/InstalledModsModel.h"

using namespace Zero;
using namespace Zero::PacksUi;
using namespace Zero::Content::Packs;
using Zero::ModsUi::ChoicePrompt;

namespace {

QString S(const char* text)
{
    return QString::fromUtf8(text);
}

QByteArray B(const char* text)
{
    return QByteArray(text);
}

QByteArray pngBytes(int size = 16)
{
    QImage image(size, size, QImage::Format_ARGB32);
    image.fill(QColor(200, 40, 40));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

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

QByteArray mcmeta(int format, const QString& descriptionJson)
{
    return QStringLiteral(R"RAW({"pack":{"pack_format":%1,"description":%2}})RAW").arg(format).arg(descriptionJson).toUtf8();
}

/// A tiny pack: a zip with pack.mcmeta and pack.png.
bool writeZip(const QString& path, int format, const QString& descriptionJson)
{
    MMCZip::ArchiveWriter writer(path);
    if (!writer.open()) {
        return false;
    }
    if (!writer.addFile(QStringLiteral("pack.mcmeta"), mcmeta(format, descriptionJson)) || !writer.addFile(QStringLiteral("pack.png"), pngBytes())) {
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

QByteArray options(const QStringList& entries, const QByteArray& before = B("version:3465\nfov:0.5\n"), const QByteArray& after = B("lang:en_us\n"))
{
    return before + QByteArray("resourcePacks:") + QJsonDocument(QJsonArray::fromStringList(entries)).toJson(QJsonDocument::Compact) + B("\n") + after;
}

struct Rig {
    QTemporaryDir root;
    QDir packs{ root.path() + S("/.minecraft/resourcepacks") };
    std::unique_ptr<ResourcePackFolderModel> model;
    std::unique_ptr<ResourcePacksPage> page;

    // the fakes
    bool running = false;
    QString mc = S("1.21.1");
    QStringList trashCalls;
    QSet<QString> failTrash;
    QList<ChoicePrompt> prompts;
    QList<int> answers;  ///< consumed one per question; empty = cancel
    int addCalls = 0;
    bool withAdd = true;
    QString instanceName = S("Test Instance");

    Rig() { QDir().mkpath(packs.absolutePath()); }

    QString optionsPath() const { return root.filePath(S(".minecraft/options.txt")); }
    QString historyPath() const { return root.filePath(S(".zero/pack-order.json")); }
    QByteArray optionsBytes() const { return readFile(optionsPath()); }
    bool writeOptions(const QByteArray& bytes) const { return writeFile(optionsPath(), bytes); }

    void addZip(const QString& name, int format = 34, const QString& description = S("\"A pack\""))
    {
        QVERIFY(writeZip(packs.filePath(name), format, description));
    }
    void addFolder(const QString& name, int format = 34)
    {
        QVERIFY(QDir().mkpath(packs.filePath(name)));
        QVERIFY(writeFile(packs.filePath(name + S("/pack.mcmeta")), mcmeta(format, S("\"Folder pack\""))));
        QVERIFY(writeFile(packs.filePath(name + S("/pack.png")), pngBytes()));
    }
    /// Alpha, Beta, Gamma (zips), "Folder Pack" and a hidden "Hidden.zip.disabled".
    void addSet()
    {
        addZip(S("Alpha.zip"));
        addZip(S("Beta.zip"));
        addZip(S("Gamma.zip"));
        addFolder(S("Folder Pack"));
        QVERIFY(writeZip(packs.filePath(S("Hidden.zip.disabled")), 34, S("\"Hidden one\"")));
    }

    /// What a launch that ended with "Caught error loading resourcepacks" leaves (zero/content/PackRollback).
    void writeRollbackMarker(const QStringList& attempted, const QStringList& suspectFiles) const
    {
        Content::Packs::Rollback::Marker marker;
        marker.valid = true;
        marker.time = QDateTime::currentDateTimeUtc();
        marker.attempted = attempted;
        for (const QString& file : suspectFiles) {
            marker.suspects.append({ file, S("minecraft:core/entity") });
        }
        QVERIFY(Content::Packs::Rollback::writeMarker(Content::Packs::Rollback::markerPathForHistory(historyPath()), marker));
    }

    Zero::PacksUi::PacksPageEnvironment environment()
    {
        PacksPageEnvironment env;
        env.target = [this]() {
            Content::TargetInstance target;
            target.id = S("inst");
            target.name = instanceName;
            target.mcVersion = mc;
            return target;
        };
        env.isRunning = [this]() { return running; };
        env.optionsPath = [this]() { return optionsPath(); };
        env.historyPath = [this]() { return historyPath(); };
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
            env.addPacks = [this]() { ++addCalls; };
        }
        return env;
    }

    void build()
    {
        model = std::make_unique<ResourcePackFolderModel>(packs.absolutePath(), nullptr, true, true);
        page = std::make_unique<ResourcePacksPage>(environment(), model.get());
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

    /// The lines of the list as text: "#On", "Alpha", "Default resources", "#Available"...
    QStringList lines() const
    {
        QStringList out;
        for (int i = 0; i < page->listModel()->rowCount(); ++i) {
            const auto& entry = page->listModel()->entryAt(i);
            out.append(entry.kind == PackListModel::Kind::Pack ? entry.row.name : QStringLiteral("#") + entry.text);
        }
        return out;
    }
    int rowOf(const QString& name) const
    {
        for (int i = 0; i < page->listModel()->rowCount(); ++i) {
            if (page->listModel()->isPack(i) && page->listModel()->entryAt(i).row.name == name) {
                return i;
            }
        }
        return -1;
    }
    const PackRow& rowNamed(const QString& name) const { return page->listModel()->entryAt(rowOf(name)).row; }

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

const QString kOn = QStringLiteral("#") + QStringLiteral("On \u2014 top wins");

}  // namespace

class ZeroResourcePacksPageTest : public QObject {
    Q_OBJECT

   private slots:
    // =============================== groups and rows =========================================================================

    void groupsAndOrderComeFromOptions()
    {
        Rig rig;
        rig.addSet();
        QVERIFY(rig.writeOptions(options({ S("vanilla"), S("file/Alpha.zip"), S("fabric"), S("file/Folder Pack") })));
        rig.build();
        QVERIFY(rig.open(5));
        QCOMPARE(rig.page->view(), ResourcePacksPage::View::List);
        QCOMPARE(rig.page->orderState(), ResourcePacksPage::OrderState::Ready);
        // top wins: the folder pack (last in the array) first, then the fixed default row, then the rest by name, the hidden file last
        QCOMPARE(rig.lines(), (QStringList{ kOn, S("Folder Pack"), S("Alpha"), S("#Default resources"), S("#Available"), S("Beta"), S("Gamma"), S("Hidden") }));
        QCOMPARE(rig.page->activePacks(), (QStringList{ S("Folder Pack"), S("Alpha.zip") }));
        QCOMPARE(rig.page->listModel()->entryAt(rig.rowOf(S("Folder Pack"))).onIndex, 0);
        QCOMPARE(rig.page->listModel()->entryAt(rig.rowOf(S("Alpha"))).onIndex, 1);
        QCOMPARE(rig.page->listModel()->entryAt(rig.rowOf(S("Beta"))).onIndex, -1);
        QVERIFY(rig.rowNamed(S("Folder Pack")).folder);
        QVERIFY(rig.rowNamed(S("Hidden")).hidden);
        QVERIFY(!rig.rowNamed(S("Hidden")).active);
        QCOMPARE(rig.page->captionLabel()->text(), S("5 packs \u00b7 2 on"));
        // only the packs are selectable: headers and the default row are not
        QVERIFY(!rig.page->listModel()->isPack(0));
        QCOMPARE(rig.page->listModel()->flags(rig.page->listModel()->index(0)), Qt::NoItemFlags);
        QCOMPARE(rig.page->listModel()->flags(rig.page->listModel()->index(3)), Qt::NoItemFlags);
        QVERIFY(rig.page->listModel()->flags(rig.page->listModel()->index(1)).testFlag(Qt::ItemIsSelectable));
        QCOMPARE(rig.page->id(), S("resourcepacks"));
        QVERIFY(rig.page->shouldDisplay());
        // opening the page changed nothing on disk
        QCOMPARE(rig.optionsBytes(), options({ S("vanilla"), S("file/Alpha.zip"), S("fabric"), S("file/Folder Pack") }));
        QVERIFY(!QFile::exists(rig.historyPath()));
    }

    void rowsShowPlainDescriptionsAndFormatNotes()
    {
        Rig rig;
        rig.addZip(S("Colorful.zip"), 34, S(R"RAW([{"text":"Hello ","color":"red"},{"text":"world","bold":true}])RAW"));
        rig.addZip(S("Legacy.zip"), 15, S("\"\\u00a7aGreen \\u00a7lbold\""));
        rig.addZip(S("Unknown.zip"), 99, S("\"x\""));
        rig.build();
        QVERIFY(rig.open(3));
        QCOMPARE(rig.rowNamed(S("Colorful")).description, S("Hello world"));  // not the HTML of the spans
        QVERIFY(!rig.rowNamed(S("Colorful")).description.contains(QLatin1Char('<')));
        QCOMPARE(rig.rowNamed(S("Legacy")).description, S("Green bold"));
        // pack 34 fits 1.21.1; 15 was made for 1.20; 99 is not known: no claim
        QCOMPARE(rig.rowNamed(S("Colorful")).fit.kind, FitKind::Fits);
        QCOMPARE(rig.rowNamed(S("Legacy")).fit.kind, FitKind::MadeFor);
        QCOMPARE(rig.rowNamed(S("Legacy")).fit.madeFor, S("1.20"));
        QCOMPARE(rig.rowNamed(S("Unknown")).fit.kind, FitKind::Unknown);
        const auto pills = pillsFor(rig.rowNamed(S("Legacy")));
        QCOMPARE(pills.last().text, S("Made for 1.20"));
        QCOMPARE(int(pills.last().tone), int(Zero::Mods::Installed::PillTone::Warning));
        // no options.txt at all: everything is available, nothing on
        QVERIFY(!QFile::exists(rig.optionsPath()));
        QCOMPARE(rig.page->activePacks(), QStringList());
        QCOMPARE(rig.lines().first(), S("#Default resources").prepend(QString()).isEmpty() ? QString() : kOn);
    }

    void stalePacksInOptionsAreNotListedButKept()
    {
        Rig rig;
        rig.addZip(S("Alpha.zip"));
        rig.addZip(S("Beta.zip"));
        const QByteArray before = options({ S("vanilla"), S("file/Gone.zip"), S("file/Alpha.zip") });
        QVERIFY(rig.writeOptions(before));
        rig.build();
        QVERIFY(rig.open(2));
        QCOMPARE(rig.lines(), (QStringList{ kOn, S("Alpha"), S("#Default resources"), S("#Available"), S("Beta") }));
        QCOMPARE(rig.page->activePacks(), QStringList{ S("Alpha.zip") });
        QVERIFY(rig.page->setPackActive(S("Beta.zip"), true));
        QCOMPARE(rig.optionsBytes(), options({ S("vanilla"), S("file/Gone.zip"), S("file/Alpha.zip"), S("file/Beta.zip") }));
    }

    // =============================== turning on and off ======================================================================

    void turnOnPutsThePackOnTop()
    {
        Rig rig;
        rig.addSet();
        const QByteArray before = options({ S("vanilla"), S("file/Alpha.zip"), S("fabric") });
        QVERIFY(rig.writeOptions(before));
        rig.build();
        QVERIFY(rig.open(5));
        rig.page->requestToggle(rig.rowOf(S("Beta")));
        QCOMPARE(rig.optionsBytes(), options({ S("vanilla"), S("file/Alpha.zip"), S("fabric"), S("file/Beta.zip") }));
        QCOMPARE(rig.page->activePacks(), (QStringList{ S("Beta.zip"), S("Alpha.zip") }));
        QCOMPARE(rig.lines().mid(0, 4), (QStringList{ kOn, S("Beta"), S("Alpha"), S("#Default resources") }));
        QVERIFY(toastTexts(rig.page.get()).contains(S("Turned on Beta")));
        QCOMPARE(readHistory(rig.historyPath()).size(), 1);
        // a folder pack the same way
        rig.page->requestToggle(rig.rowOf(S("Folder Pack")));
        QCOMPARE(rig.page->activePacks().first(), S("Folder Pack"));
        QVERIFY(rig.optionsBytes().contains(B("\"file/Folder Pack\"")));
    }

    void turnOffTakesItOutOfTheList()
    {
        Rig rig;
        rig.addSet();
        QVERIFY(rig.writeOptions(options({ S("vanilla"), S("file/Alpha.zip"), S("fabric"), S("file/Beta.zip") })));
        rig.build();
        QVERIFY(rig.open(5));
        rig.page->requestToggle(rig.rowOf(S("Alpha")));
        QCOMPARE(rig.optionsBytes(), options({ S("vanilla"), S("fabric"), S("file/Beta.zip") }));
        QVERIFY(toastTexts(rig.page.get()).contains(S("Turned off Alpha")));
        QVERIFY(!rig.rowNamed(S("Alpha")).active);
        QCOMPARE(rig.lines().mid(0, 3), (QStringList{ kOn, S("Beta"), S("#Default resources") }));
        QVERIFY(rig.lines().contains(S("Alpha")));
    }

    void noOptionsFileIsCreatedByTheFirstSwitch()
    {
        Rig rig;
        rig.addZip(S("Alpha.zip"));
        rig.build();
        QVERIFY(rig.open(1));
        rig.page->requestToggle(rig.rowOf(S("Alpha")));
        QCOMPARE(rig.optionsBytes(), B("resourcePacks:[\"vanilla\",\"file/Alpha.zip\"]\n"));
        QCOMPARE(rig.page->activePacks(), QStringList{ S("Alpha.zip") });
        // Undo removes the file that was made
        QVERIFY(rig.page->undo());
        QVERIFY(!QFile::exists(rig.optionsPath()));
        QCOMPARE(rig.page->activePacks(), QStringList());
    }

    void crlfAndMissingFinalNewlineSurvive()
    {
        Rig rig;
        rig.addSet();
        QByteArray crlf = options({ S("vanilla"), S("file/Alpha.zip") });
        crlf.replace("\n", "\r\n");
        crlf.chop(2);  // and no final newline
        QVERIFY(rig.writeOptions(crlf));
        rig.build();
        QVERIFY(rig.open(5));
        rig.page->requestToggle(rig.rowOf(S("Gamma")));
        QByteArray expected = options({ S("vanilla"), S("file/Alpha.zip"), S("file/Gamma.zip") });
        expected.replace("\n", "\r\n");
        expected.chop(2);
        QCOMPARE(rig.optionsBytes(), expected);
    }

    // =============================== order ===================================================================================

    void movesKeepTheOtherSlots()
    {
        Rig rig;
        rig.addSet();
        // array (bottom to top): vanilla, Alpha, fabric, Beta, Gamma  =>  the list shows Gamma, Beta, Alpha
        QVERIFY(rig.writeOptions(options({ S("vanilla"), S("file/Alpha.zip"), S("fabric"), S("file/Beta.zip"), S("file/Gamma.zip") })));
        rig.build();
        QVERIFY(rig.open(5));
        QCOMPARE(rig.page->activePacks(), (QStringList{ S("Gamma.zip"), S("Beta.zip"), S("Alpha.zip") }));

        rig.page->requestMove(rig.rowOf(S("Alpha")), ResourcePacksPage::Move::Top);
        QCOMPARE(rig.optionsBytes(), options({ S("vanilla"), S("file/Beta.zip"), S("fabric"), S("file/Gamma.zip"), S("file/Alpha.zip") }));
        QVERIFY(toastTexts(rig.page.get()).contains(S("Moved Alpha to the top")));
        QCOMPARE(rig.page->activePacks(), (QStringList{ S("Alpha.zip"), S("Gamma.zip"), S("Beta.zip") }));

        rig.page->requestMove(rig.rowOf(S("Alpha")), ResourcePacksPage::Move::Down);
        QCOMPARE(rig.page->activePacks(), (QStringList{ S("Gamma.zip"), S("Alpha.zip"), S("Beta.zip") }));
        QCOMPARE(rig.optionsBytes(), options({ S("vanilla"), S("file/Beta.zip"), S("fabric"), S("file/Alpha.zip"), S("file/Gamma.zip") }));
        QVERIFY(toastTexts(rig.page.get()).contains(S("Moved Alpha down")));

        rig.page->requestMove(rig.rowOf(S("Alpha")), ResourcePacksPage::Move::Up);
        QCOMPARE(rig.page->activePacks(), (QStringList{ S("Alpha.zip"), S("Gamma.zip"), S("Beta.zip") }));
        QVERIFY(toastTexts(rig.page.get()).contains(S("Moved Alpha up")));

        rig.page->requestMove(rig.rowOf(S("Alpha")), ResourcePacksPage::Move::Bottom);
        QCOMPARE(rig.page->activePacks(), (QStringList{ S("Gamma.zip"), S("Beta.zip"), S("Alpha.zip") }));
        QCOMPARE(rig.optionsBytes(), options({ S("vanilla"), S("file/Alpha.zip"), S("fabric"), S("file/Beta.zip"), S("file/Gamma.zip") }));
        QVERIFY(toastTexts(rig.page.get()).contains(S("Moved Alpha to the bottom")));
        // every move was one write with its own history entry
        QCOMPARE(readHistory(rig.historyPath()).size(), 4);
    }

    void moveEdgesDoNothing()
    {
        Rig rig;
        rig.addSet();
        const QByteArray before = options({ S("vanilla"), S("file/Alpha.zip"), S("file/Beta.zip") });
        QVERIFY(rig.writeOptions(before));
        rig.build();
        QVERIFY(rig.open(5));
        rig.page->requestMove(rig.rowOf(S("Beta")), ResourcePacksPage::Move::Up);       // already on top
        rig.page->requestMove(rig.rowOf(S("Beta")), ResourcePacksPage::Move::Top);
        rig.page->requestMove(rig.rowOf(S("Alpha")), ResourcePacksPage::Move::Down);    // already last
        rig.page->requestMove(rig.rowOf(S("Alpha")), ResourcePacksPage::Move::Bottom);
        rig.page->requestMove(rig.rowOf(S("Gamma")), ResourcePacksPage::Move::Top);     // not on
        QVERIFY(!rig.page->movePack(0, 0));
        QVERIFY(!rig.page->movePack(0, 5));
        QVERIFY(!rig.page->movePack(-1, 1));
        QCOMPARE(rig.optionsBytes(), before);
        QVERIFY(!QFile::exists(rig.historyPath()));
    }

    void movePackIsWhatADropCalls()
    {
        Rig rig;
        rig.addSet();
        QVERIFY(rig.writeOptions(options({ S("vanilla"), S("file/Alpha.zip"), S("file/Beta.zip"), S("file/Gamma.zip") })));
        rig.build();
        QVERIFY(rig.open(5));
        QCOMPARE(rig.page->activePacks(), (QStringList{ S("Gamma.zip"), S("Beta.zip"), S("Alpha.zip") }));
        QVERIFY(rig.page->movePack(2, 0));  // Alpha dropped on top
        QCOMPARE(rig.page->activePacks(), (QStringList{ S("Alpha.zip"), S("Gamma.zip"), S("Beta.zip") }));
        QVERIFY(rig.page->movePack(0, 1));
        QCOMPARE(rig.page->activePacks(), (QStringList{ S("Gamma.zip"), S("Alpha.zip"), S("Beta.zip") }));
        QVERIFY(toastTexts(rig.page.get()).contains(S("Moved Alpha down")) || toastTexts(rig.page.get()).contains(S("Moved Alpha")));
        QCOMPARE(rig.optionsBytes(), options({ S("vanilla"), S("file/Beta.zip"), S("file/Alpha.zip"), S("file/Gamma.zip") }));
    }

    void dropSlotsFollowTheRows()
    {
        Rig rig;
        rig.addSet();
        QVERIFY(rig.writeOptions(options({ S("vanilla"), S("file/Alpha.zip"), S("file/Beta.zip"), S("file/Gamma.zip") })));
        rig.build();
        QVERIFY(rig.open(5));
        PackListView* view = rig.page->listView();
        const auto rowRect = [&](const QString& name) { return view->visualRect(rig.page->listModel()->index(rig.rowOf(name))); };
        // on rows: Gamma (0), Beta (1), Alpha (2). Above the first one = slot 0, below the last one's middle = slot 3.
        QCOMPARE(view->dropSlotAt(rowRect(S("Gamma")).top() + 2), 0);
        QCOMPARE(view->dropSlotAt(rowRect(S("Beta")).top() + 2), 1);
        QCOMPARE(view->dropSlotAt(rowRect(S("Alpha")).top() + 2), 2);
        QCOMPARE(view->dropSlotAt(rowRect(S("Alpha")).top() + PackDelegate::kRowHeight), 3);
        QCOMPARE(view->dropSlotAt(-50), 0);
        QCOMPARE(view->dropSlotAt(10000), 3);
    }

    void draggingAHandleReorders()
    {
        Rig rig;
        rig.addSet();
        QVERIFY(rig.writeOptions(options({ S("vanilla"), S("file/Alpha.zip"), S("file/Beta.zip"), S("file/Gamma.zip") })));
        rig.build();
        QVERIFY(rig.open(5));
        rig.page->show();
        PackListView* view = rig.page->listView();
        QSignalSpy reorder(view, &PackListView::reorderRequested);
        const QRect alpha = view->visualRect(rig.page->listModel()->index(rig.rowOf(S("Alpha"))));
        const QRect gamma = view->visualRect(rig.page->listModel()->index(rig.rowOf(S("Gamma"))));
        const QPoint grip = PackDelegate::gripRect(PackDelegate::cardRect(alpha)).center();
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, grip);
        QTest::mouseMove(view->viewport(), grip + QPoint(0, -30));
        QTest::mouseMove(view->viewport(), QPoint(grip.x(), gamma.top() + 2));
        QVERIFY(view->isDragging());
        QCOMPARE(view->indicatorSlot(), 0);
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(grip.x(), gamma.top() + 2));
        QVERIFY(!view->isDragging());
        QCOMPARE(reorder.count(), 1);
        QCOMPARE(reorder.first().at(0).toInt(), 2);
        QCOMPARE(reorder.first().at(1).toInt(), 0);
        QCOMPARE(rig.page->activePacks(), (QStringList{ S("Alpha.zip"), S("Gamma.zip"), S("Beta.zip") }));
        // Esc cancels a drag: nothing is written
        const QByteArray after = rig.optionsBytes();
        const QRect beta = view->visualRect(rig.page->listModel()->index(rig.rowOf(S("Beta"))));
        const QPoint betaGrip = PackDelegate::gripRect(PackDelegate::cardRect(beta)).center();
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, betaGrip);
        QTest::mouseMove(view->viewport(), betaGrip + QPoint(0, -40));
        QTest::mouseMove(view->viewport(), betaGrip + QPoint(0, -60));
        QVERIFY(view->isDragging());
        view->cancelDrag();
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, betaGrip + QPoint(0, -60));
        QCOMPARE(reorder.count(), 1);
        QCOMPARE(rig.optionsBytes(), after);
    }

    void altArrowsMoveTheSelectedPack()
    {
        Rig rig;
        rig.addSet();
        QVERIFY(rig.writeOptions(options({ S("vanilla"), S("file/Alpha.zip"), S("file/Beta.zip"), S("file/Gamma.zip") })));
        rig.build();
        QVERIFY(rig.open(5));
        rig.page->selectRow(rig.rowOf(S("Beta")));
        QCOMPARE(rig.page->selectedRow(), rig.rowOf(S("Beta")));
        QTest::keyClick(rig.page->listView(), Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(rig.page->activePacks(), (QStringList{ S("Beta.zip"), S("Gamma.zip"), S("Alpha.zip") }));
        QCOMPARE(rig.page->selectedRow(), rig.rowOf(S("Beta")));  // the row stays selected: the key can be repeated
        QTest::keyClick(rig.page->listView(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(rig.page->activePacks(), (QStringList{ S("Gamma.zip"), S("Beta.zip"), S("Alpha.zip") }));
        // an available pack has no order
        rig.page->selectRow(rig.rowOf(S("Folder Pack")));
        const QByteArray before = rig.optionsBytes();
        QTest::keyClick(rig.page->listView(), Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(rig.optionsBytes(), before);
    }

    void spaceTogglesTheSelectedPack()
    {
        Rig rig;
        rig.addSet();
        QVERIFY(rig.writeOptions(options({ S("vanilla") })));
        rig.build();
        QVERIFY(rig.open(5));
        rig.page->selectRow(rig.rowOf(S("Gamma")));
        QTest::keyClick(rig.page->listView(), Qt::Key_Space);
        QCOMPARE(rig.page->activePacks(), QStringList{ S("Gamma.zip") });
        QTest::keyClick(rig.page->listView(), Qt::Key_Space);
        QCOMPARE(rig.page->activePacks(), QStringList());
    }

    // =============================== Undo ====================================================================================

    void undoRestoresTheExactBytes()
    {
        Rig rig;
        rig.addSet();
        const QByteArray before = options({ S("vanilla"), S("file/Alpha.zip"), S("fabric") }, B("a:1\r\n"), B("z:2\r\n"));
        QVERIFY(rig.writeOptions(before));
        rig.build();
        QVERIFY(rig.open(5));
        rig.page->requestToggle(rig.rowOf(S("Beta")));
        QVERIFY(rig.optionsBytes() != before);
        QVERIFY(rig.page->undo());
        QCOMPARE(rig.optionsBytes(), before);
        QCOMPARE(rig.page->activePacks(), QStringList{ S("Alpha.zip") });
        QVERIFY(toastTexts(rig.page.get()).contains(S("Restored the previous order")));
        // nothing left to restore
        QVERIFY(!rig.page->undo());
        QVERIFY(toastTexts(rig.page.get()).contains(S("There is no earlier order to restore")));
        QCOMPARE(rig.optionsBytes(), before);
    }

    // =============================== the game turned the packs off ===========================================================

    void rollbackNoticeNamesTheSuspectAndRestoresWithoutIt()
    {
        Rig rig;
        rig.addSet();
        QVERIFY(rig.writeOptions(options({ S("vanilla") })));
        rig.writeRollbackMarker({ S("vanilla"), S("file/Alpha.zip"), S("file/Beta.zip"), S("file/Gamma.zip") }, { S("Beta.zip") });
        rig.build();
        QVERIFY(rig.open(5));
        QVERIFY(rig.page->rollbackShown());
        auto* notice = rig.page->rollbackNotice();
        QCOMPARE(notice->titleLabel()->text(), S("Minecraft turned off your resource packs"));
        QCOMPARE(notice->bodyLabel()->text(),
                 S("When the game started, Beta failed to load (it changes the game's shaders), so Minecraft turned every pack off."));
        QVERIFY(notice->restoreWithoutButton()->isVisibleTo(rig.page.get()));
        QCOMPARE(notice->restoreWithoutButton()->text(), S("Restore without Beta"));
        QCOMPARE(notice->restoreAllButton()->text(), S("Restore all"));
        // only the suspect carries the pill
        QVERIFY(rig.rowNamed(S("Beta")).brokeLastLaunch);
        QCOMPARE(rig.rowNamed(S("Beta")).brokeShader, S("minecraft:core/entity"));
        QVERIFY(!rig.rowNamed(S("Alpha")).brokeLastLaunch);

        QTest::mouseClick(notice->restoreWithoutButton(), Qt::LeftButton);
        QCOMPARE(rig.page->activePacks(), (QStringList{ S("Gamma.zip"), S("Alpha.zip") }));  // top first
        QVERIFY(!QFileInfo::exists(Content::Packs::Rollback::markerPathForHistory(rig.historyPath())));
        QVERIFY(!rig.page->rollbackShown());
        QVERIFY(!rig.rowNamed(S("Beta")).brokeLastLaunch);
        QVERIFY(toastTexts(rig.page.get()).contains(S("Turned your resource packs back on, without Beta")));
        // the toast's Undo is the usual one
        QVERIFY(rig.page->undo());
        QVERIFY(rig.page->activePacks().isEmpty());
    }

    void rollbackRestoreAllWithoutSuspects()
    {
        Rig rig;
        rig.addSet();
        QVERIFY(rig.writeOptions(options({ S("vanilla") })));
        rig.writeRollbackMarker({ S("vanilla"), S("file/Alpha.zip"), S("file/Beta.zip"), S("file/Gone.zip") }, {});
        rig.build();
        QVERIFY(rig.open(5));
        auto* notice = rig.page->rollbackNotice();
        QVERIFY(rig.page->rollbackShown());
        QVERIFY(notice->bodyLabel()->text().contains(S("one of your packs")));
        QVERIFY(!notice->restoreWithoutButton()->isVisibleTo(rig.page.get()));  // only "Restore all"
        QVERIFY(notice->restoreAllButton()->isVisibleTo(rig.page.get()));
        QVERIFY(rig.page->restoreRollback(false));
        QCOMPARE(rig.page->activePacks(), (QStringList{ S("Beta.zip"), S("Alpha.zip") }));  // the pack that is gone stays out
        QVERIFY(toastTexts(rig.page.get()).contains(S("Turned your resource packs back on")));
        QVERIFY(!rig.page->rollbackShown());
    }

    void rollbackNoticeIsLockedWhileTheGameRunsAndDismissForgetsIt()
    {
        Rig rig;
        rig.addSet();
        const QByteArray before = options({ S("vanilla") });
        QVERIFY(rig.writeOptions(before));
        rig.writeRollbackMarker({ S("vanilla"), S("file/Alpha.zip") }, { S("Alpha.zip") });
        rig.build();
        QVERIFY(rig.open(5));
        QVERIFY(rig.page->rollbackNotice()->restoreAllButton()->isEnabled());
        rig.running = true;
        rig.page->refreshRunning();
        QVERIFY(!rig.page->rollbackNotice()->restoreAllButton()->isEnabled());
        QVERIFY(!rig.page->restoreRollback(false));
        QCOMPARE(rig.optionsBytes(), before);
        rig.running = false;
        rig.page->refreshRunning();
        // Dismiss: the marker and the notice go, options.txt stays as it is
        QTest::mouseClick(rig.page->rollbackNotice()->dismissButton(), Qt::LeftButton);
        QVERIFY(!QFileInfo::exists(Content::Packs::Rollback::markerPathForHistory(rig.historyPath())));
        QVERIFY(!rig.page->rollbackShown());
        QCOMPARE(rig.optionsBytes(), before);
        QVERIFY(!rig.rowNamed(S("Alpha")).brokeLastLaunch);
    }

    void rollbackNoticeStaysHiddenWithoutAMarkerOrWithAnyPackLeft()
    {
        Rig rig;
        rig.addSet();
        QVERIFY(rig.writeOptions(options({ S("vanilla") })));
        rig.build();
        QVERIFY(rig.open(5));
        QVERIFY(!rig.page->rollbackShown());  // no marker
        // a marker whose packs are all gone from the folder
        rig.writeRollbackMarker({ S("vanilla"), S("file/Gone.zip") }, {});
        rig.page->rebuildNow();
        QVERIFY(!rig.page->rollbackShown());
    }

    void turningAPackOnRemovesTheMarker()
    {
        Rig rig;
        rig.addSet();
        QVERIFY(rig.writeOptions(options({ S("vanilla") })));
        rig.writeRollbackMarker({ S("vanilla"), S("file/Alpha.zip") }, { S("Alpha.zip") });
        rig.build();
        QVERIFY(rig.open(5));
        QVERIFY(rig.page->rollbackShown());
        rig.page->requestToggle(rig.rowOf(S("Beta")));  // any new order written by the launcher
        QVERIFY(!QFileInfo::exists(Content::Packs::Rollback::markerPathForHistory(rig.historyPath())));
        QVERIFY(!rig.page->rollbackShown());
    }

    void undoOfAnOlderChangeIsRefused()
    {
        Rig rig;
        rig.addSet();
        QVERIFY(rig.writeOptions(options({ S("vanilla") })));
        rig.build();
        QVERIFY(rig.open(5));
        QVERIFY(rig.page->setPackActive(S("Alpha.zip"), true));
        const QString firstId = readHistory(rig.historyPath()).first().id;
        QVERIFY(rig.page->setPackActive(S("Beta.zip"), true));
        const QByteArray now = rig.optionsBytes();
        QVERIFY(!rig.page->undo(firstId));  // that toast is out of date: the newest change is Beta's
        QCOMPARE(rig.optionsBytes(), now);
        QVERIFY(rig.page->undo(readHistory(rig.historyPath()).first().id));
        QCOMPARE(rig.page->activePacks(), QStringList{ S("Alpha.zip") });
    }

    void restorePreviousOrderMenuEntry()
    {
        Rig rig;
        rig.addSet();
        QVERIFY(rig.writeOptions(options({ S("vanilla") })));
        rig.build();
        QVERIFY(rig.open(5));
        QMenu* menu = rig.page->overflowButton()->menu();
        QVERIFY(menu);
        emit menu->aboutToShow();
        QVERIFY(!rig.page->restoreAction()->isEnabled());  // no history yet
        QVERIFY(rig.page->setPackActive(S("Alpha.zip"), true));
        emit menu->aboutToShow();
        QVERIFY(rig.page->restoreAction()->isEnabled());
        rig.page->restoreAction()->trigger();
        QCOMPARE(rig.page->activePacks(), QStringList());
        rig.running = true;
        emit menu->aboutToShow();
        QVERIFY(!rig.page->restoreAction()->isEnabled());
        QStringList entries;
        for (const QAction* action : menu->actions()) {
            entries.append(action->text());
        }
        QVERIFY(entries.contains(S("Open resource packs folder")));
        QVERIFY(entries.contains(S("Restore previous order")));
        QVERIFY(!entries.contains(S("Open the classic resource packs page")));  // the environment has no classic page
    }

    // =============================== the running lock ========================================================================

    void runningLocksEverything()
    {
        Rig rig;
        rig.addSet();
        const QByteArray before = options({ S("vanilla"), S("file/Alpha.zip"), S("file/Beta.zip") });
        QVERIFY(rig.writeOptions(before));
        rig.build();
        QVERIFY(rig.open(5));
        QVERIFY(!rig.page->banner()->isVisibleTo(rig.page.get()));
        rig.running = true;
        rig.page->refreshRunning();
        QVERIFY(rig.page->isLocked());
        QVERIFY(rig.page->banner()->isVisibleTo(rig.page.get()));
        QVERIFY(rig.page->listView()->rowDelegate()->locked());
        rig.page->requestToggle(rig.rowOf(S("Gamma")));
        rig.page->requestToggle(rig.rowOf(S("Alpha")));
        rig.page->requestMove(rig.rowOf(S("Alpha")), ResourcePacksPage::Move::Top);
        QVERIFY(!rig.page->movePack(1, 0));
        QVERIFY(!rig.page->setPackActive(S("Gamma.zip"), true));
        rig.page->requestUnhide(rig.rowOf(S("Hidden")));
        rig.answers = { 1 };
        rig.page->requestRemove(rig.rowOf(S("Gamma")));
        QVERIFY(!rig.page->undo());
        QVERIFY(rig.prompts.isEmpty());  // Remove did not even ask
        QVERIFY(rig.trashCalls.isEmpty());
        QCOMPARE(rig.optionsBytes(), before);
        QVERIFY(QFile::exists(rig.packs.filePath(S("Hidden.zip.disabled"))));
        // a drag does not start on a locked list
        const QRect alpha = rig.page->listView()->visualRect(rig.page->listModel()->index(rig.rowOf(S("Alpha"))));
        const QPoint grip = PackDelegate::gripRect(PackDelegate::cardRect(alpha)).center();
        QTest::mousePress(rig.page->listView()->viewport(), Qt::LeftButton, Qt::NoModifier, grip);
        QTest::mouseMove(rig.page->listView()->viewport(), grip + QPoint(0, 40));
        QTest::mouseMove(rig.page->listView()->viewport(), grip + QPoint(0, 80));
        QVERIFY(!rig.page->listView()->isDragging());
        QTest::mouseRelease(rig.page->listView()->viewport(), Qt::LeftButton, Qt::NoModifier, grip + QPoint(0, 80));
        QCOMPARE(rig.optionsBytes(), before);
        // the game stops: the banner goes and the page works again
        rig.running = false;
        rig.page->refreshRunning();
        QVERIFY(!rig.page->isLocked());
        QVERIFY(!rig.page->banner()->isVisibleTo(rig.page.get()));
        QVERIFY(rig.page->setPackActive(S("Gamma.zip"), true));
    }

    void aGameThatStartsBehindThePagesBackIsStillRefused()
    {
        Rig rig;
        rig.addSet();
        const QByteArray before = options({ S("vanilla") });
        QVERIFY(rig.writeOptions(before));
        rig.build();
        QVERIFY(rig.open(5));
        // the page asks the environment at the start of the action; the write asks again right before it: the second answer is "running"
        int asked = 0;
        auto env = rig.environment();
        env.isRunning = [&asked]() { return ++asked > 1; };
        ResourcePacksPage page(env, rig.model.get());
        page.resize(800, 600);
        page.opened();
        page.show();
        QTest::qWait(200);
        page.rebuildNow();
        asked = 0;
        QVERIFY(!page.setPackActive(S("Alpha.zip"), true));
        QCOMPARE(rig.optionsBytes(), before);
        QVERIFY(page.isLocked());  // it noticed
        page.closed();
    }

    // =============================== external changes ========================================================================

    void optionsAreReadAgainWhenThePageIsShown()
    {
        Rig rig;
        rig.addSet();
        QVERIFY(rig.writeOptions(options({ S("vanilla"), S("file/Alpha.zip") })));
        rig.build();
        QVERIFY(rig.open(5));
        QCOMPARE(rig.page->activePacks(), QStringList{ S("Alpha.zip") });
        // the game (or the user) rewrote options.txt while the page was away
        QVERIFY(rig.writeOptions(options({ S("vanilla"), S("file/Beta.zip"), S("file/Gamma.zip") })));
        rig.page->hide();
        rig.page->show();
        QCOMPARE(rig.page->activePacks(), (QStringList{ S("Gamma.zip"), S("Beta.zip") }));
        QCOMPARE(rig.lines().mid(0, 4), (QStringList{ kOn, S("Gamma"), S("Beta"), S("#Default resources") }));
    }

    void optionsAreReadAgainWhenTheFolderReloads()
    {
        Rig rig;
        rig.addSet();
        QVERIFY(rig.writeOptions(options({ S("vanilla") })));
        rig.build();
        QVERIFY(rig.open(5));
        QVERIFY(rig.writeOptions(options({ S("vanilla"), S("file/Gamma.zip") })));
        QVERIFY(rig.settle());
        QCOMPARE(rig.page->activePacks(), QStringList{ S("Gamma.zip") });
    }

    void optionsAreReadAgainWhenTheGameStops()
    {
        Rig rig;
        rig.addSet();
        QVERIFY(rig.writeOptions(options({ S("vanilla"), S("file/Alpha.zip") })));
        rig.build();
        QVERIFY(rig.open(5));
        rig.running = true;
        rig.page->refreshRunning();
        QVERIFY(rig.writeOptions(options({ S("vanilla"), S("file/Beta.zip") })));  // the game saves its settings on exit
        QCOMPARE(rig.page->activePacks(), QStringList{ S("Alpha.zip") });         // not looked at while it runs
        rig.running = false;
        rig.page->refreshRunning();
        QCOMPARE(rig.page->activePacks(), QStringList{ S("Beta.zip") });
        QVERIFY(!rig.page->isLocked());
    }

    // =============================== Remove ==================================================================================

    void removeGoesToTheRecycleBinAndDropsTheEntry()
    {
        Rig rig;
        rig.addSet();
        QVERIFY(rig.writeOptions(options({ S("vanilla"), S("file/Alpha.zip"), S("fabric"), S("file/Beta.zip") })));
        rig.build();
        QVERIFY(rig.open(5));
        rig.answers = { 1 };
        rig.page->requestRemove(rig.rowOf(S("Alpha")));
        QCOMPARE(rig.prompts.size(), 1);
        QCOMPARE(rig.prompts.first().title, S("Remove Alpha?"));
        QCOMPARE(rig.prompts.first().note, S("It goes to the Recycle Bin."));
        QCOMPARE(rig.trashCalls, QStringList{ S("Alpha.zip") });
        QVERIFY(!QFile::exists(rig.packs.filePath(S("Alpha.zip"))));
        QCOMPARE(rig.optionsBytes(), options({ S("vanilla"), S("fabric"), S("file/Beta.zip") }));
        QVERIFY(toastTexts(rig.page.get()).contains(S("Removed Alpha")));
        QCOMPARE(readHistory(rig.historyPath()).size(), 1);  // the same backup as every other write
        QVERIFY(rig.settle());
        QVERIFY(rig.rowOf(S("Alpha")) < 0);
        QCOMPARE(rig.page->activePacks(), QStringList{ S("Beta.zip") });
    }

    void removingAnInactivePackLeavesOptionsAlone()
    {
        Rig rig;
        rig.addSet();
        const QByteArray before = options({ S("vanilla"), S("file/Alpha.zip") });
        QVERIFY(rig.writeOptions(before));
        rig.build();
        QVERIFY(rig.open(5));
        rig.answers = { 1 };
        rig.page->requestRemove(rig.rowOf(S("Gamma")));
        QCOMPARE(rig.trashCalls, QStringList{ S("Gamma.zip") });
        QCOMPARE(rig.optionsBytes(), before);
        QVERIFY(!QFile::exists(rig.historyPath()));
        // a hidden file and a folder pack go the same way
        rig.answers = { 1, 1 };
        rig.page->requestRemove(rig.rowOf(S("Hidden")));
        rig.page->requestRemove(rig.rowOf(S("Folder Pack")));
        QCOMPARE(rig.trashCalls, (QStringList{ S("Gamma.zip"), S("Hidden.zip.disabled"), S("Folder Pack") }));
        QVERIFY(!QFile::exists(rig.packs.filePath(S("Folder Pack"))));
        QCOMPARE(rig.optionsBytes(), before);
    }

    void aFailedTrashKeepsTheFileAndTheEntry()
    {
        Rig rig;
        rig.addSet();
        const QByteArray before = options({ S("vanilla"), S("file/Alpha.zip") });
        QVERIFY(rig.writeOptions(before));
        rig.failTrash.insert(S("Alpha.zip"));
        rig.build();
        QVERIFY(rig.open(5));
        rig.answers = { 1 };
        rig.page->requestRemove(rig.rowOf(S("Alpha")));
        QVERIFY(QFile::exists(rig.packs.filePath(S("Alpha.zip"))));  // never deleted for good
        QCOMPARE(rig.optionsBytes(), before);
        QVERIFY(toastTexts(rig.page.get()).contains(S("Couldn't move Alpha to the Recycle Bin")));
    }

    void cancelRemovesNothing()
    {
        Rig rig;
        rig.addSet();
        rig.build();
        QVERIFY(rig.open(5));
        rig.answers = { 0 };  // Cancel
        rig.page->requestRemove(rig.rowOf(S("Alpha")));
        rig.answers.clear();  // dismissed
        rig.page->requestRemove(rig.rowOf(S("Beta")));
        QCOMPARE(rig.prompts.size(), 2);
        QVERIFY(rig.trashCalls.isEmpty());
        QVERIFY(QFile::exists(rig.packs.filePath(S("Alpha.zip"))));
        QCOMPARE(rig.prompts.first().defaultButton, 0);  // Enter cancels
    }

    void aGameThatStartsDuringTheQuestionStopsTheRemove()
    {
        Rig rig;
        rig.addSet();
        rig.build();
        QVERIFY(rig.open(5));
        // the "dialog" is answered, but the game started while it was open
        auto env = rig.environment();
        env.ask = [&rig](QWidget*, const ChoicePrompt&) {
            rig.running = true;
            return 1;
        };
        ResourcePacksPage page(env, rig.model.get());
        page.resize(800, 600);
        page.opened();
        page.show();
        QTest::qWait(200);
        page.rebuildNow();
        int alpha = -1;
        for (int r = 0; r < page.listModel()->rowCount(); ++r) {
            if (page.listModel()->isPack(r) && page.listModel()->entryAt(r).row.name == S("Alpha")) {
                alpha = r;
            }
        }
        QVERIFY(alpha >= 0);
        page.requestRemove(alpha);
        QVERIFY(rig.trashCalls.isEmpty());
        QVERIFY(QFile::exists(rig.packs.filePath(S("Alpha.zip"))));
        QVERIFY(page.isLocked());
        page.closed();
    }

    // =============================== hidden files ============================================================================

    void unhideRenamesTheFile()
    {
        Rig rig;
        rig.addSet();
        QVERIFY(rig.writeOptions(options({ S("vanilla") })));
        rig.build();
        QVERIFY(rig.open(5));
        // a hidden file has no switch: clicking it does nothing
        rig.page->requestToggle(rig.rowOf(S("Hidden")));
        QCOMPARE(rig.optionsBytes(), options({ S("vanilla") }));
        rig.page->requestUnhide(rig.rowOf(S("Hidden")));
        QVERIFY(QFile::exists(rig.packs.filePath(S("Hidden.zip"))));
        QVERIFY(!QFile::exists(rig.packs.filePath(S("Hidden.zip.disabled"))));
        QVERIFY(toastTexts(rig.page.get()).contains(S("Hidden is visible to the game again")));
        QVERIFY(rig.settle());
        QVERIFY(!rig.rowNamed(S("Hidden")).hidden);
        QVERIFY(rig.page->setPackActive(S("Hidden.zip"), true));  // and now it can be turned on
        QCOMPARE(rig.optionsBytes(), options({ S("vanilla"), S("file/Hidden.zip") }));
    }

    void aHiddenFileCannotBeTurnedOnByName()
    {
        Rig rig;
        rig.addSet();
        const QByteArray before = options({ S("vanilla") });
        QVERIFY(rig.writeOptions(before));
        rig.build();
        QVERIFY(rig.open(5));
        QVERIFY(!rig.page->setPackActive(S("Hidden.zip.disabled"), true));
        QVERIFY(!rig.page->setPackActive(S("Nope.zip"), true));
        QCOMPARE(rig.optionsBytes(), before);
    }

    // =============================== old versions and unreadable options =====================================================

    void minecraft112ListsWithoutSwitches()
    {
        Rig rig;
        rig.mc = S("1.12.2");
        rig.addSet();
        const QByteArray old = B("resourcePacks:[Alpha.zip]\n");
        QVERIFY(rig.writeOptions(old));
        rig.build();
        QVERIFY(rig.open(5));
        QCOMPARE(rig.page->orderState(), ResourcePacksPage::OrderState::OldVersion);
        QVERIFY(!rig.page->isOrdered());
        QCOMPARE(rig.page->noteText(), S("Turn packs on in the game for Minecraft 1.12 and older."));
        QCOMPARE(rig.lines(), (QStringList{ S("Alpha"), S("Beta"), S("Folder Pack"), S("Gamma"), S("Hidden") }));  // flat, by name, no groups
        QVERIFY(!rig.page->listView()->rowDelegate()->ordered());
        QVERIFY(!rig.page->filterControl()->isVisibleTo(rig.page.get()));
        QCOMPARE(rig.page->captionLabel()->text(), S("5 packs"));
        rig.page->requestToggle(rig.rowOf(S("Beta")));
        rig.page->requestMove(rig.rowOf(S("Beta")), ResourcePacksPage::Move::Top);
        QVERIFY(!rig.page->movePack(0, 1));
        QVERIFY(!rig.page->setPackActive(S("Beta.zip"), true));
        QCOMPARE(rig.optionsBytes(), old);
        QVERIFY(!QFile::exists(rig.historyPath()));
        // Remove still works (it is just a file)
        rig.answers = { 1 };
        rig.page->requestRemove(rig.rowOf(S("Gamma")));
        QCOMPARE(rig.trashCalls, QStringList{ S("Gamma.zip") });
        QCOMPARE(rig.optionsBytes(), old);
    }

    void anUnreadableListIsNeverRewritten()
    {
        Rig rig;
        rig.addSet();
        const QByteArray damaged = B("fov:1\nresourcePacks:[\"vanilla\",\nlang:en_us\n");
        QVERIFY(rig.writeOptions(damaged));
        rig.build();
        QVERIFY(rig.open(5));
        QCOMPARE(rig.page->orderState(), ResourcePacksPage::OrderState::Unreadable);
        QVERIFY(rig.page->noteText().contains(S("options.txt")));
        QVERIFY(!rig.page->setPackActive(S("Alpha.zip"), true));
        rig.page->requestToggle(rig.rowOf(S("Alpha")));
        QCOMPARE(rig.optionsBytes(), damaged);
        QVERIFY(!QFile::exists(rig.historyPath()));
        QVERIFY(!rig.lines().contains(S("#Default resources")));
    }

    void aListThatTurnsUnreadableDuringAnActionIsLeftAlone()
    {
        Rig rig;
        rig.addSet();
        QVERIFY(rig.writeOptions(options({ S("vanilla") })));
        rig.build();
        QVERIFY(rig.open(5));
        // after the page read it, something rewrote the line in the old format
        const QByteArray old = B("resourcePacks:[Old.zip]\n");
        QVERIFY(rig.writeOptions(old));
        QVERIFY(!rig.page->setPackActive(S("Alpha.zip"), true));
        QCOMPARE(rig.optionsBytes(), old);
        QVERIFY(toastTexts(rig.page.get()).join(QLatin1Char('|')).contains(S("can't read")));
        QCOMPARE(rig.page->orderState(), ResourcePacksPage::OrderState::Unreadable);  // and the page noticed
    }

    // =============================== search, filter, states ==================================================================

    void searchAndFilter()
    {
        Rig rig;
        rig.addSet();
        QVERIFY(rig.writeOptions(options({ S("vanilla"), S("file/Alpha.zip"), S("file/Beta.zip") })));
        rig.build();
        QVERIFY(rig.open(5));
        rig.page->setFilter(Filter::On);
        QCOMPARE(rig.lines(), (QStringList{ kOn, S("Beta"), S("Alpha"), S("#Default resources") }));
        QCOMPARE(rig.page->captionLabel()->text(), S("2 of 5 packs"));
        rig.page->setFilter(Filter::Off);
        QCOMPARE(rig.lines(), (QStringList{ S("#Available"), S("Folder Pack"), S("Gamma"), S("Hidden") }));
        rig.page->setFilter(Filter::All);
        rig.page->setSearchText(S("gam"));
        QCOMPARE(rig.lines(), (QStringList{ S("#Available"), S("Gamma") }));
        rig.page->setSearchText(S("zzz"));
        QCOMPARE(rig.page->view(), ResourcePacksPage::View::NoMatches);
        QStringList noMatchTexts;
        for (const QLabel* label : rig.page->emptyState()->findChildren<QLabel*>()) {
            if (!label->text().isEmpty()) {
                noMatchTexts.append(label->text());
            }
        }
        QVERIFY(!noMatchTexts.isEmpty());
        rig.page->setSearchText(QString());
        QCOMPARE(rig.page->view(), ResourcePacksPage::View::List);
        QCOMPARE(rig.page->filterControl()->selected(), S("all"));
    }

    void emptyState()
    {
        Rig rig;
        rig.build();
        QVERIFY(rig.open(0));
        QCOMPARE(rig.page->view(), ResourcePacksPage::View::Empty);
        QVERIFY(rig.page->emptyState()->isVisibleTo(rig.page.get()));
        QVERIFY(!rig.page->addButton()->isVisibleTo(rig.page.get()));  // the empty state has the action
        QStringList texts;
        for (const QLabel* label : rig.page->emptyState()->findChildren<QLabel*>()) {
            texts.append(label->text());
        }
        QVERIFY(texts.contains(S("No resource packs yet")));
        QVERIFY(texts.contains(S("Find packs for Test Instance in Discover.")));
        auto* action = rig.page->emptyState()->findChild<QPushButton*>();
        QVERIFY(action);
        QCOMPARE(action->text(), S("Add packs"));
        QSignalSpy requested(rig.page.get(), &ResourcePacksPage::addPacksRequested);
        action->click();
        QCOMPARE(rig.addCalls, 1);
        QCOMPARE(requested.count(), 1);
        // without a Discover to go to there is no button
        Rig bare;
        bare.withAdd = false;
        bare.build();
        QVERIFY(bare.open(0));
        QVERIFY(bare.page->emptyState()->findChild<QPushButton*>()->isHidden());
    }

    void addButtonAndToolbar()
    {
        Rig rig;
        rig.addSet();
        rig.build();
        QVERIFY(rig.open(5));
        QVERIFY(rig.page->addButton()->isVisibleTo(rig.page.get()));
        QCOMPARE(rig.page->addButton()->text(), S("Add packs"));
        rig.page->addButton()->click();
        QCOMPARE(rig.addCalls, 1);
        QCOMPARE(rig.page->searchField()->placeholderText(), S("Search packs"));
        QCOMPARE(rig.page->filterControl()->count(), 3);
        QCOMPARE(rig.page->filterControl()->valueAt(0), S("all"));
        QCOMPARE(rig.page->filterControl()->valueAt(1), S("on"));
        QCOMPARE(rig.page->filterControl()->valueAt(2), S("off"));
        // 1100 x 700: the toolbar fits
        rig.page->resize(860, 560);
        QVERIFY(rig.page->minimumSizeHint().width() <= 860);
    }

    void loadingShowsTheSkeleton()
    {
        Rig rig;
        rig.addSet();
        rig.build();
        QCOMPARE(rig.page->view(), ResourcePacksPage::View::Loading);
        QVERIFY(!rig.page->isLoaded());
    }

    // =============================== the row menu ============================================================================

    void rowMenuEntries()
    {
        Rig rig;
        rig.addSet();
        QVERIFY(rig.writeOptions(options({ S("vanilla"), S("file/Alpha.zip"), S("file/Beta.zip"), S("file/Gamma.zip") })));
        rig.build();
        QVERIFY(rig.open(5));
        const auto entries = [&](const QString& name) {
            QMenu menu;
            rig.page->fillRowMenu(&menu, rig.rowOf(name));
            QList<QPair<QString, bool>> out;
            for (const QAction* action : menu.actions()) {
                if (!action->isSeparator()) {
                    out.append({ action->text(), action->isEnabled() });
                }
            }
            return out;
        };
        // the pack on top: it cannot move up; the middle one can move everywhere; the last one cannot move down
        auto top = entries(S("Gamma"));
        QCOMPARE(top.size(), 7);
        QCOMPARE(top.at(0).first, S("Turn off"));
        QCOMPARE(top.at(1), qMakePair(S("Move to top"), false));
        QCOMPARE(top.at(2), qMakePair(S("Move up"), false));
        QCOMPARE(top.at(3), qMakePair(S("Move down"), true));
        QCOMPARE(top.at(4), qMakePair(S("Move to bottom"), true));
        QCOMPARE(top.at(5).first, S("Show in folder"));
        QCOMPARE(top.at(6).first, S("Remove"));
        QCOMPARE(entries(S("Beta")).at(1), qMakePair(S("Move to top"), true));
        QCOMPARE(entries(S("Alpha")).at(3), qMakePair(S("Move down"), false));
        QCOMPARE(entries(S("Alpha")).at(4), qMakePair(S("Move to bottom"), false));
        // an available pack: only Turn on, Show in folder, Remove
        QStringList available;
        for (const auto& entry : entries(S("Folder Pack"))) {
            available.append(entry.first);
        }
        QCOMPARE(available, (QStringList{ S("Turn on"), S("Show in folder"), S("Remove") }));
        // a hidden file: Unhide first, no switch entry
        QStringList hidden;
        for (const auto& entry : entries(S("Hidden"))) {
            hidden.append(entry.first);
        }
        QCOMPARE(hidden, (QStringList{ S("Unhide"), S("Show in folder"), S("Remove") }));
        // the row is selected by the menu
        QMenu menu;
        rig.page->fillRowMenu(&menu, rig.rowOf(S("Beta")));
        QCOMPARE(rig.page->selectedRow(), rig.rowOf(S("Beta")));
        // while the game runs the entries that change something are disabled
        rig.running = true;
        rig.page->refreshRunning();
        for (const auto& entry : entries(S("Beta"))) {
            if (entry.first != S("Show in folder")) {
                QVERIFY2(!entry.second, qPrintable(entry.first));
            }
        }
        // the actions work through the menu
        rig.running = false;
        rig.page->refreshRunning();
        QMenu live;
        rig.page->fillRowMenu(&live, rig.rowOf(S("Beta")));
        for (QAction* action : live.actions()) {
            if (action->text() == S("Move to top")) {
                action->trigger();
            }
        }
        QCOMPARE(rig.page->activePacks().first(), S("Beta.zip"));
    }

    // =============================== the shared parts ========================================================================

    void sharedPartsOfTheInstancePages()
    {
        // the Recycle Bin step: a file that is gone counts as removed, the injected function decides otherwise, nothing deletes by itself
        QTemporaryDir dir;
        const QString file = dir.filePath(S("x.zip"));
        QVERIFY(writeFile(file, B("PK")));
        int calls = 0;
        QVERIFY(!ModsUi::moveToRecycleBin(file, [&calls](const QString&) {
            ++calls;
            return false;
        }));
        QVERIFY(QFile::exists(file));
        QVERIFY(ModsUi::moveToRecycleBin(file, [&calls](const QString& path) {
            ++calls;
            return QFile::remove(path);
        }));
        QVERIFY(ModsUi::moveToRecycleBin(file, [&calls](const QString&) {
            ++calls;
            return false;
        }));
        QCOMPARE(calls, 2);  // the third call never asked: the file was gone
        // the words of Remove for packs; the mods' words are unchanged
        using namespace Zero::Mods::Installed;
        QCOMPARE(removeConfirm({ S("a"), S("b") }, Noun::Packs).title, S("Remove 2 packs?"));
        QCOMPARE(removeConfirm({ S("a"), S("b") }).title, S("Remove 2 mods?"));
        QCOMPARE(removeConfirm({ S("a") }, Noun::Packs).title, S("Remove a?"));
        QCOMPARE(removeResult({ S("a"), S("b") }, {}, Noun::Packs).text, S("Removed 2 packs"));
        QCOMPARE(removeResult({ S("a"), S("b") }, {}).text, S("Removed 2 mods"));
        QCOMPARE(removeResult({}, { S("a"), S("b") }, Noun::Packs).text, S("Couldn't move 2 packs to the Recycle Bin"));
        QCOMPARE(removeResult({ S("a") }, { S("b") }, Noun::Packs).outcome, Zero::Mods::Installed::Outcome::Warning);
        ModsUi::RunningBanner banner(S("Close the game to change packs"), nullptr);
        QCOMPARE(banner.accessibleName(), S("Close the game to change packs"));
        QCOMPARE(banner.height(), 36);
    }
};

QTEST_MAIN(ZeroResourcePacksPageTest)

#include "ZeroResourcePacksPage_test.moc"
