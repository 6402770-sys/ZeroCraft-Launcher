// SPDX-License-Identifier: GPL-3.0-only
// The theme editor panel and the custom-theme file actions behind it (docs/zero/SHELL.md, "Theme editor"): the panel opens, Save is
// disabled for an empty name, the live preview is throttled, Cancel puts the previous look back, Save writes a folder and registers
// the id, a rename keeps the slug, Delete never removes anything it should not, a bad zip is reported. Offscreen, no Application:
// ThemeManager is replaced by hooks.
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QWidget>

#include "ui/zero/ZeroCustomFont.h"
#include "ui/zero/ZeroCustomTheme.h"
#include "ui/zero/ZeroPixelFont.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroToast.h"
#include "ui/zero/content/FilterPanel.h"
#include "ui/zero/pages/ThemeEditorPanel.h"
#include "ui/zero/pages/ThemeLibrary.h"
#include "zero/theme/ThemeArchive.h"
#include "zero/theme/ThemeFiles.h"

using namespace Zero;

Q_DECLARE_METATYPE(Zero::ThemeSpec)

namespace {

const QString kActiveId = QStringLiteral("zero-minecraft");

/// A real TrueType file whose family is "ZERO Other": the generated pixel font with its family renamed (same length, so every table
/// offset stays valid). Stands in for a font the user picked.
QByteArray otherFontFile()
{
    const auto utf16be = [](const QString& text) {
        QByteArray out;
        for (const QChar c : text) {
            out.append(char(c.unicode() >> 8));
            out.append(char(c.unicode() & 0xFF));
        }
        return out;
    };
    QByteArray data = pixelFontData();
    data.replace(utf16be(pixelFamily()), utf16be(QStringLiteral("ZERO Other")));
    return data;
}

struct Fixture {
    QTemporaryDir dir;
    QString root;
    QWidget window;
    ThemeLibrary* library = nullptr;
    ThemeEditorPanel* panel = nullptr;

    // what the hooks saw
    QStringList loadedFolders, removedIds, appliedIds, reappliedIds;
    int previews = 0;
    bool confirm = false;
    int confirmCalls = 0;

    Fixture()
    {
        root = dir.filePath(QStringLiteral("themes"));
        QDir().mkpath(root);
        ThemeSpec start = ThemeSpec::minecraft();
        start.reduceMotion = true;
        Theme::setCurrent(start);

        ThemeLibrary::Hooks hooks;
        hooks.themesRoot = root;
        hooks.loadTheme = [this](const QString& folder) {
            loadedFolders << folder;
            auto theme = ZeroCustomTheme::load(folder);
            return theme ? theme->id() : QString();
        };
        hooks.removeTheme = [this](const QString& id) { removedIds << id; };
        hooks.applyTheme = [this](const QString& id) { appliedIds << id; };
        hooks.reapplyTheme = [this](const QString& id) {
            reappliedIds << id;
            ThemeSpec spec = ThemeSpec::minecraft();
            spec.reduceMotion = true;
            Theme::setCurrent(spec);
        };
        hooks.activeThemeId = []() { return kActiveId; };
        hooks.previewSpec = [this](const ThemeSpec& spec) {
            ++previews;
            ThemeSpec s = spec;
            s.reduceMotion = true;
            Theme::setCurrent(s);
        };
        hooks.confirmDelete = [this](const QString&) {
            ++confirmCalls;
            return confirm;
        };
        library = new ThemeLibrary(std::move(hooks), &window, &window);
        panel = new ThemeEditorPanel(library, &window);
    }

    /// A saved theme made through the panel.
    QString saveNew(const QString& name)
    {
        panel->openNew();
        panel->nameEdit()->setText(name);
        emit panel->nameEdit()->textEdited(name);
        if (!panel->save()) {
            QTest::qFail("saving the theme failed", __FILE__, __LINE__);
            return {};
        }
        return appliedIds.last();
    }

    bool folderExists(const QString& slug) const { return QFileInfo::exists(root + QLatin1Char('/') + slug + QStringLiteral("/zero-theme.json")); }
    QStringList folders() const { return QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden); }
};

}  // namespace

class ZeroThemeEditorTest : public QObject {
    Q_OBJECT

   private slots:
    void cleanup()
    {
        CustomFont::load(QString());
        ThemeSpec spec = ThemeSpec::obsidian();
        spec.reduceMotion = true;
        Theme::setCurrent(spec);
    }

