// SPDX-License-Identifier: GPL-3.0-only
// WorldScanner (Worlds hub): scanWorldsDir on real folder layouts, folderSize, sorting / search, the async scanner and its generations.
// The level.dat files are generated with the bundled nbt library (the saves under testdata/WorldSaveParse have empty level.dat files).
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirListing>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <filesystem>
#include <sstream>

#include <io/stream_writer.h>
#include <tag_compound.h>
#include <tag_primitive.h>
#include <tag_string.h>

#include "GZip.h"
#include "zero/worlds/WorldScanner.h"

using namespace Zero::Worlds;

namespace {

struct Level {
    QString levelName = QStringLiteral("My World");
    qint64 lastPlayed = 1700000000000;
    int gameType = 0;
    qint64 seed = 123456789;
    QString version = QStringLiteral("1.20.4");  // empty = no Version compound
    bool hardcore = false;
};

QByteArray levelDat(const Level& level)
{
    nbt::tag_compound data;
    data.insert("LevelName", nbt::tag_string(level.levelName.toStdString()));
    data.insert("LastPlayed", nbt::tag_long(level.lastPlayed));
    data.insert("GameType", nbt::tag_int(level.gameType));
    data.insert("RandomSeed", nbt::tag_long(level.seed));
    data.insert("hardcore", nbt::tag_byte(level.hardcore ? 1 : 0));
    if (!level.version.isEmpty()) {
        nbt::tag_compound version;
        version.insert("Name", nbt::tag_string(level.version.toStdString()));
        data.insert("Version", std::move(version));
    }
    nbt::tag_compound root;
    root.insert("Data", std::move(data));
    std::ostringstream stream;
    nbt::io::write_tag("", root, stream);
    QByteArray raw(stream.str().data(), static_cast<qsizetype>(stream.str().size()));
    QByteArray gz;
    GZip::zip(raw, gz);
    return gz;
}

void writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
    f.write(data);
}

QString makeWorld(const QString& savesDir, const QString& folder, const Level& level, bool icon = false)
{
    const QString dir = savesDir + QLatin1Char('/') + folder;
    writeFile(dir + QStringLiteral("/level.dat"), levelDat(level));
    if (icon)
        writeFile(dir + QStringLiteral("/icon.png"), QByteArray(10, 'x'));
    return dir;
}

QByteArray hashTree(const QString& root)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QStringList files;
    for (const auto& e : QDirListing(root, QDirListing::IteratorFlag::Recursive | QDirListing::IteratorFlag::FilesOnly))
        files << e.filePath();
    files.sort();
    for (const QString& path : files) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        hash.addData(path.toUtf8());
        hash.addData(f.readAll());
        hash.addData(QByteArray::number(QFileInfo(path).lastModified().toMSecsSinceEpoch()));
    }
    return hash.result();
}

}  // namespace

class WorldScannerTest : public QObject {
    Q_OBJECT

   private slots:
    void scanReadsMetadata()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString saves = root.filePath("saves");
        Level hardcore;
        hardcore.levelName = "Iron Run";
        hardcore.gameType = 2;
        hardcore.hardcore = true;
        hardcore.version = "1.21";
        makeWorld(saves, "plain", Level{}, true);
        makeWorld(saves, "iron", hardcore);

        const QList<WorldEntry> worlds = scanWorldsDir("inst", "My Instance", saves);
        QCOMPARE(worlds.size(), 2);

        const WorldEntry iron = worlds.at(0);  // sorted by folder name
        QCOMPARE(iron.folderName, QStringLiteral("iron"));
        QCOMPARE(iron.name, QStringLiteral("Iron Run"));
        QCOMPARE(iron.versionName, QStringLiteral("1.21"));
        QCOMPARE(iron.gameMode, QStringLiteral("Adventure"));
        QCOMPARE(iron.gameTypeCode, 2);
        QVERIFY(iron.hardcore);
        QVERIFY(iron.iconPath.isEmpty());
        QCOMPARE(iron.sizeBytes, qint64(-1));
        QCOMPARE(iron.key(), QStringLiteral("inst/iron"));

