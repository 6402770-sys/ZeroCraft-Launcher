// SPDX-License-Identifier: GPL-3.0-only
#include "InstanceProfile.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>
#include "BaseInstance.h"
#include "minecraft/MinecraftInstance.h"
#include "settings/SettingsObject.h"
#include "ui/zero/pages/InstanceInfo.h"

namespace Zero {
namespace Profile {

namespace {

constexpr const char* kFavoriteKey = "ZeroFavorite";
constexpr const char* kTagsKey = "ZeroTags";
constexpr const char* kArtworkKey = "ZeroArtwork";

void changed(BaseInstance* instance, bool artwork = false)
{
    emit instance->propertiesChanged();
    Notifier::instance()->notify(instance->id(), artwork);
}

QString artworkFile(BaseInstance* instance)
{
    return QDir(instance->instanceRoot()).absoluteFilePath(artworkRelativePath());
}

}  // namespace

QString artworkRelativePath()
{
    return QStringLiteral(".zero/artwork.png");
}

Notifier* Notifier::instance()
{
    static Notifier* notifier = new Notifier();  // lives for the whole process
    return notifier;
}

// ----- tags -----

QStringList normalizeTags(const QStringList& tags)
{
    static const QRegularExpression spaces(QStringLiteral("\\s+"));
    QStringList out;
    for (QString tag : tags) {
        tag.replace(spaces, QStringLiteral(" "));
        tag = tag.trimmed();
        while (tag.startsWith(QLatin1Char('#')))
            tag.remove(0, 1);
        tag = tag.trimmed();
        if (tag.size() > kMaxTagLength)
            tag = tag.left(kMaxTagLength).trimmed();
        if (tag.isEmpty())
            continue;
        const bool duplicate = std::any_of(out.cbegin(), out.cend(), [&](const QString& t) { return t.compare(tag, Qt::CaseInsensitive) == 0; });
        if (duplicate)
            continue;
        out << tag;
        if (out.size() == kMaxTags)
            break;
    }
    return out;
}

QStringList parseTags(const QString& json)
{
    if (json.trimmed().isEmpty())
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isArray())
        return {};
    QStringList raw;
    for (const QJsonValue& value : doc.array()) {
        if (value.isString())
            raw << value.toString();
    }
    return normalizeTags(raw);
}

QString serializeTags(const QStringList& tags)
{
    QJsonArray array;
    for (const QString& tag : normalizeTags(tags))
        array.append(tag);
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QList<TagCount> tagCounts(const QList<QStringList>& tagLists)
{
    QList<TagCount> out;
    QHash<QString, int> index;  // lower-cased tag -> position in `out`
    for (const QStringList& list : tagLists) {
        QStringList seen;  // a list that repeats a tag differently cased counts once
        for (const QString& tag : list) {
            const QString key = tag.toLower();
            if (seen.contains(key))
                continue;
            seen << key;
            if (auto it = index.constFind(key); it != index.constEnd()) {
                out[*it].count++;
            } else {
                index.insert(key, int(out.size()));
                out.append({ tag, 1 });
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const TagCount& a, const TagCount& b) { return a.tag.compare(b.tag, Qt::CaseInsensitive) < 0; });
    return out;
}

QList<TagCount> allTags(const QList<BaseInstance*>& instances)
{
    QList<QStringList> lists;
    for (BaseInstance* instance : instances) {
        if (instance)
            lists << tags(instance);
    }
    return tagCounts(lists);
}

// ----- per-instance data -----

bool isFavorite(BaseInstance* instance)
{
    return instance && instance->settings()->get(kFavoriteKey).toBool();
}

void setFavorite(BaseInstance* instance, bool favorite)
{
    if (!instance || isFavorite(instance) == favorite)
        return;
    instance->settings()->set(kFavoriteKey, favorite);
    changed(instance);
}

QStringList tags(BaseInstance* instance)
{
    return instance ? parseTags(instance->settings()->get(kTagsKey).toString()) : QStringList();
}

void setTags(BaseInstance* instance, const QStringList& newTags)
{
    if (!instance)
        return;
    const QStringList normalized = normalizeTags(newTags);
    if (normalized == tags(instance))
        return;
    instance->settings()->set(kTagsKey, serializeTags(normalized));
    changed(instance);
}

QString description(BaseInstance* instance)
{
    return instance ? instance->notes() : QString();
}

void setDescription(BaseInstance* instance, const QString& text)
{
    if (!instance || instance->notes() == text)
        return;
    instance->setNotes(text);
    changed(instance);
}

// ----- artwork -----

QString artworkPath(BaseInstance* instance)
{
    if (!instance)
        return {};
    const QString relative = instance->settings()->get(kArtworkKey).toString();
    if (relative.isEmpty())
        return {};
    const QString root = QDir::cleanPath(QDir(instance->instanceRoot()).absolutePath());
    const QString path = QDir::cleanPath(QDir(root).absoluteFilePath(relative));
    if (!path.startsWith(root + QLatin1Char('/')) || !QFileInfo(path).isFile())
        return {};
    return path;
}

QString setArtwork(BaseInstance* instance, const QString& sourcePath)
{
    if (!instance)
        return QCoreApplication::translate("Zero::Profile", "No instance.");

    QImageReader reader(sourcePath);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull())
        return QCoreApplication::translate("Zero::Profile", "The image could not be read: %1").arg(reader.errorString());
    if (image.width() > kMaxArtworkSide || image.height() > kMaxArtworkSide)
        image = image.scaled(kMaxArtworkSide, kMaxArtworkSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    const QString target = artworkFile(instance);
    if (!QDir().mkpath(QFileInfo(target).absolutePath()))
        return QCoreApplication::translate("Zero::Profile", "The artwork folder could not be created.");

    // QSaveFile writes to a temporary file next to the target and only replaces the target when commit() succeeds.
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly))
        return QCoreApplication::translate("Zero::Profile", "The artwork could not be written: %1").arg(file.errorString());
    if (!image.save(&file, "PNG")) {
        file.cancelWriting();
        return QCoreApplication::translate("Zero::Profile", "The artwork could not be encoded.");
    }
    if (!file.commit())
        return QCoreApplication::translate("Zero::Profile", "The artwork could not be saved: %1").arg(file.errorString());

    instance->settings()->set(kArtworkKey, artworkRelativePath());
    changed(instance, true);
    return {};
}

void clearArtwork(BaseInstance* instance)
{
    if (!instance)
        return;
    QFile::remove(artworkFile(instance));
    if (!instance->settings()->get(kArtworkKey).toString().isEmpty()) {
        instance->settings()->set(kArtworkKey, QString());
    }
    changed(instance, true);
}

// ----- mods -----

std::optional<int> countEnabledMods(const QString& modsFolder)
{
    static QHash<QString, QPair<qint64, int>> cache;
    const QFileInfo folder(modsFolder);
    if (modsFolder.isEmpty() || !folder.isDir())
        return std::nullopt;
    const QString key = folder.absoluteFilePath();
    const qint64 stamp = folder.lastModified().toMSecsSinceEpoch();
    if (auto it = cache.constFind(key); it != cache.constEnd() && it->first == stamp)
        return it->second;

    int count = 0;
    const QFileInfoList entries = QDir(key).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        if (entry.fileName().endsWith(QLatin1String(".jar"), Qt::CaseInsensitive))
            ++count;
    }
    cache.insert(key, { stamp, count });
    return count;
}

std::optional<int> modCount(MinecraftInstance* instance)
{
    if (!instance || InstanceInfo::of(instance).loader.isEmpty())
        return std::nullopt;
    return countEnabledMods(instance->modsRoot());
}

LibraryFilter::Facts libraryFacts(MinecraftInstance* instance, const QString& group)
{
    LibraryFilter::Facts facts;
    if (!instance)
        return facts;
    const InstanceInfo info = InstanceInfo::of(instance);
    facts.name = instance->name();
    facts.group = group;
    facts.version = info.minecraftVersion;
    facts.loader = info.loader;
    facts.tags = tags(instance);
    facts.favorite = isFavorite(instance);
    facts.running = instance->isRunning();
    return facts;
}

}  // namespace Profile
}  // namespace Zero
