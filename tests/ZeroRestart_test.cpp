// SPDX-License-Identifier: GPL-3.0-only
// The restart wait: a launcher that was started by a restart waits for the one that started it before it asks for the single-instance lock.
#include <QElapsedTimer>
#include <QProcess>
#include <QTest>

#include "zero/ZeroRestart.h"

class ZeroRestartTest : public QObject {
    Q_OBJECT

   private slots:
    void cleanup() { qunsetenv(Zero::Restart::kWaitPidVariable); }

    void noVariableReturnsAtOnce()
    {
        QElapsedTimer timer;
        timer.start();
        Zero::Restart::waitForPredecessor(5000);
        QVERIFY(timer.elapsed() < 1000);
    }

    void aProcessThatIsGoneReturnsAtOnce()
    {
        // a process id that is not in use (Windows ids are multiples of 4; this one is far above any real id)
        qputenv(Zero::Restart::kWaitPidVariable, "4194300");
        QElapsedTimer timer;
        timer.start();
        Zero::Restart::waitForPredecessor(5000);
        QVERIFY(timer.elapsed() < 1000);
        QVERIFY2(qEnvironmentVariableIsEmpty(Zero::Restart::kWaitPidVariable), "the variable must not reach child processes");
    }

    void aGarbageValueReturnsAtOnce()
    {
        qputenv(Zero::Restart::kWaitPidVariable, "not a number");
        QElapsedTimer timer;
        timer.start();
        Zero::Restart::waitForPredecessor(5000);
        QVERIFY(timer.elapsed() < 1000);
    }

#ifdef Q_OS_WIN
    void waitsUntilTheProcessEnds()
    {
        QProcess predecessor;
        predecessor.start(QStringLiteral("ping"), { QStringLiteral("-n"), QStringLiteral("3"), QStringLiteral("127.0.0.1") });  // about 2 s
        QVERIFY(predecessor.waitForStarted(5000));
        qputenv(Zero::Restart::kWaitPidVariable, QByteArray::number(predecessor.processId()));
        QElapsedTimer timer;
        timer.start();
        Zero::Restart::waitForPredecessor(20000);
        QVERIFY2(timer.elapsed() > 800, "it returned while the predecessor still ran");
        QVERIFY2(timer.elapsed() < 15000, "it kept waiting after the predecessor ended");
        QVERIFY(predecessor.waitForFinished(1000));  // it has ended already; Qt only needs to notice
    }

    void givesUpAfterTheTimeout()
    {
        QProcess predecessor;
        predecessor.start(QStringLiteral("ping"), { QStringLiteral("-n"), QStringLiteral("30"), QStringLiteral("127.0.0.1") });
        QVERIFY(predecessor.waitForStarted(5000));
        qputenv(Zero::Restart::kWaitPidVariable, QByteArray::number(predecessor.processId()));
        QElapsedTimer timer;
        timer.start();
        Zero::Restart::waitForPredecessor(700);
        QVERIFY(timer.elapsed() >= 600);
        QVERIFY(timer.elapsed() < 5000);
        predecessor.kill();  // the process of this test, started above
        predecessor.waitForFinished(5000);
    }
#endif
};

QTEST_GUILESS_MAIN(ZeroRestartTest)
#include "ZeroRestart_test.moc"
