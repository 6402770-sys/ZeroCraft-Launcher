// SPDX-License-Identifier: GPL-3.0-only
// The theme editor's state and disk work (no widgets): base switching rules, main colors and touched flags, token overrides and
// Reset, readability, the background choice (None | Image | Video), the live spec, and ThemeFiles (save new / edit / replace the
// image or video, Image <-> Video cleanup, the video size cap, duplicate, nothing half-written on failure), the launcher icon, the
// font and the video sound (choice, stash, JSON, save / replace / remove / rollback, duplicate, source validation).
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "zero/theme/ThemeEditorModel.h"
#include "zero/theme/ThemeArchive.h"
#include "zero/theme/ThemeFiles.h"

using namespace Zero;
using namespace Zero::Custom;

namespace {

bool writePng(const QString& path, const QColor& color = QColor(30, 90, 60))
{
    QImage img(8, 8, QImage::Format_RGB32);
    img.fill(color);
    return img.save(path, "PNG");
}

/// A launcher icon needs at least 32 px on both sides.
bool writeIconPng(const QString& path, const QColor& color = QColor(30, 90, 60))
{
    QImage img(40, 40, QImage::Format_RGB32);
    img.fill(color);
    return img.save(path, "PNG");
}

bool writeJpg(const QString& path)
{
    QImage img(8, 8, QImage::Format_RGB32);
    img.fill(QColor(200, 120, 40));
    return img.save(path, "JPG");
}

bool writeBytes(const QString& path, int size)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    return f.write(QByteArray(size, 'v')) == size;
}

QStringList files(const QString& folder)
{
    return QDir(folder).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
}

}  // namespace

class ThemeEditorModelTest : public QObject {
    Q_OBJECT

   private slots:
    void newThemeStartsFromObsidian()
    {
        ThemeEditorModel m;
        m.beginNew(QStringLiteral("My theme"));
        QVERIFY(m.isNew());
        QCOMPARE(m.name(), QStringLiteral("My theme"));
        QVERIFY(m.base() == Base::Obsidian);
        QVERIFY(m.corners() == CornerStyle::Rounded);
        const ThemeSpec base = ThemeSpec::obsidian();
        QCOMPARE(m.color(MainColor::Accent).rgb(), base.accent.rgb());
        QCOMPARE(m.color(MainColor::Background).rgb(), base.bg.rgb());
        QCOMPARE(m.color(MainColor::Text).rgb(), base.text.rgb());
        for (MainColor c : { MainColor::Accent, MainColor::Background, MainColor::Text })
            QVERIFY(!m.touched(c));
        QVERIFY(!m.isOverridden(QStringLiteral("surface")));
        QCOMPARE(m.spec().id, QString::fromLatin1(ThemeEditorModel::kNewThemeId));
        QVERIFY(m.readability().level == Readability::Readable);
        QVERIFY(m.backgroundChoice() == ThemeEditorModel::BackgroundChoice::None);
        QVERIFY(!m.hasImage());
    }

    void nameValidity()
    {
        ThemeEditorModel m;
        m.beginNew(QStringLiteral("x"));
        QVERIFY(m.nameIsValid());
        m.setName(QStringLiteral("   "));
        QVERIFY(!m.nameIsValid());
        m.setName(QString());
        QVERIFY(!m.nameIsValid());
        m.setName(QStringLiteral("  Night   Owl "));
        QVERIFY(m.nameIsValid());
        QCOMPARE(m.definition().name, QStringLiteral("Night Owl"));  // saved simplified
    }

    void baseSwitchTakesBaseColorsAndCorners()
    {
        ThemeEditorModel m;
        m.beginNew(QStringLiteral("t"));
        m.setBase(Base::Minecraft);
        const ThemeSpec mc = ThemeSpec::minecraft();
        QCOMPARE(m.color(MainColor::Accent).rgb(), mc.accent.rgb());
        QCOMPARE(m.color(MainColor::Background).rgb(), mc.bg.rgb());
        QCOMPARE(m.color(MainColor::Text).rgb(), mc.text.rgb());
        QVERIFY(m.corners() == CornerStyle::Square);
        // the base's overlay / pixel touches follow the base
        QCOMPARE(m.spec().styleId(), QStringLiteral("minecraft"));

        // the user picks Rounded on the Minecraft base; switching the base resets the corners to the base's default
        m.setCorners(CornerStyle::Rounded);
        m.setBase(Base::Obsidian);
        QVERIFY(m.corners() == CornerStyle::Rounded);
        m.setBase(Base::Minecraft);
        QVERIFY(m.corners() == CornerStyle::Square);
    }

    void touchedColorsSurviveABaseSwitch()
    {
        ThemeEditorModel m;
        m.beginNew(QStringLiteral("t"));
        const QColor mine(0x12, 0x80, 0xE0);
        m.setColor(MainColor::Accent, mine);
        QVERIFY(m.touched(MainColor::Accent));
        QVERIFY(!m.touched(MainColor::Background));
        m.setBase(Base::Minecraft);
        QCOMPARE(m.color(MainColor::Accent).rgb(), mine.rgb());                                  // kept
        QCOMPARE(m.color(MainColor::Background).rgb(), ThemeSpec::minecraft().bg.rgb());        // taken from the new base
        m.setBase(Base::Obsidian);
        QCOMPARE(m.color(MainColor::Accent).rgb(), mine.rgb());
        QCOMPARE(m.color(MainColor::Text).rgb(), ThemeSpec::obsidian().text.rgb());
    }

    void editLoadsTouchedFromTheDefinition()
    {
        Definition d = Definition::fromBase(Base::Minecraft, QStringLiteral("Mine"));
        d.accent = QColor(0xE0, 0x40, 0x40);  // chosen; background and text equal the base's: not chosen
        ThemeEditorModel m;
        m.beginEdit(d, QStringLiteral("C:/themes/mine"), QStringLiteral("zero-custom-mine"));
        QVERIFY(!m.isNew());
        QCOMPARE(m.themeId(), QStringLiteral("zero-custom-mine"));
        QVERIFY(m.touched(MainColor::Accent));
        QVERIFY(!m.touched(MainColor::Background));
        QVERIFY(!m.touched(MainColor::Text));
        QVERIFY(!m.isOverridden(QStringLiteral("surface")));  // a surface equal to the base's is the default
        m.setBase(Base::Obsidian);
        QCOMPARE(m.color(MainColor::Accent).rgb(), QColor(0xE0, 0x40, 0x40).rgb());
        QCOMPARE(m.color(MainColor::Background).rgb(), ThemeSpec::obsidian().bg.rgb());
    }

