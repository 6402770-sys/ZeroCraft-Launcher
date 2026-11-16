// SPDX-License-Identifier: GPL-3.0-only
#include "ThemeEditorModel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>

namespace Zero::Custom {

namespace {

struct Token {
    const char* key;
    const char* label;
    QColor ThemeSpec::*member;
};

// One entry per overrideKeys() name.
const Token kTokens[] = {
    { "bg", QT_TRANSLATE_NOOP("Zero::ThemeEditor", "Background"), &ThemeSpec::bg },
    { "surface", QT_TRANSLATE_NOOP("Zero::ThemeEditor", "Surface"), &ThemeSpec::surface },
    { "surfaceHover", QT_TRANSLATE_NOOP("Zero::ThemeEditor", "Surface hover"), &ThemeSpec::surfaceHover },
    { "elevated", QT_TRANSLATE_NOOP("Zero::ThemeEditor", "Elevated"), &ThemeSpec::elevated },
    { "higher", QT_TRANSLATE_NOOP("Zero::ThemeEditor", "Higher"), &ThemeSpec::higher },
    { "pressed", QT_TRANSLATE_NOOP("Zero::ThemeEditor", "Pressed"), &ThemeSpec::pressed },
    { "border", QT_TRANSLATE_NOOP("Zero::ThemeEditor", "Border"), &ThemeSpec::border },
    { "borderStrong", QT_TRANSLATE_NOOP("Zero::ThemeEditor", "Border strong"), &ThemeSpec::borderStrong },
    { "accent", QT_TRANSLATE_NOOP("Zero::ThemeEditor", "Accent"), &ThemeSpec::accent },
    { "accentHover", QT_TRANSLATE_NOOP("Zero::ThemeEditor", "Accent hover"), &ThemeSpec::accentHover },
    { "accentPressed", QT_TRANSLATE_NOOP("Zero::ThemeEditor", "Accent pressed"), &ThemeSpec::accentPressed },
    { "onAccent", QT_TRANSLATE_NOOP("Zero::ThemeEditor", "Text on accent"), &ThemeSpec::onAccent },
    { "text", QT_TRANSLATE_NOOP("Zero::ThemeEditor", "Text"), &ThemeSpec::text },
    { "textSecondary", QT_TRANSLATE_NOOP("Zero::ThemeEditor", "Text secondary"), &ThemeSpec::textSecondary },
    { "textTertiary", QT_TRANSLATE_NOOP("Zero::ThemeEditor", "Text tertiary"), &ThemeSpec::textTertiary },
    { "success", QT_TRANSLATE_NOOP("Zero::ThemeEditor", "Success"), &ThemeSpec::success },
    { "warning", QT_TRANSLATE_NOOP("Zero::ThemeEditor", "Warning"), &ThemeSpec::warning },
    { "danger", QT_TRANSLATE_NOOP("Zero::ThemeEditor", "Danger"), &ThemeSpec::danger },
    { "info", QT_TRANSLATE_NOOP("Zero::ThemeEditor", "Info"), &ThemeSpec::info },
    { "accent2", QT_TRANSLATE_NOOP("Zero::ThemeEditor", "Secondary accent"), &ThemeSpec::accent2 },
    { "highlight", QT_TRANSLATE_NOOP("Zero::ThemeEditor", "Highlight"), &ThemeSpec::highlight },
    { "bevelDark", QT_TRANSLATE_NOOP("Zero::ThemeEditor", "Bevel shadow"), &ThemeSpec::bevelDark },
};

const Token* findToken(const QString& key)
{
    for (const Token& t : kTokens)
        if (key == QLatin1String(t.key))
            return &t;
    return nullptr;
}

QColor baseColor(Base base, MainColor which)
{
    const ThemeSpec s = baseSpec(base);
    switch (which) {
        case MainColor::Accent:
            return s.accent;
        case MainColor::Background:
            return s.bg;
        default:
            return s.text;
    }
}

QColor& mainField(Definition& d, MainColor which)
{
    return which == MainColor::Accent ? d.accent : which == MainColor::Background ? d.background : d.text;
}

QString backgroundNameFor(const QString& path)
{
    return QStringLiteral("background.") + QFileInfo(path).suffix().toLower();
}

}  // namespace

ThemeEditorModel::ThemeEditorModel(QObject* parent) : QObject(parent)
{
    beginNew(QString());
}

QString ThemeEditorModel::themeId() const
{
    return isNew() ? QString::fromLatin1(kNewThemeId) : m_themeId;
}

void ThemeEditorModel::begin(const Definition& def, const QString& folder, const QString& themeId)
{
    m_def = def;
    m_folder = folder;
    m_themeId = themeId;
    m_pending.clear();
    m_stash[0] = m_stash[1] = Stash();
    m_icon = Asset();
    m_font = Asset();
    m_pendingSounds.clear();
    // The three main colors are always explicit in the editor; "touched" = differs from the base theme's own color.
    for (MainColor c : { MainColor::Accent, MainColor::Background, MainColor::Text }) {
        QColor& field = mainField(m_def, c);
        const QColor base = baseColor(m_def.base, c);
        m_touched[int(c)] = field.isValid() && field.rgb() != base.rgb();
        if (!field.isValid())
            field = base;
    }
    // A surface equal to the base's (what Definition::fromBase writes) is the default, not a choice: let it derive.
    if (m_def.surface.isValid() && m_def.surface.rgb() == baseSpec(m_def.base).surface.rgb())
        m_def.surface = QColor();
    if (m_def.backgroundKind != BackgroundKind::None && m_def.backgroundFile.isEmpty())
        m_def.backgroundKind = BackgroundKind::None;
    emit changed();
    emit lookChanged();
}

void ThemeEditorModel::beginNew(const QString& name)
{
    Definition d = Definition::fromBase(Base::Obsidian, name);
    d.surface = QColor();
    begin(d, QString(), QString());
}

void ThemeEditorModel::beginEdit(const Definition& def, const QString& folder, const QString& themeId)
{
    begin(def, folder, themeId);
}

void ThemeEditorModel::edited(bool look)
{
    emit changed();
    if (look)
        emit lookChanged();
}

void ThemeEditorModel::setName(const QString& name)
{
    if (m_def.name == name)
        return;
    m_def.name = name;
    edited(false);
}

void ThemeEditorModel::setBase(Base base)
{
    if (m_def.base == base)
        return;
    m_def.base = base;
    for (MainColor c : { MainColor::Accent, MainColor::Background, MainColor::Text }) {
        if (!m_touched[int(c)])
            mainField(m_def, c) = baseColor(base, c);
    }
    m_def.corners = base == Base::Minecraft ? CornerStyle::Square : CornerStyle::Rounded;
    edited();
}

void ThemeEditorModel::setCorners(CornerStyle corners)
{
    if (m_def.corners == corners)
        return;
    m_def.corners = corners;
    edited();
}

QColor ThemeEditorModel::color(MainColor which) const
{
    const ThemeSpec s = spec();
    return which == MainColor::Accent ? s.accent : which == MainColor::Background ? s.bg : s.text;
}

void ThemeEditorModel::setColor(MainColor which, const QColor& color, bool touched)
{
    if (!color.isValid())
        return;
    QColor& field = mainField(m_def, which);
    if (field.rgb() == color.rgb() && m_touched[int(which)] == touched)
        return;
    field = QColor(color.rgb());
    m_touched[int(which)] = touched;
    edited();
}

QColor ThemeEditorModel::specColor(const ThemeSpec& spec, const QString& key)
{
    const Token* t = findToken(key);
    return t ? ThemeSpec::opaque(spec.*(t->member)) : QColor();
}

QColor ThemeEditorModel::tokenColor(const QString& key) const
{
    return specColor(spec(), key);
}

bool ThemeEditorModel::isOverridden(const QString& key) const
{
    if (key == QLatin1String("surface"))
        return m_def.surface.isValid();
    if (key == QLatin1String("bg") || key == QLatin1String("accent") || key == QLatin1String("text"))
        return false;
    return m_def.overrides.contains(key);
}

void ThemeEditorModel::setTokenColor(const QString& key, const QColor& color)
{
    if (!color.isValid() || !findToken(key))
        return;
    if (key == QLatin1String("bg"))
        return setColor(MainColor::Background, color);
    if (key == QLatin1String("accent"))
        return setColor(MainColor::Accent, color);
    if (key == QLatin1String("text"))
        return setColor(MainColor::Text, color);
    const QColor c(color.rgb());
    if (key == QLatin1String("surface")) {
        if (m_def.surface.isValid() && m_def.surface.rgb() == c.rgb())
            return;
        m_def.surface = c;
    } else {
        if (m_def.overrides.value(key).isValid() && m_def.overrides.value(key).rgb() == c.rgb())
            return;
        m_def.overrides.insert(key, c);
    }
    edited();
}

void ThemeEditorModel::resetToken(const QString& key)
{
    if (!isOverridden(key))
        return;
    if (key == QLatin1String("surface"))
        m_def.surface = QColor();
    else
        m_def.overrides.remove(key);
    edited();
}

QString ThemeEditorModel::tokenLabel(const QString& key)
{
    const Token* t = findToken(key);
    return t ? QCoreApplication::translate("Zero::ThemeEditor", t->label) : key;
}

ThemeEditorModel::BackgroundChoice ThemeEditorModel::backgroundChoice() const
{
    switch (m_def.backgroundKind) {
        case BackgroundKind::Image:
            return BackgroundChoice::Image;
        case BackgroundKind::Video:
            return BackgroundChoice::Video;
        case BackgroundKind::None:
            break;
    }
    return BackgroundChoice::None;
}

bool ThemeEditorModel::hasImage() const
{
    return m_def.backgroundKind == BackgroundKind::Image && (!m_pending.isEmpty() || !m_def.backgroundFile.isEmpty());
}

bool ThemeEditorModel::hasVideo() const
{
    return m_def.backgroundKind == BackgroundKind::Video && (!m_pending.isEmpty() || !m_def.backgroundFile.isEmpty());
}

QString ThemeEditorModel::imageName() const
{
    if (!hasImage())
        return {};
    return m_pending.isEmpty() ? m_def.backgroundFile : QFileInfo(m_pending).fileName();
}

QString ThemeEditorModel::videoName() const
{
    if (!hasVideo())
        return {};
    return m_pending.isEmpty() ? m_def.backgroundFile : QFileInfo(m_pending).fileName();
}

namespace {
int stashIndex(BackgroundKind kind)
{
    return kind == BackgroundKind::Video ? 1 : 0;
}
}  // namespace

bool ThemeEditorModel::selectKind(BackgroundKind kind)
{
    if (m_def.backgroundKind == kind)
        return true;
    const bool restore = kind != BackgroundKind::None;
    if (restore && m_stash[stashIndex(kind)].pending.isEmpty() && m_stash[stashIndex(kind)].file.isEmpty())
        return false;  // nothing to bring back: the panel asks for a file
    if (m_def.backgroundKind != BackgroundKind::None)
        m_stash[stashIndex(m_def.backgroundKind)] = { m_pending, m_def.backgroundFile };
    m_pending.clear();
    m_def.backgroundFile.clear();
    m_def.backgroundKind = kind;
    if (restore) {
        Stash& s = m_stash[stashIndex(kind)];
        m_pending = s.pending;
        m_def.backgroundFile = s.file;
    }
    edited();
    return true;
}

void ThemeEditorModel::selectNoBackground()
{
    selectKind(BackgroundKind::None);
}

bool ThemeEditorModel::selectImageBackground()
{
    return selectKind(BackgroundKind::Image);
}

bool ThemeEditorModel::selectVideoBackground()
{
    return selectKind(BackgroundKind::Video);
}

void ThemeEditorModel::setSource(BackgroundKind kind, const QString& path)
{
    if (path.isEmpty())
        return;
    if (m_def.backgroundKind != BackgroundKind::None && m_def.backgroundKind != kind)
        m_stash[stashIndex(m_def.backgroundKind)] = { m_pending, m_def.backgroundFile };  // the other kind stays remembered
    m_pending = QFileInfo(path).absoluteFilePath();
    m_def.backgroundKind = kind;
    m_def.backgroundFile = backgroundNameFor(m_pending);
    edited();
}

void ThemeEditorModel::setImage(const QString& path)
{
    setSource(BackgroundKind::Image, path);
}

void ThemeEditorModel::setVideo(const QString& path)
{
    setSource(BackgroundKind::Video, path);
}

void ThemeEditorModel::setDim(double dim)
{
    dim = std::clamp(dim, 0.0, 1.0);
    if (qFuzzyCompare(1.0 + m_def.dim, 1.0 + dim))
        return;
    m_def.dim = dim;
    edited();
}

void ThemeEditorModel::setCardOpacity(double opacity)
{
    opacity = std::clamp(opacity, kMinCardOpacity, 1.0);
    if (qFuzzyCompare(m_def.cardOpacity, opacity))
        return;
    m_def.cardOpacity = opacity;
    edited();
}

Definition ThemeEditorModel::effectiveDefinition() const
{
    Definition d = m_def;
    d.name = d.name.simplified();
    if (!m_icon.pending.isEmpty())
        d.iconFile = QStringLiteral("icon.") + QFileInfo(m_icon.pending).suffix().toLower();
    if (!m_font.pending.isEmpty())
        d.fontFile = QStringLiteral("font.") + QFileInfo(m_font.pending).suffix().toLower();
    for (auto it = m_pendingSounds.cbegin(); it != m_pendingSounds.cend(); ++it)
        d.sounds.insert(it.key(), soundFileName(it.key()));
    if (d.backgroundKind == BackgroundKind::None)
        d.cardOpacity = 1.0;  // translucent cards over a flat background would just be dull
    if (!m_pending.isEmpty())
        d.backgroundFile = backgroundNameFor(m_pending);
    return d;
}

Definition ThemeEditorModel::definition() const
{
    return effectiveDefinition();
}

ThemeSpec ThemeEditorModel::spec() const
{
    const Definition d = effectiveDefinition();
    ThemeSpec s = deriveSpec(d, m_folder, themeId());
    if (!m_pending.isEmpty() && d.backgroundKind != BackgroundKind::None && QFileInfo(m_pending).isFile()) {
        // previewed from where the user picked it, until the theme is saved
        (d.backgroundKind == BackgroundKind::Video ? s.backgroundVideo : s.backgroundImage) = m_pending;
        s.backgroundDim = std::clamp(d.dim, 0.0, 1.0);
        if (d.backgroundKind == BackgroundKind::Video) {
            s.videoSound = d.sound;
            s.videoVolume = std::clamp(d.volume, 0.0, 1.0);
        }
    }
    // A picked icon or font previews from where the user picked it, until the theme is saved.
    if (!m_icon.pending.isEmpty() && QFileInfo(m_icon.pending).isFile())
        s.appIcon = m_icon.pending;
    if (!m_font.pending.isEmpty() && QFileInfo(m_font.pending).isFile())
        s.fontFile = m_font.pending;
    for (auto it = m_pendingSounds.cbegin(); it != m_pendingSounds.cend(); ++it)
        if (QFileInfo(it.value()).isFile())
            s.soundFiles.insert(it.key(), it.value());
    return s;
}

// ---------------------------------------------------------------- video sound

void ThemeEditorModel::setSound(bool on)
{
    if (m_def.sound == on)
        return;
    m_def.sound = on;
    edited();
}

void ThemeEditorModel::setVolume(double volume)
{
    volume = std::clamp(volume, 0.0, 1.0);
    if (qFuzzyCompare(1.0 + m_def.volume, 1.0 + volume))
        return;
    m_def.volume = volume;
    edited();
}

// ---------------------------------------------------------------- icon and font

QString ThemeEditorModel::nameOf(const Asset& asset, const QString& file) const
{
    if (!asset.pending.isEmpty())
        return QFileInfo(asset.pending).fileName();
    return file;
}

QString ThemeEditorModel::pathOf(const Asset& asset, const QString& file) const
{
    if (!asset.pending.isEmpty())
        return QFileInfo(asset.pending).isFile() ? asset.pending : QString();
    if (file.isEmpty() || m_folder.isEmpty())
        return {};
    const QFileInfo fi(QDir(m_folder).absoluteFilePath(file));
    return fi.isFile() ? fi.absoluteFilePath() : QString();
}

void ThemeEditorModel::selectDefault(Asset& asset, QString Definition::* file)
{
    if (asset.pending.isEmpty() && (m_def.*file).isEmpty())
        return;
    asset.stashPending = asset.pending;
    asset.stashFile = m_def.*file;
    asset.pending.clear();
    m_def.*file = QString();
    edited();
}

bool ThemeEditorModel::selectCustom(Asset& asset, QString Definition::* file)
{
    if (asset.pending.isEmpty() && (m_def.*file).isEmpty()) {
        if (asset.stashPending.isEmpty() && asset.stashFile.isEmpty())
            return false;  // nothing to bring back: the panel asks for a file
        asset.pending = asset.stashPending;
        m_def.*file = asset.stashFile;
        asset.stashPending.clear();
        asset.stashFile.clear();
        edited();
    }
    return true;
}

void ThemeEditorModel::setAsset(Asset& asset, QString Definition::* file, const QString& stem, const QString& path)
{
    if (path.isEmpty())
        return;
    asset.pending = QFileInfo(path).absoluteFilePath();
    m_def.*file = stem + QFileInfo(path).suffix().toLower();
    asset.stashPending.clear();
    asset.stashFile.clear();
    edited();
}

QString ThemeEditorModel::iconName() const
{
    return nameOf(m_icon, m_def.iconFile);
}

QString ThemeEditorModel::iconPath() const
{
    return pathOf(m_icon, m_def.iconFile);
}

void ThemeEditorModel::selectDefaultIcon()
{
    selectDefault(m_icon, &Definition::iconFile);
}

bool ThemeEditorModel::selectCustomIcon()
{
    return selectCustom(m_icon, &Definition::iconFile);
}

void ThemeEditorModel::setIcon(const QString& path)
{
    setAsset(m_icon, &Definition::iconFile, QStringLiteral("icon."), path);
}

QString ThemeEditorModel::fontName() const
{
    return nameOf(m_font, m_def.fontFile);
}

QString ThemeEditorModel::fontPath() const
{
    return pathOf(m_font, m_def.fontFile);
}

void ThemeEditorModel::selectDefaultFont()
{
    selectDefault(m_font, &Definition::fontFile);
}

bool ThemeEditorModel::selectCustomFont()
{
    return selectCustom(m_font, &Definition::fontFile);
}

void ThemeEditorModel::setFont(const QString& path)
{
    setAsset(m_font, &Definition::fontFile, QStringLiteral("font."), path);
}

// ---------------------------------------------------------------- interface sounds

QString ThemeEditorModel::soundStyle() const
{
    if (soundSetIds().contains(m_def.soundSet))
        return m_def.soundSet;
    return m_def.base == Base::Minecraft ? QStringLiteral("retro") : QStringLiteral("soft");
}

void ThemeEditorModel::setSoundStyle(const QString& style)
{
    if (!soundSetIds().contains(style) || soundStyle() == style)
        return;
    m_def.soundSet = style;
    edited();
}

bool ThemeEditorModel::hasEventSound(const QString& event) const
{
    return m_pendingSounds.contains(event) || m_def.sounds.contains(event);
}

QString ThemeEditorModel::eventSoundName(const QString& event) const
{
    if (m_pendingSounds.contains(event))
        return QFileInfo(m_pendingSounds.value(event)).fileName();
    return m_def.sounds.value(event);
}

QString ThemeEditorModel::eventSoundPath(const QString& event) const
{
    if (m_pendingSounds.contains(event)) {
        const QString p = m_pendingSounds.value(event);
        return QFileInfo(p).isFile() ? p : QString();
    }
    const QString file = m_def.sounds.value(event);
    if (file.isEmpty() || m_folder.isEmpty())
        return {};
    const QFileInfo fi(QDir(m_folder).absoluteFilePath(file));
    return fi.isFile() ? fi.absoluteFilePath() : QString();
}

void ThemeEditorModel::setEventSound(const QString& event, const QString& path)
{
    if (path.isEmpty() || !soundEventIds().contains(event))
        return;
    m_pendingSounds.insert(event, QFileInfo(path).absoluteFilePath());
    m_def.sounds.insert(event, soundFileName(event));
    edited();
}

void ThemeEditorModel::resetEventSound(const QString& event)
{
    if (!hasEventSound(event))
        return;
    m_pendingSounds.remove(event);
    m_def.sounds.remove(event);
    edited();
}

QColor ThemeEditorModel::parseHex(const QString& text)
{
    static const QRegularExpression re(QStringLiteral("^#?([0-9A-Fa-f]{6})$"));
    const auto m = re.match(text.trimmed());
    return m.hasMatch() ? QColor(QLatin1Char('#') + m.captured(1)) : QColor();
}

}  // namespace Zero::Custom
