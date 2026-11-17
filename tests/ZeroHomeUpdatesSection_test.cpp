// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the "Updates" section of Home (docs/zero/SHELL.md, "Home > Updates"): hidden while there is nothing to show, the rows
// (instance name, "5 mods" caption, "Checked ... ago"), the states of a row's button (Update / Checking... / Queued / Downloading...), "Update all"
// and "Check again" reaching the engine, the line of a running check, and a click on a row. Offscreen, with a real UpdateSweep over fake checks
// and a temp folder (no application, no network).
//
// With the environment variable ZERO_UPDATES_SHOTS set to a folder, savesScreenshots() writes updates-idle.png and updates-checking.png there.
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QVBoxLayout>

#include <memory>

#include "ui/zero/ZeroSpinner.h"
#include "ui/zero/ZeroTheme.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/pages/HomeUpdatesSection.h"
#include "zero/updates/UpdateSweep.h"

using namespace Zero;
using namespace Zero::Updates;
using Zero::ModsUi::ModUpdate;
using Zero::ModsUi::ModUpdateCheck;

namespace {

QString S(const char* text)
{
    return QString::fromUtf8(text);
}

struct Behavior {
    QStringList mods;
    bool hang = false;
};

class FakeCheck : public ModUpdateCheck {
   public:
    FakeCheck(const Behavior& behavior, QObject* parent) : ModUpdateCheck(parent), m_behavior(behavior) {}
    void start() override
    {
        if (m_behavior.hang) {
            return;
        }
        QTimer::singleShot(0, this, [this]() {
            for (const QString& name : m_behavior.mods) {
                ModUpdate update;
                update.key = name.toLower() + QStringLiteral(".jar");
                update.name = name;
                update.newVersion = QStringLiteral("2.0");
                m_updates.append(update);
            }
            emit finished(true, QString());
        });
    }
    void abort() override {}
    QList<ModUpdate> updates() const override { return m_updates; }

   private:
    Behavior m_behavior;
    QList<ModUpdate> m_updates;
};

struct Applied {
    QString id;
    std::function<void()> started;
    std::function<void(bool, const QString&)> done;
};

/// Three instances with updates: Create Test (2 mods), Fabric Fun (5 mods), Vanilla 1.21 (1 mod), plus one without.
struct Rig {
    QTemporaryDir tmp;
    QList<InstanceSource> sources;
    QHash<QString, Behavior> behavior;
    QList<Applied> applies;
    int checks = 0;
    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    std::unique_ptr<UpdateSweep> sweep;

    Rig()
    {
        add("create", "Create Test", { "Create", "Flywheel" });
        add("fabric", "Fabric Fun", { "Sodium", "Lithium", "Iris", "Fabric API", "Ok Zoomer" });
        add("vanilla", "Vanilla 1.21", { "Sodium" });
        add("clean", "Already Current", {});

        UpdateSweep::Hooks hooks;
        hooks.instances = [this]() { return sources; };
        hooks.checkFactory = [this](const QString& id, QObject* parent) -> ModUpdateCheck* {
            ++checks;
            return new FakeCheck(behavior.value(id), parent);
        };
        hooks.apply = [this](const QString& id, const QList<ModUpdate>&, std::function<void()> started,
                             std::function<void(bool, const QString&)> done) { applies.append({ id, std::move(started), std::move(done) }); };
        hooks.autoEnabled = []() { return false; };
        hooks.now = [this]() { return nowMs - 10 * 60 * 1000; };  // checked ten minutes ago
        sweep = std::make_unique<UpdateSweep>(QDir(tmp.path()).filePath(S("updates.json")), std::move(hooks));
    }

    void add(const QString& id, const QString& name, const QStringList& mods)
    {
        InstanceSource source;
        source.id = id;
        source.name = name;
        sources.append(source);
        Behavior b;
        b.mods = mods;
        behavior.insert(id, b);
    }

    void fill()
    {
        QSignalSpy finished(sweep.get(), &UpdateSweep::finished);
        sweep->checkNow();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
    }
};

QIcon iconFor(const QString& id)
{
    // a flat tile per instance (the application's icon list is not there in a unit test)
    QPixmap pixmap(64, 64);
    const QList<QColor> colors = { QColor(Qt::darkCyan), QColor(Qt::darkGreen), QColor(Qt::darkMagenta), QColor(Qt::darkYellow) };
    pixmap.fill(colors.at(qAbs(int(qHash(id))) % colors.size()));
    return QIcon(pixmap);
}

std::unique_ptr<HomeUpdatesSection> sectionFor(Rig& rig)
{
    auto section = std::make_unique<HomeUpdatesSection>(rig.sweep.get(), &iconFor);
    section->resize(900, 320);
    return section;
}

}  // namespace

