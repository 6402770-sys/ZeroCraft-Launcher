// SPDX-License-Identifier: GPL-3.0-only
#include "ModSnapshot.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStorageInfo>
#include <QTimeZone>
#include <algorithm>

#include "FileSystem.h"
#include "zero/backup/WorldBackup.h"

namespace Zero {
namespace Mods {
namespace Snapshot {

namespace {

constexpr qint64 kSlackBytes = 5LL * 1024 * 1024;  // free space beyond the copies themselves
constexpr qsizetype kChunk = 1024 * 1024;
constexpr char kIndexDir[] = ".index";
constexpr char kFilesDir[] = "files";
constexpr char kStagingPrefix[] = ".zero-rollback-";

QString tr(const char* text)
{
    return QCoreApplication::translate("Zero::Mods::Snapshot", text);
}

QString native(const QString& path)
{
    return QDir::toNativeSeparators(path);
}

// ---------------------------------------------------------------- hooks

bool defaultRename(const QString& from, const QString& to)
{
    return QFile::rename(from, to);  // fails when `to` exists
}

qint64 defaultFreeBytes(const QString& path)
{
    const QStorageInfo info(path);
    return info.isValid() && info.isReady() ? info.bytesAvailable() : -1;
}

bool defaultTrash(const QString& path)
{
    return FS::trash(path);  // deliberately no fallback: a file that cannot go to the Recycle Bin stays where it is
}

qint64 defaultNow()
{
    return QDateTime::currentMSecsSinceEpoch();
}

Hooks resolve(const Hooks& in)
{
    Hooks h = in;
    if (!h.trashPath)
        h.trashPath = defaultTrash;
    if (!h.rename)
        h.rename = defaultRename;
    if (!h.freeBytes)
        h.freeBytes = defaultFreeBytes;
    if (!h.nowMs)
        h.nowMs = defaultNow;
    return h;
}

// ---------------------------------------------------------------- paths

/// A path the snapshot may name: a top-level file of the mods folder or a file of `.index/`. Nothing deeper, nothing unsafe.
bool isAllowedPath(const QString& path)
{
    if (!Backup::isSafeRelativePath(path))
        return false;
    const int slashes = path.count(QLatin1Char('/'));
    if (slashes == 0)
        return true;
    return slashes == 1 && path.startsWith(QLatin1String(kIndexDir) + QLatin1Char('/'));
}

/// `absolute` relative to `modsDir`, or "" when it is not inside it.
QString relativeTo(const QString& modsDir, const QString& absolute)
{
    const QString root = QDir::cleanPath(QDir(modsDir).absolutePath());
    const QString path = QDir::cleanPath(QFileInfo(absolute).absoluteFilePath());
    if (path.size() <= root.size() + 1 || !path.startsWith(root + QLatin1Char('/'), Qt::CaseInsensitive))
        return {};
    return path.mid(root.size() + 1);
}

bool makePath(const QString& path)
{
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (QDir().mkpath(path))
            return true;
    }
    return QFileInfo(path).isDir();
}

bool isIndexPath(const QString& path)
{
    return path.startsWith(QLatin1String(kIndexDir) + QLatin1Char('/'));
}

/// Jars and other top-level files before the packwiz entries.
bool lessForOrder(const QString& a, const QString& b)
{
    const bool ai = isIndexPath(a);
    const bool bi = isIndexPath(b);
    if (ai != bi)
        return !ai;
    return a.compare(b, Qt::CaseInsensitive) < 0;
}

// ---------------------------------------------------------------- hashing and copying

bool hashFile(const QString& path, QString* sha, qint64* size, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = tr("%1 could not be read (%2).").arg(native(path), file.errorString());
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray buffer(kChunk, Qt::Uninitialized);
    qint64 total = 0;
    for (;;) {
        const qint64 n = file.read(buffer.data(), buffer.size());
        if (n < 0) {
            *error = tr("%1 could not be read (%2).").arg(native(path), file.errorString());
            return false;
        }
        if (n == 0)
            break;
        hash.addData(QByteArrayView(buffer.constData(), n));
        total += n;
    }
    *sha = QString::fromLatin1(hash.result().toHex());
    *size = total;
    return true;
}

/// Copies `src` to `dst` through `<dst>.part`, verifies the copy against what was read and renames it. `dst` must not exist.
bool copyVerified(const QString& src, const QString& dst, QString* sha, qint64* size, QString* error)
{
    if (QFileInfo::exists(dst) || QFileInfo::exists(dst + QStringLiteral(".part"))) {
        *error = tr("%1 already exists.").arg(native(dst));
        return false;
    }
    QFile in(src);
    if (!in.open(QIODevice::ReadOnly)) {
        *error = tr("%1 could not be read (%2).").arg(native(src), in.errorString());
        return false;
    }
    const QString part = dst + QStringLiteral(".part");
    QFile out(part);
    if (!out.open(QIODevice::WriteOnly)) {
        *error = tr("%1 could not be written (%2).").arg(native(part), out.errorString());
        return false;
    }
    auto fail = [&](const QString& message) {
        out.close();
        QFile::remove(part);  // our own half-written copy
        *error = message;
        return false;
    };
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray buffer(kChunk, Qt::Uninitialized);
    qint64 total = 0;
    for (;;) {
        const qint64 n = in.read(buffer.data(), buffer.size());
        if (n < 0)
            return fail(tr("%1 could not be read (%2).").arg(native(src), in.errorString()));
        if (n == 0)
            break;
        if (out.write(buffer.constData(), n) != n)
            return fail(tr("%1 could not be written (%2).").arg(native(part), out.errorString()));
        hash.addData(QByteArrayView(buffer.constData(), n));
        total += n;
    }
    if (!out.flush())
        return fail(tr("%1 could not be written (%2).").arg(native(part), out.errorString()));
    out.close();
    const QString readSha = QString::fromLatin1(hash.result().toHex());
    QString writtenSha;
    qint64 writtenSize = 0;
    QString hashError;
    if (!hashFile(part, &writtenSha, &writtenSize, &hashError)) {
        QFile::remove(part);
        *error = hashError;
        return false;
    }
    if (writtenSha != readSha || writtenSize != total) {
        QFile::remove(part);
        *error = tr("The copy of %1 did not match the original.").arg(native(src));
        return false;
    }
    if (!QFile::rename(part, dst)) {
        QFile::remove(part);
        *error = tr("%1 could not be put in place.").arg(native(dst));
        return false;
    }
    *sha = readSha;
    *size = total;
    return true;
}

// ---------------------------------------------------------------- snapshot.json

QString isoOf(qint64 ms)
{
    return QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC).toString(Qt::ISODateWithMs);
}

QByteArray toJson(const Info& info)
{
    QJsonObject root;
    root[QStringLiteral("format")] = kFormat;
    root[QStringLiteral("instanceId")] = info.instanceId;
    root[QStringLiteral("created")] = isoOf(info.createdMs);
    root[QStringLiteral("kind")] = info.kind;
    root[QStringLiteral("outcome")] = info.outcome;
    root[QStringLiteral("rolledBack")] = info.rolledBack;
    root[QStringLiteral("dismissed")] = info.dismissed;
    QJsonArray files;
    for (const FileRef& f : info.files) {
        QJsonObject o;
        o[QStringLiteral("path")] = f.path;
        o[QStringLiteral("size")] = f.size;
        o[QStringLiteral("sha256")] = f.sha256;
        files.append(o);
    }
    root[QStringLiteral("files")] = files;
    QJsonArray mods;
    for (const ModEntry& m : info.mods) {
        QJsonObject o;
        o[QStringLiteral("name")] = m.name;
        o[QStringLiteral("old")] = m.oldVersion;
        o[QStringLiteral("new")] = m.newVersion;
        o[QStringLiteral("files")] = QJsonArray::fromStringList(m.files);
        o[QStringLiteral("changed")] = m.changed;
        mods.append(o);
    }
    root[QStringLiteral("mods")] = mods;
    root[QStringLiteral("added")] = QJsonArray::fromStringList(info.added);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool writeInfo(const Info& info, QString* error)
{
    QSaveFile file(QDir(info.dir).filePath(QString::fromLatin1(kFileName)));
    if (!file.open(QIODevice::WriteOnly)) {
        *error = tr("%1 could not be written (%2).").arg(native(file.fileName()), file.errorString());
        return false;
    }
    file.write(toJson(info));
    if (!file.commit()) {
        *error = tr("%1 could not be written (%2).").arg(native(file.fileName()), file.errorString());
        return false;
    }
    return true;
}

bool isHex64(const QString& s)
{
    if (s.size() != 64)
        return false;
    return std::all_of(s.begin(), s.end(), [](QChar c) { return c.isDigit() || (c >= QLatin1Char('a') && c <= QLatin1Char('f')); });
}

QStringList stringList(const QJsonValue& value)
{
    QStringList out;
    for (const QJsonValue& v : value.toArray())
        out.append(v.toString());
    return out;
}

// ---------------------------------------------------------------- names

const QRegularExpression& nameRe()
{
    static const QRegularExpression re(QStringLiteral("^(\\d{8}-\\d{6})(?:-(\\d+))?$"));
    return re;
}

/// `<snapshotsDir>/<yyyyMMdd-HHmmss>[-n]` that exists neither as a folder nor as a `.part` folder.
QString uniqueDir(const QString& snapshotsDir, qint64 nowMs)
{
    const QString stamp = QDateTime::fromMSecsSinceEpoch(nowMs).toString(QStringLiteral("yyyyMMdd-HHmmss"));
    for (int n = 1;; ++n) {
        const QString suffix = n == 1 ? QString() : QStringLiteral("-%1").arg(n);
        const QString path = QDir(snapshotsDir).filePath(stamp + suffix);
        if (!QFileInfo::exists(path) && !QFileInfo::exists(path + QStringLiteral(".part")))
            return path;
    }
}

/// The time a folder name says (local time); 0 when it is not a snapshot name. `n` = the -n suffix (1 without).
qint64 stampOfName(const QString& name, int* n)
{
    const QRegularExpressionMatch m = nameRe().match(name);
    if (!m.hasMatch())
        return 0;
    if (n)
        *n = m.captured(2).isEmpty() ? 1 : m.captured(2).toInt();
    return QDateTime::fromString(m.captured(1), QStringLiteral("yyyyMMdd-HHmmss")).toMSecsSinceEpoch();
}

}  // namespace

// ---------------------------------------------------------------- Info

int Info::changedCount() const
{
    return int(std::count_if(mods.begin(), mods.end(), [](const ModEntry& m) { return m.changed; }));
}

QStringList Info::changedNames() const
{
    QStringList names;
    for (const ModEntry& m : mods) {
        if (m.changed)
            names.append(m.name);
    }
    return names;
}

QString snapshotsDirFor(const QString& dataRoot, const QString& instanceId)
{
    return QDir(dataRoot).filePath(QStringLiteral("zero/snapshots/") + instanceId);
}

QStringList listFolder(const QString& modsDir)
{
    QStringList out;
    const QDir dir(modsDir);
    for (const QString& name : dir.entryList(QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
        out.append(name);
    const QDir index(dir.filePath(QString::fromLatin1(kIndexDir)));
    for (const QString& name : index.entryList(QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
        out.append(QString::fromLatin1(kIndexDir) + QLatin1Char('/') + name);
    out.sort(Qt::CaseInsensitive);
    return out;
}

// ---------------------------------------------------------------- reading

Info readSnapshot(const QString& snapshotDir)
{
    Info info;
    info.dir = QDir::cleanPath(QDir(snapshotDir).absolutePath());
    info.id = QFileInfo(info.dir).fileName();
    QFile file(QDir(info.dir).filePath(QString::fromLatin1(kFileName)));
    if (!file.open(QIODevice::ReadOnly)) {
        info.error = tr("snapshot.json could not be read.");
        return info;
    }
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        info.error = tr("snapshot.json is not valid.");
        return info;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toInt() != kFormat) {
        info.error = tr("snapshot.json has an unknown format.");
        return info;
    }
    info.instanceId = root.value(QStringLiteral("instanceId")).toString();
    const QDateTime created = QDateTime::fromString(root.value(QStringLiteral("created")).toString(), Qt::ISODateWithMs);
    if (!created.isValid()) {
        info.error = tr("snapshot.json has no valid time.");
        return info;
    }
    info.createdMs = created.toMSecsSinceEpoch();
    info.kind = root.value(QStringLiteral("kind")).toString();
    info.outcome = root.value(QStringLiteral("outcome")).toString();
    info.rolledBack = root.value(QStringLiteral("rolledBack")).toBool();
    info.dismissed = root.value(QStringLiteral("dismissed")).toBool();
    QSet<QString> seen;
    for (const QJsonValue& v : root.value(QStringLiteral("files")).toArray()) {
        const QJsonObject o = v.toObject();
        FileRef f;
        f.path = o.value(QStringLiteral("path")).toString();
        f.size = o.value(QStringLiteral("size")).toVariant().toLongLong();
        f.sha256 = o.value(QStringLiteral("sha256")).toString();
        if (!isAllowedPath(f.path) || !isHex64(f.sha256) || f.size < 0 || seen.contains(f.path.toLower())) {
            info.error = tr("snapshot.json lists a file that is not valid.");
            return info;
        }
        seen.insert(f.path.toLower());
        info.files.append(f);
    }
    for (const QJsonValue& v : root.value(QStringLiteral("mods")).toArray()) {
        const QJsonObject o = v.toObject();
        ModEntry m;
        m.name = o.value(QStringLiteral("name")).toString();
        m.oldVersion = o.value(QStringLiteral("old")).toString();
        m.newVersion = o.value(QStringLiteral("new")).toString();
        m.files = stringList(o.value(QStringLiteral("files")));
        m.changed = o.value(QStringLiteral("changed")).toBool();
        info.mods.append(m);
    }
    info.added = stringList(root.value(QStringLiteral("added")));
    for (const QString& path : std::as_const(info.added)) {
        if (!isAllowedPath(path)) {
            info.error = tr("snapshot.json lists a file that is not valid.");
            return info;
        }
    }
    info.valid = true;
    return info;
}

QList<Info> listSnapshots(const QString& snapshotsDir)
{
    struct Item {
        Info info;
        qint64 sortMs = 0;
        int n = 1;
    };
    QList<Item> items;
    const QDir dir(snapshotsDir);
    for (const QString& name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        Item item;
        const qint64 named = stampOfName(name, &item.n);
        if (named == 0)
            continue;  // not ours (a `.part` folder, anything else)
        item.info = readSnapshot(dir.filePath(name));
        item.sortMs = item.info.valid ? item.info.createdMs : named;
        items.append(item);
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.sortMs != b.sortMs)
            return a.sortMs > b.sortMs;
        return a.n > b.n;
    });
    QList<Info> out;
    for (const Item& item : std::as_const(items))
        out.append(item.info);
    return out;
}

Info newestActive(const QString& snapshotsDir)
{
    const QList<Info> all = listSnapshots(snapshotsDir);
    if (all.isEmpty())
        return {};
    const Info& newest = all.first();
    if (!newest.valid || newest.outcome.isEmpty() || newest.rolledBack || newest.dismissed)
        return {};
    return newest;
}

bool markDismissed(const QString& snapshotDir, QString* error)
{
    Info info = readSnapshot(snapshotDir);
    QString ignored;
    if (!error)
        error = &ignored;
    if (!info.valid) {
        *error = info.error;
        return false;
    }
    info.dismissed = true;
    return writeInfo(info, error);
}

int applyRetention(const QString& snapshotsDir, int keep)
{
    int removed = 0;
    const QDir dir(snapshotsDir);
    const QList<Info> all = listSnapshots(snapshotsDir);
    for (qsizetype i = keep; i < all.size(); ++i) {
        // listSnapshots only returns folders whose name is a snapshot name: nothing else can be removed here.
        if (QDir(all.at(i).dir).removeRecursively())
            ++removed;
    }
    // A `.part` folder older than an hour is what a crashed creation left behind.
    for (const QString& name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!name.endsWith(QLatin1String(".part")) || stampOfName(name.chopped(5), nullptr) == 0)
            continue;
        if (QFileInfo(dir.filePath(name)).lastModified().secsTo(QDateTime::currentDateTime()) > 3600 && QDir(dir.filePath(name)).removeRecursively())
            ++removed;
    }
    return removed;
}

// ---------------------------------------------------------------- create

CreateResult createSnapshot(const QString& snapshotsDir, const QString& instanceId, const QString& modsDir, const QList<ModFiles>& mods,
                            const Hooks& hooksIn)
{
    const Hooks hooks = resolve(hooksIn);
    CreateResult result;
    auto fail = [&result](const QString& message) {
        result.ok = false;
        result.error = message;
        return result;
    };
    if (mods.isEmpty())
        return fail(tr("There is nothing to save."));

    // Which files: only inside the mods folder, only the shapes a snapshot may hold, every mod has at least its jar.
    QList<FileRef> files;
    QList<ModEntry> entries;
    QSet<QString> seen;
    qint64 total = 0;
    for (const ModFiles& mod : mods) {
        ModEntry entry;
        entry.name = mod.name;
        entry.oldVersion = mod.oldVersion;
        entry.newVersion = mod.newVersion;
        for (const QString& absolute : mod.files) {
            const QString rel = relativeTo(modsDir, absolute);
            if (rel.isEmpty() || !isAllowedPath(rel))
                return fail(tr("%1 is not a file of the mods folder.").arg(native(absolute)));
            const QFileInfo fi(absolute);
            if (!fi.exists())
                continue;  // a packwiz entry that is not there
            if (!fi.isFile())
                return fail(tr("%1 is not a file.").arg(native(absolute)));
            if (seen.contains(rel.toLower())) {
                entry.files.append(rel);
                continue;
            }
            seen.insert(rel.toLower());
            FileRef ref;
            ref.path = rel;
            ref.size = fi.size();
            total += ref.size;
            files.append(ref);
            entry.files.append(rel);
        }
        if (entry.files.isEmpty())
            return fail(tr("The file of %1 was not found.").arg(mod.name));
        entries.append(entry);
    }

    if (!makePath(snapshotsDir))
        return fail(tr("The snapshots folder could not be created."));
    const qint64 free = hooks.freeBytes(snapshotsDir);
    if (free >= 0 && free < total + kSlackBytes)
        return fail(tr("There is not enough free space on the disk."));

    result.before = listFolder(modsDir);
    const qint64 now = hooks.nowMs();
    const QString finalDir = uniqueDir(snapshotsDir, now);
    const QString partDir = finalDir + QStringLiteral(".part");
    auto cleanup = [&partDir]() { QDir(partDir).removeRecursively(); };  // our own half-built folder

    for (FileRef& ref : files) {
        const QString dst = QDir(partDir).filePath(QString::fromLatin1(kFilesDir) + QLatin1Char('/') + ref.path);
        if (!makePath(QFileInfo(dst).absolutePath())) {
            cleanup();
            return fail(tr("The snapshots folder could not be created."));
        }
        QString error;
        qint64 size = 0;
        if (!copyVerified(QDir(modsDir).filePath(ref.path), dst, &ref.sha256, &size, &error)) {
            cleanup();
            return fail(error);
        }
        ref.size = size;
    }

    Info info;
    info.dir = partDir;
    info.instanceId = instanceId;
    info.createdMs = now;
    info.kind = QString::fromLatin1(kKindModsUpdate);
    info.files = files;
    info.mods = entries;
    QString error;
    if (!writeInfo(info, &error)) {
        cleanup();
        return fail(error);
    }
    if (!readSnapshot(partDir).valid) {
        cleanup();
        return fail(tr("The snapshot could not be verified."));
    }
    if (!QDir().rename(partDir, finalDir)) {
        cleanup();
        return fail(tr("The snapshot could not be put in place."));
    }
    result.ok = true;
    result.dir = finalDir;
    applyRetention(snapshotsDir);
    return result;
}

// ---------------------------------------------------------------- finalize

FinalizeResult finalizeSnapshot(const QString& snapshotDir, const QString& modsDir, const QStringList& before, bool success)
{
    FinalizeResult result;
    Info info = readSnapshot(snapshotDir);
    if (!info.valid) {
        result.error = info.error;
        return result;
    }
    const QSet<QString> was(before.begin(), before.end());
    info.added.clear();
    for (const QString& path : listFolder(modsDir)) {
        if (!was.contains(path))
            info.added.append(path);
    }

    QSet<QString> changedPaths;
    for (const FileRef& f : std::as_const(info.files)) {
        const QFileInfo now(QDir(modsDir).filePath(f.path));
        bool changed = !now.exists() || now.size() != f.size;
        if (!changed) {
            QString sha, error;
            qint64 size = 0;
            changed = !hashFile(now.absoluteFilePath(), &sha, &size, &error) || sha != f.sha256;
        }
        if (changed)
            changedPaths.insert(f.path);
    }
    for (ModEntry& mod : info.mods) {
        mod.changed = std::any_of(mod.files.begin(), mod.files.end(), [&changedPaths](const QString& p) { return changedPaths.contains(p); });
    }

    if (info.added.isEmpty() && changedPaths.isEmpty()) {
        // The update changed nothing: the snapshot protects nothing.
        result.pruned = QDir(info.dir).removeRecursively();
        result.ok = result.pruned;
        if (!result.ok)
            result.error = tr("The snapshot folder could not be removed.");
        return result;
    }
    info.outcome = QString::fromLatin1(success ? kOutcomeOk : kOutcomeFailed);
    result.changedMods = info.changedCount();
    result.changedNames = info.changedNames();
    result.ok = writeInfo(info, &result.error);
    return result;
}

// ---------------------------------------------------------------- roll back

RollbackResult rollback(const Info& info, const QString& modsDir, const Hooks& hooksIn)
{
    const Hooks hooks = resolve(hooksIn);
    RollbackResult result;
    auto fail = [&result](const QString& message) {
        result.ok = false;
        result.error = message;
        return result;
    };
    if (!info.valid)
        return fail(tr("This snapshot cannot be read, so nothing was rolled back."));
    if (info.rolledBack)
        return fail(tr("This snapshot was rolled back already."));
    if (!QFileInfo(modsDir).isDir())
        return fail(tr("The mods folder was not found."));

    // 1. Every saved copy must still be what it was. Nothing is touched before this passes.
    for (const FileRef& f : info.files) {
        if (!isAllowedPath(f.path))
            return fail(tr("This snapshot lists a file that is not valid, so nothing was rolled back."));
        QString sha, error;
        qint64 size = 0;
        const QString src = QDir(info.dir).filePath(QString::fromLatin1(kFilesDir) + QLatin1Char('/') + f.path);
        if (!QFileInfo(src).isFile() || !hashFile(src, &sha, &size, &error) || sha != f.sha256 || size != f.size)
            return fail(tr("The saved copy of %1 is damaged, so nothing was rolled back.").arg(f.path));
    }
    for (const QString& path : info.added) {
        if (!isAllowedPath(path))
            return fail(tr("This snapshot lists a file that is not valid, so nothing was rolled back."));
    }

    // 2. The plan: what to restore, what has to go to the Recycle Bin first.
    QStringList restore;
    QStringList trash;
    QSet<QString> restorePaths;
    for (const FileRef& f : info.files) {
        const QFileInfo target(QDir(modsDir).filePath(f.path));
        if (target.isDir() && !target.isSymLink())
            return fail(tr("%1 is a folder now, so nothing was rolled back.").arg(f.path));
        if (target.exists() || target.isSymLink()) {
            QString sha, error;
            qint64 size = 0;
            if (!target.isSymLink() && target.size() == f.size && hashFile(target.absoluteFilePath(), &sha, &size, &error) && sha == f.sha256)
                continue;  // already the saved version
            trash.append(f.path);  // never overwritten in place
        }
        restore.append(f.path);
        restorePaths.insert(f.path.toLower());
    }
    for (const QString& path : info.added) {
        if (restorePaths.contains(path.toLower()) || trash.contains(path, Qt::CaseInsensitive))
            continue;
        const QFileInfo fi(QDir(modsDir).filePath(path));
        if ((fi.exists() && fi.isFile()) || fi.isSymLink())
            trash.append(path);  // an added file the user already removed is skipped silently
    }
    std::sort(restore.begin(), restore.end(), lessForOrder);
    std::sort(trash.begin(), trash.end(), lessForOrder);

    qint64 totalRestore = 0;
    for (const FileRef& f : info.files) {
        if (restore.contains(f.path))
            totalRestore += f.size;
    }

    // 3. Stage the restored copies next to their final place and verify them. Nothing of the mods folder changes yet.
    QString staging;
    if (!restore.isEmpty()) {
        const qint64 free = hooks.freeBytes(modsDir);
        if (free >= 0 && free < totalRestore + kSlackBytes)
            return fail(tr("There is not enough free space on the disk, so nothing was rolled back."));
        for (int n = 1;; ++n) {
            staging = QDir(modsDir).filePath(QString::fromLatin1(kStagingPrefix) + QString::number(n));
            if (!QFileInfo::exists(staging))
                break;
        }
        for (const QString& path : restore) {
            const QString src = QDir(info.dir).filePath(QString::fromLatin1(kFilesDir) + QLatin1Char('/') + path);
            const QString dst = QDir(staging).filePath(path);
            QString sha, error;
            qint64 size = 0;
            const FileRef* ref = nullptr;
            for (const FileRef& f : info.files) {
                if (f.path == path)
                    ref = &f;
            }
            if (!makePath(QFileInfo(dst).absolutePath()) || !copyVerified(src, dst, &sha, &size, &error) || !ref || sha != ref->sha256) {
                QDir(staging).removeRecursively();  // our own staging folder
                return fail(tr("The saved copy of %1 could not be prepared, so nothing was rolled back.").arg(path));
            }
        }
    }
    auto dropStaging = [&staging]() {
        if (!staging.isEmpty())
            QDir(staging).removeRecursively();
    };

    // 4. The Recycle Bin: the added files and whatever stands where a saved file goes back. The first failure stops everything; the
    //    snapshot is kept, so a second try finishes the job (a file that is gone is skipped, a free path is restored).
    for (const QString& path : trash) {
        const QString absolute = QDir(modsDir).filePath(path);
        if (!hooks.trashPath(absolute)) {
            dropStaging();
            result.touched = !result.trashedPaths.isEmpty();
            QString state;
            if (result.trashedPaths.isEmpty()) {
                state = tr("Nothing in the mods folder was changed.");
            } else {
                state = tr("%1 file(s) had already gone to the Recycle Bin (%2) and none was put back, so some mods may be missing for now.")
                            .arg(result.trashedPaths.size())
                            .arg(result.trashedPaths.mid(0, 3).join(QStringLiteral(", ")));
            }
            return fail(tr("%1 could not be moved to the Recycle Bin, so the roll back stopped. %2 The saved copies are still here: close whatever uses "
                           "the mods folder and click Roll back again.")
                            .arg(path, state));
        }
        ++result.trashed;
        result.trashedPaths.append(path);
        result.touched = true;
    }

    // 5. Put the staged copies in place (jars first, then the packwiz entries). Renames never overwrite.
    for (const QString& path : restore) {
        const QString staged = QDir(staging).filePath(path);
        const QString target = QDir(modsDir).filePath(path);
        if (QFileInfo::exists(target) || !hooks.rename(staged, target)) {
            dropStaging();
            return fail(tr("%1 could not be put back, so the roll back stopped. %2 file(s) went to the Recycle Bin and %3 were put back; the saved "
                           "copies are still here: close whatever uses the mods folder and click Roll back again.")
                            .arg(path)
                            .arg(result.trashed)
                            .arg(result.restored));
        }
        ++result.restored;
        result.touched = true;
    }
    dropStaging();

    // 6. Mark it. The files are back whatever happens here.
    Info marked = info;
    marked.rolledBack = true;
    QString error;
    if (!writeInfo(marked, &error))
        result.warning = error;
    result.ok = true;
    return result;
}

// ---------------------------------------------------------------- texts

QString ageText(qint64 createdMs, qint64 nowMs)
{
    const qint64 seconds = std::max<qint64>(0, (nowMs - createdMs) / 1000);
    if (seconds < 60)
        return tr("just now");
    if (seconds < 3600)
        return tr("%1 min ago").arg(seconds / 60);
    if (seconds < 86400)
        return tr("%1 h ago").arg(seconds / 3600);
    if (seconds < 30LL * 86400)
        return tr("%1 d ago").arg(seconds / 86400);
    return QLocale().toString(QDateTime::fromMSecsSinceEpoch(createdMs).date(), QLocale::ShortFormat);
}

QString noticeText(const Info& info, qint64 nowMs)
{
    const int count = info.changedCount() > 0 ? info.changedCount() : int(info.mods.size());
    const QString what = count == 1 ? tr("Updated 1 mod") : tr("Updated %1 mods").arg(count);
    return what + QStringLiteral(" · ") + ageText(info.createdMs, nowMs);
}

}  // namespace Snapshot
}  // namespace Mods
}  // namespace Zero
