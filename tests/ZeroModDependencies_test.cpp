// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of InstanceDependencyResolver (zero/content/ModDependencies.h), the production dependency check of the mod
// install review, the install cart and the Boost dialog. A real ModFolderModel over a temporary folder (no instance) and a fake task
// stand in for the instance and GetModDependenciesTask: every run must END (answer, failure or timeout), whatever the model or the
// task do, and a run after a failure (Retry) must work. No network, no APPLICATION.
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <functional>
#include <memory>

#include "minecraft/mod/ModFolderModel.h"
#include "tasks/Task.h"
#include "zero/content/ModDependencies.h"

using namespace Zero::Content;

namespace {

QString S(const char* text)
{
    return QString::fromUtf8(text);
}

/// Answers by hand, or at once inside start() when `answerAtOnce`.
class FakeTask : public Task {
    Q_OBJECT
   public:
    FakeTask() { setAbortable(true); }
    bool answerAtOnce = false;
    int aborts = 0;
    void succeed() { emitSucceeded(); }
    void failWith(const QString& reason) { emitFailed(reason); }
    /// Ends without succeeded / failed / aborted.
    void endSilently() { emit finished(); }
    /// The signals of an answer after the task was aborted (emitSucceeded() would assert on a task that does not run).
    void lateAnswer()
    {
        emit succeeded();
        emit finished();
    }
    bool abort() override
    {
        ++aborts;
        return Task::abort();
    }

   protected:
    void executeTask() override
    {
        if (answerAtOnce) {
            emitSucceeded();
        }
    }
};

/// The resolver over a test model and fake tasks.
class TestResolver : public InstanceDependencyResolver {
   public:
    explicit TestResolver(ResourceFolderModel* model) : m_model(model) {}

    /// How a load request is answered: the real update() (default), nothing at all, or updateFinished() at once.
    enum class Load { Real, Never, AtOnce };
    Load load = Load::Real;
    bool taskAnswersAtOnce = false;
    bool noModel = false;
    QList<bool> updateAccepted;  ///< what each real update() returned (false = the model was already loading)
    QList<QSharedPointer<FakeTask>> tasks;
    QList<PlannedMod> answer;

   protected:
    ResourceFolderModel* modelFor(const Request&, QString& error) override
    {
        if (noModel || !m_model) {
            error = S("no model");
            return nullptr;
        }
        return m_model;
    }
    void requestLoad(ResourceFolderModel* model) override
    {
        switch (load) {
            case Load::Real:
                updateAccepted.append(model->update());
                break;
            case Load::Never:
                break;
            case Load::AtOnce:
                emit model->updateFinished();
                break;
        }
    }
    Task::Ptr makeTask(const Request&, ResourceFolderModel*) override
    {
        auto task = makeShared<FakeTask>();
        task->answerAtOnce = taskAnswersAtOnce;
        tasks.append(task);
        return task;
    }
    QList<PlannedMod> dependenciesOf(Task*) override { return answer; }

   private:
    QPointer<ResourceFolderModel> m_model;
};

DependencyResolver::Request request()
{
    DependencyResolver::Request r;
    r.instanceId = S("inst");
    DependencyResolver::Selected selected;
    selected.pack = std::make_shared<ModPlatform::IndexedPack>();
    selected.pack->addonId = S("AANobbMI");
    selected.pack->name = S("Sodium");
    selected.pack->provider = ModPlatform::ResourceProvider::MODRINTH;
    selected.version.fileId = S("v1");
    r.also.append(selected);
    return r;
}

PlannedMod dep(const QString& id)
{
    PlannedMod mod;
    mod.pack = std::make_shared<ModPlatform::IndexedPack>();
    mod.pack->addonId = id;
    mod.pack->name = id;
    return mod;
}

}  // namespace

class ZeroModDependenciesTest : public QObject {
    Q_OBJECT

   private slots:
    void initTestCase() { qRegisterMetaType<QList<Zero::Content::PlannedMod>>("QList<Zero::Content::PlannedMod>"); }

    void modelLoadsThenTheTaskAnswers()
    {
        QTemporaryDir dir;
        ModFolderModel model(dir.path(), nullptr, true, true);
        TestResolver resolver(&model);
        resolver.answer = { dep(S("fabric-api")) };
        QSignalSpy resolved(&resolver, &DependencyResolver::resolved);
        QSignalSpy failed(&resolver, &DependencyResolver::failed);
        resolver.resolve(7, request());
        QVERIFY(resolver.isRunning());
        QCOMPARE(resolver.updateAccepted, (QList<bool>{ true }));
        QTRY_COMPARE(resolver.tasks.size(), 1);  // built only after the load
        QVERIFY(resolved.isEmpty());
        resolver.tasks.first()->succeed();
        QCOMPARE(resolved.size(), 1);
        QCOMPARE(resolved.first().at(0).toULongLong(), 7ULL);
        QCOMPARE(resolved.first().at(1).value<QList<PlannedMod>>().size(), 1);
        QVERIFY(failed.isEmpty());
        QVERIFY(!resolver.isRunning());
    }

