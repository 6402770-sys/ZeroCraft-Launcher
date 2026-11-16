// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the "Before you play" dialog (docs/zero/ARCHITECTURE.md): the rows grouped into MUST FIX / WORTH A LOOK, a fix running
// through the Session (hook called, row shown as fixed, summary and Play label refreshed, an error shown), the checking state (Play disabled,
// progress, an empty answer closes the dialog with "play", items switch to the results, closing meanwhile ignores the answer, one scan per
// instance at a time), the second question of "Play anyway" with malware, the opt-out that reaches the instance only on Play, Open Mods,
// and the whole flow (runBeforeYouPlay: nothing to show = no dialog). Synthetic items and temp folders only; env.system is empty, so the real
// PC is never read. No APPLICATION: the LauncherPage group of the same feature is not tested here (it needs the application's settings).
//
// With the environment variable ZERO_PRELAUNCH_SHOTS set to a folder, savesScreenshots() writes PNG grabs of the dialog there.
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QPushButton>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QTimer>

#include <atomic>
#include <functional>
#include <memory>

#include "ui/zero/ZeroTheme.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/prelaunch/PreLaunchDialog.h"

using namespace Zero;
using namespace Zero::PreLaunch;
using namespace Zero::PreLaunchUi;

namespace {

QString S(const char* text)
{
    return QString::fromUtf8(text);
}

Fix fixOf(Fix::Kind kind, const QString& label, const QString& target = {}, bool danger = false)
{
    Fix fix;
    fix.kind = kind;
    fix.label = label;
    if (!target.isEmpty()) {
        fix.targets << target;
    }
    fix.danger = danger;
    return fix;
}

Item itemOf(const QString& id, Severity severity, Category category, const QString& title, const QString& detail, const QList<Fix>& fixes)
{
    Item item;
    item.id = id;
    item.severity = severity;
    item.category = category;
    item.title = title;
    item.detail = detail;
    item.fixes = fixes;
    return item;
}

/// The dialog's world: a temp instance, recorded hooks, and a Rerun that forgets what was recycled.
struct Rig {
    QTemporaryDir tmp;
    PreLaunchEnvironment env;
    QList<Item> items;
    QHash<QString, QString> idOfPath;
    QStringList recycled;
    QStringList shown;
    QStringList urls;
    QList<bool> skips;
    QSet<QString> gone;
    bool recycleOk = true;
    int restores = 0;
    int dismissed = 0;
    int asks = 0;
    int askAnswer = 0;
    ModsUi::ChoicePrompt lastPrompt;

    QString mal, dup, miss, packs;

    Rig()
    {
        env.instanceName = S("Fabric Fun");
        env.env.instanceName = env.instanceName;
        env.env.instanceRoot = tmp.path() + S("/inst");
        env.env.gameRoot = tmp.path() + S("/inst/minecraft");
        env.env.system = {};  // never the real PC
        QDir().mkpath(env.env.modsDir());

        env.hooks.recycle = [this](const QString& path) {
            recycled << path;
            if (recycleOk) {
                gone.insert(idOfPath.value(path));
            }
            return recycleOk;
        };
        env.hooks.showFile = [this](const QString& path) { shown << path; };
        env.hooks.openUrl = [this](const QString& url) { urls << url; };
        env.hooks.setSkipInstance = [this](bool skip) { skips << skip; };
        env.env.restorePacks = [this]() {
            ++restores;
            gone.insert(packs);
            return QString();
        };
        env.env.dismissPacks = [this]() { ++dismissed; };
        env.rerun = [this](Category category) {
            QList<Item> fresh;
            for (const Item& item : std::as_const(items)) {
                if (item.category == category && !gone.contains(item.id)) {
                    fresh << item;
                }
            }
            return fresh;
        };
        env.ask = [this](QWidget*, const ModsUi::ChoicePrompt& prompt) {
            ++asks;
            lastPrompt = prompt;
            return askAnswer;
        };
    }

