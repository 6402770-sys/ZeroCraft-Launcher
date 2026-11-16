// SPDX-License-Identifier: GPL-3.0-only
// PlayTracker: running sessions from the launch signals (fake clock / names / pids), how sessions end (played, stopped, crashed),
// the clock format, and the Now playing card (live time, memory line omitted when unreadable, timer only while visible).
#include <QApplication>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/pages/NowPlayingCard.h"
#include "zero/session/PlayTracker.h"

using namespace Zero;
using namespace Zero::Session;
using History::Phase;

class PlayTrackerTest : public QObject {
    Q_OBJECT

    struct Fixture {
        qint64 now = 1'000'000;
        PlayTracker tracker;
        Fixture()
            : tracker([this]() { return now; }, [](const QString& id) { return id == QLatin1String("a") ? QStringLiteral("Alpha") : QString(); },
                      [](const QString& id) -> qint64 { return id == QLatin1String("a") ? 4242 : -1; })
        {
        }
    };

   private slots:
    void initTestCase() { Theme::setCurrent(ThemeSpec::obsidian()); }

    void formatClock()
    {
        QCOMPARE(Session::formatClock(0), QStringLiteral("00:00"));
        QCOMPARE(Session::formatClock(9), QStringLiteral("00:09"));
        QCOMPARE(Session::formatClock(724), QStringLiteral("12:04"));
        QCOMPARE(Session::formatClock(3599), QStringLiteral("59:59"));
        QCOMPARE(Session::formatClock(3600), QStringLiteral("1:00:00"));
        QCOMPARE(Session::formatClock(4833), QStringLiteral("1:20:33"));
        QCOMPARE(Session::formatClock(-5), QStringLiteral("00:00"));
    }

    void startsAtRunningNotPreparing()
    {
        Fixture f;
        QSignalSpy changed(&f.tracker, &PlayTracker::runningChanged);
        f.tracker.onPhaseChanged(QStringLiteral("a"), Phase::Preparing);
        QVERIFY(!f.tracker.isRunning(QStringLiteral("a")));
        QCOMPARE(changed.count(), 0);
        f.tracker.onPhaseChanged(QStringLiteral("a"), Phase::Running);
        QVERIFY(f.tracker.isRunning(QStringLiteral("a")));
        QCOMPARE(changed.count(), 1);
        const auto s = f.tracker.running().value(QStringLiteral("a"));
        QCOMPARE(s.instanceName, QStringLiteral("Alpha"));
        QCOMPARE(s.startedMs, qint64(1'000'000));
        QCOMPARE(s.pid, qint64(4242));
        // a second Running for the same instance changes nothing
        f.tracker.onPhaseChanged(QStringLiteral("a"), Phase::Running);
        QCOMPARE(changed.count(), 1);
    }

    void unknownNameFallsBackToIdAndUnknownPidIsZero()
    {
        Fixture f;
        f.tracker.onPhaseChanged(QStringLiteral("zzz"), Phase::Running);
        const auto s = f.tracker.running().value(QStringLiteral("zzz"));
        QCOMPARE(s.instanceName, QStringLiteral("zzz"));
        QCOMPARE(s.pid, qint64(0));
    }

