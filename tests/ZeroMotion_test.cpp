// SPDX-License-Identifier: GPL-3.0-only
// Motion system: setting resolution (incl. legacy migration), snapshot overlays (self-deleting, replaceable, no
// leftover effects, nothing created with motion off), press ripple and toasts.
#include <QApplication>
#include <QGraphicsEffect>
#include <QLabel>
#include <QPointer>
#include <QDialog>
#include <QPushButton>
#include <QSet>
#include <QTest>
#include <QVBoxLayout>
#include <memory>

#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroRipple.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroTheme.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroToast.h"
#include "ui/zero/ZeroTransition.h"

using namespace Zero;

class ZeroMotionTest : public QObject {
    Q_OBJECT

    static void setMotion(bool on)
    {
        ThemeSpec spec = ThemeSpec::obsidian();
        spec.reduceMotion = !on;
        Theme::setCurrent(spec);
    }

    static QWidget* makeHost()
    {
        auto* host = new QWidget;
        host->resize(320, 220);
        auto* lay = new QVBoxLayout(host);
        lay->addWidget(new QLabel(QStringLiteral("hello")));
        host->show();
        return host;
    }

   private slots:
    void initTestCase() { setThemeActive(true); }
    void cleanupTestCase()
    {
        setThemeActive(false);
        Theme::setCurrent(ThemeSpec::obsidian());
    }
    void init() { setMotion(true); }

    void tokens()
    {
        QCOMPARE(Tokens::durPress, 90);
        QCOMPARE(Tokens::durFast, 140);
        QCOMPARE(Tokens::durBase, 200);
        QCOMPARE(Tokens::durSlow, 280);
        QCOMPARE(Tokens::durTheme, 360);
        QCOMPARE(Tokens::durDissolve, 420);
    }

    void easing()
    {
        QCOMPARE(Motion::easeEnter().type(), QEasingCurve::OutCubic);
        QCOMPARE(Motion::easeExit().type(), QEasingCurve::InCubic);
        QCOMPARE(Motion::easeGlide().type(), QEasingCurve::OutQuint);
        QCOMPARE(Motion::easeRelease().type(), QEasingCurve::OutBack);
        QVERIFY(Motion::easeRelease().overshoot() <= 1.3);
    }

    void resolveEnabled_data()
    {
        QTest::addColumn<QString>("setting");
        QTest::addColumn<bool>("legacyReduce");
        QTest::addColumn<bool>("system");
        QTest::addColumn<bool>("expected");

        QTest::newRow("on ignores system") << "on" << false << false << true;
        QTest::newRow("on ignores legacy") << "on" << true << false << true;
        QTest::newRow("off ignores system") << "off" << false << true << false;
        QTest::newRow("system, animations on") << "system" << false << true << true;
        QTest::newRow("system, animations off") << "system" << false << false << false;
        QTest::newRow("explicit system beats legacy reduce") << "system" << true << true << true;
        QTest::newRow("never set, defaults to system (on)") << "" << false << true << true;
        QTest::newRow("never set, defaults to system (off)") << "" << false << false << false;
        QTest::newRow("never set + legacy reduce -> off") << "" << true << true << false;
        QTest::newRow("never set + legacy reduce + system off") << "" << true << false << false;
        QTest::newRow("unknown value behaves like never set") << "bogus" << false << true << true;
        QTest::newRow("unknown value + legacy reduce -> off") << "bogus" << true << true << false;
    }

    void resolveEnabled()
    {
        QFETCH(QString, setting);
        QFETCH(bool, legacyReduce);
        QFETCH(bool, system);
        QFETCH(bool, expected);
        QCOMPARE(Motion::resolveEnabled(setting, legacyReduce, system), expected);
    }

