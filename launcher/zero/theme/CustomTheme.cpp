// SPDX-License-Identifier: GPL-3.0-only
#include "CustomTheme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Zero::Custom {

namespace {

struct NamedColor {
    const char* name;
    QColor ThemeSpec::*member;
};

// The ThemeSpec color members an `overrides` entry may name.
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

/// "#RRGGBB" only (anything else, including named colors and alpha forms, is invalid).
QColor parseColor(const QJsonValue& v)
{
    if (!v.isString())
        return {};
    static const QRegularExpression re(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    const QString s = v.toString();
    return re.match(s).hasMatch() ? QColor(s) : QColor();
}

QString colorToString(const QColor& c)
{
    return c.name(QColor::HexRgb).toUpper();
}

double clamp(double v, double lo, double hi)
{
    return std::max(lo, std::min(hi, v));
}

double linear(double v)
{
    return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

double luminance(const QColor& c)
{
    return 0.2126 * linear(c.redF()) + 0.7152 * linear(c.greenF()) + 0.0722 * linear(c.blueF());
}

/// Plain file name: no separator, no "..", not hidden-relative.
bool isPlainFileName(const QString& n)
{
    return !n.isEmpty() && n.size() <= 128 && n != QLatin1String(".") && n != QLatin1String("..") && !n.contains(QLatin1Char('/')) &&
           !n.contains(QLatin1Char('\\')) && !n.contains(QLatin1Char(':')) && !n.contains(QChar(0)) && !n.contains(QLatin1String(".."));
}

/// A plain file name whose extension is one of `extensions` (case-insensitive).
bool isPlainFileWith(const QString& name, const QStringList& extensions)
{
    return isPlainFileName(name) && extensions.contains(QFileInfo(name).suffix().toLower());
}

QColor withLightness(const QColor& c, double delta)
{
    const QColor hsl = c.toHsl();
    const double h = hsl.hslHueF() < 0 ? 0.0 : hsl.hslHueF();
    return QColor::fromHslF(h, hsl.hslSaturationF(), clamp(hsl.lightnessF() + delta, 0.0, 1.0));
}

QColor opaqueMix(const QColor& a, const QColor& b, double t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

QString kindToString(BackgroundKind k)
{
    switch (k) {
        case BackgroundKind::Image:
            return QStringLiteral("image");
        case BackgroundKind::Video:
            return QStringLiteral("video");
        default:
            return QStringLiteral("none");
    }
}

}  // namespace

// ---------------------------------------------------------------- base

QStringList iconExtensions()
{
    return { QStringLiteral("png"), QStringLiteral("svg"), QStringLiteral("ico") };
}

QStringList fontExtensions()
{
    return { QStringLiteral("ttf"), QStringLiteral("otf") };
}

QString baseToString(Base base)
{
    return base == Base::Minecraft ? QStringLiteral("minecraft") : QStringLiteral("obsidian");
}

Base baseFromString(const QString& s)
{
    return s == QLatin1String("minecraft") ? Base::Minecraft : Base::Obsidian;
}

ThemeSpec baseSpec(Base base)
{
    return base == Base::Minecraft ? ThemeSpec::minecraft() : ThemeSpec::obsidian();
}

QStringList soundEventIds()
{
    return { QStringLiteral("click"),   QStringLiteral("toggle-on"), QStringLiteral("toggle-off"), QStringLiteral("navigate"),
             QStringLiteral("success"), QStringLiteral("error"),     QStringLiteral("launch") };
}

QStringList soundSetIds()
{
    return { QStringLiteral("retro"), QStringLiteral("soft"), QStringLiteral("none") };
}

QString soundFileName(const QString& event)
{
    return soundEventIds().contains(event) ? QStringLiteral("sound-") + event + QStringLiteral(".wav") : QString();
}

QString soundEventOf(const QString& name)
{
    for (const QString& event : soundEventIds())
        if (name == soundFileName(event))
            return event;
    return {};
}

bool isSoundFileName(const QString& name)
{
    return !soundEventOf(name).isEmpty();
}

QStringList overrideKeys()
{
    QStringList keys;
    for (const auto& c : kColors)
        keys << QLatin1String(c.name);
    return keys;
}

Definition Definition::fromBase(Base base, const QString& name)
{
    const ThemeSpec s = baseSpec(base);
    Definition d;
    d.name = name;
    d.base = base;
    d.accent = s.accent;
    d.background = s.bg;
    d.text = s.text;
    d.surface = s.surface;
    d.corners = base == Base::Minecraft ? CornerStyle::Square : CornerStyle::Rounded;
    return d;
}

// ---------------------------------------------------------------- JSON

Result<Definition> parseDefinition(const QByteArray& json)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError)
        return std::unexpected{ QObject::tr("The theme file is not valid JSON (%1).").arg(err.errorString()) };
    if (!doc.isObject())
        return std::unexpected{ QObject::tr("The theme file is not a theme definition.") };
    const QJsonObject root = doc.object();

    const QJsonValue format = root.value(QStringLiteral("format"));
    if (!format.isDouble())
        return std::unexpected{ QObject::tr("The theme file has no format version.") };
    const int formatVersion = format.toInt(-1);
    if (formatVersion > kFormat)
        return std::unexpected{ QObject::tr("The theme file was made by a newer version of ZERO (format %1).").arg(formatVersion) };
    if (formatVersion != kFormat)
        return std::unexpected{ QObject::tr("The theme file has an unsupported format (%1).").arg(formatVersion) };

    Definition d;
    d.name = root.value(QStringLiteral("name")).toString().simplified();
    if (d.name.isEmpty())
        d.name = QObject::tr("Custom theme");
    d.base = baseFromString(root.value(QStringLiteral("base")).toString());

    const QJsonObject colors = root.value(QStringLiteral("colors")).toObject();
    d.accent = parseColor(colors.value(QStringLiteral("accent")));
    d.background = parseColor(colors.value(QStringLiteral("background")));
    d.text = parseColor(colors.value(QStringLiteral("text")));
    d.surface = parseColor(colors.value(QStringLiteral("surface")));

    const QJsonObject overrides = root.value(QStringLiteral("overrides")).toObject();
    const QStringList keys = overrideKeys();
    for (auto it = overrides.begin(); it != overrides.end(); ++it) {
        const QColor c = parseColor(it.value());
        if (c.isValid() && keys.contains(it.key()))  // unknown keys are ignored
            d.overrides.insert(it.key(), c);
    }

    const QString corners = root.value(QStringLiteral("corners")).toString();
    if (corners == QLatin1String("square"))
        d.corners = CornerStyle::Square;
    else if (corners == QLatin1String("rounded"))
        d.corners = CornerStyle::Rounded;
    else
        d.corners = d.base == Base::Minecraft ? CornerStyle::Square : CornerStyle::Rounded;

    const QJsonObject bg = root.value(QStringLiteral("background")).toObject();
    const QString kind = bg.value(QStringLiteral("kind")).toString();
    const QString file = bg.value(QStringLiteral("file")).toString();
    if ((kind == QLatin1String("image") || kind == QLatin1String("video")) && isPlainFileName(file)) {
        d.backgroundKind = kind == QLatin1String("video") ? BackgroundKind::Video : BackgroundKind::Image;
        d.backgroundFile = file;
    }
    d.sound = bg.value(QStringLiteral("sound")).toBool(false);
    const QJsonValue volume = bg.value(QStringLiteral("volume"));
    d.volume = volume.isDouble() ? clamp(volume.toDouble(), 0.0, 1.0) : kDefaultVolume;
    const QJsonValue dim = bg.value(QStringLiteral("dim"));
    d.dim = dim.isDouble() ? clamp(dim.toDouble(), 0.0, 1.0) : kDefaultDim;

    const QJsonValue opacity = root.value(QStringLiteral("cardOpacity"));
    d.cardOpacity = opacity.isDouble() ? clamp(opacity.toDouble(), kMinCardOpacity, 1.0) : 1.0;

    // The theme's own icon and font: plain file names with an allowed extension, anything else is ignored.
    const QString icon = root.value(QStringLiteral("icon")).toString();
    if (isPlainFileWith(icon, iconExtensions()))
        d.iconFile = icon;
    const QString font = root.value(QStringLiteral("font")).toString();
    if (isPlainFileWith(font, fontExtensions()))
        d.fontFile = font;

    // Interface sounds: a known style, and per event the one file name that event may use.
    const QJsonObject sounds = root.value(QStringLiteral("sounds")).toObject();
    const QString set = sounds.value(QStringLiteral("set")).toString();
    if (soundSetIds().contains(set))
        d.soundSet = set;
    const QJsonObject files = sounds.value(QStringLiteral("files")).toObject();
    for (const QString& event : soundEventIds())
        if (files.value(event).toString() == soundFileName(event))
            d.sounds.insert(event, soundFileName(event));
    return d;
}

QByteArray serializeDefinition(const Definition& def)
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), kFormat);
    root.insert(QStringLiteral("name"), def.name);
    root.insert(QStringLiteral("base"), baseToString(def.base));

    QJsonObject colors;
    if (def.accent.isValid())
        colors.insert(QStringLiteral("accent"), colorToString(def.accent));
    if (def.background.isValid())
        colors.insert(QStringLiteral("background"), colorToString(def.background));
    if (def.text.isValid())
        colors.insert(QStringLiteral("text"), colorToString(def.text));
    if (def.surface.isValid())
        colors.insert(QStringLiteral("surface"), colorToString(def.surface));
    root.insert(QStringLiteral("colors"), colors);

    QJsonObject overrides;
    for (auto it = def.overrides.cbegin(); it != def.overrides.cend(); ++it)
        if (it.value().isValid())
            overrides.insert(it.key(), colorToString(it.value()));
    root.insert(QStringLiteral("overrides"), overrides);

    root.insert(QStringLiteral("corners"), def.corners == CornerStyle::Square ? QStringLiteral("square") : QStringLiteral("rounded"));

    QJsonObject bg;
    bg.insert(QStringLiteral("kind"), kindToString(def.backgroundKind));
    if (def.backgroundKind != BackgroundKind::None)
        bg.insert(QStringLiteral("file"), def.backgroundFile);
    bg.insert(QStringLiteral("dim"), clamp(def.dim, 0.0, 1.0));
    if (def.backgroundKind == BackgroundKind::Video) {  // sound only exists with a video
        bg.insert(QStringLiteral("sound"), def.sound);
        bg.insert(QStringLiteral("volume"), clamp(def.volume, 0.0, 1.0));
    }
    root.insert(QStringLiteral("background"), bg);

    root.insert(QStringLiteral("cardOpacity"), clamp(def.cardOpacity, kMinCardOpacity, 1.0));
    if (!def.iconFile.isEmpty())
        root.insert(QStringLiteral("icon"), def.iconFile);
    if (!def.fontFile.isEmpty())
        root.insert(QStringLiteral("font"), def.fontFile);
    if (!def.soundSet.isEmpty() || !def.sounds.isEmpty()) {
        QJsonObject sounds;
        if (soundSetIds().contains(def.soundSet))
            sounds.insert(QStringLiteral("set"), def.soundSet);
        QJsonObject files;
        for (auto it = def.sounds.cbegin(); it != def.sounds.cend(); ++it)
            if (it.value() == soundFileName(it.key()))
                files.insert(it.key(), it.value());
        if (!files.isEmpty())
            sounds.insert(QStringLiteral("files"), files);
        root.insert(QStringLiteral("sounds"), sounds);
    }
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

