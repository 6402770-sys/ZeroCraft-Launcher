// SPDX-License-Identifier: GPL-3.0-only
// Export chooser (offscreen): the three cards and their texts, default / remembered selection, the snapshot rule for CurseForge,
// Continue and double-click report the chosen format, keyboard selection; the Library Import button.
#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

#include "ui/zero/ZeroCard.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/pages/ExportChooserDialog.h"
#include "ui/zero/pages/LibraryImportButton.h"

using namespace Zero;

namespace {

QStringList texts(QWidget* w)
{
    QStringList out;
    for (auto* l : w->findChildren<QLabel*>())
        if (!l->text().isEmpty())
            out << l->text();
    return out;
}

}  // namespace

class ZeroExportChooserTest : public QObject {
    Q_OBJECT
   private slots:
    void initTestCase() { Theme::setCurrent(ThemeSpec::obsidian()); }

    void showsThreeCardsWithTheExactTexts()
    {
        ExportChooserDialog dlg(QStringLiteral("My pack"), false, ExportFormat::Zip);
        QCOMPARE(dlg.windowTitle(), QStringLiteral("Export · My pack"));
        const QStringList all = texts(&dlg);
        QVERIFY(all.contains(QStringLiteral("Export · My pack")));
        QVERIFY(all.contains(QStringLiteral("Pick how you want to save it. You choose the exact folders on the next step.")));
        QVERIFY(all.contains(QStringLiteral("Full copy")));
        QVERIFY(all.contains(QStringLiteral("Everything as it is: mods, settings and worlds. Biggest file.")));
        QVERIFY(all.contains(QStringLiteral("Best for a backup, or a friend who uses ZERO or Prism.")));
        QVERIFY(all.contains(QStringLiteral("Modrinth pack")));
        QVERIFY(all.contains(QStringLiteral("· .mrpack")));
        QVERIFY(all.contains(QStringLiteral("A small file: mods are listed and downloaded by your friend, your configs go inside.")));
        QVERIFY(all.contains(QStringLiteral("Best for sharing with ZERO, Prism or Modrinth App players.")));
        QVERIFY(all.contains(QStringLiteral("CurseForge pack")));
        QVERIFY(all.contains(QStringLiteral("Same idea, in CurseForge's format.")));
        QVERIFY(all.contains(QStringLiteral("Best for a friend who uses the CurseForge app.")));
        QVERIFY(all.contains(QStringLiteral("How your friend opens it")));
        QVERIFY(all.contains(QStringLiteral("ZERO: New instance › Import, or drag the file onto the window.")));
        QVERIFY(all.contains(QStringLiteral("Prism: Add instance › Import. Modrinth App and CurseForge app: their own Import.")));
        QCOMPARE(dlg.cancelButton()->text(), QStringLiteral("Cancel"));
        QCOMPARE(dlg.continueButton()->text(), QStringLiteral("Continue"));
        QVERIFY(dlg.continueButton()->isDefault());
        for (auto f : { ExportFormat::Zip, ExportFormat::MrPack, ExportFormat::CurseForge }) {
            QVERIFY(dlg.card(f));
            QVERIFY(dlg.card(f)->isEnabled());
        }
    }

    void defaultAndRememberedSelection()
    {
        QCOMPARE(ExportChooserDialog(QStringLiteral("a"), false, ExportFormat::Zip).selected(), ExportFormat::Zip);
        QCOMPARE(ExportChooserDialog(QStringLiteral("a"), false, ExportFormat::MrPack).selected(), ExportFormat::MrPack);
        QCOMPARE(ExportChooserDialog(QStringLiteral("a"), false, ExportFormat::CurseForge).selected(), ExportFormat::CurseForge);
        QCOMPARE(exportFormatFromKey(QStringLiteral("garbage")), ExportFormat::Zip);
        for (auto f : { ExportFormat::Zip, ExportFormat::MrPack, ExportFormat::CurseForge })
            QCOMPARE(exportFormatFromKey(exportFormatKey(f)), f);
    }

