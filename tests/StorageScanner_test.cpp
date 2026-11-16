// SPDX-License-Identifier: GPL-3.0-only
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <tuple>

#include "zero/storage/StorageScanner.h"

using namespace Zero::Storage;

namespace {

constexpr qint64 kDayMs = 24LL * 60 * 60 * 1000;

void writeBytes(const QString& path, int bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
    f.write(QByteArray(bytes, 'x'));
}

/// Writes `bytes` bytes and sets the modification time to `ageDays` before `nowMs`.
void writeAged(const QString& path, int bytes, double ageDays, qint64 nowMs)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
    f.write(QByteArray(bytes, 'x'));
    const qint64 ms = nowMs - static_cast<qint64>(ageDays * kDayMs);
    QVERIFY(f.setFileTime(QDateTime::fromMSecsSinceEpoch(ms), QFileDevice::FileModificationTime));
}

bool makeJunction(const QString& link, const QString& target)
{
#ifdef Q_OS_WIN
    QProcess mklink;
    mklink.start("cmd", { "/c", "mklink", "/J", QDir::toNativeSeparators(link), QDir::toNativeSeparators(target) });
    return mklink.waitForFinished(10000) && mklink.exitCode() == 0;
#else
    Q_UNUSED(link);
    Q_UNUSED(target);
    return false;
#endif
}

/// Instance layout used by most tests. Sizes: worlds 5000, mods 3000, packs 300, screenshots 700, logs 70, other 180.
InstanceSource makeInstanceA(const QString& base)
{
    const QString root = base + "/instances/a";
    const QString game = root + "/.minecraft";
    writeBytes(game + "/saves/w1/level.dat", 1000);
    writeBytes(game + "/saves/w1/region/r.0.0.mca", 4000);
    writeBytes(game + "/mods/m1.jar", 2000);
    writeBytes(game + "/mods/m2.jar", 1000);
    writeBytes(game + "/resourcepacks/rp.zip", 200);
    writeBytes(game + "/shaderpacks/sp.zip", 100);  // texturepacks does not exist
    writeBytes(game + "/screenshots/s1.png", 700);
    writeBytes(game + "/logs/latest.log", 10);
    writeBytes(game + "/logs/old.log", 20);
    writeBytes(game + "/crash-reports/crash.txt", 40);
    writeBytes(root + "/instance.cfg", 30);
    writeBytes(game + "/options.txt", 60);
    writeBytes(game + "/config/x.cfg", 90);
    InstanceSource s;
    s.id = "a";
    s.name = "Alpha";
    s.instanceRoot = root;
    s.gameRoot = game;
    s.worldsDir = game + "/saves";
    s.modsDir = game + "/mods";
    s.packDirs = { game + "/resourcepacks", game + "/shaderpacks", game + "/texturepacks" };
    return s;
}

/// Small instance: worlds 1000, no mods folder, other 25.
InstanceSource makeInstanceB(const QString& base)
{
    const QString root = base + "/instances/b";
    writeBytes(root + "/minecraft/saves/world/level.dat", 1000);
    writeBytes(root + "/instance.cfg", 25);
    InstanceSource s;
    s.id = "b";
    s.name = "Bravo";
    s.instanceRoot = root;
    s.gameRoot = root + "/minecraft";
    s.worldsDir = root + "/minecraft/saves";
    s.modsDir = root + "/minecraft/mods";
    return s;
}

/// Launcher side: backups 500, libraries 100 + assets 200, java 300, cache 40 + meta 50 + metacache 7, other 11 + 13 + 17 + 19 = 60.
LauncherSource makeLauncher(const QString& base)
{
    writeBytes(base + "/zero/backups/a/w1/b.zip", 500);
    writeBytes(base + "/libraries/lib.jar", 100);
    writeBytes(base + "/assets/objects/ab", 200);
    writeBytes(base + "/java/jre/bin/java.exe", 300);
    writeBytes(base + "/cache/c.bin", 40);
    writeBytes(base + "/meta/index.json", 50);
    writeBytes(base + "/metacache", 7);
    writeBytes(base + "/zero/history.json", 11);
    writeBytes(base + "/logs/launcher.log", 13);
    writeBytes(base + "/icons/i.png", 17);
    writeBytes(base + "/prismlauncher.cfg", 19);
    writeBytes(base + "/instances/instgroups.json", 5);  // stray file of the instances dir: not counted anywhere
    return { base, base + "/instances" };
}

}  // namespace

class StorageScannerTest : public QObject {
    Q_OBJECT
   private slots:
    void categoryIds()
    {
        QSet<QString> ids;
        for (Category c : allCategories())
            ids.insert(categoryId(c));
        QCOMPARE(ids.size(), kCategoryCount);
        QCOMPARE(categoryId(Category::OtherInstance), QStringLiteral("other-instance"));
        QCOMPARE(categoryId(Category::OtherLauncher), QStringLiteral("other-launcher"));
    }

    void instanceBreakdown()
    {
        QTemporaryDir tmp;
        const InstanceRow row = measureInstance(makeInstanceA(tmp.path()));
        QCOMPARE(row.bytes.of(Category::Worlds), qint64(5000));
        QCOMPARE(row.bytes.of(Category::Mods), qint64(3000));
        QCOMPARE(row.bytes.of(Category::Packs), qint64(300));  // the missing texturepacks dir counts 0
        QCOMPARE(row.bytes.of(Category::Screenshots), qint64(700));
        QCOMPARE(row.bytes.of(Category::Logs), qint64(70));  // logs 30 + crash-reports 40
        QCOMPARE(row.bytes.of(Category::OtherInstance), qint64(180));
        QCOMPARE(row.bytes.of(Category::Backups), qint64(0));
        QCOMPARE(row.total, qint64(9250));
        QCOMPARE(row.id, QStringLiteral("a"));
        QCOMPARE(row.name, QStringLiteral("Alpha"));
    }

