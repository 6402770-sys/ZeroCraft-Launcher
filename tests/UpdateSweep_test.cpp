// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the "Updates for everything" engine (docs/zero/ARCHITECTURE.md, "UpdateSweep"): checks one at a time, the instances
// that are skipped (running, busy, managed pack, nothing to check), a timeout or an error that is kept while the next instance is checked,
// the summary file (round trip, instances that vanished, an unreadable file), the automatic check (24 h, a game that runs, the setting),
// updateInstance (a FRESH check, then the applier), updateAll (instances in turn, the next check starts when the previous job started) and
// the entries that are dropped after a job that ended Done. Fake checks, a fake applier, a fake clock and a temp folder: no network, no
// application.
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <memory>

#include "zero/updates/UpdateSweep.h"

using namespace Zero::Updates;
using Zero::ModsUi::ModUpdate;
using Zero::ModsUi::ModUpdateCheck;

namespace {

struct Rig;

struct Behavior {
    QStringList mods;        ///< names of the updates the check finds (empty = none)
    QString version = QStringLiteral("2.0");
    int delayMs = 5;
    bool hang = false;       ///< never finishes
    bool fail = false;
    QString error = QStringLiteral("no answer");
    bool nothingToCheck = false;  ///< the factory returns nullptr
};

class FakeCheck : public ModUpdateCheck {
   public:
    FakeCheck(Rig* rig, const QString& id, const Behavior& behavior, QObject* parent);
    void start() override;
    void abort() override;
    QList<ModUpdate> updates() const override { return m_updates; }

   private:
    void finish();
    Rig* m_rig;
    QString m_id;
    Behavior m_behavior;
    bool m_running = false;
    QList<ModUpdate> m_updates;
};

struct Apply {
    QString id;
    QList<ModUpdate> updates;
    std::function<void()> started;
    std::function<void(bool, const QString&)> done;
};

struct Rig {
    QTemporaryDir tmp;
    QString path;
    QList<InstanceSource> sources;
    QHash<QString, Behavior> behavior;
    qint64 nowMs = 1000000000000LL;
    int active = 0;
    int maxActive = 0;
    QStringList checked;
    QStringList aborted;
    QHash<QString, int> checkCounts;
    bool autoOn = true;
    bool hasApplier = true;
    QList<Apply> applies;
    QStringList nowTimeline;  ///< "check:<id>" / "apply:<id>" in the order they happened

    Rig() { path = QDir(tmp.path()).filePath(QStringLiteral("zero/updates.json")); }

    void add(const QString& id, const QString& name, const QStringList& mods = {})
    {
        InstanceSource source;
        source.id = id;
        source.name = name;
        sources.append(source);
        Behavior b;
        b.mods = mods;
        behavior.insert(id, b);
    }
    InstanceSource& source(const QString& id)
    {
        for (InstanceSource& s : sources) {
            if (s.id == id) {
                return s;
            }
        }
        Q_ASSERT(false);
        return sources.first();
    }

    UpdateSweep::Hooks hooks()
    {
        UpdateSweep::Hooks h;
        h.instances = [this]() { return sources; };
        h.checkFactory = [this](const QString& id, QObject* parent) -> ModUpdateCheck* {
            const Behavior b = behavior.value(id);
            if (b.nothingToCheck) {
                return nullptr;
            }
            return new FakeCheck(this, id, b, parent);
        };
        if (hasApplier) {
            h.apply = [this](const QString& id, const QList<ModUpdate>& updates, std::function<void()> started,
                             std::function<void(bool, const QString&)> done) {
                nowTimeline << QStringLiteral("apply:") + id;
                applies.append({ id, updates, std::move(started), std::move(done) });
            };
        }
        h.autoEnabled = [this]() { return autoOn; };
        h.now = [this]() { return nowMs; };
        return h;
    }