    void panelOpensOnANewTheme()
    {
        Fixture f;
        QVERIFY(!f.panel->isOpen());
        QVERIFY(f.panel->isHidden());
        QSignalSpy opened(f.panel, &ThemeEditorPanel::openChanged);
        f.panel->openNew();
        QVERIFY(f.panel->isOpen());
        QVERIFY(!f.panel->isHidden());
        QCOMPARE(opened.count(), 1);
        QCOMPARE(f.panel->nameEdit()->text(), QStringLiteral("My theme"));
        QVERIFY(f.panel->saveButton()->isEnabled());
        QVERIFY(f.panel->menuButton()->isHidden());  // a new theme has no Duplicate / Export / Delete yet
        QCOMPARE(f.panel->baseControl()->selected(), QStringLiteral("obsidian"));
        QCOMPARE(f.panel->backgroundControl()->selected(), QStringLiteral("none"));
        QCOMPARE(f.panel->backgroundControl()->count(), 3);  // None | Image | Video
        QCOMPARE(f.panel->cornersControl()->selected(), QStringLiteral("rounded"));
        QVERIFY(f.panel->imageBlock()->isHidden());  // the sliders belong to Image / Video
        QVERIFY(f.panel->allColorsBlock()->isHidden());
        QCOMPARE(f.panel->hexEdit(QStringLiteral("accent"))->text(), QStringLiteral("#D4D4D4"));
        QCOMPARE(f.panel->presetDots().size(), 5);
        QVERIFY(f.panel->width() >= ThemeEditorPanel::kMinWidth && f.panel->width() <= ThemeEditorPanel::kMaxWidth);
        // the whole app already shows the theme being made
        QCOMPARE(Theme::current().id, QString::fromLatin1(Custom::ThemeEditorModel::kNewThemeId));
        QVERIFY(f.previews >= 1);

        // the default name does not clash with an existing theme
        f.panel->cancel();
        f.saveNew(QStringLiteral("My theme"));
        f.panel->openNew();
        QCOMPARE(f.panel->nameEdit()->text(), QStringLiteral("My theme (2)"));
    }

    void previewedSignalFollowsTheLiveSpec()
    {
        Fixture f;
        QSignalSpy previewed(f.panel, &ThemeEditorPanel::previewed);
        f.panel->openNew();
        QCOMPARE(previewed.count(), 1);  // on open
        f.panel->model()->setColor(Custom::MainColor::Accent, QColor(0x40, 0x80, 0xE0));
        QTRY_COMPARE_WITH_TIMEOUT(previewed.count(), 2, 1000);  // when the throttle rings
        const ThemeSpec spec = previewed.last().first().value<ThemeSpec>();
        QCOMPARE(spec.accent.rgb(), QColor(0x40, 0x80, 0xE0).rgb());
        f.panel->model()->setCorners(Custom::CornerStyle::Square);
        f.panel->flushPreview();
        QCOMPARE(previewed.count(), 3);
        // the editing id is what the page uses for the selection ring: none for a new theme, the theme's own when editing
        QVERIFY(f.panel->editingThemeId().isEmpty());
        f.panel->cancel();
        const QString id = f.saveNew(QStringLiteral("Ring"));
        QVERIFY(f.panel->openEdit(id));
        QCOMPARE(f.panel->editingThemeId(), id);
        f.panel->cancel();
        QVERIFY(f.panel->editingThemeId().isEmpty());
    }

    void saveIsDisabledForAnEmptyName()
    {
        Fixture f;
        f.panel->openNew();
        QLineEdit* name = f.panel->nameEdit();
        name->selectAll();
        QTest::keyClick(name, Qt::Key_Delete);
        QVERIFY(name->text().isEmpty());
        QVERIFY(!f.panel->saveButton()->isEnabled());
        QVERIFY(!f.panel->save());
        QVERIFY(f.panel->isOpen());
        QVERIFY(f.folders().isEmpty());
        name->setText(QStringLiteral("   "));
        emit name->textEdited(QStringLiteral("   "));
        QVERIFY(!f.panel->saveButton()->isEnabled());
        name->setText(QStringLiteral("Real"));
        emit name->textEdited(QStringLiteral("Real"));
        QVERIFY(f.panel->saveButton()->isEnabled());
    }

    void changesPreviewLiveButThrottled()
    {
        Fixture f;
        f.panel->openNew();
        const int afterOpen = f.previews;
        auto* accent = f.panel->hexEdit(QStringLiteral("accent"));
        accent->selectAll();
        QTest::keyClicks(accent, QStringLiteral("#E0B04A"));
        QCOMPARE(f.panel->model()->color(Custom::MainColor::Accent).rgb(), QColor(0xE0, 0xB0, 0x4A).rgb());
        QVERIFY(f.panel->model()->touched(Custom::MainColor::Accent));
        QVERIFY(f.panel->livePreviewPending());
        // a burst of edits makes one apply
        auto* model = f.panel->model();
        for (int i = 0; i < 40; ++i)
            model->setDim(0.01 * i);
        QCOMPARE(f.previews, afterOpen);
        QTRY_VERIFY_WITH_TIMEOUT(f.previews > afterOpen, 1000);
        QCOMPARE(f.previews, afterOpen + 1);
        QCOMPARE(Theme::current().accent.rgb(), QColor(0xE0, 0xB0, 0x4A).rgb());
        // a name change does not touch the app at all
        const int before = f.previews;
        f.panel->nameEdit()->setText(QStringLiteral("Other"));
        emit f.panel->nameEdit()->textEdited(QStringLiteral("Other"));
        QTest::qWait(ThemeEditorPanel::kLiveMs * 2);
        QCOMPARE(f.previews, before);
        // flushPreview applies at once
        model->setColor(Custom::MainColor::Text, QColor(0xFA, 0xFA, 0xFA));
        f.panel->flushPreview();
        QCOMPARE(Theme::current().text.rgb(), QColor(0xFA, 0xFA, 0xFA).rgb());
        QVERIFY(!f.panel->livePreviewPending());
    }

