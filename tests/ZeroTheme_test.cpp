// SPDX-License-Identifier: GPL-3.0-only
// Validates theme engine v2: both built-in specs (tokens, contrast, stylesheet), panel shapes, the stone texture,
// pixel-role fonts and the ITheme wiring.
#include <QApplication>
#include <QLabel>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <QTest>
#include <cmath>

#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroPixelFont.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroTheme.h"
#include "ui/zero/ZeroThemeState.h"

namespace {

QStringList g_messages;
QtMessageHandler g_previous = nullptr;

void captureHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    g_messages << msg;
    if (g_previous)
        g_previous(type, ctx, msg);
}

bool isStyleSheetProblem(const QString& m)
{
    return m.contains("Could not parse") || m.contains("Unknown property") || m.contains("Unknown pseudo") ||
           m.contains("Unknown sub-control") || m.contains("ZeroStyle:");
}

double luminance(const QColor& c)
{
    auto lin = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF()) + 0.0722 * lin(c.blueF());
}

double contrast(const QColor& a, const QColor& b)
{
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

}  // namespace

class ZeroThemeTest : public QObject {
    Q_OBJECT

    static QList<Zero::ThemeSpec> specs() { return { Zero::ThemeSpec::obsidian(), Zero::ThemeSpec::minecraft() }; }

   private slots:
    void initTestCase() { g_previous = qInstallMessageHandler(captureHandler); }
    void cleanupTestCase()
    {
        qInstallMessageHandler(g_previous);
        Zero::Theme::setCurrent(Zero::ThemeSpec::obsidian());
    }

    void obsidianSpecKeepsTodaysValues()
    {
        const auto s = Zero::ThemeSpec::obsidian();
        QCOMPARE(s.id, QString("obsidian"));
        QCOMPARE(s.accent2, s.accent);
        QCOMPARE(s.highlight, s.warning);
        QCOMPARE(s.bevelDark, s.bg);
        QCOMPARE(s.radiusSm, 4);
        QCOMPARE(s.radiusMd, 6);
        QCOMPARE(s.radiusLg, 10);
        QCOMPARE(s.radiusXl, 14);
        QVERIFY(s.corners == Zero::Corners::Rounded);
        QVERIFY(!s.bevel);
        QVERIFY(!s.texture);
        QCOMPARE(s.pixelRoles, quint32(0));
    }

    void minecraftSpecMatchesDesignDoc()
    {
        const auto s = Zero::ThemeSpec::minecraft();
        QCOMPARE(s.id, QString("minecraft"));
        const QList<QPair<QString, QString>> expected = {
            { "bg", "#16171a" },      { "surface", "#1f2125" },  { "surfaceHover", "#25282d" }, { "elevated", "#2b2e33" },
            { "higher", "#33373d" },  { "pressed", "#3a3f46" },  { "border", "#393d44" },       { "borderStrong", "#4a4f57" },
            { "bevelDark", "#0d0e10" }, { "accent", "#67b84a" }, { "accentHover", "#78c95a" },  { "accentPressed", "#4f9a35" },
            { "onAccent", "#0e1a09" }, { "accent2", "#4fd0d6" }, { "highlight", "#e6b83e" },    { "text", "#efebe0" },
            { "textSecondary", "#a8a397" }, { "textTertiary", "#757067" }, { "success", "#3cc77a" }, { "warning", "#e6b83e" },
            { "danger", "#e25a4f" },  { "info", "#5db3e8" },
        };
        for (const auto& [name, hex] : expected) {
            const QColor c = Zero::Style::colorByName(s, name);
            QVERIFY2(c.isValid(), qPrintable(name));
            QCOMPARE(c.name(), hex);
        }
        QCOMPARE(s.radiusSm, 2);
        QCOMPARE(s.radiusMd, 2);
        QCOMPARE(s.radiusLg, 3);
        QCOMPARE(s.radiusXl, 4);
        QVERIFY(s.corners == Zero::Corners::Notched);
        QVERIFY(s.bevel);
        QVERIFY(s.texture);
        QVERIFY(s.usesPixelFont(Zero::Role::Title));
        QVERIFY(s.usesPixelFont(Zero::Role::Overline));
        QVERIFY(!s.usesPixelFont(Zero::Role::Body));
        QVERIFY(!s.usesPixelFont(Zero::Role::Display));
    }