    static UpdateSweep::Timing fast()
    {
        UpdateSweep::Timing t;
        t.startDelayMs = 20;
        t.retryMs = 25;
        t.recheckMs = 40;
        t.afterGameMs = 10;
        t.timeoutMs = 80;
        t.intervalMs = 24LL * 60 * 60 * 1000;
        return t;
    }
    std::unique_ptr<UpdateSweep> make(UpdateSweep::Timing timing = fast()) { return std::make_unique<UpdateSweep>(path, hooks(), timing); }
};

FakeCheck::FakeCheck(Rig* rig, const QString& id, const Behavior& behavior, QObject* parent)
    : ModUpdateCheck(parent), m_rig(rig), m_id(id), m_behavior(behavior)
{}

void FakeCheck::start()
{
    m_running = true;
    ++m_rig->active;
    m_rig->maxActive = qMax(m_rig->maxActive, m_rig->active);
    m_rig->checked << m_id;
    m_rig->nowTimeline << QStringLiteral("check:") + m_id;
    ++m_rig->checkCounts[m_id];
    if (m_behavior.hang) {
        return;
    }
    QTimer::singleShot(m_behavior.delayMs, this, [this]() { finish(); });
}

void FakeCheck::abort()
{
    if (m_running) {
        m_running = false;
        --m_rig->active;
        m_rig->aborted << m_id;
    }
}

void FakeCheck::finish()
{
    m_running = false;
    --m_rig->active;
    if (m_behavior.fail) {
        emit finished(false, m_behavior.error);
        return;
    }
    for (const QString& name : m_behavior.mods) {
        ModUpdate update;
        update.key = name.toLower() + QStringLiteral(".jar");
        update.name = name;
        update.newVersion = m_behavior.version;
        m_updates.append(update);
    }
    emit finished(true, QString());
}

QJsonObject readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

QStringList ids(const QList<InstanceUpdates>& results)
{
    QStringList out;
    for (const InstanceUpdates& r : results) {
        out << r.instanceId;
    }
    return out;
}

}  // namespace

class UpdateSweepTest : public QObject {
    Q_OBJECT

   private slots:
    // ---- the check of all instances ----

    void checksOneAtATime()
    {
        Rig rig;
        rig.add("a", "Alpha", { "Sodium" });
        rig.add("b", "Beta", { "Lithium", "Iris" });
        rig.add("c", "Gamma", { "Fabric API" });
        auto sweep = rig.make();
        QSignalSpy started(sweep.get(), &UpdateSweep::started);
        QSignalSpy finished(sweep.get(), &UpdateSweep::finished);
        QSignalSpy progress(sweep.get(), &UpdateSweep::progress);

        QVERIFY(!sweep->checking());
        sweep->checkNow();
        QVERIFY(sweep->checking());
        sweep->checkNow();  // ignored while it runs
        QCOMPARE(started.size(), 1);
        QTRY_COMPARE(finished.size(), 1);

        QCOMPARE(rig.maxActive, 1);
        QCOMPARE(rig.checked, QStringList({ "a", "b", "c" }));
        QCOMPARE(sweep->totalUpdates(), 4);
        QCOMPARE(ids(sweep->results()), QStringList({ "a", "b", "c" }));
        QCOMPARE(progress.size(), 3);
        QCOMPARE(progress.at(0).at(0).toInt(), 0);
        QCOMPARE(progress.at(0).at(1).toInt(), 3);
        QCOMPARE(progress.at(0).at(2).toString(), QStringLiteral("Alpha"));
        QCOMPARE(progress.at(2).at(0).toInt(), 2);
        QCOMPARE(progress.at(2).at(2).toString(), QStringLiteral("Gamma"));
        QCOMPARE(sweep->checkedAt(), rig.nowMs);
        QVERIFY(!sweep->checking());
        QCOMPARE(sweep->stateOf("a"), UpdateSweep::State::Idle);
    }

