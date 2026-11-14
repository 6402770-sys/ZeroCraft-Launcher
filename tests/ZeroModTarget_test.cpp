// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the Discover > Mods target instance (docs/zero/CONTENT-MODS.md): mod-ready, summary, the default
// target rule and the control. Offscreen, hand-built TargetInstance values: no instances, no APPLICATION.
#include <QApplication>
#include <QDir>
#include <QMenu>
#include <QPixmap>
#include <QSignalSpy>
#include <QTest>

#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/content/TargetInstanceButton.h"
#include "zero/content/ModTarget.h"

using namespace Zero;
using namespace Zero::Content;

namespace {

TargetInstance make(const QString& id, const QString& name, const QString& mc, ModPlatform::ModLoaderTypes loaders,
                    const QDateTime& last = {})
{
    TargetInstance t;
    t.id = id;
    t.name = name;
    t.iconKey = QStringLiteral("default");
    t.mcVersion = mc;
    t.loaders = loaders;
    t.loaderText = loaderDisplayNames(loaders).join(QStringLiteral(", "));
    t.lastLaunch = last;
    return t;
}

QDateTime day(int d)
{
    return QDateTime(QDate(2026, 1, d), QTime(12, 0), Qt::UTC);
}

QIcon testIcon(const QString&)
{
    QPixmap pm(20, 20);
    pm.fill(Qt::darkGreen);
    return QIcon(pm);
}

}  // namespace

class ZeroModTargetTest : public QObject {
    Q_OBJECT

   private slots:
    void modReady()
    {
        QVERIFY(!make("a", "Vanilla", "1.20.1", {}).modReady());
        QVERIFY(!make("a", "No version", "", ModPlatform::Fabric).modReady());
        QVERIFY(!make("a", "MC only", "1.20.1", ModPlatform::ModLoaderTypes()).modReady());
        QVERIFY(make("a", "Fabric", "1.20.1", ModPlatform::Fabric).modReady());
        QVERIFY(make("a", "Forge", "1.20.1", ModPlatform::Forge).modReady());
        QVERIFY(make("a", "NeoForge", "1.21.1", ModPlatform::NeoForge).modReady());
        QVERIFY(make("a", "Quilt", "1.20.1", ModPlatform::Quilt).modReady());
        // Modrinth accepts LiteLoader, CurseForge does not: either provider is enough.
        QVERIFY(make("a", "Lite", "1.12.2", ModPlatform::LiteLoader).modReady());
        // Cauldron is accepted by neither.
        QVERIFY(!make("a", "Cauldron", "1.7.10", ModPlatform::Cauldron).modReady());
    }

    void summary()
    {
        QCOMPARE(make("a", "A", "1.20.1", ModPlatform::Fabric).summary(), QStringLiteral("1.20.1 Fabric"));
        QCOMPARE(make("a", "A", "1.20.1", ModPlatform::Quilt | ModPlatform::Fabric).summary(), QStringLiteral("1.20.1 Fabric, Quilt"));
        QCOMPARE(make("a", "A", "1.21.1", ModPlatform::NeoForge | ModPlatform::Forge).summary(), QStringLiteral("1.21.1 Forge, NeoForge"));
        QCOMPARE(make("a", "A", "1.20.1", {}).summary(), QStringLiteral("1.20.1"));
    }

    void countWithoutLoader()
    {
        QCOMPARE(Content::countWithoutLoader({}), 0);
        const QList<TargetInstance> list = { make("a", "A", "1.20.1", ModPlatform::Fabric), make("b", "B", "1.20.1", {}),
                                             make("c", "C", "", ModPlatform::Fabric) };
        QCOMPARE(Content::countWithoutLoader(list), 2);
    }

    void chooseTarget_librarySelection()
    {
        const QList<TargetInstance> list = { make("a", "Alpha", "1.20.1", ModPlatform::Fabric, day(5)),
                                             make("b", "Beta", "1.20.1", ModPlatform::Forge, day(1)),
                                             make("v", "Vanilla", "1.20.1", {}, day(9)) };
        QCOMPARE(chooseTarget(list, "b", "a"), QStringLiteral("b"));      // selection wins over saved
        QCOMPARE(chooseTarget(list, "v", "b"), QStringLiteral("b"));      // selection not mod-ready: saved
        QCOMPARE(chooseTarget(list, "gone", "b"), QStringLiteral("b"));   // selection unknown: saved
    }

    void chooseTarget_saved()
    {
        const QList<TargetInstance> list = { make("a", "Alpha", "1.20.1", ModPlatform::Fabric, day(5)),
                                             make("b", "Beta", "1.20.1", ModPlatform::Forge, day(1)),
                                             make("v", "Vanilla", "1.20.1", {}, day(9)) };
        QCOMPARE(chooseTarget(list, "", "b"), QStringLiteral("b"));
        QCOMPARE(chooseTarget(list, "", "gone"), QStringLiteral("a"));  // saved vanished: most recent launch
        QCOMPARE(chooseTarget(list, "", "v"), QStringLiteral("a"));     // saved is vanilla now: most recent launch
    }

