// SPDX-License-Identifier: GPL-3.0-only
// "While playing" policy (hide to the tray when the first game starts, restore after the last one ends or after a crash, never fight
// the user) with fake windows, and the tray: menu contents and enablement, tooltip, and the signals of the menu actions.
#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QSignalSpy>
#include <QTest>

#include "ui/zero/ZeroTray.h"
#include "zero/session/WhilePlaying.h"

using namespace Zero;
using namespace Zero::Session;

namespace {

struct FakeWindows {
    WhilePlayingMode mode = WhilePlayingMode::Tray;
    bool mainVisible = true;
    int hides = 0;
    int shows = 0;
    QHash<QString, bool> instanceVisible;

    WindowHooks hooks()
    {
        WindowHooks h;
        h.mode = [this]() { return mode; };
        h.mainWindowVisible = [this]() { return mainVisible; };
        h.hideMainWindow = [this]() {
            mainVisible = false;
            ++hides;
        };
        h.showMainWindow = [this]() {
            mainVisible = true;
            ++shows;
        };
        h.setInstanceWindowsVisible = [this](const QString& id, bool v) { instanceVisible[id] = v; };
        return h;
    }
};

QString nameOf(const QString& id)
{
    static const QHash<QString, QString> names{ { "a", "Alpha" }, { "b", "Beta" }, { "c", "Gamma" }, { "d", "Delta" },
                                                { "e", "Epsilon" }, { "f", "Zeta" }, { "g", "Eta" } };
    return names.value(id);
}

}  // namespace

class WhilePlayingTest : public QObject {
    Q_OBJECT

   private slots:
    void modeStrings()
    {
        QCOMPARE(whilePlayingFromString(QStringLiteral("tray")), WhilePlayingMode::Tray);
        QCOMPARE(whilePlayingFromString(QStringLiteral("open")), WhilePlayingMode::Open);
        QCOMPARE(whilePlayingFromString(QStringLiteral("close")), WhilePlayingMode::Close);
        QCOMPARE(whilePlayingFromString(QString()), WhilePlayingMode::Tray);  // the default
        QCOMPARE(whilePlayingFromString(QStringLiteral("nonsense")), WhilePlayingMode::Tray);
        for (auto m : { WhilePlayingMode::Tray, WhilePlayingMode::Open, WhilePlayingMode::Close })
            QCOMPARE(whilePlayingFromString(whilePlayingToString(m)), m);
    }

    void trayHidesAndRestores()
    {
        FakeWindows w;
        WhilePlaying wp(w.hooks());
        wp.onGameStarted(QStringLiteral("a"));
        QVERIFY(!w.mainVisible);
        QCOMPARE(w.instanceVisible.value(QStringLiteral("a"), true), false);  // the console window of that instance too
        wp.onGameEnded(QStringLiteral("a"), false);
        QVERIFY(w.mainVisible);
        QCOMPARE(w.instanceVisible.value(QStringLiteral("a")), true);
        QCOMPARE(w.hides, 1);
        QCOMPARE(w.shows, 1);
    }

    void twoInstancesRestoreAfterTheLastOne()
    {
        FakeWindows w;
        WhilePlaying wp(w.hooks());
        wp.onGameStarted(QStringLiteral("a"));
        wp.onGameStarted(QStringLiteral("b"));
        QCOMPARE(w.hides, 1);  // hidden once
        QCOMPARE(wp.runningCount(), 2);
        wp.onGameEnded(QStringLiteral("a"), false);
        QVERIFY(!w.mainVisible);  // b still runs
        QCOMPARE(w.shows, 0);
        wp.onGameEnded(QStringLiteral("b"), false);
        QVERIFY(w.mainVisible);
        QCOMPARE(w.shows, 1);
    }

    void crashRestoresEvenWhileAnotherRuns()
    {
        FakeWindows w;
        WhilePlaying wp(w.hooks());
        wp.onGameStarted(QStringLiteral("a"));
        wp.onGameStarted(QStringLiteral("b"));
        wp.onGameEnded(QStringLiteral("a"), true);
        QVERIFY(w.mainVisible);
        QCOMPARE(w.shows, 1);
        wp.onGameEnded(QStringLiteral("b"), false);
        QCOMPARE(w.shows, 1);  // already back: nothing more
    }

    void userReopenedTheWindowIsLeftAlone()
    {
        FakeWindows w;
        WhilePlaying wp(w.hooks());
        wp.onGameStarted(QStringLiteral("a"));
        w.mainVisible = true;  // the user opened the launcher from the tray
        wp.onGameEnded(QStringLiteral("a"), false);
        QCOMPARE(w.shows, 0);
        QVERIFY(w.mainVisible);
        QVERIFY(!wp.isHidden());
    }

    void aGameStartedWhileTheUserHasTheWindowOpenDoesNotHideItAgain()
    {
        FakeWindows w;
        WhilePlaying wp(w.hooks());
        wp.onGameStarted(QStringLiteral("a"));
        w.mainVisible = true;  // reopened
        wp.onGameStarted(QStringLiteral("b"));
        QVERIFY(w.mainVisible);
        QCOMPARE(w.hides, 1);
    }

    void openAndCloseModesDoNothingHere()
    {
        for (auto mode : { WhilePlayingMode::Open, WhilePlayingMode::Close }) {
            FakeWindows w;
            w.mode = mode;
            WhilePlaying wp(w.hooks());
            wp.onGameStarted(QStringLiteral("a"));
            QVERIFY(w.mainVisible);
            QCOMPARE(w.hides, 0);
            QVERIFY(w.instanceVisible.isEmpty());
            wp.onGameEnded(QStringLiteral("a"), false);
            QCOMPARE(w.shows, 0);
        }
    }