    void motionOffCreatesNothing()
    {
        std::unique_ptr<QWidget> host(makeHost());
        QVERIFY(QTest::qWaitForWindowExposed(host.get()));
        setMotion(false);
        QVERIFY(!Motion::enabled());

        const Transition::Snapshot snap = Transition::capture(host.get());
        QVERIFY(snap.isNull());
        for (auto kind : { Transition::Kind::Crossfade, Transition::Kind::Rise, Transition::Kind::PixelDissolve }) {
            Transition::run(host.get(), snap, kind, 100);
            QCOMPARE(Transition::overlayCount(host.get()), 0);
        }
        // Even a snapshot taken while motion was on creates nothing once motion is off.
        setMotion(true);
        const Transition::Snapshot stale = Transition::capture(host.get());
        QVERIFY(!stale.isNull());
        setMotion(false);
        Transition::run(host.get(), stale, Transition::Kind::Crossfade, 100);
        QCOMPARE(Transition::overlayCount(host.get()), 0);

        // StateAnimator jumps instead of animating.
        Motion::StateAnimator anim;
        anim.setActive(true);
        QCOMPARE(anim.progress(), 1.0);
        anim.setActive(false);
        QCOMPARE(anim.progress(), 0.0);

        // No ripple.
        auto* button = new QPushButton(QStringLiteral("Go"), host.get());
        button->setProperty("zeroVariant", "primary");
        button->show();
        Ripple::install();
        QTest::mousePress(button, Qt::LeftButton);
        QCOMPARE(Ripple::rippleCount(button), 0);
        QTest::mouseRelease(button, Qt::LeftButton);
    }

