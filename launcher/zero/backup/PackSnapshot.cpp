// SPDX-License-Identifier: GPL-3.0-only
#include "PackSnapshot.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStorageInfo>
#include <QTimeZone>
#include <algorithm>

#include "FileSystem.h"
#include "WorldBackup.h"

namespace Zero {
namespace Backup {
namespace Pack {

namespace {

constexpr qint64 kSlackBytes = 50LL * 1024 * 1024;  // free space beyond the data itself

QString tr(const char* text)
{
    return QCoreApplication::translate("Zero::Backup::Pack", text);
}

QString sizeText(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

bool defaultTrash(const QString& path)
{
    return FS::trash(path);  // deliberately no fallback: a folder that cannot go to the Recycle Bin stays where it is
}

qint64 defaultFreeBytes(const QString& path)
{
    const QStorageInfo info(path);
    return info.isValid() && info.isReady() ? info.bytesAvailable() : -1;
}

Hooks resolve(const Hooks& in)
{
    Hooks h = in;
    if (!h.trashPath)
        h.trashPath = defaultTrash;
    if (!h.rename)
        h.rename = [](const QString& from, const QString& to) { return Backup::renamePath(from, to); };
    if (!h.freeBytes)
        h.freeBytes = defaultFreeBytes;
    if (!h.nowMs)
        h.nowMs = []() { return QDateTime::currentMSecsSinceEpoch(); };
    if (!h.isLink) {
        h.isLink = [](const QString& path) {
            const QFileInfo fi(path);
            return fi.isSymLink() || fi.isJunction();  // what the zip engine skips
        };
    }
    return h;
}

bool samePath(const QString& a, const QString& b)
{
    return QDir::cleanPath(QFileInfo(a).absoluteFilePath()).compare(QDir::cleanPath(QFileInfo(b).absoluteFilePath()),
#ifdef Q_OS_WIN
                                                                    Qt::CaseInsensitive
#else
                                                                    Qt::CaseSensitive
#endif
                                                                    ) == 0;
}

bool equalName(const QString& a, const QString& b)
{
#ifdef Q_OS_WIN
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}

// ---------------------------------------------------------------- names, sidecar

struct NameParts {
    QString stamp;  // yyyyMMddHHmmss (UTC): compares like the time
    int seq = 1;
};

bool parseName(const QString& fileName, NameParts* out)
{
    static const QRegularExpression re(QStringLiteral("^pack-(\\d{8})-(\\d{6})(?:-(\\d+))?\\.zip$"));
    const QRegularExpressionMatch m = re.match(fileName);
    if (!m.hasMatch())
        return false;
    out->stamp = m.captured(1) + m.captured(2);
    out->seq = m.captured(3).isEmpty() ? 1 : m.captured(3).toInt();
    return true;
}

bool newerName(const NameParts& a, const NameParts& b)
{
    return a.stamp != b.stamp ? a.stamp > b.stamp : a.seq > b.seq;
}

QString uniqueName(const QString& snapshotsDir, qint64 nowMs)
{
    const QString stamp = QDateTime::fromMSecsSinceEpoch(nowMs, QTimeZone::utc()).toString(QStringLiteral("yyyyMMdd-HHmmss"));
    for (int n = 1;; ++n) {
        const QString suffix = n == 1 ? QString() : QStringLiteral("-%1").arg(n);
        const QString name = QStringLiteral("pack-%1%2.zip").arg(stamp, suffix);
        const QString path = QDir(snapshotsDir).filePath(name);
        if (!QFileInfo::exists(path) && !QFileInfo::exists(path + QStringLiteral(".part")))
            return name;
    }
}

QString sidecarPath(const QString& zipPath)
{
    return zipPath.left(zipPath.size() - 4) + QStringLiteral(".json");  // "<name>.zip" -> "<name>.json"
}

QJsonObject metaToJson(const Meta& m, qint64 createdMs)
{
    return QJsonObject{ { "instanceId", m.instanceId }, { "instanceName", m.instanceName }, { "packId", m.packId },
                        { "packName", m.packName },     { "oldVersion", m.oldVersion },     { "newVersion", m.newVersion },
                        { "gameDir", m.gameDir },       { "createdMs", double(createdMs) } };
}

void metaFromJson(const QJsonObject& o, Info* info)
{
    info->meta.instanceId = o.value("instanceId").toString();
    info->meta.instanceName = o.value("instanceName").toString();
    info->meta.packId = o.value("packId").toString();
    info->meta.packName = o.value("packName").toString();
    info->meta.oldVersion = o.value("oldVersion").toString();
    info->meta.newVersion = o.value("newVersion").toString();
    info->meta.gameDir = o.value("gameDir").toString();
    info->createdMs = qint64(o.value("createdMs").toDouble());
}

bool writeSidecar(const Info& info)
{
    QJsonObject o = metaToJson(info.meta, info.createdMs);
    o.insert(QStringLiteral("format"), kFormat);
    o.insert(QStringLiteral("outcome"), info.outcome);
    o.insert(QStringLiteral("rolledBack"), info.rolledBack);
    QSaveFile file(sidecarPath(info.zipPath));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return file.commit();
}

/// The sidecar when there is a readable one, else what the zip's manifest says (outcome unknown = pending).
Info readInfo(const QString& zipPath)
{
    Info info;
    info.zipPath = QFileInfo(zipPath).absoluteFilePath();
    info.fileSize = QFileInfo(zipPath).size();
    QFile file(sidecarPath(info.zipPath));
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        const QJsonObject o = doc.object();
        if (doc.isObject() && o.value("format").toInt(-1) == kFormat) {
            metaFromJson(o, &info);
            const QString outcome = o.value("outcome").toString();
            info.outcome = outcome == QLatin1String(kOutcomeOk) || outcome == QLatin1String(kOutcomeFailed) ? outcome : QString::fromLatin1(kOutcomePending);
            info.rolledBack = o.value("rolledBack").toBool();
            info.valid = true;
            return info;
        }
    }
    const Backup::ManifestInfo manifest = Backup::readBackupInfo(info.zipPath);
    if (!manifest.ok) {
        info.error = manifest.error;
        return info;
    }
    metaFromJson(manifest.extra, &info);
    if (info.meta.instanceId.isEmpty())
        info.meta.instanceId = manifest.meta.instanceId;
    if (info.createdMs == 0)
        info.createdMs = manifest.createdMs;
    info.valid = true;
    return info;
}

QString joinRel(const QString& dir, const QString& name)
{
    return dir.isEmpty() ? name : dir + QLatin1Char('/') + name;
}

}  // namespace

// ---------------------------------------------------------------- pure helpers

QString snapshotsDirFor(const QString& dataRoot, const QString& instanceId)
{
    return QDir(dataRoot).filePath(QStringLiteral("zero/snapshots/") + instanceId);
}

QString relativeGameDir(const QString& instanceRoot, const QString& gameRoot)
{
    const QString root = QDir::cleanPath(QFileInfo(instanceRoot).absoluteFilePath());
    const QString game = QDir::cleanPath(QFileInfo(gameRoot).absoluteFilePath());
    if (equalName(root, game))
        return QString();
    if (!game.startsWith(root + QLatin1Char('/'), Qt::CaseInsensitive))
        return QString();
    return game.mid(root.size() + 1);
}

bool isExcluded(const QString& rel, const QString& gameDir)
{
    const QStringList segments = rel.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& s : segments) {
        if (s.startsWith(QLatin1String(".zero-"), Qt::CaseInsensitive))
            return true;
    }
    const QStringList game = gameDir.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (segments.size() <= game.size())
        return false;
    for (int i = 0; i < game.size(); ++i) {
        if (!equalName(segments.at(i), game.at(i)))
            return false;
    }
    const QString& top = segments.at(game.size());
    static const QStringList names = { QStringLiteral("saves"), QStringLiteral("screenshots"), QStringLiteral("logs"),
                                       QStringLiteral("crash-reports"), QStringLiteral(".cache") };
    return std::any_of(names.begin(), names.end(), [&](const QString& n) { return equalName(top, n); });
}

QStringList playDataFolders(const QString& gameDir)
{
    return { joinRel(gameDir, QStringLiteral("saves")), joinRel(gameDir, QStringLiteral("screenshots")), joinRel(gameDir, QStringLiteral("logs")),
             joinRel(gameDir, QStringLiteral("crash-reports")) };
}

QString mergeInstanceCfg(const QString& current, const QString& snapshot)
{
    const QString eol = current.contains(QLatin1String("\r\n")) ? QStringLiteral("\r\n") : QStringLiteral("\n");
    auto lines = [](const QString& text) {
        QStringList l = text.split(QLatin1Char('\n'));
        for (QString& s : l) {
            if (s.endsWith(QLatin1Char('\r')))
                s.chop(1);
        }
        return l;
    };
    auto header = [](const QString& line, QString* name) {
        const QString t = line.trimmed();
        if (t.size() < 2 || !t.startsWith(QLatin1Char('[')) || !t.endsWith(QLatin1Char(']')))
            return false;
        *name = t.mid(1, t.size() - 2).trimmed();
        return true;
    };
    auto isManaged = [](const QString& line) {
        const QString t = line.trimmed();
        if (t.isEmpty() || t.startsWith(QLatin1Char('[')) || t.startsWith(QLatin1Char('#')) || t.startsWith(QLatin1Char(';')))
            return false;
        return t.section(QLatin1Char('='), 0, 0).trimmed().startsWith(QLatin1String("ManagedPack"));
    };
    const QString general = QStringLiteral("General");

    QStringList wanted;  // the snapshot's ManagedPack* lines of [General]
    {
        QString section = general;
        for (const QString& line : lines(snapshot)) {
            QString name;
            if (header(line, &name))
                section = name;
            else if (equalName(section, general) && isManaged(line))
                wanted.append(line.trimmed());
        }
    }

    QStringList out;
    int lastGeneral = -1;  // index in `out` of the last non-empty line that belongs to [General]
    bool sawGeneral = false;
    QString section = general;
    for (const QString& line : lines(current)) {
        QString name;
        if (header(line, &name)) {
            section = name;
            if (equalName(section, general))
                sawGeneral = true;
        } else if (equalName(section, general) && isManaged(line)) {
            continue;  // replaced by the snapshot's keys
        }
        out.append(line);
        if (equalName(section, general) && !line.trimmed().isEmpty())
            lastGeneral = int(out.size()) - 1;
    }
    if (!wanted.isEmpty()) {
        if (lastGeneral >= 0) {
            for (int i = 0; i < wanted.size(); ++i)
                out.insert(lastGeneral + 1 + i, wanted.at(i));
        } else if (!sawGeneral) {
            while (!out.isEmpty() && out.last().trimmed().isEmpty())
                out.removeLast();
            out.append(QStringLiteral("[General]"));
            out.append(wanted);
            out.append(QString());
        }
    }
    return out.join(eol);
}

// ---------------------------------------------------------------- create / list / retention

CreateResult createSnapshot(const QString& instanceRoot, const QString& snapshotsDir, const Meta& meta, const Hooks& hooksIn, const Control& control)
{
    CreateResult result;
    const Hooks hooks = resolve(hooksIn);
    auto fail = [&](const QString& message, bool cancelled = false) {
        result.ok = false;
        result.cancelled = cancelled;
        result.error = message;
        result.zipPath.clear();
        return result;
    };
    if (!QFileInfo(instanceRoot).isDir())
        return fail(tr("The instance folder does not exist."));

    const qint64 now = hooks.nowMs();
    Backup::Hooks backupHooks;
    backupHooks.rename = hooks.rename;
    backupHooks.freeBytes = hooks.freeBytes;

    Backup::Options options;
    const QString gameDir = meta.gameDir;
    options.exclude = [gameDir](const QString& rel, bool) { return isExcluded(rel, gameDir); };
    options.fileName = uniqueName(snapshotsDir, now);
    options.unsafeName = [](const QString& rel) {
        return tr("This instance contains a file Windows cannot copy: %1. Rename or remove it, then update again.").arg(rel);
    };
    options.extra = metaToJson(meta, now);
    options.cancelled = control.cancelled;
    options.progress = control.progress;

    const Backup::BackupMeta backupMeta{ meta.instanceId, meta.instanceName, QString(), meta.packName };
    const Backup::BackupResult made = Backup::createBackup(instanceRoot, snapshotsDir, Backup::Kind::Safety, backupMeta, backupHooks, options);
    if (!made.ok)
        return fail(made.error, made.cancelled);

    Info info;
    info.zipPath = made.zipPath;
    info.meta = meta;
    info.createdMs = now;
    if (!writeSidecar(info)) {
        QFile::remove(made.zipPath);  // ZERO's own copy
        return fail(tr("Could not write the details of the saved copy."));
    }
    applyRetention(snapshotsDir, { made.zipPath });
    result.ok = true;
    result.zipPath = made.zipPath;
    return result;
}

QList<Info> listSnapshots(const QString& snapshotsDir)
{
    struct Entry {
        NameParts name;
        Info info;
    };
    QList<Entry> entries;
    const QFileInfoList zips = QDir(snapshotsDir).entryInfoList({ QStringLiteral("pack-*.zip") }, QDir::Files, QDir::NoSort);
    for (const QFileInfo& zip : zips) {
        Entry e;
        if (!parseName(zip.fileName(), &e.name))
            continue;
        e.info = readInfo(zip.absoluteFilePath());
        entries.append(e);
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return newerName(a.name, b.name); });
    QList<Info> out;
    for (const Entry& e : std::as_const(entries))
        out.append(e.info);
    return out;
}

Info newestActive(const QList<Info>& snapshots, const QString& packId)
{
    for (const Info& s : snapshots) {
        if (!s.valid || s.rolledBack)
            continue;
        if (!packId.isEmpty() && !s.meta.packId.isEmpty() && s.meta.packId != packId)
            continue;
        return s;
    }
    return {};
}

bool markOutcome(const QString& zipPath, const QString& outcome)
{
    Info info = readInfo(zipPath);
    if (!info.valid)
        return false;
    info.outcome = outcome;
    return writeSidecar(info);
}

bool markRolledBack(const QString& zipPath)
{
    Info info = readInfo(zipPath);
    if (!info.valid)
        return false;
    info.rolledBack = true;
    return writeSidecar(info);
}

int applyRetention(const QString& snapshotsDir, const QStringList& protectedZips, int keep)
{
    struct Candidate {
        QString path;
        NameParts name;
    };
    QList<Candidate> list;
    const QFileInfoList zips = QDir(snapshotsDir).entryInfoList({ QStringLiteral("pack-*.zip") }, QDir::Files, QDir::NoSort);
    for (const QFileInfo& fi : zips) {
        Candidate c;
        if (!parseName(fi.fileName(), &c.name) || fi.isSymLink())
            continue;  // not ZERO's: never touched
        c.path = fi.absoluteFilePath();
        list.append(c);
    }
    std::sort(list.begin(), list.end(), [](const Candidate& a, const Candidate& b) { return newerName(a.name, b.name); });

    int removed = 0;
    for (int i = std::max(0, keep); i < list.size(); ++i) {
        const QString& path = list.at(i).path;
        const bool isProtected = std::any_of(protectedZips.begin(), protectedZips.end(), [&](const QString& p) { return !p.isEmpty() && samePath(p, path); });
        if (isProtected)
            continue;
        if (QFile::remove(path)) {
            QFile::remove(sidecarPath(path));
            ++removed;
        } else {
            qWarning() << "ZERO pack snapshots: could not remove" << path;
        }
    }
    // a `.part` left by a crash is a half-written copy of ours (an hour is longer than any copy in progress is left alone)
    const QFileInfoList parts = QDir(snapshotsDir).entryInfoList({ QStringLiteral("pack-*.zip.part") }, QDir::Files, QDir::NoSort);
    for (const QFileInfo& fi : parts) {
        if (!fi.isSymLink() && fi.lastModified().secsTo(QDateTime::currentDateTime()) > 3600 && QFile::remove(fi.absoluteFilePath()))
            ++removed;
    }
    return removed;
}

bool removeSnapshot(const QString& zipPath, const QString& snapshotsDir)
{
    NameParts name;
    const QFileInfo info(zipPath);
    if (!parseName(info.fileName(), &name) || !samePath(info.absolutePath(), snapshotsDir) || info.isSymLink())
        return false;
    const bool removed = QFile::remove(info.absoluteFilePath());
    QFile::remove(sidecarPath(info.absoluteFilePath()));
    return removed;
}

// ---------------------------------------------------------------- rollback

namespace {

struct Moved {
    QString from;  // where it was in the instance
    QString to;    // where it is in the staging folder
};

/// Puts moved folders back. Returns the folders that could not go back (they are in the staging folder).
QStringList moveBack(const QList<Moved>& moved, const Hooks& hooks)
{
    QStringList stuck;
    for (int i = int(moved.size()) - 1; i >= 0; --i) {
        if (!hooks.rename(moved.at(i).to, moved.at(i).from))
            stuck.append(QDir::toNativeSeparators(moved.at(i).to));
    }
    return stuck;
}

/// True when something (also a broken link) is at `path`.
bool pathTaken(const QString& path)
{
    const QFileInfo fi(path);
    return fi.exists() || fi.isSymLink();
}

/// Links (symbolic, junction, shortcut) below `root`, as paths relative to it. Links are never followed and never entered; `skip(rel)` true
/// leaves a path (and everything below it) out. The zip engine skips links, so a snapshot does not hold them.
QStringList findLinks(const QString& root, const Hooks& hooks, const std::function<bool(const QString& rel)>& skip)
{
    QStringList out;
    QStringList pending{ QString() };
    while (!pending.isEmpty()) {
        const QString relDir = pending.takeLast();
        const QString dir = relDir.isEmpty() ? root : root + QLatin1Char('/') + relDir;
        const QFileInfoList entries = QDir(dir).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::NoSort);
        for (const QFileInfo& fi : entries) {
            const QString rel = joinRel(relDir, fi.fileName());
            if (skip && skip(rel))
                continue;
            if (hooks.isLink(fi.absoluteFilePath()))
                out.append(rel);
            else if (fi.isDir())
                pending.append(rel);
        }
    }
    out.sort();
    return out;
}

bool isPlayFolderName(const QString& name)
{
    static const QStringList names = { QStringLiteral("saves"), QStringLiteral("screenshots"), QStringLiteral("logs"),
                                       QStringLiteral("crash-reports"), QStringLiteral(".cache") };
    return std::any_of(names.begin(), names.end(), [&](const QString& n) { return equalName(name, n); });
}

/// True when something of the player's may be in a staging folder: a folder named like play data or a link, anywhere below it.
bool playerDataRemains(const QString& staging, const Hooks& hooks)
{
    QStringList pending{ staging };
    while (!pending.isEmpty()) {
        const QString dir = pending.takeLast();
        for (const QFileInfo& fi : QDir(dir).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::NoSort)) {
            if (hooks.isLink(fi.absoluteFilePath()) || (fi.isDir() && isPlayFolderName(fi.fileName())))
                return true;
            if (fi.isDir())
                pending.append(fi.absoluteFilePath());
        }
    }
    return false;
}

