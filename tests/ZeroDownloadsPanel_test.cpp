// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the Downloads panel and the top bar Downloads button (docs/zero/SHELL.md, "Downloads center"): rows for running
// and finished jobs, live progress, the cancel button, Clear history, the state pills, clicking a finished row, placement, the visibility
// rules of the button, its ring / badge / tooltip and the finish pulse. Offscreen, with a stand-alone DownloadCenter and hand-driven tasks.
//
// With the environment variable ZERO_DOWNLOADS_SHOTS set to a folder, savesScreenshots() writes downloads-panel.png, downloads-topbar.png
// and downloads-button-zoom.png there.
#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <memory>

#include "tasks/Task.h"
#include "ui/zero/PillTag.h"
#include "ui/zero/ZeroTheme.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/pages/ElidedLabel.h"
#include "ui/zero/shell/DownloadsButton.h"
#include "ui/zero/shell/DownloadsPanel.h"
#include "ui/zero/shell/ZeroTopBar.h"
#include "zero/downloads/DownloadCenter.h"

using namespace Zero;
using namespace Zero::Downloads;

namespace {

QString S(const char* text)
{
    return QString::fromUtf8(text);
}

constexpr qint64 kMB = 1024 * 1024;

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
    void succeed() { emitSucceeded(); }
    void fail(const QString& reason) { emitFailed(reason); }

   protected:
    void executeTask() override {}
};

struct Rig {
    QTemporaryDir dir;
    qint64 now = 1'700'000'000'000;
    DownloadCenter center{ dir.filePath(S("downloads.json")), [this]() { return now; } };
    QList<QSharedPointer<FakeTask>> keep;

    struct Started {
        QString id;
        QSharedPointer<FakeTask> task;
    };

    Started start(const QString& title, const QString& instanceId = S("inst-1"), Kind kind = Kind::Mod, bool abortable = true)
    {
        auto task = QSharedPointer<FakeTask>(new FakeTask(abortable), &QObject::deleteLater);
        keep.append(task);
        DownloadJob job;
        job.title = title;
        job.instanceId = instanceId;
        job.instanceName = instanceId.isEmpty() ? QString() : S("Fabric Fun");
        job.kind = kind;
        job.task = Task::Ptr(task);
        return { center.start(job), task };
    }
    /// A finished job in the history.
    void finished(const QString& title, DownloadResult::State state, Kind kind = Kind::Mod, const QString& error = {}, qint64 bytes = 0)
    {
        now += 60'000;
        auto s = start(title, S("inst-1"), kind);
        if (bytes > 0)
            s.task->step(QUuid::createUuid(), bytes, bytes);
        switch (state) {
            case DownloadResult::State::Done:
                s.task->succeed();
                break;
            case DownloadResult::State::Failed:
                s.task->fail(error);
                break;
            case DownloadResult::State::Cancelled:
                center.cancel(s.id);
                break;
        }
    }
};

template <typename T>
T* childNamed(QWidget* w, const char* name)
{
    return w ? w->findChild<T*>(QString::fromLatin1(name)) : nullptr;
}

QString fullText(QWidget* row, const char* name)
{
    auto* l = childNamed<ElidedLabel>(row, name);
    return l ? l->fullText() : QString();
}

void setMotion(bool on)
{
    ThemeSpec spec = ThemeSpec::obsidian();
    spec.reduceMotion = !on;
    Theme::setCurrent(spec);
}

}  // namespace

class ZeroDownloadsPanelTest : public QObject {
    Q_OBJECT

   private slots:
    void initTestCase()
    {
        ThemeSpec spec = ThemeSpec::obsidian();
        spec.reduceMotion = true;
        applySpecLive(spec);
    }
    void init() { setMotion(false); }
    void cleanup() { setMotion(false); }

    // ---- the top bar button ----

