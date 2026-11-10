// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroStyle.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStyle>
#include <QWidget>
#include <QtDebug>
#include "ZeroIcons.h"
#include "ZeroPixelFont.h"
#include "ZeroThemeState.h"

namespace Zero {
namespace Style {

namespace {

struct NamedColor {
    const char* name;
    QColor ThemeSpec::*member;
};

const NamedColor kColors[] = {
    { "bg", &ThemeSpec::bg },
    { "surface", &ThemeSpec::surface },
    { "surfaceHover", &ThemeSpec::surfaceHover },
    { "elevated", &ThemeSpec::elevated },
    { "higher", &ThemeSpec::higher },
    { "pressed", &ThemeSpec::pressed },
    { "border", &ThemeSpec::border },
    { "borderStrong", &ThemeSpec::borderStrong },
    { "accent", &ThemeSpec::accent },
    { "accentHover", &ThemeSpec::accentHover },
    { "accentPressed", &ThemeSpec::accentPressed },
    { "onAccent", &ThemeSpec::onAccent },
    { "text", &ThemeSpec::text },
    { "textSecondary", &ThemeSpec::textSecondary },
    { "textTertiary", &ThemeSpec::textTertiary },
    { "success", &ThemeSpec::success },
    { "warning", &ThemeSpec::warning },
    { "danger", &ThemeSpec::danger },
    { "info", &ThemeSpec::info },
    { "accent2", &ThemeSpec::accent2 },
    { "highlight", &ThemeSpec::highlight },
    { "bevelDark", &ThemeSpec::bevelDark },
};

QString hex(const QColor& c)
{
    return c.name(QColor::HexRgb);
}

QString rgba(const QColor& c, double alpha)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(qRound(alpha * 255));
}

QString px(int v)
{
    return QStringLiteral("%1px").arg(v);
}

struct RoleInfo {
    Role role;
    const char* name;
    const char* key;  // token suffix
    Tokens::TypeRole type;
};

const RoleInfo kRoles[] = {
    { Role::Display, "display", "Display", Tokens::display }, { Role::Title, "title", "Title", Tokens::title },
    { Role::Heading, "heading", "Heading", Tokens::heading }, { Role::Body, "body", "Body", Tokens::body },
    { Role::Label, "label", "Label", Tokens::label },         { Role::Caption, "caption", "Caption", Tokens::caption },
    { Role::Overline, "overline", "Overline", Tokens::overline },
};

const RoleInfo& roleInfo(Role r)
{
    for (const auto& i : kRoles)
        if (i.role == r)
            return i;
    return kRoles[3];
}

QString assetDirPath()
{
    return QDir(QDir::tempPath()).filePath(QStringLiteral("zero-launcher-qss"));
}

/// Writes (or reuses) a tinted SVG and returns a quoted url() for it, or "none".
QString imageUrl(const QString& iconName, const QColor& color)
{
    QByteArray svg;
    if (iconName == QLatin1String("radio-dot")) {
        svg = QStringLiteral(
                  "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\"><circle cx=\"12\" "
                  "cy=\"12\" r=\"6\" fill=\"%1\"/></svg>")
                  .arg(hex(color))
                  .toUtf8();
    } else if (iconName == QLatin1String("square-dot")) {
        svg = QStringLiteral(
                  "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\"><rect x=\"7\" "
                  "y=\"7\" width=\"10\" height=\"10\" fill=\"%1\"/></svg>")
                  .arg(hex(color))
                  .toUtf8();
    } else {
        svg = Icons::tintedSvg(iconName, color);
    }
    if (svg.isEmpty()) {
        qWarning() << "ZeroStyle: unknown icon in stylesheet:" << iconName;
        return QStringLiteral("none");
    }
    QDir dir(assetDirPath());
    if (!dir.mkpath(QStringLiteral("."))) {
        qWarning() << "ZeroStyle: cannot create" << dir.path();
        return QStringLiteral("none");
    }
    const QString path = dir.filePath(QStringLiteral("%1-%2.svg").arg(iconName, hex(color).mid(1)));
    QFile existing(path);
    if (!(existing.exists() && existing.size() == svg.size())) {
        QSaveFile out(path);
        if (!out.open(QIODevice::WriteOnly) || out.write(svg) != svg.size() || !out.commit()) {
            qWarning() << "ZeroStyle: cannot write" << path;
            return QStringLiteral("none");
        }
    }
    return QStringLiteral("url(\"%1\")").arg(path);
}

}  // namespace

QString templateSource()
{
    initResources();
    QFile f(QStringLiteral(":/zero/obsidian.qss"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "ZeroStyle: obsidian.qss resource missing";
        return {};
    }
    return QString::fromUtf8(f.readAll());
}

QString overlaySource(const ThemeSpec& spec)
{
    if (spec.id.isEmpty() || spec.id == QLatin1String("obsidian"))  // obsidian.qss is the base template itself
        return {};
    initResources();
    QFile f(QStringLiteral(":/zero/%1.qss").arg(spec.id));
    if (!f.exists() || !f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll());
}

QColor colorByName(const ThemeSpec& spec, const QString& name)
{
    for (const auto& c : kColors)
        if (name == QLatin1String(c.name))
            return spec.*(c.member);
    return {};
}

QHash<QString, QString> tokenMap(const ThemeSpec& spec)
{
    QHash<QString, QString> m;
    for (const auto& c : kColors)
        m.insert(QLatin1String(c.name), hex(spec.*(c.member)));

    // Status tints (12%) and a stronger pressed tint (20%).
    m.insert("success12", rgba(spec.success, Tokens::statusTint));
    m.insert("warning12", rgba(spec.warning, Tokens::statusTint));
    m.insert("danger12", rgba(spec.danger, Tokens::statusTint));
    m.insert("info12", rgba(spec.info, Tokens::statusTint));
    m.insert("danger20", rgba(spec.danger, 0.20));

    m.insert("bevelLight", rgba(Qt::white, Tokens::bevelLightAlpha));

    m.insert("radiusSm", px(spec.radiusSm));
    m.insert("radiusMd", px(spec.radiusMd));
    m.insert("radiusLg", px(spec.radiusLg));
    m.insert("radiusXl", px(spec.radiusXl));

    m.insert("spaceXs", px(Tokens::spaceXs));
    m.insert("spaceSm", px(Tokens::spaceSm));
    m.insert("spaceMd", px(Tokens::spaceMd));
    m.insert("spaceLg", px(Tokens::spaceLg));
    m.insert("spaceXl", px(Tokens::spaceXl));
    m.insert("spaceXxl", px(Tokens::spaceXxl));
    m.insert("spaceXxxl", px(Tokens::spaceXxxl));

    // Control heights are border-box in the spec; qss min-height is the content box (1px borders).
    m.insert("controlInner", px(Tokens::controlHeight - 2));
    m.insert("controlInnerLg", px(Tokens::controlHeightLg - 2));
    // Bevelled buttons: 1 px light top + 3 px dark bottom edge; sunken inputs: 2 px dark top + 1 px bottom edge.
    m.insert("controlInnerBevel", px(Tokens::controlHeight - 4));
    m.insert("controlInnerBevelLg", px(Tokens::controlHeightLg - 4));
    m.insert("controlInnerSunken", px(Tokens::controlHeight - 3));
    m.insert("scrollbarWidth", px(Tokens::scrollbarWidth));
    m.insert("progressHeight", px(Tokens::progressHeight));
    m.insert("badgeHeight", px(Tokens::badgeHeight));
    m.insert("itemPadV", px(spec.itemPaddingV()));

    for (const auto& r : kRoles) {
        m.insert(QStringLiteral("fs%1").arg(QLatin1String(r.key)), px(r.type.px));
        m.insert(QStringLiteral("fw%1").arg(QLatin1String(r.key)), QString::number(r.type.weight));
    }
    return m;
}

QString styleSheet(const ThemeSpec& spec)
{
    const QString overlay = overlaySource(spec);
    return styleSheet(spec, overlay.isEmpty() ? templateSource() : templateSource() + QLatin1Char('\n') + overlay);
}

QString styleSheet(const ThemeSpec& spec, const QString& templateText)
{
    const auto tokens = tokenMap(spec);
    static const QRegularExpression re(QStringLiteral("@([A-Za-z0-9_:\\-]+)@"));

    QString out;
    out.reserve(templateText.size() * 2);
    qsizetype last = 0;
    auto it = re.globalMatch(templateText);
    while (it.hasNext()) {
        const auto match = it.next();
        out += QStringView(templateText).mid(last, match.capturedStart() - last);
        last = match.capturedEnd();

        const QString key = match.captured(1);
        if (key.startsWith(QLatin1String("img:"))) {
            const auto parts = key.split(':');
            const QColor color = parts.size() == 3 ? colorByName(spec, parts[2]) : QColor();
            if (!color.isValid()) {
                qWarning() << "ZeroStyle: bad image placeholder" << key;
                out += match.captured(0);
            } else {
                out += imageUrl(parts[1], color);
            }
        } else if (auto v = tokens.constFind(key); v != tokens.constEnd()) {
            out += *v;
        } else {
            qWarning() << "ZeroStyle: unknown placeholder" << match.captured(0);
            out += match.captured(0);
        }
    }
    out += QStringView(templateText).mid(last);
    return out;
}

QPalette palette(const ThemeSpec& s)
{
    QPalette p;
    p.setColor(QPalette::Window, s.bg);
    p.setColor(QPalette::WindowText, s.text);
    p.setColor(QPalette::Base, s.surface);
    p.setColor(QPalette::AlternateBase, s.bg);
    p.setColor(QPalette::ToolTipBase, s.elevated);
    p.setColor(QPalette::ToolTipText, s.text);
    p.setColor(QPalette::Text, s.text);
    p.setColor(QPalette::Button, s.elevated);
    p.setColor(QPalette::ButtonText, s.text);
    p.setColor(QPalette::BrightText, s.text);
    // Themes with a distinct accent2 (diamond) use it for links; the others keep the info color.
    p.setColor(QPalette::Link, s.accent2 != s.accent ? s.accent2 : s.info);
    p.setColor(QPalette::LinkVisited, s.textSecondary);
    p.setColor(QPalette::Highlight, s.higher);
    p.setColor(QPalette::HighlightedText, s.text);
    p.setColor(QPalette::PlaceholderText, s.textTertiary);
    p.setColor(QPalette::Light, s.borderStrong);
    p.setColor(QPalette::Midlight, s.border);
    p.setColor(QPalette::Mid, s.border);
    p.setColor(QPalette::Dark, s.bg);
    p.setColor(QPalette::Shadow, Qt::black);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    p.setColor(QPalette::Accent, s.accent);
#endif
    for (auto role : { QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::HighlightedText })
        p.setColor(QPalette::Disabled, role, s.textTertiary);
    return p;
}

QFont font(Role role)
{
    const auto& t = roleInfo(role).type;
    QFont f;
    if (Theme::current().usesPixelFont(role) && registerPixelFont()) {
        // One font pixel = em/10: sizes that are a multiple of 10 draw each glyph pixel as a whole number of device pixels.
        f.setFamilies({ pixelFamily() });
        f.setPixelSize(qMax(10, int(qRound(t.px / 10.0)) * 10));
        f.setWeight(QFont::Normal);
        f.setHintingPreference(QFont::PreferNoHinting);
        f.setKerning(false);
        f.setStyleStrategy(QFont::NoAntialias);  // exact pixels, no LCD colour fringes
        if (t.uppercase)
            f.setCapitalization(QFont::AllUppercase);
        return f;
    }
    f.setFamilies({ QLatin1String(Tokens::fontFamilyPrimary), QLatin1String(Tokens::fontFamilyFallback) });
    f.setPixelSize(t.px);
    f.setWeight(static_cast<QFont::Weight>(t.weight));
    if (t.uppercase)
        f.setCapitalization(QFont::AllUppercase);
    if (t.letterSpacingPx > 0)
        f.setLetterSpacing(QFont::AbsoluteSpacing, t.letterSpacingPx);
    return f;
}

QString roleName(Role role)
{
    return QLatin1String(roleInfo(role).name);
}

Role roleFromName(const QString& name)
{
    for (const auto& i : kRoles)
        if (name == QLatin1String(i.name))
            return i.role;
    return Role::Body;
}

void refreshRoleFonts()
{
    const bool pixelReady = registerPixelFont();
    const QString pixel = pixelFamily();
    for (QWidget* w : QApplication::allWidgets()) {
        const QVariant v = w->property("zeroRole");
        if (!v.isValid())
            continue;
        const Role role = roleFromName(v.toString());
        const bool wantsPixel = pixelReady && Theme::current().usesPixelFont(role);
        const QFont current = w->font();
        const bool hasPixel = current.family() == pixel || current.families().contains(pixel);
        // Only widgets that switch between the pixel font and the UI font are touched.
        if (wantsPixel != hasPixel)
            w->setFont(font(role));
    }
}

QPixmap texture(const ThemeSpec& spec)
{
    return texture(spec, spec.bg);
}

QPixmap texture(const ThemeSpec&, const QColor& base)
{
    static QHash<QRgb, QPixmap> cache;
    if (auto it = cache.constFind(base.rgba()); it != cache.constEnd())
        return *it;

    constexpr int kTile = 64, kBlock = 4, kBlocks = kTile / kBlock;
    QImage img(kTile, kTile, QImage::Format_RGB32);
    quint32 state = 0x5EED1234u;  // seeded LCG: the same tile on every run and machine
    auto next = [&state]() {
        state = state * 1664525u + 1013904223u;
        return (state >> 8) & 0xFFFF;
    };
    for (int by = 0; by < kBlocks; ++by) {
        for (int bx = 0; bx < kBlocks; ++bx) {
            // +-1.6 % of the full luminance range (about +-4 of 255): visible as stone grain, quiet behind content.
            // (A multiplicative percentage would be under one level on these dark colors.)
            const double d = (next() / 65535.0 * 2.0 - 1.0) * 0.016;
            const QColor c = QColor::fromRgbF(qBound(0.0, base.redF() + d, 1.0), qBound(0.0, base.greenF() + d, 1.0),
                                              qBound(0.0, base.blueF() + d, 1.0));
            for (int y = 0; y < kBlock; ++y)
                for (int x = 0; x < kBlock; ++x)
                    img.setPixelColor(bx * kBlock + x, by * kBlock + y, c);
        }
    }
    const QPixmap pm = QPixmap::fromImage(img);
    cache.insert(base.rgba(), pm);
    return pm;
}

void setProperty(QWidget* widget, const char* name, const QVariant& value)
{
    widget->setProperty(name, value);
    if (auto* st = widget->style()) {
        st->unpolish(widget);
        st->polish(widget);
    }
    widget->update();
}

void applyRole(QWidget* widget, Role role)
{
    widget->setFont(font(role));
    setProperty(widget, "zeroRole", roleName(role));
}

}  // namespace Style
}  // namespace Zero
