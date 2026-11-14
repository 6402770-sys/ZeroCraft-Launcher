// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the pure resource pack code (docs/zero/CONTENT-PACKS.md, "D2 Installed resource packs"): the options.txt editor
// (zero/content/PackOrder: parse, merge, byte-exact edits with LF / CRLF / no final newline / BOM, history, Undo, the running refusal,
// folder packs, stale entries) and the page's pure core (zero/content/InstalledPacks: description to plain text, pack format fit, search,
// filter, the groups of the list, captions, pills, texts). Real temp files, no widgets, no network.
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "zero/content/InstalledPacks.h"
#include "zero/content/PackOrder.h"

using namespace Zero::Content::Packs;

namespace {

QString S(const char* text)
{
    return QString::fromUtf8(text);
}

QByteArray B(const char* text)
{
    return QByteArray(text);
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

QSet<QString> setOf(const QStringList& names)
{
    return QSet<QString>(names.begin(), names.end());
}

/// A temp "instance": <root>/.minecraft/options.txt, <root>/.zero/pack-order.json and a resourcepacks folder with a few packs.
struct Rig {
    QTemporaryDir root;
    QString options() const { return optionsPathFor(root.filePath(S(".minecraft"))); }
    QString history() const { return historyPathFor(root.path()); }
    QString packs() const { return root.filePath(S(".minecraft/resourcepacks")); }
    bool running = false;
    std::function<bool()> isRunning() { return [this]() { return running; }; }
    void addPack(const QString& name)
    {
        QDir().mkpath(packs());
        if (name.endsWith(S(".zip"))) {
            QVERIFY(writeFile(packs() + QLatin1Char('/') + name, B("PK")));
        } else {
            QVERIFY(QDir().mkpath(packs() + QLatin1Char('/') + name));
        }
    }
    QSet<QString> existing() const { return packFilesIn(packs()); }
};

PackRow pack(const QString& name, int activeIndex = -1, bool hidden = false, bool folder = false)
{
    PackRow row;
    row.name = name;
    row.fileName = folder ? name : name + (hidden ? S(".zip.disabled") : S(".zip"));
    row.id = row.fileName;
    row.folder = folder;
    row.hidden = hidden;
    row.active = activeIndex >= 0;
    row.activeIndex = activeIndex;
    return row;
}

FormatMap formats()
{
    FormatMap map;
    map.insert({ 15, 0 }, { Version(S("1.20")), Version(S("1.20.1")) });
    map.insert({ 34, 0 }, { Version(S("1.21")), Version(S("1.21.1")) });
    map.insert({ 35, 0 }, { Version(S("24w33a")), Version(S("24w33a")) });
    map.insert({ 46, 0 }, { Version(S("1.21.4")), Version(S("1.21.4")) });
    return map;
}

}  // namespace

class PackOrderTest : public QObject {
    Q_OBJECT

   private slots:
    // =============================== entries =================================================================================

    void entryNames()
    {
        QVERIFY(isFileEntry(S("file/Faithful 32x.zip")));
        QVERIFY(!isFileEntry(S("vanilla")));
        QVERIFY(!isFileEntry(S("fabric")));
        QCOMPARE(fileNameOf(S("file/Faithful 32x.zip")), S("Faithful 32x.zip"));
        QCOMPARE(fileNameOf(S("fabric")), S("fabric"));
        QCOMPARE(fileEntryFor(S("A.zip")), S("file/A.zip"));
        QCOMPARE(fileEntryFor(S("Folder Pack")), S("file/Folder Pack"));
    }

    void parseStates()
    {
        QCOMPARE(parseOptions({}, false).state, PackList::State::Missing);
        QCOMPARE(parseOptions(B("fov:0.5\n"), true).state, PackList::State::NoKey);
        QCOMPARE(parseOptions({}, true).state, PackList::State::NoKey);  // an empty file
        const PackList ok = parseOptions(B("a:1\nresourcePacks:[\"vanilla\",\"file/A.zip\"]\nincompatibleResourcePacks:[\"file/Old.zip\"]\n"), true);
        QCOMPARE(ok.state, PackList::State::Ok);
        QCOMPARE(ok.entries, (QStringList{ S("vanilla"), S("file/A.zip") }));
        QCOMPARE(ok.incompatible, QStringList{ S("file/Old.zip") });
        // the old format, a damaged line, an empty value and an array of something else are never rewritten
        QCOMPARE(parseOptions(B("resourcePacks:[Foo.zip]\n"), true).state, PackList::State::Unreadable);
        QCOMPARE(parseOptions(B("resourcePacks:[\"a\"\n"), true).state, PackList::State::Unreadable);
        QCOMPARE(parseOptions(B("resourcePacks:\n"), true).state, PackList::State::Unreadable);
        QCOMPARE(parseOptions(B("resourcePacks:[1,2]\n"), true).state, PackList::State::Unreadable);
        QCOMPARE(parseOptions(B("resourcePacks:{}\n"), true).state, PackList::State::Unreadable);
        // an empty array is fine, and the last line of a key wins (as in the game)
        const PackList empty = parseOptions(B("resourcePacks:[]\n"), true);
        QCOMPARE(empty.state, PackList::State::Ok);
        QVERIFY(empty.entries.isEmpty());
        QCOMPARE(parseOptions(B("resourcePacks:[\"a\"]\nresourcePacks:[\"b\"]\n"), true).entries, QStringList{ S("b") });
        // CRLF, a BOM before the first key and a key that only starts like another
        QCOMPARE(parseOptions(B("a:1\r\nresourcePacks:[\"x\"]\r\n"), true).entries, QStringList{ S("x") });
        QCOMPARE(parseOptions(B("\xEF\xBB\xBFresourcePacks:[\"x\"]\n"), true).entries, QStringList{ S("x") });
        QCOMPARE(parseOptions(B("incompatibleResourcePacks:[\"x\"]\n"), true).state, PackList::State::NoKey);
        QCOMPARE(parseOptions(B("xresourcePacks:[\"x\"]\n"), true).state, PackList::State::NoKey);
    }

