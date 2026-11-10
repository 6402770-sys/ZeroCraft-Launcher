// SPDX-License-Identifier: GPL-3.0-only
// ZeroPlayButton state machine: driven through the same slots the Application signals are connected to.
#include <QApplication>
#include <memory>
#include <QSignalSpy>
#include <QTest>

#include "ui/zero/ZeroPlayButton.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

using namespace Zero;
using Phase = Application::LaunchPhase;
using State = ZeroPlayButton::State;

class ZeroPlayButtonTest : public QObject {
    Q_OBJECT

    static ZeroPlayButton* make()
    {
        auto* b = new ZeroPlayButton;
        b->setInstanceId(QStringLiteral("inst"));
        b->setFailureHoldMs(60);
        b->resize(b->sizeHint());
        b->show();
        return b;
    }

   private slots:
    void initTestCase() { Theme::setCurrent(ThemeSpec::obsidian()); }

    void successfulLaunchCycle()
    {
        std::unique_ptr<ZeroPlayButton> b(make());
        QCOMPARE(b->state(), State::Idle);
        QCOMPARE(b->stateText(), QStringLiteral("Play"));
        b->onPhaseChanged("inst", Phase::Preparing);
        QCOMPARE(b->state(), State::Preparing);
        QCOMPARE(b->stateText(), QStringLiteral("Launching…"));
        b->onPhaseChanged("inst", Phase::Running);
        QCOMPARE(b->state(), State::Running);
        QCOMPARE(b->stateText(), QStringLiteral("Running"));
        // The game exits: launchEnded(success) then phase Idle.
        b->onLaunchEnded("inst", true, false, true);
        b->onPhaseChanged("inst", Phase::Idle);
        QCOMPARE(b->state(), State::Idle);
    }

    void failureShowsMessageThenIdle()
    {
        std::unique_ptr<ZeroPlayButton> b(make());
        b->onPhaseChanged("inst", Phase::Preparing);
        // Application order: launchEnded first, then the Idle phase.
        b->onLaunchEnded("inst", false, false, false);
        b->onPhaseChanged("inst", Phase::Idle);
        QCOMPARE(b->state(), State::Failed);
        QCOMPARE(b->stateText(), QStringLiteral("Launch failed"));
        QTRY_COMPARE_WITH_TIMEOUT(b->state(), State::Idle, 2000);
        QCOMPARE(b->stateText(), QStringLiteral("Play"));
    }

    void abortedGoesIdleSilently()
    {
        std::unique_ptr<ZeroPlayButton> b(make());
        b->onPhaseChanged("inst", Phase::Preparing);
        b->onLaunchEnded("inst", false, true, false);
        b->onPhaseChanged("inst", Phase::Idle);
        QCOMPARE(b->state(), State::Idle);
    }

    void crashAfterStartIsNotALaunchFailure()
    {
        std::unique_ptr<ZeroPlayButton> b(make());
        b->onPhaseChanged("inst", Phase::Preparing);
        b->onPhaseChanged("inst", Phase::Running);
        b->onLaunchEnded("inst", false, false, true);
        b->onPhaseChanged("inst", Phase::Idle);
        QCOMPARE(b->state(), State::Idle);
    }

    void otherInstancesAreIgnored()
    {
        std::unique_ptr<ZeroPlayButton> b(make());
        b->onPhaseChanged("other", Phase::Preparing);
        QCOMPARE(b->state(), State::Idle);
        b->onLaunchEnded("other", false, false, false);
        QCOMPARE(b->state(), State::Idle);
    }

    void clicksAreIgnoredWhilePreparing()
    {
        std::unique_ptr<ZeroPlayButton> b(make());
        QSignalSpy play(b.get(), &ZeroPlayButton::playRequested);
        QSignalSpy stop(b.get(), &ZeroPlayButton::stopRequested);
        b->onPhaseChanged("inst", Phase::Preparing);
        QTest::mouseClick(b.get(), Qt::LeftButton);
        QTest::keyClick(b.get(), Qt::Key_Space);
        QTest::keyClick(b.get(), Qt::Key_Return);
        QCOMPARE(play.count(), 0);
        QCOMPARE(stop.count(), 0);
    }

    void idleClickRequestsPlay()
    {
        std::unique_ptr<ZeroPlayButton> b(make());
        QSignalSpy play(b.get(), &ZeroPlayButton::playRequested);
        QTest::mouseClick(b.get(), Qt::LeftButton);
        QCOMPARE(play.count(), 1);
        QCOMPARE(play.first().first().toString(), QStringLiteral("inst"));
        QTest::keyClick(b.get(), Qt::Key_Return);
        QCOMPARE(play.count(), 2);
    }

    void runningClickRequestsStop()
    {
        std::unique_ptr<ZeroPlayButton> b(make());
        QSignalSpy play(b.get(), &ZeroPlayButton::playRequested);
        QSignalSpy stop(b.get(), &ZeroPlayButton::stopRequested);
        b->onPhaseChanged("inst", Phase::Preparing);
        b->onPhaseChanged("inst", Phase::Running);
        QTest::mouseClick(b.get(), Qt::LeftButton);
        QCOMPARE(stop.count(), 1);
        QCOMPARE(play.count(), 0);
    }

    void disabledWhenItCannotLaunch()
    {
        std::unique_ptr<ZeroPlayButton> b(make());
        b->setCanLaunch(false);
        QVERIFY(!b->isEnabled());
        b->onPhaseChanged("inst", Phase::Preparing);
        QVERIFY(b->isEnabled());  // a launch in progress stays visible and enabled
        b->onPhaseChanged("inst", Phase::Idle);
        QVERIFY(!b->isEnabled());
    }
};

QTEST_MAIN(ZeroPlayButtonTest)
#include "ZeroPlayButton_test.moc"