    void skipsRunningBusyManagedAndEmptyInstances()
    {
        Rig rig;
        rig.add("run", "Running", { "A" });
        rig.add("busy", "Busy", { "B" });
        rig.add("pack", "Managed", { "C" });
        rig.add("empty", "No metadata");
        rig.add("ok", "Fine", { "D" });
        rig.source("run").running = true;
        rig.source("busy").busy = true;
        rig.source("pack").managed = true;
        rig.behavior["empty"].nothingToCheck = true;
        auto sweep = rig.make();
        QSignalSpy progress(sweep.get(), &UpdateSweep::progress);
        QSignalSpy finished(sweep.get(), &UpdateSweep::finished);

        sweep->checkNow();
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(rig.checked, QStringList({ "ok" }));  // "empty" never reached a check: the factory had nothing
        QCOMPARE(ids(sweep->results()), QStringList({ "ok" }));
        QCOMPARE(progress.last().at(1).toInt(), 2);  // run / busy / pack are not part of this check
    }

    void aTimeoutKeepsTheErrorAndTheNextInstanceIsChecked()
    {
        Rig rig;
        rig.add("slow", "Slow", { "A" });
        rig.add("next", "Next", { "B" });
        rig.behavior["slow"].hang = true;
        auto sweep = rig.make();
        QSignalSpy finished(sweep.get(), &UpdateSweep::finished);

        sweep->checkNow();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
        QCOMPARE(rig.aborted, QStringList({ "slow" }));
        QCOMPARE(rig.checked, QStringList({ "slow", "next" }));
        QCOMPARE(ids(sweep->results()), QStringList({ "next" }));  // the failed one is not listed ...
        const QJsonArray saved = readFile(rig.path).value("instances").toArray();
        QStringList withError;
        for (const QJsonValue& v : saved) {
            if (!v.toObject().value("error").toString().isEmpty()) {
                withError << v.toObject().value("id").toString();
            }
        }
        QCOMPARE(withError, QStringList({ "slow" }));  // ... but its error is kept
        QCOMPARE(rig.maxActive, 1);
    }

    void anErrorOfOneInstanceIsNotFatal()
    {
        Rig rig;
        rig.add("bad", "Bad", { "A" });
        rig.add("good", "Good", { "B" });
        rig.behavior["bad"].fail = true;
        auto sweep = rig.make();
        QSignalSpy finished(sweep.get(), &UpdateSweep::finished);
        sweep->checkNow();
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(ids(sweep->results()), QStringList({ "good" }));
        QCOMPARE(sweep->totalUpdates(), 1);
    }

    void aSweepWithNothingToDoStillFinishes()
    {
        Rig rig;
        rig.add("run", "Running", { "A" });
        rig.source("run").running = true;
        auto sweep = rig.make();
        QSignalSpy started(sweep.get(), &UpdateSweep::started);
        QSignalSpy finished(sweep.get(), &UpdateSweep::finished);
        sweep->checkNow();
        QCOMPARE(started.size(), 1);
        QCOMPARE(finished.size(), 1);
        QVERIFY(!sweep->checking());
        QCOMPARE(sweep->checkedAt(), rig.nowMs);
        QVERIFY(sweep->results().isEmpty());
    }

    void aLaterCheckReplacesTheOldEntries()
    {
        Rig rig;
        rig.add("a", "Alpha", { "Sodium", "Lithium" });
        auto sweep = rig.make();
        QSignalSpy finished(sweep.get(), &UpdateSweep::finished);
        sweep->checkNow();
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(sweep->results().first().entries.size(), 2);

        rig.behavior["a"].mods = { "Sodium" };
        sweep->checkNow();
        QTRY_COMPARE(finished.size(), 2);
        QCOMPARE(sweep->results().first().entries.size(), 1);

        rig.behavior["a"].mods = {};
        sweep->checkNow();
        QTRY_COMPARE(finished.size(), 3);
        QVERIFY(sweep->results().isEmpty());
    }