    void chooseTarget_recentThenName()
    {
        QList<TargetInstance> list = { make("a", "alpha", "1.20.1", ModPlatform::Fabric, day(2)),
                                       make("b", "Beta", "1.20.1", ModPlatform::Forge, day(7)),
                                       make("v", "Vanilla", "1.20.1", {}, day(30)) };
        QCOMPARE(chooseTarget(list, "", ""), QStringLiteral("b"));  // the vanilla one is newer but not mod-ready

        // Never launched: first by name, case-insensitive.
        list = { make("z", "Zed", "1.20.1", ModPlatform::Fabric), make("b", "beta", "1.20.1", ModPlatform::Forge),
                 make("a", "Alpha", "1.20.1", ModPlatform::Quilt) };
        QCOMPARE(chooseTarget(list, "", ""), QStringLiteral("a"));
        list.removeAt(2);
        QCOMPARE(chooseTarget(list, "", ""), QStringLiteral("b"));

        // One launched, others not: the launched one.
        list[0].lastLaunch = day(3);
        QCOMPARE(chooseTarget(list, "", ""), QStringLiteral("z"));
    }

    void chooseTarget_none()
    {
        QCOMPARE(chooseTarget({}, "a", "b"), QString());
        QCOMPARE(chooseTarget({ make("v", "Vanilla", "1.20.1", {}, day(1)) }, "v", "v"), QString());
    }

    void button()
    {
        TargetInstanceButton btn(nullptr, testIcon);
        QCOMPARE(btn.sizeHint().height(), 40);
        QVERIFY(btn.accessibleName().startsWith(QStringLiteral("Target instance")));

        const QList<TargetInstance> list = { make("a", "Alpha", "1.20.1", ModPlatform::Fabric, day(1)),
                                             make("b", "Beta", "1.21.1", ModPlatform::Forge | ModPlatform::NeoForge, day(2)),
                                             make("v", "Vanilla", "1.20.1", {}), make("w", "Other", "1.8.9", {}) };
        btn.setTargets(list);
        btn.setCurrentId("b");
        QCOMPARE(btn.currentId(), QStringLiteral("b"));
        QCOMPARE(btn.accessibleName(), QStringLiteral("Target instance: Beta, 1.21.1 Forge, NeoForge"));
        btn.setCurrentId("a");
        QCOMPARE(btn.accessibleName(), QStringLiteral("Target instance: Alpha, 1.20.1 Fabric"));
        btn.show();
        QVERIFY(!btn.grab().isNull());

        std::unique_ptr<QMenu> menu(btn.buildMenu(nullptr));
        const auto actions = menu->actions();
        QCOMPARE(actions.size(), 4);  // Alpha, Beta, separator, caption
        QCOMPARE(actions[0]->text(), QStringLiteral("Alpha — 1.20.1 Fabric"));
        QVERIFY(actions[0]->isEnabled());
        QVERIFY(actions[0]->isChecked());
        QVERIFY(actions[1]->isEnabled());
        QVERIFY(!actions[1]->isChecked());
        QVERIFY(actions[2]->isSeparator());
        QVERIFY(!actions[3]->isEnabled());
        QVERIFY(actions[3]->text().contains(QStringLiteral("2 instance")));

        QSignalSpy spy(&btn, &TargetInstanceButton::targetChosen);
        actions[1]->trigger();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("b"));
        QCOMPARE(btn.currentId(), QStringLiteral("b"));
    }

    void buttonMenuWithoutCaption()
    {
        TargetInstanceButton btn(nullptr, testIcon);
        btn.setTargets({ make("a", "Alpha", "1.20.1", ModPlatform::Fabric) });
        std::unique_ptr<QMenu> menu(btn.buildMenu(nullptr));
        QCOMPARE(menu->actions().size(), 1);
    }

    void buttonLongNameIsElided()
    {
        TargetInstanceButton btn(nullptr, testIcon);
        const QString longName = QString(200, QLatin1Char('W'));
        btn.setTargets({ make("a", longName, "1.20.1", ModPlatform::Fabric) });
        btn.setCurrentId("a");
        QVERIFY(btn.sizeHint().width() < 180 + 300);  // the name takes at most 180 px
        btn.show();
        QVERIFY(!btn.grab().isNull());
    }

    void screenshots()
    {
        const QString dir = qEnvironmentVariable("ZERO_TEST_GRAB_DIR");
        if (dir.isEmpty())
            return;
        QDir().mkpath(dir);
        for (const QString& themeId : { QStringLiteral("obsidian"), QStringLiteral("minecraft") }) {
            ThemeSpec spec = themeId == QLatin1String("obsidian") ? ThemeSpec::obsidian() : ThemeSpec::minecraft();
            spec.reduceMotion = true;
            Theme::setCurrent(spec);
            qApp->setPalette(Style::palette(spec));
            qApp->setStyleSheet(Style::styleSheet(spec));
            TargetInstanceButton btn(nullptr, testIcon);
            btn.setTargets({ make("a", "Cozy Survival", "1.20.1", ModPlatform::Fabric, day(1)) });
            btn.setCurrentId("a");
            btn.resize(btn.sizeHint());
            btn.show();
            btn.grab().save(dir + QStringLiteral("/target-") + themeId + QStringLiteral(".png"));
            btn.setFocus();
            btn.grab().save(dir + QStringLiteral("/target-") + themeId + QStringLiteral("-focus.png"));
        }
        Theme::setCurrent(ThemeSpec::obsidian());
        qApp->setStyleSheet(QString());
    }
};

QTEST_MAIN(ZeroModTargetTest)
#include "ZeroModTarget_test.moc"
