// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of Tools > Playtime (ui/zero/pages/PlaytimeView): tiles, the 14 bars with their tooltips and accessible text, the
// by-instance list (top 10, "Show all", click), the empty state and the refresh on SessionHistory::changed. Injected data sources, so
// neither APPLICATION nor the real history is read.
//
// With the environment variable ZERO_PLAYTIME_SHOTS set to a folder, savesScreenshots() writes PNG grabs of the page there.
#include <QApplication>
#include <QDir>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "ui/zero/ZeroTheme.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/pages/PlaytimeView.h"
#include "zero/history/SessionHistory.h"

using namespace Zero;
using namespace Zero::History;
using namespace Zero::History::Playtime;

namespace {

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

const QDateTime kNow(QDate(2026, 11, 15), QTime(18, 0));  // a Sunday

struct Data {
    QList<Session> sessions;
    QList<InstanceTotal> totals;
};

PlaytimeView::Sources sourcesOf(Data* data)
{
    PlaytimeView::Sources s;
    s.sessions = [data]() { return data->sessions; };
    s.totals = [data]() { return data->totals; };
    s.now = []() { return kNow; };
    s.icon = [](const QString&) { return QIcon(); };
    return s;
}

Data sample(int instances = 3)
{
    Data d;
    d.sessions = { session("Fabric Fun", at(2026, 11, 15, 10), at(2026, 11, 15, 12, 15)),  // 2 h 15 min today
                   session("Fabric Fun", at(2026, 11, 12, 20), at(2026, 11, 12, 23, 12)),  // 3 h 12 min, the longest
                   session("Vanilla", at(2026, 11, 12, 9), at(2026, 11, 12, 10)),
                   session("Vanilla", at(2026, 11, 9, 18), at(2026, 11, 9, 19, 40)),
                   session("Skyblock", at(2026, 11, 5, 18), at(2026, 11, 5, 18, 30)) };
    for (int i = 0; i < instances; ++i) {
        InstanceTotal t;
        t.id = QStringLiteral("inst%1").arg(i);
        t.name = i == 0 ? QStringLiteral("Fabric Fun") : i == 1 ? QStringLiteral("Vanilla") : QStringLiteral("Instance %1").arg(i);
        t.totalSeconds = (instances - i) * 3600 + 600;
        t.lastPlayedMs = kNow.addDays(-i * 2 - 1).toMSecsSinceEpoch();
        d.totals.append(t);
    }
    return d;
}

}  // namespace

class ZeroPlaytimeViewTest : public QObject {
    Q_OBJECT

   private slots:
    void init()
    {
        ThemeSpec spec = ThemeSpec::obsidian();
        spec.reduceMotion = true;
        Theme::setCurrent(spec);
    }
    void cleanup()
    {
        ThemeSpec spec = ThemeSpec::obsidian();
        spec.reduceMotion = false;
        Theme::setCurrent(spec);
    }

    void tilesShowTheNumbers()
    {
        Data data = sample();
        PlaytimeView view(sourcesOf(&data));
        view.resize(900, 700);
        view.show();
        QVERIFY(!view.isEmptyStateShown());
        const QStringList tiles = view.tileTexts();
        QCOMPARE(tiles.size(), 9);
        QCOMPARE(tiles.at(0), QStringLiteral("This week"));
        QCOMPARE(tiles.at(1), QStringLiteral("8 h 7 min"));  // 2:15 + 3:12 + 1:00 + 1:40 (the 9th is inside the last 7 days)
        QCOMPARE(tiles.at(3), QStringLiteral("All time"));
        QCOMPARE(tiles.at(4), QStringLiteral("6 h 30 min"));  // 3:10 + 2:10 + 1:10 from the Prism totals
        QCOMPARE(tiles.at(6), QStringLiteral("Sessions (30 days)"));
        QCOMPARE(tiles.at(7), QStringLiteral("5"));
        QCOMPARE(tiles.at(8), QStringLiteral("Longest: 3 h 12 min · Fabric Fun"));
    }

