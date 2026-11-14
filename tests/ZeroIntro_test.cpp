// SPDX-License-Identifier: GPL-3.0-only
// First-run intro: the pure gating (seen / forced / screenshot / direct launch / existing data), the overlay's keys (Esc: final frame,
// then Get started), Get started (finished once, ZeroIntroSeen set, the overlay deletes itself), reduced motion (the button at once),
// following the parent's size, blocking the shell, and the shared night scene (ZeroScene) it is painted with.
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

#include "settings/INISettingsObject.h"
#include "ui/zero/ZeroScene.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroTokens.h"
#include "ui/zero/onboarding/IntroGate.h"
#include "ui/zero/onboarding/IntroOverlay.h"

using namespace Zero;
using namespace Zero::Onboarding;

namespace {

struct Host {
    QWidget widget;
    QPushButton* shellButton = new QPushButton(QStringLiteral("shell"), &widget);
    std::unique_ptr<QTemporaryDir> dir = std::make_unique<QTemporaryDir>();
    std::unique_ptr<INISettingsObject> settings;

    Host()
    {
        widget.resize(1000, 700);
        shellButton->setGeometry(20, 20, 120, 32);
        settings = std::make_unique<INISettingsObject>(dir->filePath(QStringLiteral("test.cfg")));
        settings->registerSetting("ZeroIntroSeen", false);
        widget.show();
    }
};

bool inside(const QRect& outer, const QWidget* w)
{
    return outer.contains(w->geometry());
}

}  // namespace

class ZeroIntroTest : public QObject {
    Q_OBJECT

   private slots:
    void init() { Theme::setReduceMotion(false); }
    void cleanup() { Theme::setReduceMotion(false); }

    void gating_data()
    {
        QTest::addColumn<bool>("seen");
        QTest::addColumn<bool>("forced");
        QTest::addColumn<bool>("screenshot");
        QTest::addColumn<bool>("launch");
        QTest::addColumn<bool>("existing");
        QTest::addColumn<bool>("show");
        QTest::addColumn<bool>("markSeen");
        //                                            seen   forced screenshot launch existing | show  markSeen
        QTest::newRow("fresh data dir")            << false << false << false << false << false << true << false;
        QTest::newRow("already seen")              << true << false << false << false << false << false << false;
        QTest::newRow("screenshot run")            << false << false << true << false << false << false << false;
        QTest::newRow("launches an instance")      << false << false << false << true << false << false << false;
        QTest::newRow("older data dir")            << false << false << false << false << true << false << true;
        QTest::newRow("seen and existing")         << true << false << false << false << true << false << false;
        QTest::newRow("forced, seen")              << true << true << false << false << false << true << false;
        QTest::newRow("forced, existing data")     << false << true << false << false << true << true << false;
        QTest::newRow("forced, screenshot run")    << false << true << true << false << false << false << false;
        QTest::newRow("forced, launches instance") << true << true << false << true << false << false << false;
        QTest::newRow("launch beats existing")     << false << false << false << true << true << false << false;
    }
    void gating()
    {
        QFETCH(bool, seen);
        QFETCH(bool, forced);
        QFETCH(bool, screenshot);
        QFETCH(bool, launch);
        QFETCH(bool, existing);
        QFETCH(bool, show);
        QFETCH(bool, markSeen);
        IntroGate g;
        g.seen = seen;
        g.forced = forced;
        g.screenshotRun = screenshot;
        g.launchesInstance = launch;
        g.existingData = existing;
        const IntroDecision d = decideIntro(g);
        QCOMPARE(d.show, show);
        QCOMPARE(d.markSeen, markSeen);
    }

    void startsAtTheBeginning()
    {
        Host host;
        auto* intro = new IntroOverlay(&host.widget, host.settings.get());
        QCOMPARE(intro->geometry(), host.widget.rect());
        QVERIFY(intro->isVisible());
        QVERIFY(intro->timelineMs() < IntroOverlay::kButtonStartMs);
        QVERIFY(!intro->isFinal());
        QVERIFY(!intro->startButtonShown());
        QVERIFY(!intro->skipButton()->isHidden());  // "Skip intro" is there from the start
        QVERIFY(!host.settings->get("ZeroIntroSeen").toBool());
    }

    void escFirstJumpsToTheFinalFrame_secondFinishes()
    {
        Host host;
        auto* intro = new IntroOverlay(&host.widget, host.settings.get());
        QSignalSpy finished(intro, &IntroOverlay::finished);

        QTest::keyClick(intro, Qt::Key_Escape);
        QVERIFY(intro->isFinal());
        QVERIFY(intro->startButtonShown());
        QVERIFY(!intro->isFinishing());
        QCOMPARE(finished.count(), 0);
        QVERIFY(!host.settings->get("ZeroIntroSeen").toBool());  // jumping is not "seen"

        QTest::keyClick(intro, Qt::Key_Escape);
        QVERIFY(intro->isFinishing());
        QVERIFY(finished.wait(3000));
        QCOMPARE(finished.count(), 1);
        QVERIFY(host.settings->get("ZeroIntroSeen").toBool());
    }

