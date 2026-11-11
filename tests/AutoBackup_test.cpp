// SPDX-License-Identifier: GPL-3.0-only
// Automatic world backups: the pure world selection (touched since the session start, 5 s slack, size cap) and the service
// (backs up only the worlds touched in the session as Kind::Auto, one world at a time, keeps the instance locked until the queue
// ended, does nothing when disabled or when the game never started, counts refused jobs as failed). Temp dirs only.
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
#include "zero/backup/AutoBackup.h"
#include "zero/backup/WorldBackup.h"
#include "zero/backup/WorldBackupJobs.h"
#include "zero/worlds/WorldScanner.h"

using namespace Zero::Backup;
using Zero::Worlds::InstanceSource;
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

/// A world folder with a valid level.dat whose modification time is `modified`.
QString makeWorld(const QString& saves, const QString& folder, const QDateTime& modified)
{
    const QString dir = saves + QLatin1Char('/') + folder;
    QDir().mkpath(dir + QStringLiteral("/region"));
    for (const auto& [rel, bytes] : { std::pair<QString, QByteArray>{ "level.dat", levelDat(folder) },
                                      std::pair<QString, QByteArray>{ "region/r.0.0.mca", QByteArray(4096, 'x') } }) {
        QFile f(dir + QLatin1Char('/') + rel);
        if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size())
            return {};
        if (rel == QLatin1String("level.dat"))
            f.setFileTime(modified, QFileDevice::FileModificationTime);
    }
    return dir;
}

AutoCandidate candidate(const QString& folder, qint64 modifiedMs, qint64 size = 100)
{
    AutoCandidate c;
    c.world.instanceId = "inst";
    c.world.folderName = folder;
    c.world.name = folder;
    c.levelDatModifiedMs = modifiedMs;
    c.sizeBytes = size;
    return c;
}

QStringList names(const QList<WorldEntry>& list)
{
    QStringList out;
    for (const auto& w : list)
        out << w.folderName;
    return out;
}

}  // namespace

class AutoBackupTest : public QObject {
    Q_OBJECT

   private slots:
    // ---------------------------------------------------------------- pure selection

    void touchedSinceHasFiveSecondsOfSlack()
    {
        const qint64 start = 1'000'000'000'000;
        QVERIFY(touchedSince(start + 1, start));
        QVERIFY(touchedSince(start, start));
        QVERIFY(touchedSince(start - 5000, start));  // exactly the slack
        QVERIFY(!touchedSince(start - 5001, start));
        QVERIFY(!touchedSince(0, start));  // unreadable level.dat
        QVERIFY(!touchedSince(0, 0));
    }