    void missingDirsCountZero()
    {
        QTemporaryDir tmp;
        const InstanceRow row = measureInstance(makeInstanceB(tmp.path()));
        QCOMPARE(row.bytes.of(Category::Worlds), qint64(1000));
        QCOMPARE(row.bytes.of(Category::Mods), qint64(0));
        QCOMPARE(row.bytes.of(Category::OtherInstance), qint64(25));
        QCOMPARE(row.total, qint64(1025));

        InstanceSource empty;  // nothing configured at all: no crash, no cwd measured
        empty.id = "e";
        const InstanceRow er = measureInstance(empty);
        QCOMPARE(er.total, qint64(0));
    }

    void samePathCountedOnce()
    {
        QTemporaryDir tmp;
        InstanceSource s = makeInstanceA(tmp.path());
        s.packDirs = { s.gameRoot + "/resourcepacks", s.gameRoot + "/resourcepacks", s.gameRoot + "/resourcepacks/../resourcepacks/" };
#ifdef Q_OS_WIN
        s.packDirs << s.gameRoot + "/RESOURCEPACKS";  // Windows paths compare case-insensitively
#endif
        s.worldsDir = s.gameRoot + "/saves";
        s.modsDir = s.worldsDir;  // the same dir as a different category: counted once, in the first (Worlds)
        const InstanceRow row = measureInstance(s);
        QCOMPARE(row.bytes.of(Category::Packs), qint64(200));
        QCOMPARE(row.bytes.of(Category::Worlds), qint64(5000));
        QCOMPARE(row.bytes.of(Category::Mods), qint64(0));
        // 9250 - mods 3000 - shaderpacks 100 (no longer configured) is still "other": every byte appears once
        QCOMPARE(row.total, qint64(5000 + 200 + 700 + 70 + (180 + 3000 + 100)));
    }

    void nestedDirectoryIsSubtractedFromItsParent()
    {
        QTemporaryDir tmp;
        InstanceSource s = makeInstanceA(tmp.path());
        writeBytes(s.gameRoot + "/mods/shaders/inner.zip", 50);  // a pack dir inside the mods dir
        s.packDirs = { s.gameRoot + "/mods/shaders" };
        const InstanceRow row = measureInstance(s);
        QCOMPARE(row.bytes.of(Category::Mods), qint64(3000));
        QCOMPARE(row.bytes.of(Category::Packs), qint64(50));
        // resourcepacks / shaderpacks are not configured now, so their 300 bytes are "other"
        QCOMPARE(row.bytes.of(Category::OtherInstance), qint64(180 + 300));
        QCOMPARE(row.total, qint64(9250 + 50));
    }

    void dirOutsideTheInstanceRootIsNotSubtracted()
    {
        QTemporaryDir tmp;
        InstanceSource s = makeInstanceA(tmp.path());
        writeBytes(tmp.path() + "/shared-saves/w/level.dat", 777);
        s.worldsDir = tmp.path() + "/shared-saves";
        const InstanceRow row = measureInstance(s);
        QCOMPARE(row.bytes.of(Category::Worlds), qint64(777));
        // the instance's own saves folder is no longer a configured dir, so it stays in "other"
        QCOMPARE(row.bytes.of(Category::OtherInstance), qint64(180 + 5000));
    }

    void junctionInsideInstanceIsNotCounted()
    {
        QTemporaryDir tmp;
        InstanceSource s = makeInstanceA(tmp.path());
        writeBytes(tmp.path() + "/elsewhere/big.bin", 9999);
        if (!makeJunction(s.instanceRoot + "/linked", tmp.path() + "/elsewhere"))
            QSKIP("Cannot create a junction here");
        const InstanceRow row = measureInstance(s);
        QCOMPARE(row.bytes.of(Category::OtherInstance), qint64(180));
        QCOMPARE(row.total, qint64(9250));
    }

    void launcherCategories()
    {
        QTemporaryDir tmp;
        const LauncherSource l = makeLauncher(tmp.path());
        const CategoryBytes b = measureLauncher(l);
        QCOMPARE(b.of(Category::Backups), qint64(500));
        QCOMPARE(b.of(Category::Libraries), qint64(300));
        QCOMPARE(b.of(Category::Java), qint64(300));
        QCOMPARE(b.of(Category::Cache), qint64(97));  // the metacache FILE counts too
        // zero/history.json + logs + icons + config; the rest of zero/ is included, backups and the instances dir are not
        QCOMPARE(b.of(Category::OtherLauncher), qint64(60));
        QCOMPARE(b.of(Category::Worlds), qint64(0));
        QCOMPARE(b.of(Category::OtherInstance), qint64(0));
    }

    void launcherWithInstancesDirOutsideDataRoot()
    {
        QTemporaryDir tmp;
        QTemporaryDir elsewhere;
        LauncherSource l = makeLauncher(tmp.path());
        writeBytes(elsewhere.path() + "/instances/zzz/foo.bin", 123456);
        l.instancesDir = elsewhere.path() + "/instances";
        const CategoryBytes b = measureLauncher(l);
        // the in-root "instances" folder is no longer THE instances dir, so its 5 stray bytes are plain launcher data now
        QCOMPARE(b.of(Category::OtherLauncher), qint64(65));
        QCOMPARE(b.of(Category::Backups), qint64(500));
    }

