// SPDX-License-Identifier: GPL-3.0-only
// Smooth wheel scrolling: eased glide to an accumulating target, motion off, edge propagation, pass-through cases.
#include <QApplication>
#include <QLabel>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTest>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <memory>

#include "ui/zero/ZeroSmoothScroll.h"
#include "ui/zero/ZeroTheme.h"
#include "ui/zero/ZeroThemeState.h"

using namespace Zero;

class ZeroSmoothScrollTest : public QObject {
    Q_OBJECT

    static void setMotion(bool on)
    {
        ThemeSpec spec = ThemeSpec::obsidian();
        spec.reduceMotion = !on;
        Theme::setCurrent(spec);
    }

    static int notch() { return QApplication::wheelScrollLines() * SmoothScroll::kPixelsPerLine; }

    static QWheelEvent wheel(int angleY, Qt::KeyboardModifiers mods = {}, QPoint pixel = {}, Qt::ScrollPhase phase = Qt::NoScrollPhase,
                             int angleX = 0)
    {
        return QWheelEvent(QPointF(10, 10), QPointF(10, 10), pixel, QPoint(angleX, angleY), Qt::NoButton, mods, phase, false);
    }

    struct Fixture {
        std::unique_ptr<QScrollArea> area = std::make_unique<QScrollArea>();
        QSpinBox* spin = nullptr;
        Fixture()
        {
            auto* inner = new QWidget;
            auto* lay = new QVBoxLayout(inner);
            spin = new QSpinBox;
            spin->setRange(0, 100);
            lay->addWidget(spin);
            auto* filler = new QLabel;
            filler->setFixedSize(150, 3000);
            lay->addWidget(filler);
            area->setWidget(inner);
            area->setWidgetResizable(true);
            area->resize(300, 300);
            area->show();
            QVERIFY2(QTest::qWaitForWindowExposed(area.get()), "window not exposed");
        }
    };

    static bool send(QScrollArea* area, QWheelEvent& e)
    {
        e.ignore();
        QApplication::sendEvent(area->viewport(), &e);
        return e.isAccepted();
    }

   private slots:
    void initTestCase()
    {
        SmoothScroll::install(qApp);
        SmoothScroll::install(qApp);  // idempotent
    }
    void init() { setMotion(true); }
    void cleanup() { setMotion(true); }

    void glidesToTarget()
    {
        Fixture f;
        QScrollBar* bar = f.area->verticalScrollBar();
        QVERIFY(bar->maximum() > 1000);
        auto e = wheel(-120);  // wheel down
        QVERIFY(send(f.area.get(), e));
        QVERIFY(bar->value() < notch());  // not yet jumped
        QTRY_COMPARE(bar->value(), notch());
    }

    void notchesAccumulate()
    {
        Fixture f;
        QScrollBar* bar = f.area->verticalScrollBar();
        for (int i = 0; i < 3; ++i) {
            auto e = wheel(-120);
            QVERIFY(send(f.area.get(), e));
        }
        QTRY_COMPARE(bar->value(), 3 * notch());
        auto up = wheel(120);
        QVERIFY(send(f.area.get(), up));
        QTRY_COMPARE(bar->value(), 2 * notch());
    }

    void highResolutionWheelIsProportional()
    {
        Fixture f;
        auto e = wheel(-60);
        QVERIFY(send(f.area.get(), e));
        QTRY_COMPARE(f.area->verticalScrollBar()->value(), notch() / 2);
    }

    void motionOffIsImmediate()
    {
        setMotion(false);
        Fixture f;
        auto e = wheel(-120);
        QVERIFY(send(f.area.get(), e));
        QCOMPARE(f.area->verticalScrollBar()->value(), notch());
    }

    void plainTextScrollsByLines()
    {
        setMotion(false);
        QPlainTextEdit edit;
        QStringList lines;
        for (int i = 0; i < 500; ++i)
            lines << QString::number(i);
        edit.setPlainText(lines.join(QLatin1Char('\n')));
        edit.resize(300, 200);
        edit.show();
        QVERIFY(QTest::qWaitForWindowExposed(&edit));
        auto e = wheel(-120);
        e.ignore();
        QApplication::sendEvent(edit.viewport(), &e);
        QVERIFY(e.isAccepted());
        QCOMPARE(edit.verticalScrollBar()->value(), QApplication::wheelScrollLines());
    }

