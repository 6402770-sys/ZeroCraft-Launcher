// SPDX-License-Identifier: GPL-3.0-only
// ScreenshotScanner (Screenshot Gallery): scanScreenshotsDir on real folders, sorting / search, the async scanner and its generations.
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "zero/gallery/ScreenshotScanner.h"

using namespace Zero::Gallery;

namespace {
void touch(const QString& path, qint64 modifiedMs = 0, const QByteArray& data = QByteArray(8, 'x'))
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
    f.write(data);
    f.close();
    if (modifiedMs > 0) {
        QFile g(path);
        QVERIFY(g.open(QIODevice::ReadWrite));
        QVERIFY(g.setFileTime(QDateTime::fromMSecsSinceEpoch(modifiedMs), QFileDevice::FileModificationTime));
    }
}

ScreenshotEntry entry(const QString& instance, const QString& file, qint64 modified)
{
    ScreenshotEntry e;
    e.instanceId = instance.toLower();
    e.instanceName = instance;
    e.fileName = file;
    e.path = QStringLiteral("/x/") + instance + QLatin1Char('/') + file;
    e.modifiedMs = modified;
    return e;
}
}  // namespace

class ScreenshotScannerTest : public QObject {
    Q_OBJECT
   private slots:
    void scanFiltersExtensionsAndKinds()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString dir = tmp.path() + QStringLiteral("/screenshots");
        touch(dir + QStringLiteral("/a.png"), 1700000000000);
        touch(dir + QStringLiteral("/b.JPG"));
        touch(dir + QStringLiteral("/c.jpeg"));
        touch(dir + QStringLiteral("/d.PNG"));
        touch(dir + QStringLiteral("/notes.txt"));
        touch(dir + QStringLiteral("/gif.gif"));
        touch(dir + QStringLiteral("/noext"));
        touch(dir + QStringLiteral("/sub/inner.png"));  // not directly in the folder
        QDir().mkpath(dir + QStringLiteral("/folder.png"));  // a folder with an image name