    void skipButtonActsLikeEsc()
    {
        Host host;
        auto* intro = new IntroOverlay(&host.widget, host.settings.get());
        QSignalSpy finished(intro, &IntroOverlay::finished);
        intro->skipButton()->click();
        QVERIFY(intro->isFinal());
        QVERIFY(intro->startButtonShown());
        QVERIFY(!intro->isFinishing());
        intro->skipButton()->click();
        QVERIFY(intro->isFinishing());
        QVERIFY(finished.wait(3000));
        QCOMPARE(finished.count(), 1);
    }

    void clickBeforeTheButtonJumps_clickAfterDoesNot()
    {
        Host host;
        auto* intro = new IntroOverlay(&host.widget, host.settings.get());
        QTest::mouseClick(intro, Qt::LeftButton, Qt::NoModifier, QPoint(300, 300));
        QVERIFY(intro->isFinal());
        QVERIFY(intro->startButtonShown());
        QTest::mouseClick(intro, Qt::LeftButton, Qt::NoModifier, QPoint(300, 300));  // the button is there: a click elsewhere does nothing
        QVERIFY(!intro->isFinishing());
    }

    void getStartedEmitsFinishedOnceAndSetsTheSetting()
    {
        Host host;
        QPointer<IntroOverlay> intro = new IntroOverlay(&host.widget, host.settings.get());
        QSignalSpy finished(intro.data(), &IntroOverlay::finished);
        intro->skip();  // to the final frame
        QVERIFY(intro->startButtonShown());
        QVERIFY(intro->startButton()->isEnabled());
        intro->startButton()->click();
        intro->startButton()->click();  // a second press during the fade changes nothing
        QTest::keyClick(intro.data(), Qt::Key_Return);
        QVERIFY(host.settings->get("ZeroIntroSeen").toBool());  // recorded at once, before the fade ends
        QVERIFY(finished.wait(3000));
        QTest::qWait(100);
        QCOMPARE(finished.count(), 1);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(intro.isNull());  // it deleted itself
    }

    void enterAndSpaceStartToo()
    {
        for (const Qt::Key key : { Qt::Key_Return, Qt::Key_Enter }) {
            Host host;
            auto* intro = new IntroOverlay(&host.widget, host.settings.get());
            QSignalSpy finished(intro, &IntroOverlay::finished);
            intro->skip();
            QTest::keyClick(intro, key);
            QVERIFY(intro->isFinishing());
            QVERIFY(finished.wait(3000));
        }
        Host host;
        auto* intro = new IntroOverlay(&host.widget, host.settings.get());
        QSignalSpy finished(intro, &IntroOverlay::finished);
        intro->skip();
        QTest::keyClick(intro->startButton(), Qt::Key_Space);  // Space on the focused button clicks it
        QVERIFY(intro->isFinishing());
        QVERIFY(finished.wait(3000));
        QCOMPARE(finished.count(), 1);
    }

    void reducedMotionShowsTheButtonAtOnce()
    {
        Theme::setReduceMotion(true);
        Host host;
        auto* intro = new IntroOverlay(&host.widget, host.settings.get());
        QSignalSpy finished(intro, &IntroOverlay::finished);
        QVERIFY(intro->startButtonShown());
        QVERIFY(intro->isFinal());
        QCOMPARE(intro->timelineMs(), qint64(IntroOverlay::kFinalMs));
        // nothing moves once the one 200 ms fade is over: two frames a moment apart are identical (no twinkle, no bobbing)
        QTest::qWait(IntroOverlay::kReducedFadeMs + 100);
        const QImage a = intro->grab().toImage();
        QTest::qWait(120);
        const QImage b = intro->grab().toImage();
        QCOMPARE(a, b);
        // Get started ends at once (no fade-out)
        intro->startButton()->click();
        QCOMPARE(finished.count(), 1);
        QVERIFY(host.settings->get("ZeroIntroSeen").toBool());
    }

