// SPDX-License-Identifier: GPL-3.0-only
// ZeroShell + ZeroTopBar: the top bar shows the current page's chrome (title, subtitle, breadcrumb, actions), swaps it on
// page change, follows chromeChanged(), forwards breadcrumb clicks, and Alt+N switches pages.
#include <QApplication>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <functional>
#include <memory>

#include "ui/zero/ZeroNavRail.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/shell/AccountChip.h"
#include "ui/zero/shell/ShellPage.h"
#include "ui/zero/shell/ZeroShell.h"
#include "ui/zero/shell/ZeroTopBar.h"

using namespace Zero;

namespace {

class TestPage : public ShellPage {
   public:
    TestPage(const QString& title, const QString& subtitle, bool withActions, QWidget* parent = nullptr)
        : ShellPage(parent), m_title(title), m_subtitle(subtitle)
    {
        if (withActions) {
            actionButton = new QPushButton(title + QStringLiteral(" action"));
            setTopBarActions(actionButton);
        }
    }

    QString title() const override { return m_title; }
    QString subtitle() const override { return m_subtitle; }
    QString breadcrumbParent() const override { return m_crumb; }
    void breadcrumbActivated() override
    {
        ++crumbClicks;
        if (onCrumb)
            onCrumb();
    }

    void setSubtitle(const QString& subtitle)
    {
        m_subtitle = subtitle;
        emit chromeChanged();
    }
    void setCrumb(const QString& crumb, const QString& title)
    {
        m_crumb = crumb;
        m_title = title;
        emit chromeChanged();
    }

    QPushButton* actionButton = nullptr;
    int crumbClicks = 0;
    std::function<void()> onCrumb;

   private:
    QString m_title;
    QString m_subtitle;
    QString m_crumb;
};

struct Fixture {
    std::unique_ptr<ZeroShell> shell = std::make_unique<ZeroShell>();
    TestPage* home = new TestPage(QStringLiteral("Home"), {}, false);
    TestPage* library = new TestPage(QStringLiteral("Library"), QStringLiteral("6 instances"), true);
    TestPage* tools = new TestPage(QStringLiteral("Tools"), QStringLiteral("Utilities"), false);
    TestPage* appearance = new TestPage(QStringLiteral("Appearance"), QStringLiteral("Themes and motion"), false);

    Fixture()
    {
        shell->addPage(QStringLiteral("home"), QStringLiteral("house"), QStringLiteral("Home"), home);
        shell->addPage(QStringLiteral("library"), QStringLiteral("library-big"), QStringLiteral("Library"), library);
        shell->addPage(QStringLiteral("tools"), QStringLiteral("wrench"), QStringLiteral("Tools"), tools);
        shell->addPage(QStringLiteral("appearance"), QStringLiteral("palette"), QStringLiteral("Appearance"), appearance,
                       ZeroShell::Placement::Bottom);
        shell->finishRail();
        shell->resize(1280, 800);
        shell->show();
        QVERIFY(QTest::qWaitForWindowExposed(shell.get()));
    }
};

}  // namespace

class ZeroShellTest : public QObject {
    Q_OBJECT

   private slots:
    void initTestCase()
    {
        ThemeSpec spec = ThemeSpec::obsidian();
        spec.reduceMotion = true;  // no crossfade / rise overlays: every swap is instant
        Theme::setCurrent(spec);
    }

    void topBarShowsCurrentPageChrome()
    {
        Fixture f;
        ZeroTopBar* bar = f.shell->topBar();
        QCOMPARE(f.shell->currentPage(), QStringLiteral("home"));
        QCOMPARE(bar->titleText(), QStringLiteral("Home"));
        QCOMPARE(bar->subtitleText(), QString());
        QVERIFY(bar->actionsWidget() == nullptr);
        QVERIFY(!bar->dividerVisible());
        QCOMPARE(bar->height(), 60);

        f.shell->setCurrentPage(QStringLiteral("library"));
        QCOMPARE(bar->titleText(), QStringLiteral("Library"));
        QCOMPARE(bar->subtitleText(), QStringLiteral("6 instances"));
        QVERIFY(bar->actionsWidget() == f.library->actionButton);
        QVERIFY(f.library->actionButton->isVisible());
        QVERIFY(bar->dividerVisible());
        QVERIFY(bar->isAncestorOf(f.library->actionButton));

        // another page: the previous page's actions go back to their page, hidden; the new page has none
        f.shell->setCurrentPage(QStringLiteral("tools"));
        QCOMPARE(bar->titleText(), QStringLiteral("Tools"));
        QCOMPARE(bar->subtitleText(), QStringLiteral("Utilities"));
        QVERIFY(bar->actionsWidget() == nullptr);
        QVERIFY(!bar->dividerVisible());
        QVERIFY(!f.library->actionButton->isVisible());
        QVERIFY(f.library->isAncestorOf(f.library->actionButton));

        // coming back shows the same widget again
        f.shell->setCurrentPage(QStringLiteral("library"));
        QVERIFY(bar->actionsWidget() == f.library->actionButton);
        QVERIFY(f.library->actionButton->isVisible());
    }