        const auto list = scanScreenshotsDir(QStringLiteral("id"), QStringLiteral("Name"), dir);
        QStringList names;
        for (const auto& e : list)
            names << e.fileName;
        QCOMPARE(names, (QStringList{ "a.png", "b.JPG", "c.jpeg", "d.PNG" }));
        QCOMPARE(list.first().instanceId, QStringLiteral("id"));
        QCOMPARE(list.first().instanceName, QStringLiteral("Name"));
        QCOMPARE(list.first().modifiedMs, qint64(1700000000000));
        QCOMPARE(list.first().sizeBytes, qint64(8));
        QVERIFY(QFileInfo(list.first().path).isAbsolute());
    }

    void scanMissingDirIsEmpty()
    {
        QVERIFY(scanScreenshotsDir(QStringLiteral("i"), QStringLiteral("n"), QStringLiteral("Z:/definitely/not/here")).isEmpty());
        QVERIFY(scanScreenshotsDir(QStringLiteral("i"), QStringLiteral("n"), QString()).isEmpty());
    }

    void scanSkipsSymlinks()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString dir = tmp.path() + QStringLiteral("/s");
        touch(dir + QStringLiteral("/real.png"));
        if (!QFile::link(dir + QStringLiteral("/real.png"), dir + QStringLiteral("/link.png")))
            QSKIP("cannot create a link here");
        const auto list = scanScreenshotsDir(QStringLiteral("i"), QStringLiteral("n"), dir);
        // .lnk files are shortcuts on Windows (not symlinks): they have another suffix and are not listed either way
        QStringList names;
        for (const auto& e : list)
            names << e.fileName;
        QVERIFY(names.contains(QStringLiteral("real.png")));
        QVERIFY(!names.contains(QStringLiteral("link.png.lnk")));
    }

    void scanDoesNotModifyFolder()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString dir = tmp.path() + QStringLiteral("/s");
        touch(dir + QStringLiteral("/a.png"));
        const QStringList before = QDir(dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
        scanScreenshotsDir(QStringLiteral("i"), QStringLiteral("n"), dir);
        QCOMPARE(QDir(dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot), before);
    }

    void sortModes()
    {
        QList<ScreenshotEntry> list = { entry("Beta", "b1.png", 200), entry("Alpha", "a1.png", 100), entry("Beta", "b2.png", 300),
                                        entry("Alpha", "a2.png", 400) };
        auto names = [&]() {
            QStringList out;
            for (const auto& e : list)
                out << e.fileName;
            return out;
        };
        sortScreenshots(list, SortMode::Newest);
        QCOMPARE(names(), (QStringList{ "a2.png", "b2.png", "b1.png", "a1.png" }));
        sortScreenshots(list, SortMode::Oldest);
        QCOMPARE(names(), (QStringList{ "a1.png", "b1.png", "b2.png", "a2.png" }));
        sortScreenshots(list, SortMode::Instance);
        QCOMPARE(names(), (QStringList{ "a2.png", "a1.png", "b2.png", "b1.png" }));
    }

    void sortTiesFallBackToFileName()
    {
        QList<ScreenshotEntry> list = { entry("A", "z.png", 5), entry("A", "m.png", 5), entry("A", "a.png", 5) };
        sortScreenshots(list, SortMode::Newest);
        QCOMPARE(list.at(0).fileName, QStringLiteral("a.png"));
        QCOMPARE(list.at(2).fileName, QStringLiteral("z.png"));
    }

    void sortModeStrings()
    {
        for (SortMode m : { SortMode::Newest, SortMode::Oldest, SortMode::Instance })
            QCOMPARE(sortModeFromString(sortModeToString(m)), m);
        QCOMPARE(sortModeFromString(QStringLiteral("nonsense")), SortMode::Newest);
        QCOMPARE(sortModeFromString(QString()), SortMode::Newest);
    }

    void search()
    {
        const auto e = entry("Survival 1.20", "2024-01-02_12.00.00.png", 1);
        QVERIFY(matchesSearch(e, QString()));
        QVERIFY(matchesSearch(e, QStringLiteral("   ")));
        QVERIFY(matchesSearch(e, QStringLiteral("SURVIVAL")));
        QVERIFY(matchesSearch(e, QStringLiteral("2024-01")));
        QVERIFY(!matchesSearch(e, QStringLiteral("creative")));
    }

    void asyncScanAndGenerations()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        touch(tmp.path() + QStringLiteral("/one/screenshots/a.png"));
        touch(tmp.path() + QStringLiteral("/two/screenshots/b.png"));
        touch(tmp.path() + QStringLiteral("/two/screenshots/c.jpg"));
        const QString root = tmp.path();

        ScreenshotScanner scanner([root]() {
            return QList<InstanceSource>{ { "one", "One", root + "/one/screenshots" },
                                          { "two", "Two", root + "/two/screenshots" },
                                          { "none", "None", root + "/none/screenshots" } };
        });
        QVERIFY(!scanner.hasScanned());
        QSignalSpy changed(&scanner, &ScreenshotScanner::listChanged);
        scanner.rescan();
        QVERIFY(scanner.isScanning());
        QVERIFY(QTest::qWaitFor([&]() { return scanner.hasScanned() && !scanner.isScanning(); }, 5000));
        QCOMPARE(scanner.entries().size(), 3);
        QCOMPARE(changed.count(), 1);

        // a rescan right after another: only the newest generation delivers
        changed.clear();
        scanner.rescan();
        scanner.rescan();
        QVERIFY(QTest::qWaitFor([&]() { return !scanner.isScanning(); }, 5000));
        QTest::qWait(100);
        QCOMPARE(changed.count(), 1);

        // a file added later shows up on the next scan
        touch(tmp.path() + QStringLiteral("/one/screenshots/new.png"));
        scanner.rescan();
        QVERIFY(QTest::qWaitFor([&]() { return !scanner.isScanning(); }, 5000));
        QCOMPARE(scanner.entries().size(), 4);
    }
};

QTEST_GUILESS_MAIN(ScreenshotScannerTest)

#include "ScreenshotScanner_test.moc"