    QString touch(const QString& name)
    {
        const QString path = env.env.modsDir() + QLatin1Char('/') + name;
        QFile file(path);
        file.open(QIODevice::WriteOnly);
        file.write("x");
        return path;
    }

    /// One Block (malware) and three Warn rows.
    void fill()
    {
        mal = S("malware:evil.jar");
        dup = S("mods:duplicate:a.jar:b.jar");
        miss = S("mods:missing:c.jar");
        packs = S("packs:rollback");
        const QString evil = touch(S("evil.jar"));
        const QString second = touch(S("b.jar"));
        idOfPath.insert(evil, mal);
        idOfPath.insert(second, dup);
        items = {
            itemOf(mal, Severity::Block, Category::Malware, S("evil.jar matches a known malware"),
                   S("Its code is the same as the fractureiser downloader."),
                   { fixOf(Fix::Kind::RecycleFile, S("Move to Recycle Bin"), evil, true), fixOf(Fix::Kind::ShowFile, S("Show file"), evil) }),
            itemOf(dup, Severity::Warn, Category::Mods, S("Two copies of the same mod"), S("a.jar and b.jar both add the mod \"dupe\"."),
                   { fixOf(Fix::Kind::RecycleFile, S("Remove b.jar"), second) }),
            itemOf(miss, Severity::Warn, Category::Mods, S("c.jar needs another mod"), S("It needs Fabric API, which is not installed."),
                   { fixOf(Fix::Kind::OpenMods, S("Open Mods")) }),
            itemOf(packs, Severity::Warn, Category::Packs, S("Minecraft turned off your resource packs"),
                   S("They may have stopped the game last time."),
                   { fixOf(Fix::Kind::RestorePacks, S("Turn them back on")), fixOf(Fix::Kind::KeepPacksOff, S("Play without them")) }),
        };
    }

    QList<Item> warnOnly() const
    {
        QList<Item> out;
        for (const Item& item : items) {
            if (item.severity == Severity::Warn) {
                out << item;
            }
        }
        return out;
    }

    std::unique_ptr<PreLaunchDialog> results(const QList<Item>& which)
    {
        auto dialog = std::make_unique<PreLaunchDialog>(env);
        dialog->show();
        dialog->showResults(which);
        return dialog;
    }
};

/// A scan the test controls: records how it was called, reports progress, then waits for `gate`.
struct Gate {
    QSemaphore gate;
    std::atomic<int> calls{ 0 };
    std::atomic<int> active{ 0 };
    std::atomic<int> maxActive{ 0 };
    CheckResult result;
    bool reportProgress = true;

    ScanFunction scan()
    {
        return [this](const Env&, const RunOptions& options) {
            ++calls;
            const int now = ++active;
            int seen = maxActive.load();
            while (now > seen && !maxActive.compare_exchange_weak(seen, now)) {
            }
            if (options.progress && reportProgress) {
                options.progress(0, 3);
                options.progress(2, 3);
            }
            gate.acquire();
            --active;
            return result;
        };
    }
};

PreLaunchDialog* findDialog()
{
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (auto* dialog = qobject_cast<PreLaunchDialog*>(widget); dialog && dialog->isVisible()) {
            return dialog;
        }
    }
    return nullptr;
}

/// Runs `act` on the dialog the flow opens, once it is in `state`.
void actOnDialog(QTimer& poll, PreLaunchDialog::State state, std::function<void(PreLaunchDialog*)> act)
{
    QObject::connect(&poll, &QTimer::timeout, &poll, [&poll, state, act]() {
        if (auto* dialog = findDialog(); dialog && dialog->state() == state) {
            poll.stop();
            act(dialog);
        }
    });
    poll.start(10);
}

}  // namespace

class ZeroPreLaunchDialogTest : public QObject {
    Q_OBJECT

