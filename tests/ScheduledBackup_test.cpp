// SPDX-License-Identifier: GPL-3.0-only
// Scheduled world backups: the schedule setting, when a run is due (daily / weekly / never run / clock set back), which worlds a run
// backs up (changed since the newest backup in ANY known root, or without a backup; too large ones counted), the summary text and the
// service (catch-up check, never while a game runs, interruption, failures, fallback note, recorded run time). Temp dirs only.
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
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
#include "zero/backup/ScheduledBackup.h"
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
WorldEntry makeWorld(const QString& saves, const QString& folder, const QDateTime& modified)
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
    WorldEntry e;
    e.instanceId = "inst";
    e.instanceName = "Instance";
    e.folderPath = dir;
    e.folderName = folder;
    e.name = folder;
    return e;
}

QString oneBackup(const WorldEntry& e, const QString& root)
{
    const BackupMeta meta{ e.instanceId, e.instanceName, e.folderName, e.name };
    return createBackup(e.folderPath, worldBackupDirIn(root, e.instanceId, e.folderName), Kind::Manual, meta).zipPath;
}

int zipCount(const QString& root, const QString& folder)
{
    return QDir(worldBackupDirIn(root, "inst", folder)).entryList({ "*.zip" }, QDir::Files).size();
}

ScheduleCandidate candidate(const QString& folder, qint64 levelMs, qint64 newestBackup, qint64 size = 100)
{
    ScheduleCandidate c;
    c.world.instanceId = "inst";
    c.world.folderName = folder;
    c.world.name = folder;
    c.levelDatModifiedMs = levelMs;
    c.newestBackupMs = newestBackup;
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

bool writeState(const QString& path, qint64 lastRunMs)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(QJsonObject{ { "version", 1 }, { "lastRunMs", double(lastRunMs) } }).toJson());
    return true;
}

qint64 readState(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return -1;
    return qint64(QJsonDocument::fromJson(f.readAll()).object().value("lastRunMs").toDouble());
}

constexpr qint64 kFakeNow = 1'900'000'000'000;

/// Everything a service test needs: jobs on the default root, a fixed clock, fast timers.
struct Fixture {
    QTemporaryDir tmp;
    QString saves = tmp.filePath("saves");
    QString defaultRoot = tmp.filePath("zero/backups");
    QString statePath = tmp.filePath("zero/backup-schedule.json");
    WorldBackupJobs jobs{ defaultRoot, [this](const QString&) { return instanceRuns; } };
    bool instanceRuns = false;
    Schedule schedule = Schedule::Daily;
    std::function<bool()> gameActive = [] { return false; };
    QStringList extraRoots;
    QString chosenFolder;

    Fixture()
    {
        WorldBackupJobs::Destination d;
        d.chosenFolder = [this]() { return chosenFolder; };
        d.knownRoots = [this]() { return extraRoots; };
        jobs.setDestination(d);
    }

    ScheduledBackup::Hooks hooks()
    {
        ScheduledBackup::Hooks h;
        h.schedule = [this]() { return schedule; };
        h.gameActive = [this]() { return gameActive(); };
        h.instances = [this]() { return QList<InstanceSource>{ { "inst", "Instance", saves } }; };
        h.now = []() { return kFakeNow; };
        return h;
    }
    static ScheduledBackup::Timing fast() { return { 20, 50 }; }
};

}  // namespace

class ScheduledBackupTest : public QObject {
    Q_OBJECT

   private slots:
    // ---------------------------------------------------------------- schedule setting and due time

    void scheduleStrings()
    {
        QCOMPARE(scheduleFromString("off"), Schedule::Off);
        QCOMPARE(scheduleFromString("daily"), Schedule::Daily);
        QCOMPARE(scheduleFromString("weekly"), Schedule::Weekly);
        QCOMPARE(scheduleFromString(" Weekly "), Schedule::Weekly);
        QCOMPARE(scheduleFromString(""), Schedule::Off);
        QCOMPARE(scheduleFromString("hourly"), Schedule::Off);
        for (const Schedule s : { Schedule::Off, Schedule::Daily, Schedule::Weekly })
            QCOMPARE(scheduleFromString(scheduleToString(s)), s);
        QCOMPARE(scheduleIntervalMs(Schedule::Off), qint64(0));
        QCOMPARE(scheduleIntervalMs(Schedule::Daily), qint64(24 * 3600 * 1000));
        QCOMPARE(scheduleIntervalMs(Schedule::Weekly), qint64(7 * 24 * 3600 * 1000));
    }

