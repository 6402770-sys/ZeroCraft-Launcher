// SPDX-License-Identifier: GPL-3.0-only
// ZeroCommandPalette + ZeroShortcutSheet: open / close, sections, keyboard navigation (wraps), Enter and Ctrl+Enter run the
// callbacks, commands the provider leaves out are absent, Esc and backdrop clicks close and give the focus back.
#include <QAccessible>
#include <QApplication>
#include <QLineEdit>
#include <QTest>
#include <memory>

#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/palette/ZeroCommandPalette.h"
#include "ui/zero/palette/ZeroShortcutSheet.h"

using namespace Zero;

namespace {

PaletteCommand command(const QString& id, Palette::Kind kind, const QString& title, std::function<void()> run = {})
{
    PaletteCommand c;
    c.id = id;
    c.kind = kind;
    c.title = title;
    c.iconName = QStringLiteral("play");
    c.run = run ? run : []() {};
    return c;
}

constexpr qint64 kNow = 1'800'000'000'000;

// Focus inside the host window. hasFocus() also needs the OS to activate the window, which it often refuses while
// other GUI tests run in parallel (ctest -j), so it failed at random; the overlays only control the window's focus.
bool focused(QWidget* w)
{
    return w->window()->focusWidget() == w;
}

struct Fixture {
    QWidget host;
    QLineEdit* other = new QLineEdit(&host);
    std::unique_ptr<ZeroCommandPalette> palette;
    QStringList log;
    bool includeUndo = false;

    Fixture()
    {
        Theme::setReduceMotion(true);  // instant open / close: the tests check state, not frames
        host.resize(1000, 700);
        other->setGeometry(10, 10, 200, 24);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        host.activateWindow();
        other->setFocus();
        palette = std::make_unique<ZeroCommandPalette>(&host);
        palette->setCommandProvider([this]() { return commands(); });
    }
    ~Fixture() { Theme::setReduceMotion(false); }