    void fullStyleSheetHasNoPlaceholdersOrQtWarnings()
    {
        for (const auto& spec : specs()) {
            g_messages.clear();
            const QString css = Zero::Style::styleSheet(spec);
            QVERIFY2(css.size() > 5000, qPrintable(spec.id));
            const QRegularExpression leftover("@[A-Za-z]");
            QVERIFY2(!css.contains(leftover), qPrintable(spec.id + ": " + css.mid(css.indexOf(leftover), 80)));
            for (const auto& m : std::as_const(g_messages))
                QVERIFY2(!isStyleSheetProblem(m), qPrintable(spec.id + ": " + m));
        }
        // The Minecraft sheet is the base sheet plus its overlay; Obsidian has no overlay.
        QVERIFY(Zero::Style::overlaySource(Zero::ThemeSpec::obsidian()).isEmpty());
        QVERIFY(!Zero::Style::overlaySource(Zero::ThemeSpec::minecraft()).isEmpty());
        QVERIFY(Zero::Style::styleSheet(Zero::ThemeSpec::minecraft()).size() > Zero::Style::styleSheet(Zero::ThemeSpec::obsidian()).size());
    }

    void contrastMeetsTheBar()
    {
        for (const auto& s : specs()) {
            QVERIFY2(contrast(s.text, s.bg) >= 7.0, qPrintable(s.id + " text/bg"));
            QVERIFY2(contrast(s.textSecondary, s.surface) >= 4.5, qPrintable(s.id + " textSecondary/surface"));
            QVERIFY2(contrast(s.onAccent, s.accent) >= 4.5, qPrintable(s.id + " onAccent/accent"));
        }
    }

    void obsidianTokenMapIsUnchanged()
    {
        const auto m = Zero::Style::tokenMap(Zero::ThemeSpec::obsidian());
        const QHash<QString, QString> expected = {
            { "bg", "#101010" },        { "surface", "#191919" },     { "elevated", "#222222" },   { "border", "#303030" },
            { "accent", "#d4d4d4" },    { "onAccent", "#101010" },    { "text", "#f5f5f5" },       { "textSecondary", "#929292" },
            { "success", "#7bc59a" },   { "danger", "#e07a7a" },     { "radiusSm", "4px" },       { "radiusMd", "6px" },
            { "radiusLg", "10px" },     { "radiusXl", "14px" },       { "spaceMd", "12px" },       { "controlInner", "30px" },
            { "controlInnerLg", "38px" }, { "scrollbarWidth", "8px" }, { "progressHeight", "4px" }, { "badgeHeight", "20px" },
            { "itemPadV", "5px" },      { "fsTitle", "20px" },        { "fwTitle", "600" },        { "fsOverline", "10px" },
            { "success12", "rgba(123, 197, 154, 31)" },               { "danger20", "rgba(224, 122, 122, 51)" },
        };
        for (auto it = expected.cbegin(); it != expected.cend(); ++it)
            QCOMPARE(m.value(it.key()), it.value());
        // New keys exist for both specs.
        for (const auto& s : specs())
            for (const char* key : { "accent2", "highlight", "bevelDark", "bevelLight" })
                QVERIFY2(Zero::Style::tokenMap(s).contains(key), key);
        QCOMPARE(Zero::Style::tokenMap(Zero::ThemeSpec::minecraft()).value("radiusLg"), QString("3px"));
        QCOMPARE(Zero::Style::tokenMap(Zero::ThemeSpec::minecraft()).value("bevelLight"), QString("rgba(255, 255, 255, 18)"));
    }

    void panelPathFollowsCornerStyle()
    {
        const QRectF r(10.5, 10.5, 60, 40);
        const auto rounded = Zero::Paint::panelPath(r, 10, Zero::Corners::Rounded);
        const auto notched = Zero::Paint::panelPath(r, 3, Zero::Corners::Notched);
        QCOMPARE(rounded.boundingRect(), r);
        QCOMPARE(notched.boundingRect(), r);
        // A 2 px square is cut from every corner of the notched panel; its edges stay straight.
        for (const QPointF c : { r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight() }) {
            const QPointF inward((c.x() < r.center().x() ? 1 : -1), (c.y() < r.center().y() ? 1 : -1));
            QVERIFY(!notched.contains(c + inward * 0.5));
            QVERIFY(!notched.contains(c + inward * 1.5));
            QVERIFY(notched.contains(c + inward * 2.5));
            QVERIFY(notched.contains(c + QPointF(inward.x() * 5, inward.y() * 2.5)));  // along the top/bottom edge, past the step
        }
        QVERIFY(notched.contains(r.center()));
        // A zero radius means no step at all.
        QCOMPARE(Zero::Paint::panelPath(r, 0, Zero::Corners::Notched).boundingRect(), r);
        QVERIFY(Zero::Paint::panelPath(r, 0, Zero::Corners::Notched).contains(r.topLeft() + QPointF(0.5, 0.5)));
        // The two-argument form follows the active spec.
        Zero::Theme::setCurrent(Zero::ThemeSpec::minecraft());
        QVERIFY(!Zero::Paint::panelPath(r, 3).contains(r.topLeft() + QPointF(0.5, 0.5)));
        Zero::Theme::setCurrent(Zero::ThemeSpec::obsidian());
        QVERIFY(!Zero::Paint::panelPath(r, 10).contains(r.topLeft() + QPointF(0.5, 0.5)));  // rounded corners are cut too
        QVERIFY(Zero::Paint::panelPath(r, 10).contains(r.topLeft() + QPointF(4, 4)));
    }

