// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the Downloads center core (docs/zero/ARCHITECTURE.md, "Downloads center"): starting a task in the background,
// bytes / fraction / speed tracking, cancel, the one-shot onFinished, the history file and the text helpers. No widgets, no network: the
// tasks are driven by hand and the clock is injected.
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "tasks/Task.h"
#include "zero/downloads/DownloadCenter.h"

using namespace Zero::Downloads;

namespace {

QString S(const char* text)
{
    return QString::fromUtf8(text);
}

constexpr qint64 kMB = 1024 * 1024;

/// A task the test drives by hand.
class FakeTask : public Task {
    Q_OBJECT
   public:
    explicit FakeTask(bool abortable = true) : Task(false) { setAbortable(abortable); }

    void step(const QUuid& uid, qint64 current, qint64 total)
    {
        TaskStepProgress p(uid);
        p.update(current, total);
        emit stepProgress(p);
    }
    void progressOf(qint64 current, qint64 total) { setProgress(current, total); }
    void say(const QString& status, const QString& details)
    {
        setStatus(status);
        setDetails(details);
    }
    void succeed() { emitSucceeded(); }
    void fail(const QString& reason) { emitFailed(reason); }
    void warn(const QString& line) { logWarning(line); }
    /// Both end signals without touching the task state (what a task that fails while aborting can do).
    void rawFailedThenAborted()
    {
        emit failed(QStringLiteral("late failure"));
        emit aborted();
    }

   protected:
    void executeTask() override {}
};

/// Ends with a failure when it is asked to abort.
class FailOnAbortTask : public Task {
    Q_OBJECT
   public:
    FailOnAbortTask() : Task(false) { setAbortable(true); }
    bool abort() override
    {
        emitFailed(QStringLiteral("Failed to abort all running tasks."));
        return true;
    }

   protected:
    void executeTask() override {}
};

/// Ends inside start().
class InstantTask : public Task {
    Q_OBJECT
   public:
    InstantTask() : Task(false) {}

   protected:
    void executeTask() override { emitSucceeded(); }
};

struct Rig {
    QTemporaryDir dir;
    qint64 now = 1'700'000'000'000;
    QString path;
    std::unique_ptr<DownloadCenter> center;

    Rig()
    {
        path = dir.filePath(S("zero/downloads.json"));
        reopen();
    }
    void reopen() { center.reset(new DownloadCenter(path, [this]() { return now; })); }

    struct Started {
        QString id;
        QSharedPointer<FakeTask> task;
    };

    Started startFake(const QString& title, const QString& instanceId = {}, Kind kind = Kind::Mod, int* finishedCalls = nullptr,
                      DownloadResult* last = nullptr, bool abortable = true)
    {
        auto task = QSharedPointer<FakeTask>(new FakeTask(abortable), &QObject::deleteLater);
        DownloadJob job;
        job.title = title;
        job.instanceId = instanceId;
        job.instanceName = instanceId.isEmpty() ? QString() : S("Fabric Fun");
        job.kind = kind;
        job.task = Task::Ptr(task);
        job.onFinished = [finishedCalls, last](const DownloadResult& r) {
            if (finishedCalls)
                ++*finishedCalls;
            if (last)
                *last = r;
        };
        return { center->start(job), task };
    }
};

}  // namespace

class DownloadCenterTest : public QObject {
    Q_OBJECT

   private slots:
    void startReturnsAtOnceAndTheJobIsActive()
    {
        Rig rig;
        QSignalSpy added(rig.center.get(), &DownloadCenter::jobAdded);
        QSignalSpy count(rig.center.get(), &DownloadCenter::activeCountChanged);
        auto s = rig.startFake(S("Sodium"), S("inst-1"), Kind::Mod);
        QVERIFY(!s.id.isEmpty());
        QVERIFY(s.task->isRunning());  // started, not finished: start() did not wait
        QCOMPARE(rig.center->activeCount(), 1);
        QCOMPARE(added.size(), 1);
        QCOMPARE(added.first().first().toString(), s.id);
        QCOMPARE(count.size(), 1);
        QCOMPARE(count.first().first().toInt(), 1);
        const auto jobs = rig.center->activeJobs();
        QCOMPARE(jobs.size(), 1);
        QCOMPARE(jobs.first().title, S("Sodium"));
        QCOMPARE(jobs.first().instanceName, S("Fabric Fun"));
        QCOMPARE(jobs.first().kind, Kind::Mod);
        QCOMPARE(jobs.first().fraction, -1.0);
        QVERIFY(jobs.first().canCancel);
        QVERIFY(rig.center->history().isEmpty());
    }