    QList<PaletteCommand> commands()
    {
        QList<PaletteCommand> list;
        auto instance = [&](const QString& id, const QString& title, qint64 lastUsed) {
            PaletteCommand c = command(QStringLiteral("instance:") + id, Palette::Kind::Instance, title, [this, id]() { log << "play:" + id; });
            c.lastUsedMs = lastUsed;
            c.primaryLabel = QStringLiteral("Play");
            c.secondaryLabel = QStringLiteral("Show in Library");
            c.runSecondary = [this, id]() { log << "show:" + id; };
            return c;
        };
        list << instance("fab", "Fabulous Survival", kNow - 3600'000);
        list << instance("vanilla", "Vanilla 1.21", kNow - 5 * 3600'000);
        list << instance("sky", "Skyblock", kNow - 9 * 3600'000);
        list << instance("old", "Old World", kNow - 90LL * 86400'000);
        list << instance("never", "Never Played", 0);
        list << command("nav:home", Palette::Kind::Navigation, "Go to Home", [this]() { log << "nav:home"; });
        list << command("nav:library", Palette::Kind::Navigation, "Go to Library", [this]() { log << "nav:library"; });
        PaletteCommand fresh = command("action:new", Palette::Kind::Action, "New instance", [this]() { log << "new"; });
        fresh.suggested = true;
        list << fresh;
        if (includeUndo)
            list << command("action:undo", Palette::Kind::Action, "Undo last instance deletion", [this]() { log << "undo"; });
        PaletteCommand java = command("settings:java", Palette::Kind::Setting, "Settings: Java", [this]() { log << "java"; });
        java.keywords = { "memory", "ram", "jvm" };
        list << java;
        return list;
    }
};

}  // namespace

class ZeroCommandPaletteTest : public QObject {
    Q_OBJECT
   private slots:
    void opensAndCloses()
    {
        Fixture f;
        QVERIFY(!f.palette->isOpen());
        QVERIFY(!f.palette->isVisible());
        f.palette->open();
        QVERIFY(f.palette->isOpen());
        QVERIFY(f.palette->isVisible());
        QCOMPARE(f.palette->geometry(), f.host.rect());
        QVERIFY(focused(f.palette->input()));
        // Panel: 640 px wide, centred, top edge at 14 % of the window height.
        QCOMPARE(f.palette->panelRect().width(), 640);
        QCOMPARE(f.palette->panelRect().top(), int(700 * 0.14));
        f.palette->closeOverlay();
        QVERIFY(!f.palette->isOpen());
        QVERIFY(!f.palette->isVisible());
    }

    void animatedCloseHidesAfterTheFade()
    {
        Fixture f;
        Theme::setReduceMotion(false);
        f.palette->open();
        QVERIFY(f.palette->isOpen());
        f.palette->closeOverlay();
        QVERIFY(!f.palette->isOpen());
        QTRY_VERIFY_WITH_TIMEOUT(!f.palette->isVisible(), 2000);
    }

    void emptyQuerySections()
    {
        Fixture f;
        f.palette->open();
        QCOMPARE(f.palette->sectionTitles(), (QStringList{ "Continue", "Navigation", "Suggested" }));
        // Continue: the 3 most recently played instances, newest first.
        const QStringList ids = f.palette->resultIds();
        QCOMPARE(ids.mid(0, 3), (QStringList{ "instance:fab", "instance:vanilla", "instance:sky" }));
        QCOMPARE(ids.mid(3, 2), (QStringList{ "nav:home", "nav:library" }));
        QCOMPARE(ids.mid(5), (QStringList{ "action:new" }));
        QCOMPARE(f.palette->selectedId(), QStringLiteral("instance:fab"));
    }

    void queryFiltersAndRanks()
    {
        Fixture f;
        f.palette->open();
        f.palette->setQuery("fab");
        QCOMPARE(f.palette->resultIds().first(), QStringLiteral("instance:fab"));
        QVERIFY(!f.palette->resultIds().contains("nav:home"));
        QCOMPARE(f.palette->sectionTitles().first(), QStringLiteral("Instances"));
        // An alias finds a command whose title does not contain the query.
        f.palette->setQuery("ram");
        QVERIFY(f.palette->resultIds().contains("settings:java"));
        QCOMPARE(f.palette->sectionTitles().first(), QStringLiteral("Settings"));
        f.palette->setQuery("zzzz");
        QVERIFY(f.palette->resultIds().isEmpty());
        QCOMPARE(f.palette->selectedId(), QString());
    }

    void navigationWraps()
    {
        Fixture f;
        f.palette->open();
        f.palette->setQuery("o");  // matches several commands
        const QStringList ids = f.palette->resultIds();
        QVERIFY(ids.size() >= 3);
        QCOMPARE(f.palette->selectedIndex(), 0);
        QTest::keyClick(f.palette->input(), Qt::Key_Down);
        QCOMPARE(f.palette->selectedIndex(), 1);
        QTest::keyClick(f.palette->input(), Qt::Key_Up);
        QTest::keyClick(f.palette->input(), Qt::Key_Up);  // from the first row to the last
        QCOMPARE(f.palette->selectedIndex(), ids.size() - 1);
        QCOMPARE(f.palette->selectedId(), ids.last());
        QTest::keyClick(f.palette->input(), Qt::Key_Down);  // and back to the first
        QCOMPARE(f.palette->selectedIndex(), 0);
        for (int i = 0; i < ids.size(); ++i)
            QTest::keyClick(f.palette->input(), Qt::Key_Down);
        QCOMPARE(f.palette->selectedIndex(), 0);  // a full cycle returns to the start
    }

    void rowsAreAccessible()
    {
        Fixture f;
        f.palette->open();
        f.palette->setQuery("java");
        QAccessibleInterface* list = QAccessible::queryAccessibleInterface(f.palette->results());
        QVERIFY(list);
        QCOMPARE(list->role(), QAccessible::List);
        QVERIFY(list->childCount() >= 2);
        QCOMPARE(list->child(0)->role(), QAccessible::StaticText);  // section header
        int java = -1;
        for (int i = 0; i < list->childCount(); ++i)
            if (list->child(i)->text(QAccessible::Name) == QLatin1String("Settings: Java"))
                java = i;
        QVERIFY(java > 0);
        QAccessibleInterface* item = list->child(java);
        QCOMPARE(item->role(), QAccessible::ListItem);
        QCOMPARE(list->indexOfChild(item), java);
        QCOMPARE(item->parent(), list);
        QVERIFY(item->state().selected);
        QVERIFY(item->rect().isValid());

        // A new query makes the old row interfaces invalid; the list hands out fresh ones.
        f.palette->setQuery("vanilla");
        QVERIFY(!item->isValid());
        QAccessibleInterface* vanilla = nullptr;
        for (int i = 0; i < list->childCount(); ++i)
            if (list->child(i)->text(QAccessible::Name) == QLatin1String("Vanilla 1.21"))
                vanilla = list->child(i);
        QVERIFY(vanilla);
        QVERIFY(vanilla->isValid());
        QAccessibleActionInterface* action = vanilla->actionInterface();
        QVERIFY(action);
        QVERIFY(action->actionNames().contains(QAccessibleActionInterface::pressAction()));
        action->doAction(QAccessibleActionInterface::pressAction());
        QCOMPARE(f.log, (QStringList{ "play:vanilla" }));
        QVERIFY(!f.palette->isOpen());
    }

    void enterRunsTheSelectedCommandAndCloses()
    {
        Fixture f;
        f.palette->open();
        f.palette->setQuery("vanilla");
        QCOMPARE(f.palette->selectedId(), QStringLiteral("instance:vanilla"));
        bool closedBeforeCallback = false;
        // The callback runs after the palette started closing.
        f.palette->setCommandProvider([&]() {
            auto list = f.commands();
            for (auto& c : list)
                if (c.id == "instance:vanilla")
                    c.run = [&]() {
                        closedBeforeCallback = !f.palette->isOpen();
                        f.log << "play:vanilla";
                    };
            return list;
        });
        f.palette->closeOverlay();
        f.palette->open();
        f.palette->setQuery("vanilla");
        QTest::keyClick(f.palette->input(), Qt::Key_Return);
        QCOMPARE(f.log, (QStringList{ "play:vanilla" }));
        QVERIFY(closedBeforeCallback);
        QVERIFY(!f.palette->isOpen());
    }

    void ctrlEnterRunsTheSecondaryAction()
    {
        Fixture f;
        f.palette->open();
        f.palette->setQuery("sky");
        QTest::keyClick(f.palette->input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(f.log, (QStringList{ "show:sky" }));
        QVERIFY(!f.palette->isOpen());
        // A command without a secondary action ignores Ctrl+Enter and stays open.
        f.log.clear();
        f.palette->open();
        f.palette->setQuery("Go to Home");
        QCOMPARE(f.palette->selectedId(), QStringLiteral("nav:home"));
        QTest::keyClick(f.palette->input(), Qt::Key_Return, Qt::ControlModifier);
        QVERIFY(f.log.isEmpty());
        QVERIFY(f.palette->isOpen());
        QTest::keyClick(f.palette->input(), Qt::Key_Return);
        QCOMPARE(f.log, (QStringList{ "nav:home" }));
    }

    void footerFollowsTheSelection()
    {
        Fixture f;
        f.palette->open();
        f.palette->setQuery("sky");
        QCOMPARE(f.palette->footerText(), QStringLiteral("↑↓ Navigate · ↵ Play · Ctrl+↵ Show in Library · Esc Close"));
        f.palette->setQuery("Go to Home");
        QVERIFY(!f.palette->footerText().contains("Ctrl+↵"));
    }

    void disabledCommandsAreAbsent()
    {
        Fixture f;
        f.palette->open();
        f.palette->setQuery("undo");
        QVERIFY(!f.palette->resultIds().contains("action:undo"));
        f.palette->closeOverlay();
        f.includeUndo = true;  // the provider is asked again on every open: the action became enabled
        f.palette->open();
        f.palette->setQuery("undo");
        QVERIFY(f.palette->resultIds().contains("action:undo"));
    }

    void escapeClosesAndRestoresFocus()
    {
        Fixture f;
        QVERIFY(focused(f.other));
        f.palette->open();
        QVERIFY(focused(f.palette->input()));
        QTest::keyClick(f.palette->input(), Qt::Key_Escape);
        QVERIFY(!f.palette->isOpen());
        QVERIFY(focused(f.other));
    }

    void backdropClickClosesButPanelClickDoesNot()
    {
        Fixture f;
        f.palette->open();
        QTest::mouseClick(f.palette.get(), Qt::LeftButton, {}, f.palette->panelRect().center());
        QVERIFY(f.palette->isOpen());
        QTest::mouseClick(f.palette.get(), Qt::LeftButton, {}, QPoint(5, 690));
        QVERIFY(!f.palette->isOpen());
        QVERIFY(focused(f.other));
    }

    void resultViewShowsAtMostEightRowsAndScrolls()
    {
        Fixture f;
        QList<PaletteCommand> many;
        for (int i = 0; i < 30; ++i)
            many << command(QStringLiteral("action:%1").arg(i), Palette::Kind::Action, QStringLiteral("Action %1").arg(i));
        f.palette->setCommandProvider([many]() { return many; });
        f.palette->open();
        f.palette->setQuery("action");
        QCOMPARE(f.palette->resultIds().size(), 30);
        QVERIFY(f.palette->results()->height() <= PaletteResults::kMaxViewHeight);
        for (int i = 0; i < 12; ++i)
            QTest::keyClick(f.palette->input(), Qt::Key_Down);
        QVERIFY(f.palette->results()->scrollOffset() > 0);  // the selection stayed in view
        const QRect row = f.palette->results()->selectedRowRect();
        QVERIFY(row.top() >= 0 && row.bottom() <= f.palette->results()->height());
    }

    void shortcutSheetOpensAndClosesWithEscape()
    {
        Fixture f;
        ZeroShortcutSheet sheet(&f.host);
        sheet.setGroupsProvider([]() {
            ShortcutGroup nav;
            nav.title = "Navigation";
            nav.entries << ShortcutEntry{ "Command palette", { "Ctrl", "K" } } << ShortcutEntry{ "Go to Home", { "Alt", "1" } };
            ShortcutGroup inst;
            inst.title = "Instances";
            inst.entries << ShortcutEntry{ "New instance", { "Ctrl", "N" } };
            return QList<ShortcutGroup>{ nav, inst };
        });
        sheet.open();
        QVERIFY(sheet.isOpen());
        QCOMPARE(sheet.groups().size(), 2);
        QTest::keyClick(&sheet, Qt::Key_Escape);
        QVERIFY(!sheet.isOpen());
        QVERIFY(focused(f.other));
    }

    void keyCapsSplitSequences()
    {
        QCOMPARE(keyCaps(QKeySequence(QStringLiteral("Ctrl+N"))), (QStringList{ "Ctrl", "N" }));
        QCOMPARE(keyCaps(QKeySequence(QStringLiteral("Alt+1"))), (QStringList{ "Alt", "1" }));
        QVERIFY(keyCaps(QKeySequence()).isEmpty());
    }
};

QTEST_MAIN(ZeroCommandPaletteTest)
#include "ZeroCommandPalette_test.moc"
