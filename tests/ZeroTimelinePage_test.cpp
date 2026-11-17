// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the Timeline page of the instance window (ui/zero/instance/TimelinePage): day groups and rows from fake sources, the
// Undo buttons (only the newest change of a kind, disabled with the reason while busy, the click calls the injected flow and reads again), "Show
// more" after 100 entries and the empty state. No application, no instance.
//
// With the environment variable ZERO_TIMELINE_SHOTS set to a folder, savesScreenshots() writes PNG grabs of the page there (both themes).
#include <QApplication>
#include <QDir>
#include <QPushButton>
#include <QTest>
#include <QTimeZone>

#include "ui/zero/ZeroTheme.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/instance/TimelinePage.h"

using namespace Zero;
using namespace Zero::TimelineUi;
using namespace Zero::History::Timeline;

namespace {

const QTimeZone kZone(QByteArray("UTC"));
const QDateTime kNow(QDate(2026, 11, 17), QTime(15, 0), kZone);

qint64 at(int d, int h, int m = 0)
{
    return QDateTime(QDate(2026, 11, d), QTime(h, m), kZone).toMSecsSinceEpoch();
}

Mods::Snapshot::Info modSnap(qint64 created, const QStringList& names)
{
    Mods::Snapshot::Info info;
    info.valid = true;
    info.dir = QStringLiteral("snap-") + QString::number(created);
    info.createdMs = created;
    info.outcome = QStringLiteral("ok");
    for (const QString& name : names) {
        Mods::Snapshot::ModEntry mod;
        mod.name = name;
        mod.oldVersion = QStringLiteral("1.0");
        mod.newVersion = QStringLiteral("2.0");
        mod.changed = true;
        info.mods.append(mod);
    }
    return info;
}

Sources sample()
{
    Sources s;
    s.instanceId = QStringLiteral("i1");
    s.packId = QStringLiteral("p1");
    s.modSnapshots = { modSnap(at(17, 9, 10), { QStringLiteral("Sodium"), QStringLiteral("Lithium"), QStringLiteral("Iris"), QStringLiteral("Fabric API"),
                                                 QStringLiteral("Mod Menu") }),
                       modSnap(at(14, 18), { QStringLiteral("Sodium") }) };
    Backup::Pack::Info pack;
    pack.valid = true;
    pack.zipPath = QStringLiteral("pack-1.zip");
    pack.createdMs = at(16, 20);
    pack.outcome = QStringLiteral("ok");
    pack.meta.packName = QStringLiteral("Fabulously Optimized");
    pack.meta.packId = QStringLiteral("p1");
    pack.meta.oldVersion = QStringLiteral("5.8.0");
    pack.meta.newVersion = QStringLiteral("5.9.1");
    s.packSnapshots = { pack };
    Content::Packs::HistoryEntry order;
    order.id = QStringLiteral("o1");
    order.time = QDateTime::fromMSecsSinceEpoch(at(16, 12), kZone);
    s.packOrder = { order };
    Downloads::HistoryEntry d;
    d.title = QStringLiteral("Faithful 32x");
    d.instanceId = QStringLiteral("i1");
    d.kind = Downloads::Kind::ResourcePack;
    d.bytes = 14 * 1024 * 1024;
    d.finishedAt = QDateTime::fromMSecsSinceEpoch(at(16, 11, 50), kZone);
    Downloads::HistoryEntry failed = d;
    failed.title = QStringLiteral("Broken Shader");
    failed.kind = Downloads::Kind::ShaderPack;
    failed.state = Downloads::DownloadResult::State::Failed;
    failed.error = QStringLiteral("Network error");
    failed.finishedAt = QDateTime::fromMSecsSinceEpoch(at(14, 17), kZone);
    s.downloads = { d, failed };
    History::Session played;
    played.instanceId = QStringLiteral("i1");
    played.startedMs = at(17, 10);
    played.endedMs = at(17, 11, 20);
    History::Session crashed = played;
    crashed.startedMs = at(16, 21);
    crashed.endedMs = at(16, 21, 6);
    crashed.outcome = History::Outcome::Crashed;
    History::Session noStart = played;
    noStart.startedMs = at(14, 16);
    noStart.endedMs = at(14, 16);
    noStart.outcome = History::Outcome::Failed;
    s.sessions = { played, crashed, noStart };
    return s;
}

struct Fake {
    Sources sources;
    QString busy;
    QList<Entry> undone;
    int reads = 0;
};

TimelineEnvironment envOf(Fake* fake)
{
    TimelineEnvironment env;
    env.read = [fake]() {
        ++fake->reads;
        return fake->sources;
    };
    env.busyReason = [fake]() { return fake->busy; };
    env.undo = [fake](const Entry& e) { fake->undone.append(e); };
    env.now = []() { return kNow; };
    return env;
}

}  // namespace

class ZeroTimelinePageTest : public QObject {
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