    void startWithoutATaskDoesNothing()
    {
        Rig rig;
        DownloadJob job;
        job.title = S("nothing");
        QVERIFY(rig.center->start(job).isEmpty());
        QCOMPARE(rig.center->activeCount(), 0);
    }

    void bytesAndFractionAreSummedOverSubSteps()
    {
        Rig rig;
        auto s = rig.startFake(S("Pack"));
        const QUuid a = QUuid::createUuid();
        const QUuid b = QUuid::createUuid();
        s.task->step(a, 1 * kMB, 4 * kMB);
        s.task->step(b, 2 * kMB, 6 * kMB);
        auto job = *rig.center->activeJob(s.id);
        QCOMPARE(job.bytesDone, 3 * kMB);
        QCOMPARE(job.bytesTotal, 10 * kMB);
        QVERIFY(job.bytesKnown());
        QVERIFY(qAbs(job.fraction - 0.3) < 1e-9);

        s.task->step(a, 4 * kMB, 4 * kMB);
        job = *rig.center->activeJob(s.id);
        QCOMPARE(job.bytesDone, 6 * kMB);
        QVERIFY(qAbs(job.fraction - 0.6) < 1e-9);

        // a third step appears: the byte counts grow, the bar does not run backwards
        const QUuid c = QUuid::createUuid();
        s.task->step(c, 0, 10 * kMB);
        job = *rig.center->activeJob(s.id);
        QCOMPARE(job.bytesTotal, 20 * kMB);
        QVERIFY(qAbs(job.fraction - 0.6) < 1e-9);
        s.task->step(c, 10 * kMB, 10 * kMB);
        s.task->step(b, 6 * kMB, 6 * kMB);
        job = *rig.center->activeJob(s.id);
        QVERIFY(qAbs(job.fraction - 1.0) < 1e-9);
    }

    void stepsWithoutSizeAreIgnoredAndProgressIsTheFallback()
    {
        Rig rig;
        auto s = rig.startFake(S("Count only"));
        s.task->step(QUuid::createUuid(), 5, -1);  // size unknown
        auto job = *rig.center->activeJob(s.id);
        QCOMPARE(job.bytesTotal, 0);
        QCOMPARE(job.fraction, -1.0);

        s.task->progressOf(1, 4);  // the task only counts items
        job = *rig.center->activeJob(s.id);
        QVERIFY(qAbs(job.fraction - 0.25) < 1e-9);
        QVERIFY(!job.bytesKnown());

        // a sum below kMinBytes is a count of steps, not bytes
        s.task->step(QUuid::createUuid(), 2, 4);
        job = *rig.center->activeJob(s.id);
        QVERIFY(!job.bytesKnown());
        QVERIFY(qAbs(job.fraction - 0.25) < 1e-9);  // the small sum is not trusted as bytes: progress() stays the source
    }

    void statusPrefersDetails()
    {
        Rig rig;
        auto s = rig.startFake(S("Pack"));
        s.task->say(S("Executing 1 task:"), S("sodium.jar"));
        QCOMPARE(rig.center->activeJob(s.id)->status, S("sodium.jar"));
        s.task->say(S("Executing 1 task:"), QString());
        QCOMPARE(rig.center->activeJob(s.id)->status, S("Executing 1 task:"));
    }