    void overlayDeletesItself_data()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("crossfade") << int(Transition::Kind::Crossfade);
        QTest::newRow("rise") << int(Transition::Kind::Rise);
        QTest::newRow("pixel dissolve") << int(Transition::Kind::PixelDissolve);
    }

    void overlayDeletesItself()
    {
        QFETCH(int, kind);
        std::unique_ptr<QWidget> host(makeHost());
        QVERIFY(QTest::qWaitForWindowExposed(host.get()));

        const Transition::Snapshot snap = Transition::capture(host.get());
        QVERIFY(!snap.isNull());
        Transition::run(host.get(), snap, Transition::Kind(kind), 120);
        QCOMPARE(Transition::overlayCount(host.get()), 1);

        auto* overlay = host->findChild<QWidget*>(QStringLiteral("zeroTransitionOverlay"));
        QVERIFY(overlay);
        QVERIFY(overlay->testAttribute(Qt::WA_TransparentForMouseEvents));
        QCOMPARE(overlay->parentWidget(), host.get());
        QCOMPARE(overlay->size(), host->size());
        QVERIFY(!overlay->grab().isNull());  // paints without crashing mid-flight

        QTRY_COMPARE_WITH_TIMEOUT(Transition::overlayCount(host.get()), 0, 3000);
        QVERIFY(!host->graphicsEffect());
        for (auto* child : host->findChildren<QWidget*>())
            QVERIFY2(!child->graphicsEffect(), "a transition must not leave a graphics effect behind");
    }

    void triggeringTwiceLeavesOneThenZero()
    {
        std::unique_ptr<QWidget> host(makeHost());
        QVERIFY(QTest::qWaitForWindowExposed(host.get()));

        // Fast repeated triggers, as when clicking through the nav rail.
        for (int i = 0; i < 5; ++i) {
            const Transition::Snapshot snap = Transition::capture(host.get());
            Transition::run(host.get(), snap, i % 2 ? Transition::Kind::Rise : Transition::Kind::Crossfade, 300);
            QCOMPARE(Transition::overlayCount(host.get()), 1);
            QTest::qWait(10);
        }
        QCOMPARE(Transition::overlayCount(host.get()), 1);
        QTRY_COMPARE_WITH_TIMEOUT(Transition::overlayCount(host.get()), 0, 3000);
        QVERIFY(!host->graphicsEffect());
    }

    void cancelRemovesOverlayImmediately()
    {
        std::unique_ptr<QWidget> host(makeHost());
        QVERIFY(QTest::qWaitForWindowExposed(host.get()));
        Transition::run(host.get(), Transition::capture(host.get()), Transition::Kind::Crossfade, 500);
        QCOMPARE(Transition::overlayCount(host.get()), 1);
        Transition::cancel(host.get());
        QCOMPARE(Transition::overlayCount(host.get()), 0);
    }

    void dissolveOrderIsAShuffledPermutation()
    {
        const QList<quint32> a = Transition::dissolveOrder(23, 17);
        QCOMPARE(a.size(), 23 * 17);
        QCOMPARE(QSet<quint32>(a.begin(), a.end()).size(), 23 * 17);  // every block exactly once
        QCOMPARE(Transition::dissolveOrder(23, 17), a);               // deterministic
        QVERIFY(a != Transition::dissolveOrder(23, 18));  // a different grid shuffles differently
        bool identity = true;
        for (int i = 0; i < a.size(); ++i)
            identity = identity && a[i] == quint32(i);
        QVERIFY(!identity);
        QVERIFY(Transition::dissolveOrder(0, 5).isEmpty());
    }

    void fadeInLeavesNoEffect()
    {
        std::unique_ptr<QWidget> host(makeHost());
        QVERIFY(QTest::qWaitForWindowExposed(host.get()));
        Motion::fadeIn(host.get(), 80);
        QVERIFY(host->graphicsEffect());
        QTRY_VERIFY_WITH_TIMEOUT(!host->graphicsEffect(), 2000);
    }

    void rippleAppearsAndRemovesItself()
    {
        std::unique_ptr<QWidget> host(makeHost());
        auto* button = new QPushButton(QStringLiteral("Go"), host.get());
        button->setProperty("zeroVariant", "primary");
        button->setGeometry(20, 100, 120, 32);
        button->show();
        QVERIFY(QTest::qWaitForWindowExposed(host.get()));
        Ripple::install();
        Ripple::install();  // idempotent

        QTest::mousePress(button, Qt::LeftButton, {}, QPoint(30, 10));
        QCOMPARE(Ripple::rippleCount(button), 1);
        QTest::mouseRelease(button, Qt::LeftButton, {}, QPoint(30, 10));
        QTRY_COMPARE_WITH_TIMEOUT(Ripple::rippleCount(button), 0, 3000);

        // Buttons without a ZERO variant get none.
        auto* plain = new QPushButton(QStringLiteral("Plain"), host.get());
        plain->setGeometry(160, 100, 100, 32);
        plain->show();
        QTest::mousePress(plain, Qt::LeftButton);
        QCOMPARE(Ripple::rippleCount(plain), 0);
        QTest::mouseRelease(plain, Qt::LeftButton);
    }

    void toastSlidesInAndOutWithoutLeftovers()
    {
        std::unique_ptr<QWidget> host(makeHost());
        host->resize(640, 400);
        QVERIFY(QTest::qWaitForWindowExposed(host.get()));
        ZeroToast* first = ZeroToast::show(host.get(), QStringLiteral("One"));
        ZeroToast* second = ZeroToast::show(host.get(), QStringLiteral("Two"));
        QCOMPARE(ZeroToast::count(host.get()), 2);
        QPointer<ZeroToast> guard(first);
        QTRY_VERIFY_WITH_TIMEOUT(!first->graphicsEffect(), 2000);  // entered: effect removed again
        first->dismiss();
        QCOMPARE(ZeroToast::count(host.get()), 1);
        QTRY_VERIFY_WITH_TIMEOUT(guard.isNull(), 3000);
        QVERIFY(second);
        QCOMPARE(ZeroToast::count(host.get()), 1);
        second->dismiss();
        QTRY_COMPARE_WITH_TIMEOUT(ZeroToast::count(host.get()), 0, 3000);
    }

    void dialogFadesIn()
    {
        // Installs the application hooks (popup filter incl. the dialog fade) the way the real theme does.
        ZeroTheme theme;
        theme.apply(true);
        QVERIFY(Motion::enabled());
        QDialog dialog;
        dialog.resize(200, 120);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QTRY_COMPARE_WITH_TIMEOUT(dialog.windowOpacity(), 1.0, 2000);  // never stuck transparent
        dialog.hide();
        dialog.show();  // shown again: stays opaque
        QCOMPARE(dialog.windowOpacity(), 1.0);
        qApp->setStyleSheet({});
    }

};

QTEST_MAIN(ZeroMotionTest)

#include "ZeroMotion_test.moc"