    void overridesSetAndReset()
    {
        ThemeEditorModel m;
        m.beginNew(QStringLiteral("t"));
        const QColor derived = m.tokenColor(QStringLiteral("border"));
        QVERIFY(!m.isOverridden(QStringLiteral("border")));
        const QColor mine(0x40, 0xA0, 0x40);
        m.setTokenColor(QStringLiteral("border"), mine);
        QVERIFY(m.isOverridden(QStringLiteral("border")));
        QCOMPARE(m.tokenColor(QStringLiteral("border")).rgb(), mine.rgb());
        QCOMPARE(m.spec().border.rgb(), mine.rgb());
        QCOMPARE(m.definition().overrides.value(QStringLiteral("border")).rgb(), mine.rgb());

        // a main color change re-derives every token that is not overridden, and leaves the override alone
        const QColor elevatedBefore = m.tokenColor(QStringLiteral("elevated"));
        m.setColor(MainColor::Background, QColor(0x10, 0x20, 0x40));
        QVERIFY(m.tokenColor(QStringLiteral("elevated")).rgb() != elevatedBefore.rgb());
        QCOMPARE(m.tokenColor(QStringLiteral("border")).rgb(), mine.rgb());

        m.resetToken(QStringLiteral("border"));
        QVERIFY(!m.isOverridden(QStringLiteral("border")));
        QVERIFY(m.tokenColor(QStringLiteral("border")).rgb() != mine.rgb());
        QVERIFY(!m.definition().overrides.contains(QStringLiteral("border")));
        // back on the original background the derived border is the original one
        m.setColor(MainColor::Background, ThemeSpec::obsidian().bg);
        QCOMPARE(m.tokenColor(QStringLiteral("border")).rgb(), derived.rgb());
    }

    void surfaceOverrideAndMainTokensGoThroughTheMainColors()
    {
        ThemeEditorModel m;
        m.beginNew(QStringLiteral("t"));
        const QColor surface(0x30, 0x20, 0x20);
        m.setTokenColor(QStringLiteral("surface"), surface);
        QVERIFY(m.isOverridden(QStringLiteral("surface")));
        QCOMPARE(m.definition().surface.rgb(), surface.rgb());
        m.resetToken(QStringLiteral("surface"));
        QVERIFY(!m.isOverridden(QStringLiteral("surface")));
        QVERIFY(!m.definition().surface.isValid());

        // accent / bg / text edited under "All colors" are the main colors: touched, never an override
        m.setTokenColor(QStringLiteral("accent"), QColor(0xE0, 0xB0, 0x30));
        QVERIFY(m.touched(MainColor::Accent));
        QVERIFY(!m.isOverridden(QStringLiteral("accent")));
        QVERIFY(m.definition().overrides.isEmpty());
        QVERIFY(!m.definition().surface.isValid());
    }

    void everyOverrideKeyHasALabelAndAColor()
    {
        ThemeEditorModel m;
        m.beginNew(QStringLiteral("t"));
        const ThemeSpec spec = m.spec();
        for (const QString& key : overrideKeys()) {
            QVERIFY2(ThemeEditorModel::specColor(spec, key).isValid(), qPrintable(key));
            QVERIFY2(!ThemeEditorModel::tokenLabel(key).isEmpty(), qPrintable(key));
        }
        QCOMPARE(ThemeEditorModel::tokenLabel(QStringLiteral("surfaceHover")), QStringLiteral("Surface hover"));
        QCOMPARE(ThemeEditorModel::tokenLabel(QStringLiteral("textSecondary")), QStringLiteral("Text secondary"));
        QVERIFY(!ThemeEditorModel::specColor(spec, QStringLiteral("nope")).isValid());
    }

    void readabilityFollowsTheColors()
    {
        ThemeEditorModel m;
        m.beginNew(QStringLiteral("t"));
        QVERIFY(m.readability().level == Readability::Readable);
        QVERIFY(m.readability().summary.isEmpty());
        m.setColor(MainColor::Text, QColor(0x1C, 0x1C, 0x1C));  // almost the Obsidian background
        auto r = m.readability();
        QVERIFY(r.level == Readability::HardToRead);
        QVERIFY(!r.textOnBackground);
        QVERIFY(!r.summary.isEmpty());
        m.setColor(MainColor::Text, ThemeSpec::obsidian().text);
        QVERIFY(m.readability().level == Readability::Readable);
        // the accent label: onAccent is derived for contrast, so only a hand-set override can break it
        m.setTokenColor(QStringLiteral("onAccent"), m.tokenColor(QStringLiteral("accent")));
        QVERIFY(!m.readability().accentLabel);
        QVERIFY(m.readability().level == Readability::HardToRead);
    }

    void hexParsing()
    {
        QCOMPARE(ThemeEditorModel::parseHex(QStringLiteral("#1A2B3C")).rgb(), QColor(0x1A, 0x2B, 0x3C).rgb());
        QCOMPARE(ThemeEditorModel::parseHex(QStringLiteral("1a2b3c")).rgb(), QColor(0x1A, 0x2B, 0x3C).rgb());
        QCOMPARE(ThemeEditorModel::parseHex(QStringLiteral("  #FFFFFF ")).rgb(), QColor(Qt::white).rgb());
        for (const char* bad : { "", "#", "#12345", "#1234567", "#GGGGGG", "red", "#12 456", "##123456", "#FF1A2B3C" })
            QVERIFY2(!ThemeEditorModel::parseHex(QString::fromLatin1(bad)).isValid(), bad);
        QCOMPARE(ThemeEditorModel::toHex(QColor(0x1A, 0x2B, 0x3C)), QStringLiteral("#1A2B3C"));
    }

    void signals_()
    {
        ThemeEditorModel m;
        m.beginNew(QStringLiteral("t"));
        QSignalSpy changed(&m, &ThemeEditorModel::changed);
        QSignalSpy look(&m, &ThemeEditorModel::lookChanged);
        m.setName(QStringLiteral("other"));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(look.count(), 0);  // a name does not change how the app looks
        m.setName(QStringLiteral("other"));
        QCOMPARE(changed.count(), 1);  // same value: nothing
        m.setColor(MainColor::Accent, QColor(0x12, 0x34, 0x56));
        QCOMPARE(changed.count(), 2);
        QCOMPARE(look.count(), 1);
        m.setColor(MainColor::Accent, QColor(0x12, 0x34, 0x56));
        QCOMPARE(look.count(), 1);
        m.setCorners(CornerStyle::Square);
        m.setDim(0.5);
        QCOMPARE(look.count(), 3);
    }

    void imageBackground()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString png = dir.filePath(QStringLiteral("Sky.PNG"));
        QVERIFY(writePng(png));

        ThemeEditorModel m;
        m.beginNew(QStringLiteral("t"));
        QVERIFY(!m.selectImageBackground());  // nothing to bring back: the panel asks for a file
        m.setCardOpacity(0.6);
        m.setDim(0.5);
        QVERIFY(m.spec().backgroundImage.isEmpty());
        QCOMPARE(m.definition().cardOpacity, 1.0);  // no image: cards stay opaque

