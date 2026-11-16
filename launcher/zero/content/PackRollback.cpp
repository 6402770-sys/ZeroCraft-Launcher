// SPDX-License-Identifier: GPL-3.0-only
#include "PackRollback.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>
#include <climits>
#include <functional>

#include "archive/ArchiveReader.h"
#include "zero/content/PackOrder.h"
#include "zero/mods/Versions.h"

namespace Zero {
namespace Content {
namespace Packs {
namespace Rollback {

namespace {

constexpr char kRollbackLine[] = "Caught error loading resourcepacks, removing all selected resourcepacks";
constexpr int kMarkerFormat = 1;

bool isBuiltIn(const QString& id)
{
    return id == QLatin1String("vanilla") || id == QLatin1String("programmer_art") || id == QLatin1String("high_contrast");
}

/// An entry that starts a new item of the list: `file/...`, a built-in or an id with a namespace (`continuity:glass_pane_culling_fix`).
bool looksLikeEntryStart(const QString& token)
{
    static const QRegularExpression namespaced(QStringLiteral("^[a-z0-9_.\\-]+:[a-z0-9_./\\-]+$"));
    return token.startsWith(QLatin1String("file/")) || namespaced.match(token).hasMatch();
}

QString stripBom(QString text)
{
    if (!text.isEmpty() && text.at(0) == QChar(0xFEFF)) {
        text.remove(0, 1);
    }
    return text;
}

/// "minecraft:core/entity" -> ns "minecraft", path "core/entity"
bool splitShaderId(QString id, QString* ns, QString* path)
{
    for (const char* ext : { ".vsh", ".fsh", ".glsl" }) {
        if (id.endsWith(QLatin1String(ext))) {
            id.chop(int(qstrlen(ext)));
            break;
        }
    }
    const int colon = id.indexOf(QLatin1Char(':'));
    if (colon <= 0 || colon == id.size() - 1) {
        return false;
    }
    *ns = id.left(colon);
    *path = id.mid(colon + 1);
    return !path->contains(QLatin1String(".."));
}

std::optional<int> majorOf(const QJsonValue& value)
{
    if (value.isDouble()) {
        return int(value.toDouble());
    }
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        if (!array.isEmpty() && array.first().isDouble()) {
            return int(array.first().toDouble());
        }
    }
    return std::nullopt;
}

/// What a pack provides: the names of its files (a zip: read once; a folder: asked one by one) and its pack.mcmeta.
struct PackFiles {
    QSet<QString> names;       ///< zip: every file, '/'-separated
    QString folder;            ///< folder pack: its path; empty for a zip
    QByteArray mcmeta;
    bool ok = false;

    bool has(const QString& relative) const
    {
        if (!folder.isEmpty()) {
            return QFileInfo(QDir(folder).filePath(relative)).isFile();
        }
        return names.contains(relative);
    }
};

PackFiles openPack(const QString& path)
{
    PackFiles pack;
    const QFileInfo info(path);
    if (info.isDir()) {
        pack.folder = info.absoluteFilePath();
        QFile mcmeta(QDir(pack.folder).filePath(QStringLiteral("pack.mcmeta")));
        if (mcmeta.open(QIODevice::ReadOnly)) {
            pack.mcmeta = mcmeta.readAll();
        }
        pack.ok = true;
        return pack;
    }
    if (!info.isFile()) {
        return pack;
    }
    MMCZip::ArchiveReader reader(path);
    QSet<QString> names;
    QByteArray mcmeta;
    const std::function<::Result<>(MMCZip::ArchiveReader::File*)> visit = [&](MMCZip::ArchiveReader::File* file) -> ::Result<> {
        if (!file->isFile()) {
            return file->skip();
        }
        QString name = file->filename();
        name.replace(QLatin1Char('\\'), QLatin1Char('/'));
        names.insert(name);
        if (name == QLatin1String("pack.mcmeta")) {
            if (auto data = file->readAll()) {
                mcmeta = *data;
                return {};
            }
        }
        return file->skip();
    };
    if (!reader.parse(visit)) {
        return pack;
    }
    pack.names = names;
    pack.mcmeta = mcmeta;
    pack.ok = true;
    return pack;
}

/// The paths a pack would have to contain to provide the shader, with the directory prefixes to try ("" and the applicable overlays).
bool providesShader(const PackFiles& pack, const QStringList& prefixes, const QString& ns, const QString& path)
{
    for (const QString& prefix : prefixes) {
        for (const char* ext : { ".vsh", ".fsh", ".glsl" }) {
            const QString relative = (prefix.isEmpty() ? QString() : prefix + QLatin1Char('/')) + QStringLiteral("assets/") + ns +
                                     QStringLiteral("/shaders/") + path + QLatin1String(ext);
            if (pack.has(relative)) {
                return true;
            }
        }
    }
    return false;
}

QJsonObject toJson(const Marker& marker)
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), kMarkerFormat);
    root.insert(QStringLiteral("time"), marker.time.toUTC().toString(Qt::ISODate));
    root.insert(QStringLiteral("attempted"), QJsonArray::fromStringList(marker.attempted));
    QJsonArray suspects;
    for (const Suspect& suspect : marker.suspects) {
        QJsonObject item;
        item.insert(QStringLiteral("file"), suspect.file);
        item.insert(QStringLiteral("shader"), suspect.shader);
        suspects.append(item);
    }
    root.insert(QStringLiteral("suspects"), suspects);
    root.insert(QStringLiteral("asked"), marker.asked);
    return root;
}

}  // namespace

