// SPDX-License-Identifier: GPL-3.0-only
// Validates the ZERO design system: stylesheet generation, Qt parse warnings, palette tokens, icons, key metrics,
// and renders the component gallery to build/zero-gallery.png for visual review.
#include <QApplication>
#include <QDir>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyleFactory>
#include <QTest>
#include <QToolTip>

#include "ui/zero/ZeroGallery.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroNavRail.h"
#include "ui/zero/ZeroSpinner.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroToast.h"

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

}  // namespace

class ZeroStyleTest : public QObject {
    Q_OBJECT

    Zero::ThemeSpec m_spec;

   private slots:
    void initTestCase()
    {
        m_spec = Zero::ThemeSpec::obsidian();
        m_spec.reduceMotion = true;  // deterministic screenshot: no running animations
        Zero::Theme::setCurrent(m_spec);
        g_previous = qInstallMessageHandler(captureHandler);
    }

    void cleanupTestCase() { qInstallMessageHandler(g_previous); }

    void tokensMatchDesignDoc()
    {
        // Hex values from docs/zero/DESIGN.md; fails if the tokens drift from the spec.
        const QList<QPair<QString, QString>> expected = {
            { "bg", "#101010" },          { "surface", "#191919" },   { "surfaceHover", "#1f1f1f" }, { "elevated", "#222222" },
            { "higher", "#292929" },      { "pressed", "#2e2e2e" },   { "border", "#303030" },       { "borderStrong", "#3a3a3a" },
            { "accent", "#d4d4d4" },      { "accentHover", "#e8e8e8" }, { "accentPressed", "#bdbdbd" }, { "onAccent", "#101010" },
            { "text", "#f5f5f5" },        { "textSecondary", "#929292" }, { "textTertiary", "#6b6b6b" }, { "success", "#7bc59a" },
            { "warning", "#d9b36c" },     { "danger", "#e07a7a" },    { "info", "#8ab4d9" },
        };
        for (const auto& [name, hex] : expected) {
            const QColor c = Zero::Style::colorByName(Zero::ThemeSpec::obsidian(), name);
            QVERIFY2(c.isValid(), qPrintable(name));
            QCOMPARE(c.name(), hex);
        }
        QCOMPARE(Zero::Tokens::radiusLg, 10);
        QCOMPARE(Zero::Tokens::durHover, 120);
    }

    void styleSheetHasNoPlaceholders()
    {
        g_messages.clear();
        const QString css = Zero::Style::styleSheet(m_spec);
        QVERIFY(css.size() > 5000);
        const QRegularExpression leftover("@[A-Za-z]");
        QVERIFY2(!css.contains(leftover), qPrintable(css.mid(css.indexOf(leftover), 80)));
        for (const auto& m : std::as_const(g_messages))
            QVERIFY2(!isStyleSheetProblem(m), qPrintable(m));
        // Every token the sheet needs must be known: an unknown one is reported and left in place.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("unknown placeholder"));
        const QString bad = Zero::Style::styleSheet(m_spec, "a { color: @doesNotExist@; }");
        QVERIFY(bad.contains("@doesNotExist@"));
    }

    void paletteMatchesTokens()
    {
        const QPalette p = Zero::Style::palette(m_spec);
        QCOMPARE(p.color(QPalette::Window), m_spec.bg);
        QCOMPARE(p.color(QPalette::WindowText), m_spec.text);
        QCOMPARE(p.color(QPalette::Base), m_spec.surface);
        QCOMPARE(p.color(QPalette::AlternateBase), m_spec.bg);
        QCOMPARE(p.color(QPalette::Text), m_spec.text);
        QCOMPARE(p.color(QPalette::Button), m_spec.elevated);
        QCOMPARE(p.color(QPalette::ButtonText), m_spec.text);
        QCOMPARE(p.color(QPalette::ToolTipBase), m_spec.elevated);
        QCOMPARE(p.color(QPalette::ToolTipText), m_spec.text);
        QCOMPARE(p.color(QPalette::Highlight), m_spec.higher);
        QCOMPARE(p.color(QPalette::HighlightedText), m_spec.text);
        QCOMPARE(p.color(QPalette::PlaceholderText), m_spec.textTertiary);
        QCOMPARE(p.color(QPalette::Link), m_spec.info);
        QCOMPARE(p.color(QPalette::Accent), m_spec.accent);
        QCOMPARE(p.color(QPalette::Disabled, QPalette::Text), m_spec.textTertiary);
    }

