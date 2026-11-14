// SPDX-License-Identifier: GPL-3.0-only
// Guided tour: the step list (a step without a usable target is dropped), the keys (Right / Enter next, Left back, Esc skip), Finish and Skip
// (the setting once, the overlay deletes itself), the card staying inside the window at 1100 x 700 and 1920 x 1080 for every step, the
// spotlight following a target that moved after a resize, the spotlight glide, the pure card placement and the palette entry that replays it.
#include <QHash>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

#include "settings/INISettingsObject.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroTokens.h"
#include "ui/zero/onboarding/TourOverlay.h"
#include "ui/zero/onboarding/TourSteps.h"

using namespace Zero;
using namespace Zero::Onboarding;

namespace {

/// A stand-in for the shell: rail buttons on the left (the last one at the bottom), a search pill in the top bar, a New instance button
/// at its right end. Positions follow the host's size like the real ones do.
struct Host {
    QWidget widget;
    QHash<QString, QPushButton*> rail;
    QPushButton* pill = nullptr;
    QPushButton* newButton = nullptr;
    std::unique_ptr<QTemporaryDir> dir = std::make_unique<QTemporaryDir>();
    std::unique_ptr<INISettingsObject> settings;

    explicit Host(QSize size = QSize(1100, 700))
    {
        for (const QString& id : { QStringLiteral("home"), QStringLiteral("library"), QStringLiteral("discover"), QStringLiteral("appearance") })
            rail.insert(id, new QPushButton(id, &widget));
        pill = new QPushButton(QStringLiteral("Search"), &widget);
        newButton = new QPushButton(QStringLiteral("New instance"), &widget);
        settings = std::make_unique<INISettingsObject>(dir->filePath(QStringLiteral("test.cfg")));
        settings->registerSetting("ZeroTourDone", false);
        widget.resize(size);
        layoutWidgets();
        widget.show();
        (void)QTest::qWaitForWindowExposed(&widget);
    }

    void layoutWidgets()
    {
        const int w = widget.width(), h = widget.height();
        rail["home"]->setGeometry(8, 74, 204, 36);
        rail["library"]->setGeometry(8, 112, 204, 36);
        rail["discover"]->setGeometry(8, 150, 204, 36);
        rail["appearance"]->setGeometry(8, h - 110, 204, 36);
        pill->setGeometry((w - 220 - 360) / 2 + 220, 12, 360, 34);
        newButton->setGeometry(w - 150, 14, 130, 32);
    }

    QWidget* targetOf(const QString& id) const
    {
        if (id == QLatin1String("new-instance"))
            return newButton;
        if (id == QLatin1String("search"))
            return pill;
        return rail.value(id);
    }

    /// The six steps with their targets in this host.
    QList<TourStep> steps()
    {
        QList<TourStep> out;
        for (const TourStepInfo& info : tourStepInfos()) {
            TourStep s;
            s.id = info.id;
            s.title = info.title;
            s.body = info.body;
            s.side = info.side;
            const QString id = info.id;
            s.target = [this, id]() -> QWidget* { return targetOf(id); };
            out << s;
        }
        return out;
    }
};

bool insideWindow(const Host& host, const QRect& card)
{
    return host.widget.rect().adjusted(kCardMargin, kCardMargin, -kCardMargin, -kCardMargin).contains(card);
}

}  // namespace

class ZeroTourTest : public QObject {
    Q_OBJECT

   private slots:
    void init() { Theme::setReduceMotion(true); }  // most tests: instant steps; the glide test turns motion on
    void cleanup() { Theme::setReduceMotion(false); }

    // ---- content and pure geometry
    void sixStepsInOrder()
    {
        const QList<TourStepInfo> infos = tourStepInfos();
        QStringList ids;
        for (const TourStepInfo& i : infos) {
            ids << i.id;
            QVERIFY(!i.title.isEmpty());
            QVERIFY(!i.body.isEmpty());
        }
        QCOMPARE(ids, (QStringList{ "home", "library", "new-instance", "discover", "search", "appearance" }));
    }