    void emptyLauncherSource()
    {
        const CategoryBytes b = measureLauncher({});
        QCOMPARE(b.sum(), qint64(0));
    }

    void reportTotalsAndSort()
    {
        QTemporaryDir tmp;
        const LauncherSource l = makeLauncher(tmp.path());
        InstanceSource b = makeInstanceB(tmp.path());
        InstanceSource a = makeInstanceA(tmp.path());
        // B listed first on purpose: the report must sort by total, biggest first
        const StorageReport r = buildReport({ b, a }, l, QDateTime::currentMSecsSinceEpoch());
        QCOMPARE(r.instances.size(), 2);
        QCOMPARE(r.instances.at(0).id, QStringLiteral("a"));
        QCOMPARE(r.instances.at(1).id, QStringLiteral("b"));
        QCOMPARE(r.totals.of(Category::Worlds), qint64(6000));
        QCOMPARE(r.totals.of(Category::OtherInstance), qint64(205));
        QCOMPARE(r.totals.of(Category::Backups), qint64(500));
        QCOMPARE(r.totals.of(Category::OtherLauncher), qint64(60));
        QCOMPARE(r.totalBytes, qint64(9250 + 1025 + 500 + 300 + 300 + 97 + 60));
        QCOMPARE(r.totalBytes, r.totals.sum());
        QCOMPARE(r.dataRoot, tmp.path());
        QVERIFY(r.volumeTotalBytes > 0);
        QVERIFY(r.volumeFreeBytes >= 0);
        QVERIFY(r.volumeFreeBytes <= r.volumeTotalBytes);
        QCOMPARE(r.cleanupGroups.size(), 2);
        QVERIFY(r.group(CleanupKind::OldLogs));
        QVERIFY(r.group(CleanupKind::OldCrashReports));
    }

    void sortTieFallsBackToName()
    {
        QTemporaryDir tmp;
        InstanceSource x = makeInstanceB(tmp.path());
        InstanceSource y = x;
        y.id = "b2";
        y.name = "Aardvark";
        const StorageReport r = buildReport({ x, y }, {}, 0);  // same folder twice: equal totals
        QCOMPARE(r.instances.at(0).name, QStringLiteral("Aardvark"));
    }

    // ---- cleanup detection ----------------------------------------------------------------------------------------------

    void cleanupDetection()
    {
        QTemporaryDir tmp;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        InstanceSource s = makeInstanceB(tmp.path());
        const QString logs = s.gameRoot + "/logs";
        const QString crash = s.gameRoot + "/crash-reports";
        writeAged(logs + "/old31.log", 111, 31, now);
        writeAged(logs + "/new29.log", 50, 29, now);
        writeAged(logs + "/latest.log", 50, 90, now);
        writeAged(logs + "/LATEST.LOG", 50, 90, now);
        writeAged(logs + "/debug.log", 50, 90, now);
        writeAged(logs + "/2020-01-01-1.log.gz", 222, 45, now);
        writeAged(logs + "/sub/inner.log", 50, 90, now);  // subfolders are ignored
        writeAged(logs + "/notes.txt", 50, 90, now);      // not a log name
        writeAged(logs + "/keep.dat", 50, 90, now);
        writeAged(crash + "/crash-old.txt", 333, 31, now);
        writeAged(crash + "/crash-new.txt", 50, 29, now);
        writeAged(crash + "/crash.log", 50, 90, now);  // not .txt
        writeAged(crash + "/dump.dmp", 50, 90, now);

        const QList<CleanupFile> logsFound = findCleanupFiles(s, CleanupKind::OldLogs, now);
        QSet<QString> names;
        for (const CleanupFile& f : logsFound) {
            names.insert(QFileInfo(f.path).fileName());
            QCOMPARE(f.instanceId, QStringLiteral("b"));
            QCOMPARE(f.kind, CleanupKind::OldLogs);
            QVERIFY(f.modifiedMs < now - 30 * kDayMs);
        }
        QCOMPARE(names, (QSet<QString>{ "old31.log", "2020-01-01-1.log.gz" }));
        const QList<CleanupFile> crashFound = findCleanupFiles(s, CleanupKind::OldCrashReports, now);
        QCOMPARE(crashFound.size(), 1);
        QCOMPARE(QFileInfo(crashFound.first().path).fileName(), QStringLiteral("crash-old.txt"));
        QCOMPARE(crashFound.first().sizeBytes, qint64(333));

        // through the report: groups, totals, biggest first
        const StorageReport r = buildReport({ s }, {}, now);
        const CleanupGroup* g = r.group(CleanupKind::OldLogs);
        QVERIFY(g);
        QCOMPARE(g->files.size(), 2);
        QCOMPARE(g->totalBytes, qint64(333));
        QCOMPARE(g->files.at(0).sizeBytes, qint64(222));
        QCOMPARE(r.group(CleanupKind::OldCrashReports)->totalBytes, qint64(333));
    }

    void ageBoundary()
    {
        const qint64 now = 1'000'000'000'000LL;
        QVERIFY(isOlderThanCleanupAge(now - 30 * kDayMs - 1, now));
        QVERIFY(!isOlderThanCleanupAge(now - 30 * kDayMs, now));
        QVERIFY(!isOlderThanCleanupAge(now, now));
        QVERIFY(!isOlderThanCleanupAge(now + kDayMs, now));
        QCOMPARE(kCleanupAgeDays, 30);
    }