    void activeFilesTopFirst()
    {
        const QStringList entries = { S("vanilla"), S("file/A.zip"), S("fabric"), S("file/B.zip"), S("file/Folder") };
        QCOMPARE(activeFiles(entries), (QStringList{ S("Folder"), S("B.zip"), S("A.zip") }));
        QCOMPARE(activeFiles({ S("vanilla") }), QStringList());
        QCOMPARE(activeFiles({ S("file/A.zip"), S("file/A.zip") }), QStringList{ S("A.zip") });  // a duplicate counts once
    }

    // =============================== merging =================================================================================

    void mergeTurnOnAppendsAtTheTop()
    {
        const QSet<QString> ex = setOf({ S("A.zip"), S("B.zip"), S("C.zip") });
        QCOMPARE(mergeOrder({ S("vanilla"), S("file/A.zip") }, { S("B.zip"), S("A.zip") }, ex), (QStringList{ S("vanilla"), S("file/A.zip"), S("file/B.zip") }));
        QCOMPARE(mergeOrder({ S("vanilla") }, { S("C.zip") }, ex), (QStringList{ S("vanilla"), S("file/C.zip") }));
    }

    void mergeTurnOff()
    {
        const QSet<QString> ex = setOf({ S("A.zip"), S("B.zip") });
        QCOMPARE(mergeOrder({ S("vanilla"), S("file/A.zip"), S("file/B.zip") }, { S("B.zip") }, ex), (QStringList{ S("vanilla"), S("file/B.zip") }));
        QCOMPARE(mergeOrder({ S("vanilla"), S("file/A.zip") }, {}, ex), QStringList{ S("vanilla") });
    }

    void mergeReorderKeepsTheOtherSlots()
    {
        const QSet<QString> ex = setOf({ S("A.zip"), S("B.zip"), S("C.zip") });
        // vanilla and fabric keep their indexes; the file packs swap inside their own slots
        const QStringList entries = { S("vanilla"), S("file/A.zip"), S("fabric"), S("file/B.zip"), S("file/C.zip"), S("mod_resources") };
        QCOMPARE(mergeOrder(entries, { S("A.zip"), S("C.zip"), S("B.zip") }, ex),  // top first: A wins now
                 (QStringList{ S("vanilla"), S("file/B.zip"), S("fabric"), S("file/C.zip"), S("file/A.zip"), S("mod_resources") }));
        // nothing changes: the same list comes back
        QCOMPARE(mergeOrder(entries, activeFiles(entries), ex), entries);
    }

    void mergeKeepsStaleEntriesWhereTheyAre()
    {
        // "Gone.zip" is not in the folder any more: its entry stays, the packs around it move
        const QSet<QString> ex = setOf({ S("A.zip"), S("B.zip") });
        const QStringList entries = { S("vanilla"), S("file/A.zip"), S("file/Gone.zip"), S("file/B.zip") };
        QCOMPARE(mergeOrder(entries, { S("A.zip"), S("B.zip") }, ex), (QStringList{ S("vanilla"), S("file/B.zip"), S("file/Gone.zip"), S("file/A.zip") }));
        QCOMPARE(mergeOrder(entries, {}, ex), (QStringList{ S("vanilla"), S("file/Gone.zip") }));
        // a name the folder does not have cannot be turned on
        QCOMPARE(mergeOrder({ S("vanilla") }, { S("Nope.zip") }, ex), QStringList{ S("vanilla") });
    }

    void mergeFolderPacksAndDuplicates()
    {
        const QSet<QString> ex = setOf({ S("Folder Pack"), S("A.zip") });
        QCOMPARE(mergeOrder({ S("vanilla"), S("file/A.zip") }, { S("Folder Pack"), S("A.zip") }, ex),
                 (QStringList{ S("vanilla"), S("file/A.zip"), S("file/Folder Pack") }));
        QCOMPARE(mergeOrder({ S("file/A.zip"), S("vanilla"), S("file/A.zip") }, { S("A.zip") }, ex), (QStringList{ S("file/A.zip"), S("vanilla") }));  // the duplicate goes
        // the same name asked twice counts once
        QCOMPARE(mergeOrder({ S("vanilla") }, { S("A.zip"), S("A.zip") }, ex), (QStringList{ S("vanilla"), S("file/A.zip") }));
    }

    // =============================== the bytes of options.txt ================================================================

    void setLineReplacesOnlyThatLine()
    {
        const QByteArray lf = B("fov:0.5\nresourcePacks:[\"vanilla\"]\nlang:en_us\n");
        QCOMPARE(setLine(lf, B("resourcePacks"), B("resourcePacks:[\"vanilla\",\"file/A.zip\"]")),
                 B("fov:0.5\nresourcePacks:[\"vanilla\",\"file/A.zip\"]\nlang:en_us\n"));
        const QByteArray crlf = B("fov:0.5\r\nresourcePacks:[\"vanilla\"]\r\nlang:en_us\r\n");
        QCOMPARE(setLine(crlf, B("resourcePacks"), B("resourcePacks:[]")), B("fov:0.5\r\nresourcePacks:[]\r\nlang:en_us\r\n"));
        const QByteArray noFinal = B("fov:0.5\nresourcePacks:[\"vanilla\"]");
        QCOMPARE(setLine(noFinal, B("resourcePacks"), B("resourcePacks:[]")), B("fov:0.5\nresourcePacks:[]"));
        const QByteArray bom = B("\xEF\xBB\xBFresourcePacks:[\"vanilla\"]\nfov:1\n");
        QCOMPARE(setLine(bom, B("resourcePacks"), B("resourcePacks:[]")), B("\xEF\xBB\xBFresourcePacks:[]\nfov:1\n"));
        // lines that are not understood (blank, no colon, odd bytes) stay as they are
        const QByteArray odd = B("\n# comment\nresourcePacks:[\"a\"]\n\xff\xfe:\x01\n\n");
        QCOMPARE(setLine(odd, B("resourcePacks"), B("resourcePacks:[\"b\"]")), B("\n# comment\nresourcePacks:[\"b\"]\n\xff\xfe:\x01\n\n"));
    }