bool readFileBytes(const QString& path, QByteArray* out, bool* exists)
{
    QFile file(path);
    *exists = file.exists();
    if (!*exists)
        return true;
    if (!file.open(QIODevice::ReadOnly))
        return false;
    *out = file.readAll();
    return true;
}

}  // namespace

RollbackResult rollback(const QString& instanceRootIn, const QString& zipPath, const QString& snapshotsDir, const QString& expectedPackId,
                        const Hooks& hooksIn)
{
    RollbackResult result;
    const Hooks hooks = resolve(hooksIn);
    auto fail = [&](const QString& message) {
        result.ok = false;
        result.error = message;
        return result;
    };

    const QString instanceRoot = QDir::cleanPath(QFileInfo(instanceRootIn).absoluteFilePath());
    const QFileInfo rootInfo(instanceRoot);
    const QString id = rootInfo.fileName();
    const QString parent = rootInfo.absolutePath();
    if (!rootInfo.isDir() || id.isEmpty() || id.startsWith(QLatin1String(".zero-")))
        return fail(tr("The instance folder was not found."));

    // a. the snapshot must be this instance's own, of this pack, and not used yet
    if (!samePath(QFileInfo(zipPath).absolutePath(), snapshotsDir) || !QFileInfo(zipPath).isFile())
        return fail(tr("This saved copy does not belong to this instance."));
    const Info info = readInfo(zipPath);
    if (!info.valid)
        return fail(tr("This saved copy cannot be read: %1").arg(info.error));
    if (info.rolledBack)
        return fail(tr("This update was rolled back already."));
    if (!equalName(info.meta.instanceId, id))
        return fail(tr("This saved copy does not belong to this instance."));
    if (!expectedPackId.isEmpty() && !info.meta.packId.isEmpty() && info.meta.packId != expectedPackId)
        return fail(tr("This saved copy is of another modpack."));
    const Backup::ManifestInfo manifest = Backup::readBackupInfo(zipPath);
    if (!manifest.ok)
        return fail(tr("This saved copy cannot be read: %1").arg(manifest.error));

    const QString stamp = QDateTime::fromMSecsSinceEpoch(hooks.nowMs(), QTimeZone::utc()).toString(QStringLiteral("yyyyMMddHHmmsszzz"));
    const QString staging = QDir(parent).filePath(QStringLiteral(".zero-restore-%1-%2").arg(id, stamp));
    const QString replaced = QDir(parent).filePath(QStringLiteral(".zero-replaced-%1-%2").arg(id, stamp));
    if (QFileInfo::exists(staging) || QFileInfo::exists(replaced))
        return fail(tr("A roll back is already in progress for this instance."));

    const qint64 needed = qint64(double(manifest.totalBytes) * 1.1) + kSlackBytes;
    const qint64 available = hooks.freeBytes(parent);
    if (available >= 0 && available < needed)
        return fail(tr("Not enough free disk space to roll back (needs about %1).").arg(sizeText(needed)));

    // b. extract into the sibling staging folder and verify what landed on disk (the zip is verified first)
    auto dropStaging = [&]() { QDir(staging).removeRecursively(); };  // ZERO's own extracted copy, only before anything was moved into it
    const Backup::VerifyResult extracted = Backup::extractBackup(zipPath, staging);
    if (!extracted.ok) {
        dropStaging();
        return fail(extracted.error);
    }

    // c. instance.cfg = the current file with the ManagedPack* keys of the snapshot
    const QString stagedCfg = QDir(staging).filePath(QStringLiteral("instance.cfg"));
    {
        QByteArray snapshotCfg;
        bool snapshotHas = false;
        if (!readFileBytes(stagedCfg, &snapshotCfg, &snapshotHas) || !snapshotHas) {
            dropStaging();
            return fail(tr("The saved copy has no instance settings, so nothing was changed."));
        }
        QByteArray currentCfg;
        bool currentHas = false;
        if (!readFileBytes(QDir(instanceRoot).filePath(QStringLiteral("instance.cfg")), &currentCfg, &currentHas)) {
            dropStaging();
            return fail(tr("The settings of the instance could not be read, so nothing was changed."));
        }
        if (currentHas) {
            // Latin-1 keeps every byte as it is; only ASCII keys are touched
            const QByteArray merged = mergeInstanceCfg(QString::fromLatin1(currentCfg), QString::fromLatin1(snapshotCfg)).toLatin1();
            QSaveFile out(stagedCfg);
            if (!out.open(QIODevice::WriteOnly) || out.write(merged) != merged.size() || !out.commit()) {
                dropStaging();
                return fail(tr("The settings of the instance could not be prepared, so nothing was changed."));
            }
        }
    }

    // d. the player's data moves (rename, same volume) into the staging folder
    QList<Moved> moved;
    auto abandon = [&](const QString& message) {
        const QStringList stuck = moveBack(moved, hooks);
        if (stuck.isEmpty()) {
            dropStaging();
            return fail(message + QLatin1Char(' ') + tr("Your instance was not changed."));
        }
        qWarning() << "ZERO roll back: could not move back" << stuck;
        return fail(message + QLatin1Char(' ') + tr("Your worlds and other play data are in %1").arg(QDir::toNativeSeparators(staging)));
    };
    for (const QString& rel : playDataFolders(info.meta.gameDir)) {
        const QString from = QDir(instanceRoot).filePath(rel);
        if (!QFileInfo(from).isDir())
            continue;
        const QString to = QDir(staging).filePath(rel);
        if (QFileInfo::exists(to))
            return abandon(tr("The saved copy holds a folder that must stay with the player: %1.").arg(rel));
        QDir().mkpath(QFileInfo(to).absolutePath());
        if (!hooks.rename(from, to))
            return abandon(tr("Could not move %1. Is the game or another program using it?").arg(rel));
        moved.append({ from, to });
    }

    // d2. links (a linked mods folder, a shortcut) are not in the snapshot: they move too, to the same place, when that place is free
    for (const QString& rel : findLinks(instanceRoot, hooks, [&](const QString& r) { return isExcluded(r, info.meta.gameDir); })) {
        const QString from = QDir(instanceRoot).filePath(rel);
        const QString to = QDir(staging).filePath(rel);
        if (pathTaken(to)) {
            qInfo() << "ZERO roll back: the link" << rel << "is not carried over, the saved copy has its own" << rel;
            continue;
        }
        QDir().mkpath(QFileInfo(to).absolutePath());
        if (!hooks.rename(from, to))
            return abandon(tr("Could not move the link %1. Is the game or another program using it?").arg(rel));
        moved.append({ from, to });
    }

    // e. the swap: two renames, the old instance stays on disk until the new one is in place
    if (!hooks.rename(instanceRoot, replaced))
        return abandon(tr("The instance could not be replaced. Is it open in another program?"));
    if (!hooks.rename(staging, instanceRoot)) {
        if (!hooks.rename(replaced, instanceRoot)) {
            qWarning() << "ZERO roll back: could not put the instance back, it is in" << replaced;
            return fail(tr("The saved copy could not be moved into place. Your instance is in %1 and your play data in %2")
                            .arg(QDir::toNativeSeparators(replaced), QDir::toNativeSeparators(staging)));
        }
        return abandon(tr("The saved copy could not be moved into place."));
    }

    // f. the instance is the old one now: record it, then the replaced folder goes to the Recycle Bin
    if (!markRolledBack(zipPath))
        qWarning() << "ZERO roll back: could not record the roll back of" << zipPath;
    if (!hooks.trashPath(replaced)) {
        // A leftover is renamed so that the startup recovery cannot take it for a cut-off roll back (it would bring a deleted instance back).
        const QString leftover = QDir(parent).filePath(QStringLiteral(".zero-leftover-%1-%2").arg(id, stamp));
        result.leftoverPath = !QFileInfo::exists(leftover) && hooks.rename(replaced, leftover) ? leftover : replaced;
        qWarning() << "ZERO roll back: could not move the replaced folder to the Recycle Bin, left in" << result.leftoverPath;
    }
    result.ok = true;
    return result;
}