    void rowsAndGroups()
    {
        Fake fake;
        fake.sources = sample();
        TimelinePage page(envOf(&fake));
        page.resize(640, 560);
        page.refresh();
        QVERIFY(!page.isEmptyStateShown());
        QCOMPARE(page.groupLabels(), (QStringList{ QStringLiteral("Today"), QStringLiteral("Yesterday"), QStringLiteral("Sat 14 Nov") }));
        QCOMPARE(page.rowCount(), 9);
        QCOMPARE(page.rowTitles().first(), QStringLiteral("Played 1 h 20 min"));
        QVERIFY(page.rowTitles().contains(QStringLiteral("Updated 5 mods")));
        QVERIFY(page.rowCaptions().contains(QStringLiteral("Sodium, Lithium, Iris +2 more")));
        QVERIFY(page.rowTitles().contains(QStringLiteral("Updated Fabulously Optimized to 5.9.1")));
        QVERIFY(page.rowTitles().contains(QStringLiteral("Changed resource pack order")));
        QVERIFY(!page.showMoreButton());
    }

    void undoButtonsOnNewestOfEachKindOnly()
    {
        Fake fake;
        fake.sources = sample();
        TimelinePage page(envOf(&fake));
        page.refresh();
        const QList<QPushButton*> buttons = page.undoButtons();
        QCOMPARE(buttons.size(), 3);  // mods update, modpack update, pack order; the older mods update has none
        for (QPushButton* b : buttons) {
            QCOMPARE(b->text(), QStringLiteral("Undo"));
            QCOMPARE(b->height(), 28);
            QVERIFY(b->isEnabled());
        }
    }

    void clickCallsTheFlowAndReadsAgain()
    {
        Fake fake;
        fake.sources = sample();
        TimelinePage page(envOf(&fake));
        page.refresh();
        const int reads = fake.reads;
        page.undoButtons().first()->click();
        QCOMPARE(fake.undone.size(), 1);
        QCOMPARE(int(fake.undone.first().kind), int(Kind::ModsUpdate));
        QCOMPARE(fake.undone.first().undoRef, QStringLiteral("snap-") + QString::number(at(17, 9, 10)));
        QVERIFY(fake.reads > reads);
        // the flow rolled it back: the list now shows it as undone and offers the older one nowhere
        fake.sources.modSnapshots[0].rolledBack = true;
        page.refresh();
        QVERIFY(page.rowCaptions().join(QLatin1Char('|')).contains(QStringLiteral("Undone")));
        QCOMPARE(page.undoButtons().size(), 2);
    }

    void busyDisablesUndoWithTheReason()
    {
        Fake fake;
        fake.sources = sample();
        fake.busy = QStringLiteral("Close the game first.");
        TimelinePage page(envOf(&fake));
        page.refresh();
        const QList<QPushButton*> buttons = page.undoButtons();
        QVERIFY(!buttons.isEmpty());
        for (QPushButton* b : buttons) {
            QVERIFY(!b->isEnabled());
            QCOMPARE(b->toolTip(), fake.busy);
        }
        fake.busy.clear();
        page.refresh();
        for (QPushButton* b : page.undoButtons())
            QVERIFY(b->isEnabled());
    }

    void emptyState()
    {
        Fake fake;
        TimelinePage page(envOf(&fake));
        page.refresh();
        QVERIFY(page.isEmptyStateShown());
        QCOMPARE(page.rowCount(), 0);
        QVERIFY(page.groupLabels().isEmpty());
    }

    void showMoreAfterOneHundred()
    {
        Fake fake;
        fake.sources.instanceId = QStringLiteral("i1");
        for (int i = 0; i < 130; ++i) {
            History::Session s;
            s.instanceId = QStringLiteral("i1");
            s.startedMs = at(10, 8) + qint64(i) * 60 * 1000;
            s.endedMs = s.startedMs + 30 * 60 * 1000;
            fake.sources.sessions.append(s);
        }
        TimelinePage page(envOf(&fake));
        page.refresh();
        QCOMPARE(page.totalCount(), 130);
        QCOMPARE(page.rowCount(), 100);
        QVERIFY(page.showMoreButton());
        page.showMoreButton()->click();
        QCOMPARE(page.rowCount(), 130);
        QVERIFY(!page.showMoreButton());
    }

    void savesScreenshots()
    {
        const QString dir = qEnvironmentVariable("ZERO_TIMELINE_SHOTS");
        if (dir.isEmpty())
            return;
        QDir().mkpath(dir);
        for (const char* themeName : { "obsidian", "minecraft" }) {
            ThemeSpec spec = QByteArray(themeName) == "minecraft" ? ThemeSpec::minecraft() : ThemeSpec::obsidian();
            spec.reduceMotion = true;
            applySpecLive(spec);
            Theme::setReduceMotion(true);
            const QString prefix = QDir(dir).filePath(QString::fromLatin1(themeName) + QLatin1Char('-'));
            auto shoot = [&](QWidget& widget, const QString& name) {
                widget.show();
                QTest::qWait(80);
                QVERIFY(widget.grab().save(prefix + name + QStringLiteral(".png")));
            };
            {
                Fake fake;
                fake.sources = sample();
                TimelinePage page(envOf(&fake));
                page.resize(720, 640);
                page.refresh();
                shoot(page, QStringLiteral("timeline"));
                fake.busy = QStringLiteral("Close the game first.");
                page.refresh();
                shoot(page, QStringLiteral("timeline-busy"));
            }
            {
                Fake fake;
                TimelinePage page(envOf(&fake));
                page.resize(720, 400);
                page.refresh();
                shoot(page, QStringLiteral("timeline-empty"));
            }
        }
    }
};

QTEST_MAIN(ZeroTimelinePageTest)

#include "ZeroTimelinePage_test.moc"