    void runningInstanceYieldsNothing()
    {
        QTemporaryDir tmp;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        InstanceSource s = makeInstanceB(tmp.path());
        writeAged(s.gameRoot + "/logs/old.log", 10, 60, now);
        writeAged(s.gameRoot + "/crash-reports/c.txt", 10, 60, now);
        s.running = true;
        QVERIFY(findCleanupFiles(s, CleanupKind::OldLogs, now).isEmpty());
        QVERIFY(findCleanupFiles(s, CleanupKind::OldCrashReports, now).isEmpty());
        const StorageReport r = buildReport({ s }, {}, now);
        QCOMPARE(r.group(CleanupKind::OldLogs)->files.size(), 0);
        QCOMPARE(r.group(CleanupKind::OldCrashReports)->files.size(), 0);
        // it is still measured
        QCOMPARE(r.instances.first().bytes.of(Category::Logs), qint64(20));
        s.running = false;
        QCOMPARE(findCleanupFiles(s, CleanupKind::OldLogs, now).size(), 1);
    }

    void linkedLogsFolderIsNotScanned()
    {
        QTemporaryDir tmp;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        InstanceSource s = makeInstanceB(tmp.path());
        writeAged(tmp.path() + "/outside/old.log", 10, 60, now);
        QDir().mkpath(s.gameRoot);
        if (!makeJunction(s.gameRoot + "/logs", tmp.path() + "/outside"))
            QSKIP("Cannot create a junction here");
        QVERIFY(findCleanupFiles(s, CleanupKind::OldLogs, now).isEmpty());
        CleanupFile f;
        f.kind = CleanupKind::OldLogs;
        f.path = s.gameRoot + "/logs/old.log";
        int calls = 0;
        TrashHooks hooks;
        hooks.trashPath = [&calls](const QString&) {
            ++calls;
            return true;
        };
        const TrashResult res = trashCandidates({ f }, { s }, now, hooks);
        QCOMPARE(res.trashedCount, 0);
        QCOMPARE(res.skippedCount, 1);
        QCOMPARE(calls, 0);
        QVERIFY(QFileInfo::exists(tmp.path() + "/outside/old.log"));
    }

    void restoreLeftoversAreReportedButNeverCleanup()
    {
        QTemporaryDir tmp;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        InstanceSource s = makeInstanceB(tmp.path());
        writeAged(s.worldsDir + "/.zero-restore-world-20260101/level.dat", 400, 90, now);
        writeAged(s.worldsDir + "/.zero-replaced-world-20260101/region/r.mca", 600, 90, now);
        writeBytes(s.worldsDir + "/.zero-other/x", 5);  // not a ZERO restore folder
        const QList<RestoreLeftover> l = findRestoreLeftovers(s);
        QCOMPARE(l.size(), 2);
        qint64 sum = 0;
        for (const RestoreLeftover& x : l) {
            sum += x.sizeBytes;
            QCOMPARE(x.instanceId, QStringLiteral("b"));
            QCOMPARE(x.instanceName, QStringLiteral("Bravo"));
            QVERIFY(QFileInfo(x.path).isDir());
        }
        QCOMPARE(sum, qint64(1000));

        const StorageReport r = buildReport({ s }, {}, now);
        QCOMPARE(r.leftovers.size(), 2);
        QCOMPARE(r.group(CleanupKind::OldLogs)->files.size(), 0);
        QCOMPARE(r.group(CleanupKind::OldCrashReports)->files.size(), 0);

        // and trashCandidates refuses them even when asked
        CleanupFile f;
        f.kind = CleanupKind::OldLogs;
        f.path = l.first().path;
        int calls = 0;
        TrashHooks hooks;
        hooks.trashPath = [&calls](const QString&) {
            ++calls;
            return true;
        };
        const TrashResult res = trashCandidates({ f }, { s }, now, hooks);
        QCOMPARE(calls, 0);
        QCOMPARE(res.skippedCount, 1);
        QVERIFY(QFileInfo::exists(l.first().path));
    }

    // ---- trash ------------------------------------------------------------------------------------------------------------

    void trashValidFiles()
    {
        QTemporaryDir tmp;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        InstanceSource s = makeInstanceB(tmp.path());
        writeAged(s.gameRoot + "/logs/old.log", 100, 40, now);
        writeAged(s.gameRoot + "/logs/old2.log.gz", 200, 40, now);
        writeAged(s.gameRoot + "/crash-reports/c.txt", 300, 40, now);
        QList<CleanupFile> all = findCleanupFiles(s, CleanupKind::OldLogs, now);
        all += findCleanupFiles(s, CleanupKind::OldCrashReports, now);
        QCOMPARE(all.size(), 3);

        QStringList moved;
        TrashHooks hooks;
        hooks.trashPath = [&moved](const QString& p) {
            moved << p;
            return QFile::remove(p);  // simulates the Recycle Bin: the file leaves its folder
        };
        const TrashResult res = trashCandidates(all, { s }, now, hooks);
        QCOMPARE(res.trashedCount, 3);
        QCOMPARE(res.trashedBytes, qint64(600));
        QCOMPARE(res.skippedCount, 0);
        QCOMPARE(res.failedCount, 0);
        QVERIFY(res.problems.isEmpty());
        QVERIFY(res.error.isEmpty());
        QCOMPARE(moved.size(), 3);
        for (const CleanupFile& f : all)
            QVERIFY(!QFileInfo::exists(f.path));
    }

