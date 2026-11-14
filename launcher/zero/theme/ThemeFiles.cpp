// SPDX-License-Identifier: GPL-3.0-only
#include "ThemeFiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QImageReader>
#include <QSize>

#include "ThemeArchive.h"

namespace Zero::Custom {

namespace {

constexpr int kMinIconPixels = 32;

/// One file a theme folder holds next to its JSON under a fixed stem (`background.<ext>`, `icon.<ext>`, `font.<ext>`). It is copied in
/// under a temporary name, renamed over the target (the file it replaces is kept as a backup until the JSON is safe) and the stale
/// one removed afterwards.
struct Slot {
    QString stem;
    QString tempName;
    QString backupName;
    QString oldFile;     // the file the folder's JSON named before this save
    QString installed;   // absolute path of the new file once it is in place
    QString backupPath;  // absolute path of the file it replaced (same name), until the JSON is safe
    QString target;      // absolute path the new file went to (for putting the backup back)

    Slot(const QString& stem, const QString& tempName, const QString& backupName) : stem(stem), tempName(tempName), backupName(backupName) {}
};

bool isPlainName(const QString& name)
{
    return !name.isEmpty() && QFileInfo(name).fileName() == name && name != QLatin1String(".") && name != QLatin1String("..");
}

/// Copies `source` into the folder as `<stem><ext>`. Returns an empty string on success, else the readable error (nothing stays half-done
/// for this slot).
QString installSlot(Slot& slot, const QDir& dir, const QString& source, const QString& ext, const QString& what)
{
    const QString temp = dir.absoluteFilePath(slot.tempName);
    QFile::remove(temp);
    if (!QFile::copy(source, temp)) {
        QFile::remove(temp);
        return QObject::tr("Could not copy the %1 into the theme.").arg(what);
    }
    const QString target = dir.absoluteFilePath(slot.stem + ext);
    QString backup;
    if (QFileInfo::exists(target)) {
        const QString bak = dir.absoluteFilePath(slot.backupName);
        QFile::remove(bak);
        if (!QFile::rename(target, bak)) {
            QFile::remove(temp);
            return QObject::tr("Could not replace the %1.").arg(what);
        }
        backup = bak;
    }
    if (!QFile::rename(temp, target)) {
        QFile::remove(temp);
        if (!backup.isEmpty())
            QFile::rename(backup, target);
        return QObject::tr("Could not store the %1 in the theme.").arg(what);
    }
    slot.installed = target;
    slot.target = target;
    slot.backupPath = backup;
    return {};
}

void rollbackSlot(Slot& slot)
{
    if (!slot.installed.isEmpty()) {
        QFile::remove(slot.installed);
        slot.installed.clear();
    }
    if (!slot.backupPath.isEmpty()) {
        QFile::rename(slot.backupPath, slot.target);
        slot.backupPath.clear();
    }
}

/// The JSON is safe: drop the backup, and the file the folder used to hold unless `keepName` (what the new JSON names) is that file.
void finishSlot(Slot& slot, const QDir& dir, const QString& keepName)
{
    if (!slot.backupPath.isEmpty())
        QFile::remove(slot.backupPath);
    if (!slot.oldFile.isEmpty() && isPlainName(slot.oldFile) && slot.oldFile.startsWith(slot.stem) &&
        (keepName.isEmpty() || slot.oldFile.compare(keepName, Qt::CaseInsensitive) != 0))
        QFile::remove(dir.absoluteFilePath(slot.oldFile));
}

/// Copies one theme file (named by the JSON) from `folder` into `target`. False only when the file exists and could not be copied.
bool copyAsset(const QString& folder, const QString& target, const QString& name)
{
    if (!isPlainName(name))
        return true;
    const QString from = QDir(folder).absoluteFilePath(name);
    return !QFileInfo(from).isFile() || QFile::copy(from, QDir(target).absoluteFilePath(name));
}

}  // namespace

QStringList imageExtensions()
{
    return { QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("webp"), QStringLiteral("bmp"),
             QStringLiteral("gif") };
}

QString imageExtension(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return imageExtensions().contains(ext) ? ext : QString();
}

QStringList videoExtensions()
{
    return { QStringLiteral("mp4"), QStringLiteral("webm"), QStringLiteral("mkv"), QStringLiteral("mov") };
}

QString videoExtension(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return videoExtensions().contains(ext) ? ext : QString();
}

QString iconExtension(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return iconExtensions().contains(ext) ? ext : QString();
}

QString fontExtension(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return fontExtensions().contains(ext) ? ext : QString();
}

Result<> validateIconSource(const QString& path, qint64 maxBytes)
{
    const QString ext = iconExtension(path);
    if (ext.isEmpty())
        return std::unexpected{ QObject::tr("ZERO can use png, svg or ico files as the launcher icon.") };
    const QFileInfo info(path);
    if (!info.isFile())
        return std::unexpected{ QObject::tr("The icon file could not be found: %1").arg(path) };
    if (info.size() > maxBytes)
        return std::unexpected{ QObject::tr("This icon is too big. The limit is %1 MB.").arg(maxBytes / (1024 * 1024)) };
    QImageReader reader(path);
    if (!reader.canRead())
        return std::unexpected{ QObject::tr("ZERO could not read this icon.") };
    if (ext != QLatin1String("svg")) {  // a vector icon has no pixel size
        QSize largest = reader.size();
        if (ext == QLatin1String("ico")) {
            for (const QSize& s : QIcon(path).availableSizes())
                if (s.width() * s.height() > largest.width() * largest.height())
                    largest = s;
        }
        if (!largest.isValid() || largest.width() < kMinIconPixels || largest.height() < kMinIconPixels)
            return std::unexpected{ QObject::tr("This icon is too small. It must be at least %1 x %1 pixels.").arg(kMinIconPixels) };
    }
    return {};
}

Result<> validateFontSource(const QString& path, qint64 maxBytes)
{
    if (fontExtension(path).isEmpty())
        return std::unexpected{ QObject::tr("ZERO can use ttf or otf font files.") };
    const QFileInfo info(path);
    if (!info.isFile())
        return std::unexpected{ QObject::tr("The font file could not be found: %1").arg(path) };
    if (info.size() > maxBytes)
        return std::unexpected{ QObject::tr("This font file is too big. The limit is %1 MB.").arg(maxBytes / (1024 * 1024)) };
    return {};
}

Result<SavedTheme> saveTheme(const QString& themesRoot, const QString& folderIn, Definition def, const QString& backgroundSource,
                             const SaveLimits& limits, const AssetSources& assets)
{
    const bool isNew = folderIn.isEmpty();
    QString folder = folderIn;
    if (isNew) {
        if (themesRoot.isEmpty())
            return std::unexpected{ QObject::tr("There is no folder for themes.") };
        folder = QDir(themesRoot).absoluteFilePath(uniqueSlug(themesRoot, def.name));
    }
    const QDir dir(folder);

    Slot background(QStringLiteral("background."), QStringLiteral(".zero-bg-new"), QStringLiteral(".zero-bg-old"));
    Slot icon(QStringLiteral("icon."), QStringLiteral(".zero-icon-new"), QStringLiteral(".zero-icon-old"));
    Slot font(QStringLiteral("font."), QStringLiteral(".zero-font-new"), QStringLiteral(".zero-font-old"));

    // The files the folder holds now (for the cleanup after the JSON is written).
    if (!isNew) {
        if (auto old = loadDefinition(folder)) {
            if (old->backgroundKind != BackgroundKind::None)
                background.oldFile = old->backgroundFile;
            icon.oldFile = old->iconFile;
            font.oldFile = old->fontFile;
        }
    }

    const auto fail = [&](const QString& message) -> Result<SavedTheme> {
        rollbackSlot(font);
        rollbackSlot(icon);
        rollbackSlot(background);
        if (isNew)
            QDir(folder).removeRecursively();
        return std::unexpected{ message };
    };

    if (isNew && !QDir().mkpath(folder))
        return std::unexpected{ QObject::tr("Could not create the theme folder.") };

    // ---- the new background file
    if (!backgroundSource.isEmpty()) {
        const QString imageExt = imageExtension(backgroundSource);
        const QString videoExt = videoExtension(backgroundSource);
        const QString ext = imageExt.isEmpty() ? videoExt : imageExt;
        if (ext.isEmpty())
            return fail(QObject::tr("This is not a picture or video ZERO can use as a background (png, jpg, webp, bmp, gif, mp4, webm, mkv or mov)."));
        if (!QFileInfo(backgroundSource).isFile())
            return fail(QObject::tr("The background file could not be found: %1").arg(backgroundSource));
        if (!videoExt.isEmpty() && QFileInfo(backgroundSource).size() > limits.maxVideoBytes)
            return fail(QObject::tr("This video is too big for a background (the limit is %1 MB).").arg(limits.maxVideoBytes / (1024 * 1024)));
        if (const QString error = installSlot(background, dir, backgroundSource, ext, QObject::tr("background file")); !error.isEmpty())
            return fail(error);
        def.backgroundKind = imageExt.isEmpty() ? BackgroundKind::Video : BackgroundKind::Image;
        def.backgroundFile = background.stem + ext;
    } else if (def.backgroundKind == BackgroundKind::None) {
        def.backgroundFile.clear();
    }

    // ---- the new icon
    if (!assets.icon.isEmpty()) {
        if (auto ok = validateIconSource(assets.icon, limits.maxIconBytes); !ok)
            return fail(ok.error());
        const QString ext = iconExtension(assets.icon);
        if (const QString error = installSlot(icon, dir, assets.icon, ext, QObject::tr("icon")); !error.isEmpty())
            return fail(error);
        def.iconFile = icon.stem + ext;
    }

    // ---- the new font
    if (!assets.font.isEmpty()) {
        if (auto ok = validateFontSource(assets.font, limits.maxFontBytes); !ok)
            return fail(ok.error());
        const QString ext = fontExtension(assets.font);
        if (const QString error = installSlot(font, dir, assets.font, ext, QObject::tr("font")); !error.isEmpty())
            return fail(error);
        def.fontFile = font.stem + ext;
    }

    // ---- the definition
    if (auto saved = saveDefinition(folder, def); !saved)
        return fail(saved.error());

    // ---- the JSON is safe: drop what it no longer needs
    finishSlot(background, dir, def.backgroundKind == BackgroundKind::None ? QString() : def.backgroundFile);
    finishSlot(icon, dir, def.iconFile);
    finishSlot(font, dir, def.fontFile);

    return SavedTheme{ idForFolderName(QFileInfo(folder).fileName()), QDir(folder).absolutePath() };
}

Result<SavedTheme> duplicateTheme(const QString& themesRoot, const QString& folder)
{
    auto loaded = loadDefinition(folder);
    if (!loaded)
        return std::unexpected{ loaded.error() };
    Definition def = *loaded;
    def.name = uniqueName(QObject::tr("%1 copy").arg(def.name), existingThemeNames(themesRoot));

    const QString target = QDir(themesRoot).absoluteFilePath(uniqueSlug(themesRoot, def.name));
    if (!QDir().mkpath(target))
        return std::unexpected{ QObject::tr("Could not create the theme folder.") };
    const auto abort = [&](const QString& message) -> Result<SavedTheme> {
        QDir(target).removeRecursively();
        return std::unexpected{ message };
    };
    if (def.backgroundKind != BackgroundKind::None && !copyAsset(folder, target, def.backgroundFile))
        return abort(QObject::tr("Could not copy the background of the theme."));
    if (!copyAsset(folder, target, def.iconFile))
        return abort(QObject::tr("Could not copy the icon of the theme."));
    if (!copyAsset(folder, target, def.fontFile))
        return abort(QObject::tr("Could not copy the font of the theme."));
    if (auto saved = saveDefinition(target, def); !saved)
        return abort(saved.error());
    return SavedTheme{ idForFolderName(QFileInfo(target).fileName()), QDir(target).absolutePath() };
}

}  // namespace Zero::Custom