   private slots:
    void init()
    {
        ThemeSpec spec = ThemeSpec::obsidian();
        spec.reduceMotion = true;
        Theme::setCurrent(spec);
    }
    void cleanup()
    {
        QThreadPool::globalInstance()->waitForDone();
        ThemeSpec spec = ThemeSpec::obsidian();
        spec.reduceMotion = false;
        Theme::setCurrent(spec);
    }

    // ---- rows ----

    void rowsAreGroupedIntoTwoSections()
    {
        Rig rig;
        rig.fill();
        auto dialog = rig.results(rig.items);
        QCOMPARE(dialog->state(), PreLaunchDialog::State::Results);
        QCOMPARE(dialog->titleText(), S("Before you play \xC2\xB7 Fabric Fun"));
        QCOMPARE(dialog->sectionTitles(), QStringList({ S("Must fix"), S("Worth a look") }));
        QCOMPARE(dialog->rowIds().size(), 4);
        QCOMPARE(dialog->rowIds().first(), rig.mal);  // Block first
        QCOMPARE(dialog->rowSection(rig.mal), S("Must fix"));
        QCOMPARE(dialog->rowSection(rig.dup), S("Worth a look"));
        QCOMPARE(dialog->rowSection(rig.miss), S("Worth a look"));
        QCOMPARE(dialog->rowSection(rig.packs), S("Worth a look"));
        QCOMPARE(dialog->rowTitle(rig.mal), S("evil.jar matches a known malware"));
        QCOMPARE(dialog->rowDetail(rig.dup), S("a.jar and b.jar both add the mod \"dupe\"."));
        QCOMPARE(dialog->summaryText(), S("We found 4 things that may stop the game from working."));
        // one button per fix, in order
        const auto buttons = dialog->rowButtons(rig.mal);
        QCOMPARE(buttons.size(), 2);
        QCOMPARE(buttons.at(0)->text(), S("Move to Recycle Bin"));
        QCOMPARE(buttons.at(0)->property("zeroVariant").toString(), S("danger"));
        QCOMPARE(buttons.at(1)->text(), S("Show file"));
        QCOMPARE(buttons.at(1)->property("zeroVariant").toString(), S("secondary"));
        QCOMPARE(dialog->rowIconName(rig.mal), S("triangle-alert"));
        QCOMPARE(dialog->rowIconName(rig.dup), S("puzzle"));
        QCOMPARE(dialog->rowIconName(rig.packs), S("images"));
    }

    void aSectionWithoutRowsIsNotShown()
    {
        Rig rig;
        rig.fill();
        auto warnOnly = rig.results(rig.warnOnly());
        QCOMPARE(warnOnly->sectionTitles(), QStringList({ S("Worth a look") }));
        QCOMPARE(warnOnly->rowIds().size(), 3);
        QCOMPARE(warnOnly->summaryText(), S("We found 3 things that may stop the game from working."));

        const QList<Item> blockOnly{ rig.items.first() };
        auto blocked = rig.results(blockOnly);
        QCOMPARE(blocked->sectionTitles(), QStringList({ S("Must fix") }));
        QCOMPARE(blocked->summaryText(), S("We found 1 thing that may stop the game from working."));
    }

    void titleCarriesTheInstanceName()
    {
        Rig rig;
        rig.env.instanceName = S("My Pack");
        rig.fill();
        auto dialog = rig.results(rig.items);
        QCOMPARE(dialog->titleText(), S("Before you play \xC2\xB7 My Pack"));
    }

    // ---- fixes ----

