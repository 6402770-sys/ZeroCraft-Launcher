// SPDX-License-Identifier: GPL-3.0-only
// WorldBackupJobs with a configurable destination: new backups go to the chosen folder (or the default root when it is not
// reachable, with a message), listing / restoring / deleting work across the default root and every known root, and zips outside the
// known roots are refused. Temp dirs only.
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <sstream>

#include <io/stream_writer.h>
#include <tag_compound.h>
#include <tag_primitive.h>
#include <tag_string.h>

#include "GZip.h"
#include "zero/backup/BackupDestination.h"
#include "zero/backup/WorldBackup.h"
#include "zero/backup/WorldBackupJobs.h"
#include "zero/worlds/WorldScanner.h"

using namespace Zero::Backup;
using Zero::Worlds::WorldEntry;

namespace {

QByteArray levelDat(const QString& name)
{
    nbt::tag_compound data;
    data.insert("LevelName", nbt::tag_string(name.toStdString()));
    data.insert("LastPlayed", nbt::tag_long(1700000000000));
    data.insert("GameType", nbt::tag_int(0));
    nbt::tag_compound root;
    root.insert("Data", std::move(data));
    std::ostringstream stream;
    nbt::io::write_tag("", root, stream);
    QByteArray raw(stream.str().data(), static_cast<qsizetype>(stream.str().size()));
    QByteArray gz;
    GZip::zip(raw, gz);
    return gz;
}

WorldEntry makeWorld(const QString& saves, const QString& folder, const QByteArray& marker = "a")
{
    const QString dir = saves + QLatin1Char('/') + folder;
    QDir().mkpath(dir + QStringLiteral("/region"));
    for (const auto& [rel, bytes] : { std::pair<QString, QByteArray>{ "level.dat", levelDat(folder) },
                                      std::pair<QString, QByteArray>{ "region/r.0.0.mca", marker + QByteArray(2048, 'x') } }) {
        QFile f(dir + QLatin1Char('/') + rel);
        if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size())
            return {};
    }
    WorldEntry e;
    e.instanceId = "inst";
    e.instanceName = "Instance";
    e.folderPath = dir;
    e.folderName = folder;
    e.name = folder;
    return e;
}

BackupMeta metaOf(const WorldEntry& e)
{
    return { e.instanceId, e.instanceName, e.folderName, e.name };
}

/// Trash that really deletes (the Recycle Bin is not for tests).
Hooks removeHooks()
{
    Hooks h;
    h.trashPath = [](const QString& path) { return QFileInfo(path).isDir() ? QDir(path).removeRecursively() : QFile::remove(path); };
    return h;
}

QStringList zips(const QString& dir)
{
    return QDir(dir).entryList({ "*.zip" }, QDir::Files, QDir::Name);
}

QString oneBackup(const WorldEntry& e, const QString& root, Kind kind = Kind::Manual)
{
    const BackupResult r = createBackup(e.folderPath, worldBackupDirIn(root, e.instanceId, e.folderName), kind, metaOf(e));
    return r.ok ? r.zipPath : QString();
}

}  // namespace

class BackupRootsTest : public QObject {
    Q_OBJECT

   private slots:
    // ---------------------------------------------------------------- where new backups go

