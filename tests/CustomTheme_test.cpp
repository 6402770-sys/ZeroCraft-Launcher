// SPDX-License-Identifier: GPL-3.0-only
// Custom themes core: zero-theme.json round trip and fallbacks, slugs, color derivation (dark and light), contrast and
// readability, corner mapping, background image + card opacity, launcher icon / font / video sound, the stylesheet color helper, zip export / import
// (zip-slip safe, size caps, nothing left behind) and delete safety.
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>
#include <utility>

#include "archive/ArchiveWriter.h"
#include "ui/zero/ZeroCustomTheme.h"
#include "ui/zero/ZeroStyle.h"
#include "zero/theme/CustomTheme.h"
#include "zero/theme/ThemeArchive.h"

using namespace Zero;
using namespace Zero::Custom;

namespace {

/// Distance of a color from the background, as relative luminance difference.
double lum(const QColor& c)
{
    auto lin = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF()) + 0.0722 * lin(c.blueF());
}

/// A minimal definition with one extra top-level key (the value is escaped properly, whatever it holds).
QByteArray jsonWith(const char* key, const char* value)
{
    QJsonObject o;
    o.insert(QStringLiteral("format"), 1);
    o.insert(QStringLiteral("name"), QStringLiteral("x"));
    o.insert(QString::fromLatin1(key), QString::fromLatin1(value));
    return QJsonDocument(o).toJson();
}

QByteArray validJson(const QString& name = QStringLiteral("Forest Night"))
{
    Definition d = Definition::fromBase(Base::Obsidian, name);
    return serializeDefinition(d);
}

bool writeZip(const QString& path, const QList<std::pair<QString, QByteArray>>& entries)
{
    MMCZip::ArchiveWriter w(path);
    if (!w.open())
        return false;
    for (const auto& e : entries)
        if (!w.addFile(e.first, e.second))
            return false;
    return bool(w.close());
}

bool writePng(const QString& path)
{
    QImage img(8, 8, QImage::Format_RGB32);
    img.fill(QColor(30, 90, 60));
    return img.save(path, "PNG");
}

template <typename R>
QString errorOf(const R& r)
{
    return r ? QString() : r.error();  // QVERIFY2 evaluates its message even when the check passes
}

QStringList everything(const QString& dir)
{
    return QDir(dir).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
}

}  // namespace

class CustomThemeTest : public QObject {
    Q_OBJECT
   private slots:
    // ------------------------------------------------------------------ JSON
    void jsonRoundTrip()
    {
        Definition d = Definition::fromBase(Base::Minecraft, "Forest Night");
        d.accent = QColor("#E0703A");
        d.background = QColor("#141618");
        d.text = QColor("#F0F0F0");
        d.surface = QColor("#1C1F22");
        d.overrides.insert("danger", QColor("#FF0000"));
        d.overrides.insert("border", QColor("#334455"));
        d.corners = CornerStyle::Rounded;
        d.backgroundKind = BackgroundKind::Video;  // stored, round-trips, paints nothing yet
        d.backgroundFile = "background.mp4";
        d.dim = 0.35;
        d.cardOpacity = 0.78;

        const auto parsed = parseDefinition(serializeDefinition(d));
        QVERIFY2(parsed.has_value(), qPrintable(errorOf(parsed)));
        QCOMPARE(*parsed, d);

        // and through a folder
        QTemporaryDir dir;
        QVERIFY(saveDefinition(dir.filePath("t"), d));
        const auto loaded = loadDefinition(dir.filePath("t"));
        QVERIFY(loaded.has_value());
        QCOMPARE(*loaded, d);
    }

