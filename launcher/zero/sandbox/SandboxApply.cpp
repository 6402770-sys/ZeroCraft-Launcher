// SPDX-License-Identifier: GPL-3.0-only
#include "SandboxApply.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QTimeZone>
#include <QtDebug>

#include "FileSystem.h"
#include "zero/backup/WorldBackup.h"

namespace Zero {
namespace Sandbox {

namespace {

Hooks resolve(const Hooks& in)
{
    Hooks h = in;
    if (!h.trashPath)
        h.trashPath = [](const QString& path) { return FS::trash(path); };  // deliberately no fallback: it stays where it is
    if (!h.rename)
        h.rename = [](const QString& from, const QString& to) { return Backup::renamePath(from, to); };
    if (!h.copyTree)
        h.copyTree = [](const QString& from, const QString& to) { return copyTree(from, to); };
    if (!h.nowMs)
        h.nowMs = []() { return QDateTime::currentMSecsSinceEpoch(); };
    return h;
}

bool samePath(const QString& a, const QString& b)
{
    return QDir::cleanPath(QFileInfo(a).absoluteFilePath()).compare(QDir::cleanPath(QFileInfo(b).absoluteFilePath()), Qt::CaseInsensitive) == 0;
}

/// A path that exists as itself (a broken link counts).
bool pathExists(const QString& path)
{
    const QFileInfo info(path);
    return info.exists() || info.isSymLink();
}

/// Removes empty folders below and including `path`; never a file, never a folder that has something in it.
void removeEmptyTree(const QString& path)
{
    const QFileInfo info(path);
    if (!info.isDir() || info.isSymLink())
        return;
    for (const QString& n : QDir(path).entryList(QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
        removeEmptyTree(QDir(path).filePath(n));
    QDir().rmdir(path);
}

/// A real folder (not a link / junction).
bool realDir(const QString& path)
{
    const QFileInfo info(path);
    return info.isDir() && !info.isSymLink() && !info.isJunction();
}

/// Names SandboxPlan ignores in a comparison; they stay as they are in the original.
bool ignoredName(const QString& name)
{
    return name.startsWith(QLatin1String(".zero-")) || name == QLatin1String("session.lock");
}

struct Item {
    bool world = false;
    QString key;    ///< partKey
    QString name;   ///< last path component (shown in the messages; the Recycle Bin name of an item without a group)
    QString src;    ///< in the copy
    QString dst;    ///< the original's file / folder (its own spelling)
    QString place;  ///< where the copy's version goes (= dst, or the copy's spelling of the name inside a kept folder)
    bool srcExists = false;
    bool dstExists = false;
    int group = -1;   ///< >= 0: an entry of a kept folder (index into the groups); its old version goes below that group's old folder
    QString rel;      ///< group items: the path below the kept folder
    QString newPath;  ///< staging/new/<n>
    QString oldPath;  ///< staging/old/<name> (group items: <group's old folder>/<rel>)
    bool movedOld = false;
    bool placedNew = false;
};

/// A folder of the original that both instances have (or that the copy removed): it is never renamed, only the entries inside it move.
/// Windows refuses to rename a folder while anything below it is open or watched (the launcher's own models watch `mods/.index`).
struct Group {
    QString name;     ///< the part's folder name, e.g. "mods" (the name the Recycle Bin shows)
    QString oldRoot;  ///< staging/old/<name>: the replaced entries, sent to the Recycle Bin as one folder
};

/// One differing entry below a kept folder.
struct Entry {
    QString rel;      ///< path below the kept folder (the original's spelling where it has the entry)
    QString srcName;  ///< the copy's spelling of the last name; empty = only the original has it
};

/// The differing entries of two folders. Folders both sides have are descended into instead of being listed, so no folder that exists
/// in both instances is ever renamed. Names compare case-insensitively (Windows); ignored names stay as they are.
void differingEntries(const QString& dst, const QString& src, const QString& rel, QList<Entry>& out)
{
    const auto filter = QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot;
    QStringList names;  // the original's spelling first
    QSet<QString> seen;  // lower case
    QHash<QString, QString> srcSpelling;
    for (const QString& n : QDir(dst).entryList(filter, QDir::Name)) {
        if (!ignoredName(n) && !seen.contains(n.toLower())) {
            seen.insert(n.toLower());
            names << n;
        }
    }
    for (const QString& n : QDir(src).entryList(filter, QDir::Name)) {
        if (ignoredName(n) || srcSpelling.contains(n.toLower()))
            continue;
        srcSpelling.insert(n.toLower(), n);
        if (!seen.contains(n.toLower())) {
            seen.insert(n.toLower());
            names << n;
        }
    }
    for (const QString& n : std::as_const(names)) {
        const QString d = QDir(dst).filePath(n);
        const QString srcName = srcSpelling.value(n.toLower());
        const QString s = QDir(src).filePath(srcName.isEmpty() ? n : srcName);
        const bool dExists = pathExists(d);
        const bool sExists = !srcName.isEmpty();
        if (dExists && sExists && realDir(d) && realDir(s)) {
            differingEntries(d, s, rel + n + QLatin1Char('/'), out);
            continue;
        }
        if (dExists != sExists || treesDiffer(d, s))
            out.append({ rel + n, srcName });
    }
}

}  // namespace

bool copyTree(const QString& from, const QString& to)
{
    const QFileInfo src(from);
    if (src.isDir()) {
        if (src.isSymLink() || src.isJunction())
            return false;
        if (!QDir().mkpath(to))
            return false;
        for (const QString& n : QDir(from).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
            if (!copyTree(QDir(from).filePath(n), QDir(to).filePath(n)))
                return false;
        }
        return true;
    }
    if (!src.isFile())
        return false;
    if (!QDir().mkpath(QFileInfo(to).absolutePath()) || !QFile::copy(from, to))
        return false;
    // keep the modification time: it is what the comparison of the two instances relies on
    QFile out(to);
    if (out.open(QIODevice::ReadWrite | QIODevice::ExistingOnly)) {
        out.setFileTime(src.lastModified(), QFileDevice::FileModificationTime);
        out.close();
    }
    return true;
}

std::function<QString(const QString&, const QString&)> safetyBackupHook(const QString& instanceId, const QString& instanceName,
                                                                         std::function<QString(const QString& worldFolder)> backupDirOf)
{
    return [instanceId, instanceName, backupDirOf](const QString& worldFolder, const QString& worldPath) -> QString {
        const QString dir = backupDirOf(worldFolder);
        if (dir.isEmpty())
            return QCoreApplication::translate("Zero::Sandbox", "There is no place for the backups.");
        const Backup::BackupMeta meta{ instanceId, instanceName, worldFolder, worldFolder };
        const Backup::BackupResult made = Backup::createBackup(worldPath, dir, Backup::Kind::Safety, meta);
        if (!made.ok)
            return made.error.isEmpty() ? QCoreApplication::translate("Zero::Sandbox", "The backup failed.") : made.error;
        Backup::applyRetention(dir, { made.zipPath });
        return {};
    };
}

Result apply(const Request& request, const Hooks& hooksIn)
{
    const Hooks h = resolve(hooksIn);
    Result result;
    auto fail = [&](const QString& message, bool restored = true) {
        result.ok = false;
        result.error = message;
        result.restored = restored;
        return result;
    };

    if (request.original.instanceRoot.isEmpty() || request.copy.instanceRoot.isEmpty() || samePath(request.original.instanceRoot, request.copy.instanceRoot))
        return fail(QCoreApplication::translate("Zero::Sandbox", "The instances to compare are not valid."));
    if (!QFileInfo(request.original.instanceRoot).isDir() || !QFileInfo(request.copy.instanceRoot).isDir())
        return fail(QCoreApplication::translate("Zero::Sandbox", "The instance folder was not found."));

    // 1. the targets
    QList<Item> items;
    QList<Group> groups;
    QList<Part> seen;
    for (const Part part : request.parts) {
        if (seen.contains(part))
            continue;
        seen << part;
        if (part == Part::Worlds) {
            for (const QString& name : changedWorlds(request.original, request.copy)) {
                Item item;
                item.world = true;
                item.key = partKey(part);
                item.name = name;
                item.src = QDir(request.copy.savesDir()).filePath(name);
                item.dst = QDir(request.original.savesDir()).filePath(name);
                item.place = item.dst;
                item.srcExists = true;
                item.dstExists = pathExists(item.dst);
                items.append(item);
            }
            continue;
        }
        for (const Target& t : targetsOf(part, request.original, request.copy)) {
            const bool srcExists = pathExists(t.copy);
            const bool dstExists = pathExists(t.original);
            if ((!srcExists && !dstExists) || !treesDiffer(t.original, t.copy))
                continue;  // nothing to bring back
            if (realDir(t.original) && (!srcExists || realDir(t.copy))) {
                // a folder the original has (and the copy has too, or removed): it stays, its differing entries move
                QList<Entry> entries;
                differingEntries(t.original, t.copy, QString(), entries);
                if (entries.isEmpty())
                    continue;
                const int group = static_cast<int>(groups.size());
                groups.append({ QFileInfo(t.original).fileName(), QString() });
                for (const Entry& e : std::as_const(entries)) {
                    const int slash = static_cast<int>(e.rel.lastIndexOf(QLatin1Char('/')));
                    const QString parentRel = slash < 0 ? QString() : e.rel.left(slash);
                    Item item;
                    item.key = partKey(part);
                    item.group = group;
                    item.rel = e.rel;
                    item.dst = QDir(t.original).filePath(e.rel);
                    item.name = QFileInfo(item.dst).fileName();
                    item.dstExists = pathExists(item.dst);
                    item.srcExists = !e.srcName.isEmpty();
                    item.src = QDir(QDir(t.copy).filePath(parentRel)).filePath(item.srcExists ? e.srcName : item.name);
                    item.place = QDir(QDir(t.original).filePath(parentRel)).filePath(item.srcExists ? e.srcName : item.name);
                    items.append(item);
                }
                continue;
            }
            Item item;
            item.key = partKey(part);
            item.name = QFileInfo(t.original).fileName();
            item.src = t.copy;
            item.dst = t.original;
            item.place = t.original;
            item.srcExists = srcExists;
            item.dstExists = dstExists;
            items.append(item);
        }
    }
    if (items.isEmpty()) {
        result.ok = true;
        return result;
    }

    // 2. a safety backup of every world that is about to be replaced; one that cannot be made stops everything
    for (const Item& item : std::as_const(items)) {
        if (!item.world || !item.dstExists)
            continue;
        if (!h.backupWorld)
            return fail(QCoreApplication::translate("Zero::Sandbox", "Could not save a backup of the world %1 first, so nothing was changed.").arg(item.name));
        const QString error = h.backupWorld(item.name, item.dst);
        if (!error.isEmpty())
            return fail(QCoreApplication::translate("Zero::Sandbox", "Could not save a backup of the world %1 first, so nothing was changed: %2").arg(item.name, error));
    }

    // 3. staging: the copy's version of every target, next to the original
    const QString stamp = QDateTime::fromMSecsSinceEpoch(h.nowMs(), QTimeZone::utc()).toString(QStringLiteral("yyyyMMddHHmmsszzz"));
    const QString staging = QDir(request.original.instanceRoot).filePath(QStringLiteral(".zero-bringback-%1").arg(stamp));
    const QString newDir = QDir(staging).filePath(QStringLiteral("new"));
    const QString oldDir = QDir(staging).filePath(QStringLiteral("old"));
    if (pathExists(staging))
        return fail(QCoreApplication::translate("Zero::Sandbox", "Bringing changes back is already in progress for this instance."));
    if (!QDir().mkpath(newDir) || !QDir().mkpath(oldDir)) {
        removeEmptyTree(staging);
        return fail(QCoreApplication::translate("Zero::Sandbox", "Could not prepare the folder for the changes, so nothing was changed."));
    }
    auto dropNew = [&]() {
        QDir(newDir).removeRecursively();  // ZERO's own copies of the test copy's files
        removeEmptyTree(staging);
    };
    // the names below old/: one per kept folder or per whole item, never twice
    QSet<QString> usedOld;
    auto reserveOld = [&](const QString& name) {
        QString candidate = name;
        for (int i = 2; usedOld.contains(candidate.toLower()); ++i)
            candidate = QStringLiteral("%1-%2").arg(name).arg(i);
        usedOld.insert(candidate.toLower());
        return QDir(oldDir).filePath(candidate);
    };
    for (Group& group : groups)
        group.oldRoot = reserveOld(group.name);
    for (int i = 0; i < items.size(); ++i) {
        Item& item = items[i];
        item.oldPath = item.group >= 0 ? QDir(groups.at(item.group).oldRoot).filePath(item.rel) : reserveOld(item.name);
        if (!item.srcExists)
            continue;
        item.newPath = QDir(newDir).filePath(QString::number(i));
        if (!h.copyTree(item.src, item.newPath)) {
            dropNew();
            return fail(QCoreApplication::translate("Zero::Sandbox", "Could not copy %1 from the test copy, so nothing was changed.").arg(item.name));
        }
    }

    // 4. the swap, every target by two renames; a failure puts the targets back in reverse order
    QList<int> started;
    auto undo = [&]() {
        QStringList stuck;
        for (auto it = started.crbegin(); it != started.crend(); ++it) {
            Item& item = items[*it];
            if (item.placedNew) {
                if (h.rename(item.place, item.newPath))
                    item.placedNew = false;
                else {
                    stuck << item.place;
                    continue;
                }
            }
            if (item.movedOld) {
                if (h.rename(item.oldPath, item.dst))
                    item.movedOld = false;
                else
                    stuck << item.oldPath;
            }
        }
        return stuck;
    };
    auto abandon = [&](const QString& message) {
        const QStringList stuck = undo();
        if (stuck.isEmpty()) {
            dropNew();
            return fail(message + QLatin1Char(' ') + QCoreApplication::translate("Zero::Sandbox", "Your instance was not changed."));
        }
        qWarning() << "ZERO test copy: could not put back" << stuck;
        return fail(message + QLatin1Char(' ') + QCoreApplication::translate("Zero::Sandbox", "Some files could not be put back, they are in %1").arg(QDir::toNativeSeparators(staging)), false);
    };
    for (int i = 0; i < items.size(); ++i) {
        Item& item = items[i];
        started << i;
        if (item.dstExists) {
            QDir().mkpath(QFileInfo(item.oldPath).absolutePath());
            if (!h.rename(item.dst, item.oldPath))
                return abandon(QCoreApplication::translate("Zero::Sandbox", "Could not replace %1. Is the game or another program using it?").arg(item.name));
            item.movedOld = true;
        }
        if (item.srcExists) {
            QDir().mkpath(QFileInfo(item.place).absolutePath());
            if (!h.rename(item.newPath, item.place))
                return abandon(QCoreApplication::translate("Zero::Sandbox", "Could not put %1 in place.").arg(item.name));
            item.placedNew = true;
        }
    }

    // 5. done: the replaced originals go to the Recycle Bin
    QSet<int> trashedGroups;
    for (const Item& item : std::as_const(items)) {
        if (!item.movedOld)
            continue;
        QString path = item.oldPath;
        if (item.group >= 0) {  // the replaced entries of a kept folder go as one folder named like it
            if (trashedGroups.contains(item.group))
                continue;
            trashedGroups.insert(item.group);
            path = groups.at(item.group).oldRoot;
        }
        if (!h.trashPath(path)) {
            result.leftovers << path;
            qWarning() << "ZERO test copy: could not move to the Recycle Bin, left in" << path;
        }
    }
    removeEmptyTree(staging);  // stays when something is left in it

    for (const Item& item : std::as_const(items)) {
        if (!result.appliedParts.contains(item.key))
            result.appliedParts << item.key;
        if (item.world)
            (item.dstExists ? result.replacedWorlds : result.addedWorlds) << item.name;
    }
    result.ok = true;
    result.restored = true;
    result.changedAnything = true;
    return result;
}

}  // namespace Sandbox
}  // namespace Zero