    void selectionPicksTouchedWorldsOnly()
    {
        const qint64 start = 1'000'000'000'000;
        const QList<AutoCandidate> input{ candidate("played", start + 60'000), candidate("old", start - 3'600'000),
                                          candidate("slack", start - 4000), candidate("justOutside", start - 6000),
                                          candidate("gone", 0) };
        const AutoSelection s = selectWorlds(input, start);
        QCOMPARE(names(s.toBackUp), QStringList({ "played", "slack" }));
        QVERIFY(s.tooLarge.isEmpty());
    }

    void selectionSkipsHugeWorlds()
    {
        const qint64 start = 1'000'000'000'000;
        const QList<AutoCandidate> input{ candidate("atCap", start, kAutoMaxBytes), candidate("overCap", start, kAutoMaxBytes + 1),
                                          candidate("small", start, 10), candidate("hugeButUntouched", start - 999'999, kAutoMaxBytes * 3) };
        const AutoSelection s = selectWorlds(input, start);
        QCOMPARE(names(s.toBackUp), QStringList({ "atCap", "small" }));
        QCOMPARE(names(s.tooLarge), QStringList({ "overCap" }));  // an untouched huge world is not even reported
        QCOMPARE(kAutoMaxBytes, qint64(2) * 1024 * 1024 * 1024);
    }

    void selectionOfNothing()
    {
        const AutoSelection s = selectWorlds({}, 12345);
        QVERIFY(s.toBackUp.isEmpty());
        QVERIFY(s.tooLarge.isEmpty());
    }

    void levelDatModifiedReadsTheFile()
    {
        QTemporaryDir root;
        const QDateTime t = QDateTime::fromMSecsSinceEpoch(1'700'000'123'000);
        const QString w = makeWorld(root.filePath("saves"), "W", t);
        QCOMPARE(levelDatModifiedMs(w), t.toMSecsSinceEpoch());
        QCOMPARE(levelDatModifiedMs(root.filePath("saves/none")), qint64(0));
    }

    // ---------------------------------------------------------------- service

    void backsUpOnlyTheTouchedWorldAsAuto()
    {
        QTemporaryDir root;
        const QString saves = root.filePath("saves");
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        makeWorld(saves, "Played", QDateTime::fromMSecsSinceEpoch(now));
        makeWorld(saves, "Idle", QDateTime::fromMSecsSinceEpoch(now - 3'600'000));
        WorldBackupJobs jobs(root.filePath("zero/backups"), {});
        QSignalSpy jobDone(&jobs, &WorldBackupJobs::jobFinished);
        AutoBackup service(
            &jobs, [] { return true; }, [&](const QString& id) { return InstanceSource{ id, "Instance", saves }; },
            [now] { return now - 60'000; });
        QSignalSpy finished(&service, &AutoBackup::finished);

        service.onGameRunning("inst");
        service.onLaunchEnded("inst", true, false, true);
        QVERIFY(service.isPending("inst"));
        QVERIFY(WorldBackupJobs::isInstanceLocked("inst"));  // locked from the moment the session ended
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);
        QVERIFY(!service.isPending("inst"));
        QVERIFY(!WorldBackupJobs::isInstanceLocked("inst"));

        const auto args = finished.first();
        QCOMPARE(args.at(0).toString(), QString("inst"));
        QCOMPARE(args.at(1).toString(), QString("Instance"));
        QCOMPARE(args.at(2).toInt(), 1);
        QVERIFY(args.at(3).toStringList().isEmpty());
        QCOMPARE(args.at(4).toInt(), 0);

        QCOMPARE(jobDone.size(), 1);
        QVERIFY(jobDone.first().at(3).toBool());  // flagged automatic: the Worlds page does not toast it

        const QList<BackupInfo> played = listBackups(backupDirFor(root.path(), "inst", "Played"));
        QCOMPARE(played.size(), 1);
        QCOMPARE(played.first().kind, Kind::Auto);
        QVERIFY(played.first().valid);
        QVERIFY(played.first().zipPath.endsWith("-auto.zip"));
        QVERIFY(listBackups(backupDirFor(root.path(), "inst", "Idle")).isEmpty());
    }

    void queueRunsOneWorldAtATimeAndKeepsTheInstanceLocked()
    {
        QTemporaryDir root;
        const QString saves = root.filePath("saves");
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (const char* name : { "A", "B", "C" })
            makeWorld(saves, name, QDateTime::fromMSecsSinceEpoch(now));
        WorldBackupJobs jobs(root.filePath("zero/backups"), {});
        int active = 0, maxActive = 0, started = 0;
        bool lockedAtEveryStart = true;
        connect(&jobs, &WorldBackupJobs::jobStarted, this, [&](const QString&) {
            ++started;
            maxActive = qMax(maxActive, ++active);
            lockedAtEveryStart = lockedAtEveryStart && WorldBackupJobs::isInstanceLocked("inst");
        });
        connect(&jobs, &WorldBackupJobs::jobFinished, this, [&](const QString&, bool, const QString&, bool) { --active; });
        AutoBackup service(
            &jobs, [] { return true; }, [&](const QString& id) { return InstanceSource{ id, "Instance", saves }; },
            [now] { return now - 1000; });
        QSignalSpy finished(&service, &AutoBackup::finished);

        service.onGameRunning("inst");
        service.onLaunchEnded("inst", false, false, true);  // a crashed / stopped session is backed up too
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        QCOMPARE(started, 3);
        QCOMPARE(maxActive, 1);
        QVERIFY(lockedAtEveryStart);
        QCOMPARE(finished.first().at(2).toInt(), 3);
        QCOMPARE(finished.first().at(4).toInt(), 0);
        QVERIFY(!WorldBackupJobs::isInstanceLocked("inst"));
        for (const char* name : { "A", "B", "C" })
            QCOMPARE(listBackups(backupDirFor(root.path(), "inst", name)).size(), 1);
    }

    void refusedJobsCountAsFailedAndTheQueueContinues()
    {
        QTemporaryDir root;
        const QString saves = root.filePath("saves");
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        makeWorld(saves, "A", QDateTime::fromMSecsSinceEpoch(now));
        makeWorld(saves, "B", QDateTime::fromMSecsSinceEpoch(now));
        WorldBackupJobs jobs(root.filePath("zero/backups"), [](const QString&) { return true; });  // the instance "runs"
        AutoBackup service(
            &jobs, [] { return true; }, [&](const QString& id) { return InstanceSource{ id, "Instance", saves }; },
            [now] { return now - 1000; });
        QSignalSpy finished(&service, &AutoBackup::finished);

        service.onGameRunning("inst");
        service.onLaunchEnded("inst", true, false, true);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);
        QCOMPARE(finished.first().at(2).toInt(), 0);
        QCOMPARE(finished.first().at(4).toInt(), 2);  // both worlds were tried
        QVERIFY(!finished.first().at(5).toString().isEmpty());
        QVERIFY(!WorldBackupJobs::isInstanceLocked("inst"));
        QVERIFY(!service.isPending("inst"));
    }

    void disabledOrNeverStartedDoesNothing()
    {
        QTemporaryDir root;
        const QString saves = root.filePath("saves");
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        makeWorld(saves, "A", QDateTime::fromMSecsSinceEpoch(now));
        WorldBackupJobs jobs(root.filePath("zero/backups"), {});
        QSignalSpy jobStarted(&jobs, &WorldBackupJobs::jobStarted);
        bool enabled = false;
        AutoBackup service(
            &jobs, [&] { return enabled; }, [&](const QString& id) { return InstanceSource{ id, "Instance", saves }; },
            [now] { return now - 1000; });
        QSignalSpy finished(&service, &AutoBackup::finished);

        service.onGameRunning("inst");  // setting off
        service.onLaunchEnded("inst", true, false, true);
        QVERIFY(!service.isPending("inst"));
        QVERIFY(!WorldBackupJobs::isInstanceLocked("inst"));

        enabled = true;
        service.onGameRunning("inst");  // the game never started (e.g. launch failed or was cancelled)
        service.onLaunchEnded("inst", false, true, false);
        QVERIFY(!service.isPending("inst"));

        service.onLaunchEnded("inst", true, false, true);  // no Running phase seen: no session start, nothing to select
        QVERIFY(!service.isPending("inst"));

        QTest::qWait(300);
        QCOMPARE(jobStarted.size(), 0);
        QCOMPARE(finished.size(), 0);
        QVERIFY(!QFileInfo::exists(root.filePath("zero/backups")));
    }

    void nothingTouchedGivesNoSignalAndReleasesTheLock()
    {
        QTemporaryDir root;
        const QString saves = root.filePath("saves");
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        makeWorld(saves, "Old", QDateTime::fromMSecsSinceEpoch(now - 86'400'000));
        WorldBackupJobs jobs(root.filePath("zero/backups"), {});
        AutoBackup service(
            &jobs, [] { return true; }, [&](const QString& id) { return InstanceSource{ id, "Instance", saves }; },
            [now] { return now; });
        QSignalSpy finished(&service, &AutoBackup::finished);

        service.onGameRunning("inst");
        service.onLaunchEnded("inst", true, false, true);
        QVERIFY(WorldBackupJobs::isInstanceLocked("inst"));
        QTRY_VERIFY_WITH_TIMEOUT(!service.isPending("inst"), 10000);
        QVERIFY(!WorldBackupJobs::isInstanceLocked("inst"));
        QCOMPARE(finished.size(), 0);
        QVERIFY(!QFileInfo::exists(root.filePath("zero/backups")));
    }
};

QTEST_GUILESS_MAIN(AutoBackupTest)

#include "AutoBackup_test.moc"