    void windowAlreadyHiddenIsNotHiddenTwice()
    {
        FakeWindows w;
        w.mainVisible = false;
        WhilePlaying wp(w.hooks());
        wp.onGameStarted(QStringLiteral("a"));
        QCOMPARE(w.hides, 0);
        wp.onGameEnded(QStringLiteral("a"), false);
        QCOMPARE(w.shows, 0);  // it was not hidden by us
    }

    // ----- tray menu model -----

    void menuShowsOnlyFavoritesInTheOrderGiven()
    {
        const auto menu = buildTrayMenu({ "b", "a", "c" }, nameOf, {});
        QStringList texts;
        for (const auto& e : menu)
            texts << (e.kind == TrayEntry::Kind::Separator ? QStringLiteral("-") : e.text);
        QCOMPARE(texts, (QStringList{ "Play Beta", "Play Alpha", "Play Gamma", "-", "Open ZERO Launcher", "Quit" }));
    }

    void menuMoreThanFiveFavoritesAreCut()
    {
        const auto menu = buildTrayMenu({ "a", "b", "c", "d", "e", "f", "g" }, nameOf, {});
        int plays = 0;
        for (const auto& e : menu)
            plays += e.kind == TrayEntry::Kind::Play;
        QCOMPARE(plays, kTrayMaxInstances);
    }

    void menuDisablesRunningAndUnlaunchable()
    {
        const auto menu = buildTrayMenu({ "a", "b", "c" }, nameOf, { "a" }, [](const QString& id) { return id != QLatin1String("c"); });
        QCOMPARE(menu.at(0).instanceId, QStringLiteral("a"));
        QVERIFY(!menu.at(0).enabled);  // running
        QVERIFY(menu.at(1).enabled);
        QVERIFY(!menu.at(2).enabled);  // cannot launch
        QVERIFY(menu.last().enabled);
    }

    void menuSkipsUnknownIdsAndDuplicates()
    {
        const auto menu = buildTrayMenu({ "a", "gone", "a", "b" }, nameOf, {});
        QStringList ids;
        for (const auto& e : menu)
            if (e.kind == TrayEntry::Kind::Play)
                ids << e.instanceId;
        QCOMPARE(ids, (QStringList{ "a", "b" }));
    }

    void menuWithoutFavoritesShowsDisabledHint()
    {
        const auto menu = buildTrayMenu({}, nameOf, {});
        QCOMPARE(menu.size(), 4);
        QCOMPARE(menu.at(0).kind, TrayEntry::Kind::Hint);
        QVERIFY(!menu.at(0).enabled);
        QCOMPARE(menu.at(1).kind, TrayEntry::Kind::Separator);
        QCOMPARE(menu.at(2).kind, TrayEntry::Kind::Open);
        QCOMPARE(menu.at(3).kind, TrayEntry::Kind::Quit);
    }

    void tooltips()
    {
        QCOMPARE(trayTooltip({}), QStringLiteral("ZERO Launcher"));
        QVERIFY(trayTooltip({ "Alpha" }).endsWith(QStringLiteral("Playing Alpha")));
        QVERIFY(trayTooltip({ "Alpha" }).startsWith(QStringLiteral("ZERO Launcher")));
        QVERIFY(trayTooltip({ "Alpha", "Beta", "Gamma" }).endsWith(QStringLiteral("Playing Alpha and 2 more")));
    }

    // ----- the tray widget -----

    void trayMenuAndSignals()
    {
        QSet<QString> running{ "a" };
        QStringList playing{ "Alpha" };
        TraySource src;
        src.favoriteIds = []() { return QStringList{ "a", "b" }; };
        src.nameOf = nameOf;
        src.running = [&running]() { return running; };
        src.playingNames = [&playing]() { return playing; };
        ZeroTray tray(src);
        tray.rebuildMenu();
        const auto actions = tray.menu()->actions();
        QCOMPARE(actions.size(), 5);  // 2 favorites, separator, open, quit
        QCOMPARE(actions.at(0)->text(), QStringLiteral("Play Alpha"));
        QVERIFY(!actions.at(0)->isEnabled());
        QVERIFY(actions.at(1)->isEnabled());
        QVERIFY(actions.at(2)->isSeparator());
        QCOMPARE(actions.at(3)->text(), QStringLiteral("Open ZERO Launcher"));
        QCOMPARE(actions.at(4)->text(), QStringLiteral("Quit"));
        QVERIFY(tray.tooltip().endsWith(QStringLiteral("Playing Alpha")));
        playing.clear();
        QCOMPARE(tray.tooltip(), QStringLiteral("ZERO Launcher"));

        QSignalSpy play(&tray, &ZeroTray::playRequested);
        QSignalSpy open(&tray, &ZeroTray::openRequested);
        QSignalSpy quit(&tray, &ZeroTray::quitRequested);
        actions.at(1)->trigger();
        actions.at(3)->trigger();
        actions.at(4)->trigger();
        QCOMPARE(play.count(), 1);
        QCOMPARE(play.first().first().toString(), QStringLiteral("b"));
        QCOMPARE(open.count(), 1);
        QCOMPARE(quit.count(), 1);

        running.clear();  // the menu is rebuilt every time it opens
        tray.rebuildMenu();
        QVERIFY(tray.menu()->actions().at(0)->isEnabled());
    }
};

QTEST_MAIN(WhilePlayingTest)
#include "WhilePlaying_test.moc"