class ZeroHomeUpdatesSectionTest : public QObject {
    Q_OBJECT

   private slots:
    void initTestCase()
    {
        ThemeSpec spec = ThemeSpec::obsidian();
        spec.reduceMotion = true;
        applySpecLive(spec);
        Theme::setReduceMotion(true);
    }

    void hiddenWhileThereIsNothingToShow()
    {
        Rig rig;
        auto section = sectionFor(rig);
        QVERIFY(section->isHidden());
        QCOMPARE(section->rowCount(), 0);
        QVERIFY(section->checkingText().isEmpty());

        // a check that runs and finds nothing does not make the section pop up
        for (auto& b : rig.behavior) {
            b.mods = {};
        }
        QSignalSpy finished(rig.sweep.get(), &UpdateSweep::finished);
        rig.sweep->checkNow();
        section->refresh();
        QVERIFY(section->isHidden());
        QTRY_COMPARE(finished.size(), 1);
        section->refresh();
        QVERIFY(section->isHidden());
    }

    void showsOneRowPerInstanceWithItsCaption()
    {
        Rig rig;
        rig.fill();
        auto section = sectionFor(rig);
        section->show();
        QVERIFY(!section->isHidden());
        QCOMPARE(section->rowCount(), 3);  // the instance without updates is not listed
        QCOMPARE(section->rowIds(), QStringList({ "create", "fabric", "vanilla" }));
        QCOMPARE(section->rowName(0), S("Create Test"));
        QCOMPARE(section->rowCaption(0), S("2 mods"));
        QCOMPARE(section->rowName(1), S("Fabric Fun"));
        QCOMPARE(section->rowCaption(1), S("5 mods"));
        QCOMPARE(section->rowCaption(2), S("1 mod"));
        QCOMPARE(section->rowStatus(0), S("Update"));
        QCOMPARE(section->rowButton(0)->text(), S("Update"));
        QVERIFY(!section->rowButton(0)->isHidden());
        QVERIFY(!section->updateAllButton()->isHidden());
        QVERIFY(section->updateAllButton()->isEnabled());
        QCOMPARE(section->updateAllButton()->text(), S("Update all"));
        QCOMPARE(section->checkAgainButton()->text(), S("Check again"));
        QVERIFY(section->checkAgainButton()->isEnabled());
        QCOMPARE(section->checkedText(), S("Checked 10 minutes ago"));
        QVERIFY(section->checkingText().isEmpty());
    }

    void aRowButtonUpdatesThatInstance()
    {
        Rig rig;
        rig.fill();
        auto section = sectionFor(rig);
        section->show();
        rig.checks = 0;
        section->rowButton(1)->click();
        QCOMPARE(rig.sweep->stateOf("fabric"), UpdateSweep::State::Checking);  // a fresh check first
        QTRY_COMPARE(rig.applies.size(), 1);
        QCOMPARE(rig.applies.first().id, S("fabric"));
        QCOMPARE(rig.checks, 1);
    }

    void updateAllUpdatesEveryInstanceWithUpdates()
    {
        Rig rig;
        rig.fill();
        auto section = sectionFor(rig);
        section->show();
        section->updateAllButton()->click();
        QTRY_COMPARE(rig.applies.size(), 1);
        rig.applies.at(0).started();
        QTRY_COMPARE(rig.applies.size(), 2);
        rig.applies.at(1).started();
        QTRY_COMPARE(rig.applies.size(), 3);
        QStringList ids;
        for (const Applied& a : rig.applies) {
            ids << a.id;
        }
        QCOMPARE(ids, QStringList({ "create", "fabric", "vanilla" }));  // the instance without updates is not touched
    }

    void checkAgainStartsACheck()
    {
        Rig rig;
        rig.fill();
        auto section = sectionFor(rig);
        section->show();
        rig.checks = 0;
        section->checkAgainButton()->click();
        QVERIFY(rig.sweep->checking());
        QTRY_VERIFY(!rig.sweep->checking());
        QCOMPARE(rig.checks, 4);
    }