    void placeCardRightOfARailItem()
    {
        const QRect spot(0, 100, 220, 52);
        const CardPlacement p = placeCard(spot, QSize(300, 190), QSize(1280, 800), TourSide::Right);
        QCOMPARE(p.card.left(), spot.right() + 1 + kCardGap);
        QVERIFY(qAbs(p.card.center().y() - spot.center().y()) <= 1);
        QCOMPARE(p.card.size(), QSize(300, 190));
        QVERIFY(p.pointer > 0 && p.pointer < 190);
    }

    void placeCardIsClampedInsideTheWindow()
    {
        const QSize window(1100, 700);
        // the bottom rail item: the card would hang below the window
        const CardPlacement low = placeCard(QRect(0, 570, 220, 52), QSize(300, 190), window, TourSide::Right);
        QVERIFY(low.card.bottom() <= window.height() - kCardMargin);
        QVERIFY(low.card.top() >= kCardMargin);
        // the pointer still points at the cut-out's centre from inside the card
        QCOMPARE(low.card.top() + low.pointer, QRect(0, 570, 220, 52).center().y());
        // a control at the right end of the top bar: the card stays inside the right edge
        const CardPlacement right = placeCard(QRect(960, 6, 140, 48), QSize(300, 190), window, TourSide::Below);
        QCOMPARE(right.card.right(), window.width() - kCardMargin - 1);
        QCOMPARE(right.card.top(), 54 + kCardGap);
        QVERIFY(right.card.left() + right.pointer >= 960 && right.card.left() + right.pointer <= 1100);
        // a window too small for the card never gives a negative position
        const CardPlacement tiny = placeCard(QRect(0, 0, 100, 40), QSize(300, 190), QSize(200, 150), TourSide::Below);
        QVERIFY(tiny.card.left() >= kCardMargin && tiny.card.top() >= kCardMargin);
    }

    void spotlightIsPaddedAndKeptInsideTheWindow()
    {
        QCOMPARE(spotlightRect(QRect(100, 100, 50, 30), QRect(0, 0, 800, 600)), QRect(92, 92, 66, 46));
        QCOMPARE(spotlightRect(QRect(2, 2, 50, 30), QRect(0, 0, 800, 600)), QRect(0, 0, 60, 40));
    }

    void paletteEntryReplaysTheTour()
    {
        int runs = 0;
        const PaletteCommand c = tourPaletteCommand([&runs]() { ++runs; });
        QCOMPARE(c.id, QStringLiteral("action:take-tour"));
        QCOMPARE(c.id, tourCommandId());
        QVERIFY(c.kind == Palette::Kind::Action);
        QVERIFY(!c.title.isEmpty());
        for (const QString& k : { QStringLiteral("tour"), QStringLiteral("help"), QStringLiteral("guide"), QStringLiteral("tutorial") })
            QVERIFY2(c.keywords.contains(k), qPrintable(k));
        QVERIFY(!Icons::icon(c.iconName, Qt::white).isNull());
        QVERIFY(c.run);
        c.run();
        QCOMPARE(runs, 1);
    }

    // ---- the step list
    void aStepWithoutATargetIsSkipped()
    {
        Host host;
        host.rail["library"]->hide();    // hidden
        QList<TourStep> steps = host.steps();
        steps[4].target = []() -> QWidget* { return nullptr; };  // search: no widget at all
        auto* tour = new TourOverlay(&host.widget, steps, host.settings.get());
        QCOMPARE(tour->stepCount(), 4);
        QCOMPARE(tour->stepIds(), (QStringList{ "home", "new-instance", "discover", "appearance" }));
        QCOMPARE(tour->currentStep(), 0);
        QCOMPARE(tour->overlineText(), QStringLiteral("Step 1 of 4"));
        tour->nextButton()->click();
        QCOMPARE(tour->currentStepId(), QStringLiteral("new-instance"));
        QCOMPARE(tour->overlineText(), QStringLiteral("Step 2 of 4"));
        tour->skip();
    }