// ---- the log --------------------------------------------------------------------------------------------------------------------

QStringList splitReloadList(const QString& list)
{
    const QStringList raw = list.split(QStringLiteral(", "), Qt::SkipEmptyParts);
    QStringList joined;
    bool inFiles = false;  // after the first `file/` item only `file/` items and namespaced ids follow (mod ids come before)
    for (const QString& piece : raw) {
        const QString token = piece.trimmed();
        if (token.isEmpty()) {
            continue;
        }
        if (inFiles && !looksLikeEntryStart(token) && !joined.isEmpty()) {
            joined.last() += QStringLiteral(", ") + token;  // a pack whose own name has ", " in it
            continue;
        }
        joined.append(token);
        if (token.startsWith(QLatin1String("file/"))) {
            inFiles = true;
        }
    }
    return joined;
}

LogAnalysis analyzeLog(const QString& logText)
{
    static const QRegularExpression reloadRe(QStringLiteral("Reloading ResourceManager:\\s*(.*)$"));
    static const QRegularExpression shaderRe(QStringLiteral("Couldn't parse GLSL:\\s*([a-z0-9_.\\-]+:[a-z0-9_./\\-]+?):\\d+"),
                                             QRegularExpression::CaseInsensitiveOption);
    LogAnalysis out;
    QString attemptedLine;
    QStringList shaders;
    const QStringList lines = logText.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        if (line.contains(QLatin1String(kRollbackLine))) {
            out.rolledBack = true;
            break;
        }
        const auto reload = reloadRe.match(line);
        if (reload.hasMatch()) {
            attemptedLine = reload.captured(1).trimmed();
            shaders.clear();
            continue;
        }
        const auto shader = shaderRe.match(line);
        if (shader.hasMatch()) {
            const QString id = shader.captured(1);
            if (!shaders.contains(id)) {
                shaders.append(id);
            }
        }
    }
    QStringList entries;
    for (const QString& token : splitReloadList(attemptedLine)) {
        if (!(isBuiltIn(token) || token.startsWith(QLatin1String("file/")) || token.contains(QLatin1Char(':')))) {
            continue;  // a mod's own resources
        }
        if (!entries.contains(token)) {
            entries.append(token);
        }
    }
    out.attempted = entries;
    if (out.rolledBack) {
        out.failingShaders = shaders;
    }
    return out;
}

// ---- the shaders ----------------------------------------------------------------------------------------------------------------