    void trashRefusesWhatIsNoLongerValid()
    {
        QTemporaryDir tmp;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        InstanceSource s = makeInstanceB(tmp.path());
        const QString logs = s.gameRoot + "/logs";
        writeAged(logs + "/old.log", 10, 40, now);
        writeAged(logs + "/became-new.log", 10, 40, now);
        writeAged(logs + "/renamed.log", 10, 40, now);
        writeAged(logs + "/sub/nested.log", 10, 40, now);
        writeAged(tmp.path() + "/outside/other.log", 10, 40, now);
        writeAged(s.gameRoot + "/mods/m.log", 10, 40, now);  // right name, wrong folder
        writeAged(s.worldsDir + "/w/level.dat", 10, 40, now);
        writeAged(s.gameRoot + "/crash-reports/in-crash.log", 10, 40, now);  // OldLogs file in the crash folder
        writeAged(logs + "/latest.log", 10, 90, now);

        auto file = [](const QString& path, CleanupKind kind = CleanupKind::OldLogs) {
            CleanupFile f;
            f.kind = kind;
            f.path = path;
            f.sizeBytes = 10;
            f.instanceId = "b";
            return f;
        };
        // state changes after detection
        QFile touch(logs + "/became-new.log");
        QVERIFY(touch.open(QIODevice::ReadWrite));
        QVERIFY(touch.setFileTime(QDateTime::fromMSecsSinceEpoch(now - 29 * kDayMs), QFileDevice::FileModificationTime));
        touch.close();

        QStringList moved;
        TrashHooks hooks;
        hooks.trashPath = [&moved](const QString& p) {
            moved << p;
            return QFile::remove(p);
        };
        const QList<CleanupFile> req = {
            file(logs + "/old.log"),                    // valid
            file(logs + "/became-new.log"),             // newer than 30 days now
            file(logs + "/gone.log"),                   // no longer exists
            file(logs + "/latest.log"),                 // protected name
            file(logs + "/sub/nested.log"),             // not directly inside logs
            file(logs + "/../mods/m.log"),              // path trick
            file(tmp.path() + "/outside/other.log"),    // outside any known logs dir
            file(s.gameRoot + "/mods/m.log"),           // wrong folder
            file(s.worldsDir + "/w/level.dat"),         // a world file
            file(s.gameRoot + "/crash-reports/in-crash.log"),  // kind does not match the folder
            file(logs),                                 // a directory
            file(QString()),                            // empty
            file(logs + "/old.log", CleanupKind::OldCrashReports),  // right file, wrong kind for the folder
        };
        const TrashResult res = trashCandidates(req, { s }, now, hooks);
        QCOMPARE(moved, QStringList{ QFileInfo(logs + "/old.log").absoluteFilePath() });
        QCOMPARE(res.trashedCount, 1);
        QCOMPARE(res.skippedCount, int(req.size()) - 1);
        QCOMPARE(res.failedCount, 0);
        QCOMPARE(res.problems.size(), int(req.size()) - 1);
        for (const TrashProblem& p : res.problems) {
            QVERIFY(p.skipped);
            QVERIFY(!p.error.isEmpty());
        }
        // nothing else was touched
        QVERIFY(QFileInfo::exists(logs + "/became-new.log"));
        QVERIFY(QFileInfo::exists(logs + "/renamed.log"));
        QVERIFY(QFileInfo::exists(logs + "/sub/nested.log"));
        QVERIFY(QFileInfo::exists(tmp.path() + "/outside/other.log"));
        QVERIFY(QFileInfo::exists(s.gameRoot + "/mods/m.log"));
        QVERIFY(QFileInfo::exists(s.worldsDir + "/w/level.dat"));
        QVERIFY(QFileInfo::exists(logs + "/latest.log"));
        QVERIFY(QFileInfo::exists(s.gameRoot + "/crash-reports/in-crash.log"));
    }

    void trashRefusesUnknownInstanceAndRunningInstance()
    {
        QTemporaryDir tmp;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        InstanceSource s = makeInstanceB(tmp.path());
        writeAged(s.gameRoot + "/logs/old.log", 10, 40, now);
        CleanupFile f;
        f.kind = CleanupKind::OldLogs;
        f.path = s.gameRoot + "/logs/old.log";
        f.instanceId = "b";
        int calls = 0;
        TrashHooks hooks;
        hooks.trashPath = [&calls](const QString&) {
            ++calls;
            return true;
        };

        TrashResult res = trashCandidates({ f }, {}, now, hooks);  // no known instances
        QCOMPARE(res.skippedCount, 1);
        QCOMPARE(calls, 0);

        s.running = true;  // it started after the candidates were found
        res = trashCandidates({ f }, { s }, now, hooks);
        QCOMPARE(res.skippedCount, 1);
        QCOMPARE(calls, 0);
        QVERIFY(QFileInfo::exists(f.path));

        // a second, non-running source sharing the folder does not make it safe: any running owner refuses
        InstanceSource twin = s;
        twin.id = "twin";
        twin.running = false;
        res = trashCandidates({ f }, { twin, s }, now, hooks);
        QCOMPARE(res.skippedCount, 1);
        QCOMPARE(calls, 0);

        s.running = false;
        res = trashCandidates({ f }, { s }, now, hooks);
        QCOMPARE(res.trashedCount, 1);
        QCOMPARE(calls, 1);
    }