    void followsTheParentResize()
    {
        Host host;
        auto* intro = new IntroOverlay(&host.widget, host.settings.get());
        intro->skip();
        QCOMPARE(intro->geometry(), host.widget.rect());
        for (const QSize size : { QSize(1280, 800), QSize(900, 600), QSize(640, 420), QSize(480, 340) }) {
            host.widget.resize(size);
            QCoreApplication::processEvents();
            QCOMPARE(intro->geometry(), host.widget.rect());
            // small windows: the button and the skip row are never clipped
            QVERIFY2(inside(intro->rect(), intro->startButton()), qPrintable(QStringLiteral("start button at %1x%2").arg(size.width()).arg(size.height())));
            QVERIFY2(inside(intro->rect(), intro->skipButton()), qPrintable(QStringLiteral("skip button at %1x%2").arg(size.width()).arg(size.height())));
            QVERIFY(intro->startButton()->geometry().center().x() == intro->width() / 2 || qAbs(intro->startButton()->geometry().center().x() - intro->width() / 2) <= 1);
            QVERIFY(!intro->startButton()->geometry().intersects(intro->skipButton()->geometry()));
        }
    }

    void staysAboveTheShell()
    {
        Host host;
        auto* intro = new IntroOverlay(&host.widget, host.settings.get());
        // the shell button is under the overlay: the topmost widget at its position belongs to the overlay
        QWidget* top = host.widget.childAt(QPoint(40, 36));
        QVERIFY(top == intro || intro->isAncestorOf(top));
        // a child added later (a toast, a palette) does not end up above it
        auto* later = new QWidget(&host.widget);
        later->setGeometry(0, 0, 200, 200);
        later->show();
        later->raise();
        QTRY_VERIFY(host.widget.children().indexOf(intro) > host.widget.children().indexOf(later));
    }

    void tabStaysInsideTheOverlay()
    {
        Host host;
        host.shellButton->setFocus();
        auto* intro = new IntroOverlay(&host.widget, host.settings.get());
        intro->skip();
        QVERIFY(intro->isAncestorOf(host.widget.focusWidget()) || host.widget.focusWidget() == intro);
        for (int i = 0; i < 5; ++i) {
            QTest::keyClick(intro->startButton(), Qt::Key_Tab);
            QWidget* f = host.widget.focusWidget();
            QVERIFY(f && (f == intro || intro->isAncestorOf(f)));
        }
        for (int i = 0; i < 3; ++i) {
            QTest::keyClick(intro->skipButton(), Qt::Key_Backtab, Qt::ShiftModifier);
            QWidget* f = host.widget.focusWidget();
            QVERIFY(f && (f == intro || intro->isAncestorOf(f)));
        }
    }

    void frozenFramesForScreenshots()
    {
        Host host;
        auto* intro = new IntroOverlay(&host.widget, nullptr);
        intro->freezeAt(600);
        QCOMPARE(intro->timelineMs(), qint64(600));
        QVERIFY(!intro->startButtonShown());
        QTest::qWait(60);
        QCOMPARE(intro->timelineMs(), qint64(600));  // frozen: the clock does not run
        intro->freezeAt(4500);
        QVERIFY(intro->startButtonShown());
        QVERIFY(intro->isFinal());
        // the sky is the brand night colour in the corner (the stars keep away from the edges), the ground at the bottom is not
        const QImage img = intro->grab().toImage();
        QCOMPARE(img.pixelColor(1, 1), QColor::fromRgba(Tokens::Minecraft::bg));
        QVERIFY(img.pixelColor(1, img.height() - 2) != QColor::fromRgba(Tokens::Minecraft::bg));
    }

    // ---- ZeroScene, the night scene shared with ZERO Setup
    void sceneStarsAreDeterministicAndAvoidTheHero()
    {
        const QRectF keepOut(230, 30, 300, 240);
        const auto a = Scene::makeStars(760, 460, 30, keepOut, 0.55, 3);
        const auto b = Scene::makeStars(760, 460, 30, keepOut, 0.55, 3);
        QCOMPARE(a.size(), 30);
        QCOMPARE(a.size(), b.size());
        for (int i = 0; i < a.size(); ++i) {
            QCOMPARE(a[i].pos, b[i].pos);
            QCOMPARE(a[i].phase, b[i].phase);
            QVERIFY(!keepOut.contains(a[i].pos));
            QVERIFY(a[i].pos.y() <= 460 * 0.55);
        }
    }

    void sceneMarkAndPixelSizes()
    {
        QCOMPARE(Scene::markBlocks().size(), 252);  // the 16 x 16 mark minus its transparent corners
        QVERIFY(qAbs(Scene::markBuildMs(252) - (1400 + Scene::kDropMs)) <= 1);
        QCOMPARE(Scene::snapPixelSize(50, 1.0), 50);  // a multiple of 10 device pixels is exact
        const int snapped = Scene::snapPixelSize(50, 1.25);
        QVERIFY(qAbs(snapped * 1.25 / 10.0 - qRound(snapped * 1.25 / 10.0)) < 1e-9);
        QVERIFY(snapped >= 36 && snapped <= 64);
    }
};

QTEST_MAIN(ZeroIntroTest)
#include "ZeroIntro_test.moc"