    void edgeIsNotConsumed()
    {
        Fixture f;
        QScrollBar* bar = f.area->verticalScrollBar();
        bar->setValue(bar->maximum());
        auto down = wheel(-120);
        QVERIFY(!send(f.area.get(), down));
        bar->setValue(0);
        auto up = wheel(120);
        QVERIFY(!send(f.area.get(), up));
    }

    void ctrlPassesThrough()
    {
        Fixture f;
        auto e = wheel(-120, Qt::ControlModifier);
        send(f.area.get(), e);
        // Not claimed by the filter: Qt handles it synchronously, so no glide follows.
        const int v = f.area->verticalScrollBar()->value();
        QTest::qWait(350);
        QCOMPARE(f.area->verticalScrollBar()->value(), v);
    }

    void touchpadPassesThrough()
    {
        Fixture f;
        // The filter must not claim pixel-delta / phased events: nothing glides, so Qt's own handling moves the bar at once.
        auto e = wheel(-120, {}, QPoint(0, -30), Qt::ScrollUpdate);
        send(f.area.get(), e);
        QVERIFY(f.area->verticalScrollBar()->value() < notch());
        QTest::qWait(350);
        QVERIFY(f.area->verticalScrollBar()->value() < notch());  // no delayed glide to the notch target
    }

    void shiftScrollsHorizontally()
    {
        Fixture f;
        f.area->widget()->setMinimumWidth(1500);
        f.area->setWidgetResizable(false);
        f.area->widget()->resize(1500, 3000);
        QScrollBar* h = f.area->horizontalScrollBar();
        QVERIFY(h->maximum() > 0);
        auto e = wheel(-120, Qt::ShiftModifier);
        QVERIFY(send(f.area.get(), e));
        QTRY_COMPARE(h->value(), notch());
        QCOMPARE(f.area->verticalScrollBar()->value(), 0);
    }

    void optOutIsRespected()
    {
        Fixture f;
        f.area->setProperty(SmoothScroll::kOptOutProperty, true);
        auto e = wheel(-120);
        send(f.area.get(), e);
        // Qt's own stepping (no glide): value is set synchronously and is not the filter's notch distance.
        QVERIFY(f.area->verticalScrollBar()->value() > 0);
        const int v = f.area->verticalScrollBar()->value();
        QTest::qWait(350);
        QCOMPARE(f.area->verticalScrollBar()->value(), v);
    }

    void spinBoxKeepsItsWheel()
    {
        Fixture f;
        f.spin->setFocusPolicy(Qt::WheelFocus);
        const QPointF pos(f.spin->width() / 2.0, f.spin->height() / 2.0);
        QWheelEvent e(pos, f.spin->mapToGlobal(pos), QPoint(), QPoint(0, 120), Qt::NoButton, {}, Qt::NoScrollPhase, false);
        e.ignore();
        QApplication::sendEvent(f.spin, &e);
        QCOMPARE(f.spin->value(), 1);
        QTest::qWait(350);
        QCOMPARE(f.area->verticalScrollBar()->value(), 0);
    }

    void externalChangeStopsGlide()
    {
        Fixture f;
        QScrollBar* bar = f.area->verticalScrollBar();
        auto e = wheel(-120);
        QVERIFY(send(f.area.get(), e));
        QTest::qWait(30);
        bar->setValue(500);
        QTest::qWait(350);
        QCOMPARE(bar->value(), 500);
        // A new notch starts from the current value, not from the dropped target.
        auto e2 = wheel(-120);
        QVERIFY(send(f.area.get(), e2));
        QTRY_COMPARE(bar->value(), 500 + notch());
    }
};

QTEST_MAIN(ZeroSmoothScrollTest)

#include "ZeroSmoothScroll_test.moc"
