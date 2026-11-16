// SPDX-License-Identifier: GPL-3.0-only
#include "ThemeArchive.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <functional>

#include "CustomTheme.h"
#include "FileSystem.h"
#include "ThemeFiles.h"
#include "archive/ArchiveReader.h"
#include "archive/ArchiveWriter.h"

namespace Zero::Custom {

namespace {

const QString kBackgroundStem = QStringLiteral("background.");
const QString kIconStem = QStringLiteral("icon.");
const QString kFontStem = QStringLiteral("font.");

bool hasStemAndExtension(const QString& name, const QString& stem, const QStringList& extensions)
{
    if (!name.startsWith(stem))
        return false;
    return extensions.contains(name.mid(stem.size()), Qt::CaseInsensitive);
}

bool isBackgroundEntry(const QString& name)
{
    return hasStemAndExtension(name, kBackgroundStem, backgroundExtensions());
}

bool isIconEntry(const QString& name)
{
    return hasStemAndExtension(name, kIconStem, iconExtensions());
}

bool isFontEntry(const QString& name)
{
    return hasStemAndExtension(name, kFontStem, fontExtensions());
}

}  // namespace

QStringList backgroundExtensions()
{
    return imageExtensions() + videoExtensions();
}

QStringList existingThemeNames(const QString& themesRoot)
{
    QStringList names;
    const QDir root(themesRoot);
    for (const QString& dir : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (auto def = loadDefinition(root.absoluteFilePath(dir)))
            names << def->name;
    }
    return names;
}

// ---------------------------------------------------------------- export

Result<> exportTheme(const QString& themeFolder, const QString& zipPath)
{
    auto loaded = loadDefinition(themeFolder);
    if (!loaded)
        return std::unexpected{ loaded.error() };
    Definition def = *loaded;
    const QDir folder(themeFolder);

    QString backgroundSource;
    QString entryName;
    if (def.backgroundKind != BackgroundKind::None) {
        const QFileInfo bg(folder.absoluteFilePath(def.backgroundFile));
        const QString ext = bg.suffix().toLower();
        if (!bg.isFile()) {
            def.backgroundKind = BackgroundKind::None;  // missing file = no background
            def.backgroundFile.clear();
        } else if (!backgroundExtensions().contains(ext)) {
            return std::unexpected{ QObject::tr("The background file type \"%1\" cannot be exported.").arg(ext) };
        } else {
            backgroundSource = bg.absoluteFilePath();
            entryName = kBackgroundStem + ext;
            def.backgroundFile = entryName;
        }
    }

    // The theme's own icon and font travel the same way: a flat `icon.<ext>` / `font.<ext>` entry, or nothing when the file is gone.
    QString iconSource, iconEntry, fontSource, fontEntry;
    const auto collect = [&](QString& file, const QString& stem, const QStringList& extensions, qint64 maxBytes, const QString& what,
                             QString& source, QString& entry) -> Result<> {
        if (file.isEmpty())
            return {};
        const QFileInfo fi(folder.absoluteFilePath(file));
        const QString ext = fi.suffix().toLower();
        if (!fi.isFile()) {
            file.clear();  // missing file = default
            return {};
        }
        if (!extensions.contains(ext))
            return std::unexpected{ QObject::tr("The %1 file type \"%2\" cannot be exported.").arg(what, ext) };
        if (fi.size() > maxBytes)
            return std::unexpected{ QObject::tr("The %1 is too big to export.").arg(what) };
        source = fi.absoluteFilePath();
        entry = stem + ext;
        file = entry;
        return {};
    };
    if (auto r = collect(def.iconFile, kIconStem, iconExtensions(), kMaxIconBytes, QObject::tr("icon"), iconSource, iconEntry); !r)
        return std::unexpected{ r.error() };
    if (auto r = collect(def.fontFile, kFontStem, fontExtensions(), kMaxFontBytes, QObject::tr("font"), fontSource, fontEntry); !r)
        return std::unexpected{ r.error() };

    // Interface sounds: flat `sound-<event>.wav` entries; a missing or oversize file is dropped from the definition.
    QMap<QString, QString> soundSources;  // entry name -> file
    for (auto it = def.sounds.begin(); it != def.sounds.end();) {
        const QFileInfo fi(folder.absoluteFilePath(it.value()));
        if (!isSoundFileName(it.value()) || !fi.isFile() || fi.size() > kMaxSoundBytes) {
            it = def.sounds.erase(it);
            continue;
        }
        soundSources.insert(it.value(), fi.absoluteFilePath());
        ++it;
    }

    const QString partPath = zipPath + QStringLiteral(".part");
    QFile::remove(partPath);
    {
        MMCZip::ArchiveWriter writer(partPath);
        auto fail = [&](const QString& why) -> Result<> {
            writer.close();
            QFile::remove(partPath);
            return std::unexpected{ QObject::tr("Could not write the theme file: %1").arg(why) };
        };
        if (auto r = writer.open(); !r)
            return fail(r.error());
        if (auto r = writer.addFile(QString::fromLatin1(kDefinitionFile), serializeDefinition(def)); !r)
            return fail(r.error());
        if (!backgroundSource.isEmpty()) {
            if (auto r = writer.addFile(backgroundSource, entryName); !r)
                return fail(r.error());
        }
        if (!iconSource.isEmpty()) {
            if (auto r = writer.addFile(iconSource, iconEntry); !r)
                return fail(r.error());
        }
        if (!fontSource.isEmpty()) {
            if (auto r = writer.addFile(fontSource, fontEntry); !r)
                return fail(r.error());
        }
        for (auto it = soundSources.cbegin(); it != soundSources.cend(); ++it) {
            if (auto r = writer.addFile(it.value(), it.key()); !r)
                return fail(r.error());
        }
        if (auto r = writer.close(); !r) {
            QFile::remove(partPath);
            return std::unexpected{ QObject::tr("Could not write the theme file: %1").arg(r.error()) };
        }
    }
    QFile::remove(zipPath);
    if (!QFile::rename(partPath, zipPath)) {
        QFile::remove(partPath);
        return std::unexpected{ QObject::tr("Could not write %1").arg(zipPath) };
    }
    return {};
}

// ---------------------------------------------------------------- import

Result<ImportedTheme> importTheme(const QString& zipPath, const QString& themesRoot, const ImportLimits& limits)
{
    if (!QFileInfo(zipPath).isFile())
        return std::unexpected{ QObject::tr("The theme file does not exist: %1").arg(zipPath) };
    const QString root = QDir(themesRoot).absolutePath();
    if (!QDir().mkpath(root))
        return std::unexpected{ QObject::tr("Could not create the themes folder.") };

    // Everything is written below this hidden folder first; it removes itself unless it is renamed into place.
    QTemporaryDir staging(root + QStringLiteral("/.zero-import-XXXXXX"));
    if (!staging.isValid())
        return std::unexpected{ QObject::tr("Could not create a temporary folder for the import.") };
    const QDir stage(staging.path());

    QByteArray json;
    bool haveJson = false;
    QString backgroundEntry, iconEntry, fontEntry;
    QStringList soundEntries;
    bool tooBig = false;

    MMCZip::ArchiveReader reader(zipPath);
    const std::function<Result<>(MMCZip::ArchiveReader::File*)> visit = [&](MMCZip::ArchiveReader::File* f) -> Result<> {
        const QString name = f->filename();
        const bool isJson = name == QLatin1String(kDefinitionFile);
        const bool isBackground = isBackgroundEntry(name);
        const bool isIcon = isIconEntry(name);
        const bool isFont = isFontEntry(name);
        const bool isSound = isSoundFileName(name);
        // Flat entries only: anything with a separator, "..", a drive or an unknown name fails both checks.
        if (!f->isFile() || (!isJson && !isBackground && !isIcon && !isFont && !isSound))
            return std::unexpected{ QObject::tr("The theme file contains an unexpected entry (\"%1\") and was rejected.").arg(name) };

        if (isJson) {
            if (haveJson)
                return std::unexpected{ QObject::tr("The theme file contains two theme definitions and was rejected.") };
            haveJson = true;
            TRY(f->readBlocks([&](const char* data, qint64 len) {
                json.append(data, len);
                tooBig = json.size() > limits.maxJsonBytes;
                return !tooBig;
            }));
            return {};
        }

        QString soundDummy;
        QString* entry = isBackground ? &backgroundEntry : isIcon ? &iconEntry : isFont ? &fontEntry : &soundDummy;
        if (isSound) {
            if (soundEntries.contains(name))
                return std::unexpected{ QObject::tr("The theme file contains two copies of a sound and was rejected.") };
            soundEntries << name;
        } else if (!entry->isEmpty())
            return std::unexpected{ isBackground ? QObject::tr("The theme file contains two background files and was rejected.")
                                                 : isIcon ? QObject::tr("The theme file contains two icons and was rejected.")
                                                          : QObject::tr("The theme file contains two fonts and was rejected.") };
        if (!isSound)
            *entry = name;
        const qint64 cap = isBackground ? limits.maxBackgroundBytes
                           : isIcon     ? limits.maxIconBytes
                           : isFont     ? limits.maxFontBytes
                                        : limits.maxSoundBytes;
        QFile out(stage.absoluteFilePath(name));
        if (!out.open(QIODevice::WriteOnly))
            return std::unexpected{ QObject::tr("Could not write %1: %2").arg(name, out.errorString()) };
        qint64 total = 0;
        bool writeOk = true;
        TRY(f->readBlocks([&](const char* data, qint64 len) {
            total += len;
            tooBig = total > cap;
            if (tooBig)
                return false;
            writeOk = out.write(data, len) == len;
            return writeOk;
        }));
        if (!out.flush() || !writeOk)
            return std::unexpected{ QObject::tr("Could not write %1: %2").arg(name, out.errorString()) };
        out.close();
        return {};
    };
    const auto parsed = reader.parse(visit);
    if (tooBig)
        return std::unexpected{ QObject::tr("The theme file is too large and was rejected.") };
    if (!parsed) {
        return std::unexpected{ parsed.error() == QLatin1String("Aborted") ? QObject::tr("Could not write the imported files (disk full?).")
                                                                            : parsed.error() };
    }
    if (!haveJson)
        return std::unexpected{ QObject::tr("The file is not a ZERO theme (zero-theme.json is missing).") };

    auto def = parseDefinition(json);
    if (!def)
        return std::unexpected{ def.error() };

    // The background the definition points at is the one in the archive; one nobody points at is dropped.
    if (def->backgroundKind != BackgroundKind::None && !backgroundEntry.isEmpty()) {
        def->backgroundFile = backgroundEntry;
    } else {
        if (def->backgroundKind != BackgroundKind::None) {
            def->backgroundKind = BackgroundKind::None;  // declared but not shipped
            def->backgroundFile.clear();
        }
        if (!backgroundEntry.isEmpty())
            QFile::remove(stage.absoluteFilePath(backgroundEntry));
    }

    // The icon and the font the definition points at are the ones in the archive; a shipped file nobody points at is dropped.
    const auto reconcile = [&](QString& declared, const QString& shipped) {
        if (!declared.isEmpty() && !shipped.isEmpty()) {
            declared = shipped;
        } else {
            declared.clear();
            if (!shipped.isEmpty())
                QFile::remove(stage.absoluteFilePath(shipped));
        }
    };
    reconcile(def->iconFile, iconEntry);
    reconcile(def->fontFile, fontEntry);

    // The sounds the definition names are the ones in the archive; shipped files nobody names, and named ones that are missing, are dropped.
    for (auto it = def->sounds.begin(); it != def->sounds.end();) {
        if (soundEntries.contains(it.value()))
            ++it;
        else
            it = def->sounds.erase(it);
    }
    for (const QString& entry : std::as_const(soundEntries))
        if (!def->sounds.contains(soundEventOf(entry)))
            QFile::remove(stage.absoluteFilePath(entry));

    def->name = uniqueName(def->name, existingThemeNames(root));
    const QString slug = uniqueSlug(root, def->name);
    if (auto saved = saveDefinition(staging.path(), *def); !saved)
        return std::unexpected{ saved.error() };

    const QString target = QDir(root).absoluteFilePath(slug);
    if (QFileInfo::exists(target) || !QDir().rename(staging.path(), target))
        return std::unexpected{ QObject::tr("Could not create the theme folder.") };
    staging.setAutoRemove(false);  // it is the theme folder now
    return ImportedTheme{ idForFolderName(slug), target, def->name };
}

// ---------------------------------------------------------------- delete

Result<> trashTheme(const QString& themeFolder)
{
    const QFileInfo info(themeFolder);
    if (!info.isDir() || !QFileInfo::exists(QDir(themeFolder).absoluteFilePath(QString::fromLatin1(kDefinitionFile))))
        return std::unexpected{ QObject::tr("This is not a ZERO theme folder: %1").arg(themeFolder) };
    if (!FS::trash(info.absoluteFilePath()))
        return std::unexpected{ QObject::tr("Could not move the theme to the Recycle Bin. It was not deleted.") };
    return {};
}

}  // namespace Zero::Custom
