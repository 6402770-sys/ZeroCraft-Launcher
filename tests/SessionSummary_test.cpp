// SPDX-License-Identifier: GPL-3.0-only
// SessionSummary: the screenshots of a session window (inside / outside), crashed and short sessions give no summary, persistence of the
// last summary (survives a restart until dismissed, corrupt file ignored, backups added later), and the Last session card.
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/pages/LastSessionCard.h"
#include "zero/session/SessionSummary.h"

using namespace Zero;
using namespace Zero::Session;

namespace {

/// Writes a small image and stamps its modified time.
QString shot(const QString& dir, const QString& name, qint64 modifiedMs)
{
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(name);
    QImage img(8, 8, QImage::Format_RGB32);
    img.fill(Qt::darkGreen);
    if (!img.save(path, "PNG"))
        return {};
    QFile f(path);
    if (f.open(QIODevice::ReadWrite)) {
        f.setFileTime(QDateTime::fromMSecsSinceEpoch(modifiedMs), QFileDevice::FileModificationTime);
        f.close();
    }
    return path;
}

EndedSession ended(qint64 start, qint64 end, bool crashed = false)
{
    EndedSession e;
    e.instanceId = QStringLiteral("inst");
    e.instanceName = QStringLiteral("Survival");
    e.startedMs = start;
    e.endedMs = end;
    e.crashed = crashed;
    return e;
}

}  // namespace

class SessionSummaryTest : public QObject {
    Q_OBJECT

   private slots:
    void initTestCase() { Theme::setCurrent(ThemeSpec::obsidian()); }