    void speedIsAnAverageOverTheLastThreeSeconds()
    {
        Rig rig;
        auto s = rig.startFake(S("Pack"));
        QCOMPARE(rig.center->activeJob(s.id)->speed, 0.0);

        const QUuid a = QUuid::createUuid();
        rig.now += 1000;
        s.task->step(a, 3 * kMB, 30 * kMB);
        QVERIFY(qAbs(rig.center->activeJob(s.id)->speed - 3.0 * kMB) < 0.02 * kMB);  // 3 MB in 1 s

        rig.now += 1000;
        s.task->step(a, 6 * kMB, 30 * kMB);
        QVERIFY(qAbs(rig.center->activeJob(s.id)->speed - 3.0 * kMB) < 0.02 * kMB);  // 6 MB in 2 s

        rig.now += 1000;
        s.task->step(a, 15 * kMB, 30 * kMB);  // a burst: 15 MB in 3 s (the window starts at the beginning)
        QVERIFY(qAbs(rig.center->activeJob(s.id)->speed - 5.0 * kMB) < 0.05 * kMB);

        // nothing arrives any more: the average fades and reaches 0 once the last bytes left the window
        rig.now += 1500;
        const double fading = rig.center->activeJob(s.id)->speed;
        QVERIFY(fading > 0 && fading < 5.0 * kMB);
        rig.now += 10'000;
        QCOMPARE(rig.center->activeJob(s.id)->speed, 0.0);
    }

    void cancelEndsTheJobAsCancelledAndCallsOnFinishedOnce()
    {
        Rig rig;
        int calls = 0;
        DownloadResult result;
        auto s = rig.startFake(S("Pack"), S("inst-1"), Kind::Modpack, &calls, &result);
        QSignalSpy finished(rig.center.get(), &DownloadCenter::jobFinished);
        QVERIFY(rig.center->cancel(s.id));
        QCOMPARE(calls, 1);
        QCOMPARE(result.state, DownloadResult::State::Cancelled);
        QVERIFY(result.error.isEmpty());
        QCOMPARE(rig.center->activeCount(), 0);
        QCOMPARE(finished.size(), 1);
        QCOMPARE(rig.center->history().size(), 1);
        QCOMPARE(rig.center->history().first().state, DownloadResult::State::Cancelled);
        QVERIFY(!rig.center->cancel(s.id));  // gone
        QCOMPARE(calls, 1);
    }

    void aTaskThatCannotAbortIsNotCancelled()
    {
        Rig rig;
        int calls = 0;
        auto s = rig.startFake(S("Pack"), {}, Kind::Mixed, &calls, nullptr, /*abortable*/ false);
        QVERIFY(!rig.center->activeJob(s.id)->canCancel);
        QVERIFY(!rig.center->cancel(s.id));
        QCOMPARE(rig.center->activeCount(), 1);
        QCOMPARE(calls, 0);
        rig.center->cancelAll();  // does not change that
        QCOMPARE(rig.center->activeCount(), 1);
        s.task->succeed();
        QCOMPARE(calls, 1);
    }

    void failedAndAbortedBothFiringCallOnFinishedOnce()
    {
        Rig rig;
        int calls = 0;
        DownloadResult result;
        auto s = rig.startFake(S("Pack"), {}, Kind::Mixed, &calls, &result);
        s.task->rawFailedThenAborted();
        QCOMPARE(calls, 1);
        QCOMPARE(result.state, DownloadResult::State::Failed);  // the first one counts
        QCOMPARE(result.error, S("late failure"));
        QCOMPARE(rig.center->history().size(), 1);
    }

    void aFailureAfterCancelCountsAsCancelled()
    {
        Rig rig;
        int calls = 0;
        DownloadResult result;
        DownloadJob job;
        job.title = S("unwinds badly");
        job.task = Task::Ptr(new FailOnAbortTask);  // reports a failure instead of an abort when asked to stop
        job.onFinished = [&](const DownloadResult& r) {
            ++calls;
            result = r;
        };
        const QString id = rig.center->start(job);
        QVERIFY(rig.center->cancel(id));
        QCOMPARE(calls, 1);
        QCOMPARE(result.state, DownloadResult::State::Cancelled);  // the user asked to stop it: not a failure
        QVERIFY(result.error.isEmpty());
    }