        m.setImage(png);
        QVERIFY(m.backgroundChoice() == ThemeEditorModel::BackgroundChoice::Image);
        QVERIFY(m.hasImage());
        QCOMPARE(m.imageName(), QStringLiteral("Sky.PNG"));
        QCOMPARE(m.pendingImage(), QFileInfo(png).absoluteFilePath());
        QCOMPARE(m.definition().backgroundFile, QStringLiteral("background.png"));
        QVERIFY(m.definition().backgroundKind == BackgroundKind::Image);
        QCOMPARE(m.definition().cardOpacity, 0.6);
        const ThemeSpec s = m.spec();
        QCOMPARE(s.backgroundImage, QFileInfo(png).absoluteFilePath());  // previewed from where it was picked
        QCOMPARE(s.backgroundDim, 0.5);
        QCOMPARE(s.cardOpacity, 0.6);

        m.selectNoBackground();
        QVERIFY(m.backgroundChoice() == ThemeEditorModel::BackgroundChoice::None);
        QVERIFY(m.spec().backgroundImage.isEmpty());
        QCOMPARE(m.definition().cardOpacity, 1.0);
        QVERIFY(m.definition().backgroundFile.isEmpty());
        QVERIFY(m.selectImageBackground());  // the picked image comes back
        QCOMPARE(m.imageName(), QStringLiteral("Sky.PNG"));
        QCOMPARE(m.definition().cardOpacity, 0.6);