    void presetsAndHexEntry()
    {
        Fixture f;
        f.panel->openNew();
        QAbstractButton* dot = f.panel->presetDots().at(1);
        dot->click();
        QCOMPARE(f.panel->model()->color(Custom::MainColor::Accent).rgb(), QColor::fromRgba(Tokens::accentPresets[1].rgb).rgb());
        QVERIFY(dot->isChecked());
        QVERIFY(!f.panel->presetDots().at(0)->isChecked());

        // invalid text reverts when the field loses focus; the color stays
        auto* bg = f.panel->hexEdit(QStringLiteral("bg"));
        const QString before = bg->text();
        bg->setFocus();
        bg->selectAll();
        QTest::keyClicks(bg, QStringLiteral("12"));
        bg->clearFocus();
        emit bg->editingFinished();
        QCOMPARE(bg->text(), before);
        // "RRGGBB" without the hash is fine
        bg->selectAll();
        QTest::keyClicks(bg, QStringLiteral("203040"));
        emit bg->editingFinished();
        QCOMPARE(f.panel->model()->color(Custom::MainColor::Background).rgb(), QColor(0x20, 0x30, 0x40).rgb());
        QCOMPARE(bg->text(), QStringLiteral("#203040"));
    }

    void readabilityLabelFollowsTheColors()
    {
        Fixture f;
        f.panel->openNew();
        QCOMPARE(f.panel->readabilityLabel()->text(), QStringLiteral("Readable"));
        QCOMPARE(f.panel->readabilityLabel()->property("zeroTone").toString(), QStringLiteral("success"));
        f.panel->model()->setColor(Custom::MainColor::Text, QColor(0x1C, 0x1C, 0x1C));
        QCOMPARE(f.panel->readabilityLabel()->text(), QStringLiteral("Hard to read"));
        QCOMPARE(f.panel->readabilityLabel()->property("zeroTone").toString(), QStringLiteral("warning"));
        QVERIFY(!f.panel->readabilityLabel()->toolTip().isEmpty());
    }

    void allColorsOverrideAndReset()
    {
        Fixture f;
        f.panel->openNew();
        auto* border = f.panel->hexEdit(QStringLiteral("border"), true);
        QVERIFY(border);
        QCOMPARE(Custom::overrideKeys().size(), 22);
        // accent, background and text have their rows above: "All colors" lists the other 19 tokens only
        QCOMPARE(f.panel->allColorsKeys().size(), 19);
        for (const char* main : { "accent", "bg", "text" }) {
            QVERIFY(!f.panel->allColorsKeys().contains(QLatin1String(main)));
            QVERIFY(!f.panel->hexEdit(QLatin1String(main), true));
            QVERIFY(f.panel->hexEdit(QLatin1String(main), false));
        }
        QVERIFY(f.panel->hexEdit(QStringLiteral("surface"), true));
        QVERIFY(f.panel->resetButton(QStringLiteral("border"))->isHidden());  // only overridden tokens offer Reset
        f.panel->setAllColorsExpanded(true);
        QVERIFY(f.panel->allColorsBlock()->isVisibleTo(f.panel));
        const QString derived = border->text();
        border->selectAll();
        QTest::keyClicks(border, QStringLiteral("#44AA44"));
        QVERIFY(f.panel->model()->isOverridden(QStringLiteral("border")));
        QVERIFY(!f.panel->resetButton(QStringLiteral("border"))->isHidden());
        // a main color change re-derives the rest and leaves the override
        f.panel->model()->setColor(Custom::MainColor::Background, QColor(0x10, 0x20, 0x40));
        QCOMPARE(border->text(), QStringLiteral("#44AA44"));
        QVERIFY(f.panel->hexEdit(QStringLiteral("elevated"), true)->text() != QStringLiteral("#222222"));
        f.panel->resetButton(QStringLiteral("border"))->click();
        QVERIFY(!f.panel->model()->isOverridden(QStringLiteral("border")));
        QVERIFY(f.panel->resetButton(QStringLiteral("border"))->isHidden());
        QVERIFY(border->text() != QStringLiteral("#44AA44"));
        Q_UNUSED(derived);
    }

    void baseControlRederives()
    {
        Fixture f;
        f.panel->openNew();
        f.panel->baseControl()->onActivated(QStringLiteral("minecraft"));
        QCOMPARE(f.panel->baseControl()->selected(), QStringLiteral("minecraft"));
        QCOMPARE(f.panel->cornersControl()->selected(), QStringLiteral("square"));
        QCOMPARE(f.panel->hexEdit(QStringLiteral("accent"))->text(), Custom::ThemeEditorModel::toHex(ThemeSpec::minecraft().accent));
        f.panel->flushPreview();
        QCOMPARE(Theme::current().styleId(), QStringLiteral("minecraft"));
    }