QList<Overlay> parseOverlays(const QByteArray& mcmeta)
{
    QList<Overlay> overlays;
    QByteArray bytes = mcmeta;
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        bytes.remove(0, 3);
    }
    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject()) {
        return overlays;
    }
    const QJsonArray entries = doc.object().value(QStringLiteral("overlays")).toObject().value(QStringLiteral("entries")).toArray();
    for (const QJsonValue& value : entries) {
        const QJsonObject entry = value.toObject();
        const QString directory = entry.value(QStringLiteral("directory")).toString();
        if (directory.isEmpty() || directory.contains(QLatin1String("..")) || directory.startsWith(QLatin1Char('/')) ||
            directory.contains(QLatin1Char('\\'))) {
            continue;
        }
        Overlay overlay;
        overlay.directory = directory;
        const QJsonValue formats = entry.value(QStringLiteral("formats"));
        if (formats.isDouble()) {
            overlay.minFormat = overlay.maxFormat = int(formats.toDouble());
        } else if (formats.isArray()) {
            const QJsonArray array = formats.toArray();
            if (array.size() >= 2) {
                overlay.minFormat = int(array.at(0).toDouble(-1));
                overlay.maxFormat = int(array.at(1).toDouble(-1));
            } else if (array.size() == 1) {
                overlay.minFormat = overlay.maxFormat = int(array.at(0).toDouble(-1));
            }
        } else if (formats.isObject()) {
            const QJsonObject range = formats.toObject();
            overlay.minFormat = majorOf(range.value(QStringLiteral("min_inclusive"))).value_or(-1);
            overlay.maxFormat = majorOf(range.value(QStringLiteral("max_inclusive"))).value_or(-1);
        }
        if (const auto minimum = majorOf(entry.value(QStringLiteral("min_format")))) {
            overlay.minFormat = *minimum;
        }
        if (const auto maximum = majorOf(entry.value(QStringLiteral("max_format")))) {
            overlay.maxFormat = *maximum;
        }
        overlays.append(overlay);
    }
    return overlays;
}

bool overlayApplies(const Overlay& overlay, std::optional<int> format)
{
    if (!format) {
        return true;
    }
    if (overlay.minFormat >= 0 && *format < overlay.minFormat) {
        return false;
    }
    if (overlay.maxFormat >= 0 && *format > overlay.maxFormat) {
        return false;
    }
    return true;
}

std::optional<int> formatForVersion(const FormatMap& mappings, const QString& mcVersion)
{
    const QString mc = mcVersion.trimmed();
    if (mc.isEmpty()) {
        return std::nullopt;
    }
    for (auto it = mappings.constBegin(); it != mappings.constEnd(); ++it) {
        const auto low = Mods::compareVersions(it.value().first.toString(), mc);
        const auto high = Mods::compareVersions(mc, it.value().second.toString());
        if (low && high && *low <= 0 && *high <= 0) {
            return it.key().first;
        }
    }
    return std::nullopt;
}

std::optional<int> formatFromVersionJson(const QByteArray& versionJson)
{
    const QJsonDocument doc = QJsonDocument::fromJson(versionJson);
    if (!doc.isObject()) {
        return std::nullopt;
    }
    const QJsonValue pv = doc.object().value(QStringLiteral("pack_version"));
    if (pv.isDouble()) {
        return pv.toInt();
    }
    if (pv.isObject()) {
        for (const char* key : { "resource_major", "resource" }) {
            const QJsonValue v = pv.toObject().value(QLatin1String(key));
            if (v.isDouble()) {
                return v.toInt();
            }
        }
    }
    return std::nullopt;
}

std::optional<int> formatFromClientJar(const QString& jarPath)
{
    if (jarPath.isEmpty() || !QFileInfo(jarPath).isFile()) {
        return std::nullopt;
    }
    MMCZip::ArchiveReader reader(jarPath);
    QByteArray json;
    const std::function<::Result<>(MMCZip::ArchiveReader::File*)> visit = [&](MMCZip::ArchiveReader::File* file) -> ::Result<> {
        if (file->isFile() && file->filename() == QLatin1String("version.json")) {
            if (auto data = file->readAll()) {
                json = *data;
            }
        }
        return file->skip();
    };
    if (!reader.parse(visit) && json.isEmpty()) {
        return std::nullopt;
    }
    return formatFromVersionJson(json);
}