    void failedTrashLeavesTheFileAndNeverDeletes()
    {
        QTemporaryDir tmp;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        InstanceSource s = makeInstanceB(tmp.path());
        writeAged(s.gameRoot + "/logs/a.log", 10, 40, now);
        writeAged(s.gameRoot + "/logs/b.log", 20, 40, now);
        const QList<CleanupFile> files = findCleanupFiles(s, CleanupKind::OldLogs, now);
        QCOMPARE(files.size(), 2);

        int calls = 0;
        TrashHooks hooks;
        hooks.trashPath = [&calls](const QString&) {
            ++calls;
            return false;  // Recycle Bin unavailable
        };
        const TrashResult res = trashCandidates(files, { s }, now, hooks);
        QCOMPARE(calls, 2);  // one attempt each, no retry through another route
        QCOMPARE(res.trashedCount, 0);
        QCOMPARE(res.trashedBytes, qint64(0));
        QCOMPARE(res.failedCount, 2);
        QCOMPARE(res.skippedCount, 0);
        QCOMPARE(res.problems.size(), 2);
        for (const TrashProblem& p : res.problems) {
            QVERIFY(!p.skipped);
            QVERIFY(!p.error.isEmpty());
            QVERIFY(QFileInfo::exists(p.path));
        }
        for (const CleanupFile& f : files)
            QVERIFY(QFileInfo::exists(f.path));
    }

    void emptyRequestDoesNothing()
    {
        const TrashResult res = trashCandidates({}, {}, 0);
        QCOMPARE(res.trashedCount, 0);
        QVERIFY(res.problems.isEmpty());
    }

    // ---- async ------------------------------------------------------------------------------------------------------------