    void screenshotsInsideAndOutsideTheWindow()
    {
        QTemporaryDir tmp;
        const QString dir = tmp.filePath(QStringLiteral("screenshots"));
        const qint64 start = 1'700'000'000'000;
        const qint64 end = start + 30 * 60 * 1000;
        shot(dir, QStringLiteral("before.png"), start - 1000);
        const QString first = shot(dir, QStringLiteral("a_first.png"), start + 1000);
        const QString last = shot(dir, QStringLiteral("z_last.png"), end);
        const QString grace = shot(dir, QStringLiteral("grace.jpg"), end + 3000);
        shot(dir, QStringLiteral("after.png"), end + kScreenshotGraceMs + 2000);
        QFile text(QDir(dir).filePath(QStringLiteral("notes.txt")));
        QVERIFY(text.open(QIODevice::WriteOnly));
        text.close();
        QVERIFY(QFile::setPermissions(text.fileName(), QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        QFile(text.fileName()).setFileTime(QDateTime::fromMSecsSinceEpoch(start + 5000), QFileDevice::FileModificationTime);

        const QStringList got = screenshotsTakenDuring(dir, start, end);
        QCOMPARE(got, (QStringList{ first, last, grace }));  // oldest first; the text file and the out-of-window ones are left out
        QVERIFY(screenshotsTakenDuring(tmp.filePath(QStringLiteral("missing")), start, end).isEmpty());
    }

    void summaryRules()
    {
        QTemporaryDir tmp;
        const QString dir = tmp.filePath(QStringLiteral("screenshots"));
        const qint64 start = 1'700'000'000'000;
        shot(dir, QStringLiteral("one.png"), start + 50'000);

        const auto ok = buildSummary(ended(start, start + 90'000), dir);
        QVERIFY(ok);
        QCOMPARE(ok->instanceName, QStringLiteral("Survival"));
        QCOMPARE(ok->durationSeconds(), qint64(90));
        QCOMPARE(ok->screenshots.size(), 1);
        QCOMPARE(ok->worldsBackedUp, 0);

        QVERIFY(buildSummary(ended(start, start + 59'999), dir) == std::nullopt);      // shorter than a minute
        QVERIFY(buildSummary(ended(start, start + 60'000), dir).has_value());          // exactly a minute counts
        QVERIFY(buildSummary(ended(start, start + 3'600'000, true), dir) == std::nullopt);  // crashed
        QVERIFY(buildSummary(ended(start, start - 5), dir) == std::nullopt);           // clock went backwards
    }

    void noScreenshotsNoBackupStillASummary()
    {
        QTemporaryDir tmp;
        const auto s = buildSummary(ended(1000, 1000 + 120'000), tmp.filePath(QStringLiteral("none")));
        QVERIFY(s);
        QVERIFY(s->screenshots.isEmpty());
    }

    void storeSurvivesARestartUntilDismissed()
    {
        QTemporaryDir tmp;
        const QString file = tmp.filePath(QStringLiteral("zero/last-session.json"));
        LastSession s;
        s.instanceId = QStringLiteral("inst");
        s.instanceName = QStringLiteral("Survival");
        s.startedMs = 1000;
        s.endedMs = 500'000;
        s.screenshots = { QStringLiteral("C:/x/a.png"), QStringLiteral("C:/x/b.png") };
        {
            LastSessionStore store(file);
            QVERIFY(!store.current());
            QSignalSpy changed(&store, &LastSessionStore::changed);
            store.set(s);
            QCOMPARE(changed.count(), 1);
        }
        {
            LastSessionStore again(file);  // "restart"
            QVERIFY(again.current());
            QCOMPARE(*again.current(), s);
            // the next session replaces it
            LastSession next = s;
            next.instanceName = QStringLiteral("Other");
            next.endedMs = 900'000;
            again.set(next);
            QCOMPARE(again.current()->instanceName, QStringLiteral("Other"));
        }
        {
            LastSessionStore third(file);
            QCOMPARE(third.current()->instanceName, QStringLiteral("Other"));
            QSignalSpy changed(&third, &LastSessionStore::changed);
            third.dismiss();
            QVERIFY(!third.current());
            QCOMPARE(changed.count(), 1);
            third.dismiss();  // nothing left: no second signal
            QCOMPARE(changed.count(), 1);
        }
        LastSessionStore fourth(file);
        QVERIFY(!fourth.current());  // dismissed stays dismissed
    }

    void backupsAddedAfterTheGameEnded()
    {
        QTemporaryDir tmp;
        const QString file = tmp.filePath(QStringLiteral("last.json"));
        LastSessionStore store(file);
        LastSession s;
        s.instanceId = QStringLiteral("inst");
        s.startedMs = 1;
        s.endedMs = 100'000;
        store.set(s);
        store.addBackups(QStringLiteral("other"), 3);  // another instance: ignored
        QCOMPARE(store.current()->worldsBackedUp, 0);
        store.addBackups(QStringLiteral("inst"), 0);
        QCOMPARE(store.current()->worldsBackedUp, 0);
        store.addBackups(QStringLiteral("inst"), 2);
        QCOMPARE(store.current()->worldsBackedUp, 2);
        LastSessionStore reread(file);
        QCOMPARE(reread.current()->worldsBackedUp, 2);
    }

    void corruptAndMalformedFilesAreIgnored()
    {
        QTemporaryDir tmp;
        const QString file = tmp.filePath(QStringLiteral("last.json"));
        for (const QByteArray& bad : { QByteArray("not json"), QByteArray("[1,2]"), QByteArray("{}"),
                                       QByteArray("{\"instanceId\":\"a\",\"startedMs\":10,\"endedMs\":5}") }) {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(bad);
            f.close();
            LastSessionStore store(file);
            QVERIFY(!store.current());
        }
    }

    // ----- Last session card -----

    void cardShowsThumbnailsPlusMoreAndBackupLine()
    {
        QTemporaryDir tmp;
        const QString dir = tmp.filePath(QStringLiteral("screenshots"));
        LastSession s;
        s.instanceId = QStringLiteral("inst");
        s.instanceName = QStringLiteral("Survival");
        s.startedMs = 1'700'000'000'000;
        s.endedMs = s.startedMs + 80 * 60 * 1000;
        for (int i = 0; i < 9; ++i)
            s.screenshots << shot(dir, QStringLiteral("s%1.png").arg(i), s.startedMs + 1000 * (i + 1));
        s.screenshots << QDir(dir).filePath(QStringLiteral("deleted.png"));  // gone since: not shown, not counted
        s.worldsBackedUp = 2;
        LastSessionCard card(s);
        QCOMPARE(card.thumbnailCount(), 6);
        QCOMPARE(card.extraCount(), 3);
        QVERIFY(card.hasBackupLine());
        QCOMPARE(card.durationLine(), QStringLiteral("You played 1 h 20 min"));
    }

    void cardOmitsEmptyParts()
    {
        LastSession s;
        s.instanceId = QStringLiteral("inst");
        s.instanceName = QStringLiteral("Survival");
        s.startedMs = 1000;
        s.endedMs = 1000 + 5 * 60 * 1000;
        LastSessionCard card(s);
        QCOMPARE(card.thumbnailCount(), 0);
        QCOMPARE(card.extraCount(), 0);
        QVERIFY(!card.hasBackupLine());
        QSignalSpy dismissed(&card, &LastSessionCard::dismissed);
        card.closeButton()->click();
        QCOMPARE(dismissed.count(), 1);
    }

    void cardWithFewScreenshotsHasNoMoreTile()
    {
        QTemporaryDir tmp;
        LastSession s;
        s.instanceId = QStringLiteral("inst");
        s.instanceName = QStringLiteral("Survival");
        s.startedMs = 1'700'000'000'000;
        s.endedMs = s.startedMs + 120'000;
        s.screenshots << shot(tmp.path(), QStringLiteral("a.png"), s.startedMs + 1000) << shot(tmp.path(), QStringLiteral("b.png"), s.startedMs + 2000);
        LastSessionCard card(s);
        QCOMPARE(card.thumbnailCount(), 2);
        QCOMPARE(card.extraCount(), 0);
    }
};

QTEST_MAIN(SessionSummaryTest)
#include "SessionSummary_test.moc"