    void setLineAppendsInTheStyleOfTheFile()
    {
        QCOMPARE(setLine(B("a:1\n"), B("resourcePacks"), B("resourcePacks:[]")), B("a:1\nresourcePacks:[]\n"));
        QCOMPARE(setLine(B("a:1\r\nb:2\r\n"), B("resourcePacks"), B("resourcePacks:[]")), B("a:1\r\nb:2\r\nresourcePacks:[]\r\n"));
        QCOMPARE(setLine(B("a:1\nb:2"), B("resourcePacks"), B("resourcePacks:[]")), B("a:1\nb:2\nresourcePacks:[]"));  // still no final newline
        QCOMPARE(setLine(B("a:1\r\nb:2"), B("resourcePacks"), B("resourcePacks:[]")), B("a:1\r\nb:2\r\nresourcePacks:[]"));
        QCOMPARE(setLine({}, B("resourcePacks"), B("resourcePacks:[]")), B("resourcePacks:[]\n"));
        QCOMPARE(setLine(B("a:1"), B("resourcePacks"), std::nullopt), B("a:1"));  // nothing to remove
    }

    void setLineRemoves()
    {
        QCOMPARE(setLine(B("a:1\nresourcePacks:[]\nb:2\n"), B("resourcePacks"), std::nullopt), B("a:1\nb:2\n"));
        QCOMPARE(setLine(B("a:1\r\nresourcePacks:[]\r\nb:2\r\n"), B("resourcePacks"), std::nullopt), B("a:1\r\nb:2\r\n"));
        QCOMPARE(setLine(B("a:1\nresourcePacks:[]"), B("resourcePacks"), std::nullopt), B("a:1"));  // the last line had no newline: neither has the file now
        QCOMPARE(setLine(B("resourcePacks:[]\n"), B("resourcePacks"), std::nullopt), QByteArray());
        QCOMPARE(setLine(B("\xEF\xBB\xBFresourcePacks:[]\na:1\n"), B("resourcePacks"), std::nullopt), B("\xEF\xBB\xBF" "a:1\n"));
    }

    void lineOfAndEncode()
    {
        QCOMPARE(lineOf(B("a:1\nresourcePacks:[\"x\"]\r\n"), B("resourcePacks")), std::optional<QByteArray>(B("resourcePacks:[\"x\"]")));
        QVERIFY(!lineOf(B("a:1\n"), B("resourcePacks")));
        QCOMPARE(encodeEntries({ S("vanilla"), S("file/A B.zip") }), B("[\"vanilla\",\"file/A B.zip\"]"));
        QCOMPARE(encodeEntries({}), B("[]"));
    }

    // =============================== writing =================================================================================

    void writeKeepsEveryOtherByte_data()
    {
        QTest::addColumn<QByteArray>("before");
        QTest::addColumn<QByteArray>("after");
        const QByteArray body = B("version:3465\nautoJump:false\nlang:en_us\nresourcePacks:[\"vanilla\",\"file/A.zip\"]\nincompatibleResourcePacks:[]\nfov:0.0\n");
        const QByteArray bodyAfter =
            B("version:3465\nautoJump:false\nlang:en_us\nresourcePacks:[\"vanilla\",\"file/A.zip\",\"file/B.zip\"]\nincompatibleResourcePacks:[]\nfov:0.0\n");
        QTest::newRow("LF") << body << bodyAfter;
        QByteArray crlf = body;
        crlf.replace("\n", "\r\n");
        QByteArray crlfAfter = bodyAfter;
        crlfAfter.replace("\n", "\r\n");
        QTest::newRow("CRLF") << crlf << crlfAfter;
        QTest::newRow("no final newline") << body.left(body.size() - 1) << bodyAfter.left(bodyAfter.size() - 1);
        QTest::newRow("BOM") << (B("\xEF\xBB\xBF") + body) << (B("\xEF\xBB\xBF") + bodyAfter);
        QTest::newRow("odd lines") << B("\n\xff:1\nresourcePacks:[\"vanilla\",\"file/A.zip\"]\n\n") << B("\n\xff:1\nresourcePacks:[\"vanilla\",\"file/A.zip\",\"file/B.zip\"]\n\n");
    }

    void writeKeepsEveryOtherByte()
    {
        QFETCH(QByteArray, before);
        QFETCH(QByteArray, after);
        Rig rig;
        rig.addPack(S("A.zip"));
        rig.addPack(S("B.zip"));
        QVERIFY(writeFile(rig.options(), before));
        const Outcome out = writeOrder(rig.options(), rig.history(), { S("B.zip"), S("A.zip") }, rig.existing(), rig.isRunning());
        QCOMPARE(out.result, Result::Ok);
        QCOMPARE(readFile(rig.options()), after);
    }

    void missingFileIsCreatedWithOneLine()
    {
        Rig rig;
        rig.addPack(S("A.zip"));
        QVERIFY(!QFile::exists(rig.options()));
        const Outcome out = writeOrder(rig.options(), rig.history(), { S("A.zip") }, rig.existing(), rig.isRunning());
        QCOMPARE(out.result, Result::Ok);
        QCOMPARE(readFile(rig.options()), B("resourcePacks:[\"vanilla\",\"file/A.zip\"]\n"));
        // nothing to turn on: no file is invented
        Rig empty;
        QCOMPARE(writeOrder(empty.options(), empty.history(), {}, empty.existing(), empty.isRunning()).result, Result::NoChange);
        QVERIFY(!QFile::exists(empty.options()));
    }

    void missingKeyIsAppended()
    {
        Rig rig;
        rig.addPack(S("A.zip"));
        QVERIFY(writeFile(rig.options(), B("fov:0.5\r\nlang:en_us\r\n")));
        QCOMPARE(writeOrder(rig.options(), rig.history(), { S("A.zip") }, rig.existing(), rig.isRunning()).result, Result::Ok);
        QCOMPARE(readFile(rig.options()), B("fov:0.5\r\nlang:en_us\r\nresourcePacks:[\"vanilla\",\"file/A.zip\"]\r\n"));
        // an empty file
        Rig blank;
        blank.addPack(S("A.zip"));
        QVERIFY(writeFile(blank.options(), {}));
        QCOMPARE(writeOrder(blank.options(), blank.history(), { S("A.zip") }, blank.existing(), blank.isRunning()).result, Result::Ok);
        QCOMPARE(readFile(blank.options()), B("resourcePacks:[\"vanilla\",\"file/A.zip\"]\n"));
    }

