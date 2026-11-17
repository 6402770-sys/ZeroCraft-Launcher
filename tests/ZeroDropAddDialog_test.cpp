// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the "Add to an instance" dialog and the drop overlay of smart drag and drop (docs/zero/SHELL.md, "Drag and drop").
// Offscreen, synthetic files in temp folders, the install step injected: no instance, no APPLICATION.
//
// With the environment variable ZERO_DROP_SHOTS set to a folder, savesScreenshots() writes drop-dialog.png and drop-overlay.png there.
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QMimeData>
#include <QPixmap>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include "archive/ArchiveWriter.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroTheme.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/content/DropAddDialog.h"
#include "ui/zero/shell/DropOverlay.h"

using namespace Zero;
using namespace Zero::Content;
using namespace Zero::Content::Drop;

namespace {

QString S(const char* text)
{
    return QString::fromUtf8(text);
}

QIcon testIcon(const QString&)
{
    QPixmap pm(20, 20);
    pm.fill(Qt::darkGreen);
    return QIcon(pm);
}

TargetInstance instance(const QString& id, const QString& name, const QString& mc, ModPlatform::ModLoaderTypes loaders)
{
    TargetInstance t;
    t.id = id;
    t.name = name;
    t.iconKey = QStringLiteral("default");
    t.mcVersion = mc;
    t.loaders = loaders;
    t.loaderText = loaderDisplayNames(loaders).join(QStringLiteral(", "));
    return t;
}

DropFile file(const QString& fileName, Kind kind, const QString& name = {}, qint64 size = 2048)
{
    DropFile f;
    f.path = fileName;
    f.fileName = fileName;
    f.name = name.isEmpty() ? fileName : name;
    f.kind = kind;
    f.size = size;
    return f;
}

DropFile mod(const QString& fileName, const QString& name, ModPlatform::ModLoaderTypes loaders, const QString& mc = {})
{
    DropFile f = file(fileName, Kind::Mod, name, 1500 * 1024);
    f.mod.displayName = name;
    f.mod.loaders = loaders;
    if (!mc.isEmpty()) {
        f.mod.minecraft.push_back({ mc, false });
    }
    return f;
}

/// A window that takes drops like the main window does.
class DropWindow : public QWidget {
   protected:
    void dragEnterEvent(QDragEnterEvent* event) override { event->acceptProposedAction(); }
    void dragMoveEvent(QDragMoveEvent* event) override { event->acceptProposedAction(); }
    void dropEvent(QDropEvent* event) override { event->acceptProposedAction(); }
};

struct Rig {
    DropAddEnvironment env;
    QStringList installed;  ///< "<target>:<file>"
    QSet<QString> failing;
    int created = 0;
    QString replaceFolder;

    Rig()
    {
        env.targets = { instance("fab", "Fabric Fun", "1.21.1", ModPlatform::Fabric), instance("forge", "Forge Fun", "1.20.1", ModPlatform::Forge),
                        instance("vanilla", "Vanilla", "1.21.1", {}) };
        env.icons = testIcon;
        env.folderFor = [this](const QString&, Kind) { return replaceFolder; };
        env.install = [this](const QString& target, const DropFile& f) -> QString {
            if (failing.contains(f.fileName)) {
                return S("disk full");
            }
            installed << target + QLatin1Char(':') + f.fileName;
            return {};
        };
        env.createInstance = [this]() { ++created; };
    }

    /// A Fabric mod made for another Minecraft, a resource pack, a data pack.
    QList<DropFile> three() const
    {
        return { mod(S("sodium-0.5.jar"), S("Sodium"), ModPlatform::Fabric, S("1.20.1")), file(S("Faithful.zip"), Kind::ResourcePack),
                 file(S("terrain.zip"), Kind::DataPack) };
    }
};

bool writeFabricJar(const QString& path, const QString& json)
{
    MMCZip::ArchiveWriter writer(path);
    if (!writer.open()) {
        return false;
    }
    if (!writer.addFile(QStringLiteral("fabric.mod.json"), json.toUtf8())) {
        return false;
    }
    return bool(writer.close());
}

}  // namespace

class ZeroDropAddDialogTest : public QObject {
    Q_OBJECT

   private slots:
    void initTestCase()
    {
        ThemeSpec spec = ThemeSpec::obsidian();
        spec.reduceMotion = true;
        applySpecLive(spec);
        Theme::setReduceMotion(true);
        QVERIFY(Motion::duration(DropOverlay::kFadeMs) == 0);
    }