    void dueComputation()
    {
        const qint64 now = kFakeNow;
        // off is never due
        QVERIFY(!isDue(Schedule::Off, 0, now));
        QVERIFY(!isDue(Schedule::Off, now - kWeeklyMs * 9, now));
        // never ran: due
        QVERIFY(isDue(Schedule::Daily, 0, now));
        QVERIFY(isDue(Schedule::Weekly, 0, now));
        // daily: 24 h exactly is due, one ms less is not
        QVERIFY(!isDue(Schedule::Daily, now - kDailyMs + 1, now));
        QVERIFY(isDue(Schedule::Daily, now - kDailyMs, now));
        QVERIFY(isDue(Schedule::Daily, now - 3 * kDailyMs, now));
        // weekly: a 3 day old run is not due, 7 days is
        QVERIFY(!isDue(Schedule::Weekly, now - 3 * kDailyMs, now));
        QVERIFY(!isDue(Schedule::Weekly, now - kWeeklyMs + 1, now));
        QVERIFY(isDue(Schedule::Weekly, now - kWeeklyMs, now));
        // a run that just ended
        QVERIFY(!isDue(Schedule::Daily, now, now));
        // the clock was set back: a run "in the future" cannot be trusted, so it is due
        QVERIFY(isDue(Schedule::Daily, now + 1, now));
        QVERIFY(isDue(Schedule::Weekly, now + kWeeklyMs, now));
    }

    // ---------------------------------------------------------------- world selection

    void needsBackupRule()
    {
        QVERIFY(needsBackup(1000, 0));      // no backup at all
        QVERIFY(needsBackup(0, 0));         // no backup, unreadable level.dat: still backed up
        QVERIFY(needsBackup(2000, 1000));   // changed after the newest backup
        QVERIFY(!needsBackup(1000, 1000));  // equal: nothing changed
        QVERIFY(!needsBackup(500, 1000));   // older than the backup
        QVERIFY(!needsBackup(0, 1000));     // unreadable level.dat but already backed up
    }

    void selectionPicksChangedAndUnbackedWorlds()
    {
        const QList<ScheduleCandidate> input{ candidate("none", 100, 0), candidate("fresh", 100, 200), candidate("changed", 300, 200),
                                              candidate("equal", 200, 200), candidate("unreadable", 0, 0) };
        const AutoSelection s = selectScheduled(input);
        QCOMPARE(names(s.toBackUp), QStringList({ "none", "changed", "unreadable" }));
        QVERIFY(s.tooLarge.isEmpty());
    }

    void selectionCountsHugeWorldsAsTooLarge()
    {
        const QList<ScheduleCandidate> input{ candidate("atCap", 100, 0, kAutoMaxBytes), candidate("overCap", 100, 0, kAutoMaxBytes + 1),
                                              candidate("hugeButUnchanged", 100, 200, kAutoMaxBytes * 3), candidate("small", 300, 200, 10) };
        const AutoSelection s = selectScheduled(input);
        QCOMPARE(names(s.toBackUp), QStringList({ "atCap", "small" }));
        QCOMPARE(names(s.tooLarge), QStringList({ "overCap" }));  // an unchanged huge world is not even reported
        QVERIFY(selectScheduled({}).toBackUp.isEmpty());
    }