    void unreadableLineIsNeverTouched()
    {
        Rig rig;
        rig.addPack(S("A.zip"));
        const QByteArray old = B("fov:0.5\nresourcePacks:[Old.zip]\n");
        QVERIFY(writeFile(rig.options(), old));
        const Outcome out = writeOrder(rig.options(), rig.history(), { S("A.zip") }, rig.existing(), rig.isRunning());
        QCOMPARE(out.result, Result::Unreadable);
        QCOMPARE(readFile(rig.options()), old);
        QVERIFY(!QFile::exists(rig.history()));
    }

    void runningRefusesEverything()
    {
        Rig rig;
        rig.addPack(S("A.zip"));
        const QByteArray before = B("resourcePacks:[\"vanilla\"]\n");
        QVERIFY(writeFile(rig.options(), before));
        rig.running = true;
        QCOMPARE(writeOrder(rig.options(), rig.history(), { S("A.zip") }, rig.existing(), rig.isRunning()).result, Result::Running);
        QCOMPARE(readFile(rig.options()), before);
        QVERIFY(!QFile::exists(rig.history()));
        QCOMPARE(undoLast(rig.options(), rig.history(), rig.isRunning()).result, Result::Running);
        // and when it starts between the look and the write: asked a second time, just before the first byte
        rig.running = false;
        int asked = 0;
        const std::function<bool()> startsLate = [&asked]() { return ++asked >= 2; };
        QCOMPARE(writeOrder(rig.options(), rig.history(), { S("A.zip") }, rig.existing(), startsLate).result, Result::Running);
        QCOMPARE(readFile(rig.options()), before);
        QVERIFY(!QFile::exists(rig.history()));
    }

    void noChangeWritesNothing()
    {
        Rig rig;
        rig.addPack(S("A.zip"));
        const QByteArray before = B("a:1\nresourcePacks:[\"vanilla\",\"file/A.zip\"]\n");
        QVERIFY(writeFile(rig.options(), before));
        QCOMPARE(writeOrder(rig.options(), rig.history(), { S("A.zip") }, rig.existing(), rig.isRunning()).result, Result::NoChange);
        QCOMPARE(readFile(rig.options()), before);
        QVERIFY(!QFile::exists(rig.history()));
    }

    void folderPacksAreFileEntries()
    {
        Rig rig;
        rig.addPack(S("Folder Pack"));
        rig.addPack(S("A.zip"));
        QVERIFY(QDir().mkpath(rig.packs() + S("/.index")));  // packwiz metadata: not a pack
        QVERIFY(writeFile(rig.packs() + S("/Old.zip.disabled"), B("PK")));  // hidden: not a pack the game sees
        QCOMPARE(rig.existing(), setOf({ S("Folder Pack"), S("A.zip") }));
        QVERIFY(writeFile(rig.options(), B("resourcePacks:[\"vanilla\"]\n")));
        QCOMPARE(writeOrder(rig.options(), rig.history(), { S("Folder Pack"), S("A.zip") }, rig.existing(), rig.isRunning()).result, Result::Ok);
        QCOMPARE(readFile(rig.options()), B("resourcePacks:[\"vanilla\",\"file/A.zip\",\"file/Folder Pack\"]\n"));
        QCOMPARE(activeFiles(readOptions(rig.options()).entries), (QStringList{ S("Folder Pack"), S("A.zip") }));
    }

    void staleEntriesSurviveAWrite()
    {
        Rig rig;
        rig.addPack(S("A.zip"));
        rig.addPack(S("B.zip"));
        QVERIFY(writeFile(rig.options(), B("resourcePacks:[\"vanilla\",\"file/Gone.zip\",\"file/A.zip\"]\n")));
        QCOMPARE(writeOrder(rig.options(), rig.history(), { S("B.zip"), S("A.zip") }, rig.existing(), rig.isRunning()).result, Result::Ok);
        QCOMPARE(readOptions(rig.options()).entries, (QStringList{ S("vanilla"), S("file/Gone.zip"), S("file/A.zip"), S("file/B.zip") }));
    }

    void turnOnAtTopAfterADownload()
    {
        Rig rig;
        rig.addPack(S("A.zip"));
        rig.addPack(S("New.zip"));
        QVERIFY(writeFile(rig.options(), B("fov:1\nresourcePacks:[\"vanilla\",\"file/A.zip\"]\n")));
        const Outcome out = turnOnAtTop(rig.options(), rig.history(), rig.packs(), S("New.zip"), rig.isRunning());
        QCOMPARE(out.result, Result::Ok);
        QCOMPARE(readFile(rig.options()), B("fov:1\nresourcePacks:[\"vanilla\",\"file/A.zip\",\"file/New.zip\"]\n"));
        // the same history as every other change: Undo takes it back
        QCOMPARE(undoLast(rig.options(), rig.history(), rig.isRunning(), out.id).result, Result::Ok);
        QCOMPARE(readFile(rig.options()), B("fov:1\nresourcePacks:[\"vanilla\",\"file/A.zip\"]\n"));
        // a pack that is not there, and a running game
        QCOMPARE(turnOnAtTop(rig.options(), rig.history(), rig.packs(), S("Nope.zip"), rig.isRunning()).result, Result::IoError);
        rig.running = true;
        QCOMPARE(turnOnAtTop(rig.options(), rig.history(), rig.packs(), S("New.zip"), rig.isRunning()).result, Result::Running);
        // no options.txt yet
        Rig fresh;
        fresh.addPack(S("New.zip"));
        QCOMPARE(turnOnAtTop(fresh.options(), fresh.history(), fresh.packs(), S("New.zip"), fresh.isRunning()).result, Result::Ok);
        QCOMPARE(readFile(fresh.options()), B("resourcePacks:[\"vanilla\",\"file/New.zip\"]\n"));
    }