        const WorldEntry plain = worlds.at(1);
        QCOMPARE(plain.instanceId, QStringLiteral("inst"));
        QCOMPARE(plain.instanceName, QStringLiteral("My Instance"));
        QCOMPARE(plain.name, QStringLiteral("My World"));
        QCOMPARE(plain.versionName, QStringLiteral("1.20.4"));
        QCOMPARE(plain.gameMode, QStringLiteral("Survival"));
        QCOMPARE(plain.gameTypeCode, 0);
        QVERIFY(!plain.hardcore);
        QCOMPARE(plain.lastPlayedMs, qint64(1700000000000));
        QCOMPARE(plain.seed, qint64(123456789));
        QCOMPARE(QFileInfo(plain.iconPath), QFileInfo(saves + "/plain/icon.png"));
        QCOMPARE(QFileInfo(plain.folderPath), QFileInfo(saves + "/plain"));
        QCOMPARE(plain.sizeBytes, qint64(-1));
    }

    void missingVersionIsEmpty()
    {
        QTemporaryDir root;
        Level level;
        level.version.clear();
        makeWorld(root.filePath("saves"), "old", level);
        const auto worlds = scanWorldsDir("i", "I", root.filePath("saves"));
        QCOMPARE(worlds.size(), 1);
        QVERIFY(worlds.first().versionName.isEmpty());
    }

    void skipsInvalidEntries()
    {
        QTemporaryDir root;
        const QString saves = root.filePath("saves");
        makeWorld(saves, "good", Level{});
        QVERIFY(QDir().mkpath(saves + "/no-level-dat"));                         // folder without level.dat
        writeFile(saves + "/no-level-dat/region/r.0.0.mca", "x");
        writeFile(saves + "/broken/level.dat", "this is not a gzipped nbt file");  // level.dat that does not parse
        writeFile(saves + "/archive.zip", "PK");                                   // a zip next to the folders
        writeFile(saves + "/notes.txt", "hello");                                  // a loose file

        const auto worlds = scanWorldsDir("i", "I", saves);
        QCOMPARE(worlds.size(), 1);
        QCOMPARE(worlds.first().folderName, QStringLiteral("good"));
    }

    void emptyAndMissingSavesDir()
    {
        QTemporaryDir root;
        QVERIFY(QDir().mkpath(root.filePath("saves")));
        QVERIFY(scanWorldsDir("i", "I", root.filePath("saves")).isEmpty());
        QVERIFY(scanWorldsDir("i", "I", root.filePath("does-not-exist")).isEmpty());
        QVERIFY(scanWorldsDir("i", "I", QString()).isEmpty());
    }

    void scanDoesNotModifyWorlds()
    {
        QTemporaryDir root;
        const QString saves = root.filePath("saves");
        makeWorld(saves, "a", Level{}, true);
        writeFile(saves + "/a/region/r.0.0.mca", QByteArray(4096, 'r'));
        const QByteArray before = hashTree(saves);
        const QStringList namesBefore = QDir(saves + "/a").entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
        scanWorldsDir("i", "I", saves);
        folderSize(saves + "/a");
        QCOMPARE(hashTree(saves), before);
        QCOMPARE(QDir(saves + "/a").entryList(QDir::AllEntries | QDir::NoDotAndDotDot), namesBefore);
    }

    void folderSizeSumsNestedFiles()
    {
        QTemporaryDir root;
        writeFile(root.filePath("a.bin"), QByteArray(100, 'a'));
        writeFile(root.filePath("region/r.0.0.mca"), QByteArray(2000, 'b'));
        writeFile(root.filePath("deep/er/still/c.dat"), QByteArray(30, 'c'));
        writeFile(root.filePath(".hidden"), QByteArray(5, 'h'));
        QCOMPARE(folderSize(root.path()), qint64(2135));
        QCOMPARE(folderSize(root.filePath("missing")), qint64(0));
    }

    void folderSizeDoesNotFollowSymlinks()
    {
        QTemporaryDir root;
        writeFile(root.filePath("world/a.bin"), QByteArray(100, 'a'));
        writeFile(root.filePath("world/sub/b.bin"), QByteArray(50, 'b'));
        writeFile(root.filePath("outside/big.bin"), QByteArray(10000, 'z'));

        std::error_code ec;
        std::filesystem::create_directory_symlink(std::filesystem::path(root.filePath("outside").toStdWString()),
                                                  std::filesystem::path(root.filePath("world/linked-outside").toStdWString()), ec);
        if (ec)
            QSKIP("Cannot create symbolic links without privileges on this machine");
        // a link back up the tree: following it would never end
        std::filesystem::create_directory_symlink(std::filesystem::path(root.filePath("world").toStdWString()),
                                                  std::filesystem::path(root.filePath("world/sub/loop").toStdWString()), ec);
        QVERIFY2(!ec, "second symlink");
        QCOMPARE(folderSize(root.filePath("world")), qint64(150));
    }

    void folderSizeDoesNotFollowJunctions()
    {
#ifdef Q_OS_WIN
        QTemporaryDir root;
        writeFile(root.filePath("world/a.bin"), QByteArray(100, 'a'));
        writeFile(root.filePath("outside/big.bin"), QByteArray(10000, 'z'));
        QProcess mklink;
        mklink.start("cmd", { "/c", "mklink", "/J", QDir::toNativeSeparators(root.filePath("world/junction")),
                              QDir::toNativeSeparators(root.filePath("outside")) });
        if (!mklink.waitForFinished(10000) || mklink.exitCode() != 0)
            QSKIP("Cannot create a junction here");
        QCOMPARE(folderSize(root.filePath("world")), qint64(100));
#else
        QSKIP("Junctions are a Windows feature");
#endif
    }

    void searchAndSort_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("matches");
        QTest::newRow("empty") << "" << true;
        QTest::newRow("spaces only") << "   " << true;
        QTest::newRow("world name") << "iron" << true;
        QTest::newRow("name, other case") << "RUN" << true;
        QTest::newRow("folder name") << "world_01" << true;
        QTest::newRow("instance name") << "skyblock" << true;
        QTest::newRow("version") << "1.21" << true;
        QTest::newRow("no match") << "nether" << false;
        QTest::newRow("seed is not searched") << "999" << false;
    }

    void searchAndSort()
    {
        QFETCH(QString, text);
        QFETCH(bool, matches);
        WorldEntry e;
        e.instanceId = "i";
        e.instanceName = "Skyblock Neo";
        e.folderName = "World_01";
        e.name = "Iron Run";
        e.versionName = "1.21.1";
        e.seed = 999;
        QCOMPARE(matchesSearch(e, text), matches);
    }

    void sortModes()
    {
        auto entry = [](const QString& inst, const QString& name, qint64 played, qint64 size) {
            WorldEntry e;
            e.instanceId = inst;
            e.instanceName = inst.toUpper();
            e.folderName = name;
            e.name = name;
            e.lastPlayedMs = played;
            e.sizeBytes = size;
            return e;
        };
        const QList<WorldEntry> input = { entry("b", "Charlie", 100, 5), entry("a", "alpha", 300, -1), entry("b", "Bravo", 200, 900),
                                          entry("a", "Delta", 50, 20) };
        auto names = [](const QList<WorldEntry>& l) {
            QStringList out;
            for (const auto& e : l)
                out << e.name;
            return out;
        };

        QList<WorldEntry> list = input;
        sortWorlds(list, SortMode::Recent);
        QCOMPARE(names(list), (QStringList{ "alpha", "Bravo", "Charlie", "Delta" }));
        list = input;
        sortWorlds(list, SortMode::Name);
        QCOMPARE(names(list), (QStringList{ "alpha", "Bravo", "Charlie", "Delta" }));
        list = input;
        sortWorlds(list, SortMode::Size);  // biggest first, not computed yet last
        QCOMPARE(names(list), (QStringList{ "Bravo", "Delta", "Charlie", "alpha" }));
        list = input;
        sortWorlds(list, SortMode::Instance);  // instance A-Z, then last played first
        QCOMPARE(names(list), (QStringList{ "alpha", "Delta", "Bravo", "Charlie" }));

        QCOMPARE(sortModeFromString(sortModeToString(SortMode::Size)), SortMode::Size);
        QCOMPARE(sortModeFromString(sortModeToString(SortMode::Instance)), SortMode::Instance);
        QCOMPARE(sortModeFromString(sortModeToString(SortMode::Name)), SortMode::Name);
        QCOMPARE(sortModeFromString("garbage"), SortMode::Recent);
        QCOMPARE(sortModeFromString(QString()), SortMode::Recent);
    }

    void scannerDeliversWorldsThenSizes()
    {
        QTemporaryDir root;
        makeWorld(root.filePath("a/saves"), "w1", Level{});
        writeFile(root.filePath("a/saves/w1/region/r.mca"), QByteArray(1000, 'x'));
        makeWorld(root.filePath("b/saves"), "w2", Level{});
        const QList<InstanceSource> sources = { { "a", "A", root.filePath("a/saves") }, { "b", "B", root.filePath("b/saves") },
                                                { "c", "C", root.filePath("c/saves") } };  // c has no saves folder

        WorldScanner scanner([sources]() { return sources; });
        QVERIFY(!scanner.isScanning());
        QVERIFY(!scanner.hasScanned());
        QSignalSpy worlds(&scanner, &WorldScanner::worldsChanged);
        QSignalSpy sizes(&scanner, &WorldScanner::sizeReady);
        QSignalSpy scanning(&scanner, &WorldScanner::scanningChanged);

        scanner.rescan();
        QVERIFY(scanner.isScanning());
        QTRY_COMPARE(worlds.count(), 1);
        QCOMPARE(scanner.entries().size(), 2);
        QVERIFY(scanner.hasScanned());
        QTRY_COMPARE(sizes.count(), 2);
        QTRY_VERIFY(!scanner.isScanning());
        QCOMPARE(scanning.count(), 2);  // true, then false
        QCOMPARE(scanning.at(0).at(0).toBool(), true);
        QCOMPARE(scanning.at(1).at(0).toBool(), false);

        qint64 total = 0;
        for (const auto& e : scanner.entries()) {
            QVERIFY(e.sizeBytes > 0);
            total += e.sizeBytes;
        }
        QVERIFY(total >= 1000);
        for (const auto& call : sizes)
            QVERIFY(call.at(0).toString() == "a/w1" || call.at(0).toString() == "b/w2");

        // a second scan keeps the known sizes (no flicker) and ends again
        worlds.clear();
        scanner.rescan();
        QTRY_COMPARE(worlds.count(), 1);
        for (const auto& e : scanner.entries())
            QVERIFY(e.sizeBytes > 0);
        QTRY_VERIFY(!scanner.isScanning());
    }

    void scannerWithNoInstancesFinishes()
    {
        WorldScanner scanner([]() { return QList<InstanceSource>(); });
        QSignalSpy worlds(&scanner, &WorldScanner::worldsChanged);
        scanner.rescan();
        QTRY_COMPARE(worlds.count(), 1);
        QVERIFY(scanner.entries().isEmpty());
        QTRY_VERIFY(!scanner.isScanning());
        QVERIFY(scanner.hasScanned());
    }

    void olderGenerationsAreDropped()
    {
        QTemporaryDir root;
        makeWorld(root.filePath("first/saves"), "from-first-scan", Level{});
        makeWorld(root.filePath("second/saves"), "from-second-scan", Level{});

        int call = 0;
        WorldScanner scanner([&]() {
            ++call;
            return QList<InstanceSource>{ call == 1 ? InstanceSource{ "first", "First", root.filePath("first/saves") }
                                                    : InstanceSource{ "second", "Second", root.filePath("second/saves") } };
        });
        QSignalSpy worlds(&scanner, &WorldScanner::worldsChanged);
        QSignalSpy sizes(&scanner, &WorldScanner::sizeReady);

        scanner.rescan();
        const quint64 firstGeneration = scanner.generation();
        scanner.rescan();  // supersedes the first one before anything was delivered
        QCOMPARE(scanner.generation(), firstGeneration + 1);

        QTRY_COMPARE(worlds.count(), 1);
        QTRY_VERIFY(!scanner.isScanning());
        QTest::qWait(200);  // a late result of the first generation would show up now
        QCOMPARE(worlds.count(), 1);
        QCOMPARE(scanner.entries().size(), 1);
        QCOMPARE(scanner.entries().first().key(), QStringLiteral("second/from-second-scan"));
        QCOMPARE(sizes.count(), 1);
        QCOMPARE(sizes.first().at(0).toString(), QStringLiteral("second/from-second-scan"));
    }

    void destroyingTheScannerWhileJobsRun()
    {
        QTemporaryDir root;
        for (int i = 0; i < 20; ++i)
            makeWorld(root.filePath("a/saves"), QStringLiteral("w%1").arg(i), Level{});
        const QList<InstanceSource> sources = { { "a", "A", root.filePath("a/saves") } };
        auto* scanner = new WorldScanner([sources]() { return sources; });
        QSignalSpy worlds(scanner, &WorldScanner::worldsChanged);
        scanner->rescan();
        QTRY_COMPARE(worlds.count(), 1);  // size jobs are queued now
        delete scanner;                    // pool threads only hold a QPointer: nothing may touch the dead object
        QTest::qWait(300);
    }
};

QTEST_GUILESS_MAIN(WorldScannerTest)

#include "WorldScanner_test.moc"