    void newestBackupLooksInEveryRoot()
    {
        QTemporaryDir tmp;
        const WorldEntry w = makeWorld(tmp.filePath("saves"), "W", QDateTime::currentDateTime());
        QCOMPARE(newestBackupMs({ worldBackupDirIn(tmp.filePath("a"), "inst", "W"), worldBackupDirIn(tmp.filePath("b"), "inst", "W") }), qint64(0));
        const QString inA = oneBackup(w, tmp.filePath("a"));
        QTest::qWait(30);
        const QString inB = oneBackup(w, tmp.filePath("b"));
        QVERIFY(!inA.isEmpty() && !inB.isEmpty());
        const qint64 ofA = listBackups(worldBackupDirIn(tmp.filePath("a"), "inst", "W")).first().createdMs;
        const qint64 ofB = listBackups(worldBackupDirIn(tmp.filePath("b"), "inst", "W")).first().createdMs;
        QVERIFY(ofB > ofA);
        QCOMPARE(newestBackupMs({ worldBackupDirIn(tmp.filePath("a"), "inst", "W") }), ofA);
        QCOMPARE(newestBackupMs({ worldBackupDirIn(tmp.filePath("a"), "inst", "W"), worldBackupDirIn(tmp.filePath("b"), "inst", "W") }), ofB);
    }

    // ---------------------------------------------------------------- summary text

    void summaryTexts()
    {
        QCOMPARE(summaryText(0, 0, 0, {}, {}), QString());
        QCOMPARE(summaryText(0, 0, 4, {}, "E:/Cloud"), QString());  // nothing done: no toast
        QCOMPARE(summaryText(3, 0, 0, {}, {}), QString("Scheduled backup: 3 worlds backed up."));
        QCOMPARE(summaryText(1, 0, 0, {}, {}), QString("Scheduled backup: 1 world backed up."));
        QCOMPARE(summaryText(2, 1, 0, "disk full", {}), QString("Scheduled backup: 2 worlds backed up, 1 world failed (disk full)."));
        QCOMPARE(summaryText(0, 2, 1, {}, {}), QString("Scheduled backup: 2 worlds failed, 1 world too large to back up automatically."));
        const QString withFallback = summaryText(1, 0, 0, {}, "E:/Cloud");
        QVERIFY(withFallback.startsWith("Scheduled backup: 1 world backed up. Saved in the ZERO folder: "));
        QVERIFY(withFallback.endsWith("isn't reachable."));
    }

    // ---------------------------------------------------------------- service

    void dueRunBacksUpOnlyWhatChanged()
    {
        Fixture fx;
        const QDateTime now = QDateTime::currentDateTime();
        const QDateTime old = now.addSecs(-3600);
        makeWorld(fx.saves, "NeverBackedUp", old);
        const WorldEntry unchanged = makeWorld(fx.saves, "Unchanged", old);
        const WorldEntry changed = makeWorld(fx.saves, "Changed", now.addSecs(60));  // newer than the backup made below
        const WorldEntry inOtherRoot = makeWorld(fx.saves, "OnlyInOldRoot", old);
        fx.extraRoots = { fx.tmp.filePath("old/ZERO Backups") };
        QVERIFY(!oneBackup(unchanged, fx.defaultRoot).isEmpty());
        QVERIFY(!oneBackup(changed, fx.defaultRoot).isEmpty());
        QVERIFY(!oneBackup(inOtherRoot, fx.extraRoots.first()).isEmpty());

        ScheduledBackup service(&fx.jobs, fx.statePath, fx.hooks(), Fixture::fast());
        QCOMPARE(service.lastRunMs(), qint64(0));
        QSignalSpy finished(&service, &ScheduledBackup::finished);
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);

        const auto args = finished.first();
        QCOMPARE(args.at(0).toInt(), 2);  // backed up: NeverBackedUp + Changed
        QCOMPARE(args.at(1).toInt(), 0);
        QCOMPARE(args.at(2).toInt(), 0);
        QVERIFY(args.at(4).toString().isEmpty());
        QCOMPARE(zipCount(fx.defaultRoot, "NeverBackedUp"), 1);
        QCOMPARE(zipCount(fx.defaultRoot, "Changed"), 2);
        QCOMPARE(zipCount(fx.defaultRoot, "Unchanged"), 1);
        QCOMPARE(zipCount(fx.defaultRoot, "OnlyInOldRoot"), 0);  // its backup in the old root counts
        QVERIFY(!service.isRunning());
        QVERIFY(!WorldBackupJobs::isInstanceLocked("inst"));