    // =============================== history and Undo ========================================================================

    void undoPutsBackTheExactBytes()
    {
        Rig rig;
        rig.addPack(S("A.zip"));
        rig.addPack(S("B.zip"));
        const QByteArray before = B("a:1\r\nresourcePacks:[\"vanilla\",\"file/A.zip\"]\r\nincompatibleResourcePacks:[\"file/Old.zip\"]\r\nz:9");
        QVERIFY(writeFile(rig.options(), before));
        const Outcome out = writeOrder(rig.options(), rig.history(), { S("B.zip"), S("A.zip") }, rig.existing(), rig.isRunning());
        QCOMPARE(out.result, Result::Ok);
        QVERIFY(readFile(rig.options()) != before);
        const QList<HistoryEntry> history = readHistory(rig.history());
        QCOMPARE(history.size(), 1);
        QCOMPARE(history.first().id, out.id);
        QCOMPARE(history.first().packsLine, std::optional<QByteArray>(B("resourcePacks:[\"vanilla\",\"file/A.zip\"]")));
        QCOMPARE(history.first().incompatibleLine, std::optional<QByteArray>(B("incompatibleResourcePacks:[\"file/Old.zip\"]")));
        QVERIFY(history.first().fileExisted);
        QCOMPARE(undoLast(rig.options(), rig.history(), rig.isRunning()).result, Result::Ok);
        QCOMPARE(readFile(rig.options()), before);
        QVERIFY(readHistory(rig.history()).isEmpty());  // the entry is spent
        QCOMPARE(undoLast(rig.options(), rig.history(), rig.isRunning()).result, Result::NothingToUndo);
    }

    void undoOfACreatedFileRemovesIt()
    {
        Rig rig;
        rig.addPack(S("A.zip"));
        QCOMPARE(writeOrder(rig.options(), rig.history(), { S("A.zip") }, rig.existing(), rig.isRunning()).result, Result::Ok);
        const QList<HistoryEntry> history = readHistory(rig.history());
        QVERIFY(!history.first().fileExisted);
        QVERIFY(!history.first().packsLine);
        QCOMPARE(undoLast(rig.options(), rig.history(), rig.isRunning()).result, Result::Ok);
        QVERIFY(!QFile::exists(rig.options()));
        // when the game wrote its own lines in between, they stay and only the line goes
        QCOMPARE(writeOrder(rig.options(), rig.history(), { S("A.zip") }, rig.existing(), rig.isRunning()).result, Result::Ok);
        QVERIFY(writeFile(rig.options(), readFile(rig.options()) + B("fov:1\n")));
        QCOMPARE(undoLast(rig.options(), rig.history(), rig.isRunning()).result, Result::Ok);
        QCOMPARE(readFile(rig.options()), B("fov:1\n"));
    }

    void undoOfAnAppendedLineRemovesTheLine()
    {
        Rig rig;
        rig.addPack(S("A.zip"));
        const QByteArray before = B("fov:1\nlang:x");
        QVERIFY(writeFile(rig.options(), before));
        QCOMPARE(writeOrder(rig.options(), rig.history(), { S("A.zip") }, rig.existing(), rig.isRunning()).result, Result::Ok);
        QCOMPARE(undoLast(rig.options(), rig.history(), rig.isRunning()).result, Result::Ok);
        QCOMPARE(readFile(rig.options()), before);
    }

    void undoOnlyTheChangeTheCallerMeans()
    {
        Rig rig;
        rig.addPack(S("A.zip"));
        rig.addPack(S("B.zip"));
        QVERIFY(writeFile(rig.options(), B("resourcePacks:[\"vanilla\"]\n")));
        const Outcome first = writeOrder(rig.options(), rig.history(), { S("A.zip") }, rig.existing(), rig.isRunning());
        const Outcome second = writeOrder(rig.options(), rig.history(), { S("B.zip"), S("A.zip") }, rig.existing(), rig.isRunning());
        QVERIFY(first.id != second.id);
        QVERIFY(second.id.toLongLong() > first.id.toLongLong());
        QCOMPARE(undoLast(rig.options(), rig.history(), rig.isRunning(), first.id).result, Result::Stale);  // the newest is the second
        QCOMPARE(readOptions(rig.options()).entries, (QStringList{ S("vanilla"), S("file/A.zip"), S("file/B.zip") }));
        QCOMPARE(undoLast(rig.options(), rig.history(), rig.isRunning(), second.id).result, Result::Ok);
        QCOMPARE(readOptions(rig.options()).entries, (QStringList{ S("vanilla"), S("file/A.zip") }));
        QCOMPARE(undoLast(rig.options(), rig.history(), rig.isRunning(), first.id).result, Result::Ok);
        QCOMPARE(readFile(rig.options()), B("resourcePacks:[\"vanilla\"]\n"));
    }

    void historyKeepsTheNewestTwenty()
    {
        Rig rig;
        for (int i = 0; i < 25; ++i) {
            rig.addPack(S("P%1.zip").arg(i));
        }
        QVERIFY(writeFile(rig.options(), B("resourcePacks:[\"vanilla\"]\n")));
        QStringList active;
        QString lastId;
        for (int i = 0; i < 25; ++i) {
            active.prepend(S("P%1.zip").arg(i));
            const Outcome out = writeOrder(rig.options(), rig.history(), active, rig.existing(), rig.isRunning());
            QCOMPARE(out.result, Result::Ok);
            lastId = out.id;
        }
        const QList<HistoryEntry> history = readHistory(rig.history());
        QCOMPARE(history.size(), kHistoryCap);
        QCOMPARE(history.first().id, lastId);  // newest first
        for (int i = 1; i < history.size(); ++i) {
            QVERIFY(history.at(i - 1).id.toLongLong() > history.at(i).id.toLongLong());
        }
        // the oldest five are gone: undoing everything we remember reaches the state before change 5
        for (int i = 0; i < kHistoryCap; ++i) {
            QCOMPARE(undoLast(rig.options(), rig.history(), rig.isRunning()).result, Result::Ok);
        }
        QCOMPARE(activeFiles(readOptions(rig.options()).entries).size(), 5);
        QCOMPARE(undoLast(rig.options(), rig.history(), rig.isRunning()).result, Result::NothingToUndo);
    }