    // update() while the model already loads returns false and coalesces: Prism runs one more load and only then emits
    // updateFinished(). The run must still go on (and not take the earlier load for its own).
    void modelAlreadyLoadingStillAnswers()
    {
        QTemporaryDir dir;
        ModFolderModel model(dir.path(), nullptr, true, true);
        QSignalSpy loads(&model, &ResourceFolderModel::updateFinished);
        QVERIFY(model.update());  // someone else (the Mods page) loads the folder right now
        TestResolver resolver(&model);
        QSignalSpy resolved(&resolver, &DependencyResolver::resolved);
        resolver.resolve(1, request());
        QCOMPARE(resolver.updateAccepted, (QList<bool>{ false }));  // coalesced with the running load
        QTRY_COMPARE(resolver.tasks.size(), 1);
        QCOMPARE(loads.size(), 1);  // ONE updateFinished() for both requests, after the scheduled second load
        resolver.tasks.first()->succeed();
        QCOMPARE(resolved.size(), 1);
    }

    void stressManyRunsOverABusyModel()
    {
        QTemporaryDir dir;
        ModFolderModel model(dir.path(), nullptr, true, true);
        TestResolver resolver(&model);
        resolver.taskAnswersAtOnce = true;
        QSignalSpy failed(&resolver, &DependencyResolver::failed);
        quint64 token = 0;
        QList<quint64> answered;
        int stale = 0;
        connect(&resolver, &DependencyResolver::resolved, this, [&](quint64 t, const QList<PlannedMod>&) {
            if (t != token) {
                ++stale;  // a replaced run answered
            }
            answered.append(t);
        });
        for (int i = 0; i < 40; ++i) {
            if (i % 3 == 0) {
                model.update();  // another reader of the folder
            }
            resolver.resolve(++token, request());  // replaces the previous run, like a quick version change
            if (i % 4 == 0) {
                QTest::qWait(1);  // sometimes a run gets to answer before it is replaced
            }
        }
        QTRY_VERIFY(!answered.isEmpty() && answered.last() == token);  // the last run always answers
        QTest::qWait(50);
        QCOMPARE(stale, 0);
        QCOMPARE(answered.last(), token);
        QCOMPARE(QSet<quint64>(answered.begin(), answered.end()).size(), answered.size());  // each run answers once at most
        QVERIFY(failed.isEmpty());
        QVERIFY(!resolver.isRunning());
    }

    void synchronousLoadAndAnswerAreNotMissed()
    {
        QTemporaryDir dir;
        ModFolderModel model(dir.path(), nullptr, true, true);
        TestResolver resolver(&model);
        resolver.load = TestResolver::Load::AtOnce;
        resolver.taskAnswersAtOnce = true;
        QSignalSpy resolved(&resolver, &DependencyResolver::resolved);
        resolver.resolve(3, request());
        QCOMPARE(resolved.size(), 1);  // inside resolve()
        QCOMPARE(resolved.first().at(0).toULongLong(), 3ULL);
        QVERIFY(!resolver.isRunning());
    }

    // The caller may resolve() again from its failed() slot (Retry): the new run is not torn down by the end of the old one.
    void resolveAgainFromTheAnswerSlot()
    {
        QTemporaryDir dir;
        ModFolderModel model(dir.path(), nullptr, true, true);
        TestResolver resolver(&model);
        resolver.load = TestResolver::Load::AtOnce;
        QSignalSpy resolved(&resolver, &DependencyResolver::resolved);
        bool retried = false;
        connect(&resolver, &DependencyResolver::failed, this, [&](quint64, const QString&) {
            if (!retried) {
                retried = true;
                resolver.resolve(2, request());
            }
        });
        resolver.resolve(1, request());
        QCOMPARE(resolver.tasks.size(), 1);
        resolver.tasks.first()->failWith(S("offline"));
        QVERIFY(retried);
        QCOMPARE(resolver.tasks.size(), 2);
        QVERIFY(resolver.isRunning());
        resolver.tasks.last()->succeed();
        QCOMPARE(resolved.size(), 1);
        QCOMPARE(resolved.first().at(0).toULongLong(), 2ULL);
    }

    void modelThatNeverLoadsFailsAndRetryWorks()
    {
        QTemporaryDir dir;
        ModFolderModel model(dir.path(), nullptr, true, true);
        TestResolver resolver(&model);
        resolver.setTimeouts(60, 5000);
        resolver.load = TestResolver::Load::Never;
        QSignalSpy resolved(&resolver, &DependencyResolver::resolved);
        QSignalSpy failed(&resolver, &DependencyResolver::failed);
        resolver.resolve(1, request());
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(failed.first().at(1).toString(), S("Reading the installed mods took too long."));
        QVERIFY(resolver.tasks.isEmpty());
        QVERIFY(!resolver.isRunning());

        resolver.load = TestResolver::Load::Real;  // Retry
        resolver.resolve(2, request());
        QTRY_COMPARE(resolver.tasks.size(), 1);
        resolver.tasks.first()->succeed();
        QCOMPARE(resolved.size(), 1);
        QCOMPARE(failed.size(), 1);
    }