    void endedSessionCarriesTimes()
    {
        Fixture f;
        QSignalSpy ended(&f.tracker, &PlayTracker::sessionEnded);
        QSignalSpy all(&f.tracker, &PlayTracker::allEnded);
        f.tracker.onPhaseChanged(QStringLiteral("a"), Phase::Running);
        f.now += 125'000;
        f.tracker.onLaunchEnded(QStringLiteral("a"), true, false, true);
        QCOMPARE(ended.count(), 1);
        const auto e = qvariant_cast<EndedSession>(ended.first().first());
        QCOMPARE(e.durationMs(), qint64(125'000));
        QVERIFY(!e.crashed);
        QVERIFY(!e.stoppedByUser);
        QCOMPARE(all.count(), 1);
        QVERIFY(f.tracker.running().isEmpty());
    }

    void crashAndStopAreTold()
    {
        Fixture f;
        QSignalSpy ended(&f.tracker, &PlayTracker::sessionEnded);
        // crash: exit code reported first, then the launch ends as failed
        f.tracker.onPhaseChanged(QStringLiteral("a"), Phase::Running);
        f.tracker.onStoppedUnexpectedly(QStringLiteral("a"), 1);
        f.tracker.onLaunchEnded(QStringLiteral("a"), false, false, true);
        QVERIFY(qvariant_cast<EndedSession>(ended.at(0).first()).crashed);
        // failed without a crash report still counts as a crash when the game had started
        f.tracker.onPhaseChanged(QStringLiteral("a"), Phase::Running);
        f.tracker.onLaunchEnded(QStringLiteral("a"), false, false, true);
        QVERIFY(qvariant_cast<EndedSession>(ended.at(1).first()).crashed);
        // stopped by the user is not a crash
        f.tracker.onPhaseChanged(QStringLiteral("a"), Phase::Running);
        f.tracker.onLaunchEnded(QStringLiteral("a"), false, true, true);
        const auto stopped = qvariant_cast<EndedSession>(ended.at(2).first());
        QVERIFY(!stopped.crashed);
        QVERIFY(stopped.stoppedByUser);
        // a crash flag does not leak into the next session
        f.tracker.onPhaseChanged(QStringLiteral("a"), Phase::Running);
        f.tracker.onLaunchEnded(QStringLiteral("a"), true, false, true);
        QVERIFY(!qvariant_cast<EndedSession>(ended.at(3).first()).crashed);
    }

    void launchThatNeverRanEmitsNothing()
    {
        Fixture f;
        QSignalSpy ended(&f.tracker, &PlayTracker::sessionEnded);
        f.tracker.onPhaseChanged(QStringLiteral("a"), Phase::Preparing);
        f.tracker.onLaunchEnded(QStringLiteral("a"), false, true, false);
        QCOMPARE(ended.count(), 0);
    }

    void allEndedOnlyAfterTheLastOne()
    {
        Fixture f;
        QSignalSpy all(&f.tracker, &PlayTracker::allEnded);
        f.tracker.onPhaseChanged(QStringLiteral("a"), Phase::Running);
        f.tracker.onPhaseChanged(QStringLiteral("b"), Phase::Running);
        QCOMPARE(f.tracker.running().size(), 2);
        f.tracker.onLaunchEnded(QStringLiteral("a"), true, false, true);
        QCOMPARE(all.count(), 0);
        f.tracker.onLaunchEnded(QStringLiteral("b"), true, false, true);
        QCOMPARE(all.count(), 1);
    }

    // ----- Now playing card -----

    void cardFormatsLiveTime()
    {
        qint64 now = 10'000;
        NowPlayingCard card(QStringLiteral("a"), QStringLiteral("Alpha"), 10'000, 0, [&now]() { return now; }, {});
        QCOMPARE(card.elapsedText(), QStringLiteral("00:00"));
        now += 724'000;
        QCOMPARE(card.elapsedText(), QStringLiteral("12:04"));
        now += (4833 - 724) * 1000LL;
        QCOMPARE(card.elapsedText(), QStringLiteral("1:20:33"));
        card.tick();
        QCOMPARE(card.metaText(), QStringLiteral("1:20:33"));  // no pid: no memory line
    }

    void cardShowsMemoryOnlyWhenReadable()
    {
        qint64 now = 0;
        bool readable = true;
        auto memory = [&readable](qint64 pid) -> std::optional<qint64> {
            if (!readable || pid != 77)
                return std::nullopt;
            return qint64(2'254'857'830);  // 2.1 GB
        };
        NowPlayingCard card(QStringLiteral("a"), QStringLiteral("Alpha"), 0, 77, [&now]() { return now; }, memory);
        QCOMPARE(card.memoryText(), QStringLiteral("2.1 GB"));
        QVERIFY(card.metaText().contains(QStringLiteral("2.1 GB")));
        readable = false;
        card.tick();
        QVERIFY(card.memoryText().isEmpty());
        QVERIFY(!card.metaText().contains(QStringLiteral("GB")));
    }

    void cardTimerRunsOnlyWhileVisible()
    {
        qint64 now = 0;
        NowPlayingCard card(QStringLiteral("a"), QStringLiteral("Alpha"), 0, 0, [&now]() { return now; }, {});
        QVERIFY(!card.timerActive());
        card.show();
        QVERIFY(card.timerActive());
        card.hide();
        QVERIFY(!card.timerActive());
    }

    void cardButtonsEmitTheirInstance()
    {
        NowPlayingCard card(QStringLiteral("a"), QStringLiteral("Alpha"), 0, 0, []() { return qint64(0); }, {});
        QSignalSpy shots(&card, &NowPlayingCard::screenshotsRequested);
        QSignalSpy folder(&card, &NowPlayingCard::folderRequested);
        QSignalSpy stop(&card, &NowPlayingCard::stopRequested);
        const auto buttons = card.findChildren<QPushButton*>();
        QCOMPARE(buttons.size(), 3);
        for (auto* b : buttons)
            b->click();
        QCOMPARE(shots.count(), 1);
        QCOMPARE(folder.count(), 1);
        QCOMPARE(stop.count(), 1);
        QCOMPARE(stop.first().first().toString(), QStringLiteral("a"));
    }
};

QTEST_MAIN(PlayTrackerTest)
#include "PlayTracker_test.moc"