    void imageRevealsTheSliders()
    {
        Fixture f;
        const QString png = f.dir.filePath(QStringLiteral("sky.png"));
        QImage img(8, 8, QImage::Format_RGB32);
        img.fill(QColor(20, 60, 90));
        QVERIFY(img.save(png, "PNG"));
        f.panel->openNew();
        f.panel->model()->setImage(png);
        QCOMPARE(f.panel->backgroundControl()->selected(), QStringLiteral("image"));
        QVERIFY(f.panel->imageBlock()->isVisibleTo(f.panel));
        QCOMPARE(f.panel->dimSlider()->minimum(), 0);
        QCOMPARE(f.panel->dimSlider()->maximum(), 90);
        QCOMPARE(f.panel->cardsSlider()->minimum(), 40);
        QCOMPARE(f.panel->cardsSlider()->maximum(), 100);
        f.panel->dimSlider()->setValue(60);
        f.panel->cardsSlider()->setValue(70);
        QCOMPARE(f.panel->model()->dim(), 0.6);
        QCOMPARE(f.panel->model()->cardOpacity(), 0.7);
        f.panel->flushPreview();
        QCOMPARE(Theme::current().backgroundImage, QFileInfo(png).absoluteFilePath());  // previewed from the picked file
        f.panel->backgroundControl()->onActivated(QStringLiteral("none"));
        QVERIFY(f.panel->imageBlock()->isHidden());
        f.panel->flushPreview();
        QVERIFY(Theme::current().backgroundImage.isEmpty());
    }

    void cancelPutsThePreviousLookBack()
    {
        Fixture f;
        const ThemeSpec before = Theme::current();
        f.panel->openNew();
        f.panel->model()->setColor(Custom::MainColor::Accent, QColor(0xE0, 0x40, 0x40));
        f.panel->flushPreview();
        QVERIFY(Theme::current().id != before.id);
        QSignalSpy closed(f.panel, &ThemeEditorPanel::openChanged);
        f.panel->cancelButton()->click();
        QVERIFY(!f.panel->isOpen());
        QVERIFY(f.panel->isHidden());
        QCOMPARE(closed.count(), 1);
        QCOMPARE(f.reappliedIds, QStringList{ kActiveId });  // via the theme manager, even though that id was "already" active
        QCOMPARE(Theme::current().id, before.id);
        QCOMPARE(Theme::current().accent.rgb(), before.accent.rgb());
        QVERIFY(f.folders().isEmpty());  // nothing written
        QVERIFY(f.appliedIds.isEmpty());
        QVERIFY(!f.panel->livePreviewPending());
    }

