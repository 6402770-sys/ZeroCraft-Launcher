// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the widgets of "Try it in a copy" (ui/zero/sandbox): the two questions ("Try it in a copy", "Bring changes back to
// <X>?" with one checkbox per part that differs and the "nothing to bring back" variant) and the banner of a test copy (the three states, the
// busy state, the clicks). Synthetic plans only: no instances, no application.
//
// With the environment variable ZERO_SANDBOX_SHOTS set to a folder, savesScreenshots() writes PNG grabs of the dialogs and banners there.
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QVBoxLayout>

#include "ui/zero/ZeroTheme.h"
#include "ui/zero/ZeroPageNav.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroTokens.h"
#include "ui/zero/sandbox/SandboxBanner.h"
#include "ui/zero/sandbox/SandboxDialogs.h"

using namespace Zero;
using namespace Zero::SandboxUi;
using Zero::Sandbox::Part;
using Zero::Sandbox::PartChange;

namespace {

QString S(const char* text)
{
    return QString::fromUtf8(text);
}

PartChange change(Part part, bool differs, const QStringList& worlds = {})
{
    PartChange c;
    c.part = part;
    c.differs = differs;
    c.worlds = worlds;
    return c;
}

/// A plan with one entry per part, in `allParts()` order; `differing` are the parts that differ.
QList<PartChange> planOf(const QList<Part>& differing)
{
    QList<PartChange> plan;
    for (const Part part : Sandbox::allParts()) {
        const bool differs = differing.contains(part);
        plan.append(change(part, differs, part == Part::Worlds && differs ? QStringList{ "World 1" } : QStringList{}));
    }
    return plan;
}

QStringList labels(const CheckDialog& dialog)
{
    QStringList out;
    for (const QString& id : dialog.optionIds())
        out << dialog.checkBox(id)->text();
    return out;
}

}  // namespace