    // ---- the summary file ----

    void theSummaryIsSavedAndLoadedAgain()
    {
        Rig rig;
        rig.add("a", "Alpha", { "Sodium" });
        rig.add("b", "Beta", { "Lithium", "Iris" });
        {
            auto sweep = rig.make();
            QSignalSpy finished(sweep.get(), &UpdateSweep::finished);
            sweep->checkNow();
            QTRY_COMPARE(finished.size(), 1);
        }
        QVERIFY(QFile::exists(rig.path));

        auto again = rig.make();  // a new start: nothing was checked yet in this session
        QCOMPARE(ids(again->results()), QStringList({ "a", "b" }));
        QCOMPARE(again->totalUpdates(), 3);
        QCOMPARE(again->checkedAt(), rig.nowMs);
        const InstanceUpdates beta = again->results().at(1);
        QCOMPARE(beta.instanceName, QStringLiteral("Beta"));
        QCOMPARE(beta.entries.size(), 2);
        QCOMPARE(beta.entries.first().name, QStringLiteral("Lithium"));
        QCOMPARE(beta.entries.first().newVersion, QStringLiteral("2.0"));
        QCOMPARE(beta.entries.first().kind, QStringLiteral("mod"));
        QCOMPARE(beta.checkedAt, rig.nowMs);
        QCOMPARE(readFile(rig.path).value("version").toInt(), 1);
    }

    void instancesThatVanishedAreDropped()
    {
        Rig rig;
        rig.add("a", "Alpha", { "Sodium" });
        rig.add("b", "Beta", { "Lithium" });
        {
            auto sweep = rig.make();
            QSignalSpy finished(sweep.get(), &UpdateSweep::finished);
            sweep->checkNow();
            QTRY_COMPARE(finished.size(), 1);
        }
        rig.sources.removeLast();  // "b" was deleted
        auto again = rig.make();
        QCOMPARE(ids(again->results()), QStringList({ "a" }));
        QStringList saved;  // dropped from the file too, at once
        for (const QJsonValue& v : readFile(rig.path).value("instances").toArray()) {
            saved << v.toObject().value("id").toString();
        }
        QCOMPARE(saved, QStringList({ "a" }));
    }

    void aRenamedInstanceShowsItsCurrentName()
    {
        Rig rig;
        rig.add("a", "Alpha", { "Sodium" });
        auto sweep = rig.make();
        QSignalSpy finished(sweep.get(), &UpdateSweep::finished);
        sweep->checkNow();
        QTRY_COMPARE(finished.size(), 1);
        rig.source("a").name = QStringLiteral("Alpha 2");
        QCOMPARE(sweep->results().first().instanceName, QStringLiteral("Alpha 2"));
    }