    void buildsOneRowPerFile()
    {
        Rig rig;
        DropAddDialog dialog(rig.env, rig.three());
        dialog.show();
        QVERIFY(!dialog.isEmptyState());
        QCOMPARE(dialog.rowCount(), 3);
        QCOMPARE(dialog.titleText(), S("Add 3 files"));
        QCOMPARE(dialog.rowKind(0), S("Mod"));
        QCOMPARE(dialog.rowKind(1), S("Resource pack"));
        QCOMPARE(dialog.rowKind(2), S("Data pack"));
        QCOMPARE(dialog.rowName(0), S("Sodium"));  // the metadata name, the file name is a caption
        QCOMPARE(dialog.rowName(1), S("Faithful.zip"));
        // the library selection is not set: the saved id is empty too, so the first mod-ready instance by recent use or name
        QVERIFY(!dialog.targetId().isEmpty());
        QVERIFY(dialog.rowBox(0)->isChecked());
        QVERIFY(dialog.rowBox(1)->isChecked());
        // the data pack is listed, not installable
        QVERIFY(!dialog.rowBox(2)->isEnabled());
        QVERIFY(!dialog.rowBox(2)->isChecked());
        QCOMPARE(dialog.rowNote(2), S("Data packs go inside a world. Open the world's folder to add it."));
        QCOMPARE(dialog.summaryText(), S("Adds 2 files"));
    }

    void oneFileTitle()
    {
        Rig rig;
        DropAddDialog dialog(rig.env, { file(S("Faithful.zip"), Kind::ResourcePack) });
        QCOMPARE(dialog.titleText(), S("Add 1 file"));
        QCOMPARE(dialog.summaryText(), S("Adds 1 file"));
    }

    void defaultTargetUsesLibraryThenSavedAndNeedsLoaderForMods()
    {
        Rig rig;
        rig.env.librarySelectedId = S("forge");
        {
            DropAddDialog dialog(rig.env, rig.three());
            QCOMPARE(dialog.targetId(), S("forge"));
            QCOMPARE(dialog.primaryButton()->text(), S("Add to Forge Fun"));
        }
        rig.env.librarySelectedId = S("vanilla");  // cannot take mods: the saved drop target
        rig.env.savedTargetId = S("fab");
        {
            DropAddDialog dialog(rig.env, rig.three());
            QCOMPARE(dialog.targetId(), S("fab"));
        }
        {
            // no mod dropped: vanilla is a fine target for a pack
            DropAddDialog dialog(rig.env, { file(S("Faithful.zip"), Kind::ResourcePack) });
            QCOMPARE(dialog.targetId(), S("vanilla"));
        }
    }

    void changingTheTargetUpdatesTheCaptions()
    {
        Rig rig;
        rig.env.librarySelectedId = S("fab");
        DropAddDialog dialog(rig.env, { mod(S("forgemod.jar"), S("Forge Mod"), ModPlatform::Forge, S("1.20.1")),
                                        mod(S("anyfabric.jar"), S("Any Fabric"), ModPlatform::Fabric), mod(S("unknown.jar"), S("Unknown"), {}) });
        dialog.show();
        QCOMPARE(dialog.targetId(), S("fab"));
        QCOMPARE(dialog.rowFit(0), S("Made for Forge \u2014 this instance uses Fabric"));
        QVERIFY(dialog.rowFit(1).isEmpty());
        QVERIFY(dialog.rowFit(2).isEmpty());  // unknown never warns
        QVERIFY(dialog.rowBox(0)->isChecked());  // a mismatch stays ticked: the caption is the warning
        QVERIFY(dialog.primaryButton()->isEnabled());
        dialog.setTarget(S("forge"));
        QVERIFY(dialog.rowFit(0).isEmpty());  // fits Forge 1.20.1
        QCOMPARE(dialog.rowFit(1), S("Made for Fabric \u2014 this instance uses Forge"));
        QCOMPARE(dialog.primaryButton()->text(), S("Add to Forge Fun"));
    }

    void versionMismatchCaptionShowsForTheChosenInstance()
    {
        Rig rig;
        rig.env.librarySelectedId = S("fab");
        DropAddDialog dialog(rig.env, { mod(S("old.jar"), S("Old"), ModPlatform::Fabric, S("1.20.1")) });
        const QString caption = dialog.rowFit(0);
        QVERIFY2(caption.startsWith(S("Made for Minecraft ")), qPrintable(caption));
        QVERIFY2(caption.endsWith(S("this instance is 1.21.1")), qPrintable(caption));
    }

