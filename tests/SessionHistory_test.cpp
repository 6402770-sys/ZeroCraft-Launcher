// SPDX-License-Identifier: GPL-3.0-only
// SessionHistory: launch classification, recorder sequences, persistence, cap, corrupt / missing / malformed files.
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "zero/history/SessionHistory.h"

using namespace Zero::History;

Q_DECLARE_METATYPE(Outcome)

namespace {

struct FakeClock {
    qint64 value = 1000;
    SessionHistory::Clock fn() { return [this]() { return value; }; }
};

QByteArray readAll(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void writeAll(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

Session make(const QString& id, qint64 started, qint64 ended, Outcome o = Outcome::Played)
{
    Session s;
    s.instanceId = id;
    s.instanceName = id + QStringLiteral(" name");
    s.startedMs = started;
    s.endedMs = ended;
    s.outcome = o;
    return s;
}

}  // namespace

class SessionHistoryTest : public QObject {
    Q_OBJECT
   private slots:
    void classify_data()
    {
        QTest::addColumn<bool>("success");
        QTest::addColumn<bool>("aborted");
        QTest::addColumn<bool>("started");
        QTest::addColumn<bool>("recorded");
        QTest::addColumn<Outcome>("outcome");
        QTest::newRow("played") << true << false << true << true << Outcome::Played;
        QTest::newRow("stopped") << false << true << true << true << Outcome::Stopped;
        QTest::newRow("crashed") << false << false << true << true << Outcome::Crashed;
        QTest::newRow("cancelled in preparation") << false << true << false << false << Outcome::Played;
        QTest::newRow("failed") << false << false << false << true << Outcome::Failed;
        QTest::newRow("success without game") << true << false << false << false << Outcome::Played;
    }
    void classify()
    {
        QFETCH(bool, success);
        QFETCH(bool, aborted);
        QFETCH(bool, started);
        QFETCH(bool, recorded);
        QFETCH(Outcome, outcome);
        const auto result = Zero::History::classify(success, aborted, started);
        QCOMPARE(result.has_value(), recorded);
        if (recorded)
            QCOMPARE(*result, outcome);
    }

    void outcomeStrings()
    {
        for (Outcome o : { Outcome::Played, Outcome::Stopped, Outcome::Crashed, Outcome::Failed })
            QCOMPARE(outcomeFromString(outcomeToString(o)), std::optional<Outcome>(o));
        QVERIFY(!outcomeFromString(QStringLiteral("exploded")));
    }

    void recorderCrash()
    {
        QTemporaryDir dir;
        FakeClock clock;
        SessionHistory h(dir.filePath("zero/session-history.json"), clock.fn(), [](const QString&) { return QStringLiteral("Pack"); });
        QSignalSpy spy(&h, &SessionHistory::changed);
        clock.value = 1000;
        h.onPhaseChanged("a", Phase::Preparing);
        clock.value = 5000;
        h.onPhaseChanged("a", Phase::Running);
        clock.value = 65000;
        h.onStoppedUnexpectedly("a", -1);
        clock.value = 65100;
        h.onLaunchEnded("a", false, false, true);
        h.onPhaseChanged("a", Phase::Idle);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(h.sessions().size(), 1);
        const Session& s = h.sessions().first();
        QCOMPARE(s.outcome, Outcome::Crashed);
        QCOMPARE(s.exitCode, std::optional<int>(-1));
        QCOMPARE(s.startedMs, qint64(5000));
        QCOMPARE(s.endedMs, qint64(65100));
        QCOMPARE(s.durationMs(), qint64(60100));
        QCOMPARE(s.instanceName, QStringLiteral("Pack"));
    }

    void recorderPlayedAndStopped()
    {
        QTemporaryDir dir;
        FakeClock clock;
        SessionHistory h(dir.filePath("h.json"), clock.fn());
        h.onPhaseChanged("a", Phase::Preparing);
        clock.value = 2000;
        h.onPhaseChanged("a", Phase::Running);
        clock.value = 9000;
        h.onLaunchEnded("a", true, false, true);
        QCOMPARE(h.sessions().first().outcome, Outcome::Played);
        QVERIFY(!h.sessions().first().exitCode);
        QCOMPARE(h.sessions().first().instanceName, QStringLiteral("a"));  // no lookup: the id

        h.onPhaseChanged("a", Phase::Preparing);
        h.onPhaseChanged("a", Phase::Running);
        h.onLaunchEnded("a", false, true, true);
        QCOMPARE(h.sessions().size(), 2);
        QCOMPARE(h.sessions().first().outcome, Outcome::Stopped);
    }

    void recorderCancelledInPreparation()
    {
        QTemporaryDir dir;
        FakeClock clock;
        SessionHistory h(dir.filePath("h.json"), clock.fn());
        h.onPhaseChanged("a", Phase::Preparing);
        clock.value = 3000;
        h.onLaunchEnded("a", false, true, false);
        QVERIFY(h.sessions().isEmpty());
        QVERIFY(!QFile::exists(dir.filePath("h.json")));
    }

    void recorderFailed()
    {
        QTemporaryDir dir;
        FakeClock clock;
        SessionHistory h(dir.filePath("h.json"), clock.fn());
        clock.value = 1500;
        h.onPhaseChanged("a", Phase::Preparing);
        clock.value = 4500;
        h.onLaunchEnded("a", false, false, false);
        QCOMPARE(h.sessions().size(), 1);
        QCOMPARE(h.sessions().first().outcome, Outcome::Failed);
        QCOMPARE(h.sessions().first().startedMs, qint64(1500));
        QCOMPARE(h.sessions().first().endedMs, qint64(4500));
    }

    void recorderPendingClearedBetweenLaunches()
    {
        QTemporaryDir dir;
        FakeClock clock;
        SessionHistory h(dir.filePath("h.json"), clock.fn());
        h.onPhaseChanged("a", Phase::Preparing);
        h.onPhaseChanged("a", Phase::Running);
        h.onStoppedUnexpectedly("a", 7);
        clock.value = 2000;
        h.onLaunchEnded("a", false, false, true);
        // the next launch fails before the game starts: no leftover exit code or start time
        clock.value = 3000;
        h.onPhaseChanged("a", Phase::Preparing);
        clock.value = 3500;
        h.onLaunchEnded("a", false, false, false);
        QCOMPARE(h.sessions().first().outcome, Outcome::Failed);
        QCOMPARE(h.sessions().first().startedMs, qint64(3000));
        QVERIFY(!h.sessions().first().exitCode);
    }

    void recorderInterleaved()
    {
        QTemporaryDir dir;
        FakeClock clock;
        SessionHistory h(dir.filePath("h.json"), clock.fn());
        clock.value = 100;
        h.onPhaseChanged("a", Phase::Preparing);
        clock.value = 200;
        h.onPhaseChanged("b", Phase::Preparing);
        clock.value = 300;
        h.onPhaseChanged("a", Phase::Running);
        clock.value = 400;
        h.onPhaseChanged("b", Phase::Running);
        clock.value = 500;
        h.onStoppedUnexpectedly("b", 3);
        clock.value = 600;
        h.onLaunchEnded("b", false, false, true);
        clock.value = 700;
        h.onLaunchEnded("a", true, false, true);
        QCOMPARE(h.sessions().size(), 2);
        QCOMPARE(h.sessions().at(0).instanceId, QStringLiteral("a"));
        QCOMPARE(h.sessions().at(0).outcome, Outcome::Played);
        QCOMPARE(h.sessions().at(0).startedMs, qint64(300));
        QVERIFY(!h.sessions().at(0).exitCode);
        QCOMPARE(h.sessions().at(1).instanceId, QStringLiteral("b"));
        QCOMPARE(h.sessions().at(1).outcome, Outcome::Crashed);
        QCOMPARE(h.sessions().at(1).exitCode, std::optional<int>(3));
        QCOMPARE(h.sessions().at(1).startedMs, qint64(400));
    }

    void persistenceRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("zero/session-history.json");
        {
            SessionHistory h(path);
            Session crashed = make("a", 1000, 2000, Outcome::Crashed);
            crashed.exitCode = -1;
            h.record(make("b", 10, 20, Outcome::Played));
            h.record(crashed);
        }
        SessionHistory h(path);
        QCOMPARE(h.sessions().size(), 2);
        QCOMPARE(h.sessions().at(0).instanceId, QStringLiteral("a"));
        QCOMPARE(h.sessions().at(0).instanceName, QStringLiteral("a name"));
        QCOMPARE(h.sessions().at(0).startedMs, qint64(1000));
        QCOMPARE(h.sessions().at(0).endedMs, qint64(2000));
        QCOMPARE(h.sessions().at(0).outcome, Outcome::Crashed);
        QCOMPARE(h.sessions().at(0).exitCode, std::optional<int>(-1));
        QCOMPARE(h.sessions().at(1).instanceId, QStringLiteral("b"));
        QVERIFY(!h.sessions().at(1).exitCode);
        const QByteArray raw = readAll(path);
        QVERIFY(raw.contains("\"version\": 1"));
        QVERIFY(raw.contains("\"outcome\": \"crashed\""));
    }

    void capAt500()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("h.json");
        {
            SessionHistory h(path);
            for (int i = 0; i < kMaxSessions + 5; ++i)
                h.record(make("x", i, i + 1));
            QCOMPARE(h.sessions().size(), kMaxSessions);
            QCOMPARE(h.sessions().first().startedMs, qint64(kMaxSessions + 4));
            QCOMPARE(h.sessions().last().startedMs, qint64(5));  // the 5 oldest were dropped
        }
        SessionHistory h(path);
        QCOMPARE(h.sessions().size(), kMaxSessions);
    }