    void asyncScanDeliversReport()
    {
        QTemporaryDir tmp;
        const LauncherSource l = makeLauncher(tmp.path());
        const InstanceSource a = makeInstanceA(tmp.path());
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        writeAged(a.gameRoot + "/logs/old.log", 30, 60, now);

        int providerCalls = 0;
        StorageScanner scanner([&]() {
            ++providerCalls;
            return QPair<QList<InstanceSource>, LauncherSource>({ a }, l);
        });
        QSignalSpy started(&scanner, &StorageScanner::scanStarted);
        QSignalSpy ready(&scanner, &StorageScanner::reportReady);
        QVERIFY(!scanner.isScanning());
        QVERIFY(!scanner.hasReport());
        scanner.rescan();
        QVERIFY(scanner.isScanning());
        QCOMPARE(started.count(), 1);
        QCOMPARE(providerCalls, 1);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 20000);
        QVERIFY(!scanner.isScanning());
        QVERIFY(scanner.hasReport());
        const StorageReport r = ready.first().first().value<StorageReport>();
        QCOMPARE(r.instances.size(), 1);
        QCOMPARE(r.instances.first().bytes.of(Category::Worlds), qint64(5000));
        QCOMPARE(r.totals.of(Category::Backups), qint64(500));
        QCOMPARE(r.group(CleanupKind::OldLogs)->files.size(), 1);
        QCOMPARE(scanner.report().totalBytes, r.totalBytes);
    }

    void olderRescanIsDropped()
    {
        QTemporaryDir tmp;
        const LauncherSource l = makeLauncher(tmp.path());
        const InstanceSource a = makeInstanceA(tmp.path());
        const InstanceSource b = makeInstanceB(tmp.path());
        int call = 0;
        StorageScanner scanner([&]() {
            ++call;
            return QPair<QList<InstanceSource>, LauncherSource>(call == 1 ? QList<InstanceSource>{ a } : QList<InstanceSource>{ b }, l);
        });
        QSignalSpy ready(&scanner, &StorageScanner::reportReady);
        scanner.rescan();
        scanner.rescan();
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 20000);
        QTest::qWait(300);
        QCOMPARE(ready.count(), 1);  // the first generation was dropped
        QCOMPARE(scanner.report().instances.first().id, QStringLiteral("b"));
    }

    void asyncTrashUsesFreshRunningFlagsAndRefusesSecondJob()
    {
        QTemporaryDir tmp;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        InstanceSource s = makeInstanceB(tmp.path());
        writeAged(s.gameRoot + "/logs/old.log", 10, 40, now);
        writeAged(s.gameRoot + "/logs/old2.log", 10, 40, now);
        const QList<CleanupFile> files = findCleanupFiles(s, CleanupKind::OldLogs, now);
        QCOMPARE(files.size(), 2);

        bool running = true;
        QAtomicInt trashed = 0;
        TrashHooks hooks;
        hooks.trashPath = [&trashed](const QString& p) {
            trashed.ref();
            return QFile::remove(p);
        };
        StorageScanner scanner(
            [&]() {
                InstanceSource copy = s;
                copy.running = running;  // read at request time
                return QPair<QList<InstanceSource>, LauncherSource>({ copy }, LauncherSource{});
            },
            nullptr, hooks);
        QSignalSpy finished(&scanner, &StorageScanner::trashFinished);

        scanner.trash(files);  // the instance is "running" now: every file must be refused
        QVERIFY(scanner.isTrashing());
        scanner.trash(files);  // a second request while the first runs
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 20000);
        QVERIFY(!scanner.isTrashing());
        int refusedBusy = 0;
        int firstJob = 0;
        for (const QList<QVariant>& args : finished) {
            const TrashResult r = args.first().value<TrashResult>();
            if (!r.error.isEmpty())
                ++refusedBusy;
            else {
                ++firstJob;
                QCOMPARE(r.trashedCount, 0);
                QCOMPARE(r.skippedCount, 2);
            }
        }
        QCOMPARE(refusedBusy, 1);
        QCOMPARE(firstJob, 1);
        QCOMPARE(int(trashed), 0);

        running = false;
        finished.clear();
        scanner.trash(files);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 20000);
        const TrashResult r = finished.first().first().value<TrashResult>();
        QVERIFY(r.error.isEmpty());
        QCOMPARE(r.trashedCount, 2);
        QCOMPARE(int(trashed), 2);
    }

    void destroyingTheScannerWhileJobsRun()
    {
        QTemporaryDir tmp;
        const LauncherSource l = makeLauncher(tmp.path());
        const InstanceSource a = makeInstanceA(tmp.path());
        auto* scanner = new StorageScanner([&]() { return QPair<QList<InstanceSource>, LauncherSource>({ a }, l); });
        scanner->rescan();
        delete scanner;  // the pool job only holds a QPointer: nothing may touch the dead object
        QTest::qWait(300);
    }

    // ---- Largest items -------------------------------------------------------------------------------------------------------

    void largestItemsAggregateTheInstanceTree()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const InstanceSource a = makeInstanceA(tmp.path());
        writeBytes(a.gameRoot + "/saves/w2/level.dat", 300);    // a second, smaller world
        writeBytes(a.gameRoot + "/config/deep/huge.bin", 9000);  // a big file inside a top-level folder, above the threshold below
        writeBytes(a.instanceRoot + "/.zero/pack-order.json", 120);
        const QList<LargeItem> items = findLargeItems(a, 100, 8000);

        auto find = [&items](ItemKind kind, const QString& name) -> const LargeItem* {
            for (const LargeItem& i : items)
                if (i.kind == kind && i.name == name)
                    return &i;
            return nullptr;
        };
        const LargeItem* world = find(ItemKind::World, "w1");
        QVERIFY(world);
        QCOMPARE(world->sizeBytes, qint64(5000));
        QCOMPARE(world->instanceId, QStringLiteral("a"));
        QVERIFY(world->isDir);
        QVERIFY(find(ItemKind::World, "w2"));
        QCOMPARE(find(ItemKind::Mod, "m1.jar")->sizeBytes, qint64(2000));
        QVERIFY(find(ItemKind::Mod, "m2.jar"));
        QVERIFY(find(ItemKind::Pack, "rp.zip"));
        QVERIFY(find(ItemKind::Pack, "sp.zip"));
        QCOMPARE(find(ItemKind::Screenshots, "screenshots")->sizeBytes, qint64(700));
        QCOMPARE(find(ItemKind::Logs, "logs")->sizeBytes, qint64(30));
        QCOMPARE(find(ItemKind::CrashReports, "crash-reports")->sizeBytes, qint64(40));
        QCOMPARE(find(ItemKind::Folder, "config")->sizeBytes, qint64(9090));
        QCOMPARE(find(ItemKind::BigFile, "huge.bin")->sizeBytes, qint64(9000));
        QCOMPARE(find(ItemKind::ZeroData, ".zero")->sizeBytes, qint64(120));
        // saves / mods / resourcepacks / shaderpacks are listed entry by entry, never as one folder.
        QVERIFY(!find(ItemKind::Folder, "saves"));
        QVERIFY(!find(ItemKind::Folder, "mods"));
        QVERIFY(!find(ItemKind::Folder, "resourcepacks"));
        // The big file inside a world folder is part of the world, not an item of its own.
        QVERIFY(!find(ItemKind::BigFile, "r.0.0.mca"));
        // No path twice, biggest first.
        QSet<QString> paths;
        for (int i = 0; i < items.size(); ++i) {
            QVERIFY(!paths.contains(items.at(i).path));
            paths.insert(items.at(i).path);
            if (i > 0)
                QVERIFY(items.at(i - 1).sizeBytes >= items.at(i).sizeBytes);
        }
        QCOMPARE(items.first().sizeBytes, qint64(9090));  // config (folder)
        // The limit cuts the list.
        QCOMPARE(findLargeItems(a, 3, 8000).size(), 3);
        // A threshold of 0 lists no single files.
        for (const LargeItem& i : findLargeItems(a, 100, 0))
            QVERIFY(i.kind != ItemKind::BigFile);
    }

    void largestItemsAcrossInstancesAndPerInstance()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const InstanceSource a = makeInstanceA(tmp.path());
        const InstanceSource b = makeInstanceB(tmp.path());
        const LauncherSource l = makeLauncher(tmp.path());
        const StorageReport report = buildReport({ a, b }, l, QDateTime::currentMSecsSinceEpoch());
        QVERIFY(!report.largest.isEmpty());
        for (int i = 1; i < report.largest.size(); ++i)
            QVERIFY(report.largest.at(i - 1).sizeBytes >= report.largest.at(i).sizeBytes);

        const QList<LargeItem> all = aggregateLargest(report.largest, QString(), 20);
        QVERIFY(all.size() <= 20);
        QCOMPARE(all.first().sizeBytes, report.largest.first().sizeBytes);
        const QList<LargeItem> onlyB = aggregateLargest(report.largest, "b", 20);
        QVERIFY(!onlyB.isEmpty());
        for (const LargeItem& i : onlyB)
            QCOMPARE(i.instanceId, QStringLiteral("b"));
        QCOMPARE(aggregateLargest(report.largest, QString(), 2).size(), 2);
        QCOMPARE(aggregateLargest(report.largest, "nope", 20).size(), 0);

        // ZERO's own copies are listed (world backups per world, snapshots per instance) but are not trashable.
        bool sawBackup = false;
        for (const LargeItem& i : report.largest) {
            if (i.kind == ItemKind::WorldBackups) {
                sawBackup = true;
                QVERIFY(!isTrashableKind(i.kind));
            }
        }
        QVERIFY(sawBackup);
    }

    void runningInstanceItemsCannotBeTrashed()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        InstanceSource a = makeInstanceA(tmp.path());
        const LargeItem world = [&]() {
            for (const LargeItem& i : findLargeItems(a))
                if (i.kind == ItemKind::World)
                    return i;
            return LargeItem();
        }();
        QVERIFY(!world.path.isEmpty());
        QStringList moved;
        TrashHooks hooks;
        hooks.trashPath = [&moved](const QString& path) {
            moved << path;
            return true;
        };
        // Running: refused, nothing moved.
        a.running = true;
        TrashResult refused = trashLargeItems({ world }, { a }, hooks);
        QCOMPARE(refused.trashedCount, 0);
        QCOMPARE(refused.skippedCount, 1);
        QVERIFY(refused.problems.first().error.contains("running"));
        QVERIFY(moved.isEmpty());
        // Not running: it goes through the Recycle Bin hook with the right size.
        a.running = false;
        const TrashResult ok = trashLargeItems({ world }, { a }, hooks);
        QCOMPARE(ok.trashedCount, 1);
        QCOMPARE(ok.trashedBytes, qint64(5000));
        QCOMPARE(moved.size(), 1);
        QVERIFY(QFileInfo::exists(world.path));  // the hook is a fake: only the Recycle Bin call was made
        // Unknown instance.
        LargeItem stray = world;
        stray.instanceId = "zzz";
        QCOMPARE(trashLargeItems({ stray }, { a }, hooks).skippedCount, 1);
    }

    void trashValidationRefusesWrongPlacesAndZeroOwnedKinds()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const InstanceSource a = makeInstanceA(tmp.path());
        const LauncherSource l = makeLauncher(tmp.path());
        QString why;
        auto item = [&a](ItemKind kind, const QString& path) {
            LargeItem i;
            i.kind = kind;
            i.path = path;
            i.name = QFileInfo(path).fileName();
            i.instanceId = a.id;
            return i;
        };
        // Right places.
        QVERIFY(isTrashableItem(item(ItemKind::World, a.gameRoot + "/saves/w1"), a, &why));
        QVERIFY(isTrashableItem(item(ItemKind::Mod, a.gameRoot + "/mods/m1.jar"), a, &why));
        QVERIFY(isTrashableItem(item(ItemKind::Pack, a.gameRoot + "/shaderpacks/sp.zip"), a, &why));
        QVERIFY(isTrashableItem(item(ItemKind::Logs, a.gameRoot + "/logs"), a, &why));
        QVERIFY(isTrashableItem(item(ItemKind::Folder, a.gameRoot + "/config"), a, &why));
        // Wrong places: a world kind pointing at the saves folder, a folder item for the mods folder, a file inside a world, the game root.
        QVERIFY(!isTrashableItem(item(ItemKind::World, a.gameRoot + "/saves"), a, &why));
        QVERIFY(!isTrashableItem(item(ItemKind::Folder, a.gameRoot + "/mods"), a, &why));
        QVERIFY(!isTrashableItem(item(ItemKind::Folder, a.gameRoot + "/saves"), a, &why));
        QVERIFY(!isTrashableItem(item(ItemKind::BigFile, a.gameRoot + "/saves/w1/region/r.0.0.mca"), a, &why));
        QVERIFY(!isTrashableItem(item(ItemKind::Folder, a.gameRoot), a, &why));
        QVERIFY(!isTrashableItem(item(ItemKind::Mod, a.gameRoot + "/config/x.cfg"), a, &why));
        QVERIFY(!isTrashableItem(item(ItemKind::Mod, a.gameRoot + "/mods/missing.jar"), a, &why));
        // ZERO-owned kinds are never trashed from here.
        QVERIFY(!isTrashableItem(item(ItemKind::WorldBackups, l.dataRoot + "/zero/backups/a/w1"), a, &why));
        QVERIFY(why.contains("ZERO"));
        QVERIFY(!isTrashableItem(item(ItemKind::ZeroData, a.instanceRoot + "/.zero"), a, &why));
        // A single big file elsewhere in the game root is fine.
        writeBytes(a.gameRoot + "/misc/dump.hprof", 50);
        QVERIFY(isTrashableItem(item(ItemKind::BigFile, a.gameRoot + "/misc/dump.hprof"), a, &why));
    }

    void scanReportsProgressPerInstance()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const InstanceSource a = makeInstanceA(tmp.path());
        const InstanceSource b = makeInstanceB(tmp.path());
        const LauncherSource l = makeLauncher(tmp.path());
        QList<std::tuple<int, int, QString>> steps;
        buildReport({ a, b }, l, QDateTime::currentMSecsSinceEpoch(),
                    [&steps](int done, int total, const QString& current) { steps.append({ done, total, current }); });
        QCOMPARE(steps.size(), 3);  // Alpha, Bravo, launcher data
        QCOMPARE(std::get<0>(steps.at(0)), 0);
        QCOMPARE(std::get<1>(steps.at(0)), 3);
        QCOMPARE(std::get<2>(steps.at(0)), QStringLiteral("Alpha"));
        QCOMPARE(std::get<2>(steps.at(1)), QStringLiteral("Bravo"));
        QCOMPARE(std::get<0>(steps.at(2)), 2);
        QVERIFY(std::get<2>(steps.at(2)).isEmpty());

        // The async scanner relays it on the GUI thread, determinate and never past `total`.
        auto* scanner = new StorageScanner([&]() { return QPair<QList<InstanceSource>, LauncherSource>({ a, b }, l); });
        QSignalSpy progress(scanner, &StorageScanner::scanProgress);
        QSignalSpy ready(scanner, &StorageScanner::reportReady);
        scanner->rescan();
        QTRY_COMPARE(ready.count(), 1);
        for (const QList<QVariant>& args : progress) {
            QCOMPARE(args.at(1).toInt(), 3);
            QVERIFY(args.at(0).toInt() <= 3);
        }
        delete scanner;
    }
};

QTEST_GUILESS_MAIN(StorageScannerTest)

#include "StorageScanner_test.moc"