    void fontsFollowTypeRoles()
    {
        QCOMPARE(Zero::Style::font(Zero::Role::Body).pixelSize(), 13);
        QCOMPARE(Zero::Style::font(Zero::Role::Display).pixelSize(), 28);
        QCOMPARE(int(Zero::Style::font(Zero::Role::Title).weight()), 600);
        const QFont overline = Zero::Style::font(Zero::Role::Overline);
        QCOMPARE(overline.pixelSize(), 10);
        QCOMPARE(overline.capitalization(), QFont::AllUppercase);
        QVERIFY(qAbs(overline.letterSpacing() - 0.8) < 0.05);
        QCOMPARE(Zero::Style::font(Zero::Role::Body).families().value(0), QString("Segoe UI Variable Text"));
    }

    void iconsRenderAndCache()
    {
        const QStringList required = { "house", "library-big", "compass", "earth", "wrench", "download", "palette", "settings", "search",
                                       "play", "square", "plus", "star", "folder-open", "ellipsis", "pencil", "trash-2", "copy", "x",
                                       "check", "triangle-alert", "info", "circle-check", "circle-x", "layout-grid", "list", "list-filter",
                                       "clock", "user", "chevron-down", "chevron-right", "refresh-cw", "archive", "hard-drive", "cpu",
                                       "stethoscope", "terminal", "command", "package", "box", "image" };
        const auto names = Zero::Icons::availableNames();
        for (const auto& n : required)
            QVERIFY2(names.contains(n), qPrintable(n));

        const QColor tint(0x12, 0x34, 0x56);
        for (const auto& n : required) {
            const QIcon icon = Zero::Icons::icon(n, tint);
            QVERIFY2(!icon.isNull(), qPrintable(n));
            const QImage img = icon.pixmap(QSize(32, 32), 2.0).toImage();
            QCOMPARE(img.size(), QSize(64, 64));
            bool tinted = false;
            for (int y = 0; y < img.height() && !tinted; ++y)
                for (int x = 0; x < img.width(); ++x) {
                    const QColor c = img.pixelColor(x, y);
                    if (c.alpha() > 200 && c.red() == 0x12 && c.green() == 0x34 && c.blue() == 0x56) {
                        tinted = true;
                        break;
                    }
                }
            QVERIFY2(tinted, qPrintable(n + ": no pixels in the requested tint"));
        }
        QCOMPARE(Zero::Icons::icon("house", tint).cacheKey(), Zero::Icons::icon("house", tint).cacheKey());
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("unknown icon"));
        QVERIFY(Zero::Icons::icon("no-such-icon", tint).isNull());
    }

    void galleryRendersWithoutStyleSheetWarnings()
    {
        QApplication::setStyle(QStyleFactory::create("Fusion"));
        QApplication::setPalette(Zero::Style::palette(m_spec));
        QApplication::setFont(Zero::Style::font(Zero::Role::Body));
        g_messages.clear();
        qApp->setStyleSheet(Zero::Style::styleSheet(m_spec));

        Zero::ZeroGallery gallery;
        gallery.resize(1200, 900);
        gallery.ensurePolished();
        QVERIFY2(gallery.minimumSizeHint().width() <= 1200 && gallery.minimumSizeHint().height() <= 900,
                 qPrintable(QString("gallery needs %1x%2").arg(gallery.minimumSizeHint().width()).arg(gallery.minimumSizeHint().height())));

        // Key metrics from DESIGN.md.
        auto buttonHeight = [&](const QString& variant, const QString& size) {
            for (auto* b : gallery.findChildren<QPushButton*>())
                if (b->property("zeroVariant").toString() == variant && b->property("zeroSize").toString() == size && b->isEnabled()) {
                    b->ensurePolished();
                    return b->sizeHint().height();
                }
            return -1;
        };
        QCOMPARE(buttonHeight("primary", ""), 32);
        QCOMPARE(buttonHeight("secondary", ""), 32);
        QCOMPARE(buttonHeight("ghost", ""), 32);
        QCOMPARE(buttonHeight("primary", "lg"), 40);
        QCOMPARE(gallery.navRail()->width(), 220);

        // Spinner: runs only while visible and motion is enabled.
        Zero::ZeroSpinner spinner;
        Zero::Theme::setReduceMotion(false);
        QVERIFY(!spinner.isAnimating());
        spinner.show();
        QVERIFY(spinner.isAnimating());
        spinner.hide();
        QVERIFY(!spinner.isAnimating());
        Zero::Theme::setReduceMotion(true);
        spinner.show();
        QVERIFY(!spinner.isAnimating());
        spinner.hide();

        // Toast stack (instant fade because motion is reduced).
        QCOMPARE(Zero::ZeroToast::count(&gallery), 0);
        bool acted = false;
        gallery.show();
        auto* t1 = Zero::ZeroToast::show(&gallery, "Instance created", Zero::ZeroToast::Kind::Success);
        auto* t2 = Zero::ZeroToast::show(&gallery, "Download failed", Zero::ZeroToast::Kind::Error, "Retry", [&] { acted = true; });
        QCOMPARE(Zero::ZeroToast::count(&gallery), 2);
        QVERIFY(t1->geometry().bottom() > t2->geometry().bottom());  // newer toast stacks above
        QVERIFY(t1->geometry().right() <= gallery.width() - Zero::Tokens::spaceXl);

        QPixmap shot = gallery.grab();

        // Hover can only be produced by a real pointer: move it over each "Hover" sample, check the :hover colors
        // from the stylesheet are applied, and composite the hovered look into the screenshot.
        // Opt-in (ZERO_TEST_REAL_POINTER=1): it moves the user's actual cursor and is flaky while someone uses the PC.
        if (qEnvironmentVariableIsSet("ZERO_TEST_REAL_POINTER")) {
            QPainter painter(&shot);
            for (auto* b : gallery.hoverSamples()) {
                const QString variant = b->property("zeroVariant").toString();
                QColor expected;
                if (variant == "primary")
                    expected = m_spec.accentHover;
                else if (variant == "secondary")
                    expected = m_spec.higher;
                else if (variant == "ghost")
                    expected = m_spec.surfaceHover;
                else  // danger: 12% danger tint over the card surface
                    expected = Zero::mix(m_spec.surface, m_spec.danger, Zero::Tokens::statusTint);

                const QPoint probe(5, b->height() / 2);
                QImage img;
                QColor px;
                bool ok = false;
                QTest::mouseMove(gallery.window()->windowHandle() ? &gallery : &gallery, QPoint(1, 1));  // leave previous sample
                for (int attempt = 0; attempt < 60 && !ok; ++attempt) {
                    QTest::mouseMove(b, probe + QPoint(attempt % 2, 0));
                    QTest::qWait(30);
                    img = b->grab().toImage();
                    px = img.pixelColor(probe);
                    if (px.alpha() < 255)  // translucent tint: composite over the card surface like the real window does
                        px = Zero::mix(m_spec.surface, QColor(px.rgb()), px.alphaF());
                    ok = qAbs(px.red() - expected.red()) <= 2 && qAbs(px.green() - expected.green()) <= 2 && qAbs(px.blue() - expected.blue()) <= 2;
                }
                QVERIFY2(ok, qPrintable(QString("%1 hover pixel %2, expected %3").arg(variant, px.name(), expected.name())));
                painter.drawImage(b->mapTo(&gallery, QPoint(0, 0)), img);
            }
        }
        const QString out = QDir(QCoreApplication::applicationDirPath()).filePath("zero-gallery.png");
        QVERIFY(shot.save(out));
        QCOMPARE(shot.size(), QSize(1200, 900) * shot.devicePixelRatio());
        qInfo() << "Gallery screenshot:" << out;

        t2->dismiss();
        QTRY_COMPARE(Zero::ZeroToast::count(&gallery), 1);
        QVERIFY(!acted);
        gallery.hide();

        // A styled menu, rendered on its own (popups are separate windows and not part of the gallery grab).
        QMenu menu;
        menu.addAction(Zero::Icons::icon("play", m_spec.text), "Launch");
        menu.addAction(Zero::Icons::icon("pencil", m_spec.text), "Edit");
        auto* sub = menu.addMenu(Zero::Icons::icon("folder-open", m_spec.text), "Open folder");
        sub->addAction("Instance");
        auto* checked = menu.addAction("Pinned");
        checked->setCheckable(true);
        checked->setChecked(true);
        menu.addSeparator();
        menu.addAction(Zero::Icons::icon("trash-2", m_spec.danger), "Delete");
        menu.addAction("Disabled")->setEnabled(false);
        menu.ensurePolished();
        menu.resize(menu.sizeHint());
        QVERIFY(menu.grab().save(QDir(QCoreApplication::applicationDirPath()).filePath("zero-menu.png")));

        for (const auto& m : std::as_const(g_messages))
            QVERIFY2(!isStyleSheetProblem(m), qPrintable(m));
    }
};

QTEST_MAIN(ZeroStyleTest)

#include "ZeroStyle_test.moc"