    void anUnreadableFileIsMovedAside()
    {
        Rig rig;
        rig.add("a", "Alpha", { "Sodium" });
        QDir().mkpath(QFileInfo(rig.path).absolutePath());
        {
            QFile file(rig.path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("{ not json");
        }
        auto sweep = rig.make();
        QVERIFY(sweep->results().isEmpty());
        QCOMPARE(sweep->checkedAt(), qint64(0));
        QVERIFY(QFile::exists(rig.path + QStringLiteral(".corrupt")));
        QVERIFY(!QFile::exists(rig.path));
    }

    // ---- the automatic check ----

    void autoChecksWhenTheLastCheckIsOlderThan24Hours()
    {
        Rig rig;
        rig.add("a", "Alpha", { "Sodium" });
        {
            auto sweep = rig.make();
            QSignalSpy finished(sweep.get(), &UpdateSweep::finished);
            sweep->checkNow();
            QTRY_COMPARE(finished.size(), 1);
        }
        rig.checked.clear();
        rig.nowMs += 25LL * 60 * 60 * 1000;
        auto sweep = rig.make();
        QSignalSpy started(sweep.get(), &UpdateSweep::started);
        sweep->start();
        QTRY_COMPARE(started.size(), 1);
        QTRY_COMPARE(rig.checked, QStringList({ "a" }));
    }

    void autoDoesNotCheckAgainWithin24Hours()
    {
        Rig rig;
        rig.add("a", "Alpha", { "Sodium" });
        {
            auto sweep = rig.make();
            QSignalSpy finished(sweep.get(), &UpdateSweep::finished);
            sweep->checkNow();
            QTRY_COMPARE(finished.size(), 1);
        }
        rig.checked.clear();
        rig.nowMs += 23LL * 60 * 60 * 1000;
        auto sweep = rig.make();
        QSignalSpy started(sweep.get(), &UpdateSweep::started);
        sweep->start();
        QTest::qWait(250);  // several ticks of the fast timing
        QCOMPARE(started.size(), 0);
        QVERIFY(rig.checked.isEmpty());
        // ... and it does once the 24 hours are over
        rig.nowMs += 2LL * 60 * 60 * 1000;
        QTRY_COMPARE(started.size(), 1);
    }

    void autoChecksAtOnceWhenNothingWasEverChecked()
    {
        Rig rig;
        rig.add("a", "Alpha", { "Sodium" });
        auto sweep = rig.make();
        QSignalSpy finished(sweep.get(), &UpdateSweep::finished);
        sweep->start();
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(ids(sweep->results()), QStringList({ "a" }));
    }

    void autoWaitsWhileAGameRuns()
    {
        Rig rig;
        rig.add("a", "Alpha", { "Sodium" });
        rig.add("game", "Playing", { "B" });
        rig.source("game").running = true;
        auto sweep = rig.make();
        QSignalSpy started(sweep.get(), &UpdateSweep::started);
        sweep->start();
        QTest::qWait(250);
        QCOMPARE(started.size(), 0);  // several ticks, the game still runs
        QVERIFY(rig.checked.isEmpty());
        rig.source("game").running = false;
        sweep->onGameStateChanged();  // the game ended: it looks again soon
        QTRY_COMPARE(started.size(), 1);
        QTRY_VERIFY(rig.checked.contains("a"));
    }

    void autoIsOffWithTheSetting()
    {
        Rig rig;
        rig.add("a", "Alpha", { "Sodium" });
        rig.autoOn = false;
        auto sweep = rig.make();
        QSignalSpy started(sweep.get(), &UpdateSweep::started);
        sweep->start();
        QTest::qWait(250);
        QCOMPARE(started.size(), 0);
        rig.autoOn = true;  // switched on later: picked up by the next look
        QTRY_COMPARE(started.size(), 1);
    }

    void aManualCheckIgnoresTheRule()
    {
        Rig rig;
        rig.add("a", "Alpha", { "Sodium" });
        auto sweep = rig.make();
        QSignalSpy finished(sweep.get(), &UpdateSweep::finished);
        sweep->checkNow();
        QTRY_COMPARE(finished.size(), 1);
        sweep->checkNow();  // just checked: a manual one runs anyway
        QTRY_COMPARE(finished.size(), 2);
        QCOMPARE(rig.checkCounts.value("a"), 2);
    }

    // ---- updating ----

    void updateInstanceRunsAFreshCheckThenTheApplier()
    {
        Rig rig;
        rig.add("a", "Alpha", { "Sodium" });
        auto sweep = rig.make();
        QSignalSpy finished(sweep.get(), &UpdateSweep::finished);
        QSignalSpy resultsChanged(sweep.get(), &UpdateSweep::resultsChanged);
        sweep->checkNow();
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(rig.checkCounts.value("a"), 1);

        // the world moved on: the fresh check finds another version (the old tasks are not reused)
        rig.behavior["a"].version = QStringLiteral("3.0");
        sweep->updateInstance("a");
        QCOMPARE(sweep->stateOf("a"), UpdateSweep::State::Checking);
        QTRY_COMPARE(rig.applies.size(), 1);
        QCOMPARE(rig.checkCounts.value("a"), 2);  // a fresh check
        QCOMPARE(rig.applies.first().id, QStringLiteral("a"));
        QCOMPARE(rig.applies.first().updates.size(), 1);
        QCOMPARE(rig.applies.first().updates.first().newVersion, QStringLiteral("3.0"));
        QCOMPARE(sweep->stateOf("a"), UpdateSweep::State::Updating);  // the job is being prepared / started

        rig.applies.first().started();
        QCOMPARE(sweep->stateOf("a"), UpdateSweep::State::Updating);
        QCOMPARE(sweep->results().first().entries.first().newVersion, QStringLiteral("3.0"));  // the fresh result replaced the old one

        resultsChanged.clear();
        rig.applies.first().done(true, QString());  // ended Done: the entries are used up
        QVERIFY(sweep->results().isEmpty());
        QCOMPARE(sweep->stateOf("a"), UpdateSweep::State::Idle);
        QVERIFY(resultsChanged.size() >= 1);
        QVERIFY(readFile(rig.path).value("instances").toArray().isEmpty());
    }

    void anEndedJobThatFailedKeepsTheEntries()
    {
        Rig rig;
        rig.add("a", "Alpha", { "Sodium" });
        auto sweep = rig.make();
        QSignalSpy problems(sweep.get(), &UpdateSweep::problem);
        sweep->updateInstance("a");
        QTRY_COMPARE(rig.applies.size(), 1);
        rig.applies.first().started();
        rig.applies.first().done(false, QStringLiteral("Could not save a copy"));
        QCOMPARE(sweep->stateOf("a"), UpdateSweep::State::Idle);
        QCOMPARE(ids(sweep->results()), QStringList({ "a" }));
        QCOMPARE(problems.size(), 1);
        QVERIFY(problems.first().at(1).toString().contains("Could not save a copy"));
    }

    void doneBeforeStartedStillStartsFirst()
    {
        Rig rig;
        rig.add("a", "Alpha", { "Sodium" });
        rig.add("b", "Beta", { "Lithium" });
        auto sweep = rig.make();
        QSignalSpy finished(sweep.get(), &UpdateSweep::finished);
        sweep->checkNow();
        QTRY_COMPARE(finished.size(), 1);
        rig.checked.clear();
        sweep->updateAll();
        QTRY_COMPARE(rig.applies.size(), 1);
        rig.applies.first().done(true, QString());  // an applier that ended at once (nothing could start, say)
        QTRY_COMPARE(rig.applies.size(), 2);        // the queue went on: `started` was implied
        QCOMPARE(sweep->stateOf("a"), UpdateSweep::State::Idle);
        rig.applies.at(1).started();
        rig.applies.at(1).done(true, QString());
        QVERIFY(sweep->results().isEmpty());
    }

    void updateAllRunsInstancesInTurnAndStartsTheNextCheckAfterTheJobStarted()
    {
        Rig rig;
        rig.add("a", "Alpha", { "Sodium" });
        rig.add("b", "Beta", { "Lithium" });
        rig.add("c", "Gamma", { "Iris" });
        auto sweep = rig.make();
        QSignalSpy finished(sweep.get(), &UpdateSweep::finished);
        sweep->checkNow();
        QTRY_COMPARE(finished.size(), 1);
        rig.checked.clear();
        rig.nowTimeline.clear();
        rig.maxActive = 0;

        sweep->updateAll();
        QCOMPARE(sweep->stateOf("a"), UpdateSweep::State::Checking);
        QCOMPARE(sweep->stateOf("b"), UpdateSweep::State::Queued);
        QCOMPARE(sweep->stateOf("c"), UpdateSweep::State::Queued);
        QTRY_COMPARE(rig.applies.size(), 1);
        QTest::qWait(60);
        QCOMPARE(rig.checked, QStringList({ "a" }));  // "b" waits: the job of "a" did not start yet
        QCOMPARE(sweep->stateOf("b"), UpdateSweep::State::Queued);

        rig.applies.at(0).started();
        QTRY_COMPARE(rig.applies.size(), 2);          // "b" was checked while the downloads of "a" go on
        QCOMPARE(sweep->stateOf("a"), UpdateSweep::State::Updating);
        QCOMPARE(sweep->stateOf("c"), UpdateSweep::State::Queued);
        rig.applies.at(1).started();
        QTRY_COMPARE(rig.applies.size(), 3);
        rig.applies.at(2).started();

        QCOMPARE(rig.checked, QStringList({ "a", "b", "c" }));
        QCOMPARE(rig.nowTimeline, QStringList({ "check:a", "apply:a", "check:b", "apply:b", "check:c", "apply:c" }));
        QCOMPARE(rig.maxActive, 1);  // never two checks at once

        // each instance's entries go when ITS job ended Done
        rig.applies.at(1).done(true, QString());
        QCOMPARE(ids(sweep->results()), QStringList({ "a", "c" }));
        rig.applies.at(0).done(true, QString());
        rig.applies.at(2).done(false, QString());  // a failed job keeps them
        QCOMPARE(ids(sweep->results()), QStringList({ "c" }));
    }

    void updatesRunBeforeTheChecksOfARunningSweep()
    {
        Rig rig;
        rig.add("a", "Alpha", { "Sodium" });
        rig.add("b", "Beta", { "Lithium" });
        rig.add("c", "Gamma", { "Iris" });
        auto sweep = rig.make();
        QSignalSpy finished(sweep.get(), &UpdateSweep::finished);
        sweep->checkNow();
        QTRY_COMPARE(finished.size(), 1);
        rig.nowTimeline.clear();

        sweep->checkNow();  // a is being checked, b and c are queued
        sweep->updateInstance("c");
        QCOMPARE(sweep->stateOf("b"), UpdateSweep::State::Queued);
        QTRY_COMPARE(rig.applies.size(), 1);
        rig.applies.first().started();
        QTRY_COMPARE(finished.size(), 2);
        // "a" was already being checked; the update of "c" came before the check of "b"
        QCOMPARE(rig.nowTimeline.mid(0, 4), QStringList({ "check:a", "check:c", "apply:c", "check:b" }));
        QCOMPARE(rig.applies.size(), 1);  // c is updating: its sweep check was skipped, no second update
    }

    void updateIsRefusedForARunningOrBusyInstance()
    {
        Rig rig;
        rig.add("a", "Alpha", { "Sodium" });
        rig.add("b", "Beta", { "Lithium" });
        rig.source("a").running = true;
        rig.source("b").busy = true;
        auto sweep = rig.make();
        QSignalSpy problems(sweep.get(), &UpdateSweep::problem);
        sweep->updateInstance("a");
        sweep->updateInstance("b");
        QCOMPARE(problems.size(), 2);
        QVERIFY(problems.at(0).at(1).toString().contains("Alpha"));
        QVERIFY(problems.at(1).at(1).toString().contains("Beta"));
        QVERIFY(rig.applies.isEmpty());
        QVERIFY(rig.checked.isEmpty());
    }

    void anInstanceThatStartedRunningDuringTheFreshCheckIsNotUpdated()
    {
        Rig rig;
        rig.add("a", "Alpha", { "Sodium" });
        auto sweep = rig.make();
        QSignalSpy problems(sweep.get(), &UpdateSweep::problem);
        sweep->updateInstance("a");
        rig.source("a").running = true;  // launched while the fresh check ran
        QTRY_COMPARE(problems.size(), 1);
        QVERIFY(rig.applies.isEmpty());
        QCOMPARE(sweep->stateOf("a"), UpdateSweep::State::Idle);
    }

    void aFailedFreshCheckReportsAProblemAndStartsNothing()
    {
        Rig rig;
        rig.add("a", "Alpha", { "Sodium" });
        rig.behavior["a"].fail = true;
        rig.behavior["a"].error = QStringLiteral("Modrinth did not answer");
        auto sweep = rig.make();
        QSignalSpy problems(sweep.get(), &UpdateSweep::problem);
        sweep->updateInstance("a");
        QTRY_COMPARE(problems.size(), 1);
        QVERIFY(problems.first().at(1).toString().contains("Modrinth did not answer"));
        QVERIFY(rig.applies.isEmpty());
        QCOMPARE(sweep->stateOf("a"), UpdateSweep::State::Idle);
    }

    void aFreshCheckWithNothingLeftDropsTheEntries()
    {
        Rig rig;
        rig.add("a", "Alpha", { "Sodium" });
        auto sweep = rig.make();
        QSignalSpy finished(sweep.get(), &UpdateSweep::finished);
        sweep->checkNow();
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(sweep->totalUpdates(), 1);
        rig.behavior["a"].mods = {};  // updated elsewhere in the meantime
        sweep->updateInstance("a");
        QTRY_VERIFY(sweep->results().isEmpty());
        QTRY_COMPARE(sweep->stateOf("a"), UpdateSweep::State::Idle);
        QVERIFY(rig.applies.isEmpty());
    }

    void updateInstanceTwiceQueuesOnce()
    {
        Rig rig;
        rig.add("a", "Alpha", { "Sodium" });
        auto sweep = rig.make();
        sweep->updateInstance("a");
        sweep->updateInstance("a");
        QTRY_COMPARE(rig.applies.size(), 1);
        sweep->updateInstance("a");  // while it is updating
        QTest::qWait(60);
        QCOMPARE(rig.applies.size(), 1);
        QCOMPARE(rig.checkCounts.value("a"), 1);
    }

    void withoutAnApplierNothingCanBeUpdated()
    {
        Rig rig;
        rig.hasApplier = false;
        rig.add("a", "Alpha", { "Sodium" });
        auto sweep = rig.make();
        QSignalSpy problems(sweep.get(), &UpdateSweep::problem);
        sweep->updateInstance("a");
        QTest::qWait(60);
        QCOMPARE(sweep->stateOf("a"), UpdateSweep::State::Idle);
        QVERIFY(rig.applies.isEmpty());
    }

    void managedInstancesAreNotUpdatedHere()
    {
        Rig rig;
        rig.add("pack", "Managed", { "Sodium" });
        rig.source("pack").managed = true;
        auto sweep = rig.make();
        sweep->updateInstance("pack");
        QTest::qWait(60);
        QVERIFY(rig.checked.isEmpty());
        QVERIFY(rig.applies.isEmpty());
    }

    // ---- captions ----

    void captionsCountTheKinds()
    {
        int counter = 0;
        auto entry = [&counter](const QString& kind) {
            UpdateEntry e;
            e.key = kind + QString::number(++counter);
            e.kind = kind;
            return e;
        };
        QCOMPARE(captionFor({ entry("mod") }), QStringLiteral("1 mod"));
        QCOMPARE(captionFor({ entry("mod"), entry("mod"), entry("mod"), entry("mod"), entry("mod") }), QStringLiteral("5 mods"));
        QCOMPARE(captionFor({ entry("mod"), entry("mod"), entry("shaderpack") }), QStringLiteral("2 mods · 1 shader pack"));
        QCOMPARE(captionFor({ entry("shaderpack"), entry("shaderpack"), entry("resourcepack") }),
                 QStringLiteral("2 shader packs · 1 resource pack"));
        QCOMPARE(captionFor({}), QString());
    }
};

QTEST_GUILESS_MAIN(UpdateSweepTest)

#include "UpdateSweep_test.moc"