    void textureIsDeterministicAndSubtle()
    {
        const auto spec = Zero::ThemeSpec::minecraft();
        const QImage a = Zero::Style::texture(spec).toImage();
        const QImage b = Zero::Style::texture(spec).toImage();
        QCOMPARE(a.size(), QSize(64, 64));
        QCOMPARE(a, b);
        // 4 px blocks, within +-3 % of the luminance range around bg, and not flat.
        bool varies = false;
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const QColor c = a.pixelColor(x, y);
                QCOMPARE(c, a.pixelColor((x / 4) * 4, (y / 4) * 4));
                for (auto [got, base] : { std::pair{ c.red(), spec.bg.red() }, std::pair{ c.green(), spec.bg.green() },
                                          std::pair{ c.blue(), spec.bg.blue() } })
                    QVERIFY(std::abs(got - base) <= 9);  // +-3 % of 255, plus rounding
                varies = varies || c != a.pixelColor(0, 0);
            }
        QVERIFY(varies);
        // A different base gives a different (cached) tile of the same pattern.
        QVERIFY(Zero::Style::texture(spec, spec.surface).toImage() != a);
    }

    void pixelRolesSwitchFontWithTheSpec()
    {
        QVERIFY(Zero::registerPixelFont());
        QLabel title, body;
        Zero::Theme::setCurrent(Zero::ThemeSpec::obsidian());
        Zero::Style::applyRole(&title, Zero::Role::Title);
        Zero::Style::applyRole(&body, Zero::Role::Body);
        QCOMPARE(title.font().families().value(0), QString("Segoe UI Variable Text"));

        Zero::Theme::setCurrent(Zero::ThemeSpec::minecraft());
        Zero::Style::refreshRoleFonts();  // ZeroTheme connects this to Theme::changed
        QCOMPARE(title.font().family(), Zero::pixelFamily());
        QCOMPARE(title.font().pixelSize(), 20);
        QCOMPARE(Zero::Style::font(Zero::Role::Overline).pixelSize(), 10);
        QCOMPARE(Zero::Style::font(Zero::Role::Overline).capitalization(), QFont::AllUppercase);
        QCOMPARE(Zero::Style::font(Zero::Role::Heading).families().value(0), QString("Segoe UI Variable Text"));
        QCOMPARE(body.font().families().value(0), QString("Segoe UI Variable Text"));

        Zero::Theme::setCurrent(Zero::ThemeSpec::obsidian());
        Zero::Style::refreshRoleFonts();
        QCOMPARE(title.font().families().value(0), QString("Segoe UI Variable Text"));
    }

    void themesRegisterUnderZeroIds()
    {
        QVERIFY(Zero::isZeroThemeId(Zero::ZeroTheme::kId));
        QVERIFY(Zero::isZeroThemeId(Zero::ZeroMinecraftTheme::kId));
        QVERIFY(!Zero::isZeroThemeId("fusion"));
        Zero::ZeroMinecraftTheme mc;
        QCOMPARE(mc.id(), QString("zero-minecraft"));
        QCOMPARE(mc.name(), QString("Minecraft"));
        QVERIFY(mc.tooltip().contains("Overworld stone"));
        Zero::ZeroTheme ob;
        QCOMPARE(ob.id(), QString("zero-obsidian"));
    }

    void bothThemesRenderAWidgetTree()
    {
        for (const auto& spec : specs()) {
            Zero::Theme::setCurrent(spec);
            g_messages.clear();
            qApp->setStyleSheet(Zero::Style::styleSheet(spec));
            QWidget w;
            auto* l = new QVBoxLayout(&w);
            auto* primary = new QPushButton("Play");
            primary->setProperty("zeroVariant", "primary");
            auto* secondary = new QPushButton("Cancel");
            secondary->setProperty("zeroVariant", "secondary");
            l->addWidget(primary);
            l->addWidget(secondary);
            w.ensurePolished();
            w.resize(w.sizeHint());
            QVERIFY(!w.grab().isNull());
            // The border-box height of a control stays 32 px in both themes (bevel does not change metrics).
            QCOMPARE(primary->sizeHint().height(), 32);
            QCOMPARE(secondary->sizeHint().height(), 32);
            for (const auto& m : std::as_const(g_messages))
                QVERIFY2(!isStyleSheetProblem(m), qPrintable(spec.id + ": " + m));
        }
        qApp->setStyleSheet({});
    }
};

QTEST_MAIN(ZeroThemeTest)

#include "ZeroTheme_test.moc"