    void aFixCallsTheHookAndTheRowShowsAsFixed()
    {
        Rig rig;
        rig.fill();
        auto dialog = rig.results(rig.items);
        QCOMPARE(dialog->playButton()->text(), S("Play anyway"));
        QVERIFY(!dialog->playButton()->isDefault());  // a Block row is open: Play is not the primary button
        QVERIFY(dialog->cancelButton()->isDefault());

        dialog->rowButtons(rig.mal).at(0)->click();
        QCOMPARE(rig.recycled.size(), 1);
        QVERIFY(rig.recycled.first().endsWith(S("evil.jar")));
        QVERIFY(dialog->rowFixed(rig.mal));
        QCOMPARE(dialog->rowFixedText(rig.mal), S("Fixed"));
        QVERIFY(dialog->rowButtons(rig.mal).isEmpty());
        QCOMPARE(dialog->rowIds().size(), 4);  // the row stays, now calm
        QCOMPARE(dialog->summaryText(), S("We found 3 things that may stop the game from working."));
        // no Block row is open any more: Play is the primary button again, still "Play anyway" (Warn rows are open)
        QCOMPARE(dialog->playButton()->text(), S("Play anyway"));
        QVERIFY(dialog->playButton()->isDefault());
        QCOMPARE(dialog->playButton()->property("zeroVariant").toString(), S("primary"));
        QVERIFY(!dialog->cancelButton()->isDefault());

        dialog->rowButtons(rig.dup).at(0)->click();
        dialog->rowButtons(rig.packs).at(1)->click();  // Play without them
        QCOMPARE(rig.dismissed, 1);
        QCOMPARE(dialog->rowFixedText(rig.packs), S("Left off"));
        QCOMPARE(dialog->summaryText(), S("We found 1 thing that may stop the game from working."));
    }

    void everythingFixedSaysNothingLeftAndPlay()
    {
        Rig rig;
        rig.fill();
        rig.items = { rig.items.at(0), rig.items.at(1) };  // the rerun knows only these two
        auto dialog = rig.results(rig.items);
        dialog->rowButtons(rig.mal).at(0)->click();
        dialog->rowButtons(rig.dup).at(0)->click();
        QCOMPARE(dialog->summaryText(), S("Nothing left to fix."));
        QCOMPARE(dialog->playButton()->text(), S("Play"));
        QVERIFY(dialog->playButton()->isDefault());
    }

    void restoringThePacksCallsTheHook()
    {
        Rig rig;
        rig.fill();
        auto dialog = rig.results(rig.warnOnly());
        dialog->rowButtons(rig.packs).at(0)->click();
        QCOMPARE(rig.restores, 1);
        QVERIFY(dialog->rowFixed(rig.packs));
    }

    void showFileAndUrlFixesKeepTheRow()
    {
        Rig rig;
        rig.fill();
        auto dialog = rig.results(rig.items);
        dialog->rowButtons(rig.mal).at(1)->click();  // Show file
        QCOMPARE(rig.shown.size(), 1);
        QVERIFY(!dialog->rowFixed(rig.mal));
        QCOMPARE(dialog->rowButtons(rig.mal).size(), 2);
    }

    void aFailedFixShowsTheErrorAndKeepsTheButtons()
    {
        Rig rig;
        rig.fill();
        rig.recycleOk = false;
        auto dialog = rig.results(rig.items);
        dialog->rowButtons(rig.mal).at(0)->click();
        QVERIFY(!dialog->rowFixed(rig.mal));
        QCOMPARE(dialog->rowError(rig.mal), S("Couldn't move evil.jar to the Recycle Bin."));
        QCOMPARE(dialog->rowButtons(rig.mal).size(), 2);
        QCOMPARE(dialog->summaryText(), S("We found 4 things that may stop the game from working."));
        // trying again after the problem went away clears the error
        rig.recycleOk = true;
        dialog->rowButtons(rig.mal).at(0)->click();
        QVERIFY(dialog->rowFixed(rig.mal));
        QCOMPARE(dialog->rowError(rig.mal), QString());
    }

    // ---- checking ----