    void missingFileIsEmpty()
    {
        QTemporaryDir dir;
        SessionHistory h(dir.filePath("nope/h.json"));
        QVERIFY(h.sessions().isEmpty());
    }

    void corruptFileIsMovedAside()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("h.json");
        writeAll(path, "{ this is not json");
        writeAll(path + ".corrupt", "older corrupt copy");
        SessionHistory h(path);
        QVERIFY(h.sessions().isEmpty());
        QVERIFY(!QFile::exists(path));
        QCOMPARE(readAll(path + ".corrupt"), QByteArray("{ this is not json"));
        // the next record starts a fresh file
        h.record(make("a", 1, 2));
        QVERIFY(QFile::exists(path));
        QCOMPARE(readAll(path + ".corrupt"), QByteArray("{ this is not json"));
    }

    void wrongShapeIsCorrupt()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("h.json");
        writeAll(path, "[1, 2, 3]");
        SessionHistory h(path);
        QVERIFY(h.sessions().isEmpty());
        QVERIFY(QFile::exists(path + ".corrupt"));
    }

    void malformedEntriesAreSkipped()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("h.json");
        writeAll(path, R"({ "version": 1, "sessions": [
            { "instanceId": "ok", "instanceName": "Ok", "started": 1, "ended": 2, "outcome": "played" },
            { "instanceId": "bad-outcome", "started": 1, "ended": 2, "outcome": "exploded" },
            { "instanceId": "no-times", "outcome": "played" },
            { "instanceName": "no id", "started": 1, "ended": 2, "outcome": "played" },
            "not an object",
            { "instanceId": "ok2", "instanceName": "Ok2", "started": 3, "ended": 4, "outcome": "crashed", "exitCode": 5 }
        ] })");
        SessionHistory h(path);
        QCOMPARE(h.sessions().size(), 2);
        QCOMPARE(h.sessions().at(0).instanceId, QStringLiteral("ok"));
        QCOMPARE(h.sessions().at(1).instanceId, QStringLiteral("ok2"));
        QCOMPARE(h.sessions().at(1).exitCode, std::optional<int>(5));
        QVERIFY(!QFile::exists(path + ".corrupt"));
    }
};

QTEST_GUILESS_MAIN(SessionHistoryTest)
#include "SessionHistory_test.moc"