// ---------------------------------------------------------------- startup recovery

namespace {

/// Moves everything of the player's that sits in `staging` (play-data folders, links) back into `instance`, where that place is free, and
/// removes the staging folder (ZERO's own extracted copy) only when nothing of the player remains in it. True = the staging folder is gone.
bool recoverStaging(const QString& staging, const QString& instance, const Hooks& hooks, RecoverResult* out)
{
    auto note = [&](const QString& m) {
        out->actions.append(m);
        qInfo().noquote() << "ZERO recovery:" << m;
    };
    auto problem = [&](const QString& m) {
        out->problems.append(m);
        qWarning().noquote() << "ZERO recovery:" << m;
    };

    QStringList items;
    for (const QString& gameDir : { QStringLiteral(".minecraft"), QStringLiteral("minecraft"), QString() }) {
        for (const QString& rel : playDataFolders(gameDir)) {
            if (pathTaken(QDir(staging).filePath(rel)) && !items.contains(rel))
                items.append(rel);
        }
    }
    const QStringList links = findLinks(staging, hooks, [](const QString& rel) { return isPlayFolderName(rel.section(QLatin1Char('/'), -1)); });
    for (const QString& rel : links) {
        if (!items.contains(rel))
            items.append(rel);
    }

    for (const QString& rel : std::as_const(items)) {
        const QString from = QDir(staging).filePath(rel);
        const QString to = QDir(instance).filePath(rel);
        if (pathTaken(to)) {
            problem(QStringLiteral("%1 is in %2 and in the instance, left in %2").arg(rel, QDir::toNativeSeparators(staging)));
            continue;
        }
        QDir().mkpath(QFileInfo(to).absolutePath());
        if (hooks.rename(from, to))
            note(QStringLiteral("moved %1 back into %2").arg(rel, QDir::toNativeSeparators(instance)));
        else
            problem(QStringLiteral("could not move %1 back into %2, left in %3").arg(rel, QDir::toNativeSeparators(instance), QDir::toNativeSeparators(staging)));
    }

    if (playerDataRemains(staging, hooks)) {
        problem(QStringLiteral("%1 is kept: something of the player's is still in it").arg(QDir::toNativeSeparators(staging)));
        return false;
    }
    if (!QDir(staging).removeRecursively()) {  // only the extracted copy is left in it
        problem(QStringLiteral("could not remove the extracted copy %1").arg(QDir::toNativeSeparators(staging)));
        return false;
    }
    note(QStringLiteral("removed the extracted copy %1").arg(QDir::toNativeSeparators(staging)));
    return true;
}

}  // namespace