    void damagedHistoryIsEmptyAndNeverBlocksAWrite()
    {
        Rig rig;
        rig.addPack(S("A.zip"));
        QVERIFY(writeFile(rig.history(), B("{ this is not json")));
        QVERIFY(readHistory(rig.history()).isEmpty());
        QVERIFY(writeFile(rig.options(), B("resourcePacks:[\"vanilla\"]\n")));
        QCOMPARE(writeOrder(rig.options(), rig.history(), { S("A.zip") }, rig.existing(), rig.isRunning()).result, Result::Ok);
        QCOMPARE(readHistory(rig.history()).size(), 1);
    }

    void historyThatCannotBeWrittenStopsTheWrite()
    {
        Rig rig;
        rig.addPack(S("A.zip"));
        const QByteArray before = B("resourcePacks:[\"vanilla\"]\n");
        QVERIFY(writeFile(rig.options(), before));
        // `.zero` is a file: the history cannot be written, so options.txt must stay as it is
        QVERIFY(writeFile(rig.root.filePath(S(".zero")), B("in the way")));
        const Outcome out = writeOrder(rig.options(), rig.history(), { S("A.zip") }, rig.existing(), rig.isRunning());
        QCOMPARE(out.result, Result::IoError);
        QVERIFY(!out.error.isEmpty());
        QCOMPARE(readFile(rig.options()), before);
    }

    void nonUtf8LinesSurviveTheHistory()
    {
        Rig rig;
        rig.addPack(S("A.zip"));
        const QByteArray before = B("resourcePacks:[\"vanilla\"]\nincompatibleResourcePacks:[\"file/\xe9.zip\"]\n");
        QVERIFY(writeFile(rig.options(), before));
        QCOMPARE(writeOrder(rig.options(), rig.history(), { S("A.zip") }, rig.existing(), rig.isRunning()).result, Result::Ok);
        QCOMPARE(undoLast(rig.options(), rig.history(), rig.isRunning()).result, Result::Ok);
        QCOMPARE(readFile(rig.options()), before);
    }

    // =============================== Minecraft versions ======================================================================

    void orderingNeedsMinecraft113()
    {
        for (const char* version : { "1.13", "1.13.2", "1.14.4", "1.20.1", "1.21.1", "1.21.11", "26.1", "26.1.2", "1.13-pre1", "18w01a", "23w13a", "17w43a", "24w10a",
                                     "1.21.5-rc1", "" }) {
            QVERIFY2(supportsOrdering(S(version)), version);
        }
        for (const char* version : { "1.12", "1.12.2", "1.8.9", "1.7.10", "1.0", "b1.7.3", "a1.2.6", "c0.30_01c", "rd-132211", "inf-20100618", "17w31a", "15w14a",
                                     "1.RV-Pre1" }) {
            QVERIFY2(!supportsOrdering(S(version)), version);
        }
    }

    // =============================== the description =========================================================================

    void descriptionIsPlainText()
    {
        // JSON text components after Prism's processComponent: spans, underline, strike, links, raw text, no escaping
        QCOMPARE(plainDescription(S("<span style=\"color: red; font-weight: bold;\">Faithful</span> 32x")), S("Faithful 32x"));
        QCOMPARE(plainDescription(S("<u>under</u><s>struck</s> <a href=\"https://x.test/?a=1&b=2\">link</a>")), S("understruck link"));
        QCOMPARE(plainDescription(S("<span style=\"color: #ff0000;\">A</span><span style=\"font-style: italic;\"><u>B</u></span>C")), S("ABC"));
        QCOMPARE(plainDescription(S("line one<br>line two<br/>three")), S("line one line two three"));
        // legacy section codes (also the hex form) and a lone sign
        QCOMPARE(plainDescription(S("§a§lGreen§r and §cred")), S("Green and red"));
        QCOMPARE(plainDescription(S("§x§f§f§0§0§0§0Hex")), S("Hex"));
        QCOMPARE(plainDescription(S("trailing §")), S("trailing"));
        // entities and white space
        QCOMPARE(plainDescription(S("Tom &amp; Jerry &lt;3 &quot;x&quot; it&#39;s&nbsp;ok")), S("Tom & Jerry <3 \"x\" it's ok"));
        QCOMPARE(plainDescription(S("  many \n\n  spaces\t here  ")), S("many spaces here"));
        QCOMPARE(plainDescription(S("&lt;b&gt;not a tag&lt;/b&gt;")), S("<b>not a tag</b>"));
        // text that merely contains a "<" is kept, and so is a number or an empty description
        QCOMPARE(plainDescription(S("a < b and c > d")), S("a < b and c > d"));
        QCOMPARE(plainDescription(S("")), S(""));
        QCOMPARE(plainDescription(S("<span></span>")), S(""));
        // an array of components: pieces joined as Prism joined them
        QCOMPARE(plainDescription(S("<span style=\"color: gold;\">One</span><span style=\"color: aqua;\"> Two</span>")), S("One Two"));
    }

    // =============================== the pack format =========================================================================

    void packFitRules()
    {
        const FormatMap map = formats();
        QCOMPARE(packFit(34, map, S("1.21.1")).kind, FitKind::Fits);
        QCOMPARE(packFit(34, map, S("1.21")).kind, FitKind::Fits);
        const PackFit older = packFit(15, map, S("1.21.1"));
        QCOMPARE(older.kind, FitKind::MadeFor);
        QCOMPARE(older.madeFor, S("1.20"));
        QCOMPARE(madeForText(older.madeFor), S("Made for 1.20"));
        QCOMPARE(packFit(34, map, S("1.20.1")).kind, FitKind::MadeFor);  // newer than the instance
        QCOMPARE(packFit(46, map, S("1.21.4")).kind, FitKind::Fits);
        QCOMPARE(packFit(34, map, S("1.21.1")).madeFor, QString());
    }