    void checkingDisablesPlayAndShowsProgress()
    {
        Rig rig;
        Gate gate;
        rig.env.scan = gate.scan();
        PreLaunchDialog dialog(rig.env);
        dialog.show();
        dialog.startChecking(3);
        QCOMPARE(dialog.state(), PreLaunchDialog::State::Checking);
        QCOMPARE(dialog.checkingText(), S("Checking 3 mods for known malware\xE2\x80\xA6"));
        QVERIFY(!dialog.playButton()->isEnabled());
        QCOMPARE(dialog.playButton()->text(), S("Play"));
        QVERIFY(dialog.cancelButton()->isEnabled());
        QVERIFY(!dialog.optOutBox()->isVisible());
        QCOMPARE(dialog.summaryText(), QString());
        QTRY_COMPARE(dialog.progressText(), S("2 of 3"));
        QVERIFY(!dialog.playButton()->isEnabled());
        gate.gate.release();
        QTRY_COMPARE(dialog.result(), int(QDialog::Accepted));
    }

    void oneModIsSingular()
    {
        Rig rig;
        Gate gate;
        gate.reportProgress = false;
        rig.env.scan = gate.scan();
        PreLaunchDialog dialog(rig.env);
        dialog.show();
        dialog.startChecking(1);
        QCOMPARE(dialog.checkingText(), S("Checking 1 mod for known malware\xE2\x80\xA6"));
        gate.gate.release();
        QTRY_COMPARE(dialog.result(), int(QDialog::Accepted));
    }

    void startCheckingCountsTheJarsOfTheModsFolder()
    {
        Rig rig;
        rig.touch(S("a.jar"));
        rig.touch(S("b.jar"));
        rig.touch(S("c.jar.disabled"));
        Gate gate;
        gate.reportProgress = false;
        rig.env.scan = gate.scan();
        PreLaunchDialog dialog(rig.env);
        dialog.show();
        dialog.startChecking();
        QCOMPARE(dialog.checkingText(), S("Checking 2 mods for known malware\xE2\x80\xA6"));
        gate.gate.release();
        QTRY_COMPARE(dialog.result(), int(QDialog::Accepted));
    }