    void snapshotDisablesCurseForgeWithTheReason()
    {
        ExportChooserDialog dlg(QStringLiteral("snap"), true, ExportFormat::CurseForge);
        QVERIFY(!dlg.card(ExportFormat::CurseForge)->isEnabled());
        QVERIFY(texts(dlg.card(ExportFormat::CurseForge)).contains(QStringLiteral("CurseForge packs can't hold snapshot versions.")));
        QVERIFY(!texts(dlg.card(ExportFormat::CurseForge)).contains(QStringLiteral("Best for a friend who uses the CurseForge app.")));
        QCOMPARE(dlg.selected(), ExportFormat::Zip);  // remembered CurseForge is not available
        dlg.setSelected(ExportFormat::CurseForge);
        QCOMPARE(dlg.selected(), ExportFormat::Zip);
        QTest::keyClick(&dlg, Qt::Key_3);
        QCOMPARE(dlg.selected(), ExportFormat::Zip);
        dlg.setSelected(ExportFormat::MrPack);
        QTest::keyClick(&dlg, Qt::Key_Down);  // skips the disabled card
        QCOMPARE(dlg.selected(), ExportFormat::Zip);
    }

    void continueReportsTheChosenFormat()
    {
        ExportChooserDialog dlg(QStringLiteral("a"), false, ExportFormat::Zip);
        QTest::mouseClick(dlg.card(ExportFormat::MrPack), Qt::LeftButton);
        QCOMPARE(dlg.selected(), ExportFormat::MrPack);
        QSignalSpy accepted(&dlg, &QDialog::accepted);
        QTest::mouseClick(dlg.continueButton(), Qt::LeftButton);
        QCOMPARE(accepted.count(), 1);
        QCOMPARE(dlg.result(), int(QDialog::Accepted));
        QCOMPARE(dlg.selected(), ExportFormat::MrPack);
    }

    void cancelRejects()
    {
        ExportChooserDialog dlg(QStringLiteral("a"), false, ExportFormat::Zip);
        QSignalSpy rejected(&dlg, &QDialog::rejected);
        QTest::mouseClick(dlg.cancelButton(), Qt::LeftButton);
        QCOMPARE(rejected.count(), 1);
    }

    void doubleClickAcceptsThatCard()
    {
        ExportChooserDialog dlg(QStringLiteral("a"), false, ExportFormat::Zip);
        QSignalSpy accepted(&dlg, &QDialog::accepted);
        QTest::mouseDClick(dlg.card(ExportFormat::CurseForge), Qt::LeftButton);
        QCOMPARE(accepted.count(), 1);
        QCOMPARE(dlg.selected(), ExportFormat::CurseForge);
    }

    void keyboardSelectionAndEnter()
    {
        ExportChooserDialog dlg(QStringLiteral("a"), false, ExportFormat::Zip);
        dlg.show();
        QTest::keyClick(&dlg, Qt::Key_Down);
        QCOMPARE(dlg.selected(), ExportFormat::MrPack);
        QTest::keyClick(&dlg, Qt::Key_Right);
        QCOMPARE(dlg.selected(), ExportFormat::CurseForge);
        QTest::keyClick(&dlg, Qt::Key_Down);  // wraps
        QCOMPARE(dlg.selected(), ExportFormat::Zip);
        QTest::keyClick(&dlg, Qt::Key_Up);
        QCOMPARE(dlg.selected(), ExportFormat::CurseForge);
        QTest::keyClick(&dlg, Qt::Key_2);
        QCOMPARE(dlg.selected(), ExportFormat::MrPack);
        QTest::keyClick(&dlg, Qt::Key_1);
        QCOMPARE(dlg.selected(), ExportFormat::Zip);
        QSignalSpy accepted(&dlg, &QDialog::accepted);
        QTest::keyClick(&dlg, Qt::Key_3);
        QTest::keyClick(&dlg, Qt::Key_Return);
        QCOMPARE(accepted.count(), 1);
        QCOMPARE(dlg.selected(), ExportFormat::CurseForge);
    }

    void libraryImportButtonEmitsAndCollapses()
    {
        LibraryImportButton button;
        QCOMPARE(button.text(), QStringLiteral("Import"));
        QCOMPARE(button.toolTip(), QStringLiteral("Open a .zip or .mrpack you already have"));
        QSignalSpy clicked(&button, &QPushButton::clicked);
        QTest::mouseClick(&button, Qt::LeftButton);
        QCOMPARE(clicked.count(), 1);
        button.setCompact(true);
        QVERIFY(button.text().isEmpty());
        QVERIFY(!button.icon().isNull());
        QCOMPARE(button.accessibleName(), QStringLiteral("Import"));
        QCOMPARE(button.toolTip(), QStringLiteral("Open a .zip or .mrpack you already have"));
        QTest::mouseClick(&button, Qt::LeftButton);
        QCOMPARE(clicked.count(), 2);
        button.setCompact(false);
        QCOMPARE(button.text(), QStringLiteral("Import"));
    }
};

QTEST_MAIN(ZeroExportChooserTest)
#include "ZeroExportChooser_test.moc"