    void replaceCaptionComesFromTheTargetFolder()
    {
        QTemporaryDir tmp;
        QFile f(tmp.filePath(S("sodium-0.5.jar")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.close();
        Rig rig;
        rig.replaceFolder = tmp.path();
        DropAddDialog dialog(rig.env, rig.three());
        QCOMPARE(dialog.rowReplace(0), S("Replaces the file with the same name"));
        QVERIFY(dialog.rowReplace(1).isEmpty());
    }

    void addIsDisabledWhenNothingIsTicked()
    {
        Rig rig;
        DropAddDialog dialog(rig.env, rig.three());
        dialog.show();
        QVERIFY(dialog.primaryButton()->isEnabled());
        dialog.rowBox(0)->setChecked(false);
        QVERIFY(dialog.primaryButton()->isEnabled());  // the pack is still ticked
        QCOMPARE(dialog.summaryText(), S("Adds 1 file"));
        dialog.rowBox(1)->setChecked(false);
        QVERIFY(!dialog.primaryButton()->isEnabled());
        QCOMPARE(dialog.summaryText(), S("Nothing to add"));
        dialog.rowBox(1)->setChecked(true);
        QVERIFY(dialog.primaryButton()->isEnabled());
    }

    void addIsDisabledWithoutATarget()
    {
        Rig rig;
        rig.env.targets = { instance("vanilla", "Vanilla", "1.21.1", {}) };  // cannot take the mod
        DropAddDialog dialog(rig.env, { mod(S("a.jar"), S("A"), ModPlatform::Fabric) });
        QVERIFY(dialog.targetId().isEmpty());
        QVERIFY(!dialog.primaryButton()->isEnabled());
    }

    void emptyStateWhenThereIsNoInstance()
    {
        Rig rig;
        rig.env.targets.clear();
        DropAddDialog dialog(rig.env, rig.three());
        dialog.show();
        QVERIFY(dialog.isEmptyState());
        QCOMPARE(dialog.rowCount(), 0);
        QCOMPARE(dialog.primaryButton()->text(), S("Create instance"));
        QVERIFY(dialog.primaryButton()->isEnabled());
        dialog.primaryButton()->click();
        QCOMPARE(rig.created, 1);
        QVERIFY(dialog.createInstanceRequested());
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QVERIFY(rig.installed.isEmpty());
    }

    void acceptInstallsTheTickedFilesAndReturnsThem()
    {
        Rig rig;
        rig.env.librarySelectedId = S("fab");
        DropAddDialog dialog(rig.env, rig.three());
        dialog.show();
        dialog.primaryButton()->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.targetId(), S("fab"));
        QCOMPARE(rig.installed, QStringList({ S("fab:sodium-0.5.jar"), S("fab:Faithful.zip") }));  // not the data pack
        QCOMPARE(dialog.addedFiles().size(), 2);
        QVERIFY(dialog.failures().isEmpty());
        QCOMPARE(dialog.chosenFiles().size(), 2);
    }

    void unticked_filesAreNotInstalled()
    {
        Rig rig;
        rig.env.librarySelectedId = S("fab");
        DropAddDialog dialog(rig.env, rig.three());
        dialog.rowBox(0)->setChecked(false);
        dialog.primaryButton()->click();
        QCOMPARE(rig.installed, QStringList({ S("fab:Faithful.zip") }));
    }

    void aFailedInstallIsReportedAndTheRestGoesOn()
    {
        Rig rig;
        rig.env.librarySelectedId = S("fab");
        rig.failing.insert(S("sodium-0.5.jar"));
        DropAddDialog dialog(rig.env, rig.three());
        dialog.primaryButton()->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.addedFiles().size(), 1);
        QCOMPARE(dialog.addedFiles().first().fileName, S("Faithful.zip"));
        QCOMPARE(dialog.failures().size(), 1);
        QCOMPARE(dialog.failures().first().first.fileName, S("sodium-0.5.jar"));
        QCOMPARE(dialog.failures().first().second, S("disk full"));
    }

    void cancelInstallsNothing()
    {
        Rig rig;
        DropAddDialog dialog(rig.env, rig.three());
        dialog.show();
        const auto buttons = dialog.findChildren<QPushButton*>();
        QPushButton* cancel = nullptr;
        for (QPushButton* b : buttons) {
            if (b->text() == S("Cancel")) {
                cancel = b;
            }
        }
        QVERIFY(cancel);
        cancel->click();
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QVERIFY(rig.installed.isEmpty());
    }

    // ---- the overlay