    void packFitUnknown()
    {
        const FormatMap map = formats();
        QCOMPARE(packFit(0, map, S("1.21.1")).kind, FitKind::Unknown);     // no pack_format (a newer min / max format pack)
        QCOMPARE(packFit(99, map, S("1.21.1")).kind, FitKind::Unknown);    // not in the table
        QCOMPARE(packFit(35, map, S("1.21.1")).kind, FitKind::Unknown);    // a snapshot format cannot be ordered against a release
        QCOMPARE(packFit(34, map, S("24w10a")).kind, FitKind::Unknown);    // the instance runs a snapshot
        QCOMPARE(packFit(34, map, QString()).kind, FitKind::Unknown);
        QCOMPARE(packFit(34, {}, S("1.21.1")).kind, FitKind::Unknown);
        QCOMPARE(packFit(-3, map, S("1.21.1")).kind, FitKind::Unknown);
    }

    // =============================== rows, groups, captions ==================================================================

    void namesOfFiles()
    {
        QCOMPARE(packNameOf(S("Faithful 32x.zip"), false), S("Faithful 32x"));
        QCOMPARE(packNameOf(S("Faithful 32x.zip.disabled"), false), S("Faithful 32x"));
        QCOMPARE(packNameOf(S("Folder.zip"), true), S("Folder.zip"));  // a folder keeps its name, even one that looks like a zip
        QCOMPARE(packNameOf(S("UPPER.ZIP"), false), S("UPPER"));
        QCOMPARE(entryNameOf(pack(S("A"))), S("A.zip"));
        QCOMPARE(entryNameOf(pack(S("B"), -1, true)), QString());
        QCOMPARE(entryNameOf(pack(S("Dir"), -1, false, true)), S("Dir"));
    }

    void overlayFollowsOptions()
    {
        QList<PackRow> rows = { pack(S("A")), pack(S("B")), pack(S("C"), -1, true), pack(S("D")) };
        overlayActive(rows, { S("D.zip"), S("Gone.zip"), S("A.zip"), S("C.zip.disabled") });
        QVERIFY(rows.at(0).active);
        QCOMPARE(rows.at(0).activeIndex, 1);  // the pack that is gone takes no place
        QVERIFY(!rows.at(1).active);
        QVERIFY(!rows.at(2).active);          // a hidden file is never on
        QVERIFY(rows.at(3).active);
        QCOMPARE(rows.at(3).activeIndex, 0);
        QCOMPARE(activatableNames(rows), (QStringList{ S("A.zip"), S("B.zip"), S("D.zip") }));
        overlayActive(rows, {});
        QVERIFY(!rows.at(0).active && !rows.at(3).active);
    }

    void filterIdsAndSearch()
    {
        for (const Filter filter : allFilters()) {
            QCOMPARE(filterFromId(filterId(filter)), filter);
        }
        QCOMPARE(filterFromId(S("nonsense")), Filter::All);
        QCOMPARE(filterLabel(Filter::On), S("On"));
        PackRow faithful = pack(S("Faithful 32x"), 0);
        faithful.description = S("Faithful 32x resolution for 1.21");
        QVERIFY(matchesFilter(faithful, Filter::All));
        QVERIFY(matchesFilter(faithful, Filter::On));
        QVERIFY(!matchesFilter(faithful, Filter::Off));
        QVERIFY(matchesFilter(pack(S("X")), Filter::Off));
        QVERIFY(matchesSearch(faithful, S("faith 32")));
        QVERIFY(matchesSearch(faithful, S("RESOLUTION")));  // the description counts
        QVERIFY(matchesSearch(faithful, S("")));
        QVERIFY(!matchesSearch(faithful, S("faith nope")));
        QVERIFY(matchesSearch(faithful, S("32x.zip")));  // the file name counts
    }

    void listHasTwoGroups()
    {
        QList<PackRow> rows = { pack(S("Charlie")), pack(S("Alpha"), 1), pack(S("Bravo"), 0), pack(S("Delta"), -1, true), pack(S("Echo"), -1, false, true),
                                pack(S("alpha two")) };
        const QList<ListEntry> list = layoutList(rows, Filter::All, QString(), true);
        // ON - top wins: Bravo (0), Alpha (1), Default resources; AVAILABLE by name, hidden last
        QCOMPARE(list.size(), 9);  // header, 2 on, default, header, 4 available
        QCOMPARE(int(list.at(0).kind), int(ListEntry::Kind::Header));
        QCOMPARE(list.at(0).text, onHeaderText());
        QCOMPARE(list.at(1).row, 2);
        QCOMPARE(list.at(1).onIndex, 0);
        QCOMPARE(list.at(2).row, 1);
        QCOMPARE(list.at(2).onIndex, 1);
        QCOMPARE(int(list.at(3).kind), int(ListEntry::Kind::Default));
        QCOMPARE(list.at(3).text, defaultRowName());
        QCOMPARE(int(list.at(4).kind), int(ListEntry::Kind::Header));
        QCOMPARE(list.at(4).text, availableHeaderText());
        QCOMPARE(rows.at(list.at(5).row).name, S("alpha two"));
        QCOMPARE(rows.at(list.at(6).row).name, S("Charlie"));
        QCOMPARE(rows.at(list.at(7).row).name, S("Echo"));
        QCOMPARE(rows.at(list.at(8).row).name, S("Delta"));  // hidden: last
        QCOMPARE(list.at(8).onIndex, -1);
    }