class ZeroSandboxDialogsTest : public QObject {
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
        ThemeSpec spec = ThemeSpec::obsidian();
        spec.reduceMotion = false;
        Theme::setCurrent(spec);
    }

    // ---------------------------------------------------------------- "Try it in a copy"

    void theStartQuestionOffersWorldsAndTwoButtons()
    {
        const CheckPrompt prompt = startPrompt(S("My Pack"));
        QCOMPARE(prompt.title, S("Try it in a copy"));
        QCOMPARE(prompt.body, S("Make a test copy of My Pack. Change mods, versions or settings there, then bring the changes back to My Pack or discard "
                                "the copy. My Pack stays as it is."));
        QCOMPARE(prompt.options.size(), 1);
        QCOMPARE(prompt.options.first().id, S("worlds"));
        QCOMPARE(prompt.options.first().label, S("Copy worlds too"));
        QVERIFY(prompt.options.first().checked);  // default on
        QCOMPARE(prompt.buttons.size(), 2);
        QCOMPARE(prompt.buttons.at(0).label, S("Cancel"));
        QCOMPARE(prompt.buttons.at(1).label, S("Make test copy"));
        QCOMPARE(int(prompt.buttons.at(1).kind), int(ModsUi::ChoiceButton::Kind::Primary));

        CheckDialog dialog(prompt);
        QCOMPARE(dialog.windowTitle(), S("Try it in a copy"));
        QVERIFY(dialog.checkBox(S("worlds"))->isChecked());
        QCOMPARE(dialog.buttonAt(1)->text(), S("Make test copy"));
        QVERIFY(dialog.buttonAt(1)->isDefault());
        QVERIFY(!dialog.buttonAt(0)->isDefault());
    }

    void theStartAnswerCarriesTheWorldsChoice()
    {
        CheckDialog dialog(startPrompt(S("X")));
        dialog.show();
        QTimer::singleShot(0, &dialog, [&]() {
            dialog.checkBox(S("worlds"))->setChecked(false);
            dialog.buttonAt(1)->click();
        });
        const CheckAnswer answer = dialog.choose();
        QCOMPARE(answer.button, 1);
        QVERIFY(!answer.isChecked(S("worlds")));
    }

    // ---------------------------------------------------------------- "Bring changes back"

    void theBringBackQuestionListsOnlyThePartsThatDiffer()
    {
        const CheckPrompt prompt = bringBackPrompt(S("My Pack"), planOf({ Part::Mods, Part::GameVersion, Part::Worlds }));
        QCOMPARE(prompt.title, S("Bring changes back to My Pack?"));
        QStringList ids;
        for (const CheckOption& o : prompt.options)
            ids << o.id;
        QCOMPARE(ids, (QStringList{ "mods", "gameversion", "worlds", "discard" }));

        CheckDialog dialog(prompt);
        QCOMPARE(labels(dialog), (QStringList{ "Mods", "Game version and loader", "Worlds", "Discard the test copy afterwards" }));
        QVERIFY(dialog.checkBox(S("mods"))->isChecked());
        QVERIFY(dialog.checkBox(S("gameversion"))->isChecked());
        QVERIFY(!dialog.checkBox(S("worlds"))->isChecked());  // worlds are never brought back by default
        QVERIFY(dialog.checkBox(S("discard"))->isChecked());
        QVERIFY(!dialog.checkBox(S("config")));  // not offered: it does not differ
        QCOMPARE(prompt.note, S("A copy of My Pack is saved first, so you can undo this."));
        QCOMPARE(prompt.buttons.at(1).label, S("Bring changes back"));
        QVERIFY(dialog.buttonAt(0)->isDefault());  // Enter cancels
        QVERIFY(dialog.findChild<QLabel*>(S("zeroSandboxCaption-worlds")));
        QCOMPARE(dialog.findChild<QLabel*>(S("zeroSandboxCaption-worlds"))->text(),
                 S("Replaces My Pack's worlds with the copy's. The old ones are kept as backups."));
        QVERIFY(!dialog.findChild<QLabel*>(S("zeroSandboxCaption-mods")));
        QVERIFY(dialog.findChild<QLabel*>(S("zeroSandboxDialogNote")));
    }

    void everyPartHasItsOwnLabelInTheOrderOfTheDialog()
    {
        const CheckPrompt prompt = bringBackPrompt(S("X"), planOf(Sandbox::allParts()));
        CheckDialog dialog(prompt);
        QCOMPARE(labels(dialog), (QStringList{ "Mods", "Config", "Resource packs", "Shader packs", "Game version and loader", "Worlds",
                                              "Discard the test copy afterwards" }));
    }

    void theChosenPartsFollowTheCheckboxes()
    {
        CheckDialog dialog(bringBackPrompt(S("X"), planOf(Sandbox::allParts())));
        dialog.checkBox(S("config"))->setChecked(false);
        dialog.checkBox(S("worlds"))->setChecked(true);
        dialog.checkBox(S("discard"))->setChecked(false);
        dialog.buttonAt(1)->click();
        const CheckAnswer answer = dialog.answer();
        QCOMPARE(answer.button, 1);
        QCOMPARE(chosenParts(answer), (QList<Part>{ Part::Mods, Part::ResourcePacks, Part::ShaderPacks, Part::GameVersion, Part::Worlds }));
        QVERIFY(!answer.isChecked(S("discard")));
    }

    void whenNothingDiffersTheQuestionSaysSo()
    {
        const CheckPrompt prompt = bringBackPrompt(S("My Pack"), planOf({}));
        QVERIFY(prompt.options.isEmpty());
        QCOMPARE(prompt.title, S("No changes to bring back"));
        QCOMPARE(prompt.buttons.size(), 2);
        QCOMPARE(prompt.buttons.at(0).label, S("Close"));
        QCOMPARE(prompt.buttons.at(1).label, S("Discard copy"));
        QVERIFY(prompt.note.isEmpty());  // nothing is saved first, there is nothing to undo

        CheckDialog dialog(prompt);
        QVERIFY(dialog.optionIds().isEmpty());
        QVERIFY(dialog.buttonAt(0)->isDefault());
        QVERIFY(!dialog.findChild<QLabel*>(S("zeroSandboxDialogNote")));
    }

    void aDismissedDialogAnswersMinusOne()
    {
        CheckDialog dialog(startPrompt(S("X")));
        dialog.show();
        QTimer::singleShot(0, &dialog, [&]() { dialog.reject(); });
        QCOMPARE(dialog.choose().button, -1);
    }

    // ---------------------------------------------------------------- the banner

    void theBannerShowsTheOriginalOrSaysItIsGone()
    {
        SandboxBanner banner(SandboxBanner::Look::Card);
        banner.setState(S("copy-1"), S("My Pack"));
        QVERIFY(!banner.isHidden());
        QCOMPARE(banner.titleLabel()->text(), S("Test copy of My Pack"));
        QCOMPARE(banner.bringBackButton()->text(), S("Bring changes back"));
        QCOMPARE(banner.discardButton()->text(), S("Discard copy"));
        QVERIFY(!banner.bringBackButton()->isHidden());
        QVERIFY(banner.keepButton()->isHidden());
        QCOMPARE(banner.copyId(), S("copy-1"));

        banner.setState(S("copy-1"), QString());  // the original was deleted
        QCOMPARE(banner.titleLabel()->text(), S("The original instance is gone"));
        QVERIFY(banner.bringBackButton()->isHidden());
        QVERIFY(!banner.keepButton()->isHidden());
        QCOMPARE(banner.keepButton()->text(), S("Keep as a normal instance"));
        QVERIFY(!banner.discardButton()->isHidden());

        banner.setState(QString(), QString());  // not a test copy
        QVERIFY(banner.isHidden());
    }

    void theBannerButtonsReportTheirClicksWithTheCopyId()
    {
        SandboxBanner banner(SandboxBanner::Look::Compact);
        banner.setState(S("copy-7"), S("Orig"));
        QSignalSpy bring(Requests::instance(), &Requests::bringBackRequested);
        QSignalSpy discard(Requests::instance(), &Requests::discardRequested);
        QSignalSpy keep(Requests::instance(), &Requests::keepRequested);
        QSignalSpy ownBring(&banner, &SandboxBanner::bringBackClicked);
        banner.bringBackButton()->click();
        banner.discardButton()->click();
        QCOMPARE(bring.count(), 1);
        QCOMPARE(bring.first().first().toString(), S("copy-7"));
        QCOMPARE(discard.count(), 1);
        QCOMPARE(discard.first().first().toString(), S("copy-7"));
        QCOMPARE(ownBring.count(), 1);
        banner.setState(S("copy-7"), QString());
        banner.keepButton()->click();
        QCOMPARE(keep.count(), 1);
        QCOMPARE(keep.first().first().toString(), S("copy-7"));
    }

    // The owner's bug: in the instance window the compact title wrapped onto two lines and the icon was drawn over its first letters.
    // Now, inside the side column (220 px, never wider): icon + one-line elided title, then the two buttons one under the other at full width.
    void theCompactBannerFitsTheSideColumnWithoutOverlaps()
    {
        applySpecLive(ThemeSpec::obsidian());
        const int column = PageNavDelegate::kWidth - 2 * Tokens::spaceLg;  // 220
        QCOMPARE(column, 220);
        const QString longName = S("Axas Perfect Pack With A Rather Long Name");
        for (const QString& name : { S("Pack"), longName }) {
            for (const bool gone : { false, true }) {
                QWidget host;
                auto* layout = new QVBoxLayout(&host);
                layout->setContentsMargins(0, 0, 0, 0);
                auto* banner = new SandboxBanner(SandboxBanner::Look::Compact);
                layout->addWidget(banner);
                banner->setState(S("c"), gone ? QString() : name);
                // the banner never asks the column for more width
                QVERIFY2(banner->minimumSizeHint().width() <= column, qPrintable(QString::number(banner->minimumSizeHint().width())));
                QVERIFY2(banner->sizeHint().width() <= column, qPrintable(QString::number(banner->sizeHint().width())));
                host.setFixedWidth(column);
                host.show();
                QVERIFY(QTest::qWaitForWindowExposed(&host));
                QCoreApplication::processEvents();
                QCOMPARE(banner->width(), column);

                QLabel* icon = banner->iconLabel();
                QLabel* title = banner->titleLabel();
                QVERIFY(icon);
                QCOMPARE(icon->size(), QSize(16, 16));
                QVERIFY(!icon->pixmap().isNull());
                // one line: no wrapping, as tall as one line of its font, on the icon's centre line
                QVERIFY(!title->wordWrap());
                QVERIFY(!title->text().contains(QLatin1Char('\n')));
                QVERIFY(title->height() < 2 * title->fontMetrics().height());
                const QRect iconR = icon->geometry();
                const QRect titleR = title->geometry();
                QVERIFY(!iconR.intersects(titleR));
                QVERIFY(iconR.right() < titleR.left());
                QVERIFY(qAbs(iconR.center().y() - titleR.center().y()) <= 1);
                QVERIFY(titleR.right() < banner->width());
                // the buttons: one under the other, full width, inside the banner, spaceSm apart, below the title row
                QPushButton* primary = gone ? banner->keepButton() : banner->bringBackButton();
                QPushButton* discard = banner->discardButton();
                const QRect primaryR = primary->geometry();
                const QRect discardR = discard->geometry();
                const int top = qMax(iconR.bottom(), titleR.bottom());
                QCOMPARE(primaryR.top() - top - 1, Tokens::spaceSm);
                QCOMPARE(discardR.top() - primaryR.bottom() - 1, Tokens::spaceSm);
                QCOMPARE(primaryR.height(), 28);
                QCOMPARE(discardR.height(), 28);
                QCOMPARE(primaryR.left(), iconR.left());
                QCOMPARE(discardR.left(), primaryR.left());
                QCOMPARE(discardR.width(), primaryR.width());
                QVERIFY(primaryR.right() < banner->width());
                QVERIFY(discardR.bottom() < banner->height());
                // the button text fits the button
                QVERIFY(primary->fontMetrics().horizontalAdvance(primary->text()) < primaryR.width());
                // the title: whole when it fits, else elided with the whole text in the tooltip
                const QString shown = title->text();
                if (title->fontMetrics().horizontalAdvance(banner->titleText()) <= titleR.width()) {
                    QCOMPARE(shown, banner->titleText());
                } else {
                    QVERIFY(shown.endsWith(QChar(0x2026)));
                    QVERIFY(shown.size() > 3);
                    QCOMPARE(title->toolTip(), banner->titleText());
                }
                if (name == longName && !gone)
                    QVERIFY(shown != banner->titleText());  // this one is too long for 220 px
            }
        }
    }

    void theCardBannerKeepsTheIconBesideTheWrappedTitle()
    {
        SandboxBanner banner(SandboxBanner::Look::Card);
        banner.setState(S("c"), S("Axas Perfect Pack"));
        QVERIFY(!banner.iconLabel());  // the card paints its icon in the title's left margin
        QVERIFY(banner.titleLabel()->wordWrap());
        QCOMPARE(banner.titleLabel()->text(), S("Test copy of Axas Perfect Pack"));
        QVERIFY(banner.titleLabel()->contentsMargins().left() >= 16 + Tokens::spaceSm);
    }

    void aBusyInstanceDisablesTheButtonsAndSaysWhy()
    {
        SandboxBanner banner(SandboxBanner::Look::Card);
        banner.setState(S("c"), S("Orig"), S("Close the game first."));
        for (QPushButton* b : { banner.bringBackButton(), banner.discardButton() }) {
            QVERIFY(!b->isEnabled());
            QCOMPARE(b->toolTip(), S("Close the game first."));
        }
        banner.setBusyReason(QString());
        QVERIFY(banner.bringBackButton()->isEnabled());
        QVERIFY(banner.discardButton()->isEnabled());
        QVERIFY(banner.bringBackButton()->toolTip().isEmpty());
    }

    // ---------------------------------------------------------------- screenshots for review

    void savesScreenshots()
    {
        const QString dir = qEnvironmentVariable("ZERO_SANDBOX_SHOTS");
        if (dir.isEmpty())
            return;
        QDir().mkpath(dir);
        for (const char* themeName : { "obsidian", "minecraft" }) {
            ThemeSpec spec = QByteArray(themeName) == "minecraft" ? ThemeSpec::minecraft() : ThemeSpec::obsidian();
            spec.reduceMotion = true;
            applySpecLive(spec);
            const QString prefix = QDir(dir).filePath(QString::fromLatin1(themeName) + QLatin1Char('-'));

            auto shoot = [&](QWidget& widget, const QString& name) {
                widget.show();
                QTest::qWait(60);
                QVERIFY(widget.grab().save(prefix + name + S(".png")));
            };
            {
                CheckDialog dialog(startPrompt(S("Survival 1.20")));
                shoot(dialog, S("sandbox-start"));
            }
            {
                CheckDialog dialog(bringBackPrompt(S("Survival 1.20"), planOf({ Part::Mods, Part::Config, Part::GameVersion, Part::Worlds })));
                shoot(dialog, S("sandbox-bring-back"));
            }
            {
                CheckDialog dialog(bringBackPrompt(S("Survival 1.20"), planOf({})));
                shoot(dialog, S("sandbox-nothing"));
            }
            {
                SandboxBanner card(SandboxBanner::Look::Card);
                card.setState(S("c"), S("Survival 1.20"));
                card.setFixedWidth(266);
                shoot(card, S("sandbox-banner-card"));
                card.setState(S("c"), QString());
                shoot(card, S("sandbox-banner-card-gone"));
                card.setState(S("c"), S("Survival 1.20"), S("Close the game first."));
                shoot(card, S("sandbox-banner-card-busy"));
            }
            {
                SandboxBanner compact(SandboxBanner::Look::Compact);
                compact.setState(S("c"), S("Survival 1.20"));
                compact.setFixedWidth(640);
                shoot(compact, S("sandbox-banner-compact"));
                compact.setState(S("c"), S("Axas Perfect Pack"));
                compact.setFixedWidth(PageNavDelegate::kWidth - 2 * Tokens::spaceLg);  // the instance window's side column (header slot margins)
                shoot(compact, S("sandbox-banner-compact-sidebar"));
                compact.setState(S("c"), S("Axas Perfect Pack With A Rather Long Name"));
                shoot(compact, S("sandbox-banner-compact-sidebar-long"));
                compact.setMinimumWidth(0);
                compact.setMaximumWidth(QWIDGETSIZE_MAX);
                compact.setState(S("c"), S("Axas Perfect Pack"));
                compact.adjustSize();
                shoot(compact, S("sandbox-banner-compact-natural"));
            }
        }
    }
};

QTEST_MAIN(ZeroSandboxDialogsTest)

#include "ZeroSandboxDialogs_test.moc"