    void barsAndTooltips()
    {
        Data data = sample();
        PlaytimeView view(sourcesOf(&data));
        view.resize(900, 700);
        view.show();
        PlaytimeChart* chart = view.chart();
        QVERIFY(chart);
        QCOMPARE(chart->barCount(), 14);
        QCOMPARE(chart->height(), 180);
        QCOMPARE(chart->axisLabels().size(), 14);
        QCOMPARE(chart->axisLabels().last(), QStringLiteral("S"));  // Sunday
        QCOMPARE(chart->tooltipFor(13), QStringLiteral("Sun 15 Nov · 2 h 15 min · 1 session"));
        QCOMPARE(chart->tooltipFor(10), QStringLiteral("Thu 12 Nov · 4 h 12 min · 2 sessions"));
        QCOMPARE(chart->tooltipFor(0), QStringLiteral("Mon 2 Nov · 0 min · 0 sessions"));
        QCOMPARE(chart->tooltipFor(14), QString());
        QVERIFY(chart->accessibleDescription().contains(QStringLiteral("Thu 12 Nov · 4 h 12 min · 2 sessions")));
        QCOMPARE(chart->gridLabels(), QStringList({ QStringLiteral("2 h"), QStringLiteral("4 h"), QStringLiteral("6 h") }));
        QCOMPARE(chart->barAt(QPoint(5, 60)), 0);
        QCOMPARE(chart->barAt(QPoint(chart->width() - 10, 60)), -1);  // the label gutter
        // hover: a synthetic move event (the real cursor is not trusted)
        const QPointF local(chart->width() - 80, 60);
        QMouseEvent move(QEvent::MouseMove, local, chart->mapToGlobal(local), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(chart, &move);
        QCOMPARE(chart->hoveredBar(), chart->barAt(local.toPoint()));
        QVERIFY(chart->hoveredBar() >= 0);
    }

    void instanceRowsTopTenShowAllAndClick()
    {
        Data data = sample(13);
        PlaytimeView view(sourcesOf(&data));
        view.resize(900, 900);
        view.show();
        QCOMPARE(view.instanceRows().size(), 10);
        QVERIFY(view.instanceRows().first().startsWith(QStringLiteral("Fabric Fun\t")));
        QVERIFY(view.showAllButton());
        QCOMPARE(view.showAllButton()->text(), QStringLiteral("Show all (13)"));
        QSignalSpy spy(&view, &PlaytimeView::showInstanceRequested);
        view.clickRow(1);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toString(), QStringLiteral("inst1"));
        view.showAllButton()->click();
        QCOMPARE(view.instanceRows().size(), 13);
        QVERIFY(!view.showAllButton());

        Data few = sample(4);
        PlaytimeView small(sourcesOf(&few));
        small.show();
        QCOMPARE(small.instanceRows().size(), 4);
        QVERIFY(!small.showAllButton());
    }

    void emptyStateWhenNothingWasPlayed()
    {
        Data data;
        PlaytimeView view(sourcesOf(&data));
        view.show();
        QVERIFY(view.isEmptyStateShown());
        QVERIFY(!view.chart());
        data = sample();
        view.refresh();
        QVERIFY(!view.isEmptyStateShown());
        QVERIFY(view.chart());
    }

    void refreshesWhenTheHistoryChanges()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        SessionHistory history(QDir(dir.path()).filePath(QStringLiteral("h.json")));  // the first one created is global()
        Data data;
        PlaytimeView view(sourcesOf(&data));
        view.show();
        QVERIFY(view.isEmptyStateShown());
        data = sample();
        history.record(session("Fabric Fun", at(2026, 11, 15, 10), at(2026, 11, 15, 11)));
        QVERIFY(!view.isEmptyStateShown());
    }

    void savesScreenshots()
    {
        const QString dir = qEnvironmentVariable("ZERO_PLAYTIME_SHOTS");
        if (dir.isEmpty())
            QSKIP("ZERO_PLAYTIME_SHOTS is not set");
        QDir().mkpath(dir);
        ThemeSpec spec = ThemeSpec::obsidian();
        spec.reduceMotion = true;
        applySpecLive(spec);  // the ZERO stylesheet and palette, as in the application
        {
            Data data = sample(12);
            PlaytimeView view(sourcesOf(&data));
            view.resize(1000, 760);
            view.show();
            QTest::qWait(60);
            QVERIFY(view.grab().save(QDir(dir).filePath(QStringLiteral("playtime-page.png"))));
        }
        {
            Data data;
            PlaytimeView view(sourcesOf(&data));
            view.resize(1000, 520);
            view.show();
            QTest::qWait(60);
            QVERIFY(view.grab().save(QDir(dir).filePath(QStringLiteral("playtime-empty.png"))));
        }
    }
};

QTEST_MAIN(ZeroPlaytimeViewTest)

#include "ZeroPlaytimeView_test.moc"