Result<Definition> loadDefinition(const QString& folder)
{
    QFile f(QDir(folder).absoluteFilePath(QString::fromLatin1(kDefinitionFile)));
    if (!f.open(QIODevice::ReadOnly))
        return std::unexpected{ QObject::tr("Could not read %1: %2").arg(f.fileName(), f.errorString()) };
    return parseDefinition(f.readAll());
}

Result<> saveDefinition(const QString& folder, const Definition& def)
{
    if (!QDir().mkpath(folder))
        return std::unexpected{ QObject::tr("Could not create the theme folder %1").arg(folder) };
    QSaveFile out(QDir(folder).absoluteFilePath(QString::fromLatin1(kDefinitionFile)));
    const QByteArray data = serializeDefinition(def);
    if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit())
        return std::unexpected{ QObject::tr("Could not write the theme: %1").arg(out.errorString()) };
    return {};
}

// ---------------------------------------------------------------- identity

QString slugify(const QString& name)
{
    // Decompose accents ("é" -> "e" + mark) so they survive as their ascii letter.
    const QString decomposed = name.normalized(QString::NormalizationForm_D).toLower();
    QString slug;
    bool pendingDash = false;
    for (const QChar ch : decomposed) {
        const ushort u = ch.unicode();
        const bool alnum = (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9');
        if (alnum) {
            if (pendingDash && !slug.isEmpty())
                slug += QLatin1Char('-');
            pendingDash = false;
            slug += ch;
        } else if (ch.category() != QChar::Mark_NonSpacing) {
            pendingDash = true;
        }
    }
    if (slug.size() > 48)
        slug.truncate(48);
    while (slug.endsWith(QLatin1Char('-')))
        slug.chop(1);
    return slug.isEmpty() ? QStringLiteral("theme") : slug;
}

QString uniqueSlug(const QString& themesRoot, const QString& name)
{
    const QString base = slugify(name);
    const QDir root(themesRoot);
    QString slug = base;
    for (int n = 2; QFileInfo::exists(root.absoluteFilePath(slug)); ++n)
        slug = QStringLiteral("%1-%2").arg(base).arg(n);
    return slug;
}

bool isCustomThemeId(const QString& id)
{
    return id.startsWith(QLatin1String(kIdPrefix)) && id.size() > int(strlen(kIdPrefix));
}

QString uniqueName(const QString& name, const QStringList& taken)
{
    auto isTaken = [&](const QString& n) {
        return std::any_of(taken.cbegin(), taken.cend(), [&](const QString& t) { return t.compare(n, Qt::CaseInsensitive) == 0; });
    };
    if (!isTaken(name))
        return name;
    for (int n = 2;; ++n) {
        const QString candidate = QStringLiteral("%1 (%2)").arg(name).arg(n);
        if (!isTaken(candidate))
            return candidate;
    }
}

// ---------------------------------------------------------------- derivation

ThemeSpec deriveSpec(const Definition& def, const QString& folder, const QString& themeId)
{
    ThemeSpec s = baseSpec(def.base);
    s.baseId = s.id;  // keeps the base's overlay stylesheet / pixel mark
    s.id = themeId.isEmpty() ? idForFolderName(slugify(def.name)) : themeId;
    s.name = def.name;
    s.texture = false;  // a custom theme never tiles the stone: flat bg or the user's image

    const QColor bg = def.background.isValid() ? def.background : s.bg;
    const QColor text = def.text.isValid() ? def.text : s.text;
    const QColor accent = def.accent.isValid() ? def.accent : s.accent;
    const bool darkBg = contrastRatio(bg, Qt::white) > contrastRatio(bg, Qt::black);

    s.bg = bg;
    s.text = text;
    s.accent = accent;
    s.accent2 = accent;

    // Surfaces step from the background toward the text color (works for light and dark backgrounds).
    s.surface = def.surface.isValid() ? def.surface : opaqueMix(bg, text, 0.04);
    s.surfaceHover = opaqueMix(bg, text, 0.065);
    s.elevated = opaqueMix(bg, text, 0.08);
    s.higher = opaqueMix(bg, text, 0.11);
    s.pressed = opaqueMix(bg, text, 0.13);
    s.border = opaqueMix(s.surface, text, 0.10);
    s.borderStrong = opaqueMix(s.surface, text, 0.16);

    s.textSecondary = opaqueMix(bg, text, 0.70);
    s.textTertiary = opaqueMix(bg, text, 0.50);

    // Accent states: on a dark background hover is lighter, on a light one darker; pressed goes further the other way.
    constexpr double kStep = 0.07;
    s.accentHover = withLightness(accent, darkBg ? kStep : -kStep);
    s.accentPressed = withLightness(accent, darkBg ? -kStep : -2 * kStep);
    s.onAccent = contrastRatio(Qt::black, accent) >= contrastRatio(Qt::white, accent) ? QColor(Qt::black) : QColor(Qt::white);

    s.bevelDark = opaqueMix(bg, Qt::black, 0.35);
    // success / warning / danger / info / highlight stay the base theme's.

    for (auto it = def.overrides.cbegin(); it != def.overrides.cend(); ++it) {
        for (const auto& c : kColors) {
            if (it.key() == QLatin1String(c.name) && it.value().isValid())
                s.*(c.member) = it.value();
        }
    }

    // Corners: Rounded = the Obsidian radii; Square = the Minecraft look on a Minecraft base, radius 0 on an Obsidian one.
    if (def.corners == CornerStyle::Rounded) {
        s.corners = Corners::Rounded;
        s.radiusSm = Tokens::radiusSm;
        s.radiusMd = Tokens::radiusMd;
        s.radiusLg = Tokens::radiusLg;
        s.radiusXl = Tokens::radiusXl;
    } else if (def.base == Base::Minecraft) {
        const ThemeSpec mc = ThemeSpec::minecraft();
        s.corners = Corners::Notched;
        s.radiusSm = mc.radiusSm;
        s.radiusMd = mc.radiusMd;
        s.radiusLg = mc.radiusLg;
        s.radiusXl = mc.radiusXl;
    } else {
        s.corners = Corners::Rounded;
        s.radiusSm = s.radiusMd = s.radiusLg = s.radiusXl = 0;
    }

    // Background image or video: a missing file is treated as "none".
    if (def.backgroundKind != BackgroundKind::None && !folder.isEmpty() && isPlainFileName(def.backgroundFile)) {
        const QFileInfo fi(QDir(folder).absoluteFilePath(def.backgroundFile));
        if (fi.isFile()) {
            (def.backgroundKind == BackgroundKind::Video ? s.backgroundVideo : s.backgroundImage) = fi.absoluteFilePath();
            s.backgroundDim = clamp(def.dim, 0.0, 1.0);
            if (def.backgroundKind == BackgroundKind::Video) {
                s.videoSound = def.sound;
                s.videoVolume = clamp(def.volume, 0.0, 1.0);
            }
        }
    }

    // The theme's own icon and font: a missing file is treated as "default".
    const auto folderFile = [&](const QString& name, const QStringList& extensions) -> QString {
        if (folder.isEmpty() || !isPlainFileWith(name, extensions))
            return {};
        const QFileInfo fi(QDir(folder).absoluteFilePath(name));
        return fi.isFile() ? fi.absoluteFilePath() : QString();
    };
    s.appIcon = folderFile(def.iconFile, iconExtensions());
    s.fontFile = folderFile(def.fontFile, fontExtensions());

    // Interface sounds: the chosen style, and the theme's own WAVs (a missing file is treated as "not replaced").
    if (soundSetIds().contains(def.soundSet))
        s.soundSet = def.soundSet;
    if (!folder.isEmpty()) {
        for (auto it = def.sounds.cbegin(); it != def.sounds.cend(); ++it) {
            const QString path = folderFile(it.value(), { QStringLiteral("wav") });
            if (it.value() == soundFileName(it.key()) && !path.isEmpty())
                s.soundFiles.insert(it.key(), path);
        }
    }

    // Card opacity applies to surface / surfaceHover only (cards and panels); everything else stays opaque.
    s.cardOpacity = clamp(def.cardOpacity, kMinCardOpacity, 1.0);
    if (s.cardOpacity < 1.0) {
        s.surface.setAlphaF(s.cardOpacity);
        s.surfaceHover.setAlphaF(s.cardOpacity);
    }
    return s;
}

// ---------------------------------------------------------------- readability

double contrastRatio(const QColor& a, const QColor& b)
{
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

ReadabilityReport readability(const ThemeSpec& spec)
{
    ReadabilityReport r;
    r.textOnBackground = contrastRatio(spec.text, spec.bg) >= 4.5;
    r.textOnSurface = contrastRatio(spec.text, spec.surfaceSolid()) >= 4.5;
    r.accentLabel = contrastRatio(spec.onAccent, spec.accent) >= 3.0;
    QStringList failed;
    if (!r.textOnBackground)
        failed << QObject::tr("Text on background");
    if (!r.textOnSurface)
        failed << QObject::tr("Text on cards");
    if (!r.accentLabel)
        failed << QObject::tr("Button labels");
    r.level = failed.isEmpty() ? Readability::Readable : Readability::HardToRead;
    r.summary = failed.join(QStringLiteral(", "));
    return r;
}

}  // namespace Zero::Custom