    void escapeInThePanelCancels()
    {
        Fixture f;
        f.window.resize(900, 600);
        f.window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&f.window));
        f.window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&f.window));
        f.panel->openNew();
        QVERIFY(f.panel->isVisible());
        f.panel->nameEdit()->setFocus();
        QTest::keyClick(f.panel->nameEdit(), Qt::Key_Escape);
        QVERIFY(!f.panel->isOpen());
        QCOMPARE(f.reappliedIds, QStringList{ kActiveId });
        QVERIFY(f.folders().isEmpty());
    }

    void saveWritesAFolderAndRegistersTheId()
    {
        Fixture f;
        f.panel->openNew();
        f.panel->nameEdit()->setText(QStringLiteral("Sunset Glow"));
        emit f.panel->nameEdit()->textEdited(QStringLiteral("Sunset Glow"));
        f.panel->model()->setColor(Custom::MainColor::Accent, QColor(0xE0, 0x64, 0x5C));
        QSignalSpy saved(f.panel, &ThemeEditorPanel::saved);
        QSignalSpy changed(f.library, &ThemeLibrary::themesChanged);
        f.panel->saveButton()->click();

        QVERIFY(f.folderExists(QStringLiteral("sunset-glow")));
        QCOMPARE(f.loadedFolders.size(), 1);
        QCOMPARE(QFileInfo(f.loadedFolders.first()).fileName(), QStringLiteral("sunset-glow"));
        QCOMPARE(f.appliedIds, QStringList{ QStringLiteral("zero-custom-sunset-glow") });  // applied and persisted, like the Apply button
        QCOMPARE(saved.count(), 1);
        QCOMPARE(saved.first().first().toString(), QStringLiteral("zero-custom-sunset-glow"));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.first().first().toString(), QStringLiteral("zero-custom-sunset-glow"));
        QVERIFY(!f.panel->isOpen());
        QCOMPARE(ZeroToast::count(&f.window), 1);  // "Saved Sunset Glow"
        auto def = Custom::loadDefinition(f.root + QStringLiteral("/sunset-glow"));
        QVERIFY(def.has_value());
        QCOMPARE(def->name, QStringLiteral("Sunset Glow"));
        QCOMPARE(def->accent.rgb(), QColor(0xE0, 0x64, 0x5C).rgb());
        QVERIFY(f.reappliedIds.isEmpty());
    }

    void editRenameKeepsTheSlug()
    {
        Fixture f;
        const QString id = f.saveNew(QStringLiteral("Sunset"));
        QCOMPARE(id, QStringLiteral("zero-custom-sunset"));
        f.appliedIds.clear();
        QVERIFY(f.panel->openEdit(id));
        QCOMPARE(f.panel->editingThemeId(), id);
        QVERIFY(!f.panel->menuButton()->isHidden());  // an existing theme has the options menu
        QCOMPARE(f.panel->nameEdit()->text(), QStringLiteral("Sunset"));
        f.panel->nameEdit()->setText(QStringLiteral("Dusk"));
        emit f.panel->nameEdit()->textEdited(QStringLiteral("Dusk"));
        QVERIFY(f.panel->save());
        QCOMPARE(f.folders(), QStringList{ QStringLiteral("sunset") });
        QCOMPARE(f.appliedIds, QStringList{ id });
        QCOMPARE(Custom::loadDefinition(f.root + QStringLiteral("/sunset"))->name, QStringLiteral("Dusk"));

        QVERIFY(!f.panel->openEdit(QStringLiteral("zero-custom-missing")));
        QVERIFY(!f.panel->openEdit(QStringLiteral("zero-minecraft")));  // built-in themes are not editable
    }

    void saveFailureKeepsThePanelOpen()
    {
        Fixture f;
        f.panel->openNew();
        f.panel->nameEdit()->setText(QStringLiteral("Broken"));
        emit f.panel->nameEdit()->textEdited(QStringLiteral("Broken"));
        f.panel->model()->setImage(f.dir.filePath(QStringLiteral("not-there.png")));
        QVERIFY(!f.panel->save());
        QVERIFY(f.panel->isOpen());
        QVERIFY(f.folders().isEmpty());
        QVERIFY(f.appliedIds.isEmpty());
        QVERIFY(f.loadedFolders.isEmpty());
        QVERIFY(ZeroToast::count(&f.window) >= 1);  // the error
    }

    void deleteRefusalKeepsTheFolder()
    {
        Fixture f;
        const QString id = f.saveNew(QStringLiteral("Keep me"));
        QVERIFY(f.folderExists(QStringLiteral("keep-me")));

        f.confirm = false;  // the user says no
        QVERIFY(!f.library->deleteTheme(id));
        QCOMPARE(f.confirmCalls, 1);
        QVERIFY(f.folderExists(QStringLiteral("keep-me")));
        QVERIFY(f.removedIds.isEmpty());

        // the user says yes but the folder cannot be moved to the Recycle Bin (it is not a theme folder any more): refused
        QVERIFY(QFile::remove(f.root + QStringLiteral("/keep-me/zero-theme.json")));
        f.confirm = true;
        QSignalSpy deleted(f.library, &ThemeLibrary::themeDeleted);
        QVERIFY(!f.library->deleteTheme(id));
        QCOMPARE(f.confirmCalls, 2);
        QVERIFY(QFileInfo(f.root + QStringLiteral("/keep-me")).isDir());
        QVERIFY(f.removedIds.isEmpty());  // nothing unregistered
        QCOMPARE(deleted.count(), 0);

        // not a custom theme id: nothing to ask, nothing to do
        QVERIFY(!f.library->deleteTheme(QStringLiteral("zero-minecraft")));
        QCOMPARE(f.confirmCalls, 2);
    }

    void importOfABadZipShowsAnError()
    {
        Fixture f;
        const QString bad = f.dir.filePath(QStringLiteral("bad.zip"));
        QFile file(bad);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("this is not a zip archive");
        file.close();
        QSignalSpy changed(f.library, &ThemeLibrary::themesChanged);
        QVERIFY(!f.library->importZip(bad));
        QCOMPARE(changed.count(), 0);
        QVERIFY(f.loadedFolders.isEmpty());
        QVERIFY(f.folders().isEmpty());
        QCOMPARE(ZeroToast::count(&f.window), 1);
        QVERIFY(!f.library->importZip(f.dir.filePath(QStringLiteral("missing.zip"))));
        QVERIFY(f.folders().isEmpty());
    }

    void exportAndImportRoundTrip()
    {
        Fixture f;
        const QString id = f.saveNew(QStringLiteral("Travel"));
        const QString zip = f.dir.filePath(QStringLiteral("travel.zip"));
        QVERIFY(f.library->exportTheme(id, zip));
        QVERIFY(QFileInfo::exists(zip));
        QSignalSpy changed(f.library, &ThemeLibrary::themesChanged);
        QVERIFY(f.library->importZip(zip));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.first().first().toString(), QStringLiteral("zero-custom-travel-2"));  // a new folder, never an overwrite
        QCOMPARE(Custom::loadDefinition(f.root + QStringLiteral("/travel-2"))->name, QStringLiteral("Travel (2)"));
        QVERIFY(f.appliedIds.size() == 1);  // importing selects, it does not apply
    }

    void duplicateSelectsTheCopy()
    {
        Fixture f;
        const QString id = f.saveNew(QStringLiteral("Base"));
        f.appliedIds.clear();
        QSignalSpy changed(f.library, &ThemeLibrary::themesChanged);
        QVERIFY(f.library->duplicateTheme(id));
        QCOMPARE(changed.first().first().toString(), QStringLiteral("zero-custom-base-copy"));
        QVERIFY(f.folderExists(QStringLiteral("base-copy")));
        QVERIFY(f.appliedIds.isEmpty());  // selected, not applied
        QVERIFY(!f.library->duplicateTheme(QStringLiteral("zero-obsidian")));
    }

    void deletingTheThemeBeingEditedClosesThePanel()
    {
        Fixture f;
        const QString id = f.saveNew(QStringLiteral("Doomed"));
        QVERIFY(f.panel->openEdit(id));
        emit f.library->themeDeleted(id);  // (what a successful Recycle Bin move announces)
        QVERIFY(!f.panel->isOpen());
        QCOMPARE(f.reappliedIds.size(), 1);  // the persisted theme is back on screen
    }

    void reopeningWhileOpenKeepsTheOriginalLook()
    {
        Fixture f;
        const QString id = f.saveNew(QStringLiteral("One"));
        f.panel->openNew();
        f.panel->model()->setColor(Custom::MainColor::Accent, QColor(0x40, 0x80, 0xE0));
        QVERIFY(f.panel->openEdit(id));  // switching to another theme in the same open session
        f.panel->cancel();
        QCOMPARE(f.reappliedIds, QStringList{ kActiveId });
    }

    // ------------------------------------------------------------------ the color dialog (owner QA)

    void colorDialogPreviewsInThePanelAndAppliesOnceOnOk()
    {
        Fixture f;
        f.panel->openNew();
        f.panel->flushPreview();
        const int baseline = f.previews;
        const QColor before = f.panel->model()->color(Custom::MainColor::Accent);

        QColorDialog* dialog = f.panel->pickColor(QStringLiteral("accent"));
        QVERIFY(dialog);
        QVERIFY(dialog->isVisible());
        // a drag in the dialog: many colors in a row
        for (int i = 0; i < 12; ++i)
            dialog->setCurrentColor(QColor::fromHsv(i * 25, 200, 220));
        const QColor last = dialog->currentColor();
        QTest::qWait(ThemeEditorPanel::kLiveMs * 3);  // longer than the live throttle: a re-apply would have happened by now

        // nothing re-applied the style sheet (the live preview hook is the only way the app is restyled) and the theme is untouched...
        QCOMPARE(f.previews, baseline);
        QVERIFY(!f.panel->livePreviewPending());
        QCOMPARE(f.panel->model()->color(Custom::MainColor::Accent), before);
        // ...but the row shows the color being picked
        QCOMPARE(f.panel->hexEdit(QStringLiteral("accent"))->text(), Custom::ThemeEditorModel::toHex(last));

        dialog->accept();  // OK: the theme is updated, once
        QCOMPARE(f.panel->model()->color(Custom::MainColor::Accent).rgb(), last.rgb());
        QVERIFY(f.panel->livePreviewPending());
        QTRY_VERIFY(!f.panel->livePreviewPending());
        QCOMPARE(f.previews, baseline + 1);
        QCOMPARE(Theme::current().accent.rgb(), last.rgb());
        QCOMPARE(f.panel->hexEdit(QStringLiteral("accent"))->text(), Custom::ThemeEditorModel::toHex(last));
    }

    void colorDialogCancelLeavesTheThemeAndTheRowAlone()
    {
        Fixture f;
        f.panel->openNew();
        f.panel->flushPreview();
        const int baseline = f.previews;
        const QColor before = f.panel->model()->color(Custom::MainColor::Background);
        const QString beforeHex = f.panel->hexEdit(QStringLiteral("bg"))->text();

        QColorDialog* dialog = f.panel->pickColor(QStringLiteral("bg"));
        dialog->setCurrentColor(QColor(200, 30, 30));
        QCOMPARE(f.panel->hexEdit(QStringLiteral("bg"))->text(), Custom::ThemeEditorModel::toHex(dialog->currentColor()));
        QCOMPARE(dialog->currentColor().rgb(), QColor(200, 30, 30).rgb());
        dialog->reject();
        QCOMPARE(f.panel->hexEdit(QStringLiteral("bg"))->text(), beforeHex);  // the row is back
        QCOMPARE(f.panel->model()->color(Custom::MainColor::Background), before);
        QVERIFY(!f.panel->model()->touched(Custom::MainColor::Background));
        QTest::qWait(ThemeEditorPanel::kLiveMs * 3);
        QCOMPARE(f.previews, baseline);  // not even the Cancel re-applied anything
    }

    void colorDialogWorksForAnyTokenAndFlushesAPendingPreviewFirst()
    {
        Fixture f;
        f.panel->openNew();
        f.panel->setAllColorsExpanded(true);
        f.panel->model()->setColor(Custom::MainColor::Text, QColor(250, 250, 240));  // schedules a live apply
        QVERIFY(f.panel->livePreviewPending());
        const int before = f.previews;
        QColorDialog* dialog = f.panel->pickColor(QStringLiteral("danger"));
        QVERIFY(!f.panel->livePreviewPending());  // flushed now, so none lands while the dialog is open
        QCOMPARE(f.previews, before + 1);
        dialog->setCurrentColor(QColor(10, 160, 40));
        QCOMPARE(f.panel->hexEdit(QStringLiteral("danger"), true)->text(), Custom::ThemeEditorModel::toHex(dialog->currentColor()));
        QVERIFY(!f.panel->model()->isOverridden(QStringLiteral("danger")));
        const QColor picked = dialog->currentColor();
        dialog->accept();  // deleted on close: not used again
        QVERIFY(f.panel->model()->isOverridden(QStringLiteral("danger")));
        QCOMPARE(f.panel->model()->tokenColor(QStringLiteral("danger")).rgb(), picked.rgb());
    }

    // ------------------------------------------------------------------ launcher icon

    void appIconSectionPreviewsAndSaves()
    {
        Fixture f;
        const QString png = f.dir.filePath(QStringLiteral("mark.png"));
        QImage img(48, 48, QImage::Format_ARGB32);
        img.fill(QColor(20, 180, 90));
        QVERIFY(img.save(png, "PNG"));

        f.panel->openNew();
        QCOMPARE(f.panel->iconControl()->selected(), QStringLiteral("default"));
        QCOMPARE(f.panel->iconControl()->count(), 2);
        QVERIFY(f.panel->iconBlock()->isHidden());
        QVERIFY(f.panel->iconControl()->toolTip().contains(QStringLiteral("The desktop shortcut keeps the ZERO icon.")));

        f.panel->model()->setIcon(png);
        QCOMPARE(f.panel->iconControl()->selected(), QStringLiteral("custom"));
        QVERIFY(f.panel->iconBlock()->isVisibleTo(f.panel));
        QVERIFY(!f.panel->iconPreview()->pixmap().isNull());
        QCOMPARE(f.panel->iconPreview()->width(), 32);  // a 32 px preview
        QCOMPARE(f.panel->iconPreview()->height(), 32);
        QVERIFY(f.panel->iconPreview()->toolTip().contains(QStringLiteral("taskbar")));
        f.panel->flushPreview();
        QCOMPARE(Theme::current().appIcon, QFileInfo(png).absoluteFilePath());  // the app shows it live, before saving

        // Default hides it again and the app goes back to the mark; Custom brings the remembered file back
        f.panel->iconControl()->onActivated(QStringLiteral("default"));
        QVERIFY(f.panel->iconBlock()->isHidden());
        f.panel->flushPreview();
        QVERIFY(Theme::current().appIcon.isEmpty());
        f.panel->iconControl()->onActivated(QStringLiteral("custom"));
        QVERIFY(f.panel->iconBlock()->isVisibleTo(f.panel));
        f.panel->flushPreview();
        QCOMPARE(Theme::current().appIcon, QFileInfo(png).absoluteFilePath());

        // Cancel restores the previous look, the icon included
        f.panel->cancel();
        QVERIFY(Theme::current().appIcon.isEmpty());
        QVERIFY(f.folders().isEmpty());  // nothing written

        // Save copies it into the theme folder
        const QString id = f.saveNew(QStringLiteral("Marked"));
        QVERIFY(!id.isEmpty());
        QVERIFY(f.panel->openEdit(id));
        QCOMPARE(f.panel->iconControl()->selected(), QStringLiteral("default"));  // nothing was picked in the first session
        f.panel->model()->setIcon(png);
        QVERIFY(f.panel->save());
        const QString folder = f.root + QStringLiteral("/marked");
        QVERIFY(QFileInfo(folder + QStringLiteral("/icon.png")).isFile());
        QVERIFY(f.panel->openEdit(id));
        QCOMPARE(f.panel->iconControl()->selected(), QStringLiteral("custom"));
        QCOMPARE(f.panel->iconBlock()->isVisibleTo(f.panel), true);
        QVERIFY(!f.panel->iconPreview()->pixmap().isNull());
        f.panel->cancel();
    }

    // ------------------------------------------------------------------ font

    void fontSectionShowsTheFontAndSaves()
    {
        Fixture f;
        const QString ttf = f.dir.filePath(QStringLiteral("Other.ttf"));
        {
            QFile file(ttf);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.write(otherFontFile()) > 0);
        }
        f.panel->openNew();
        QCOMPARE(f.panel->fontControl()->selected(), QStringLiteral("default"));
        QVERIFY(f.panel->fontBlock()->isHidden());
        QCOMPARE(f.panel->fontPreview()->text(), QStringLiteral("The quick brown fox jumps over the lazy dog"));

        f.panel->model()->setFont(ttf);
        QCOMPARE(f.panel->fontControl()->selected(), QStringLiteral("custom"));
        QVERIFY(f.panel->fontBlock()->isVisibleTo(f.panel));
        // the preview line is drawn in that font
        QCOMPARE(f.panel->fontPreview()->font().families().value(0), QStringLiteral("ZERO Other"));
        f.panel->flushPreview();
        QCOMPARE(Theme::current().fontFile, QFileInfo(ttf).absoluteFilePath());

        f.panel->fontControl()->onActivated(QStringLiteral("default"));
        QVERIFY(f.panel->fontBlock()->isHidden());
        QVERIFY(f.panel->fontPreview()->font().families().value(0) != QStringLiteral("ZERO Other"));
        f.panel->fontControl()->onActivated(QStringLiteral("custom"));  // remembered
        QCOMPARE(f.panel->fontPreview()->font().families().value(0), QStringLiteral("ZERO Other"));

        f.panel->nameEdit()->setText(QStringLiteral("Lettered"));
        emit f.panel->nameEdit()->textEdited(QStringLiteral("Lettered"));
        QVERIFY(f.panel->save());
        QVERIFY(QFileInfo(f.root + QStringLiteral("/lettered/font.ttf")).isFile());
        const auto def = Custom::loadDefinition(f.root + QStringLiteral("/lettered"));
        QVERIFY(def.has_value());
        QCOMPARE(def->fontFile, QStringLiteral("font.ttf"));
        CustomFont::load(QString());
    }

    // ------------------------------------------------------------------ video sound

    void soundToggleAndVolumeBelongToTheVideoRow()
    {
        Fixture f;
        const QString clip = f.dir.filePath(QStringLiteral("clip.mp4"));
        {
            QFile file(clip);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QByteArray(64, 'v'));
        }
        const QString png = f.dir.filePath(QStringLiteral("sky.png"));
        QImage img(8, 8, QImage::Format_RGB32);
        img.fill(QColor(20, 60, 90));
        QVERIFY(img.save(png, "PNG"));

        f.panel->openNew();
        QVERIFY(f.panel->soundBlock()->isHidden());  // none
        f.panel->model()->setImage(png);
        QVERIFY(f.panel->soundBlock()->isHidden());  // an image has no sound

        f.panel->model()->setVideo(clip);
        QVERIFY(f.panel->soundBlock()->isVisibleTo(f.panel));
        QVERIFY(!f.panel->soundCheck()->isChecked());  // default off
        QVERIFY(f.panel->volumeSlider()->isHidden() || !f.panel->volumeSlider()->isVisibleTo(f.panel));  // the volume shows only while on
        QCOMPARE(f.panel->volumeSlider()->minimum(), 0);
        QCOMPARE(f.panel->volumeSlider()->maximum(), 100);
        QCOMPARE(f.panel->volumeSlider()->value(), 50);  // default 50 %

        f.panel->soundCheck()->setChecked(true);
        QVERIFY(f.panel->model()->sound());
        QVERIFY(f.panel->volumeSlider()->isVisibleTo(f.panel));
        f.panel->volumeSlider()->setValue(80);
        QCOMPARE(f.panel->model()->volume(), 0.8);
        f.panel->flushPreview();
        QVERIFY(Theme::current().videoSound);  // live: the app's video gets it before saving
        QCOMPARE(Theme::current().videoVolume, 0.8);
        f.panel->soundCheck()->setChecked(false);
        f.panel->flushPreview();
        QVERIFY(!Theme::current().videoSound);
        f.panel->soundCheck()->setChecked(true);

        // saved in the JSON, shown again when the theme is edited
        f.panel->nameEdit()->setText(QStringLiteral("Loud video"));
        emit f.panel->nameEdit()->textEdited(QStringLiteral("Loud video"));
        QVERIFY(f.panel->save());
        const auto def = Custom::loadDefinition(f.root + QStringLiteral("/loud-video"));
        QVERIFY(def.has_value());
        QVERIFY(def->sound);
        QCOMPARE(def->volume, 0.8);
        QVERIFY(f.panel->openEdit(QStringLiteral("zero-custom-loud-video")));
        QVERIFY(f.panel->soundCheck()->isChecked());
        QCOMPARE(f.panel->volumeSlider()->value(), 80);
        f.panel->cancel();
    }

    void panelWidthFollowsThePage()
    {
        Fixture f;
        f.panel->fitTo(400);
        QCOMPARE(f.panel->width(), ThemeEditorPanel::kMinWidth);
        f.panel->fitTo(1000);
        QCOMPARE(f.panel->width(), 280);
        f.panel->fitTo(3000);
        QCOMPARE(f.panel->width(), ThemeEditorPanel::kMaxWidth);
    }
};

QTEST_MAIN(ZeroThemeEditorTest)
#include "ZeroThemeEditor_test.moc"