    void checkingThenNothingFoundClosesWithPlay()
    {
        Rig rig;
        Gate gate;
        rig.env.scan = gate.scan();
        PreLaunchDialog dialog(rig.env);
        dialog.show();
        dialog.startChecking(3);
        gate.gate.release();
        QTRY_COMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.decision(), Decision::Play);
        QVERIFY(rig.skips.isEmpty());
    }

    void checkingThenItemsShowsTheResults()
    {
        Rig rig;
        rig.fill();
        Gate gate;
        gate.result.items = rig.items;
        rig.env.scan = gate.scan();
        PreLaunchDialog dialog(rig.env);
        dialog.show();
        dialog.startChecking(3);
        gate.gate.release();
        QTRY_COMPARE(dialog.state(), PreLaunchDialog::State::Results);
        QVERIFY(dialog.isVisible());
        QCOMPARE(dialog.rowIds().size(), 4);
        QCOMPARE(dialog.sectionTitles(), QStringList({ S("Must fix"), S("Worth a look") }));
        QVERIFY(dialog.playButton()->isEnabled());
        QVERIFY(dialog.optOutBox()->isVisible());
        QCOMPARE(dialog.checkingText(), QString());
    }

    void closingDuringCheckingIgnoresTheAnswer()
    {
        Rig rig;
        rig.fill();
        Gate gate;
        gate.result.items = rig.items;
        rig.env.scan = gate.scan();
        PreLaunchDialog dialog(rig.env);
        dialog.show();
        dialog.startChecking(3);
        QTRY_COMPARE(gate.calls.load(), 1);
        dialog.reject();
        QCOMPARE(dialog.decision(), Decision::Cancel);
        gate.gate.release();
        QThreadPool::globalInstance()->waitForDone();
        QTest::qWait(50);  // the watcher's answer is delivered now: nothing happens
        QCOMPARE(dialog.state(), PreLaunchDialog::State::Checking);
        QVERIFY(dialog.rowIds().isEmpty());
        QCOMPARE(dialog.decision(), Decision::Cancel);
        QVERIFY(rig.skips.isEmpty());
    }

    void destroyingTheDialogDuringCheckingIsSafe()
    {
        Rig rig;
        rig.fill();
        Gate gate;
        gate.result.items = rig.items;
        rig.env.scan = gate.scan();
        auto* dialog = new PreLaunchDialog(rig.env);
        dialog->show();
        dialog->startChecking(3);
        QTRY_COMPARE(gate.calls.load(), 1);
        delete dialog;  // the worker is still blocked inside the scan
        gate.gate.release();
        QThreadPool::globalInstance()->waitForDone();  // it ends on its own copies
        QTest::qWait(50);
        QCOMPARE(gate.active.load(), 0);
    }

    void onlyOneScanPerInstanceRunsAtATime()
    {
        Rig rig;
        Gate gate;
        gate.reportProgress = false;
        rig.env.scan = gate.scan();
        auto* first = new PreLaunchDialog(rig.env);
        first->show();
        first->startChecking(1);
        QTRY_COMPARE(gate.calls.load(), 1);
        first->reject();  // closed, but its scan cannot be cancelled and still runs
        auto* second = new PreLaunchDialog(rig.env);
        second->show();
        second->startChecking(1);
        QTest::qWait(150);
        QCOMPARE(gate.calls.load(), 1);  // the second waits for the first
        gate.gate.release();
        QTRY_COMPARE(gate.calls.load(), 2);
        gate.gate.release();
        QTRY_COMPARE(second->result(), int(QDialog::Accepted));
        QCOMPARE(gate.maxActive.load(), 1);
        delete first;
        delete second;
    }

    // ---- Play ----

    void playWithAnOpenBlockAsksASecondQuestion()
    {
        Rig rig;
        rig.fill();
        auto dialog = rig.results(rig.items);
        rig.askAnswer = 1;  // Go back
        dialog->playButton()->click();
        QCOMPARE(rig.asks, 1);
        QCOMPARE(rig.lastPrompt.title, S("Play with malware on this instance?"));
        QCOMPARE(rig.lastPrompt.body, S("The game would run the flagged file. Only do this if you are sure it is a false alarm."));
        QCOMPARE(rig.lastPrompt.buttons.size(), 2);
        QCOMPARE(rig.lastPrompt.buttons.at(0).label, S("Play anyway"));
        QVERIFY(rig.lastPrompt.buttons.at(0).kind == ModsUi::ChoiceButton::Kind::Danger);
        QCOMPARE(rig.lastPrompt.buttons.at(1).label, S("Go back"));
        QCOMPARE(rig.lastPrompt.defaultButton, 1);
        QVERIFY(dialog->isVisible());  // stays as it was
        QCOMPARE(dialog->decision(), Decision::Cancel);
        QCOMPARE(dialog->rowIds().size(), 4);

        rig.askAnswer = -1;  // dismissed
        dialog->playButton()->click();
        QCOMPARE(rig.asks, 2);
        QVERIFY(dialog->isVisible());

        rig.askAnswer = 0;  // Play anyway
        dialog->playButton()->click();
        QCOMPARE(rig.asks, 3);
        QCOMPARE(dialog->decision(), Decision::Play);
        QCOMPARE(dialog->result(), int(QDialog::Accepted));
    }

    void playWithOnlyWarnRowsAsksNothing()
    {
        Rig rig;
        rig.fill();
        auto dialog = rig.results(rig.warnOnly());
        QVERIFY(dialog->playButton()->isDefault());
        QCOMPARE(dialog->playButton()->text(), S("Play anyway"));
        dialog->playButton()->click();
        QCOMPARE(rig.asks, 0);
        QCOMPARE(dialog->decision(), Decision::Play);
        QCOMPARE(dialog->result(), int(QDialog::Accepted));
        QCOMPARE(rig.dismissed, 1);  // the pack row was left open: "play without them", not asked again
    }

    void cancelStoresNothing()
    {
        Rig rig;
        rig.fill();
        auto dialog = rig.results(rig.warnOnly());
        dialog->optOutBox()->setChecked(true);
        dialog->cancelButton()->click();
        QCOMPARE(dialog->decision(), Decision::Cancel);
        QCOMPARE(dialog->result(), int(QDialog::Rejected));
        QVERIFY(rig.skips.isEmpty());
        QCOMPARE(rig.dismissed, 0);
    }

    void escapeIsCancel()
    {
        Rig rig;
        rig.fill();
        auto dialog = rig.results(rig.warnOnly());
        dialog->optOutBox()->setChecked(true);
        QTest::keyClick(dialog.get(), Qt::Key_Escape);
        QCOMPARE(dialog->result(), int(QDialog::Rejected));
        QVERIFY(rig.skips.isEmpty());
    }

    void theOptOutReachesTheInstanceOnlyOnPlay()
    {
        Rig rig;
        rig.fill();
        auto dialog = rig.results(rig.warnOnly());
        QCOMPARE(dialog->optOutBox()->text(), S("Don't check this instance again"));
        QCOMPARE(dialog->optOutBox()->toolTip(), S("Skips the mod and pack checks for this instance. The malware check always runs."));
        QVERIFY(!dialog->optOutBox()->isChecked());
        dialog->optOutBox()->setChecked(true);
        QVERIFY(rig.skips.isEmpty());  // ticking alone stores nothing
        dialog->playButton()->click();
        QCOMPARE(rig.skips, QList<bool>({ true }));
    }

    void playWithoutTheOptOutStoresNothing()
    {
        Rig rig;
        rig.fill();
        auto dialog = rig.results(rig.warnOnly());
        dialog->playButton()->click();
        QVERIFY(rig.skips.isEmpty());
    }

    void openModsLeavesTheCheckWithThatDecision()
    {
        Rig rig;
        rig.fill();
        auto dialog = rig.results(rig.warnOnly());
        dialog->optOutBox()->setChecked(true);
        dialog->rowButtons(rig.miss).at(0)->click();
        QCOMPARE(dialog->decision(), Decision::OpenMods);
        QCOMPARE(dialog->result(), int(QDialog::Rejected));
        QVERIFY(rig.skips.isEmpty());  // the launch is cancelled: nothing is stored
    }

    // ---- the whole flow ----

    void theSettingOffGoesStraightToPlay()
    {
        Rig rig;
        Gate gate;
        rig.env.env.enabled = false;
        rig.env.scan = gate.scan();
        QCOMPARE(runBeforeYouPlay(rig.env, nullptr), Decision::Play);
        QCOMPARE(gate.calls.load(), 0);
    }

    void nothingToShowOpensNoDialog()
    {
        Rig rig;
        auto calls = std::make_shared<QList<bool>>();
        rig.env.scan = [calls](const Env&, const RunOptions& options) {
            calls->append(options.cachedOnly);
            return CheckResult();  // complete, no items
        };
        QCOMPARE(runBeforeYouPlay(rig.env, nullptr), Decision::Play);
        QCOMPARE(*calls, QList<bool>({ true }));  // one cached look, nothing else
        QVERIFY(findDialog() == nullptr);
    }

    void anIncompleteLookRunsTheFullScanAndGoesOnWhenItIsClean()
    {
        Rig rig;
        auto calls = std::make_shared<QList<bool>>();
        rig.env.scan = [calls](const Env&, const RunOptions& options) {
            calls->append(options.cachedOnly);
            CheckResult result;
            result.complete = !options.cachedOnly;
            return result;
        };
        QCOMPARE(runBeforeYouPlay(rig.env, nullptr), Decision::Play);
        QCOMPARE(*calls, QList<bool>({ true, false }));
    }

    void anIncompleteLookThenFindingsShowsTheResults()
    {
        Rig rig;
        rig.fill();
        const QList<Item> found = rig.warnOnly();
        rig.env.scan = [found](const Env&, const RunOptions& options) {
            CheckResult result;
            result.complete = !options.cachedOnly;
            if (!options.cachedOnly) {
                result.items = found;  // an incomplete cached run never carries items
            }
            return result;
        };
        QTimer poll;
        actOnDialog(poll, PreLaunchDialog::State::Results, [](PreLaunchDialog* dialog) {
            QCOMPARE(dialog->rowIds().size(), 3);
            dialog->cancelButton()->click();
        });
        QCOMPARE(runBeforeYouPlay(rig.env, nullptr), Decision::Cancel);
    }

    void aCompleteLookWithFindingsShowsTheResultsAtOnce()
    {
        Rig rig;
        rig.fill();
        const QList<Item> found = rig.warnOnly();
        auto calls = std::make_shared<int>(0);
        rig.env.scan = [found, calls](const Env&, const RunOptions& options) {
            ++*calls;
            CheckResult result;
            if (options.cachedOnly) {
                result.items = found;
            }
            return result;
        };
        QTimer poll;
        actOnDialog(poll, PreLaunchDialog::State::Results, [](PreLaunchDialog* dialog) { dialog->playButton()->click(); });
        QCOMPARE(runBeforeYouPlay(rig.env, nullptr), Decision::Play);
        QCOMPARE(*calls, 1);  // no full scan: the cache already knew
    }

    void openModsThroughTheFlow()
    {
        Rig rig;
        rig.fill();
        const QList<Item> found = rig.warnOnly();
        rig.env.scan = [found](const Env&, const RunOptions&) {
            CheckResult result;
            result.items = found;
            return result;
        };
        QTimer poll;
        const QString miss = rig.miss;
        actOnDialog(poll, PreLaunchDialog::State::Results, [miss](PreLaunchDialog* dialog) { dialog->rowButtons(miss).at(0)->click(); });
        QCOMPARE(runBeforeYouPlay(rig.env, nullptr), Decision::OpenMods);
    }

    // ---- screenshots for review ----

    void savesScreenshots()
    {
        const QString dir = qEnvironmentVariable("ZERO_PRELAUNCH_SHOTS");
        if (dir.isEmpty()) {
            return;
        }
        QDir().mkpath(dir);
        ThemeSpec spec = ThemeSpec::obsidian();
        spec.reduceMotion = true;
        applySpecLive(spec);

        Rig rig;
        rig.fill();
        {
            Gate gate;
            rig.env.scan = gate.scan();
            PreLaunchDialog dialog(rig.env);
            dialog.show();
            dialog.startChecking(140);
            QTRY_COMPARE(dialog.progressText(), S("2 of 3"));
            QTest::qWait(50);
            QVERIFY(dialog.grab().save(QDir(dir).filePath(S("prelaunch-checking.png"))));
            gate.gate.release();
            QTRY_COMPARE(dialog.result(), int(QDialog::Accepted));
        }
        const QList<Item> all = rig.items;
        {
            rig.items = { all.at(0), all.at(1), all.at(3) };  // the rerun knows only these three
            auto dialog = rig.results(rig.items);
            dialog->rowButtons(rig.dup).at(0)->click();  // one row fixed
            QTest::qWait(50);
            QVERIFY(dialog->grab().save(QDir(dir).filePath(S("prelaunch-results.png"))));
        }
        {
            rig.items = all;
            auto dialog = rig.results(rig.items);
            QTest::qWait(50);
            QVERIFY(dialog->grab().save(QDir(dir).filePath(S("prelaunch-results-all.png"))));
        }
    }
};

QTEST_MAIN(ZeroPreLaunchDialogTest)

#include "ZeroPreLaunchDialog_test.moc"