    void buttonIsHiddenWhileThereIsNothingToShow()
    {
        Rig rig;
        ZeroTopBar bar;
        bar.attachDownloads(&rig.center);
        bar.resize(800, ZeroTopBar::kHeight);
        bar.show();
        auto* button = bar.downloadsButton();
        QVERIFY(button);
        QVERIFY(button->isHidden());
        QVERIFY(!button->available());
        QVERIFY(!bar.dividerVisible());

        auto s = rig.start(S("Sodium"));
        QVERIFY(!button->isHidden());
        QVERIFY(button->available());
        QVERIFY(bar.dividerVisible());  // the rule separates the button from the account chip
        QCOMPARE(button->size(), QSize(32, 32));

        s.task->succeed();  // nothing runs, but there is a history entry
        QVERIFY(!button->isHidden());
        QCOMPARE(button->activeCount(), 0);

        rig.center.clearHistory();
        QVERIFY(button->isHidden());
        QVERIFY(!bar.dividerVisible());
    }

    void buttonRestoresFromAPersistedHistory()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(S("downloads.json"));
        qint64 now = 1'700'000'000'000;
        {
            DownloadCenter first(path, [&now]() { return now; });
            auto t = QSharedPointer<FakeTask>(new FakeTask, &QObject::deleteLater);
            DownloadJob job;
            job.title = S("Sodium");
            job.task = Task::Ptr(t);
            first.start(job);
            t->succeed();
        }
        DownloadCenter second(path, [&now]() { return now; });
        ZeroTopBar bar;
        bar.attachDownloads(&second);
        bar.show();
        QVERIFY(!bar.downloadsButton()->isHidden());
    }

    void buttonShowsCountFractionAndTooltip()
    {
        Rig rig;
        DownloadsButton button(&rig.center);
        QCOMPARE(button.accessibleName(), S("Downloads"));
        QCOMPARE(button.toolTip(), S("Downloads"));

        auto a = rig.start(S("A"));
        QCOMPARE(button.activeCount(), 1);
        QCOMPARE(button.fraction(), -1.0);  // indeterminate: rotating arc
        QCOMPARE(button.toolTip(), S("Downloading 1 item"));

        auto b = rig.start(S("B"));
        a.task->step(QUuid::createUuid(), 9 * kMB, 20 * kMB);  // 45 %
        rig.now += 500;
        b.task->step(QUuid::createUuid(), 9 * kMB, 20 * kMB);
        QCOMPARE(button.activeCount(), 2);
        QVERIFY(qAbs(button.fraction() - 0.45) < 1e-9);
        QCOMPARE(button.toolTip(), S("Downloading 2 items · 45%"));
        QCOMPARE(button.accessibleName(), S("Downloads"));

        a.task->succeed();
        b.task->succeed();
        QCOMPARE(button.activeCount(), 0);
        QCOMPARE(button.toolTip(), S("Downloads"));
    }

    void buttonPulsesOnceWhenAJobEndsWhilePanelIsClosed()
    {
        Rig rig;
        DownloadsButton button(&rig.center);
        button.show();

        // reduced motion: no pulse
        setMotion(false);
        auto a = rig.start(S("A"));
        a.task->succeed();
        QCOMPARE(button.pulseCount(), 0);
        QVERIFY(!button.pulsing());

        // motion on: one pulse
        setMotion(true);
        auto b = rig.start(S("B"));
        b.task->succeed();
        QCOMPARE(button.pulseCount(), 1);
        QVERIFY(button.pulsing());

        // the panel is open: no pulse
        button.setPanelOpen(true);
        auto c = rig.start(S("C"));
        c.task->succeed();
        QCOMPARE(button.pulseCount(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!button.pulsing(), 2000);
    }

    // ---- the panel ----

    void panelListsRunningAndRecentJobs()
    {
        Rig rig;
        rig.finished(S("Old pack"), DownloadResult::State::Done, Kind::Modpack, {}, 30 * kMB);
        rig.finished(S("Broken mod"), DownloadResult::State::Failed, Kind::Mod, S("HTTP 404 for sodium.jar"));
        auto a = rig.start(S("Sodium"), S("inst-1"), Kind::Mod);
        a.task->step(QUuid::createUuid(), qint64(12.4 * kMB), 30 * kMB);
        auto b = rig.start(S("Faithful 32x"), S("inst-1"), Kind::ResourcePack);

        DownloadsPanel panel(&rig.center);
        panel.show();
        QCOMPARE(panel.activeRowCount(), 2);
        QCOMPARE(panel.historyRowCount(), 2);

        QWidget* row = panel.activeRow(a.id);
        QVERIFY(row);
        QCOMPARE(fullText(row, "zeroDownloadTitle"), S("Sodium"));
        QCOMPARE(fullText(row, "zeroDownloadCaption"), S("Fabric Fun · 12.4 MB of 30 MB"));  // no speed yet
        QVERIFY(childNamed<QToolButton>(row, "zeroDownloadCancel"));
        QCOMPARE(childNamed<QToolButton>(row, "zeroDownloadCancel")->toolTip(), S("Cancel download"));
        QVERIFY(qAbs(childNamed<QWidget>(row, "zeroDownloadBar")->property("zeroFraction").toDouble() - 12.4 / 30) < 0.01);

        QWidget* indeterminate = panel.activeRow(b.id);
        QCOMPARE(fullText(indeterminate, "zeroDownloadCaption"), S("Fabric Fun · Preparing..."));
        QCOMPARE(childNamed<QWidget>(indeterminate, "zeroDownloadBar")->property("zeroFraction").toDouble(), -1.0);

        // newest first: Broken mod, then Old pack
        QCOMPARE(fullText(panel.historyRow(0), "zeroDownloadTitle"), S("Broken mod"));
        QCOMPARE(fullText(panel.historyRow(1), "zeroDownloadTitle"), S("Old pack"));
        QCOMPARE(fullText(panel.historyRow(1), "zeroDownloadCaption"), S("Fabric Fun · 30 MB · 1 minute ago"));
        QCOMPARE(fullText(panel.historyRow(0), "zeroDownloadCaption"), S("Fabric Fun · Just now"));
    }

    void progressUpdatesTheRow()
    {
        Rig rig;
        auto a = rig.start(S("Sodium"));
        DownloadsPanel panel(&rig.center);
        panel.show();
        QWidget* row = panel.activeRow(a.id);
        QCOMPARE(fullText(row, "zeroDownloadCaption"), S("Fabric Fun · Preparing..."));
        auto* bar = childNamed<QWidget>(row, "zeroDownloadBar");
        QCOMPARE(bar->property("zeroFraction").toDouble(), -1.0);

        rig.now += 1000;
        a.task->step(QUuid::createUuid(), 5 * kMB, 20 * kMB);
        QCOMPARE(fullText(row, "zeroDownloadCaption"), S("Fabric Fun · 5 MB of 20 MB · 5 MB/s"));
        QVERIFY(qAbs(bar->property("zeroFraction").toDouble() - 0.25) < 1e-9);
        QCOMPARE(panel.activeRow(a.id), row);  // the same row: updated, not rebuilt
    }

    void cancelButtonCancelsTheJob()
    {
        Rig rig;
        auto a = rig.start(S("Sodium"));
        auto b = rig.start(S("Other"));
        DownloadsPanel panel(&rig.center);
        panel.show();
        auto* cancel = childNamed<QToolButton>(panel.activeRow(a.id), "zeroDownloadCancel");
        QVERIFY(cancel && !cancel->isHidden());
        cancel->click();
        QCOMPARE(rig.center.activeCount(), 1);
        QCOMPARE(rig.center.history().first().state, DownloadResult::State::Cancelled);
        QCOMPARE(rig.center.history().first().title, S("Sodium"));
        QCOMPARE(panel.activeRowCount(), 1);
        QVERIFY(panel.activeRow(b.id));
        QCOMPARE(panel.historyRowCount(), 1);
        QCOMPARE(fullText(panel.historyRow(0), "zeroDownloadTitle"), S("Sodium"));
    }

    void cancelButtonIsHiddenWhenTheTaskCannotAbort()
    {
        Rig rig;
        auto a = rig.start(S("Sodium"), S("inst-1"), Kind::Mod, /*abortable*/ false);
        DownloadsPanel panel(&rig.center);
        panel.show();
        auto* cancel = childNamed<QToolButton>(panel.activeRow(a.id), "zeroDownloadCancel");
        QVERIFY(cancel);
        QVERIFY(cancel->isHidden());
    }

    void clearHistoryButton()
    {
        Rig rig;
        auto a = rig.start(S("Running"));
        DownloadsPanel panel(&rig.center);
        panel.show();
        QVERIFY(panel.clearButton()->isHidden());  // no history yet
        QCOMPARE(panel.clearButton()->text(), S("Clear history"));

        rig.finished(S("Done 1"), DownloadResult::State::Done);
        rig.finished(S("Done 2"), DownloadResult::State::Done);
        QVERIFY(!panel.clearButton()->isHidden());
        QCOMPARE(panel.historyRowCount(), 2);
        panel.clearButton()->click();
        QCOMPARE(panel.historyRowCount(), 0);
        QVERIFY(rig.center.history().isEmpty());
        QVERIFY(panel.clearButton()->isHidden());
        QCOMPARE(panel.activeRowCount(), 1);  // the running job stays
        QVERIFY(panel.isVisible());
        a.task->succeed();
    }

    void panelClosesWhenNothingIsLeftToShow()
    {
        Rig rig;
        rig.finished(S("Done"), DownloadResult::State::Done);
        QWidget anchor;
        anchor.setGeometry(400, 100, 32, 32);
        anchor.show();
        DownloadsPanel panel(&rig.center);
        panel.popup(&anchor);
        QVERIFY(panel.isVisible());
        panel.clearButton()->click();  // the empty state: button hides, panel closes
        QVERIFY(!panel.isVisible());
    }

    void popupDoesNotOpenWithNothingToShow()
    {
        Rig rig;
        QWidget anchor;
        anchor.show();
        DownloadsPanel panel(&rig.center);
        panel.popup(&anchor);
        QVERIFY(!panel.isVisible());
    }

    void statePillsAndTheErrorLine()
    {
        Rig rig;
        rig.finished(S("Cancelled one"), DownloadResult::State::Cancelled);
        rig.finished(S("Failed one"), DownloadResult::State::Failed, Kind::Mod, S("HTTP 404 for a very long file name that cannot possibly fit into a 380 pixel wide popover.jar"));
        rig.finished(S("Done one"), DownloadResult::State::Done);
        DownloadsPanel panel(&rig.center);
        panel.show();
        QCOMPARE(panel.historyRowCount(), 3);
        auto pill = [&](int i) { return childNamed<PillTag>(panel.historyRow(i), "zeroDownloadPill")->text(); };
        QCOMPARE(pill(0), S("Done"));
        QCOMPARE(pill(1), S("Failed"));
        QCOMPARE(pill(2), S("Cancelled"));
        QVERIFY(!childNamed<QWidget>(panel.historyRow(0), "zeroDownloadError"));
        QVERIFY(childNamed<QWidget>(panel.historyRow(1), "zeroDownloadError"));
        QVERIFY(!childNamed<QWidget>(panel.historyRow(2), "zeroDownloadError"));
        auto* error = childNamed<ElidedLabel>(panel.historyRow(1), "zeroDownloadError");
        QVERIFY(error->fullText().startsWith(S("HTTP 404")));
        QTRY_VERIFY(error->text().endsWith(QChar(0x2026)));  // elided to the row width...
        QCOMPARE(error->toolTip(), error->fullText());   // ...with the full text in the tooltip
    }

    void aFinishedRowOpensTheInstance()
    {
        Rig rig;
        rig.finished(S("Cancelled"), DownloadResult::State::Cancelled, Kind::World);
        rig.finished(S("Failed"), DownloadResult::State::Failed, Kind::ShaderPack, S("nope"));
        rig.finished(S("Done"), DownloadResult::State::Done, Kind::Modpack);
        QWidget anchor;
        anchor.setGeometry(400, 100, 32, 32);
        anchor.show();
        DownloadsPanel panel(&rig.center);
        panel.setInstanceExists([](const QString& id) { return id == QLatin1String("inst-1"); });
        QString openedId;
        Kind openedKind = Kind::Mixed;
        int opened = 0;
        connect(&panel, &DownloadsPanel::openInstanceRequested, this, [&](const QString& id, Kind kind) {
            openedId = id;
            openedKind = kind;
            ++opened;
        });
        panel.popup(&anchor);
        QVERIFY(panel.isVisible());

        // a Cancelled row does nothing
        QTest::mouseClick(panel.historyRow(2), Qt::LeftButton);
        QCOMPARE(opened, 0);
        QVERIFY(panel.isVisible());

        // a Failed row opens its instance on the matching page and closes the panel
        QTest::mouseClick(panel.historyRow(1), Qt::LeftButton);
        QCOMPARE(opened, 1);
        QCOMPARE(openedId, S("inst-1"));
        QCOMPARE(openedKind, Kind::ShaderPack);
        QVERIFY(!panel.isVisible());

        panel.popup(&anchor);
        QTest::mouseClick(panel.historyRow(0), Qt::LeftButton);  // Done
        QCOMPARE(opened, 2);
        QCOMPARE(openedKind, Kind::Modpack);
    }

    void aRowOfAnInstanceThatIsGoneIsNotClickable()
    {
        Rig rig;
        rig.finished(S("Done"), DownloadResult::State::Done);
        DownloadsPanel panel(&rig.center);
        panel.setInstanceExists([](const QString&) { return false; });
        int opened = 0;
        connect(&panel, &DownloadsPanel::openInstanceRequested, this, [&](const QString&, Kind) { ++opened; });
        panel.show();
        QTest::mouseClick(panel.historyRow(0), Qt::LeftButton);
        QCOMPARE(opened, 0);
        QVERIFY(panel.isVisible());
        QCOMPARE(panel.historyRow(0)->cursor().shape(), Qt::ArrowCursor);

        panel.setInstanceExists([](const QString&) { return true; });  // the rows follow
        QCOMPARE(panel.historyRow(0)->cursor().shape(), Qt::PointingHandCursor);
    }

    void escapeClosesAndPlacementIsRightAlignedAndCapped()
    {
        Rig rig;
        for (int i = 0; i < 40; ++i)
            rig.finished(S("Done %1").arg(i), DownloadResult::State::Done);
        QWidget host;
        host.setGeometry(100, 100, 900, 200);
        QWidget anchor(&host);
        anchor.setGeometry(600, 20, 32, 32);
        host.show();
        DownloadsPanel panel(&rig.center);
        panel.popup(&anchor);
        QVERIFY(panel.isVisible());
        QCOMPARE(panel.body()->width(), DownloadsPanel::kWidth);
        QCOMPARE(panel.body()->height(), DownloadsPanel::kMaxHeight);  // 40 rows scroll inside 480 px
        const QPoint anchorBottomRight = anchor.mapToGlobal(QPoint(anchor.width(), anchor.height()));
        const QPoint bodyTopRight = panel.body()->mapToGlobal(QPoint(panel.body()->width(), 0));
        QCOMPARE(bodyTopRight.x(), anchorBottomRight.x());       // right-aligned to the button
        QVERIFY(bodyTopRight.y() >= anchorBottomRight.y());     // under it

        QTest::keyClick(&panel, Qt::Key_Escape);
        QVERIFY(!panel.isVisible());
    }

    void aSmallListIsShorterThanTheCap()
    {
        Rig rig;
        rig.finished(S("Done"), DownloadResult::State::Done);
        QWidget anchor;
        anchor.setGeometry(500, 100, 32, 32);
        anchor.show();
        DownloadsPanel panel(&rig.center);
        panel.popup(&anchor);
        QVERIFY(panel.body()->height() < DownloadsPanel::kMaxHeight);
        QVERIFY(panel.body()->height() > 80);
    }

    void panelKeepsOpenSignalsForTheButton()
    {
        Rig rig;
        rig.finished(S("Done"), DownloadResult::State::Done);
        QWidget anchor;
        anchor.setGeometry(500, 100, 32, 32);
        anchor.show();
        DownloadsPanel panel(&rig.center);
        int opened = 0, closed = 0;
        connect(&panel, &DownloadsPanel::opened, this, [&]() { ++opened; });
        connect(&panel, &DownloadsPanel::closed, this, [&]() { ++closed; });
        panel.toggle(&anchor);
        QCOMPARE(opened, 1);
        panel.toggle(&anchor);
        QCOMPARE(closed, 1);
        QVERIFY(!panel.isVisible());
    }

    // ---- screenshots ----

    void savesScreenshots()
    {
        const QString dir = qEnvironmentVariable("ZERO_DOWNLOADS_SHOTS");
        if (dir.isEmpty())
            return;
        QDir().mkpath(dir);
        ThemeSpec spec = ThemeSpec::obsidian();
        spec.reduceMotion = true;
        applySpecLive(spec);
        Theme::setReduceMotion(true);  // applySpecLive merges the app setting (motion on): static shimmer and ring for the PNGs

        Rig rig;
        rig.finished(S("Cherry Blossom Shaders"), DownloadResult::State::Cancelled, Kind::ShaderPack);
        rig.finished(S("Sodium 0.5.8 and 2 dependencies"), DownloadResult::State::Failed, Kind::Mod,
                     S("Could not download sodium-fabric-0.5.8.jar: HTTP 503 from cdn.modrinth.com"));
        rig.finished(S("Fabric Fun 1.2"), DownloadResult::State::Done, Kind::Modpack, {}, 30 * kMB);
        auto known = rig.start(S("Faithful 32x"), S("inst-1"), Kind::ResourcePack);
        auto unknown = rig.start(S("Terralith and 3 more mods"), S("inst-1"), Kind::Mixed);
        const QUuid file = QUuid::createUuid();
        rig.now += 1000;
        known.task->step(file, 4 * kMB, 30 * kMB);
        rig.now += 1000;
        known.task->step(file, qint64(12.4 * kMB), 30 * kMB);
        Q_UNUSED(unknown);

        QWidget anchor;
        anchor.setGeometry(800, 40, 32, 32);
        anchor.show();
        DownloadsPanel panel(&rig.center);
        panel.popup(&anchor);
        QTest::qWait(120);
        {
            const int pad = 24;
            QPixmap body = panel.body()->grab();
            QPixmap canvas(body.width() + 2 * pad, body.height() + 2 * pad);
            canvas.fill(Theme::current().bg);
            QPainter p(&canvas);
            p.drawPixmap(pad, pad, body);
            p.end();
            QVERIFY(canvas.save(QDir(dir).filePath(S("downloads-panel.png"))));
        }
        panel.close();

        // the top bar with the button: two jobs running, 45 %
        ZeroTopBar bar;
        bar.attachDownloads(&rig.center);
        bar.resize(760, ZeroTopBar::kHeight);
        bar.show();
        QTest::qWait(60);
        QVERIFY(bar.downloadsButton() && !bar.downloadsButton()->isHidden());
        QVERIFY(bar.grab().save(QDir(dir).filePath(S("downloads-topbar.png"))));
        {
            auto* button = bar.downloadsButton();
            const int scale = 6;
            QPixmap zoom(button->width() * scale, button->height() * scale);
            zoom.fill(Theme::current().bg);
            QPainter p(&zoom);
            p.scale(scale, scale);
            button->render(&p);
            p.end();
            QVERIFY(zoom.save(QDir(dir).filePath(S("downloads-button-zoom.png"))));
        }
    }
};

QTEST_MAIN(ZeroDownloadsPanelTest)
#include "ZeroDownloadsPanel_test.moc"