        // the run was recorded, in memory and on disk
        QCOMPARE(service.lastRunMs(), kFakeNow);
        QCOMPARE(readState(fx.statePath), kFakeNow);
        // and flagged as automatic backups
        const auto list = listBackups(worldBackupDirIn(fx.defaultRoot, "inst", "NeverBackedUp"));
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.first().kind, Kind::Auto);

        // not due any more: later checks do nothing
        QTest::qWait(300);
        QCOMPARE(finished.size(), 1);
        QCOMPARE(zipCount(fx.defaultRoot, "NeverBackedUp"), 1);

        // a new service reads the recorded time
        ScheduledBackup again(&fx.jobs, fx.statePath, fx.hooks(), Fixture::fast());
        QCOMPARE(again.lastRunMs(), kFakeNow);
    }

    void aRunWithNothingToDoIsStillRecorded()
    {
        Fixture fx;
        const QDateTime old = QDateTime::currentDateTime().addSecs(-3600);
        const WorldEntry w = makeWorld(fx.saves, "W", old);
        QVERIFY(!oneBackup(w, fx.defaultRoot).isEmpty());
        ScheduledBackup service(&fx.jobs, fx.statePath, fx.hooks(), Fixture::fast());
        QSignalSpy finished(&service, &ScheduledBackup::finished);
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        QCOMPARE(finished.first().at(0).toInt(), 0);
        QCOMPARE(finished.first().at(1).toInt(), 0);
        QCOMPARE(readState(fx.statePath), kFakeNow);
        QCOMPARE(zipCount(fx.defaultRoot, "W"), 1);
    }

    void notDueOrOffDoesNothing()
    {
        Fixture fx;
        makeWorld(fx.saves, "W", QDateTime::currentDateTime());
        QVERIFY(writeState(fx.statePath, kFakeNow - kDailyMs / 2));  // ran 12 h ago
        ScheduledBackup service(&fx.jobs, fx.statePath, fx.hooks(), Fixture::fast());
        QSignalSpy finished(&service, &ScheduledBackup::finished);
        service.start();
        QTest::qWait(300);
        QCOMPARE(finished.size(), 0);
        QCOMPARE(zipCount(fx.defaultRoot, "W"), 0);

        fx.schedule = Schedule::Off;
        QVERIFY(writeState(fx.statePath, 0));
        ScheduledBackup off(&fx.jobs, fx.statePath, fx.hooks(), Fixture::fast());
        QSignalSpy offFinished(&off, &ScheduledBackup::finished);
        off.start();
        QTest::qWait(300);
        QCOMPARE(offFinished.size(), 0);
        QCOMPARE(zipCount(fx.defaultRoot, "W"), 0);
    }

    void weeklyWaitsSevenDays()
    {
        Fixture fx;
        fx.schedule = Schedule::Weekly;
        makeWorld(fx.saves, "W", QDateTime::currentDateTime());
        QVERIFY(writeState(fx.statePath, kFakeNow - 3 * kDailyMs));
        ScheduledBackup service(&fx.jobs, fx.statePath, fx.hooks(), Fixture::fast());
        QSignalSpy finished(&service, &ScheduledBackup::finished);
        service.start();
        QTest::qWait(300);
        QCOMPARE(finished.size(), 0);
    }

    void neverStartsWhileAGameRuns()
    {
        Fixture fx;
        makeWorld(fx.saves, "W", QDateTime::currentDateTime());
        bool playing = true;
        fx.gameActive = [&playing]() { return playing; };
        ScheduledBackup service(&fx.jobs, fx.statePath, fx.hooks(), Fixture::fast());
        QSignalSpy finished(&service, &ScheduledBackup::finished);
        service.start();
        QTest::qWait(300);
        QCOMPARE(finished.size(), 0);  // the checks keep coming, none starts a run
        QVERIFY(!service.isRunning());
        QCOMPARE(zipCount(fx.defaultRoot, "W"), 0);
        QCOMPARE(readState(fx.statePath), qint64(-1));

        playing = false;  // the next check runs it
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        QCOMPARE(finished.first().at(0).toInt(), 1);
        QCOMPARE(zipCount(fx.defaultRoot, "W"), 1);
    }

    void aGameStartingMeanwhileInterruptsTheRunWithoutRecordingIt()
    {
        Fixture fx;
        makeWorld(fx.saves, "W", QDateTime::currentDateTime());
        int calls = 0;
        // 1st call = the check that starts the run (no game), 2nd = before the first instance's queue (a game started), then none
        fx.gameActive = [&calls]() { return ++calls == 2; };
        ScheduledBackup service(&fx.jobs, fx.statePath, fx.hooks(), Fixture::fast());
        qint64 lastRunAtFirstFinish = -1;
        QSignalSpy finished(&service, &ScheduledBackup::finished);
        connect(&service, &ScheduledBackup::finished, this, [&]() {
            if (finished.size() == 1)
                lastRunAtFirstFinish = service.lastRunMs();
        });
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(finished.size() >= 1, 30000);
        QCOMPARE(finished.first().at(0).toInt(), 0);
        QCOMPARE(lastRunAtFirstFinish, qint64(0));  // interrupted: not recorded
        QVERIFY(!WorldBackupJobs::isInstanceLocked("inst"));

        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 30000);  // the next check finished the job
        QCOMPARE(finished.at(1).at(0).toInt(), 1);
        QCOMPARE(service.lastRunMs(), kFakeNow);
        QCOMPARE(zipCount(fx.defaultRoot, "W"), 1);
    }

    void aRefusedJobCountsAsFailedAndTheRunIsStillRecorded()
    {
        Fixture fx;
        makeWorld(fx.saves, "A", QDateTime::currentDateTime());
        makeWorld(fx.saves, "B", QDateTime::currentDateTime());
        fx.instanceRuns = true;  // the job runner refuses: the instance runs
        ScheduledBackup service(&fx.jobs, fx.statePath, fx.hooks(), Fixture::fast());
        QSignalSpy finished(&service, &ScheduledBackup::finished);
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        QCOMPARE(finished.first().at(0).toInt(), 0);
        QCOMPARE(finished.first().at(1).toInt(), 2);
        QVERIFY(!finished.first().at(3).toString().isEmpty());
        QCOMPARE(service.lastRunMs(), kFakeNow);
        QVERIFY(!WorldBackupJobs::isInstanceLocked("inst"));
    }

    void anUnreachableDestinationIsReportedInTheSummary()
    {
        Fixture fx;
        makeWorld(fx.saves, "W", QDateTime::currentDateTime());
        fx.chosenFolder = fx.tmp.filePath("unplugged/Cloud");
        ScheduledBackup service(&fx.jobs, fx.statePath, fx.hooks(), Fixture::fast());
        QSignalSpy finished(&service, &ScheduledBackup::finished);
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        QCOMPARE(finished.first().at(0).toInt(), 1);
        QCOMPARE(finished.first().at(1).toInt(), 0);
        QCOMPARE(finished.first().at(4).toString(), QDir::cleanPath(fx.chosenFolder));
        QCOMPARE(zipCount(fx.defaultRoot, "W"), 1);  // the backup is safe in the ZERO folder
        const QString text = summaryText(1, 0, 0, {}, finished.first().at(4).toString());
        QVERIFY(text.contains("isn't reachable"));
    }

    void backupsGoToTheChosenFolderWhenItWorks()
    {
        Fixture fx;
        makeWorld(fx.saves, "W", QDateTime::currentDateTime());
        QVERIFY(QDir().mkpath(fx.tmp.filePath("cloud")));
        fx.chosenFolder = fx.tmp.filePath("cloud");
        ScheduledBackup service(&fx.jobs, fx.statePath, fx.hooks(), Fixture::fast());
        QSignalSpy finished(&service, &ScheduledBackup::finished);
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        QCOMPARE(finished.first().at(0).toInt(), 1);
        QVERIFY(finished.first().at(4).toString().isEmpty());
        QCOMPARE(zipCount(fx.tmp.filePath("cloud/ZERO Backups"), "W"), 1);
        QCOMPARE(zipCount(fx.defaultRoot, "W"), 0);
    }
};

QTEST_GUILESS_MAIN(ScheduledBackupTest)

#include "ScheduledBackup_test.moc"