    void successKeepsBytesWarningsAndLeavesTheActiveList()
    {
        Rig rig;
        int calls = 0;
        DownloadResult result;
        auto s = rig.startFake(S("Fabric Fun pack"), S("inst-1"), Kind::Modpack, &calls, &result);
        QSignalSpy finished(rig.center.get(), &DownloadCenter::jobFinished);
        QSignalSpy history(rig.center.get(), &DownloadCenter::historyChanged);
        QSignalSpy count(rig.center.get(), &DownloadCenter::activeCountChanged);
        s.task->step(QUuid::createUuid(), 30 * kMB, 30 * kMB);
        s.task->warn(S("a mod has no hash"));
        rig.now += 5000;
        s.task->succeed();
        QCOMPARE(calls, 1);
        QCOMPARE(result.state, DownloadResult::State::Done);
        QCOMPARE(result.bytes, 30 * kMB);
        QCOMPARE(result.warnings, QStringList{ S("a mod has no hash") });
        QCOMPARE(rig.center->activeCount(), 0);
        QVERIFY(!rig.center->isBusy(S("inst-1")));
        QCOMPARE(finished.size(), 1);
        QCOMPARE(history.size(), 1);
        QCOMPARE(count.size(), 1);
        QCOMPARE(count.first().first().toInt(), 0);
        const HistoryEntry e = rig.center->history().first();
        QCOMPARE(e.id, s.id);
        QCOMPARE(e.title, S("Fabric Fun pack"));
        QCOMPARE(e.instanceId, S("inst-1"));
        QCOMPARE(e.instanceName, S("Fabric Fun"));
        QCOMPARE(e.kind, Kind::Modpack);
        QCOMPARE(e.state, DownloadResult::State::Done);
        QCOMPARE(e.bytes, 30 * kMB);
        QCOMPARE(e.finishedAt.timeSpec(), Qt::UTC);
        QCOMPARE(e.finishedMs(), rig.now);
    }

    void failureKeepsTheError()
    {
        Rig rig;
        int calls = 0;
        DownloadResult result;
        auto s = rig.startFake(S("Pack"), {}, Kind::Mod, &calls, &result);
        s.task->fail(S("HTTP 404 for sodium.jar"));
        QCOMPARE(calls, 1);
        QCOMPARE(result.state, DownloadResult::State::Failed);
        QCOMPARE(result.error, S("HTTP 404 for sodium.jar"));
        QCOMPARE(rig.center->history().first().error, S("HTTP 404 for sodium.jar"));
    }

    void aTaskThatEndsInsideStartIsStillReported()
    {
        Rig rig;
        int calls = 0;
        DownloadJob job;
        job.title = S("instant");
        job.task = Task::Ptr(new InstantTask);
        job.onFinished = [&calls](const DownloadResult& r) {
            QCOMPARE(r.state, DownloadResult::State::Done);
            ++calls;
        };
        QSignalSpy added(rig.center.get(), &DownloadCenter::jobAdded);
        const QString id = rig.center->start(job);
        QVERIFY(!id.isEmpty());
        QCOMPARE(added.size(), 1);  // listed before the task ran
        QCOMPARE(calls, 1);
        QCOMPARE(rig.center->activeCount(), 0);
        QCOMPARE(rig.center->history().size(), 1);
    }

    void isBusyIsPerInstance()
    {
        Rig rig;
        QVERIFY(!rig.center->isBusy(S("a")));
        QVERIFY(!rig.center->isBusy(QString()));
        auto one = rig.startFake(S("One"), S("a"));
        auto two = rig.startFake(S("Two"), S("a"));
        auto other = rig.startFake(S("Other"), S("b"));
        auto none = rig.startFake(S("No instance"));
        QVERIFY(rig.center->isBusy(S("a")));
        QVERIFY(rig.center->isBusy(S("b")));
        QVERIFY(!rig.center->isBusy(S("c")));
        QVERIFY(!rig.center->isBusy(QString()));  // a job without an instance blocks nothing
        one.task->succeed();
        QVERIFY(rig.center->isBusy(S("a")));  // the second one still runs
        two.task->fail(S("x"));
        QVERIFY(!rig.center->isBusy(S("a")));
        QVERIFY(rig.center->isBusy(S("b")));
        other.task->succeed();
        none.task->succeed();
        QCOMPARE(rig.center->activeCount(), 0);
    }