    void listFiltersAndSearch()
    {
        QList<PackRow> rows = { pack(S("Alpha"), 0), pack(S("Beta")), pack(S("Gamma"), 1) };
        auto kinds = [](const QList<ListEntry>& list) {
            QStringList out;
            for (const ListEntry& e : list) {
                out.append(e.kind == ListEntry::Kind::Header ? S("H") : e.kind == ListEntry::Kind::Default ? S("D") : S("P"));
            }
            return out.join(QString());
        };
        QCOMPARE(kinds(layoutList(rows, Filter::All, {}, true)), S("HPPDHP"));
        QCOMPARE(kinds(layoutList(rows, Filter::On, {}, true)), S("HPPD"));
        QCOMPARE(kinds(layoutList(rows, Filter::Off, {}, true)), S("HP"));  // the default row is not an Off pack
        // a search that matches only an ON pack: the default row is hidden (it does not match), the empty group has no header
        QCOMPARE(kinds(layoutList(rows, Filter::All, S("gam"), true)), S("HP"));
        QCOMPARE(kinds(layoutList(rows, Filter::All, S("beta"), true)), S("HP"));
        QCOMPARE(layoutList(rows, Filter::All, S("beta"), true).last().row, 1);
        // "default" finds the default row
        QCOMPARE(kinds(layoutList(rows, Filter::All, S("default"), true)), S("HD"));
        QCOMPARE(kinds(layoutList(rows, Filter::All, S("zzz"), true)), QString());
        // no packs at all: just the default row
        QCOMPARE(kinds(layoutList({}, Filter::All, {}, true)), S("HD"));
    }

    void listWithoutOrder()
    {
        QList<PackRow> rows = { pack(S("b")), pack(S("A"), 0), pack(S("c"), -1, true) };
        const QList<ListEntry> flat = layoutList(rows, Filter::All, {}, false);
        QCOMPARE(flat.size(), 3);  // no headers, no default row; by name
        for (const ListEntry& entry : flat) {
            QCOMPARE(int(entry.kind), int(ListEntry::Kind::Pack));
            QCOMPARE(entry.onIndex, -1);
        }
        QCOMPARE(rows.at(flat.at(0).row).name, S("A"));
        QCOMPARE(rows.at(flat.at(1).row).name, S("b"));
        QCOMPARE(rows.at(flat.at(2).row).name, S("c"));
    }

    void captions()
    {
        QList<PackRow> rows = { pack(S("A"), 0), pack(S("B")), pack(S("C"), 1) };
        const Counts counts = countOf(rows);
        QCOMPARE(counts.total, 3);
        QCOMPARE(counts.on, 2);
        QCOMPARE(captionText(counts, 3, false, true), S("3 packs · 2 on"));
        QCOMPARE(captionText(countOf({ pack(S("A")) }), 1, false, true), S("1 pack · 0 on"));
        QCOMPARE(captionText(counts, 1, true, true), S("1 of 3 packs"));
        QCOMPARE(captionText(counts, 3, false, false), S("3 packs"));
        QCOMPARE(selectionText(2), S("2 selected"));
        PackRow hidden = pack(S("H"), -1, true);
        QCOMPARE(countOf({ hidden, pack(S("A")) }).hidden, 1);
    }

    void pillsAndTooltip()
    {
        PackRow local = pack(S("A"));
        QCOMPARE(pillsFor(local).size(), 1);
        QCOMPARE(pillsFor(local).first().text, S("Local file"));
        PackRow modrinth = pack(S("B"));
        modrinth.source = Source::Modrinth;
        QCOMPARE(pillsFor(modrinth).first().text, S("Modrinth"));
        modrinth.source = Source::CurseForge;
        QCOMPARE(pillsFor(modrinth).first().text, S("CurseForge"));
        PackRow dir = pack(S("Dir"), -1, false, true);
        QStringList texts;
        for (const auto& pill : pillsFor(dir)) {
            texts.append(pill.text);
        }
        QCOMPARE(texts, (QStringList{ S("Local file"), S("Folder") }));
        PackRow hidden = pack(S("H"), -1, true);
        QCOMPARE(pillsFor(hidden).last().text, S("Hidden from the game"));
        QVERIFY(pillsFor(hidden).last().priority >= Zero::Mods::Installed::kKeepPriority);
        PackRow old = pack(S("O"));
        old.fit.kind = FitKind::MadeFor;
        old.fit.madeFor = S("1.20");
        QCOMPARE(pillsFor(old).last().text, S("Made for 1.20"));
        QCOMPARE(int(pillsFor(old).last().tone), int(Zero::Mods::Installed::PillTone::Warning));
        QVERIFY(rowToolTip(old).contains(S("O.zip")));
        QVERIFY(rowToolTip(old).contains(S("Made for 1.20")));
        PackRow named = pack(S("O"));
        named.projectName = S("Original Pack");
        QVERIFY(rowToolTip(named).contains(S("Original Pack")));
    }

    void movingAndTexts()
    {
        const QStringList list = { S("a"), S("b"), S("c"), S("d") };
        QCOMPARE(moved(list, 3, 0), (QStringList{ S("d"), S("a"), S("b"), S("c") }));
        QCOMPARE(moved(list, 0, 3), (QStringList{ S("b"), S("c"), S("d"), S("a") }));
        QCOMPARE(moved(list, 1, 2), (QStringList{ S("a"), S("c"), S("b"), S("d") }));
        QCOMPARE(moved(list, 2, 2), list);
        QCOMPARE(moved(list, -1, 2), list);
        QCOMPARE(moved(list, 1, 9), list);
        QCOMPARE(turnedOnText(S("Faithful")), S("Turned on Faithful"));
        QCOMPARE(turnedOffText(S("Faithful")), S("Turned off Faithful"));
        QCOMPARE(movedToTopText(S("Faithful")), S("Moved Faithful to the top"));
        QCOMPARE(movedToBottomText(S("Faithful")), S("Moved Faithful to the bottom"));
        QCOMPARE(movedUpText(S("Faithful")), S("Moved Faithful up"));
        QCOMPARE(movedDownText(S("Faithful")), S("Moved Faithful down"));
        QCOMPARE(movedText(S("Faithful")), S("Moved Faithful"));
        QCOMPARE(addedAndOnText(S("Faithful"), S("Test")), S("Added Faithful to Test and turned it on"));
    }
};

QTEST_GUILESS_MAIN(PackOrderTest)

#include "PackOrder_test.moc"
