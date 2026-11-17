// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the playtime statistics (zero/history/PlaytimeStats): day buckets (midnight split, empty days), the 7 / 30 day
// windows, the longest session, the by-instance order and shares, and the duration text. Local calendar days, "now" injected.
#include <QTest>

#include "zero/history/PlaytimeStats.h"

using namespace Zero::History;
using namespace Zero::History::Playtime;

namespace {

constexpr qint64 kMin = 60LL * 1000;
constexpr qint64 kHour = 60 * kMin;

qint64 at(int y, int m, int d, int hh, int mm = 0)
{
    return QDateTime(QDate(y, m, d), QTime(hh, mm)).toMSecsSinceEpoch();
}

Session session(const QString& name, qint64 start, qint64 end)
{
    Session s;
    s.instanceId = name.toLower();
    s.instanceName = name;
    s.startedMs = start;
    s.endedMs = end;
    return s;
}

const QDateTime kNow(QDate(2026, 11, 15), QTime(18, 0));

}  // namespace

class PlaytimeStatsTest : public QObject {
    Q_OBJECT
   private slots:
    void daysAreOldestFirstAndEmptyDaysExist()
    {
        const auto d = days({}, kNow, 14);
        QCOMPARE(d.size(), 14);
        QCOMPARE(d.first().date, QDate(2026, 11, 2));
        QCOMPARE(d.last().date, QDate(2026, 11, 15));
        for (const Day& day : d) {
            QCOMPARE(day.ms, qint64(0));
            QCOMPARE(day.sessionCount, 0);
        }
    }

    void sessionsLandOnTheirDay()
    {
        const QList<Session> list = { session("A", at(2026, 11, 15, 10), at(2026, 11, 15, 11, 30)),
                                      session("A", at(2026, 11, 15, 14), at(2026, 11, 15, 14, 45)),
                                      session("B", at(2026, 11, 13, 20), at(2026, 11, 13, 21)) };
        const auto d = days(list, kNow, 14);
        QCOMPARE(d.last().ms, 135 * kMin);
        QCOMPARE(d.last().sessionCount, 2);
        QCOMPARE(d.at(11).date, QDate(2026, 11, 13));
        QCOMPARE(d.at(11).ms, kHour);
        QCOMPARE(d.at(11).sessionCount, 1);
        QCOMPARE(d.at(12).ms, qint64(0));
    }

    void sessionCrossingMidnightIsSplit()
    {
        const QList<Session> list = { session("A", at(2026, 11, 14, 23), at(2026, 11, 15, 1, 30)) };
        const auto d = days(list, kNow, 14);
        QCOMPARE(d.at(12).date, QDate(2026, 11, 14));
        QCOMPARE(d.at(12).ms, kHour);
        QCOMPARE(d.at(13).ms, 90 * kMin);
        QCOMPARE(d.at(12).sessionCount, 1);
        QCOMPARE(d.at(13).sessionCount, 1);
        QCOMPARE(d.at(12).ms + d.at(13).ms, 150 * kMin);
    }

    void sessionEndingExactlyAtMidnightStaysOnItsDay()
    {
        const QList<Session> list = { session("A", at(2026, 11, 14, 23), at(2026, 11, 15, 0)) };
        const auto d = days(list, kNow, 14);
        QCOMPARE(d.at(12).ms, kHour);
        QCOMPARE(d.at(13).ms, qint64(0));
        QCOMPARE(d.at(13).sessionCount, 0);
    }

    void sessionsOutsideTheWindowAreIgnoredAndTheEdgeIsClipped()
    {
        const QList<Session> list = { session("old", at(2026, 10, 1, 10), at(2026, 10, 1, 12)),
                                      session("edge", at(2026, 11, 1, 23), at(2026, 11, 2, 1)) };
        const auto d = days(list, kNow, 14);
        QCOMPARE(d.first().date, QDate(2026, 11, 2));
        QCOMPARE(d.first().ms, kHour);  // only the part after midnight
        qint64 sum = 0;
        for (const Day& day : d)
            sum += day.ms;
        QCOMPARE(sum, kHour);
    }

    void zeroLengthSessionCountsOnItsDay()
    {
        const auto d = days({ session("A", at(2026, 11, 15, 9), at(2026, 11, 15, 9)) }, kNow, 14);
        QCOMPARE(d.last().sessionCount, 1);
        QCOMPARE(d.last().ms, qint64(0));
    }