    void invalidAndMissingFieldsFallBackToBase()
    {
        const QByteArray json = R"({
            "format": 1, "name": "  ", "base": "nonsense",
            "colors": { "accent": "red", "background": "#12345", "text": 7, "surface": "#GGGGGG" },
            "overrides": { "danger": "#AA0000", "nope": "#00FF00", "border": "blue" },
            "corners": "triangle",
            "background": { "kind": "image", "file": "../evil.png", "dim": 4.0 },
            "cardOpacity": 0.1 })";
        const auto parsed = parseDefinition(json);
        QVERIFY2(parsed.has_value(), qPrintable(errorOf(parsed)));
        QVERIFY(!parsed->name.isEmpty());
        QCOMPARE(parsed->base, Base::Obsidian);
        QVERIFY(!parsed->accent.isValid() && !parsed->background.isValid() && !parsed->text.isValid() && !parsed->surface.isValid());
        QCOMPARE(parsed->overrides.size(), 1);  // unknown key and invalid color dropped
        QCOMPARE(parsed->overrides.value("danger"), QColor("#AA0000"));
        QCOMPARE(parsed->corners, CornerStyle::Rounded);  // Obsidian default
        QCOMPARE(parsed->backgroundKind, BackgroundKind::None);  // a path in the file name is refused
        QCOMPARE(parsed->dim, 1.0);                              // clamped
        QCOMPARE(parsed->cardOpacity, kMinCardOpacity);          // clamped up

        const ThemeSpec s = deriveSpec(*parsed);
        const ThemeSpec base = ThemeSpec::obsidian();
        QCOMPARE(s.bg, base.bg);
        QCOMPARE(s.text, base.text);
        QCOMPARE(s.accent, base.accent);
    }

    void minecraftBaseDefaultsToSquare()
    {
        const auto parsed = parseDefinition(R"({"format":1,"name":"M","base":"minecraft"})");
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->base, Base::Minecraft);
        QCOMPARE(parsed->corners, CornerStyle::Square);
        QCOMPARE(parsed->dim, kDefaultDim);
        QCOMPARE(parsed->cardOpacity, 1.0);
    }

    void unknownOrBrokenFormatIsRejected()
    {
        QVERIFY(!parseDefinition(R"({"format":2,"name":"x"})").has_value());
        QVERIFY(!parseDefinition(R"({"format":0,"name":"x"})").has_value());
        QVERIFY(!parseDefinition(R"({"name":"x"})").has_value());
        QVERIFY(!parseDefinition(R"({"format":"1","name":"x"})").has_value());
        QVERIFY(!parseDefinition("not json at all").has_value());
        QVERIFY(!parseDefinition("[1,2]").has_value());
        QVERIFY(!parseDefinition("").has_value());
    }

    void themeLoaderSkipsBadFoldersAndLoadsGoodOnes()
    {
        QTemporaryDir dir;
        // a newer-format folder: skipped (null), never a crash
        QDir().mkpath(dir.filePath("future"));
        QFile f(dir.filePath("future/zero-theme.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"format":9,"name":"Future"})");
        f.close();
        QVERIFY(!ZeroCustomTheme::load(dir.filePath("future")));
        QVERIFY(!ZeroCustomTheme::load(dir.filePath("missing")));

        Definition d = Definition::fromBase(Base::Obsidian, "Forest Night");
        QVERIFY(saveDefinition(dir.filePath("forest-night"), d));
        auto theme = ZeroCustomTheme::load(dir.filePath("forest-night"));
        QVERIFY(theme);
        QCOMPARE(theme->id(), QStringLiteral("zero-custom-forest-night"));
        QVERIFY(isZeroThemeId(theme->id()));  // so the Appearance page lists it
        QCOMPARE(theme->name(), QStringLiteral("Forest Night"));
        QVERIFY(theme->tooltip().contains("Obsidian"));
        const ThemeSpec s = theme->derivedSpec();
        QCOMPARE(s.id, theme->id());
        QVERIFY(!s.texture);
    }

    // ------------------------------------------------------------------ identity
    void slugs()
    {
        QCOMPARE(slugify("Forest Night"), QStringLiteral("forest-night"));
        QCOMPARE(slugify("  --My  Theme!! v2 "), QStringLiteral("my-theme-v2"));
        QCOMPARE(slugify(QString::fromUtf8("Th\xC3\xA8me \xC3\x9C" "ber")), QStringLiteral("theme-uber"));
        QCOMPARE(slugify("!!!"), QStringLiteral("theme"));
        QCOMPARE(slugify(""), QStringLiteral("theme"));
        QCOMPARE(slugify(QString::fromUtf8("\xE6\x97\xA5\xE6\x9C\xAC")), QStringLiteral("theme"));
        QVERIFY(slugify(QString(200, 'a')).size() <= 48);
        QVERIFY(isCustomThemeId("zero-custom-forest"));
        QVERIFY(!isCustomThemeId("zero-obsidian"));
        QVERIFY(!isCustomThemeId("zero-custom-"));
    }

    void slugsAreUniqueAmongFolders()
    {
        QTemporaryDir dir;
        QCOMPARE(uniqueSlug(dir.path(), "Forest Night"), QStringLiteral("forest-night"));
        QDir().mkpath(dir.filePath("forest-night"));
        QCOMPARE(uniqueSlug(dir.path(), "Forest Night"), QStringLiteral("forest-night-2"));
        QDir().mkpath(dir.filePath("forest-night-2"));
        QCOMPARE(uniqueSlug(dir.path(), "forest night"), QStringLiteral("forest-night-3"));
        QDir().mkpath(dir.filePath("theme"));
        QCOMPARE(uniqueSlug(dir.path(), "???"), QStringLiteral("theme-2"));

        QCOMPARE(uniqueName("A", { "B" }), QStringLiteral("A"));
        QCOMPARE(uniqueName("A", { "a" }), QStringLiteral("A (2)"));
        QCOMPARE(uniqueName("A", { "A", "A (2)" }), QStringLiteral("A (3)"));
    }

    // ------------------------------------------------------------------ derivation
    void derivationOnDarkBackground()
    {
        Definition d = Definition::fromBase(Base::Obsidian, "Dark");
        d.accent = QColor("#E0703A");
        d.background = QColor("#141618");
        d.text = QColor("#F0F0F0");
        d.surface = QColor();  // derived
        const ThemeSpec s = deriveSpec(d, {}, "zero-custom-dark");

        QCOMPARE(s.id, QStringLiteral("zero-custom-dark"));
        QCOMPARE(s.baseId, QStringLiteral("obsidian"));
        QVERIFY(!s.texture);
        QCOMPARE(s.bg, d.background);
        QCOMPARE(s.accent, d.accent);
        QCOMPARE(s.accent2, d.accent);
        QCOMPARE(s.onAccent, QColor(Qt::black));  // orange: black has the higher contrast
        QVERIFY(lum(s.accentHover) > lum(s.accent));    // dark background: hover lighter
        QVERIFY(lum(s.accentPressed) < lum(s.accent));  // pressed darker

        // surfaces step away from the background toward the text, increasing
        const double base = lum(s.bg);
        const QColor steps[] = { s.surface, s.surfaceHover, s.elevated, s.higher, s.pressed };
        double previous = base;
        for (const QColor& c : steps) {
            QVERIFY(lum(c) > previous);
            previous = lum(c);
        }
        QVERIFY(lum(s.border) > lum(s.surface));
        QVERIFY(lum(s.borderStrong) > lum(s.border));
        // text tiers fade toward the background
        QVERIFY(lum(s.text) > lum(s.textSecondary));
        QVERIFY(lum(s.textSecondary) > lum(s.textTertiary));
        QVERIFY(lum(s.textTertiary) > base);
        QVERIFY(lum(s.bevelDark) < base);
        // status colors come from the base
        const ThemeSpec ob = ThemeSpec::obsidian();
        QCOMPARE(s.success, ob.success);
        QCOMPARE(s.danger, ob.danger);
        QCOMPARE(s.highlight, ob.highlight);
        QCOMPARE(readability(s).level, Readability::Readable);
    }

    void derivationOnLightBackground()
    {
        Definition d = Definition::fromBase(Base::Obsidian, "Paper");
        d.accent = QColor("#2255AA");
        d.background = QColor("#F4F1EA");
        d.text = QColor("#1A1A1A");
        d.surface = QColor();
        const ThemeSpec s = deriveSpec(d);

        QCOMPARE(s.onAccent, QColor(Qt::white));  // dark blue: white label
        QVERIFY(lum(s.accentHover) < lum(s.accent));  // light background: hover darker
        // surfaces are ordered by distance from the (light) background: each one darker than the last
        double previous = lum(s.bg);
        for (const QColor& c : { s.surface, s.surfaceHover, s.elevated, s.higher, s.pressed }) {
            QVERIFY(lum(c) < previous);
            previous = lum(c);
        }
        QVERIFY(lum(s.textSecondary) < lum(s.bg) && lum(s.textSecondary) > lum(s.text));
        QCOMPARE(readability(s).level, Readability::Readable);
    }

    void givenSurfaceIsKeptAndOverridesWin()
    {
        Definition d = Definition::fromBase(Base::Obsidian, "O");
        d.background = QColor("#101010");
        d.text = QColor("#F0F0F0");
        d.surface = QColor("#223344");
        d.overrides.insert("border", QColor("#FF0000"));
        d.overrides.insert("onAccent", QColor("#00FF00"));
        d.overrides.insert("elevated", QColor("#0A0B0C"));
        const ThemeSpec s = deriveSpec(d);
        QCOMPARE(s.surface, QColor("#223344"));
        QCOMPARE(s.border, QColor("#FF0000"));
        QCOMPARE(s.onAccent, QColor("#00FF00"));
        QCOMPARE(s.elevated, QColor("#0A0B0C"));
    }

    void everyOverrideKeyIsAColorMember()
    {
        const QStringList keys = overrideKeys();
        QCOMPARE(keys.size(), 22);
        for (const QString& k : keys) {
            Definition d = Definition::fromBase(Base::Obsidian, "O");
            d.overrides.insert(k, QColor("#123456"));
            const ThemeSpec s = deriveSpec(d);
            QVERIFY2(Style::colorByName(s, k) == QColor("#123456") || k == "surface" || k == "surfaceHover",
                     qPrintable(k));
        }
    }

    void cornersMapping()
    {
        auto corners = [](Base b, CornerStyle c) {
            Definition d = Definition::fromBase(b, "C");
            d.corners = c;
            return deriveSpec(d);
        };
        const ThemeSpec mc = ThemeSpec::minecraft();
        {
            const ThemeSpec s = corners(Base::Obsidian, CornerStyle::Rounded);
            QCOMPARE(s.corners, Corners::Rounded);
            QCOMPARE(s.radiusSm, Tokens::radiusSm);
            QCOMPARE(s.radiusMd, Tokens::radiusMd);
            QCOMPARE(s.radiusLg, Tokens::radiusLg);
            QCOMPARE(s.radiusXl, Tokens::radiusXl);
        }
        {
            const ThemeSpec s = corners(Base::Obsidian, CornerStyle::Square);
            QCOMPARE(s.corners, Corners::Rounded);
            QVERIFY(s.radiusSm == 0 && s.radiusMd == 0 && s.radiusLg == 0 && s.radiusXl == 0);
        }
        {
            const ThemeSpec s = corners(Base::Minecraft, CornerStyle::Square);
            QCOMPARE(s.corners, Corners::Notched);
            QCOMPARE(s.radiusSm, mc.radiusSm);
            QCOMPARE(s.radiusXl, mc.radiusXl);
            QVERIFY(s.bevel);
            QCOMPARE(s.pixelRoles, mc.pixelRoles);
            QCOMPARE(s.styleId(), QStringLiteral("minecraft"));
            QVERIFY(!s.texture);  // never the stone tile
        }
        {
            const ThemeSpec s = corners(Base::Minecraft, CornerStyle::Rounded);
            QCOMPARE(s.corners, Corners::Rounded);
            QCOMPARE(s.radiusSm, Tokens::radiusSm);
            QCOMPARE(s.radiusXl, Tokens::radiusXl);
        }
    }

    void backgroundImageAndCardOpacity()
    {
        QTemporaryDir dir;
        QVERIFY(writePng(dir.filePath("background.png")));
        Definition d = Definition::fromBase(Base::Obsidian, "Img");
        d.backgroundKind = BackgroundKind::Image;
        d.backgroundFile = "background.png";
        d.dim = 0.35;
        d.cardOpacity = 0.78;

        ThemeSpec s = deriveSpec(d, dir.path());
        QCOMPARE(s.backgroundImage, QFileInfo(dir.filePath("background.png")).absoluteFilePath());
        QCOMPARE(s.backgroundDim, 0.35);
        QCOMPARE(s.cardOpacity, 0.78);
        // only the card colors are translucent
        QCOMPARE(s.surface.alpha(), qRound(0.78 * 255));
        QCOMPARE(s.surfaceHover.alpha(), qRound(0.78 * 255));
        for (const QColor& c : { s.elevated, s.higher, s.pressed, s.border, s.bg, s.accent })
            QCOMPARE(c.alpha(), 255);
        QCOMPARE(s.surfaceSolid().alpha(), 255);
        QCOMPARE(s.surfaceSolid().rgb(), s.surface.rgb());

        // a missing file is treated as none; a video fills backgroundVideo (not the image); no folder = no image
        QFile::remove(dir.filePath("background.png"));
        QVERIFY(deriveSpec(d, dir.path()).backgroundImage.isEmpty());
        QVERIFY(writePng(dir.filePath("background.png")));
        d.backgroundKind = BackgroundKind::Video;
        {
            // any existing file stands in for the clip
            const ThemeSpec v = deriveSpec(d, dir.path());
            QVERIFY(v.backgroundImage.isEmpty());
            QCOMPARE(v.backgroundVideo, QFileInfo(dir.filePath("background.png")).absoluteFilePath());
            QCOMPARE(v.backgroundDim, d.dim);
            const QString keep = d.backgroundFile;
            d.backgroundFile = QStringLiteral("gone.mp4");
            QVERIFY(deriveSpec(d, dir.path()).backgroundVideo.isEmpty());
            d.backgroundFile = keep;
        }
        QVERIFY(deriveSpec(d).backgroundVideo.isEmpty());
        d.backgroundKind = BackgroundKind::Image;
        QVERIFY(deriveSpec(d).backgroundImage.isEmpty());

        // the built-ins are untouched
        for (const ThemeSpec& b : { ThemeSpec::obsidian(), ThemeSpec::minecraft() }) {
            QVERIFY(b.backgroundImage.isEmpty());
            QCOMPARE(b.cardOpacity, 1.0);
            QCOMPARE(b.surface.alpha(), 255);
        }
    }

    // ------------------------------------------------------------------ contrast / readability
    void contrastKnownValues()
    {
        QCOMPARE(contrastRatio(Qt::black, Qt::white), 21.0);
        QCOMPARE(contrastRatio(Qt::white, Qt::black), 21.0);
        QCOMPARE(contrastRatio(Qt::white, Qt::white), 1.0);
        QVERIFY(qAbs(contrastRatio(QColor("#777777"), Qt::white) - 4.48) < 0.02);  // the classic just-under-4.5 grey
        QVERIFY(contrastRatio(QColor("#767676"), Qt::white) >= 4.5);
    }

    void readabilityPassAndFail()
    {
        QCOMPARE(readability(ThemeSpec::obsidian()).level, Readability::Readable);
        QCOMPARE(readability(ThemeSpec::minecraft()).level, Readability::Readable);
        QVERIFY(readability(ThemeSpec::obsidian()).summary.isEmpty());

        Definition d = Definition::fromBase(Base::Obsidian, "Murky");
        d.background = QColor("#202020");
        d.text = QColor("#3A3A3A");  // text almost on the background
        d.surface = QColor("#242424");
        const auto bad = readability(deriveSpec(d));
        QCOMPARE(bad.level, Readability::HardToRead);
        QVERIFY(!bad.textOnBackground);
        QVERIFY(!bad.textOnSurface);
        QVERIFY(!bad.summary.isEmpty());

        // text fine, button label not: an override makes onAccent equal the accent
        Definition e = Definition::fromBase(Base::Obsidian, "Flat button");
        e.overrides.insert("onAccent", e.accent);
        const auto label = readability(deriveSpec(e));
        QCOMPARE(label.level, Readability::HardToRead);
        QVERIFY(label.textOnBackground && label.textOnSurface);
        QVERIFY(!label.accentLabel);

        // a translucent card is judged on its solid color
        Definition f = Definition::fromBase(Base::Obsidian, "Glass");
        f.cardOpacity = 0.4;
        QCOMPARE(readability(deriveSpec(f)).level, Readability::Readable);
    }

    // ------------------------------------------------------------------ stylesheet color helper
    void qssColorHelperEmitsRgbaOnlyWhenTranslucent()
    {
        const auto opaque = Style::tokenMap(ThemeSpec::obsidian());
        QVERIFY(opaque.value("surface").startsWith('#'));
        QVERIFY(opaque.value("surfaceHover").startsWith('#'));

        Definition d = Definition::fromBase(Base::Obsidian, "Glass");
        d.cardOpacity = 0.78;
        const ThemeSpec s = deriveSpec(d);
        const auto glass = Style::tokenMap(s);
        QVERIFY(glass.value("surface").startsWith("rgba("));
        QVERIFY(glass.value("surface").endsWith(QStringLiteral(", %1)").arg(qRound(0.78 * 255))));
        QVERIFY(glass.value("surfaceHover").startsWith("rgba("));
        for (const char* k : { "bg", "elevated", "higher", "pressed", "border", "accent", "text" })
            QVERIFY2(glass.value(k).startsWith('#'), k);
        // popups use the opaque twins
        QVERIFY(glass.value("surfaceSolid").startsWith('#'));
        QVERIFY(glass.value("surfaceHoverSolid").startsWith('#'));
        QCOMPARE(glass.value("surfaceSolid"), s.surfaceSolid().name());
    }

    void generatedStyleSheetKeepsPopupsOpaque()
    {
        Definition d = Definition::fromBase(Base::Minecraft, "Glass");
        d.cardOpacity = 0.6;
        const QString sheet = Style::styleSheet(deriveSpec(d));
        QVERIFY(!sheet.contains(QLatin1Char('@')));  // every placeholder resolved
        auto background = [&](const QString& selector) {
            const qsizetype at = sheet.indexOf(QStringLiteral("\n") + selector + QStringLiteral(" {"));
            if (at < 0)
                return QString();
            const qsizetype line = sheet.indexOf(QStringLiteral("background-color:"), at);
            return sheet.mid(line + 17, 12).trimmed();
        };
        QVERIFY2(background("QMenu").startsWith('#'), qPrintable(background("QMenu")));
        QVERIFY2(background("QComboBox QAbstractItemView").startsWith('#'), qPrintable(background("QComboBox QAbstractItemView")));
        QVERIFY(sheet.contains("rgba("));  // while the cards (inputs, tables) are translucent
    }

    // ------------------------------------------------------------------ export / import
    void exportImportRoundTrip()
    {
        QTemporaryDir work;
        const QString themes = work.filePath("themes");
        const QString src = themes + "/forest-night";
        Definition d = Definition::fromBase(Base::Minecraft, "Forest Night");
        d.accent = QColor("#E0703A");
        d.overrides.insert("danger", QColor("#CC0000"));
        d.backgroundKind = BackgroundKind::Image;
        d.backgroundFile = "wallpaper.png";  // a hand-made name: exported as background.png
        d.dim = 0.5;
        d.cardOpacity = 0.7;
        QVERIFY(saveDefinition(src, d));
        QVERIFY(writePng(src + "/wallpaper.png"));

        const QString zip = work.filePath("forest.zip");
        const auto exported = exportTheme(src, zip);
        QVERIFY2(exported.has_value(), qPrintable(errorOf(exported)));
        QVERIFY(QFileInfo(zip).isFile());
        QVERIFY(!QFileInfo::exists(zip + ".part"));

        const auto imported = importTheme(zip, themes);
        QVERIFY2(imported.has_value(), qPrintable(errorOf(imported)));
        QCOMPARE(imported->name, QStringLiteral("Forest Night (2)"));  // the original is still there: clash
        QCOMPARE(imported->id, QStringLiteral("zero-custom-forest-night-2"));
        QVERIFY(QFileInfo(imported->folder).isDir());
        QCOMPARE(QFileInfo(imported->folder).fileName(), QStringLiteral("forest-night-2"));
        QVERIFY(QFileInfo(imported->folder + "/background.png").isFile());

        auto back = loadDefinition(imported->folder);
        QVERIFY(back.has_value());
        Definition expected = d;
        expected.name = "Forest Night (2)";
        expected.backgroundFile = "background.png";
        QCOMPARE(*back, expected);
        // the zip held exactly the json and the background: the new folder has nothing else, and the old one is untouched
        QCOMPARE(everything(imported->folder), (QStringList{ "background.png", "zero-theme.json" }));
        QVERIFY(QFileInfo(src + "/wallpaper.png").isFile());
        QCOMPARE(QStringList(everything(themes)).size(), 2);

        // a third import gets yet another slug and name
        const auto again = importTheme(zip, themes);
        QVERIFY(again.has_value());
        QCOMPARE(again->name, QStringLiteral("Forest Night (3)"));
        QCOMPARE(QFileInfo(again->folder).fileName(), QStringLiteral("forest-night-3"));

        // into an empty themes dir the name stays as it is
        const QString other = work.filePath("other");
        const auto fresh = importTheme(zip, other);
        QVERIFY(fresh.has_value());
        QCOMPARE(fresh->name, QStringLiteral("Forest Night"));
        QCOMPARE(fresh->id, QStringLiteral("zero-custom-forest-night"));
        // and the result loads as a real theme with the image
        auto theme = ZeroCustomTheme::load(fresh->folder);
        QVERIFY(theme);
        QVERIFY(!theme->derivedSpec().backgroundImage.isEmpty());
    }

    void exportWithoutBackgroundAndWithMissingFile()
    {
        QTemporaryDir work;
        const QString src = work.filePath("themes/plain");
        Definition d = Definition::fromBase(Base::Obsidian, "Plain");
        d.backgroundKind = BackgroundKind::Image;
        d.backgroundFile = "gone.png";  // file does not exist: exported as none
        QVERIFY(saveDefinition(src, d));
        QVERIFY(exportTheme(src, work.filePath("plain.zip")).has_value());
        const auto imported = importTheme(work.filePath("plain.zip"), work.filePath("elsewhere"));
        QVERIFY(imported.has_value());
        const auto back = loadDefinition(imported->folder);
        QVERIFY(back.has_value());
        QCOMPARE(back->backgroundKind, BackgroundKind::None);
        QCOMPARE(everything(imported->folder), (QStringList{ "zero-theme.json" }));

        QVERIFY(!exportTheme(work.filePath("nothing-here"), work.filePath("x.zip")).has_value());
    }

    void importRejectsUnsafeOrUnexpectedArchives_data()
    {
        QTest::addColumn<QString>("entryName");
        QTest::addColumn<int>("size");
        QTest::newRow("dotdot") << QStringLiteral("../x") << 10;
        QTest::newRow("dotdot json") << QStringLiteral("../zero-theme.json") << 10;
        QTest::newRow("subfolder json") << QStringLiteral("sub/zero-theme.json") << 10;
        QTest::newRow("subfolder background") << QStringLiteral("sub/background.png") << 10;
        QTest::newRow("absolute") << QStringLiteral("/etc/evil.txt") << 10;
        QTest::newRow("absolute json") << QStringLiteral("/zero-theme.json") << 10;
        QTest::newRow("absolute background") << QStringLiteral("/background.png") << 10;
        QTest::newRow("drive") << QStringLiteral("C:/evil.txt") << 10;
        QTest::newRow("backslash") << QStringLiteral("..\\evil.txt") << 10;
        QTest::newRow("extra file") << QStringLiteral("notes.txt") << 10;
        QTest::newRow("exe renamed") << QStringLiteral("background.exe") << 10;
        QTest::newRow("hidden") << QStringLiteral(".hidden") << 10;
        QTest::newRow("oversize json") << QStringLiteral("zero-theme.json") << (256 * 1024 + 1);
    }

    void importRejectsUnsafeOrUnexpectedArchives()
    {
        QFETCH(QString, entryName);
        QFETCH(int, size);
        QTemporaryDir work;
        const QString themes = work.filePath("themes");
        QVERIFY(QDir().mkpath(themes));

        QList<std::pair<QString, QByteArray>> entries;
        if (entryName != QLatin1String("zero-theme.json"))
            entries.append({ QStringLiteral("zero-theme.json"), validJson() });
        entries.append({ entryName, QByteArray(size, ' ') });  // oversize json: spaces keep it parseable, only the size is wrong
        const QString zip = work.filePath("bad.zip");
        QVERIFY(writeZip(zip, entries));

        const auto result = importTheme(zip, themes);
        QVERIFY2(!result.has_value(), qPrintable(entryName));
        QVERIFY(!result.error().isEmpty());
        QVERIFY2(everything(themes).isEmpty(), qPrintable(everything(themes).join(",")));  // nothing left behind
        QVERIFY(!QFileInfo::exists(work.filePath("x")));
        QVERIFY(!QFileInfo::exists(work.filePath("evil.txt")));
    }

    void importRejectsOtherBrokenArchives()
    {
        QTemporaryDir work;
        const QString themes = work.filePath("themes");
        const QString zip = work.filePath("bad.zip");

        // no definition at all
        QVERIFY(writeZip(zip, { { "background.png", QByteArray("x") } }));
        QVERIFY(!importTheme(zip, themes).has_value());
        QVERIFY(everything(themes).isEmpty());

        // invalid / newer-format definition
        QVERIFY(writeZip(zip, { { "zero-theme.json", QByteArray("{ nope") } }));
        QVERIFY(!importTheme(zip, themes).has_value());
        QVERIFY(writeZip(zip, { { "zero-theme.json", QByteArray(R"({"format":5,"name":"x"})") } }));
        QVERIFY(!importTheme(zip, themes).has_value());
        QVERIFY(everything(themes).isEmpty());

        // two backgrounds / two definitions
        QVERIFY(writeZip(zip, { { "zero-theme.json", validJson() }, { "background.png", "a" }, { "background.jpg", "b" } }));
        QVERIFY(!importTheme(zip, themes).has_value());
        QVERIFY(writeZip(zip, { { "zero-theme.json", validJson() }, { "zero-theme.json", validJson() } }));
        QVERIFY(!importTheme(zip, themes).has_value());

        // an oversize background (the cap is a parameter so the test stays small)
        ImportLimits tiny;
        tiny.maxBackgroundBytes = 16;
        QVERIFY(writeZip(zip, { { "zero-theme.json", validJson() }, { "background.png", QByteArray(17, 'x') } }));
        QVERIFY(!importTheme(zip, themes, tiny).has_value());
        QVERIFY(everything(themes).isEmpty());
        QVERIFY(importTheme(zip, themes).has_value());  // fine under the real cap

        // not a zip / missing file
        QFile junk(work.filePath("junk.zip"));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write("this is not an archive");
        junk.close();
        QVERIFY(!importTheme(work.filePath("junk.zip"), work.filePath("t2")).has_value());
        QVERIFY(everything(work.filePath("t2")).isEmpty());
        QVERIFY(!importTheme(work.filePath("missing.zip"), work.filePath("t2")).has_value());
    }

    void importAcceptsEveryBackgroundExtensionAndDropsUnreferencedOnes()
    {
        QTemporaryDir work;
        for (const QString& ext : backgroundExtensions()) {
            Definition d = Definition::fromBase(Base::Obsidian, "Ext " + ext);
            d.backgroundKind = ext == "mp4" ? BackgroundKind::Video : BackgroundKind::Image;
            d.backgroundFile = "background." + ext;
            const QString zip = work.filePath("e.zip");
            QVERIFY(writeZip(zip, { { "zero-theme.json", serializeDefinition(d) }, { "background." + ext, "data" } }));
            const auto r = importTheme(zip, work.filePath("themes"));
            QVERIFY2(r.has_value(), qPrintable(ext + ": " + errorOf(r)));
            QVERIFY(QFileInfo(r->folder + "/background." + ext).isFile());
        }
        // a background nobody points at is not kept
        Definition none = Definition::fromBase(Base::Obsidian, "NoBg");
        QVERIFY(writeZip(work.filePath("n.zip"), { { "zero-theme.json", serializeDefinition(none) }, { "background.png", "data" } }));
        const auto r = importTheme(work.filePath("n.zip"), work.filePath("themes"));
        QVERIFY(r.has_value());
        QCOMPARE(everything(r->folder), (QStringList{ "zero-theme.json" }));
    }

    // ------------------------------------------------------------------ launcher icon, font, video sound
    void iconFontAndSoundRoundTripThroughJson()
    {
        Definition d = Definition::fromBase(Base::Obsidian, "Voices");
        d.backgroundKind = BackgroundKind::Video;
        d.backgroundFile = "background.mp4";
        d.sound = true;
        d.volume = 0.3;
        d.iconFile = "icon.png";
        d.fontFile = "font.otf";
        const auto back = parseDefinition(serializeDefinition(d));
        QVERIFY2(back.has_value(), qPrintable(errorOf(back)));
        QCOMPARE(*back, d);
        QVERIFY(back->sound);
        QCOMPARE(back->volume, 0.3);

        // The keys are in the documented places.
        const QJsonObject root = QJsonDocument::fromJson(serializeDefinition(d)).object();
        QCOMPARE(root.value("icon").toString(), QStringLiteral("icon.png"));
        QCOMPARE(root.value("font").toString(), QStringLiteral("font.otf"));
        QCOMPARE(root.value("background").toObject().value("sound").toBool(), true);
        QCOMPARE(root.value("background").toObject().value("volume").toDouble(), 0.3);

        // A theme that has none of them writes none of the keys.
        const QJsonObject plain = QJsonDocument::fromJson(validJson()).object();
        QVERIFY(!plain.contains("icon"));
        QVERIFY(!plain.contains("font"));
        QVERIFY(!plain.value("background").toObject().contains("sound"));
    }

    void oldThemesWithoutTheNewKeysHaveNoSoundIconOrFont()
    {
        const auto d = parseDefinition(R"({"format":1,"name":"Old","background":{"kind":"video","file":"background.mp4","dim":0.4}})");
        QVERIFY(d.has_value());
        QVERIFY(!d->sound);  // default off
        QCOMPARE(d->volume, kDefaultVolume);
        QVERIFY(d->iconFile.isEmpty());
        QVERIFY(d->fontFile.isEmpty());
    }

    void iconFontSoundValuesAreSanitised()
    {
        const auto loud = parseDefinition(R"({"format":1,"name":"x","background":{"kind":"video","file":"background.mp4","sound":true,"volume":7}})");
        QVERIFY(loud.has_value());
        QCOMPARE(loud->volume, 1.0);
        const auto quiet = parseDefinition(R"({"format":1,"name":"x","background":{"kind":"video","file":"background.mp4","sound":true,"volume":-2}})");
        QCOMPARE(quiet->volume, 0.0);
        const auto text = parseDefinition(R"({"format":1,"name":"x","background":{"kind":"video","file":"background.mp4","sound":"yes","volume":"loud"}})");
        QVERIFY(!text->sound);  // not a bool
        QCOMPARE(text->volume, kDefaultVolume);

        for (const char* icon : { "../icon.png", "sub/icon.png", "icon.gif", "icon.exe", "icon", "..\\icon.png", "C:icon.png", "" }) {
            const auto d = parseDefinition(jsonWith("icon", icon));
            QVERIFY2(d.has_value(), icon);
            QVERIFY2(d->iconFile.isEmpty(), icon);
        }
        for (const char* font : { "../font.ttf", "a/font.ttf", "font.woff", "font.exe", "font", "..\\font.ttf", "C:font.ttf", "" }) {
            const auto d = parseDefinition(jsonWith("font", font));
            QVERIFY2(d.has_value(), font);
            QVERIFY2(d->fontFile.isEmpty(), font);
        }
        for (const char* icon : { "icon.png", "icon.svg", "icon.ico", "ICON.PNG" }) {
            const auto d = parseDefinition(jsonWith("icon", icon));
            QCOMPARE(d->iconFile, QString::fromLatin1(icon));
        }
    }

    void deriveSpecResolvesIconFontAndSound()
    {
        QTemporaryDir work;
        const QString folder = work.filePath("voices");
        QVERIFY(QDir().mkpath(folder));
        for (const char* name : { "icon.png", "font.ttf", "background.mp4" }) {
            QFile f(folder + "/" + name);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("data");
        }
        Definition d = Definition::fromBase(Base::Obsidian, "Voices");
        d.backgroundKind = BackgroundKind::Video;
        d.backgroundFile = "background.mp4";
        d.sound = true;
        d.volume = 0.25;
        d.iconFile = "icon.png";
        d.fontFile = "font.ttf";
        ThemeSpec s = deriveSpec(d, folder, "zero-custom-voices");
        QCOMPARE(s.appIcon, QDir(folder).absoluteFilePath("icon.png"));
        QCOMPARE(s.fontFile, QDir(folder).absoluteFilePath("font.ttf"));
        QVERIFY(s.fontFamily.isEmpty());  // resolved when the theme is applied, not here
        QVERIFY(s.videoSound);
        QCOMPARE(s.videoVolume, 0.25);

        // sound is a video thing: an image or no background never plays it
        d.backgroundKind = BackgroundKind::None;
        s = deriveSpec(d, folder, "zero-custom-voices");
        QVERIFY(!s.videoSound);

        // files that are gone fall back to the defaults
        QVERIFY(QFile::remove(folder + "/icon.png"));
        QVERIFY(QFile::remove(folder + "/font.ttf"));
        s = deriveSpec(d, folder, "zero-custom-voices");
        QVERIFY(s.appIcon.isEmpty());
        QVERIFY(s.fontFile.isEmpty());
        // and without a folder there is nothing to point at
        QVERIFY(deriveSpec(d).appIcon.isEmpty());

        // built-in themes have none of it
        QVERIFY(ThemeSpec::obsidian().appIcon.isEmpty());
        QVERIFY(ThemeSpec::minecraft().fontFile.isEmpty());
        QVERIFY(!ThemeSpec::obsidian().videoSound);
    }

    void exportAndImportCarryIconFontAndSound()
    {
        QTemporaryDir work;
        const QString themes = work.filePath("themes");
        const QString src = themes + "/voices";
        Definition d = Definition::fromBase(Base::Obsidian, "Voices");
        d.backgroundKind = BackgroundKind::Video;
        d.backgroundFile = "clip.mp4";  // a hand-made name: exported as background.mp4
        d.sound = true;
        d.volume = 0.8;
        d.iconFile = "my-icon.svg";  // exported as icon.svg
        d.fontFile = "Fancy.OTF";    // exported as font.otf
        QVERIFY(saveDefinition(src, d));
        for (const char* name : { "clip.mp4", "my-icon.svg", "Fancy.OTF" }) {
            QFile f(src + "/" + name);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(name);
        }
        const QString zip = work.filePath("voices.zip");
        const auto exported = exportTheme(src, zip);
        QVERIFY2(exported.has_value(), qPrintable(errorOf(exported)));

        const auto imported = importTheme(zip, work.filePath("other"));
        QVERIFY2(imported.has_value(), qPrintable(errorOf(imported)));
        QCOMPARE(everything(imported->folder), (QStringList{ "background.mp4", "font.otf", "icon.svg", "zero-theme.json" }));
        const auto back = loadDefinition(imported->folder);
        QVERIFY(back.has_value());
        QCOMPARE(back->iconFile, QStringLiteral("icon.svg"));
        QCOMPARE(back->fontFile, QStringLiteral("font.otf"));
        QVERIFY(back->sound);
        QCOMPARE(back->volume, 0.8);
        QFile icon(imported->folder + "/icon.svg");
        QVERIFY(icon.open(QIODevice::ReadOnly));
        QCOMPARE(icon.readAll(), QByteArray("my-icon.svg"));  // the content travelled, not just the name

        // a theme whose icon / font file is gone exports as default
        QVERIFY(QFile::remove(src + "/my-icon.svg"));
        QVERIFY(QFile::remove(src + "/Fancy.OTF"));
        QVERIFY(exportTheme(src, zip).has_value());
        const auto lean = importTheme(zip, work.filePath("lean"));
        QVERIFY(lean.has_value());
        QVERIFY(loadDefinition(lean->folder)->iconFile.isEmpty());
        QVERIFY(loadDefinition(lean->folder)->fontFile.isEmpty());
        QCOMPARE(everything(lean->folder), (QStringList{ "background.mp4", "zero-theme.json" }));
    }

    void importAcceptsTheIconAndFontNames()
    {
        QTemporaryDir work;
        for (const QString& name : { "icon.png", "icon.svg", "icon.ico", "icon.PNG", "font.ttf", "font.otf", "font.TTF" }) {
            Definition d = Definition::fromBase(Base::Obsidian, "Names " + name);
            (name.startsWith("icon") ? d.iconFile : d.fontFile) = name;
            const QString zip = work.filePath("n.zip");
            QVERIFY(writeZip(zip, { { "zero-theme.json", serializeDefinition(d) }, { name, "data" } }));
            const auto r = importTheme(zip, work.filePath("themes"));
            QVERIFY2(r.has_value(), qPrintable(name + ": " + errorOf(r)));
            QVERIFY(QFileInfo(r->folder + "/" + name).isFile());
        }
        // an icon / font nobody points at is not kept, and a pointer to a file that is not there is cleared
        Definition none = Definition::fromBase(Base::Obsidian, "Nothing declared");
        QVERIFY(writeZip(work.filePath("u.zip"), { { "zero-theme.json", serializeDefinition(none) }, { "icon.png", "a" }, { "font.ttf", "b" } }));
        const auto kept = importTheme(work.filePath("u.zip"), work.filePath("themes"));
        QVERIFY(kept.has_value());
        QCOMPARE(everything(kept->folder), (QStringList{ "zero-theme.json" }));
        Definition declared = Definition::fromBase(Base::Obsidian, "Declared only");
        declared.iconFile = "icon.png";
        declared.fontFile = "font.ttf";
        QVERIFY(writeZip(work.filePath("d.zip"), { { "zero-theme.json", serializeDefinition(declared) } }));
        const auto cleared = importTheme(work.filePath("d.zip"), work.filePath("themes"));
        QVERIFY(cleared.has_value());
        QVERIFY(loadDefinition(cleared->folder)->iconFile.isEmpty());
        QVERIFY(loadDefinition(cleared->folder)->fontFile.isEmpty());
    }

    void importRejectsWrongIconFontEntriesAndOversize()
    {
        QTemporaryDir work;
        const QString themes = work.filePath("themes");
        const QString zip = work.filePath("bad.zip");
        const QByteArray json = validJson();
        for (const QString& name : { "icon.gif", "icon.exe", "icon.png.exe", "icon", "icons/icon.png", "../icon.png", "/icon.png", "font.woff", "font.ttf.exe",
                                     "fonts/font.ttf", "../font.ttf", "C:/font.ttf" }) {
            QVERIFY(writeZip(zip, { { "zero-theme.json", json }, { name, "data" } }));
            QVERIFY2(!importTheme(zip, themes).has_value(), qPrintable(name));
            QVERIFY2(everything(themes).isEmpty(), qPrintable(name));  // nothing left behind
        }
        // two icons / two fonts
        QVERIFY(writeZip(zip, { { "zero-theme.json", json }, { "icon.png", "a" }, { "icon.svg", "b" } }));
        QVERIFY(!importTheme(zip, themes).has_value());
        QVERIFY(writeZip(zip, { { "zero-theme.json", json }, { "font.ttf", "a" }, { "font.otf", "b" } }));
        QVERIFY(!importTheme(zip, themes).has_value());
        QVERIFY(everything(themes).isEmpty());

        // oversize (the caps are parameters so the test stays small); exactly the cap is fine
        ImportLimits tiny;
        tiny.maxIconBytes = 16;
        tiny.maxFontBytes = 32;
        QVERIFY(writeZip(zip, { { "zero-theme.json", json }, { "icon.png", QByteArray(17, 'x') } }));
        QVERIFY(!importTheme(zip, themes, tiny).has_value());
        QVERIFY(writeZip(zip, { { "zero-theme.json", json }, { "font.ttf", QByteArray(33, 'x') } }));
        QVERIFY(!importTheme(zip, themes, tiny).has_value());
        QVERIFY(everything(themes).isEmpty());
        QVERIFY(writeZip(zip, { { "zero-theme.json", json }, { "icon.png", QByteArray(16, 'x') }, { "font.ttf", QByteArray(32, 'x') } }));
        QVERIFY(importTheme(zip, themes, tiny).has_value());
        QVERIFY(kMaxIconBytes == 2 * 1024 * 1024 && kMaxFontBytes == 20 * 1024 * 1024);
    }

    // ------------------------------------------------------------------ delete
    void trashRefusesFoldersThatAreNotThemes()
    {
        QTemporaryDir work;
        QVERIFY(!trashTheme(work.filePath("missing")).has_value());
        QDir().mkpath(work.filePath("other"));
        QFile keep(work.filePath("other/keep.txt"));
        QVERIFY(keep.open(QIODevice::WriteOnly));
        keep.close();
        const auto r = trashTheme(work.filePath("other"));
        QVERIFY(!r.has_value());
        QVERIFY(QFileInfo(work.filePath("other/keep.txt")).isFile());  // not touched
    }
};

QTEST_GUILESS_MAIN(CustomThemeTest)
#include "CustomTheme_test.moc"