    void activeJobsAreListedOldestFirst()
    {
        Rig rig;
        auto a = rig.startFake(S("A"));
        auto b = rig.startFake(S("B"));
        auto c = rig.startFake(S("C"));
        const auto jobs = rig.center->activeJobs();
        QCOMPARE(jobs.size(), 3);
        QCOMPARE(jobs[0].title, S("A"));
        QCOMPARE(jobs[2].title, S("C"));
        b.task->succeed();
        QCOMPARE(rig.center->activeJobs().size(), 2);
        QCOMPARE(rig.center->activeJobs()[1].title, S("C"));
    }

    void historyIsNewestFirstAndCappedAt100()
    {
        Rig rig;
        for (int i = 1; i <= 105; ++i) {
            rig.now += 1000;
            auto s = rig.startFake(S("job %1").arg(i));
            s.task->succeed();
        }
        const auto& h = rig.center->history();
        QCOMPARE(h.size(), kMaxHistory);
        QCOMPARE(h.first().title, S("job 105"));
        QCOMPARE(h.last().title, S("job 6"));  // the 5 oldest were dropped
        QVERIFY(h[0].finishedMs() > h[1].finishedMs());
    }

    void historyIsPersistedAndReloaded()
    {
        Rig rig;
        rig.now += 1000;
        auto a = rig.startFake(S("Sodium"), S("inst-1"), Kind::Mod);
        a.task->step(QUuid::createUuid(), 2 * kMB, 2 * kMB);
        a.task->succeed();
        rig.now += 1000;
        auto b = rig.startFake(S("Shader"), S("inst-2"), Kind::ShaderPack);
        b.task->fail(S("disk full"));
        rig.now += 1000;
        auto c = rig.startFake(S("World"), S("inst-1"), Kind::World);
        rig.center->cancel(c.id);
        const QList<HistoryEntry> before = rig.center->history();
        QVERIFY(QFile::exists(rig.path));

        rig.reopen();  // a new center on the same file
        const QList<HistoryEntry> after = rig.center->history();
        QCOMPARE(after.size(), 3);
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(after[i].id, before[i].id);
            QCOMPARE(after[i].title, before[i].title);
            QCOMPARE(after[i].instanceId, before[i].instanceId);
            QCOMPARE(after[i].instanceName, before[i].instanceName);
            QCOMPARE(after[i].kind, before[i].kind);
            QCOMPARE(after[i].state, before[i].state);
            QCOMPARE(after[i].error, before[i].error);
            QCOMPARE(after[i].bytes, before[i].bytes);
            QCOMPARE(after[i].finishedMs(), before[i].finishedMs());
        }
        QCOMPARE(after[0].kind, Kind::World);
        QCOMPARE(after[1].error, S("disk full"));
        QCOMPARE(after[2].bytes, 2 * kMB);
        QCOMPARE(rig.center->activeCount(), 0);  // running jobs are never persisted
    }

    void aCorruptHistoryFileIsIgnored()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(S("downloads.json"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("{ this is not json ]");
        }
        qint64 now = 1'700'000'000'000;
        DownloadCenter center(path, [&now]() { return now; });
        QVERIFY(center.history().isEmpty());
        QVERIFY(QFile::exists(path + S(".corrupt")));  // kept for the user
        // and it keeps working
        auto task = QSharedPointer<FakeTask>(new FakeTask, &QObject::deleteLater);
        DownloadJob job;
        job.title = S("after");
        job.task = Task::Ptr(task);
        center.start(job);
        task->succeed();
        QCOMPARE(center.history().size(), 1);

        // valid json of the wrong shape, and entries without an id / title are skipped
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write("{\"version\":1,\"entries\":[{\"id\":\"\",\"title\":\"x\"},{\"id\":\"a\"},{\"id\":\"b\",\"title\":\"ok\",\"kind\":\"zzz\",\"state\":\"done\"},7]}");
        }
        DownloadCenter again(path, [&now]() { return now; });
        QCOMPARE(again.history().size(), 1);
        QCOMPARE(again.history().first().title, S("ok"));
        QCOMPARE(again.history().first().kind, Kind::Mixed);  // unknown kind
    }

    void aMissingFileMeansEmptyHistory()
    {
        QTemporaryDir dir;
        DownloadCenter center(dir.filePath(S("nope/downloads.json")));
        QVERIFY(center.history().isEmpty());
        DownloadCenter noPath{ QString() };  // never writes anywhere
        QVERIFY(noPath.history().isEmpty());
    }

    void clearHistoryEmptiesItAndTheFile()
    {
        Rig rig;
        auto s = rig.startFake(S("A"));
        s.task->succeed();
        QSignalSpy history(rig.center.get(), &DownloadCenter::historyChanged);
        rig.center->clearHistory();
        QVERIFY(rig.center->history().isEmpty());
        QCOMPARE(history.size(), 1);
        rig.center->clearHistory();  // nothing to clear: no signal
        QCOMPARE(history.size(), 1);
        rig.reopen();
        QVERIFY(rig.center->history().isEmpty());
    }

    void jobChangedIsThrottled()
    {
        Rig rig;
        auto s = rig.startFake(S("Pack"));
        QSignalSpy changed(rig.center.get(), &DownloadCenter::jobChanged);
        for (int i = 1; i <= 50; ++i)
            s.task->step(QUuid::createUuid(), i * 1024, 100 * 1024);
        QCOMPARE(changed.size(), 1);  // the first change goes out at once, the other 49 share the 80 ms gap
        rig.now += 200;
        s.task->step(QUuid::createUuid(), 1024, 2048);
        QCOMPARE(changed.size(), 2);
        // a change held back is flushed by the timer
        s.task->step(QUuid::createUuid(), 1024, 2048);
        QCOMPARE(changed.size(), 2);
        rig.now += 200;
        QTRY_VERIFY_WITH_TIMEOUT(changed.size() >= 3, 1000);
    }

    void textHelpers()
    {
        QCOMPARE(sizeText(30 * kMB), S("30 MB"));
        QCOMPARE(sizeText(1 * kMB + kMB / 2), S("1.5 MB"));
        QCOMPARE(sizeText(0), S("0 B"));
        QCOMPARE(sizeText(812 * 1024), S("812 KB"));
        QCOMPARE(speedText(2.1 * kMB), S("2.1 MB/s"));
        QCOMPARE(progressText(qint64(12.4 * kMB), 30 * kMB, 2.1 * kMB), S("12.4 MB of 30 MB · 2.1 MB/s"));
        QCOMPARE(progressText(5 * kMB, 0, 0), S("5 MB"));
        QCOMPARE(progressText(5 * kMB, 10 * kMB, 0.4), S("5 MB of 10 MB"));

        ActiveDownload a;
        a.title = S("Fabric Fun");
        a.instanceName = S("Fabric Fun");
        a.bytesDone = qint64(12.4 * kMB);
        a.bytesTotal = 30 * kMB;
        a.fraction = 0.41;
        a.speed = 2.1 * kMB;
        QCOMPARE(activeCaption(a), S("Fabric Fun · 12.4 MB of 30 MB · 2.1 MB/s"));
        a.bytesTotal = 0;
        a.bytesDone = 0;
        QCOMPARE(activeCaption(a), S("Fabric Fun · 41%"));
        a.fraction = -1;
        QCOMPARE(activeCaption(a), S("Fabric Fun · Preparing..."));
        a.instanceName.clear();
        QCOMPARE(activeCaption(a), S("Preparing..."));

        HistoryEntry e;
        e.title = S("Sodium");
        e.instanceName = S("Fabric Fun");
        e.bytes = 30 * kMB;
        e.state = DownloadResult::State::Done;
        const qint64 now = 1'700'000'000'000;
        e.finishedAt = QDateTime::fromMSecsSinceEpoch(now - 2 * 60 * 1000, Qt::UTC);
        QCOMPARE(historyCaption(e, now), S("Fabric Fun · 30 MB · 2 minutes ago"));
        e.state = DownloadResult::State::Failed;  // a failed job has no meaningful size
        QCOMPARE(historyCaption(e, now), S("Fabric Fun · 2 minutes ago"));
        e.instanceName.clear();
        e.bytes = 0;
        QCOMPARE(historyCaption(e, now), S("2 minutes ago"));

        QCOMPARE(stateText(DownloadResult::State::Done), S("Done"));
        QCOMPARE(stateText(DownloadResult::State::Failed), S("Failed"));
        QCOMPARE(stateText(DownloadResult::State::Cancelled), S("Cancelled"));
        for (auto st : { DownloadResult::State::Done, DownloadResult::State::Failed, DownloadResult::State::Cancelled })
            QCOMPARE(stateFromString(stateToString(st)), st);
        for (auto k : { Kind::Mod, Kind::ResourcePack, Kind::ShaderPack, Kind::World, Kind::Modpack, Kind::Mixed, Kind::Update })
            QCOMPARE(kindFromString(kindToString(k)), k);
    }

    void summaryAndOverallFraction()
    {
        QCOMPARE(summaryText({}), QString());
        QCOMPARE(overallFraction({}), -1.0);

        ActiveDownload a;
        a.fraction = -1;
        QCOMPARE(summaryText({ a }), S("Downloading 1 item"));

        a.bytesTotal = 30 * kMB;
        a.fraction = 0.5;
        ActiveDownload b;
        b.bytesTotal = 10 * kMB;
        b.fraction = 1.0;
        // weighted by size: (15 + 10) / 40 = 62.5 %
        QVERIFY(qAbs(overallFraction({ a, b }) - 0.625) < 1e-9);
        QCOMPARE(summaryText({ a, b }), S("Downloading 2 items · 63%"));
        QCOMPARE(summaryText({ a }), S("Downloading 1 item · 50%"));

        ActiveDownload unknown;  // no fraction: left out of the mean
        QVERIFY(qAbs(overallFraction({ a, unknown }) - 0.5) < 1e-9);
        ActiveDownload counted;  // a fraction without bytes: plain mean
        counted.fraction = 0.25;
        QVERIFY(qAbs(overallFraction({ a, counted }) - 0.375) < 1e-9);
    }

    void flowTexts()
    {
        QCOMPARE(startedToastText(S("Sodium")), QString::fromUtf8("Downloading Sodium…"));
        QCOMPARE(failedToastText(S("Sodium")), S("Couldn't download Sodium"));
        QCOMPARE(warningsToastText(), S("Downloaded with warnings"));
        QCOMPARE(downloadsActionText(), S("Downloads"));
        QCOMPARE(instanceBusyText(), S("Files for this instance are still downloading. Try again when they finish."));
        QCOMPARE(modTitleText(S("Sodium"), 0), S("Sodium"));
        QCOMPARE(modTitleText(S("Sodium"), 1), S("Sodium and 1 dependency"));
        QCOMPARE(modTitleText(S("Sodium"), 3), S("Sodium and 3 dependencies"));
        QCOMPARE(updateTitleText({ S("Sodium") }), S("Sodium"));
        QCOMPARE(updateTitleText({ S("Sodium"), S("Lithium"), S("Iris") }), S("Update 3 mods"));
        QCOMPARE(contentTitleText(3, 0, 0), S("3 mods"));
        QCOMPARE(contentTitleText(1, 0, 1), S("1 mod and 1 shader"));
        QCOMPARE(contentTitleText(2, 1, 1), S("2 mods, 1 resource pack and 1 shader"));
    }
};

QTEST_GUILESS_MAIN(DownloadCenterTest)
#include "DownloadCenter_test.moc"