QList<Suspect> findSuspects(const QStringList& attempted, const QStringList& failingShaders, const QString& resourcePacksDir,
                            std::optional<int> format)
{
    QList<Suspect> suspects;
    if (failingShaders.isEmpty()) {
        return suspects;
    }
    // top-most first
    QStringList files;
    for (int i = attempted.size() - 1; i >= 0; --i) {
        if (attempted.at(i).startsWith(QLatin1String("file/"))) {
            files.append(attempted.at(i).mid(5));
        }
    }
    QList<PackFiles> packs;
    QList<QStringList> prefixes;
    for (const QString& file : files) {
        PackFiles pack = openPack(QDir(resourcePacksDir).filePath(file));
        QStringList dirs{ QString() };
        if (pack.ok) {
            for (const Overlay& overlay : parseOverlays(pack.mcmeta)) {
                if (overlayApplies(overlay, format)) {
                    dirs.append(overlay.directory);
                }
            }
        }
        packs.append(pack);
        prefixes.append(dirs);
    }
    for (const QString& shader : failingShaders) {
        QString ns;
        QString path;
        if (!splitShaderId(shader, &ns, &path)) {
            continue;
        }
        for (int i = 0; i < files.size(); ++i) {
            if (packs.at(i).ok && providesShader(packs.at(i), prefixes.at(i), ns, path)) {
                suspects.append({ files.at(i), shader });
                break;  // the highest pack that has it wins in the game: the ones below are shadowed
            }
        }
    }
    return suspects;
}

// ---- the marker -----------------------------------------------------------------------------------------------------------------

QString markerPathFor(const QString& instanceRoot)
{
    return QDir(instanceRoot).filePath(QStringLiteral(".zero/") + QLatin1String(kMarkerFileName));
}

QString markerPathForHistory(const QString& historyPath)
{
    return QDir(QFileInfo(historyPath).absolutePath()).filePath(QLatin1String(kMarkerFileName));
}

Marker readMarker(const QString& markerPath)
{
    Marker marker;
    QFile file(markerPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return marker;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return marker;
    }
    const QJsonObject root = doc.object();
    marker.time = QDateTime::fromString(root.value(QStringLiteral("time")).toString(), Qt::ISODate);
    for (const QJsonValue& value : root.value(QStringLiteral("attempted")).toArray()) {
        if (value.isString()) {
            marker.attempted.append(value.toString());
        }
    }
    for (const QJsonValue& value : root.value(QStringLiteral("suspects")).toArray()) {
        const QJsonObject item = value.toObject();
        const QString name = item.value(QStringLiteral("file")).toString();
        if (!name.isEmpty()) {
            marker.suspects.append({ name, item.value(QStringLiteral("shader")).toString() });
        }
    }
    marker.asked = root.value(QStringLiteral("asked")).toBool();
    marker.valid = !marker.attempted.isEmpty();
    return marker;
}

bool writeMarker(const QString& markerPath, const Marker& marker)
{
    if (!QDir().mkpath(QFileInfo(markerPath).absolutePath())) {
        return false;
    }
    QSaveFile file(markerPath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(toJson(marker)).toJson(QJsonDocument::Indented));
    return file.commit();
}

bool clearMarker(const QString& markerPath)
{
    return !QFileInfo::exists(markerPath) || QFile::remove(markerPath);
}

bool markAsked(const QString& markerPath)
{
    Marker marker = readMarker(markerPath);
    if (!marker.valid) {
        return false;
    }
    marker.asked = true;
    return writeMarker(markerPath, marker);
}

QStringList attemptedFilesTopFirst(const QStringList& attempted)
{
    QStringList files;
    for (int i = attempted.size() - 1; i >= 0; --i) {
        if (isFileEntry(attempted.at(i))) {
            const QString name = fileNameOf(attempted.at(i));
            if (!name.isEmpty() && !files.contains(name)) {
                files.append(name);
            }
        }
    }
    return files;
}

QStringList suspectFiles(const QList<Suspect>& suspects)
{
    QStringList files;
    for (const Suspect& suspect : suspects) {
        if (!files.contains(suspect.file)) {
            files.append(suspect.file);
        }
    }
    return files;
}

// ---- after the game stopped -----------------------------------------------------------------------------------------------------

DetectResult detectAfterLaunch(const DetectInput& input)
{
    const QFileInfo log(QDir(input.gameRoot).filePath(QStringLiteral("logs/latest.log")));
    if (!log.isFile()) {
        return DetectResult::NoLog;
    }
    if (input.launchStart.isValid() && log.lastModified() < input.launchStart) {
        return DetectResult::Stale;
    }
    QFile file(log.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return DetectResult::NoLog;
    }
    const LogAnalysis analysis = analyzeLog(QString::fromUtf8(file.readAll()));
    file.close();

    const QString markerPath = markerPathFor(input.instanceRoot);
    const bool hasFiles = std::any_of(analysis.attempted.begin(), analysis.attempted.end(), [](const QString& e) { return isFileEntry(e); });
    if (!hasFiles) {
        return DetectResult::NoPacks;
    }
    if (!analysis.rolledBack) {
        clearMarker(markerPath);
        return DetectResult::LoadedFine;
    }
    Marker marker;
    marker.valid = true;
    marker.time = QDateTime::currentDateTimeUtc();
    marker.attempted = analysis.attempted;
    marker.suspects = findSuspects(analysis.attempted, analysis.failingShaders, QDir(input.gameRoot).filePath(QStringLiteral("resourcepacks")),
                                   input.format);
    marker.asked = false;
    return writeMarker(markerPath, marker) ? DetectResult::Recorded : DetectResult::WriteFailed;
}