    // The safety net of the whole run: a network task that never answers ends as a failure (the task is aborted), its late answer
    // is dropped, and Retry runs again.
    void silentTaskHitsTheRunTimeoutAndRetryWorks()
    {
        QTemporaryDir dir;
        ModFolderModel model(dir.path(), nullptr, true, true);
        TestResolver resolver(&model);
        resolver.setTimeouts(5000, 150);
        resolver.load = TestResolver::Load::AtOnce;
        QSignalSpy resolved(&resolver, &DependencyResolver::resolved);
        QSignalSpy failed(&resolver, &DependencyResolver::failed);
        resolver.resolve(1, request());
        QCOMPARE(resolver.tasks.size(), 1);
        QVERIFY(resolver.isRunning());
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(failed.first().at(0).toULongLong(), 1ULL);
        QCOMPARE(failed.first().at(1).toString(), S("The dependency check took too long."));
        QCOMPARE(resolver.tasks.first()->aborts, 1);
        QVERIFY(!resolver.isRunning());
        resolver.tasks.first()->lateAnswer();  // the network answers after all: dropped
        QCOMPARE(resolved.size(), 0);

        resolver.resolve(2, request());  // Retry
        QCOMPARE(resolver.tasks.size(), 2);
        resolver.tasks.last()->succeed();
        QCOMPARE(resolved.size(), 1);
        QCOMPARE(resolved.first().at(0).toULongLong(), 2ULL);
        QTest::qWait(250);  // the run timer of the answered run is off
        QCOMPARE(failed.size(), 1);
    }

    void everyWayATaskEndsAnswers()
    {
        QTemporaryDir dir;
        ModFolderModel model(dir.path(), nullptr, true, true);
        TestResolver resolver(&model);
        resolver.load = TestResolver::Load::AtOnce;
        QSignalSpy failed(&resolver, &DependencyResolver::failed);

        resolver.resolve(1, request());
        resolver.tasks.last()->failWith(QString());
        QCOMPARE(failed.last().at(1).toString(), S("The dependency check failed."));

        resolver.resolve(2, request());
        resolver.tasks.last()->failWith(S("HTTP 503"));
        QCOMPARE(failed.last().at(1).toString(), S("HTTP 503"));

        resolver.resolve(3, request());
        resolver.tasks.last()->abort();  // aborted by someone else
        QCOMPARE(failed.last().at(1).toString(), S("The dependency check was cancelled."));

        resolver.resolve(4, request());
        resolver.tasks.last()->endSilently();
        QCOMPARE(failed.last().at(1).toString(), S("The dependency check ended without an answer."));
        QCOMPARE(failed.size(), 4);
        QVERIFY(!resolver.isRunning());
    }

    void replacedAndCancelledRunsNeverAnswer()
    {
        QTemporaryDir dir;
        ModFolderModel model(dir.path(), nullptr, true, true);
        TestResolver resolver(&model);
        resolver.load = TestResolver::Load::AtOnce;
        QSignalSpy resolved(&resolver, &DependencyResolver::resolved);
        QSignalSpy failed(&resolver, &DependencyResolver::failed);
        resolver.resolve(1, request());
        resolver.resolve(2, request());
        QCOMPARE(resolver.tasks.first()->aborts, 1);
        resolver.tasks.first()->lateAnswer();  // already aborted and disconnected: nothing
        resolver.tasks.last()->succeed();
        QCOMPARE(resolved.size(), 1);
        QCOMPARE(resolved.first().at(0).toULongLong(), 2ULL);

        resolver.resolve(3, request());
        resolver.cancel();
        QVERIFY(!resolver.isRunning());
        QTest::qWait(20);
        QCOMPARE(resolved.size(), 1);
        QVERIFY(failed.isEmpty());  // cancel() answers nothing, not even "cancelled"
    }

    void modelGoneOrMissingFailsAtOnce()
    {
        QTemporaryDir dir;
        auto* model = new ModFolderModel(dir.path(), nullptr, true, true);
        TestResolver resolver(model);
        resolver.load = TestResolver::Load::Never;
        QSignalSpy failed(&resolver, &DependencyResolver::failed);
        resolver.resolve(1, request());
        delete model;  // the instance went away while its folder loaded
        QCOMPARE(failed.size(), 1);
        QCOMPARE(failed.first().at(1).toString(), S("The instance is no longer available."));
        QVERIFY(!resolver.isRunning());

        resolver.noModel = true;
        resolver.resolve(2, request());
        QCOMPARE(failed.size(), 2);
        QCOMPARE(failed.last().at(1).toString(), S("no model"));

        resolver.resolve(3, DependencyResolver::Request{});  // nothing to check
        QCOMPARE(failed.size(), 3);
        QCOMPARE(failed.last().at(1).toString(), S("This mod cannot be checked."));
        QVERIFY(!resolver.isRunning());
    }
};

QTEST_GUILESS_MAIN(ZeroModDependenciesTest)

#include "ZeroModDependencies_test.moc"