    void aStepThatNeedsAPageCountsViaAvailable()
    {
        Host host;
        host.newButton->hide();  // like Library's button while another page is current
        QList<TourStep> steps = host.steps();
        steps[2].available = []() { return true; };
        int entered = 0, left = 0;
        steps[2].enter = [&]() {
            ++entered;
            host.newButton->show();
        };
        steps[2].leave = [&]() {
            ++left;
            host.newButton->hide();
        };
        auto* tour = new TourOverlay(&host.widget, steps, host.settings.get());
        QCOMPARE(tour->stepCount(), 6);
        tour->showStep(2, false);
        QCOMPARE(entered, 1);
        QCOMPARE(tour->currentStepId(), QStringLiteral("new-instance"));
        QCOMPARE(tour->spotlightTarget(), spotlightRect(host.newButton->geometry(), host.widget.rect()));
        tour->nextButton()->click();  // leaving it runs `leave`
        QCOMPARE(left, 1);
        QCOMPARE(tour->currentStepId(), QStringLiteral("discover"));
        tour->showStep(2, false);
        QCOMPARE(entered, 2);
        tour->skip();  // ending the tour while it is current also runs `leave`
        QCOMPARE(left, 2);
    }

    void aStepWhoseTargetVanishesAfterTheListWasBuiltIsSkipped()
    {
        Host host;
        auto* tour = new TourOverlay(&host.widget, host.steps(), host.settings.get());
        QCOMPARE(tour->stepCount(), 6);
        host.pill->hide();  // search disappears while the tour runs
        tour->showStep(3, false);
        tour->nextButton()->click();
        QCOMPARE(tour->currentStepId(), QStringLiteral("appearance"));  // search was skipped
        tour->skip();
    }