    void chromeChangedUpdatesSubtitleAndTitle()
    {
        Fixture f;
        f.shell->setCurrentPage(QStringLiteral("library"));
        f.library->setSubtitle(QStringLiteral("7 instances"));
        QCOMPARE(f.shell->topBar()->subtitleText(), QStringLiteral("7 instances"));

        // a page that is not current does not touch the top bar
        f.tools->setSubtitle(QStringLiteral("changed in the background"));
        QCOMPARE(f.shell->topBar()->subtitleText(), QStringLiteral("7 instances"));
        f.shell->setCurrentPage(QStringLiteral("tools"));
        QCOMPARE(f.shell->topBar()->subtitleText(), QStringLiteral("changed in the background"));

        f.tools->setCrumb(QStringLiteral("Tools"), QStringLiteral("Crash Detective"));
        QCOMPARE(f.shell->topBar()->titleText(), QStringLiteral("Crash Detective"));
        QCOMPARE(f.shell->topBar()->breadcrumbText(), QStringLiteral("Tools"));
        f.tools->setCrumb(QString(), QStringLiteral("Tools"));
        QCOMPARE(f.shell->topBar()->breadcrumbText(), QString());
    }

    void breadcrumbClickGoesBack()
    {
        Fixture f;
        f.shell->setCurrentPage(QStringLiteral("tools"));
        f.tools->setCrumb(QStringLiteral("Tools"), QStringLiteral("Crash Detective"));
        f.tools->onCrumb = [&f]() { f.tools->setCrumb(QString(), QStringLiteral("Tools")); };

        ChromeTitle* title = f.shell->topBar()->titleBlock();
        const QRect crumb = title->crumbRect();
        QVERIFY(!crumb.isEmpty());
        QSignalSpy spy(f.shell->topBar(), &ZeroTopBar::breadcrumbClicked);
        QTest::mouseClick(title, Qt::LeftButton, Qt::NoModifier, crumb.center());
        QCOMPARE(spy.count(), 1);
        QCOMPARE(f.tools->crumbClicks, 1);
        QCOMPARE(f.shell->topBar()->breadcrumbText(), QString());
        QCOMPARE(f.shell->topBar()->titleText(), QStringLiteral("Tools"));

        // clicking the title text itself (not the parent segment) does nothing
        f.tools->setCrumb(QStringLiteral("Tools"), QStringLiteral("Crash Detective"));
        QTest::mouseClick(title, Qt::LeftButton, Qt::NoModifier, QPoint(title->width() - 4, title->height() / 2));
        QCOMPARE(f.tools->crumbClicks, 1);
    }

    void pageShortcutsAreAltN()
    {
        Fixture f;
        QCOMPARE(f.shell->pageShortcut(QStringLiteral("home")), QKeySequence(Qt::ALT | Qt::Key_1));
        QCOMPARE(f.shell->pageShortcut(QStringLiteral("library")), QKeySequence(Qt::ALT | Qt::Key_2));
        QCOMPARE(f.shell->pageShortcut(QStringLiteral("tools")), QKeySequence(Qt::ALT | Qt::Key_3));
        QCOMPARE(f.shell->pageShortcut(QStringLiteral("appearance")), QKeySequence(Qt::ALT | Qt::Key_4));

        f.shell->window()->activateWindow();
        if (!QTest::qWaitForWindowActive(f.shell.get(), 2000))
            QSKIP("the test window cannot be activated here: key delivery is not checked");
        QTest::keyClick(f.shell.get(), Qt::Key_2, Qt::AltModifier);
        QCOMPARE(f.shell->currentPage(), QStringLiteral("library"));
        QTest::keyClick(f.shell.get(), Qt::Key_4, Qt::AltModifier);
        QCOMPARE(f.shell->currentPage(), QStringLiteral("appearance"));
        QTest::keyClick(f.shell.get(), Qt::Key_3, Qt::AltModifier);
        QCOMPARE(f.shell->currentPage(), QStringLiteral("tools"));
        QTest::keyClick(f.shell.get(), Qt::Key_1, Qt::AltModifier);
        QCOMPARE(f.shell->currentPage(), QStringLiteral("home"));
    }

    void accountChipAndCenterSlot()
    {
        Fixture f;
        ZeroTopBar* bar = f.shell->topBar();
        f.shell->setAccount(QStringLiteral("Steve"), QStringLiteral("Offline"), QPixmap());
        QCOMPARE(bar->accountChip()->name(), QStringLiteral("Steve"));
        QVERIFY(bar->accountChip()->isVisible());
        QVERIFY(bar->centerWidget() == nullptr);  // reserved slot: nothing is shown

        QSignalSpy menu(bar, &ZeroTopBar::accountMenuRequested);
        QTest::mouseClick(bar->accountChip(), Qt::LeftButton);
        QCOMPARE(menu.count(), 1);
        const QPoint anchor = menu.first().first().toPoint();
        QCOMPARE(anchor, bar->accountChip()->mapToGlobal(QPoint(bar->accountChip()->width(), bar->accountChip()->height())));
    }

    void railHeaderAlignsWithTopBarLine()
    {
        Fixture f;
        // The rail header and the top bar are both 60 px tall and start at the shell's top edge: one continuous rule.
        QCOMPARE(f.shell->topBar()->mapTo(f.shell.get(), QPoint(0, 0)).y(), 0);
        QCOMPARE(f.shell->topBar()->height(), 60);
        QCOMPARE(f.shell->rail()->mapTo(f.shell.get(), QPoint(0, 0)).y(), 0);
    }
};

QTEST_MAIN(ZeroShellTest)
#include "ZeroShell_test.moc"