        m.setCardOpacity(0.01);
        QCOMPARE(m.cardOpacity(), kMinCardOpacity);
        m.setDim(5.0);
        QCOMPARE(m.dim(), 1.0);
    }

    void videoBackground()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString clip = dir.filePath(QStringLiteral("Loop.MP4"));
        QVERIFY(writeBytes(clip, 64));
        const QString png = dir.filePath(QStringLiteral("Sky.png"));
        QVERIFY(writePng(png));

        ThemeEditorModel m;
        m.beginNew(QStringLiteral("t"));
        QVERIFY(!m.selectVideoBackground());  // nothing to bring back: the panel asks for a file
        m.setCardOpacity(0.6);
        m.setDim(0.5);

        m.setVideo(clip);
        QVERIFY(m.backgroundChoice() == ThemeEditorModel::BackgroundChoice::Video);
        QVERIFY(m.hasVideo());
        QVERIFY(!m.hasImage());
        QCOMPARE(m.videoName(), QStringLiteral("Loop.MP4"));
        QCOMPARE(m.pendingVideo(), QFileInfo(clip).absoluteFilePath());
        QVERIFY(m.pendingImage().isEmpty());
        QCOMPARE(m.pendingBackground(), QFileInfo(clip).absoluteFilePath());
        QVERIFY(m.definition().backgroundKind == BackgroundKind::Video);
        QCOMPARE(m.definition().backgroundFile, QStringLiteral("background.mp4"));
        QCOMPARE(m.definition().cardOpacity, 0.6);
        const ThemeSpec s = m.spec();
        QCOMPARE(s.backgroundVideo, QFileInfo(clip).absoluteFilePath());  // the live preview plays the source file
        QVERIFY(s.backgroundImage.isEmpty());
        QCOMPARE(s.backgroundDim, 0.5);

        // Image <-> Video: each remembers its file
        m.setImage(png);
        QVERIFY(m.backgroundChoice() == ThemeEditorModel::BackgroundChoice::Image);
        QVERIFY(m.spec().backgroundVideo.isEmpty());
        QCOMPARE(m.spec().backgroundImage, QFileInfo(png).absoluteFilePath());
        QVERIFY(m.selectVideoBackground());
        QCOMPARE(m.videoName(), QStringLiteral("Loop.MP4"));
        QCOMPARE(m.spec().backgroundVideo, QFileInfo(clip).absoluteFilePath());
        QVERIFY(m.selectImageBackground());
        QCOMPARE(m.imageName(), QStringLiteral("Sky.png"));

        m.selectNoBackground();
        QVERIFY(m.spec().backgroundVideo.isEmpty() && m.spec().backgroundImage.isEmpty());
        QCOMPARE(m.definition().cardOpacity, 1.0);
        QVERIFY(m.selectVideoBackground());  // from None too
        QCOMPARE(m.videoName(), QStringLiteral("Loop.MP4"));
    }

    void storedVideoRoundTrips()
    {
        Definition d = Definition::fromBase(Base::Obsidian, QStringLiteral("Clip"));
        d.backgroundKind = BackgroundKind::Video;
        d.backgroundFile = QStringLiteral("background.mp4");
        d.dim = 0.5;
        d.cardOpacity = 0.7;
        ThemeEditorModel m;
        m.beginEdit(d, QStringLiteral("C:/themes/clip"), QStringLiteral("zero-custom-clip"));
        QVERIFY(m.backgroundChoice() == ThemeEditorModel::BackgroundChoice::Video);
        QVERIFY(m.hasVideo());
        QCOMPARE(m.videoName(), QStringLiteral("background.mp4"));
        QVERIFY(!m.hasImage());
        QVERIFY(m.pendingBackground().isEmpty());

        // unrelated edits leave the stored video alone
        m.setName(QStringLiteral("Clip 2"));
        m.setCorners(CornerStyle::Square);
        m.setColor(MainColor::Accent, QColor(0xE0, 0x40, 0x40));
        m.setBase(Base::Minecraft);
        Definition out = m.definition();
        QVERIFY(out.backgroundKind == BackgroundKind::Video);
        QCOMPARE(out.backgroundFile, QStringLiteral("background.mp4"));
        QCOMPARE(out.dim, 0.5);
        QCOMPARE(out.cardOpacity, 0.7);

        // choosing None drops it, choosing Video again brings it back
        m.selectNoBackground();
        out = m.definition();
        QVERIFY(out.backgroundKind == BackgroundKind::None);
        QVERIFY(out.backgroundFile.isEmpty());
        QCOMPARE(out.cardOpacity, 1.0);
        QVERIFY(!m.selectImageBackground());  // a video is not an image to bring back
        QVERIFY(m.selectVideoBackground());
        QCOMPARE(m.definition().backgroundFile, QStringLiteral("background.mp4"));
    }

    void definitionRoundTripsThroughJson()
    {
        ThemeEditorModel m;
        m.beginNew(QStringLiteral("Round trip"));
        m.setBase(Base::Minecraft);
        m.setColor(MainColor::Accent, QColor(0x12, 0x80, 0xE0));
        m.setTokenColor(QStringLiteral("danger"), QColor(0xFF, 0x00, 0x00));
        m.setTokenColor(QStringLiteral("surface"), QColor(0x22, 0x22, 0x28));
        m.setCorners(CornerStyle::Rounded);
        auto parsed = parseDefinition(serializeDefinition(m.definition()));
        QVERIFY(parsed.has_value());
        ThemeEditorModel again;
        again.beginEdit(*parsed, QString(), QStringLiteral("zero-custom-x"));
        QCOMPARE(again.name(), QStringLiteral("Round trip"));
        QVERIFY(again.base() == Base::Minecraft);
        QVERIFY(again.corners() == CornerStyle::Rounded);
        QCOMPARE(again.color(MainColor::Accent).rgb(), QColor(0x12, 0x80, 0xE0).rgb());
        QVERIFY(again.touched(MainColor::Accent));
        QVERIFY(again.isOverridden(QStringLiteral("danger")));
        QVERIFY(again.isOverridden(QStringLiteral("surface")));
        QCOMPARE(again.spec().danger.rgb(), QColor(0xFF, 0x00, 0x00).rgb());
    }

    // ---- ThemeFiles -------------------------------------------------------------------------------------------------

    void saveNewThemeWithAnImage()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.filePath(QStringLiteral("themes"));
        const QString png = dir.filePath(QStringLiteral("pic.png"));
        QVERIFY(writePng(png));

        ThemeEditorModel m;
        m.beginNew(QStringLiteral("Forest Night"));
        m.setImage(png);
        auto saved = saveTheme(root, QString(), m.definition(), m.pendingImage());
        QVERIFY2(saved.has_value(), qPrintable(saved ? QString() : saved.error()));
        QCOMPARE(saved->id, QStringLiteral("zero-custom-forest-night"));
        QCOMPARE(QFileInfo(saved->folder).fileName(), QStringLiteral("forest-night"));
        QCOMPARE(files(saved->folder), (QStringList{ QStringLiteral("background.png"), QStringLiteral("zero-theme.json") }));
        auto def = loadDefinition(saved->folder);
        QVERIFY(def.has_value());
        QVERIFY(def->backgroundKind == BackgroundKind::Image);
        QCOMPARE(def->backgroundFile, QStringLiteral("background.png"));
        // the spec of the saved folder now finds the image next to the JSON
        QVERIFY(!deriveSpec(*def, saved->folder, saved->id).backgroundImage.isEmpty());

        // a second theme with the same name gets its own folder
        auto second = saveTheme(root, QString(), m.definition());
        QVERIFY(second.has_value());
        QCOMPARE(QFileInfo(second->folder).fileName(), QStringLiteral("forest-night-2"));
    }

    void saveEditKeepsTheSlugAndReplacesTheImage()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.filePath(QStringLiteral("themes"));
        const QString png = dir.filePath(QStringLiteral("a.png"));
        const QString png2 = dir.filePath(QStringLiteral("b.png"));
        const QString jpg = dir.filePath(QStringLiteral("c.jpg"));
        QVERIFY(writePng(png, QColor(10, 20, 30)));
        QVERIFY(writePng(png2, QColor(200, 100, 50)));
        QVERIFY(writeJpg(jpg));

        ThemeEditorModel m;
        m.beginNew(QStringLiteral("Sunset"));
        m.setImage(png);
        auto saved = saveTheme(root, QString(), m.definition(), m.pendingImage());
        QVERIFY(saved.has_value());
        const QByteArray original = [&] {
            QFile f(saved->folder + QStringLiteral("/background.png"));
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }();

        // rename: same folder, same id
        auto loaded = loadDefinition(saved->folder);
        QVERIFY(loaded.has_value());
        ThemeEditorModel edit;
        edit.beginEdit(*loaded, saved->folder, saved->id);
        edit.setName(QStringLiteral("Dusk"));
        auto renamed = saveTheme(root, edit.folder(), edit.definition(), edit.pendingImage());
        QVERIFY(renamed.has_value());
        QCOMPARE(renamed->folder, saved->folder);
        QCOMPARE(renamed->id, saved->id);
        QCOMPARE(loadDefinition(saved->folder)->name, QStringLiteral("Dusk"));
        QVERIFY(!QDir(root).exists(QStringLiteral("dusk")));
        QCOMPARE(files(saved->folder), (QStringList{ QStringLiteral("background.png"), QStringLiteral("zero-theme.json") }));

        // replaced by another png: same file name, new bytes, no leftovers
        edit.setImage(png2);
        QVERIFY(saveTheme(root, edit.folder(), edit.definition(), edit.pendingImage()).has_value());
        QCOMPARE(files(saved->folder), (QStringList{ QStringLiteral("background.png"), QStringLiteral("zero-theme.json") }));
        QFile now(saved->folder + QStringLiteral("/background.png"));
        QVERIFY(now.open(QIODevice::ReadOnly));
        QVERIFY(now.readAll() != original);
        now.close();

        // replaced by a jpg: the png is removed after the JSON is written
        loaded = loadDefinition(saved->folder);
        edit.beginEdit(*loaded, saved->folder, saved->id);
        edit.setImage(jpg);
        QVERIFY(saveTheme(root, edit.folder(), edit.definition(), edit.pendingImage()).has_value());
        QCOMPARE(files(saved->folder), (QStringList{ QStringLiteral("background.jpg"), QStringLiteral("zero-theme.json") }));
        QCOMPARE(loadDefinition(saved->folder)->backgroundFile, QStringLiteral("background.jpg"));

        // None: the file goes, the JSON says none, cards are opaque again
        loaded = loadDefinition(saved->folder);
        edit.beginEdit(*loaded, saved->folder, saved->id);
        edit.selectNoBackground();
        QVERIFY(saveTheme(root, edit.folder(), edit.definition(), edit.pendingImage()).has_value());
        QCOMPARE(files(saved->folder), (QStringList{ QStringLiteral("zero-theme.json") }));
        QVERIFY(loadDefinition(saved->folder)->backgroundKind == BackgroundKind::None);
    }

    void storedVideoFileIsLeftAloneOnSave()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.filePath(QStringLiteral("themes"));
        const QString folder = root + QStringLiteral("/clip");
        Definition d = Definition::fromBase(Base::Obsidian, QStringLiteral("Clip"));
        d.backgroundKind = BackgroundKind::Video;
        d.backgroundFile = QStringLiteral("background.mp4");
        QVERIFY(saveDefinition(folder, d).has_value());
        QFile video(folder + QStringLiteral("/background.mp4"));
        QVERIFY(video.open(QIODevice::WriteOnly));
        video.write("not really a video");
        video.close();

        auto loaded = loadDefinition(folder);
        ThemeEditorModel m;
        m.beginEdit(*loaded, folder, QStringLiteral("zero-custom-clip"));
        m.setName(QStringLiteral("Clip renamed"));
        QVERIFY(saveTheme(root, folder, m.definition(), m.pendingImage()).has_value());
        QVERIFY(QFileInfo::exists(folder + QStringLiteral("/background.mp4")));
        QVERIFY(loadDefinition(folder)->backgroundKind == BackgroundKind::Video);

        // picking None explicitly removes it
        loaded = loadDefinition(folder);
        m.beginEdit(*loaded, folder, QStringLiteral("zero-custom-clip"));
        m.selectNoBackground();
        QVERIFY(saveTheme(root, folder, m.definition(), m.pendingImage()).has_value());
        QVERIFY(!QFileInfo::exists(folder + QStringLiteral("/background.mp4")));
    }

    void failedSaveLeavesNothingBehind()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.filePath(QStringLiteral("themes"));
        ThemeEditorModel m;
        m.beginNew(QStringLiteral("Broken"));

        // an image that is not there
        auto missing = saveTheme(root, QString(), m.definition(), dir.filePath(QStringLiteral("nope.png")));
        QVERIFY(!missing.has_value());
        QVERIFY(!missing.error().isEmpty());
        QVERIFY(!QDir(root).exists(QStringLiteral("broken")));

        // a file that is not a usable picture type
        const QString txt = dir.filePath(QStringLiteral("a.txt"));
        QFile f(txt);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.close();
        QVERIFY(!saveTheme(root, QString(), m.definition(), txt).has_value());
        QVERIFY(!QDir(root).exists(QStringLiteral("broken")));

        // an edit that fails keeps the old image
        const QString png = dir.filePath(QStringLiteral("a.png"));
        QVERIFY(writePng(png));
        m.setImage(png);
        auto saved = saveTheme(root, QString(), m.definition(), m.pendingImage());
        QVERIFY(saved.has_value());
        auto loaded = loadDefinition(saved->folder);
        ThemeEditorModel edit;
        edit.beginEdit(*loaded, saved->folder, saved->id);
        QVERIFY(!saveTheme(root, edit.folder(), edit.definition(), dir.filePath(QStringLiteral("gone.png"))).has_value());
        QCOMPARE(files(saved->folder), (QStringList{ QStringLiteral("background.png"), QStringLiteral("zero-theme.json") }));
    }

    void saveVideoThenSwitchBetweenImageAndVideo()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.filePath(QStringLiteral("themes"));
        const QString mp4 = dir.filePath(QStringLiteral("a.mp4"));
        const QString webm = dir.filePath(QStringLiteral("b.WEBM"));
        const QString png = dir.filePath(QStringLiteral("a.png"));
        QVERIFY(writeBytes(mp4, 100));
        QVERIFY(writeBytes(webm, 120));
        QVERIFY(writePng(png));

        ThemeEditorModel m;
        m.beginNew(QStringLiteral("Clip"));
        m.setVideo(mp4);
        auto saved = saveTheme(root, QString(), m.definition(), m.pendingBackground());
        QVERIFY2(saved.has_value(), qPrintable(saved ? QString() : saved.error()));
        QCOMPARE(files(saved->folder), (QStringList{ QStringLiteral("background.mp4"), QStringLiteral("zero-theme.json") }));
        QVERIFY(loadDefinition(saved->folder)->backgroundKind == BackgroundKind::Video);
        QCOMPARE(QFileInfo(QDir(saved->folder).filePath(QStringLiteral("background.mp4"))).size(), qint64(100));

        // Video -> Video with another extension: the old file goes after the JSON is written
        ThemeEditorModel edit;
        edit.beginEdit(*loadDefinition(saved->folder), saved->folder, saved->id);
        edit.setVideo(webm);
        QVERIFY(saveTheme(root, edit.folder(), edit.definition(), edit.pendingBackground()).has_value());
        QCOMPARE(files(saved->folder), (QStringList{ QStringLiteral("background.webm"), QStringLiteral("zero-theme.json") }));

        // Video -> Image
        edit.beginEdit(*loadDefinition(saved->folder), saved->folder, saved->id);
        edit.setImage(png);
        QVERIFY(saveTheme(root, edit.folder(), edit.definition(), edit.pendingBackground()).has_value());
        QCOMPARE(files(saved->folder), (QStringList{ QStringLiteral("background.png"), QStringLiteral("zero-theme.json") }));
        QVERIFY(loadDefinition(saved->folder)->backgroundKind == BackgroundKind::Image);

        // Image -> Video, then the same name replaced in place (mp4 over mp4 keeps exactly one file)
        edit.beginEdit(*loadDefinition(saved->folder), saved->folder, saved->id);
        edit.setVideo(mp4);
        QVERIFY(saveTheme(root, edit.folder(), edit.definition(), edit.pendingBackground()).has_value());
        QCOMPARE(files(saved->folder), (QStringList{ QStringLiteral("background.mp4"), QStringLiteral("zero-theme.json") }));
        edit.beginEdit(*loadDefinition(saved->folder), saved->folder, saved->id);
        edit.setVideo(mp4);
        QVERIFY(saveTheme(root, edit.folder(), edit.definition(), edit.pendingBackground()).has_value());
        QCOMPARE(files(saved->folder), (QStringList{ QStringLiteral("background.mp4"), QStringLiteral("zero-theme.json") }));

        // Video -> None
        edit.beginEdit(*loadDefinition(saved->folder), saved->folder, saved->id);
        edit.selectNoBackground();
        QVERIFY(saveTheme(root, edit.folder(), edit.definition(), edit.pendingBackground()).has_value());
        QCOMPARE(files(saved->folder), (QStringList{ QStringLiteral("zero-theme.json") }));
    }

    void videoSaveRefusesWrongExtensionAndOversize()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.filePath(QStringLiteral("themes"));
        const QString avi = dir.filePath(QStringLiteral("a.avi"));
        const QString mp4 = dir.filePath(QStringLiteral("a.mp4"));
        QVERIFY(writeBytes(avi, 10));
        QVERIFY(writeBytes(mp4, 2048));
        ThemeEditorModel m;
        m.beginNew(QStringLiteral("Big"));

        QVERIFY(!saveTheme(root, QString(), m.definition(), avi).has_value());  // not in either extension list
        QVERIFY(!QDir(root).exists(QStringLiteral("big")));

        SaveLimits tiny;
        tiny.maxVideoBytes = 1024;
        auto big = saveTheme(root, QString(), m.definition(), mp4, tiny);
        QVERIFY(!big.has_value());
        QVERIFY(!big.error().isEmpty());
        QVERIFY(!QDir(root).exists(QStringLiteral("big")));  // a new theme leaves nothing behind
        tiny.maxVideoBytes = 2048;
        QVERIFY(saveTheme(root, QString(), m.definition(), mp4, tiny).has_value());  // exactly the cap is fine

        // an edit refused for size keeps the old file
        const QString png = dir.filePath(QStringLiteral("a.png"));
        QVERIFY(writePng(png));
        ThemeEditorModel img;
        img.beginNew(QStringLiteral("Keep"));
        img.setImage(png);
        auto saved = saveTheme(root, QString(), img.definition(), img.pendingBackground());
        QVERIFY(saved.has_value());
        ThemeEditorModel edit;
        edit.beginEdit(*loadDefinition(saved->folder), saved->folder, saved->id);
        edit.setVideo(mp4);
        tiny.maxVideoBytes = 100;
        QVERIFY(!saveTheme(root, edit.folder(), edit.definition(), edit.pendingBackground(), tiny).has_value());
        QCOMPARE(files(saved->folder), (QStringList{ QStringLiteral("background.png"), QStringLiteral("zero-theme.json") }));
        QVERIFY(loadDefinition(saved->folder)->backgroundKind == BackgroundKind::Image);
    }

    void backgroundExtensionsAreOnePerKind()
    {
        QCOMPARE(videoExtension(QStringLiteral("x/Clip.MOV")), QStringLiteral("mov"));
        QVERIFY(videoExtension(QStringLiteral("x.png")).isEmpty());
        QVERIFY(imageExtension(QStringLiteral("x.mp4")).isEmpty());
        for (const QString& e : videoExtensions())
            QVERIFY(!imageExtensions().contains(e));
        const QStringList all = backgroundExtensions();
        QCOMPARE(all.size(), imageExtensions().size() + videoExtensions().size());
        for (const QString& e : imageExtensions() + videoExtensions())
            QVERIFY(all.contains(e));
    }

    void duplicateCopiesDefinitionAndBackground()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.filePath(QStringLiteral("themes"));
        const QString png = dir.filePath(QStringLiteral("a.png"));
        QVERIFY(writePng(png));
        ThemeEditorModel m;
        m.beginNew(QStringLiteral("Original"));
        m.setColor(MainColor::Accent, QColor(0x12, 0x80, 0xE0));
        m.setImage(png);
        auto saved = saveTheme(root, QString(), m.definition(), m.pendingImage());
        QVERIFY(saved.has_value());

        auto copy = duplicateTheme(root, saved->folder);
        QVERIFY2(copy.has_value(), qPrintable(copy ? QString() : copy.error()));
        QCOMPARE(copy->id, QStringLiteral("zero-custom-original-copy"));
        QCOMPARE(loadDefinition(copy->folder)->name, QStringLiteral("Original copy"));
        QCOMPARE(loadDefinition(copy->folder)->accent.rgb(), QColor(0x12, 0x80, 0xE0).rgb());
        QCOMPARE(files(copy->folder), (QStringList{ QStringLiteral("background.png"), QStringLiteral("zero-theme.json") }));
        QCOMPARE(loadDefinition(saved->folder)->name, QStringLiteral("Original"));  // the source is untouched

        auto again = duplicateTheme(root, saved->folder);
        QVERIFY(again.has_value());
        QCOMPARE(loadDefinition(again->folder)->name, QStringLiteral("Original copy (2)"));

        QVERIFY(!duplicateTheme(root, dir.filePath(QStringLiteral("nothing-here"))).has_value());
    }

    // ------------------------------------------------------------------ launcher icon, font, video sound
    void soundAndVolumeLiveInTheModel()
    {
        ThemeEditorModel m;
        m.beginNew(QStringLiteral("Loud"));
        QVERIFY(!m.sound());  // default off
        QCOMPARE(m.volume(), kDefaultVolume);
        QSignalSpy looks(&m, &ThemeEditorModel::lookChanged);
        m.setSound(true);
        m.setVolume(0.8);
        QCOMPARE(looks.count(), 2);
        m.setSound(true);
        m.setVolume(0.8);
        QCOMPARE(looks.count(), 2);  // no change, no signal
        m.setVolume(4.0);
        QCOMPARE(m.volume(), 1.0);
        m.setVolume(-1.0);
        QCOMPARE(m.volume(), 0.0);

        // it only reaches the spec (and the file) with a video
        m.setSound(true);
        m.setVolume(0.6);
        QVERIFY(!m.spec().videoSound);
        QTemporaryDir dir;
        const QString clip = dir.filePath(QStringLiteral("clip.mp4"));
        QVERIFY(writeBytes(clip, 64));
        m.setVideo(clip);
        QVERIFY(m.spec().videoSound);
        QCOMPARE(m.spec().videoVolume, 0.6);
        QVERIFY(m.definition().sound);
        m.setSound(false);
        QVERIFY(!m.spec().videoSound);
        QVERIFY(writePng(dir.filePath(QStringLiteral("p.png"))));
        m.setImage(dir.filePath(QStringLiteral("p.png")));
        m.setSound(true);
        QVERIFY(!m.spec().videoSound);
    }

    void iconChoiceStashesAndRestores()
    {
        QTemporaryDir dir;
        const QString png = dir.filePath(QStringLiteral("mark.png"));
        QVERIFY(writePng(png, QColor(10, 200, 90)));
        ThemeEditorModel m;
        m.beginNew(QStringLiteral("Icons"));
        QVERIFY(m.iconChoice() == ThemeEditorModel::AssetChoice::Default);
        QVERIFY(m.iconName().isEmpty());
        QVERIFY(m.iconPath().isEmpty());
        QVERIFY(!m.selectCustomIcon());  // nothing to bring back: the panel asks for a file

        m.setIcon(png);
        QVERIFY(m.iconChoice() == ThemeEditorModel::AssetChoice::Custom);
        QCOMPARE(m.iconName(), QStringLiteral("mark.png"));
        QCOMPARE(m.pendingIcon(), QFileInfo(png).absoluteFilePath());
        QCOMPARE(m.iconPath(), QFileInfo(png).absoluteFilePath());
        QCOMPARE(m.definition().iconFile, QStringLiteral("icon.png"));
        QCOMPARE(m.spec().appIcon, QFileInfo(png).absoluteFilePath());  // the live preview shows the picked file

        m.selectDefaultIcon();
        QVERIFY(m.iconChoice() == ThemeEditorModel::AssetChoice::Default);
        QVERIFY(m.definition().iconFile.isEmpty());
        QVERIFY(m.pendingIcon().isEmpty());
        QVERIFY(m.spec().appIcon.isEmpty());
        QVERIFY(m.selectCustomIcon());  // remembered
        QCOMPARE(m.iconName(), QStringLiteral("mark.png"));
        QCOMPARE(m.spec().appIcon, QFileInfo(png).absoluteFilePath());

        // a new pick replaces; the extension is lower-cased in the stored name
        const QString big = dir.filePath(QStringLiteral("Big.PNG"));
        QVERIFY(writePng(big));
        m.setIcon(big);
        QCOMPARE(m.definition().iconFile, QStringLiteral("icon.png"));
        QCOMPARE(m.iconName(), QStringLiteral("Big.PNG"));

        // a new theme starts with the default icon again
        m.beginNew(QStringLiteral("Again"));
        QVERIFY(m.iconChoice() == ThemeEditorModel::AssetChoice::Default);
        QVERIFY(m.pendingIcon().isEmpty());
    }

    void fontChoiceMirrorsTheIconChoice()
    {
        QTemporaryDir dir;
        const QString ttf = dir.filePath(QStringLiteral("Face.TTF"));
        QVERIFY(writeBytes(ttf, 128));
        ThemeEditorModel m;
        m.beginNew(QStringLiteral("Fonts"));
        QVERIFY(m.fontChoice() == ThemeEditorModel::AssetChoice::Default);
        QVERIFY(!m.selectCustomFont());
        m.setFont(ttf);
        QVERIFY(m.fontChoice() == ThemeEditorModel::AssetChoice::Custom);
        QCOMPARE(m.fontName(), QStringLiteral("Face.TTF"));
        QCOMPARE(m.definition().fontFile, QStringLiteral("font.ttf"));
        QCOMPARE(m.pendingFont(), QFileInfo(ttf).absoluteFilePath());
        QCOMPARE(m.spec().fontFile, QFileInfo(ttf).absoluteFilePath());
        m.selectDefaultFont();
        QVERIFY(m.spec().fontFile.isEmpty());
        QVERIFY(m.definition().fontFile.isEmpty());
        QVERIFY(m.selectCustomFont());
        QCOMPARE(m.spec().fontFile, QFileInfo(ttf).absoluteFilePath());
    }

    void saveStoresIconAndFontAndRoundTrips()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath(QStringLiteral("themes"));
        const QString png = dir.filePath(QStringLiteral("mark.png"));
        const QString ttf = dir.filePath(QStringLiteral("face.ttf"));
        QVERIFY(writeIconPng(png));
        QVERIFY(writeBytes(ttf, 200));

        ThemeEditorModel m;
        m.beginNew(QStringLiteral("Marked"));
        m.setIcon(png);
        m.setFont(ttf);
        auto saved = saveTheme(root, QString(), m.definition(), QString(), {}, { m.pendingIcon(), m.pendingFont() });
        QVERIFY2(saved.has_value(), qPrintable(saved ? QString() : saved.error()));
        QCOMPARE(files(saved->folder), (QStringList{ "font.ttf", "icon.png", "zero-theme.json" }));  // no temp or backup files
        const auto def = loadDefinition(saved->folder);
        QVERIFY(def.has_value());
        QCOMPARE(def->iconFile, QStringLiteral("icon.png"));
        QCOMPARE(def->fontFile, QStringLiteral("font.ttf"));
        const ThemeSpec spec = deriveSpec(*def, saved->folder, saved->id);
        QCOMPARE(spec.appIcon, QDir(saved->folder).absoluteFilePath("icon.png"));
        QCOMPARE(spec.fontFile, QDir(saved->folder).absoluteFilePath("font.ttf"));

        // edit: the stored files show without a pick
        ThemeEditorModel edit;
        edit.beginEdit(*def, saved->folder, saved->id);
        QVERIFY(edit.hasIcon() && edit.hasFont());
        QCOMPARE(edit.iconPath(), QDir(saved->folder).absoluteFilePath("icon.png"));
        QCOMPARE(edit.fontPath(), QDir(saved->folder).absoluteFilePath("font.ttf"));
        QVERIFY(edit.pendingIcon().isEmpty());

        // save again without picking: still there, byte for byte
        QVERIFY(saveTheme(root, edit.folder(), edit.definition(), QString(), {}, { edit.pendingIcon(), edit.pendingFont() }).has_value());
        QCOMPARE(files(saved->folder), (QStringList{ "font.ttf", "icon.png", "zero-theme.json" }));

        // replace the icon with an svg: the png goes away once the JSON is saved
        const QString svg = dir.filePath(QStringLiteral("mark.svg"));
        QFile f(svg);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("<svg xmlns='http://www.w3.org/2000/svg' width='64' height='64'><rect width='64' height='64' fill='red'/></svg>");
        f.close();
        if (QImageReader::supportedImageFormats().contains("svg")) {
            edit.setIcon(svg);
            QVERIFY(saveTheme(root, edit.folder(), edit.definition(), QString(), {}, { edit.pendingIcon(), edit.pendingFont() }).has_value());
            QCOMPARE(files(saved->folder), (QStringList{ "font.ttf", "icon.svg", "zero-theme.json" }));
        }

        // choosing Default removes the icon and the font from the folder
        ThemeEditorModel plain;
        plain.beginEdit(loadDefinition(saved->folder).value(), saved->folder, saved->id);
        plain.selectDefaultIcon();
        plain.selectDefaultFont();
        QVERIFY(saveTheme(root, plain.folder(), plain.definition(), QString(), {}, { plain.pendingIcon(), plain.pendingFont() }).has_value());
        QCOMPARE(files(saved->folder), (QStringList{ "zero-theme.json" }));
        QVERIFY(loadDefinition(saved->folder)->iconFile.isEmpty());
        QVERIFY(loadDefinition(saved->folder)->fontFile.isEmpty());
    }

    void failedIconOrFontSaveRollsBackEverything()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath(QStringLiteral("themes"));
        const QString png = dir.filePath(QStringLiteral("mark.png"));
        const QString ttf = dir.filePath(QStringLiteral("face.ttf"));
        const QString bg = dir.filePath(QStringLiteral("sky.png"));
        QVERIFY(writeIconPng(png));
        QVERIFY(writeBytes(ttf, 200));
        QVERIFY(writePng(bg));

        // a new theme: a bad font after a good icon leaves no folder at all
        ThemeEditorModel m;
        m.beginNew(QStringLiteral("Doomed"));
        m.setIcon(png);
        auto bad = saveTheme(root, QString(), m.definition(), QString(), {}, { png, dir.filePath(QStringLiteral("nothing.ttf")) });
        QVERIFY(!bad.has_value());
        QVERIFY(!bad.error().isEmpty());
        QVERIFY(QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty());

        // an existing theme keeps its icon, its font and its background when a later part of the save fails
        ThemeEditorModel good;
        good.beginNew(QStringLiteral("Keeper"));
        good.setIcon(png);
        good.setFont(ttf);
        good.setImage(bg);
        auto saved = saveTheme(root, QString(), good.definition(), good.pendingBackground(), {}, { good.pendingIcon(), good.pendingFont() });
        QVERIFY(saved.has_value());
        QFile icon(saved->folder + "/icon.png");
        QVERIFY(icon.open(QIODevice::ReadOnly));
        const QByteArray iconBytes = icon.readAll();
        icon.close();
        const QStringList before = files(saved->folder);

        ThemeEditorModel edit;
        edit.beginEdit(loadDefinition(saved->folder).value(), saved->folder, saved->id);
        QImage other(40, 40, QImage::Format_RGB32);
        other.fill(Qt::red);
        const QString red = dir.filePath(QStringLiteral("red.png"));
        QVERIFY(other.save(red, "PNG"));
        edit.setIcon(red);
        auto failing = saveTheme(root, edit.folder(), edit.definition(), QString(), {}, { edit.pendingIcon(), dir.filePath(QStringLiteral("gone.otf")) });
        QVERIFY(!failing.has_value());
        QCOMPARE(files(saved->folder), before);  // no temp / backup files
        QVERIFY(icon.open(QIODevice::ReadOnly));
        QCOMPARE(icon.readAll(), iconBytes);  // the old icon is back, not the red one
        QCOMPARE(loadDefinition(saved->folder)->iconFile, QStringLiteral("icon.png"));
        QVERIFY(QFileInfo(saved->folder + "/font.ttf").isFile());
        QVERIFY(QFileInfo(saved->folder + "/background.png").isFile());

        // wrong extension / oversize caps, and a tiny icon
        auto wrongIcon = saveTheme(root, saved->folder, loadDefinition(saved->folder).value(), QString(), {}, { dir.filePath(QStringLiteral("a.gif")), QString() });
        QVERIFY(!wrongIcon.has_value());
        SaveLimits tiny;
        tiny.maxIconBytes = 10;
        tiny.maxFontBytes = 10;
        QVERIFY(!saveTheme(root, saved->folder, loadDefinition(saved->folder).value(), QString(), tiny, { png, QString() }).has_value());
        QVERIFY(!saveTheme(root, saved->folder, loadDefinition(saved->folder).value(), QString(), tiny, { QString(), ttf }).has_value());
        QCOMPARE(files(saved->folder), before);
    }

    void duplicateCopiesIconAndFont()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath(QStringLiteral("themes"));
        const QString png = dir.filePath(QStringLiteral("mark.png"));
        const QString ttf = dir.filePath(QStringLiteral("face.otf"));
        QVERIFY(writeIconPng(png));
        QVERIFY(writeBytes(ttf, 99));
        ThemeEditorModel m;
        m.beginNew(QStringLiteral("Original"));
        m.setIcon(png);
        m.setFont(ttf);
        m.setSound(true);
        auto saved = saveTheme(root, QString(), m.definition(), QString(), {}, { m.pendingIcon(), m.pendingFont() });
        QVERIFY(saved.has_value());
        auto copy = duplicateTheme(root, saved->folder);
        QVERIFY2(copy.has_value(), qPrintable(copy ? QString() : copy.error()));
        QCOMPARE(files(copy->folder), (QStringList{ "font.otf", "icon.png", "zero-theme.json" }));
        const auto def = loadDefinition(copy->folder);
        QCOMPARE(def->iconFile, QStringLiteral("icon.png"));
        QCOMPARE(def->fontFile, QStringLiteral("font.otf"));
        QFile font(copy->folder + "/font.otf");
        QVERIFY(font.open(QIODevice::ReadOnly));
        QCOMPARE(font.size(), qint64(99));
    }

    void iconSourceValidation()
    {
        QTemporaryDir dir;
        const auto check = [&](const QString& path, qint64 cap = kMaxIconBytes) { return validateIconSource(path, cap); };

        const QString ok = dir.filePath(QStringLiteral("ok.png"));
        QImage big(32, 32, QImage::Format_RGB32);
        big.fill(Qt::blue);
        QVERIFY(big.save(ok, "PNG"));
        QVERIFY(check(ok).has_value());  // exactly 32 px is fine

        const QString tiny = dir.filePath(QStringLiteral("tiny.png"));
        QImage small(31, 64, QImage::Format_RGB32);
        small.fill(Qt::blue);
        QVERIFY(small.save(tiny, "PNG"));
        const auto tooSmall = check(tiny);
        QVERIFY(!tooSmall.has_value());
        QVERIFY(tooSmall.error().contains(QStringLiteral("32")));  // says why

        QVERIFY(!check(dir.filePath(QStringLiteral("missing.png"))).has_value());
        QVERIFY(!check(dir.filePath(QStringLiteral("a.gif"))).has_value());  // wrong extension
        QVERIFY(!check(dir.filePath(QStringLiteral("a.jpg"))).has_value());
        const QString notAnImage = dir.filePath(QStringLiteral("fake.png"));
        QVERIFY(writeBytes(notAnImage, 100));
        QVERIFY(!check(notAnImage).has_value());  // a png by name only
        QVERIFY(!check(ok, 10).has_value());      // over the size cap
        QVERIFY(check(ok, QFileInfo(ok).size()).has_value());  // exactly the cap

        const QString ico = dir.filePath(QStringLiteral("big.ico"));
        QImage icoImage(48, 48, QImage::Format_ARGB32);
        icoImage.fill(Qt::green);
        if (icoImage.save(ico, "ICO")) {
            QVERIFY(check(ico).has_value());
            const QString smallIco = dir.filePath(QStringLiteral("small.ico"));
            QImage s16(16, 16, QImage::Format_ARGB32);
            s16.fill(Qt::green);
            QVERIFY(s16.save(smallIco, "ICO"));
            QVERIFY(!check(smallIco).has_value());
        }

        const QString svg = dir.filePath(QStringLiteral("v.svg"));
        QFile f(svg);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("<svg xmlns='http://www.w3.org/2000/svg' width='8' height='8'><rect width='8' height='8'/></svg>");
        f.close();
        if (QImageReader::supportedImageFormats().contains("svg"))
            QVERIFY(check(svg).has_value());  // a vector icon has no minimum pixel size
    }

    void fontSourceValidation()
    {
        QTemporaryDir dir;
        const QString ttf = dir.filePath(QStringLiteral("a.TTF"));
        QVERIFY(writeBytes(ttf, 64));
        QVERIFY(validateFontSource(ttf).has_value());
        QVERIFY(validateFontSource(dir.filePath(QStringLiteral("nope.ttf"))).has_value() == false);
        const QString woff = dir.filePath(QStringLiteral("a.woff"));
        QVERIFY(writeBytes(woff, 64));
        QVERIFY(!validateFontSource(woff).has_value());
        QVERIFY(!validateFontSource(ttf, 63).has_value());
        QVERIFY(validateFontSource(ttf, 64).has_value());  // exactly the cap
        QCOMPARE(fontExtension(QStringLiteral("C:/x/Face.OTF")), QStringLiteral("otf"));
        QVERIFY(fontExtension(QStringLiteral("a.woff")).isEmpty());
        QCOMPARE(iconExtension(QStringLiteral("a.SVG")), QStringLiteral("svg"));
        QVERIFY(iconExtension(QStringLiteral("a.gif")).isEmpty());
    }

    void imageExtensions_()
    {
        QCOMPARE(imageExtension(QStringLiteral("C:/x/Photo.JPG")), QStringLiteral("jpg"));
        QCOMPARE(imageExtension(QStringLiteral("a.webp")), QStringLiteral("webp"));
        QVERIFY(imageExtension(QStringLiteral("a.mp4")).isEmpty());
        QVERIFY(imageExtension(QStringLiteral("a")).isEmpty());
        QCOMPARE(imageExtensions().size(), 6);
    }
};

QTEST_MAIN(ThemeEditorModelTest)
#include "ThemeEditorModel_test.moc"
