// SPDX-License-Identifier: GPL-3.0-only
// What a custom theme brings besides colors, and how top-level windows look under it: top-level windows (dialogs, message boxes,
// tooltips) are OPAQUE under a translucent card opacity, the theme font (registered, used by every role except the pixel roles,
// a file Qt cannot load falls back to the default font with one warning, never a crash), and the theme's own launcher icon (which
// file the window / rail show).
#include <QApplication>
#include <QColorDialog>
#include <QFile>
#include <QFileDialog>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

#include "ui/zero/ZeroCustomFont.h"
#include "ui/zero/ZeroPixelFont.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroTheme.h"
#include "ui/zero/ZeroThemeState.h"

using namespace Zero;

namespace {

QStringList g_warnings;
QtMessageHandler g_previous = nullptr;

void captureHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    if (type == QtWarningMsg)
        g_warnings << msg;
    if (g_previous)
        g_previous(type, ctx, msg);
}

QStringList fontWarnings()
{
    return g_warnings.filter(QStringLiteral("ZeroCustomFont:"));
}

/// The generated pixel font with its family renamed (same length, so every table offset stays valid): a real TrueType file whose family
/// is NOT the built-in pixel family, standing in for a font the user picked.
QByteArray otherFontData()
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

bool writeFile(const QString& path, const QByteArray& data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

/// The declaration block of the rule whose selector list names exactly `selector` (not a descendant of it), or an empty string.
QString ruleFor(const QString& qss, const QString& selector)
{
    static const QRegularExpression block(QStringLiteral("([^{}]+)\\{([^}]*)\\}"));
    auto it = block.globalMatch(qss);
    while (it.hasNext()) {
        const auto m = it.next();
        // strip comments, then split the selector list
        QString head = m.captured(1);
        head.remove(QRegularExpression(QStringLiteral("/\\*.*?\\*/"), QRegularExpression::DotMatchesEverythingOption));
        for (const QString& part : head.split(QLatin1Char(','))) {
            if (part.simplified() == selector)
                return m.captured(2);
        }
    }
    return {};
}

QString backgroundOf(const QString& declarations)
{
    static const QRegularExpression re(QStringLiteral("background-color:\\s*([^;]+);"));
    const auto m = re.match(declarations);
    return m.hasMatch() ? m.captured(1).simplified() : QString();
}

ThemeSpec translucentSpec(const ThemeSpec& base)
{
    ThemeSpec s = base;
    s.cardOpacity = 0.6;
    s.surface.setAlphaF(0.6);
    s.surfaceHover.setAlphaF(0.6);
    return s;
}

}  // namespace

class ZeroThemeAssetsTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_font;  // a real TrueType file, family "ZERO Other"

   private slots:
    void initTestCase()
    {
        g_previous = qInstallMessageHandler(captureHandler);
        m_font = m_dir.filePath(QStringLiteral("themefont.ttf"));
        QVERIFY(writeFile(m_font, otherFontData()));
    }

    void cleanupTestCase() { qInstallMessageHandler(g_previous); }

    void cleanup()
    {
        CustomFont::load(QString());
        ThemeSpec spec = ThemeSpec::obsidian();
        spec.reduceMotion = true;
        Theme::setCurrent(spec);
        qApp->setStyleSheet(QString());
    }

    // ------------------------------------------------------------------ top-level windows are opaque

    void stylesheetPaintsEveryTopLevelWindowOpaque_data()
    {
        QTest::addColumn<bool>("minecraft");
        QTest::newRow("obsidian") << false;
        QTest::newRow("minecraft") << true;
    }

    void stylesheetPaintsEveryTopLevelWindowOpaque()
    {
        QFETCH(bool, minecraft);
        const ThemeSpec spec = translucentSpec(minecraft ? ThemeSpec::minecraft() : ThemeSpec::obsidian());
        QVERIFY(spec.surface.alpha() < 255);  // the case the bug needs: cards are see-through
        const QString qss = Style::styleSheet(spec);
        QVERIFY(qss.contains(QStringLiteral("rgba(")));  // and the sheet does carry that alpha for the cards

        for (const char* cls : { "QMainWindow", "QDialog", "QColorDialog", "QFileDialog", "QInputDialog", "QMessageBox", "QProgressDialog", "QWizard" }) {
            const QString rule = ruleFor(qss, QString::fromLatin1(cls));
            QVERIFY2(!rule.isEmpty(), cls);
            const QString bg = backgroundOf(rule);
            QVERIFY2(bg.startsWith(QLatin1Char('#')) && bg.size() == 7, qPrintable(QString::fromLatin1(cls) + ": " + bg));
            QCOMPARE(bg, spec.bg.name());
        }
        const QString tip = backgroundOf(ruleFor(qss, QStringLiteral("QToolTip")));
        QVERIFY2(tip.startsWith(QLatin1Char('#')), qPrintable(tip));
        QCOMPARE(tip, ThemeSpec::opaque(spec.elevated).name());
        // popups that already used the solid tokens still do
        QCOMPARE(backgroundOf(ruleFor(qss, QStringLiteral("QMenu"))), spec.surfaceSolid().name());
    }

    void dialogsGrabOpaqueUnderATranslucentCardOpacity()
    {
        const ThemeSpec spec = translucentSpec(ThemeSpec::obsidian());
        qApp->setPalette(Style::palette(spec));
        qApp->setStyleSheet(Style::styleSheet(spec));

        const auto opaque = [&](QWidget& w, const char* what) {
            w.resize(w.sizeHint().expandedTo(QSize(320, 200)));
            w.show();
            QVERIFY2(QTest::qWaitForWindowExposed(&w), what);
            const QImage img = w.grab().toImage().convertToFormat(QImage::Format_ARGB32);
            QVERIFY2(!img.isNull(), what);
            // the corners and the middle of the bare window background: no alpha anywhere
            for (const QPoint& p : { QPoint(1, 1), QPoint(img.width() - 2, 1), QPoint(1, img.height() - 2), QPoint(img.width() - 2, img.height() - 2) }) {
                QVERIFY2(qAlpha(img.pixel(p)) == 255, what);
                QVERIFY2(QColor(img.pixel(p)).rgb() == spec.bg.rgb(), qPrintable(QString::fromLatin1(what) + " " + QColor(img.pixel(p)).name()));
            }
        };
        QColorDialog color(QColor(10, 20, 30));
        color.setOption(QColorDialog::DontUseNativeDialog, true);
        opaque(color, "QColorDialog");
        QMessageBox box(QMessageBox::Question, QStringLiteral("Q"), QStringLiteral("Move it?"));
        opaque(box, "QMessageBox");
        QInputDialog input;
        input.setLabelText(QStringLiteral("Name"));
        opaque(input, "QInputDialog");
        QProgressDialog progress(QStringLiteral("Working"), QStringLiteral("Cancel"), 0, 10);
        opaque(progress, "QProgressDialog");
        QFileDialog files;
        files.setOption(QFileDialog::DontUseNativeDialog, true);
        opaque(files, "QFileDialog");
        QDialog plain;
        opaque(plain, "QDialog");
    }

    // ------------------------------------------------------------------ the theme font

    void fontFileIsRegisteredAndRemembered()
    {
        QVERIFY(CustomFont::currentFamily().isEmpty());
        const QString family = CustomFont::load(m_font);
        QCOMPARE(family, QStringLiteral("ZERO Other"));
        QCOMPARE(CustomFont::currentFamily(), family);
        QVERIFY(CustomFont::isThemeFontFamily(family));
        QVERIFY(!CustomFont::isThemeFontFamily(pixelFamily()));
        QCOMPARE(CustomFont::load(m_font), family);  // same file: same answer
        QVERIFY(fontWarnings().isEmpty());

        // a theme without a font unregisters it; the family stays known as "was a theme font"
        QVERIFY(CustomFont::load(QString()).isEmpty());
        QVERIFY(CustomFont::currentFamily().isEmpty());
        QVERIFY(CustomFont::isThemeFontFamily(family));
    }

    void aLoadedFontFileCanStillBeReplacedAndRemoved()
    {
        // The editor replaces font.<ext> in the theme folder and Delete moves the folder to the Recycle Bin: Windows refuses both for a
        // file that is still open, so the font is read into memory and the file is let go.
        const QString copy = m_dir.filePath(QStringLiteral("in-use.ttf"));
        QVERIFY(QFile::copy(m_font, copy));
        const QString family = CustomFont::load(copy);
        QVERIFY(!family.isEmpty());
        QVERIFY(QFile::rename(copy, copy + QStringLiteral(".bak")));
        QVERIFY(QFile::remove(copy + QStringLiteral(".bak")));
        QCOMPARE(CustomFont::currentFamily(), family);  // the font itself is still registered
    }

    void aFileQtCannotLoadFallsBackWithOneWarning()
    {
        g_warnings.clear();
        const QString junk = m_dir.filePath(QStringLiteral("junk.ttf"));
        QVERIFY(writeFile(junk, QByteArray(300, 'x')));
        QVERIFY(CustomFont::load(junk).isEmpty());
        QCOMPARE(fontWarnings().size(), 1);
        QVERIFY(fontWarnings().first().contains(QStringLiteral("junk.ttf")));
        QVERIFY(CustomFont::load(junk).isEmpty());  // asked again on every theme change: not logged again
        QCOMPARE(fontWarnings().size(), 1);
        QVERIFY(CustomFont::currentFamily().isEmpty());

        // a missing file is the same
        QVERIFY(CustomFont::load(m_dir.filePath(QStringLiteral("gone.otf"))).isEmpty());
        QCOMPARE(fontWarnings().size(), 2);

        // and a good one afterwards works as if nothing happened
        QVERIFY(!CustomFont::load(m_font).isEmpty());
        QCOMPARE(fontWarnings().size(), 2);
    }

    void everyRoleUsesTheThemeFontExceptThePixelRoles()
    {
        QVERIFY(registerPixelFont());
        const QString family = CustomFont::load(m_font);
        QVERIFY(!family.isEmpty());

        ThemeSpec plain = ThemeSpec::obsidian();
        plain.fontFamily = family;
        Theme::setCurrent(plain);
        for (Role r : { Role::Display, Role::Title, Role::Heading, Role::Body, Role::Label, Role::Caption, Role::Overline }) {
            const QStringList families = Style::font(r).families();
            QCOMPARE(families.value(0), family);
            QVERIFY(families.contains(QLatin1String(Tokens::fontFamilyPrimary)));  // the default families stay behind it (missing glyphs)
        }

        ThemeSpec mc = ThemeSpec::minecraft();
        mc.fontFamily = family;
        Theme::setCurrent(mc);
        int pixelRoles = 0, uiRoles = 0;
        for (Role r : { Role::Display, Role::Title, Role::Heading, Role::Body, Role::Label, Role::Caption, Role::Overline }) {
            const QStringList families = Style::font(r).families();
            if (mc.usesPixelFont(r)) {
                ++pixelRoles;
                QCOMPARE(families, QStringList{ pixelFamily() });  // untouched
            } else {
                ++uiRoles;
                QCOMPARE(families.value(0), family);
            }
        }
        QVERIFY(pixelRoles > 0);
        QVERIFY(uiRoles > 0);

        // no theme font (or one that failed to load = empty family): the default families only
        ThemeSpec none = ThemeSpec::obsidian();
        none.fontFamily = CustomFont::load(m_dir.filePath(QStringLiteral("not-there.ttf")));
        Theme::setCurrent(none);
        QCOMPARE(Style::font(Role::Body).families(),
                 (QStringList{ QLatin1String(Tokens::fontFamilyPrimary), QLatin1String(Tokens::fontFamilyFallback) }));
    }

    void widgetsFollowTheThemeFontWhenItChanges()
    {
        QVERIFY(registerPixelFont());
        const QString family = CustomFont::load(m_font);
        ThemeSpec plain = ThemeSpec::obsidian();
        plain.reduceMotion = true;
        Theme::setCurrent(plain);

        QLabel body(QStringLiteral("Body"));
        Style::applyRole(&body, Role::Body);
        QVERIFY(!body.font().families().contains(family));

        ThemeSpec withFont = plain;
        withFont.fontFamily = family;
        Theme::setCurrent(withFont);
        Style::refreshRoleFonts();
        QCOMPARE(body.font().families().value(0), family);

        // the font goes away again: back to the default families, not stuck on the old one
        Theme::setCurrent(plain);
        Style::refreshRoleFonts();
        QVERIFY(!body.font().families().contains(family));
        QCOMPARE(body.font().families().value(0), QLatin1String(Tokens::fontFamilyPrimary));

        // pixel roles of a Minecraft theme keep the pixel font even with a theme font
        ThemeSpec mc = ThemeSpec::minecraft();
        mc.reduceMotion = true;
        mc.fontFamily = family;
        Role pixelRole = Role::Body;
        for (Role r : { Role::Display, Role::Title, Role::Heading, Role::Body, Role::Label, Role::Caption, Role::Overline }) {
            if (mc.usesPixelFont(r)) {
                pixelRole = r;
                break;
            }
        }
        QVERIFY(mc.usesPixelFont(pixelRole));
        QLabel pixel(QStringLiteral("Pixel"));
        Style::applyRole(&pixel, pixelRole);
        Theme::setCurrent(mc);
        Style::refreshRoleFonts();
        QCOMPARE(pixel.font().families(), QStringList{ pixelFamily() });
    }

    // ------------------------------------------------------------------ the launcher icon

    void logoFileFollowsTheThemesOwnIcon()
    {
        const QString mark = QStringLiteral(":/zero/zero-logo.svg");
        const QString pixel = QStringLiteral(":/zero/zero-logo-minecraft.svg");
        QCOMPARE(logoPathFor(ThemeSpec::obsidian()), mark);
        QCOMPARE(logoPathFor(ThemeSpec::minecraft()), pixel);

        const QString icon = m_dir.filePath(QStringLiteral("icon.png"));
        QImage img(40, 40, QImage::Format_ARGB32);
        img.fill(Qt::green);
        QVERIFY(img.save(icon, "PNG"));
        ThemeSpec custom = ThemeSpec::obsidian();
        custom.appIcon = icon;
        QCOMPARE(logoPathFor(custom), icon);

        // wins over the Minecraft pixel mark of its base theme
        ThemeSpec onMinecraft = ThemeSpec::minecraft();
        onMinecraft.baseId = onMinecraft.id;
        onMinecraft.id = QStringLiteral("zero-custom-x");
        onMinecraft.appIcon = icon;
        QCOMPARE(logoPathFor(onMinecraft), icon);

        // a file that is gone: the base theme's mark, never an empty icon
        custom.appIcon = m_dir.filePath(QStringLiteral("deleted.png"));
        QCOMPARE(logoPathFor(custom), mark);
        onMinecraft.appIcon = custom.appIcon;
        QCOMPARE(logoPathFor(onMinecraft), pixel);

        // a Default theme (built-in or custom without an icon) is unchanged
        ThemeSpec none = ThemeSpec::obsidian();
        none.baseId = none.id;
        none.id = QStringLiteral("zero-custom-y");
        QCOMPARE(logoPathFor(none), mark);
        QVERIFY(!QIcon(logoPathFor(custom)).isNull());
    }
};

QTEST_MAIN(ZeroThemeAssetsTest)
#include "ZeroThemeAssets_test.moc"