    void weekAndThirtyDayWindows()
    {
        const QList<Session> list = { session("A", at(2026, 11, 15, 10), at(2026, 11, 15, 11)),    // today
                                      session("A", at(2026, 11, 9, 10), at(2026, 11, 9, 12)),      // 6 days ago: inside the week
                                      session("A", at(2026, 11, 8, 10), at(2026, 11, 8, 13)),      // 7 days ago: outside
                                      session("A", at(2026, 10, 17, 10), at(2026, 10, 17, 11)),    // 29 days ago: inside the 30
                                      session("A", at(2026, 10, 16, 10), at(2026, 10, 16, 11)) };  // 30 days ago: outside
        QCOMPARE(thisWeekMs(list, kNow), 3 * kHour);
        QCOMPARE(sessionsLast30(list, kNow), 4);
        QCOMPARE(sessionsLast30({}, kNow), 0);
    }

    void longestSessionIsFound()
    {
        QVERIFY(!Playtime::longestSession({}).valid());
        const QList<Session> list = { session("A", at(2026, 11, 15, 10), at(2026, 11, 15, 11)),
                                      session("Fabric Fun", at(2026, 11, 12, 10), at(2026, 11, 12, 13, 12)),
                                      session("B", at(2026, 11, 13, 10), at(2026, 11, 13, 12)) };
        const Longest l = Playtime::longestSession(list);
        QVERIFY(l.valid());
        QCOMPARE(l.ms, 192 * kMin);
        QCOMPARE(l.instanceName, QStringLiteral("Fabric Fun"));
        QCOMPARE(l.date, QDate(2026, 11, 12));
    }

    void allTimeAndByInstance()
    {
        const QList<InstanceTotal> totals = { { "a", "Alpha", 3600, 0 }, { "z", "Zeta", 7200, 5 }, { "n", "Never", 0, 0 }, { "b", "Beta", 3600, 9 } };
        QCOMPARE(allTimeMs(totals), qint64(14400) * 1000);
        const auto rows = byInstance(totals);
        QCOMPARE(rows.size(), 3);  // zero totals are left out
        QCOMPARE(rows.at(0).instance.name, QStringLiteral("Zeta"));
        QCOMPARE(rows.at(1).instance.name, QStringLiteral("Alpha"));  // ties by name
        QCOMPARE(rows.at(2).instance.name, QStringLiteral("Beta"));
        QCOMPARE(rows.at(0).share, 0.5);
        QCOMPARE(rows.at(1).share, 0.25);
        QVERIFY(byInstance({}).isEmpty());
        QCOMPARE(allTimeMs({}), qint64(0));
    }

    void formatting_data()
    {
        QTest::addColumn<qint64>("ms");
        QTest::addColumn<QString>("text");
        QTest::newRow("zero") << qint64(0) << "0 min";
        QTest::newRow("negative") << qint64(-5) << "0 min";
        QTest::newRow("under a minute") << qint64(59 * 1000) << "< 1 min";
        QTest::newRow("one minute") << qint64(60 * 1000) << "1 min";
        QTest::newRow("45 min") << 45 * kMin << "45 min";
        QTest::newRow("exact hours") << 3 * kHour << "3 h";
        QTest::newRow("2 h 15") << 135 * kMin << "2 h 15 min";
        QTest::newRow("big") << (34 * kHour + 12 * kMin) << "34 h 12 min";
    }
    void formatting()
    {
        QFETCH(qint64, ms);
        QFETCH(QString, text);
        QCOMPARE(formatDuration(ms), text);
    }

    void niceStep()
    {
        QCOMPARE(niceStepMs(0), 30 * kMin);
        QCOMPARE(niceStepMs(90 * kMin), 30 * kMin);
        QCOMPARE(niceStepMs(91 * kMin), kHour);
        QCOMPARE(niceStepMs(3 * kHour), kHour);
        QCOMPARE(niceStepMs(5 * kHour), 2 * kHour);
        QCOMPARE(niceStepMs(7 * kHour), 4 * kHour);
    }
};

QTEST_GUILESS_MAIN(PlaytimeStatsTest)

#include "PlaytimeStats_test.moc"