    void aRowButtonShowsCheckingQueuedAndDownloading()
    {
        Rig rig;
        rig.fill();
        auto section = sectionFor(rig);
        section->show();
        rig.behavior["create"].hang = true;  // its fresh check never ends: it stays "Checking..."
        rig.sweep->updateInstance("create");
        rig.sweep->updateInstance("fabric");
        section->refresh();
        QCOMPARE(section->rowStatus(0), S("Checking…"));
        QCOMPARE(section->rowStatus(1), S("Queued"));
        QCOMPARE(section->rowStatus(2), S("Update"));
        QVERIFY(section->rowButton(0)->isHidden());  // the button gave way to a spinner and the word
        QVERIFY(section->rowButton(1)->isHidden());
        QVERIFY(!section->rowButton(2)->isHidden());
        QVERIFY(section->updateAllButton()->isEnabled());  // one instance is still idle

        // another instance: its job runs
        Rig rig2;
        rig2.fill();
        auto second = sectionFor(rig2);
        second->show();
        rig2.sweep->updateInstance("vanilla");
        QTRY_COMPARE(rig2.applies.size(), 1);
        rig2.applies.first().started();
        second->refresh();
        QCOMPARE(second->rowStatus(2), S("Downloading…"));
        QCOMPARE(second->rowStatus(0), S("Update"));

        // everything busy: "Update all" has nothing to do
        rig2.sweep->updateAll();
        second->refresh();
        QCOMPARE(second->rowStatus(0), S("Checking…"));
        QTRY_COMPARE(rig2.applies.size(), 2);
        rig2.applies.at(1).started();
        QTRY_COMPARE(rig2.applies.size(), 3);
        rig2.applies.at(2).started();
        second->refresh();
        QCOMPARE(second->rowStatus(0), S("Downloading…"));
        QCOMPARE(second->rowStatus(1), S("Downloading…"));
        QCOMPARE(second->rowStatus(2), S("Downloading…"));
        QVERIFY(!second->updateAllButton()->isEnabled());
    }

    void theLineOfARunningCheckNamesTheInstance()
    {
        Rig rig;
        rig.fill();
        auto section = sectionFor(rig);
        section->show();
        rig.behavior["create"].hang = true;
        rig.sweep->checkNow();
        section->refresh();
        QCOMPARE(section->checkingText(), S("Checking Create Test… (1 of 4)"));
        QVERIFY(!section->checkAgainButton()->isEnabled());
        QCOMPARE(section->rowStatus(0), S("Checking…"));
        QCOMPARE(section->rowStatus(1), S("Queued"));
        QVERIFY(!section->updateAllButton()->isHidden());
    }

    void aRowClickOpensTheInstance()
    {
        Rig rig;
        rig.fill();
        auto section = sectionFor(rig);
        section->show();
        QSignalSpy opened(section.get(), &HomeUpdatesSection::openInstanceRequested);
        section->clickRow(1);
        QCOMPARE(opened.size(), 1);
        QCOMPARE(opened.first().first().toString(), S("fabric"));
    }

    void anUpdatedInstanceLeavesTheList()
    {
        Rig rig;
        rig.fill();
        auto section = sectionFor(rig);
        section->show();
        rig.sweep->updateInstance("fabric");
        QTRY_COMPARE(rig.applies.size(), 1);
        rig.applies.first().started();
        rig.applies.first().done(true, QString());
        section->refresh();
        QCOMPARE(section->rowIds(), QStringList({ "create", "vanilla" }));

        rig.sweep->updateAll();
        QTRY_COMPARE(rig.applies.size(), 2);
        rig.applies.at(1).started();
        QTRY_COMPARE(rig.applies.size(), 3);
        rig.applies.at(1).done(true, QString());
        rig.applies.at(2).started();
        rig.applies.at(2).done(true, QString());
        section->refresh();
        QVERIFY(section->isHidden());  // nothing left, nothing checking
    }

    void savesScreenshots()
    {
        const QString dir = qEnvironmentVariable("ZERO_UPDATES_SHOTS");
        if (dir.isEmpty()) {
            return;
        }
        QDir().mkpath(dir);
        ThemeSpec spec = ThemeSpec::obsidian();
        spec.reduceMotion = true;
        applySpecLive(spec);
        Theme::setReduceMotion(true);

        Rig rig;
        rig.fill();
        auto grab = [&](HomeUpdatesSection& section, const QString& file) {
            QWidget host;
            host.setAutoFillBackground(true);
            QPalette palette = host.palette();
            palette.setColor(QPalette::Window, Theme::current().bg);
            host.setPalette(palette);
            auto* lay = new QVBoxLayout(&host);
            lay->setContentsMargins(Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl);
            lay->addWidget(&section);
            host.resize(960, 360);
            host.show();
            QTest::qWait(120);
            QVERIFY(host.grab().save(QDir(dir).filePath(file)));
            lay->removeWidget(&section);
            section.setParent(nullptr);
        };
        {
            HomeUpdatesSection section(rig.sweep.get(), &iconFor);
            grab(section, S("updates-idle.png"));
        }
        {
            HomeUpdatesSection section(rig.sweep.get(), &iconFor);
            rig.behavior["fabric"].hang = true;
            rig.sweep->updateInstance("fabric");  // checking
            rig.sweep->updateInstance("vanilla");  // queued
            section.refresh();
            grab(section, S("updates-checking.png"));
        }
    }
};

QTEST_MAIN(ZeroHomeUpdatesSectionTest)

#include "ZeroHomeUpdatesSection_test.moc"