    // ---- keys
    void keys()
    {
        Host host;
        QPointer<TourOverlay> tour = new TourOverlay(&host.widget, host.steps(), host.settings.get());
        QCOMPARE(tour->currentStep(), 0);
        QVERIFY(tour->backButton()->isHidden());  // step 1: no Back
        QVERIFY(tour->isAncestorOf(host.widget.focusWidget()));
        QCOMPARE(host.widget.focusWidget(), tour->nextButton());  // focus starts on Next
        QTest::keyClick(tour->nextButton(), Qt::Key_Right);
        QCOMPARE(tour->currentStep(), 1);
        QVERIFY(!tour->backButton()->isHidden());
        QTest::keyClick(tour->nextButton(), Qt::Key_Return);
        QCOMPARE(tour->currentStep(), 2);
        QTest::keyClick(tour->nextButton(), Qt::Key_Enter);
        QCOMPARE(tour->currentStep(), 3);
        QTest::keyClick(tour->nextButton(), Qt::Key_Left);
        QCOMPARE(tour->currentStep(), 2);
        QTest::keyClick(tour->nextButton(), Qt::Key_Left);
        QTest::keyClick(tour->nextButton(), Qt::Key_Left);
        QCOMPARE(tour->currentStep(), 0);
        QTest::keyClick(tour->nextButton(), Qt::Key_Left);  // Back at step 1 does nothing
        QCOMPARE(tour->currentStep(), 0);
        QSignalSpy finished(tour.data(), &TourOverlay::finished);
        QTest::keyClick(tour->nextButton(), Qt::Key_Escape);
        QVERIFY(tour->isFinishing());
        QTRY_VERIFY(tour.isNull());
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.at(0).at(0).toBool(), false);
    }

    void enterOnTheSkipButtonSkips()
    {
        Host host;
        QPointer<TourOverlay> tour = new TourOverlay(&host.widget, host.steps(), host.settings.get());
        tour->skipButton()->setFocus();
        QTest::keyClick(tour->skipButton(), Qt::Key_Return);
        QVERIFY(tour->isFinishing());
        QTRY_VERIFY(tour.isNull());
    }

    void lastStepIsFinishAndBackWorks()
    {
        Host host;
        auto* tour = new TourOverlay(&host.widget, host.steps(), host.settings.get());
        QCOMPARE(tour->nextButton()->text(), QStringLiteral("Next"));
        tour->showStep(5, false);
        QCOMPARE(tour->nextButton()->text(), QStringLiteral("Finish"));
        tour->backButton()->click();
        QCOMPARE(tour->currentStep(), 4);
        QCOMPARE(tour->nextButton()->text(), QStringLiteral("Next"));
        tour->skip();
    }

    void tabStaysInsideTheTour()
    {
        Host host;
        host.rail["home"]->setFocus();
        auto* tour = new TourOverlay(&host.widget, host.steps(), host.settings.get());
        for (int i = 0; i < 6; ++i) {
            QTest::keyClick(host.widget.focusWidget(), Qt::Key_Tab);
            QWidget* f = host.widget.focusWidget();
            QVERIFY(f && (f == tour || tour->isAncestorOf(f)));
        }
        tour->skip();
    }

    void shellIsBlockedBehindTheTour()
    {
        Host host;
        auto* tour = new TourOverlay(&host.widget, host.steps(), host.settings.get());
        QWidget* top = host.widget.childAt(host.rail["home"]->geometry().center());
        QVERIFY(top == tour || tour->isAncestorOf(top));
        QSignalSpy clicked(host.rail["home"], &QPushButton::clicked);
        QTest::mouseClick(tour, Qt::LeftButton, Qt::NoModifier, host.rail["home"]->geometry().center());
        QCOMPARE(clicked.size(), 0);
        QVERIFY(!tour->isFinishing());  // a click on the dim layer does not end the tour
        // a child added later does not end up above it
        auto* later = new QWidget(&host.widget);
        later->setGeometry(0, 0, 200, 200);
        later->show();
        later->raise();
        QTRY_VERIFY(host.widget.children().indexOf(tour) > host.widget.children().indexOf(later));
        tour->skip();
    }

    // ---- Finish and Skip
    void finishSetsTheSettingOnceAndDeletesTheOverlay()
    {
        Host host;
        QPointer<TourOverlay> tour = new TourOverlay(&host.widget, host.steps(), host.settings.get());
        QSignalSpy finished(tour.data(), &TourOverlay::finished);
        QCOMPARE(host.settings->get("ZeroTourDone").toBool(), false);
        tour->showStep(5, false);
        tour->nextButton()->click();  // Finish
        tour->nextButton()->click();  // a second press changes nothing
        QTest::keyClick(tour->nextButton(), Qt::Key_Return);
        QCOMPARE(host.settings->get("ZeroTourDone").toBool(), true);
        QTRY_VERIFY(tour.isNull());
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.at(0).at(0).toBool(), true);  // completed: the controller shows "You're all set"
    }

    void skipSetsTheSettingOnceAndDeletesTheOverlay()
    {
        Host host;
        QPointer<TourOverlay> tour = new TourOverlay(&host.widget, host.steps(), host.settings.get());
        QSignalSpy finished(tour.data(), &TourOverlay::finished);
        tour->nextButton()->click();
        tour->skipButton()->click();
        tour->skip();
        QCOMPARE(host.settings->get("ZeroTourDone").toBool(), true);
        QTRY_VERIFY(tour.isNull());
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.at(0).at(0).toBool(), false);  // skipped: no toast
    }

    void fadeOutTakesAboutTwoHundredMsWithMotion()
    {
        Theme::setReduceMotion(false);
        Host host;
        QPointer<TourOverlay> tour = new TourOverlay(&host.widget, host.steps(), host.settings.get());
        tour->skip();
        QVERIFY(tour->isVisible());  // still fading
        QCOMPARE(host.settings->get("ZeroTourDone").toBool(), true);  // but the setting is already written
        QTRY_VERIFY_WITH_TIMEOUT(tour.isNull(), 2000);
    }

    void noUsableStepEndsWithoutTouchingTheSetting()
    {
        Host host;
        for (QPushButton* b : host.rail)
            b->hide();
        host.pill->hide();
        host.newButton->hide();
        QPointer<TourOverlay> tour = new TourOverlay(&host.widget, host.steps(), host.settings.get());
        QCOMPARE(tour->stepCount(), 0);
        QTRY_VERIFY(tour.isNull());
        QCOMPARE(host.settings->get("ZeroTourDone").toBool(), false);
    }

    // ---- geometry
    void cardStaysInsideTheWindowForEveryStep_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::newRow("1100x700") << QSize(1100, 700);
        QTest::newRow("1280x800") << QSize(1280, 800);
        QTest::newRow("1920x1080") << QSize(1920, 1080);
    }
    void cardStaysInsideTheWindowForEveryStep()
    {
        QFETCH(QSize, size);
        Host host(size);
        auto* tour = new TourOverlay(&host.widget, host.steps(), host.settings.get());
        QCOMPARE(tour->geometry(), host.widget.rect());
        QCOMPARE(tour->stepCount(), 6);
        for (int i = 0; i < tour->stepCount(); ++i) {
            tour->showStep(i, false);
            const QRect card = tour->cardRect();
            QVERIFY2(insideWindow(host, card), qPrintable(QStringLiteral("step %1: %2").arg(i + 1).arg(card.x())));
            QVERIFY2(!card.intersects(tour->spotlight()), qPrintable(QStringLiteral("step %1 overlaps the spotlight").arg(i + 1)));
            QCOMPARE(card.width(), kCardWidth);
            QVERIFY2(tour->bodyLines() <= 3, qPrintable(QStringLiteral("step %1: %2 lines").arg(i + 1).arg(tour->bodyLines())));
            const QRect expected = spotlightRect(host.targetOf(tour->currentStepId())->geometry(), host.widget.rect());
            QCOMPARE(tour->spotlight(), expected);
        }
        tour->skip();
    }

    void spotlightFollowsATargetMovedAfterAResize()
    {
        Host host(QSize(1100, 700));
        auto* tour = new TourOverlay(&host.widget, host.steps(), host.settings.get());
        tour->showStep(3, false);  // discover
        QCOMPARE(tour->spotlight(), spotlightRect(host.rail["discover"]->geometry(), host.widget.rect()));

        host.widget.resize(1400, 900);
        host.layoutWidgets();
        QCOMPARE(tour->geometry(), host.widget.rect());  // the overlay follows the window
        host.rail["discover"]->move(8, 300);              // and the target moved on its own
        QTRY_COMPARE(tour->spotlight(), spotlightRect(QRect(8, 300, 204, 36), host.widget.rect()));
        QVERIFY(insideWindow(host, tour->cardRect()));
        QCOMPARE(tour->cardRect().center().y(), tour->spotlight().center().y());

        // the bottom item follows the window height
        tour->showStep(5, false);
        host.widget.resize(1100, 700);
        host.layoutWidgets();
        QTRY_COMPARE(tour->spotlight(), spotlightRect(host.rail["appearance"]->geometry(), host.widget.rect()));
        QVERIFY(insideWindow(host, tour->cardRect()));
        tour->skip();
    }

    void spotlightGlidesBetweenStepsWithMotion()
    {
        Theme::setReduceMotion(false);
        Host host;
        auto* tour = new TourOverlay(&host.widget, host.steps(), host.settings.get());
        const QRect first = tour->spotlight();
        QCOMPARE(first, spotlightRect(host.rail["home"]->geometry(), host.widget.rect()));
        tour->nextButton()->click();  // library, 38 px below
        const QRect end = spotlightRect(host.rail["library"]->geometry(), host.widget.rect());
        QCOMPARE(tour->spotlightTarget(), end);
        QVERIFY(first != end);
        QTRY_COMPARE_WITH_TIMEOUT(tour->spotlight(), end, 1500);
        // next to a far target: it passes through the places in between
        tour->showStep(5, true);
        const int fromY = tour->spotlight().y();
        QVERIFY(fromY <= spotlightRect(host.rail["appearance"]->geometry(), host.widget.rect()).y());
        QTRY_COMPARE_WITH_TIMEOUT(tour->spotlight(), spotlightRect(host.rail["appearance"]->geometry(), host.widget.rect()), 1500);
        tour->skip();
    }

    void reducedMotionJumpsAtOnce()
    {
        Host host;  // init() turned motion off
        auto* tour = new TourOverlay(&host.widget, host.steps(), host.settings.get());
        tour->nextButton()->click();
        QCOMPARE(tour->spotlight(), spotlightRect(host.rail["library"]->geometry(), host.widget.rect()));
        tour->nextButton()->click();
        QCOMPARE(tour->spotlight(), spotlightRect(host.newButton->geometry(), host.widget.rect()));
        tour->skip();
    }
};

QTEST_MAIN(ZeroTourTest)
#include "ZeroTour_test.moc"