RecoverResult recoverInterrupted(const QString& instancesDir, const Hooks& hooksIn)
{
    RecoverResult out;
    const Hooks hooks = resolve(hooksIn);
    auto note = [&](const QString& m) {
        out.actions.append(m);
        qInfo().noquote() << "ZERO recovery:" << m;
    };
    auto problem = [&](const QString& m) {
        out.problems.append(m);
        qWarning().noquote() << "ZERO recovery:" << m;
    };

    struct Group {
        QStringList restore, replaced, leftover;  // folder names, newest first
    };
    QMap<QString, Group> groups;
    static const QRegularExpression re(QStringLiteral("^[.]zero-(restore|replaced|leftover)-(.+)-([0-9]{17})$"));
    const QFileInfoList dirs = QDir(instancesDir).entryInfoList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
    for (const QFileInfo& fi : dirs) {
        const QRegularExpressionMatch m = re.match(fi.fileName());
        if (!m.hasMatch() || fi.isSymLink())
            continue;
        Group& g = groups[m.captured(2)];
        const QString kind = m.captured(1);
        (kind == QLatin1String("restore") ? g.restore : kind == QLatin1String("replaced") ? g.replaced : g.leftover).append(fi.fileName());
    }

    for (auto it = groups.begin(); it != groups.end(); ++it) {
        const QString id = it.key();
        Group& g = it.value();
        const QString instance = QDir(instancesDir).filePath(id);
        bool exists = QFileInfo(instance).isDir();
        bool steppedBack = false;

        // the instance was stepped aside and the roll back never finished: it comes back first
        if (!exists && !g.replaced.isEmpty()) {
            const QString from = QDir(instancesDir).filePath(g.replaced.first());
            if (!hooks.rename(from, instance)) {
                problem(QStringLiteral("could not rename %1 back to %2, the instance is left in it").arg(QDir::toNativeSeparators(from), id));
                continue;
            }
            note(QStringLiteral("renamed %1 back to %2").arg(g.replaced.first(), id));
            g.replaced.removeFirst();
            exists = true;
            steppedBack = true;
        }
        if (!exists) {
            if (!g.restore.isEmpty())
                problem(QStringLiteral("%1 holds play data of %2 but %2 is gone and nothing to restore it from; left in place").arg(g.restore.first(), id));
            continue;
        }

        bool stagingLeft = false;
        for (const QString& name : std::as_const(g.restore)) {
            if (!recoverStaging(QDir(instancesDir).filePath(name), instance, hooks, &out))
                stagingLeft = true;
        }

        if (!g.replaced.isEmpty()) {
            if (steppedBack || stagingLeft) {
                problem(QStringLiteral("%1 is kept (a roll back of %2 was recovered or is not settled)").arg(g.replaced.join(QStringLiteral(", ")), id));
            } else {
                for (const QString& name : std::as_const(g.replaced)) {
                    const QString path = QDir(instancesDir).filePath(name);
                    if (hooks.trashPath(path))
                        note(QStringLiteral("moved %1 to the Recycle Bin").arg(name));
                    else
                        problem(QStringLiteral("could not move %1 to the Recycle Bin, left in place").arg(QDir::toNativeSeparators(path)));
                }
            }
        }
        for (const QString& name : std::as_const(g.leftover)) {
            const QString path = QDir(instancesDir).filePath(name);
            if (hooks.trashPath(path))
                note(QStringLiteral("moved %1 to the Recycle Bin").arg(name));
            else
                problem(QStringLiteral("could not move %1 to the Recycle Bin, left in place").arg(QDir::toNativeSeparators(path)));
        }
    }
    return out;
}

}  // namespace Pack
}  // namespace Backup
}  // namespace Zero