    void defaultDestinationWritesIntoTheDefaultRoot()
    {
        QTemporaryDir tmp;
        const WorldEntry w = makeWorld(tmp.filePath("saves"), "W");
        WorldBackupJobs jobs(tmp.filePath("zero/backups"), {});
        QCOMPARE(jobs.activeRoot(), tmp.filePath("zero/backups"));
        QSignalSpy finished(&jobs, &WorldBackupJobs::jobFinished);
        QSignalSpy fell(&jobs, &WorldBackupJobs::fellBackToDefault);
        QVERIFY(jobs.backup(w));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);
        QVERIFY(finished.first().at(1).toBool());
        QCOMPARE(finished.first().at(2).toString(), QString("Backup created"));
        QCOMPARE(fell.size(), 0);
        QCOMPARE(zips(tmp.filePath("zero/backups/inst/W")).size(), 1);
    }

    void chosenFolderReceivesTheBackupAndIsRemembered()
    {
        QTemporaryDir tmp;
        const WorldEntry w = makeWorld(tmp.filePath("saves"), "W");
        QVERIFY(QDir().mkpath(tmp.filePath("cloud")));
        WorldBackupJobs jobs(tmp.filePath("zero/backups"), {});
        QString chosen;
        QStringList remembered;
        WorldBackupJobs::Destination d;
        d.chosenFolder = [&chosen]() { return chosen; };
        d.rootUsed = [&remembered](const QString& root) { remembered << root; };
        jobs.setDestination(d);

        chosen = tmp.filePath("cloud");  // changed after construction: read at request time
        QCOMPARE(jobs.activeRoot(), tmp.filePath("cloud/ZERO Backups"));
        QSignalSpy finished(&jobs, &WorldBackupJobs::jobFinished);
        QVERIFY(jobs.backup(w));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);
        QVERIFY(finished.first().at(1).toBool());
        QCOMPARE(finished.first().at(2).toString(), QString("Backup created"));
        QCOMPARE(zips(tmp.filePath("cloud/ZERO Backups/inst/W")).size(), 1);
        QVERIFY(!QFileInfo::exists(tmp.filePath("zero/backups/inst/W")));
        QCOMPARE(remembered, QStringList({ QDir::cleanPath(tmp.filePath("cloud/ZERO Backups")) }));
    }

    void unreachableFolderFallsBackToTheDefaultRootAndSaysSo()
    {
        QTemporaryDir tmp;
        const WorldEntry w = makeWorld(tmp.filePath("saves"), "W");
        const QString gone = tmp.filePath("unplugged-drive/Cloud");
        WorldBackupJobs jobs(tmp.filePath("zero/backups"), {});
        QStringList remembered;
        WorldBackupJobs::Destination d;
        d.chosenFolder = [gone]() { return gone; };
        d.rootUsed = [&remembered](const QString& root) { remembered << root; };
        jobs.setDestination(d);
        QSignalSpy finished(&jobs, &WorldBackupJobs::jobFinished);
        QSignalSpy fell(&jobs, &WorldBackupJobs::fellBackToDefault);

        QVERIFY(jobs.backup(w, Kind::Auto));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);
        QVERIFY(finished.first().at(1).toBool());  // the backup itself succeeded
        const QString message = finished.first().at(2).toString();
        QVERIFY2(message.startsWith("Backup created. Saved in the ZERO folder: "), qPrintable(message));
        QVERIFY(message.endsWith("isn't reachable"));
        QVERIFY(finished.first().at(3).toBool());  // automatic flag kept
        QCOMPARE(fell.size(), 1);
        QCOMPARE(fell.first().at(0).toString(), w.key());
        QCOMPARE(fell.first().at(1).toString(), QDir::cleanPath(gone));
        QCOMPARE(zips(tmp.filePath("zero/backups/inst/W")).size(), 1);
        QVERIFY(!QFileInfo::exists(tmp.filePath("unplugged-drive")));  // nothing was created at the chosen place
        QVERIFY(remembered.isEmpty());                                 // a root that failed is not "used"
    }

    // ---------------------------------------------------------------- several roots

    void listingMergesEveryKnownRootNewestFirst()
    {
        QTemporaryDir tmp;
        const WorldEntry w = makeWorld(tmp.filePath("saves"), "W");
        const QString rootDefault = tmp.filePath("zero/backups");
        const QString rootOld = tmp.filePath("old/ZERO Backups");
        const QString rootCloud = tmp.filePath("cloud/ZERO Backups");
        const QString first = oneBackup(w, rootOld);
        QTest::qWait(30);
        const QString second = oneBackup(w, rootDefault, Kind::Auto);
        QTest::qWait(30);
        const QString third = oneBackup(w, rootCloud);
        QVERIFY(!first.isEmpty() && !second.isEmpty() && !third.isEmpty());
        QVERIFY(QDir().mkpath(tmp.filePath("cloud")));

        WorldBackupJobs jobs(rootDefault, {});
        WorldBackupJobs::Destination d;
        d.chosenFolder = [&]() { return tmp.filePath("cloud"); };
        d.knownRoots = [&]() { return QStringList({ rootOld }); };
        jobs.setDestination(d);
        QCOMPARE(jobs.allRoots(), QStringList({ rootDefault, rootOld, rootCloud }));

        int listed = 0;
        QList<BackupInfo> list;
        connect(&jobs, &WorldBackupJobs::backupsListed, this, [&](const QString&, const QList<BackupInfo>& l) {
            ++listed;
            list = l;
        });
        jobs.refreshList(w);
        QTRY_COMPARE_WITH_TIMEOUT(listed, 1, 20000);
        QCOMPARE(list.size(), 3);
        QCOMPARE(list.at(0).zipPath, third);
        QCOMPARE(list.at(1).zipPath, second);
        QCOMPARE(list.at(2).zipPath, first);
    }

    void listingOfAWorldWithoutBackupsIsEmptyAndOtherWorldsAreNotMixedIn()
    {
        QTemporaryDir tmp;
        const WorldEntry w = makeWorld(tmp.filePath("saves"), "W");
        const WorldEntry v = makeWorld(tmp.filePath("saves"), "V");
        const QString rootOld = tmp.filePath("old");
        QVERIFY(!oneBackup(v, rootOld).isEmpty());
        WorldBackupJobs jobs(tmp.filePath("zero/backups"), {});
        WorldBackupJobs::Destination d;
        d.knownRoots = [&]() { return QStringList({ rootOld }); };
        jobs.setDestination(d);
        int listed = 0;
        QList<BackupInfo> list;
        connect(&jobs, &WorldBackupJobs::backupsListed, this, [&](const QString&, const QList<BackupInfo>& l) {
            ++listed;
            list = l;
        });
        jobs.refreshList(w);
        QTRY_COMPARE_WITH_TIMEOUT(listed, 1, 20000);
        QVERIFY(list.isEmpty());
    }

    void removeWorksInAnyKnownRootAndRefusesOthers()
    {
        QTemporaryDir tmp;
        const WorldEntry w = makeWorld(tmp.filePath("saves"), "W");
        const QString rootDefault = tmp.filePath("zero/backups");
        const QString rootOld = tmp.filePath("old/ZERO Backups");
        const QString rootStranger = tmp.filePath("stranger/ZERO Backups");
        const QString inOld = oneBackup(w, rootOld);
        const QString inDefault = oneBackup(w, rootDefault, Kind::Auto);
        const QString inStranger = oneBackup(w, rootStranger);
        QVERIFY(!inOld.isEmpty() && !inDefault.isEmpty() && !inStranger.isEmpty());

        WorldBackupJobs jobs(rootDefault, {});
        jobs.setHooks(removeHooks());
        WorldBackupJobs::Destination d;
        d.knownRoots = [&]() { return QStringList({ rootOld }); };
        jobs.setDestination(d);
        QSignalSpy finished(&jobs, &WorldBackupJobs::jobFinished);

        // a zip in an unknown root is refused, for delete and for restore, and stays on disk
        QVERIFY(!jobs.remove(w, inStranger));
        QVERIFY(!jobs.restore(w, inStranger));
        QCOMPARE(finished.size(), 2);
        QVERIFY(!finished.at(0).at(1).toBool());
        QCOMPARE(finished.at(0).at(2).toString(), QString("This backup does not belong to this world."));
        QVERIFY(QFileInfo::exists(inStranger));

        // a zip of another world inside a known root is refused as well
        const WorldEntry v = makeWorld(tmp.filePath("saves"), "V");
        const QString otherWorld = oneBackup(v, rootOld);
        QVERIFY(!jobs.remove(w, otherWorld));
        QVERIFY(QFileInfo::exists(otherWorld));

        // a path that only looks like a known root (".." tricks) is refused
        QVERIFY(!jobs.remove(w, rootOld + "/inst/W/../../../../stranger/ZERO Backups/inst/W/" + QFileInfo(inStranger).fileName()));
        QVERIFY(QFileInfo::exists(inStranger));

        // the two legitimate ones are deleted, each through its own root
        QVERIFY(jobs.remove(w, inOld));
        QTRY_VERIFY_WITH_TIMEOUT(!jobs.isBusy(w.key()), 20000);
        QVERIFY(!QFileInfo::exists(inOld));
        QVERIFY(finished.last().at(1).toBool());
        QVERIFY(jobs.remove(w, inDefault));
        QTRY_VERIFY_WITH_TIMEOUT(!jobs.isBusy(w.key()), 20000);
        QVERIFY(!QFileInfo::exists(inDefault));
        QVERIFY(QFileInfo::exists(inStranger));
        QVERIFY(QFileInfo::exists(otherWorld));
    }

    void restoreFromAnOldRootMakesItsSafetyBackupInTheActiveRoot()
    {
        QTemporaryDir tmp;
        const WorldEntry w = makeWorld(tmp.filePath("saves"), "W", "orig");
        const QString rootDefault = tmp.filePath("zero/backups");
        const QString rootOld = tmp.filePath("old/ZERO Backups");
        const QString inOld = oneBackup(w, rootOld);
        QVERIFY(!inOld.isEmpty());
        makeWorld(tmp.filePath("saves"), "W", "changed");  // the world changed after the backup

        WorldBackupJobs jobs(rootDefault, {});
        jobs.setHooks(removeHooks());
        WorldBackupJobs::Destination d;
        d.knownRoots = [&]() { return QStringList({ rootOld }); };
        jobs.setDestination(d);
        QSignalSpy finished(&jobs, &WorldBackupJobs::jobFinished);
        QSignalSpy restored(&jobs, &WorldBackupJobs::worldRestored);
        QVERIFY(jobs.restore(w, inOld));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        QVERIFY2(finished.first().at(1).toBool(), qPrintable(finished.first().at(2).toString()));
        QCOMPARE(restored.size(), 1);

        QFile f(w.folderPath + "/region/r.0.0.mca");
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.readAll().startsWith("orig"));
        const QStringList safety = zips(tmp.filePath("zero/backups/inst/W"));
        QCOMPARE(safety.size(), 1);
        QVERIFY(safety.first().endsWith("-safety.zip"));
        QVERIFY(QFileInfo::exists(inOld));  // the old root keeps its backup (retention only touches the dir just written to)
        QVERIFY(zips(tmp.filePath("old/ZERO Backups/inst/W")).size() == 1);
    }

    void backupFolderToOpenIsTheActiveOneOrTheFirstThatExists()
    {
        QTemporaryDir tmp;
        const WorldEntry w = makeWorld(tmp.filePath("saves"), "W");
        const QString rootDefault = tmp.filePath("zero/backups");
        QVERIFY(QDir().mkpath(tmp.filePath("cloud")));
        WorldBackupJobs jobs(rootDefault, {});
        WorldBackupJobs::Destination d;
        d.chosenFolder = [&]() { return tmp.filePath("cloud"); };
        jobs.setDestination(d);

        const QString activeDir = tmp.filePath("cloud/ZERO Backups/inst/W");
        const QString defaultDir = tmp.filePath("zero/backups/inst/W");
        QCOMPARE(jobs.backupDir(w), activeDir);  // nothing exists yet: the active one
        QVERIFY(QDir().mkpath(defaultDir));
        QCOMPARE(jobs.backupDir(w), defaultDir);  // only the default one exists
        QVERIFY(QDir().mkpath(activeDir));
        QCOMPARE(jobs.backupDir(w), activeDir);  // both: the active one
    }

    void settingTheDestinationTwiceDoesNotDuplicateRoots()
    {
        QTemporaryDir tmp;
        WorldBackupJobs jobs(tmp.filePath("zero/backups"), {});
        QStringList known{ tmp.filePath("cloud/ZERO Backups") };
        WorldBackupJobs::Destination d;
        d.chosenFolder = [&]() { return tmp.filePath("cloud"); };
        d.knownRoots = [&]() { return known; };
        jobs.setDestination(d);
        QCOMPARE(jobs.allRoots(), QStringList({ tmp.filePath("zero/backups"), tmp.filePath("cloud/ZERO Backups") }));
    }
};

QTEST_GUILESS_MAIN(BackupRootsTest)

#include "BackupRoots_test.moc"
