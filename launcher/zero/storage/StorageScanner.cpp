// SPDX-License-Identifier: GPL-3.0-only
#include "StorageScanner.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirListing>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QSet>
#include <QStorageInfo>
#include <QThreadPool>
#include <algorithm>
#include <optional>

#include "FileSystem.h"
#include "zero/worlds/WorldScanner.h"

namespace Zero {
namespace Storage {

namespace {

bool isLink(const QFileInfo& info)
{
    return info.isSymLink() || info.isJunction();
}

/// True when `child` lies strictly below `parent` (both are pathKey()s).
bool keyInside(const QString& child, const QString& parent)
{
    if (child.size() <= parent.size() || !child.startsWith(parent))
        return false;
    return parent.endsWith(QLatin1Char('/')) || child.at(parent.size()) == QLatin1Char('/');
}

/// A directory (or a single file such as `metacache`); links count 0 like folderSize does for nested ones. Missing = 0.
qint64 pathSize(const QString& path)
{
    const QFileInfo info(path);
    if (info.isDir())
        return Worlds::folderSize(path);
    if (info.isFile() && !isLink(info))
        return info.size();
    return 0;
}

/// False when a link (symlink / junction) lies on the way from `root` down to `path`: folderSize(root) did not count `path` then.
bool reachableWithoutLinks(const QString& path, const QString& root)
{
    const QString rootKey = pathKey(root);
    QString cur = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    while (keyInside(pathKey(cur), rootKey)) {
        if (isLink(QFileInfo(cur)))
            return false;
        cur = QFileInfo(cur).path();
    }
    return true;
}

/// Size of `dir` without the entries in `excluded` (pathKeys). Folders that do not contain an excluded path are measured with
/// folderSize (links skipped); folders that do are walked. This is what keeps the "everything else" buckets exact without
/// measuring the big trees twice.
qint64 sizeExcluding(const QString& dir, const QSet<QString>& excluded)
{
    qint64 total = 0;
    for (const auto& entry : QDirListing(dir, QDirListing::IteratorFlag::IncludeHidden)) {
        const QFileInfo info = entry.fileInfo();
        if (isLink(info))
            continue;
        const QString key = pathKey(entry.filePath());
        if (excluded.contains(key))
            continue;
        if (entry.isDir()) {
            bool hasExcludedInside = false;
            for (const QString& e : excluded) {
                if (keyInside(e, key)) {
                    hasExcludedInside = true;
                    break;
                }
            }
            total += hasExcludedInside ? sizeExcluding(entry.filePath(), excluded) : Worlds::folderSize(entry.filePath());
        } else if (entry.isFile()) {
            total += entry.size();
        }
    }
    return total;
}

/// A configured directory and the category its bytes go to (nullopt = only excluded from a parent, counted elsewhere).
struct Region {
    QString path;
    std::optional<Category> category;
    QString key;
    qint64 bytes = 0;  ///< exclusive size (without nested regions)
};

/// Measures every region exactly once: the same path twice counts once, a region nested in another is subtracted from it.
QList<Region> measureRegions(const QList<Region>& input)
{
    QList<Region> regions;
    QSet<QString> seen;
    for (Region r : input) {
        if (r.path.isEmpty())
            continue;
        r.key = pathKey(r.path);
        if (seen.contains(r.key))
            continue;  // duplicate path: the first one owns the bytes
        seen.insert(r.key);
        regions.append(r);
    }
    QList<qint64> raw;
    for (const Region& r : std::as_const(regions))
        raw.append(pathSize(r.path));
    for (int i = 0; i < regions.size(); ++i)
        regions[i].bytes = raw[i];
    for (int i = 0; i < regions.size(); ++i) {
        int parent = -1;  // nearest enclosing region
        for (int j = 0; j < regions.size(); ++j) {
            if (j != i && keyInside(regions[i].key, regions[j].key) && (parent < 0 || regions[j].key.size() > regions[parent].key.size()))
                parent = j;
        }
        if (parent >= 0 && reachableWithoutLinks(regions[i].path, regions[parent].path))
            regions[parent].bytes = std::max<qint64>(0, regions[parent].bytes - raw[i]);
    }
    return regions;
}

QSet<QString> keysInside(const QList<Region>& regions, const QString& root)
{
    QSet<QString> keys;
    const QString rootKey = pathKey(root);
    for (const Region& r : regions) {
        if (keyInside(r.key, rootKey) && reachableWithoutLinks(r.path, root))
            keys.insert(r.key);
    }
    return keys;
}

qint64 sizeBelowRoot(const QString& root, const QList<Region>& regions)
{
    const QString rootKey = pathKey(root);
    for (const Region& r : regions) {
        if (r.key == rootKey)
            return 0;  // the root itself is one of the configured dirs: its bytes belong to that category
    }
    return sizeExcluding(root, keysInside(regions, root));
}

bool isRestoreLeftoverName(const QString& name)
{
    return name.startsWith(QLatin1String(".zero-restore-")) || name.startsWith(QLatin1String(".zero-replaced-"));
}

bool isInstanceDirPlain(const QString& dir)
{
    const QFileInfo info(dir);
    return info.isDir() && !isLink(info);
}

}  // namespace

QString categoryId(Category category)
{
    switch (category) {
        case Category::Worlds:
            return QStringLiteral("worlds");
        case Category::Mods:
            return QStringLiteral("mods");
        case Category::Packs:
            return QStringLiteral("packs");
        case Category::Screenshots:
            return QStringLiteral("screenshots");
        case Category::Logs:
            return QStringLiteral("logs");
        case Category::OtherInstance:
            return QStringLiteral("other-instance");
        case Category::Backups:
            return QStringLiteral("backups");
        case Category::Libraries:
            return QStringLiteral("libraries");
        case Category::Java:
            return QStringLiteral("java");
        case Category::Cache:
            return QStringLiteral("cache");
        case Category::OtherLauncher:
            break;
    }
    return QStringLiteral("other-launcher");
}

const std::array<Category, kCategoryCount>& allCategories()
{
    static const std::array<Category, kCategoryCount> all = { Category::Worlds,  Category::Mods,      Category::Packs,
                                                              Category::Screenshots, Category::Logs,   Category::OtherInstance,
                                                              Category::Backups, Category::Libraries, Category::Java,
                                                              Category::Cache,   Category::OtherLauncher };
    return all;
}

QString cleanupKindId(CleanupKind kind)
{
    return kind == CleanupKind::OldCrashReports ? QStringLiteral("old-crash-reports") : QStringLiteral("old-logs");
}

qint64 CategoryBytes::sum() const
{
    qint64 total = 0;
    for (qint64 b : bytes)
        total += b;
    return total;
}

const CleanupGroup* StorageReport::group(CleanupKind kind) const
{
    for (const CleanupGroup& g : cleanupGroups) {
        if (g.kind == kind)
            return &g;
    }
    return nullptr;
}

QString pathKey(const QString& path)
{
    if (path.isEmpty())
        return {};
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toLower();
}

QString cleanupDir(CleanupKind kind, const QString& gameRoot)
{
    if (gameRoot.isEmpty())
        return {};
    return QDir::cleanPath(QFileInfo(gameRoot).absoluteFilePath()) +
           (kind == CleanupKind::OldCrashReports ? QStringLiteral("/crash-reports") : QStringLiteral("/logs"));
}

bool matchesCleanupName(CleanupKind kind, const QString& fileName)
{
    if (kind == CleanupKind::OldCrashReports)
        return fileName.endsWith(QLatin1String(".txt"), Qt::CaseInsensitive);
    if (fileName.compare(QLatin1String("latest.log"), Qt::CaseInsensitive) == 0 ||
        fileName.compare(QLatin1String("debug.log"), Qt::CaseInsensitive) == 0)
        return false;
    return fileName.endsWith(QLatin1String(".log"), Qt::CaseInsensitive) || fileName.endsWith(QLatin1String(".log.gz"), Qt::CaseInsensitive);
}

bool isOlderThanCleanupAge(qint64 modifiedMs, qint64 nowMs)
{
    return modifiedMs < nowMs - kCleanupAgeMs;
}

bool isPlainFile(const QFileInfo& info)
{
    return info.isFile() && !isLink(info);
}

bool isCleanupCandidate(CleanupKind kind, const QFileInfo& info, qint64 nowMs, QString* why)
{
    auto no = [why](const char* reason) {
        if (why)
            *why = QString::fromLatin1(reason);
        return false;
    };
    if (!isPlainFile(info))
        return no("Not a regular file any more");
    if (!matchesCleanupName(kind, info.fileName()))
        return no("The file name is not a log / crash report name");
    if (!isOlderThanCleanupAge(info.lastModified().toMSecsSinceEpoch(), nowMs))
        return no("The file is newer than 30 days");
    return true;
}

QList<CleanupFile> findCleanupFiles(const InstanceSource& source, CleanupKind kind, qint64 nowMs)
{
    QList<CleanupFile> out;
    if (source.running)
        return out;
    const QString dir = cleanupDir(kind, source.gameRoot);
    if (dir.isEmpty() || !isInstanceDirPlain(dir))
        return out;
    for (const auto& entry : QDirListing(dir, QDirListing::IteratorFlag::IncludeHidden)) {  // non-recursive
        const QFileInfo info = entry.fileInfo();
        if (!isCleanupCandidate(kind, info, nowMs))
            continue;
        CleanupFile f;
        f.kind = kind;
        f.path = info.absoluteFilePath();
        f.sizeBytes = info.size();
        f.modifiedMs = info.lastModified().toMSecsSinceEpoch();
        f.instanceId = source.id;
        out.append(f);
    }
    return out;
}

QList<RestoreLeftover> findRestoreLeftovers(const InstanceSource& source)
{
    QList<RestoreLeftover> out;
    if (source.worldsDir.isEmpty())
        return out;
    for (const auto& entry : QDirListing(source.worldsDir, QDirListing::IteratorFlag::IncludeHidden | QDirListing::IteratorFlag::DirsOnly)) {
        const QFileInfo info = entry.fileInfo();
        if (isLink(info) || !isRestoreLeftoverName(info.fileName()))
            continue;
        RestoreLeftover l;
        l.path = info.absoluteFilePath();
        l.instanceId = source.id;
        l.instanceName = source.name;
        l.sizeBytes = Worlds::folderSize(l.path);
        out.append(l);
    }
    std::sort(out.begin(), out.end(), [](const RestoreLeftover& a, const RestoreLeftover& b) { return a.path < b.path; });
    return out;
}

// ---- Largest items ---------------------------------------------------------------------------------------------------------------

QString itemKindId(ItemKind kind)
{
    switch (kind) {
        case ItemKind::World: return QStringLiteral("world");
        case ItemKind::Mod: return QStringLiteral("mod");
        case ItemKind::Pack: return QStringLiteral("pack");
        case ItemKind::Logs: return QStringLiteral("logs");
        case ItemKind::CrashReports: return QStringLiteral("crash-reports");
        case ItemKind::Screenshots: return QStringLiteral("screenshots");
        case ItemKind::WorldBackups: return QStringLiteral("world-backups");
        case ItemKind::Snapshots: return QStringLiteral("snapshots");
        case ItemKind::ZeroData: return QStringLiteral("zero-data");
        case ItemKind::Folder: return QStringLiteral("folder");
        case ItemKind::BigFile: return QStringLiteral("big-file");
    }
    return {};
}

bool isTrashableKind(ItemKind kind)
{
    return kind != ItemKind::WorldBackups && kind != ItemKind::Snapshots && kind != ItemKind::ZeroData;
}

namespace {

QString cleanAbs(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

/// Entries directly inside `dir` (hidden included, links skipped).
QList<QFileInfo> plainChildren(const QString& dir)
{
    QList<QFileInfo> out;
    if (dir.isEmpty() || !QFileInfo(dir).isDir())
        return out;
    for (const auto& entry : QDirListing(dir, QDirListing::IteratorFlag::IncludeHidden)) {
        const QFileInfo info = entry.fileInfo();
        if (!isLink(info))
            out.append(info);
    }
    return out;
}

void collectBigFiles(const QString& dir, const QSet<QString>& skipDirs, qint64 minBytes, QList<QPair<QString, qint64>>& out)
{
    for (const auto& entry : QDirListing(dir, QDirListing::IteratorFlag::IncludeHidden)) {
        const QFileInfo info = entry.fileInfo();
        if (isLink(info))
            continue;
        if (entry.isDir()) {
            if (info.fileName().startsWith(QLatin1String(".zero-")) || skipDirs.contains(pathKey(entry.filePath())))
                continue;
            collectBigFiles(entry.filePath(), skipDirs, minBytes, out);
        } else if (entry.isFile() && entry.size() > minBytes) {
            out.append({ cleanAbs(entry.filePath()), entry.size() });
        }
    }
}

bool sameDir(const QString& a, const QString& b)
{
    return !a.isEmpty() && !b.isEmpty() && pathKey(a) == pathKey(b);
}

QStringList packDirsOf(const InstanceSource& s)
{
    QStringList out;
    for (const QString& p : s.packDirs)
        if (!p.isEmpty())
            out << p;
    return out;
}

/// Folders of the game root whose contents are listed entry by entry (or as one named item): never a "Folder" item themselves.
QStringList handledDirs(const InstanceSource& s)
{
    QStringList out = packDirsOf(s);
    for (const QString& p : { s.worldsDir, s.modsDir })
        if (!p.isEmpty())
            out << p;
    if (!s.gameRoot.isEmpty())
        out << s.gameRoot + QStringLiteral("/logs") << s.gameRoot + QStringLiteral("/crash-reports") << s.gameRoot + QStringLiteral("/screenshots");
    return out;
}

}  // namespace

QList<LargeItem> findLargeItems(const InstanceSource& source, int limit, qint64 bigFileBytes)
{
    QList<LargeItem> items;
    QSet<QString> seen;
    auto add = [&](ItemKind kind, const QString& path, qint64 size, bool isDir) {
        const QString clean = cleanAbs(path);
        const QString key = pathKey(clean);
        if (size <= 0 || seen.contains(key))
            return;
        seen.insert(key);
        LargeItem item;
        item.kind = kind;
        item.path = clean;
        item.name = QFileInfo(clean).fileName();
        item.instanceId = source.id;
        item.instanceName = source.name;
        item.sizeBytes = size;
        item.isDir = isDir;
        item.running = source.running;
        items.append(item);
    };
    auto addChildren = [&](ItemKind kind, const QString& dir) {
        for (const QFileInfo& info : plainChildren(dir)) {
            if (info.isDir())
                add(kind, info.absoluteFilePath(), Worlds::folderSize(info.absoluteFilePath()), true);
            else if (info.isFile())
                add(kind, info.absoluteFilePath(), info.size(), false);
        }
    };
    auto addWhole = [&](ItemKind kind, const QString& dir) {
        const QFileInfo info(dir);
        if (!dir.isEmpty() && info.isDir() && !isLink(info))
            add(kind, dir, Worlds::folderSize(dir), true);
    };

    if (!source.worldsDir.isEmpty() && !isLink(QFileInfo(source.worldsDir)))
        for (const QFileInfo& info : plainChildren(source.worldsDir))
            if (info.isDir() && !info.fileName().startsWith(QLatin1String(".zero-")))  // ".zero-*": an interrupted restore, see findRestoreLeftovers
                add(ItemKind::World, info.absoluteFilePath(), Worlds::folderSize(info.absoluteFilePath()), true);
    if (!source.modsDir.isEmpty() && !isLink(QFileInfo(source.modsDir)))
        addChildren(ItemKind::Mod, source.modsDir);
    for (const QString& pack : packDirsOf(source))
        if (!isLink(QFileInfo(pack)))
            addChildren(ItemKind::Pack, pack);
    if (!source.gameRoot.isEmpty()) {
        addWhole(ItemKind::Logs, source.gameRoot + QStringLiteral("/logs"));
        addWhole(ItemKind::CrashReports, source.gameRoot + QStringLiteral("/crash-reports"));
        addWhole(ItemKind::Screenshots, source.gameRoot + QStringLiteral("/screenshots"));

        // The other top-level folders of the game root (a folder that holds worlds / mods / packs is already listed entry by entry).
        QSet<QString> handledKeys;
        for (const QString& h : handledDirs(source))
            handledKeys.insert(pathKey(h));
        for (const QFileInfo& info : plainChildren(source.gameRoot)) {
            if (!info.isDir() || info.fileName().startsWith(QLatin1String(".zero-")))
                continue;
            const QString key = pathKey(info.absoluteFilePath());
            bool covered = handledKeys.contains(key);
            for (const QString& h : std::as_const(handledKeys))
                covered = covered || keyInside(h, key);
            if (!covered)
                add(ItemKind::Folder, info.absoluteFilePath(), Worlds::folderSize(info.absoluteFilePath()), true);
        }

        // Single files over the threshold; folders listed as a whole or entry by entry (worlds, mods, packs, logs...) are not walked.
        if (bigFileBytes > 0 && QFileInfo(source.gameRoot).isDir()) {
            QList<QPair<QString, qint64>> big;
            collectBigFiles(source.gameRoot, handledKeys, bigFileBytes, big);
            for (const auto& f : std::as_const(big))
                add(ItemKind::BigFile, f.first, f.second, false);
        }
    }
    if (!source.instanceRoot.isEmpty()) {
        const QString zero = source.instanceRoot + QStringLiteral("/.zero");
        const QFileInfo info(zero);
        if (info.isDir() && !isLink(info))
            add(ItemKind::ZeroData, zero, Worlds::folderSize(zero), true);
    }

    std::sort(items.begin(), items.end(), [](const LargeItem& a, const LargeItem& b) {
        return a.sizeBytes != b.sizeBytes ? a.sizeBytes > b.sizeBytes : a.path < b.path;
    });
    if (limit >= 0 && items.size() > limit)
        items = items.mid(0, limit);
    return items;
}

QList<LargeItem> findZeroBackupItems(const InstanceSource& source, const QString& dataRoot)
{
    QList<LargeItem> items;
    if (dataRoot.isEmpty() || source.id.isEmpty())
        return items;
    auto make = [&](ItemKind kind, const QString& path, qint64 size) {
        LargeItem item;
        item.kind = kind;
        item.path = cleanAbs(path);
        item.name = QFileInfo(path).fileName();
        item.instanceId = source.id;
        item.instanceName = source.name;
        item.sizeBytes = size;
        item.isDir = true;
        item.running = source.running;
        return item;
    };
    for (const QFileInfo& info : plainChildren(dataRoot + QStringLiteral("/zero/backups/") + source.id)) {
        if (!info.isDir())
            continue;
        const qint64 size = Worlds::folderSize(info.absoluteFilePath());
        if (size > 0)
            items.append(make(ItemKind::WorldBackups, info.absoluteFilePath(), size));
    }
    const QString snapshots = dataRoot + QStringLiteral("/zero/snapshots/") + source.id;
    if (QFileInfo(snapshots).isDir() && !isLink(QFileInfo(snapshots))) {
        const qint64 size = Worlds::folderSize(snapshots);
        if (size > 0)
            items.append(make(ItemKind::Snapshots, snapshots, size));
    }
    return items;
}

QList<LargeItem> aggregateLargest(const QList<LargeItem>& all, const QString& instanceId, int limit)
{
    QList<LargeItem> out;
    for (const LargeItem& item : all)
        if (instanceId.isEmpty() || item.instanceId == instanceId)
            out.append(item);
    std::sort(out.begin(), out.end(), [](const LargeItem& a, const LargeItem& b) {
        return a.sizeBytes != b.sizeBytes ? a.sizeBytes > b.sizeBytes : a.path < b.path;
    });
    if (limit >= 0 && out.size() > limit)
        out = out.mid(0, limit);
    return out;
}

bool isTrashableItem(const LargeItem& item, const InstanceSource& source, QString* why)
{
    auto no = [why](const QString& reason) {
        if (why)
            *why = reason;
        return false;
    };
    if (!isTrashableKind(item.kind))
        return no(QStringLiteral("ZERO manages this item itself"));
    if (item.path.isEmpty())
        return no(QStringLiteral("Empty path"));
    if (source.running)
        return no(QStringLiteral("The instance is running"));
    const QString clean = cleanAbs(item.path);
    const QFileInfo info(clean);
    if (!info.exists() || isLink(info))
        return no(QStringLiteral("The item is gone or is a link"));
    const QString parent = info.path();
    const QString gameRoot = source.gameRoot;
    bool placeOk = false;
    switch (item.kind) {
        case ItemKind::World:
            placeOk = info.isDir() && sameDir(parent, source.worldsDir) && !info.fileName().startsWith(QLatin1String(".zero-"));
            break;
        case ItemKind::Mod:
            placeOk = sameDir(parent, source.modsDir);
            break;
        case ItemKind::Pack:
            for (const QString& p : packDirsOf(source))
                placeOk = placeOk || sameDir(parent, p);
            break;
        case ItemKind::Logs:
            placeOk = info.isDir() && !gameRoot.isEmpty() && sameDir(clean, gameRoot + QStringLiteral("/logs"));
            break;
        case ItemKind::CrashReports:
            placeOk = info.isDir() && !gameRoot.isEmpty() && sameDir(clean, gameRoot + QStringLiteral("/crash-reports"));
            break;
        case ItemKind::Screenshots:
            placeOk = info.isDir() && !gameRoot.isEmpty() && sameDir(clean, gameRoot + QStringLiteral("/screenshots"));
            break;
        case ItemKind::Folder: {
            // A top-level folder of the game root that is not one of the managed folders (or the one that holds them).
            placeOk = info.isDir() && sameDir(parent, gameRoot) && !info.fileName().startsWith(QLatin1String(".zero-"));
            const QString key = pathKey(clean);
            for (const QString& h : handledDirs(source))
                placeOk = placeOk && pathKey(h) != key && !keyInside(pathKey(h), key);
            break;
        }
        case ItemKind::BigFile: {
            placeOk = info.isFile() && !gameRoot.isEmpty() && keyInside(pathKey(clean), pathKey(gameRoot));
            for (const QString& h : handledDirs(source))
                placeOk = placeOk && !keyInside(pathKey(clean), pathKey(h));
            if (placeOk)
                placeOk = reachableWithoutLinks(clean, gameRoot);
            break;
        }
        default:
            break;
    }
    if (!placeOk)
        return no(QStringLiteral("Not where this kind of item lives in the instance"));
    return true;
}

TrashResult trashLargeItems(const QList<LargeItem>& items, const QList<InstanceSource>& sources, const TrashHooks& hooks)
{
    TrashResult result;
    const std::function<bool(const QString&)> trashPath = hooks.trashPath ? hooks.trashPath : [](const QString& p) { return FS::trash(p); };
    for (const LargeItem& item : items) {
        const InstanceSource* source = nullptr;
        for (const InstanceSource& s : sources)
            if (s.id == item.instanceId)
                source = &s;
        QString why;
        if (!source) {
            result.skippedCount++;
            result.problems.append({ item.path, QStringLiteral("Unknown instance"), true });
            continue;
        }
        if (!isTrashableItem(item, *source, &why)) {
            result.skippedCount++;
            result.problems.append({ item.path, why, true });
            continue;
        }
        const qint64 size = pathSize(item.path);
        if (trashPath(cleanAbs(item.path))) {
            result.trashedCount++;
            result.trashedBytes += size;
        } else {
            result.failedCount++;
            result.problems.append({ item.path, QStringLiteral("Could not move the item to the Recycle Bin"), false });
        }
    }
    return result;
}

InstanceRow measureInstance(const InstanceSource& source)
{
    InstanceRow row;
    row.id = source.id;
    row.name = source.name;
    row.instanceRoot = source.instanceRoot;

    QList<Region> input;
    input.append({ source.worldsDir, Category::Worlds });
    input.append({ source.modsDir, Category::Mods });
    for (const QString& pack : source.packDirs)
        input.append({ pack, Category::Packs });
    if (!source.gameRoot.isEmpty()) {
        input.append({ source.gameRoot + QStringLiteral("/screenshots"), Category::Screenshots });
        input.append({ source.gameRoot + QStringLiteral("/logs"), Category::Logs });
        input.append({ source.gameRoot + QStringLiteral("/crash-reports"), Category::Logs });
    }
    const QList<Region> regions = measureRegions(input);
    for (const Region& r : regions)
        row.bytes.at(*r.category) += r.bytes;
    if (!source.instanceRoot.isEmpty())
        row.bytes.at(Category::OtherInstance) = sizeBelowRoot(source.instanceRoot, regions);
    row.total = row.bytes.sum();
    return row;
}

CategoryBytes measureLauncher(const LauncherSource& launcher)
{
    CategoryBytes out;
    if (launcher.dataRoot.isEmpty())
        return out;
    const QString root = launcher.dataRoot;
    auto sub = [&root](const QString& rel) { return QDir::cleanPath(QFileInfo(root).absoluteFilePath()) + QLatin1Char('/') + rel; };
    QList<Region> input;
    input.append({ sub(QStringLiteral("zero/backups")), Category::Backups });
    input.append({ sub(QStringLiteral("libraries")), Category::Libraries });
    input.append({ sub(QStringLiteral("assets")), Category::Libraries });
    input.append({ sub(QStringLiteral("java")), Category::Java });
    input.append({ sub(QStringLiteral("cache")), Category::Cache });
    input.append({ sub(QStringLiteral("meta")), Category::Cache });
    input.append({ sub(QStringLiteral("metacache")), Category::Cache });
    const QList<Region> counted = measureRegions(input);
    for (const Region& r : counted)
        out.at(*r.category) += r.bytes;

    // The instances dir is measured per instance (see measureInstance), so it is only excluded here.
    QList<Region> withInstances = counted;
    if (!launcher.instancesDir.isEmpty()) {
        Region inst;
        inst.path = launcher.instancesDir;
        inst.key = pathKey(inst.path);
        withInstances.append(inst);
    }
    out.at(Category::OtherLauncher) = sizeBelowRoot(root, withInstances);
    return out;
}

StorageReport buildReport(const QList<InstanceSource>& instances, const LauncherSource& launcher, qint64 nowMs, const ProgressFn& progress)
{
    StorageReport report;
    report.generatedMs = nowMs;
    report.dataRoot = launcher.dataRoot;

    const int steps = int(instances.size()) + 1;  // every instance, then the launcher's own folders
    int done = 0;
    for (const InstanceSource& source : instances) {
        if (progress)
            progress(done, steps, source.name);
        ++done;
        InstanceRow row = measureInstance(source);
        report.largest += findLargeItems(source);
        report.largest += findZeroBackupItems(source, launcher.dataRoot);
        for (Category c : allCategories())
            report.totals.at(c) += row.bytes.of(c);
        report.instances.append(row);
    }
    std::stable_sort(report.instances.begin(), report.instances.end(), [](const InstanceRow& a, const InstanceRow& b) {
        if (a.total != b.total)
            return a.total > b.total;
        const int c = a.name.localeAwareCompare(b.name);
        return c != 0 ? c < 0 : a.id < b.id;
    });

    report.largest = aggregateLargest(report.largest, QString(), -1);  // biggest first
    if (progress)
        progress(done, steps, QString());
    const CategoryBytes launcherBytes = measureLauncher(launcher);
    for (Category c : allCategories())
        report.totals.at(c) += launcherBytes.of(c);
    report.totalBytes = report.totals.sum();

    for (CleanupKind kind : { CleanupKind::OldLogs, CleanupKind::OldCrashReports }) {
        CleanupGroup group;
        group.kind = kind;
        for (const InstanceSource& source : instances)
            group.files += findCleanupFiles(source, kind, nowMs);
        std::sort(group.files.begin(), group.files.end(), [](const CleanupFile& a, const CleanupFile& b) {
            return a.sizeBytes != b.sizeBytes ? a.sizeBytes > b.sizeBytes : a.path < b.path;
        });
        for (const CleanupFile& f : std::as_const(group.files))
            group.totalBytes += f.sizeBytes;
        report.cleanupGroups.append(group);
    }
    for (const InstanceSource& source : instances)
        report.leftovers += findRestoreLeftovers(source);

    if (!launcher.dataRoot.isEmpty()) {
        const QStorageInfo volume(launcher.dataRoot);
        if (volume.isValid() && volume.isReady()) {
            report.volumeFreeBytes = volume.bytesAvailable();
            report.volumeTotalBytes = volume.bytesTotal();
        }
    }
    return report;
}

TrashResult trashCandidates(const QList<CleanupFile>& files, const QList<InstanceSource>& sources, qint64 nowMs, const TrashHooks& hooks)
{
    TrashResult result;
    const std::function<bool(const QString&)> trashPath = hooks.trashPath ? hooks.trashPath : [](const QString& p) { return FS::trash(p); };

    auto skip = [&result](const QString& path, const QString& reason) {
        result.skippedCount++;
        result.problems.append({ path, reason, true });
    };

    for (const CleanupFile& file : files) {
        if (file.path.isEmpty()) {
            skip(file.path, QStringLiteral("Empty path"));
            continue;
        }
        const QString clean = QDir::cleanPath(QFileInfo(file.path).absoluteFilePath());
        const QFileInfo info(clean);

        // The parent folder must be exactly the logs / crash-reports folder of a known instance, and no such instance may be running.
        const QString parentKey = pathKey(info.path());
        bool known = false;
        bool running = false;
        for (const InstanceSource& source : sources) {
            const QString dir = cleanupDir(file.kind, source.gameRoot);
            if (dir.isEmpty() || pathKey(dir) != parentKey)
                continue;
            known = true;
            running = running || source.running;
        }
        if (!known) {
            skip(file.path, QStringLiteral("Not inside the logs / crash-reports folder of a known instance"));
            continue;
        }
        if (running) {
            skip(file.path, QStringLiteral("The instance is running"));
            continue;
        }
        // The folder itself must not be a link (that would reach files outside the instance).
        if (!isInstanceDirPlain(info.path())) {
            skip(file.path, QStringLiteral("The folder is a link or does not exist"));
            continue;
        }
        QString why;
        if (!isCleanupCandidate(file.kind, info, nowMs, &why)) {
            skip(file.path, why);
            continue;
        }

        const qint64 size = info.size();
        if (trashPath(info.absoluteFilePath())) {
            result.trashedCount++;
            result.trashedBytes += size;
        } else {
            // Never fall back to a permanent delete: the file stays where it is.
            result.failedCount++;
            result.problems.append({ file.path, QStringLiteral("Could not move the file to the Recycle Bin"), false });
        }
    }
    return result;
}

StorageScanner::StorageScanner(SourceProvider provider, QObject* parent, TrashHooks hooks)
    : QObject(parent), m_provider(std::move(provider)), m_hooks(std::move(hooks))
{
    qRegisterMetaType<Zero::Storage::StorageReport>();
    qRegisterMetaType<Zero::Storage::TrashResult>();
}

void StorageScanner::rescan()
{
    const quint64 generation = ++m_generation;
    QPair<QList<InstanceSource>, LauncherSource> sources;
    if (m_provider)
        sources = m_provider();
    m_scanning = true;
    emit scanStarted();

    // Same pattern as WorldScanner: the pool job never touches `this`; it posts to the application object and the QPointer is
    // read on the GUI thread.
    QPointer<StorageScanner> self(this);
    QThreadPool::globalInstance()->start([self, sources, generation]() {
        auto* app = QCoreApplication::instance();
        if (!app)
            return;
        const ProgressFn progress = [self, generation, app](int done, int total, const QString& current) {
            QMetaObject::invokeMethod(
                app,
                [self, generation, done, total, current]() {
                    if (self && self->generation() == generation && self->isScanning())
                        emit self->scanProgress(done, total, current);
                },
                Qt::QueuedConnection);
        };
        const StorageReport report = buildReport(sources.first, sources.second, QDateTime::currentMSecsSinceEpoch(), progress);
        QMetaObject::invokeMethod(
            app,
            [self, generation, report]() {
                if (self)
                    self->onReport(generation, report);
            },
            Qt::QueuedConnection);
    });
}

void StorageScanner::onReport(quint64 generation, const StorageReport& report)
{
    if (generation != m_generation)
        return;  // a newer rescan() superseded this one
    m_report = report;
    m_hasReport = true;
    m_scanning = false;
    emit reportReady(m_report);
}

void StorageScanner::trash(const QList<CleanupFile>& files)
{
    startTrash([files](const QList<InstanceSource>& sources, const TrashHooks& hooks) {
        return trashCandidates(files, sources, QDateTime::currentMSecsSinceEpoch(), hooks);
    });
}

void StorageScanner::trashItems(const QList<LargeItem>& items)
{
    startTrash([items](const QList<InstanceSource>& sources, const TrashHooks& hooks) { return trashLargeItems(items, sources, hooks); });
}

void StorageScanner::startTrash(std::function<TrashResult(const QList<InstanceSource>&, const TrashHooks&)> job)
{
    if (m_trashing) {
        TrashResult refused;
        refused.error = QStringLiteral("Another cleanup is already running");
        QPointer<StorageScanner> self(this);
        QMetaObject::invokeMethod(
            this,
            [self, refused]() {
                if (self)
                    emit self->trashFinished(refused);
            },
            Qt::QueuedConnection);
        return;
    }
    m_trashing = true;
    // Fresh `running` flags, read on the GUI thread right before the job is posted.
    QPair<QList<InstanceSource>, LauncherSource> sources;
    if (m_provider)
        sources = m_provider();

    QPointer<StorageScanner> self(this);
    const TrashHooks hooks = m_hooks;
    QThreadPool::globalInstance()->start([self, job, sources, hooks]() {
        const TrashResult result = job(sources.first, hooks);
        auto* app = QCoreApplication::instance();
        if (!app)
            return;
        QMetaObject::invokeMethod(
            app,
            [self, result]() {
                if (self)
                    self->onTrashed(result);
            },
            Qt::QueuedConnection);
    });
}

void StorageScanner::onTrashed(const TrashResult& result)
{
    m_trashing = false;
    emit trashFinished(result);
}

}  // namespace Storage
}  // namespace Zero