// ---- what the user is told ------------------------------------------------------------------------------------------------------

QString namesText(const QStringList& files)
{
    QStringList names;
    for (const QString& file : files) {
        const QString name = packNameOf(file, false);  // drops ".zip"; a folder keeps its name
        names.append(name.trimmed().isEmpty() ? file : name);
    }
    if (names.size() <= 1) {
        return names.value(0);
    }
    const QString last = names.takeLast();
    return names.join(QStringLiteral(", ")) + QObject::tr(" and ") + last;
}

QString noticeTitle()
{
    return QObject::tr("Minecraft turned off your resource packs");
}

QString noticeBody(const QStringList& files)
{
    if (files.isEmpty()) {
        return QObject::tr("When the game started, one of your packs failed to load, so Minecraft turned every pack off.");
    }
    if (files.size() == 1) {
        return QObject::tr("When the game started, %1 failed to load (it changes the game's shaders), so Minecraft turned every pack off.")
            .arg(namesText(files));
    }
    return QObject::tr("When the game started, %1 failed to load (they change the game's shaders), so Minecraft turned every pack off.")
        .arg(namesText(files));
}

QString restoreWithoutText(const QStringList& files)
{
    return QObject::tr("Restore without %1").arg(namesText(files));
}

QString restoreAllText()
{
    return QObject::tr("Restore all");
}

QString dismissText()
{
    return QObject::tr("Dismiss");
}

QString brokeLastLaunchText()
{
    return QObject::tr("Broke the last launch");
}

QString brokeTooltip(const QString& shader)
{
    return shader.isEmpty() ? QObject::tr("This pack made the game turn every pack off.")
                            : QObject::tr("It replaces the game's shader %1, which failed to compile.").arg(shader);
}

QString restoredAllText()
{
    return QObject::tr("Turned your resource packs back on");
}

QString restoredWithoutText(const QStringList& files)
{
    return QObject::tr("Turned your resource packs back on, without %1").arg(namesText(files));
}

QString promptTitle()
{
    return QObject::tr("Minecraft turned off your resource packs last time");
}

QString promptBody(const QStringList& files)
{
    if (files.isEmpty()) {
        return QObject::tr("One of your packs failed to load when the game started, so Minecraft turned every pack off. "
                           "Do you want your packs back before you play?");
    }
    return QObject::tr("%1 failed to load when the game started, so Minecraft turned every pack off. "
                       "Do you want your other packs back before you play?")
        .arg(namesText(files));
}

QString promptRestoreAndPlayText(const QStringList& files)
{
    return files.isEmpty() ? QObject::tr("Restore and play") : QObject::tr("Restore without %1 and play").arg(namesText(files));
}

QString promptPlayWithoutText()
{
    return QObject::tr("Play without them");
}

QStringList restoreOrder(const Marker& marker, bool withoutSuspects, const QStringList& activeTopFirst, const QSet<QString>& existing)
{
    QStringList files = attemptedFilesTopFirst(marker.attempted);
    if (withoutSuspects) {
        const QStringList bad = suspectFiles(marker.suspects);
        files.erase(std::remove_if(files.begin(), files.end(), [&bad](const QString& name) { return bad.contains(name); }), files.end());
    }
    return orderAfterTurningOn(files, activeTopFirst, existing);
}

bool shouldAskBeforeLaunch(const Marker& marker, const QStringList& activeFiles)
{
    return marker.valid && !marker.asked && activeFiles.isEmpty() && !attemptedFilesTopFirst(marker.attempted).isEmpty();
}

}  // namespace Rollback
}  // namespace Packs
}  // namespace Content
}  // namespace Zero