    void overlayFollowsTheDrag()
    {
        DropWindow window;
        window.resize(800, 500);
        window.setAcceptDrops(true);
        window.show();
        DropOverlay overlay(&window);
        QVERIFY(!overlay.overlayShown());
        QVERIFY(overlay.testAttribute(Qt::WA_TransparentForMouseEvents));  // it never steals the drop

        QMimeData files;
        files.setUrls({ QUrl::fromLocalFile(S("C:/x/a.jar")) });
        QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &files, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &enter);
        QVERIFY(overlay.overlayShown());
        QVERIFY(overlay.isVisible());
        QCOMPARE(overlay.geometry(), window.rect());
        QCOMPARE(overlay.opacity(), 1.0);  // reduced motion: instant

        // leaving one child to enter the next must not flicker: a DragEnter inside the grace time keeps it
        QDragLeaveEvent leave;
        QApplication::sendEvent(&window, &leave);
        QVERIFY(overlay.overlayShown());
        QApplication::sendEvent(&window, &enter);
        QTest::qWait(DropOverlay::kLeaveGraceMs + 40);
        QVERIFY(overlay.overlayShown());

        QApplication::sendEvent(&window, &leave);
        QTRY_VERIFY(!overlay.overlayShown());
        QTRY_VERIFY(!overlay.isVisible());

        QApplication::sendEvent(&window, &enter);
        QVERIFY(overlay.overlayShown());
        QDropEvent drop(QPoint(10, 10), Qt::CopyAction, &files, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &drop);
        QVERIFY(!overlay.overlayShown());
        QVERIFY(!overlay.isVisible());
    }

    void overlayIgnoresDragsWithoutLinks()
    {
        QWidget window;
        window.resize(400, 300);
        window.show();
        DropOverlay overlay(&window);
        QMimeData text;
        text.setText(S("hello"));
        QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &text, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &enter);
        QVERIFY(!overlay.overlayShown());
    }

    void overlayFollowsTheWindowSize()
    {
        QWidget window;
        window.resize(400, 300);
        window.show();
        DropOverlay overlay(&window);
        window.resize(640, 480);
        QCOMPARE(overlay.geometry(), QRect(0, 0, 640, 480));
    }

    void savesScreenshots()
    {
        const QString dir = qEnvironmentVariable("ZERO_DROP_SHOTS");
        if (dir.isEmpty()) {
            return;
        }
        QDir().mkpath(dir);
        ThemeSpec spec = ThemeSpec::obsidian();
        spec.reduceMotion = true;
        applySpecLive(spec);

        QTemporaryDir tmp;
        const QString jar = tmp.filePath(S("sodium-fabric-0.5.jar"));
        QVERIFY(writeFabricJar(jar, S("{\"schemaVersion\":1,\"id\":\"sodium\",\"name\":\"Sodium\",\"version\":\"0.5\",\"depends\":{\"minecraft\":\"~1.20.1\"}}")));
        QFile pack(tmp.filePath(S("Faithful 32x.zip")));
        QVERIFY(pack.open(QIODevice::WriteOnly));
        pack.write(QByteArray(3 * 1024 * 1024, 'x'));
        pack.close();
        QFile data(tmp.filePath(S("better-terrain.zip")));
        QVERIFY(data.open(QIODevice::WriteOnly));
        data.write(QByteArray(40 * 1024, 'x'));
        data.close();

        Rig rig;
        rig.env.librarySelectedId = S("fab");
        rig.replaceFolder = tmp.path();  // the pack is "already there"
        QList<DropFile> files = { inspectFile(jar, Kind::Mod), inspectFile(pack.fileName(), Kind::ResourcePack),
                                  inspectFile(data.fileName(), Kind::DataPack) };
        {
            DropAddDialog dialog(rig.env, files);
            dialog.show();
            QTest::qWait(80);
            QVERIFY(dialog.grab().save(QDir(dir).filePath(S("drop-dialog.png"))));
        }
        {
            Rig empty;
            empty.env.targets.clear();
            DropAddDialog dialog(empty.env, files);
            dialog.show();
            QTest::qWait(80);
            QVERIFY(dialog.grab().save(QDir(dir).filePath(S("drop-dialog-empty.png"))));
        }
        {
            QWidget window;
            window.resize(960, 600);
            window.show();
            DropOverlay overlay(&window);
            overlay.showOverlay();
            QTest::qWait(80);
            QVERIFY(window.grab().save(QDir(dir).filePath(S("drop-overlay.png"))));
        }
    }
};

QTEST_MAIN(ZeroDropAddDialogTest)
#include "ZeroDropAddDialog_test.moc"
