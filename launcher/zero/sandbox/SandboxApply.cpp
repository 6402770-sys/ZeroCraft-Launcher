// SPDX-License-Identifier: GPL-3.0-only
#include "SandboxApply.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimeZone>
#include <QtDebug>

#include "FileSystem.h"
#include "zero/backup/WorldBackup.h"

namespace Zero {
namespace Sandbox {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("Zero::Sandbox", text);
}

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

struct Item {
    bool world = false;
    QString key;   ///< partKey
    QString name;  ///< last path component (the name the Recycle Bin shows)
    QString src;   ///< in the copy
    QString dst;   ///< in the original
    bool srcExists = false;
    bool dstExists = false;
    QString newPath;  ///< staging/new/<n>
    QString oldPath;  ///< staging/old/<name>
    bool movedOld = false;
    bool placedNew = false;
};

QString uniqueUnder(const QString& dir, const QString& name)
{
    QString candidate = QDir(dir).filePath(name);
    for (int i = 2; pathExists(candidate); ++i)
        candidate = QDir(dir).filePath(QStringLiteral("%1-%2").arg(name).arg(i));
    return candidate;
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
            return tr("There is no place for the backups.");
        const Backup::BackupMeta meta{ instanceId, instanceName, worldFolder, worldFolder };
        const Backup::BackupResult made = Backup::createBackup(worldPath, dir, Backup::Kind::Safety, meta);
        if (!made.ok)
            return made.error.isEmpty() ? tr("The backup failed.") : made.error;
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
        return fail(tr("The instances to compare are not valid."));
    if (!QFileInfo(request.original.instanceRoot).isDir() || !QFileInfo(request.copy.instanceRoot).isDir())
        return fail(tr("The instance folder was not found."));

    // 1. the targets
    QList<Item> items;
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
                item.srcExists = true;
                item.dstExists = pathExists(item.dst);
                items.append(item);
            }
            continue;
        }
        for (const Target& t : targetsOf(part, request.original, request.copy)) {
            Item item;
            item.key = partKey(part);
            item.name = QFileInfo(t.original).fileName();
            item.src = t.copy;
            item.dst = t.original;
            item.srcExists = pathExists(t.copy);
            item.dstExists = pathExists(t.original);
            if ((!item.srcExists && !item.dstExists) || !treesDiffer(t.original, t.copy))
                continue;  // nothing to bring back
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
            return fail(tr("Could not save a backup of the world %1 first, so nothing was changed.").arg(item.name));
        const QString error = h.backupWorld(item.name, item.dst);
        if (!error.isEmpty())
            return fail(tr("Could not save a backup of the world %1 first, so nothing was changed: %2").arg(item.name, error));
    }

    // 3. staging: the copy's version of every target, next to the original
    const QString stamp = QDateTime::fromMSecsSinceEpoch(h.nowMs(), QTimeZone::utc()).toString(QStringLiteral("yyyyMMddHHmmsszzz"));
    const QString staging = QDir(request.original.instanceRoot).filePath(QStringLiteral(".zero-bringback-%1").arg(stamp));
    const QString newDir = QDir(staging).filePath(QStringLiteral("new"));
    const QString oldDir = QDir(staging).filePath(QStringLiteral("old"));
    if (pathExists(staging))
        return fail(tr("Bringing changes back is already in progress for this instance."));
    if (!QDir().mkpath(newDir) || !QDir().mkpath(oldDir)) {
        removeEmptyTree(staging);
        return fail(tr("Could not prepare the folder for the changes, so nothing was changed."));
    }
    auto dropNew = [&]() {
        QDir(newDir).removeRecursively();  // ZERO's own copies of the test copy's files
        removeEmptyTree(staging);
    };
    for (int i = 0; i < items.size(); ++i) {
        Item& item = items[i];
        item.oldPath = uniqueUnder(oldDir, item.name);
        if (!item.srcExists)
            continue;
        item.newPath = QDir(newDir).filePath(QString::number(i));
        if (!h.copyTree(item.src, item.newPath)) {
            dropNew();
            return fail(tr("Could not copy %1 from the test copy, so nothing was changed.").arg(item.name));
        }
    }

    // 4. the swap, every target by two renames; a failure puts the targets back in reverse order
    QList<int> started;
    auto undo = [&]() {
        QStringList stuck;
        for (auto it = started.crbegin(); it != started.crend(); ++it) {
            Item& item = items[*it];
            if (item.placedNew) {
                if (h.rename(item.dst, item.newPath))
                    item.placedNew = false;
                else {
                    stuck << item.dst;
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
            return fail(message + QLatin1Char(' ') + tr("Your instance was not changed."));
        }
        qWarning() << "ZERO test copy: could not put back" << stuck;
        return fail(message + QLatin1Char(' ') + tr("Some files could not be put back, they are in %1").arg(QDir::toNativeSeparators(staging)), false);
    };
    for (int i = 0; i < items.size(); ++i) {
        Item& item = items[i];
        started << i;
        if (item.dstExists) {
            if (!h.rename(item.dst, item.oldPath))
                return abandon(tr("Could not replace %1. Is the game or another program using it?").arg(item.name));
            item.movedOld = true;
        }
        if (item.srcExists) {
            QDir().mkpath(QFileInfo(item.dst).absolutePath());
            if (!h.rename(item.newPath, item.dst))
                return abandon(tr("Could not put %1 in place.").arg(item.name));
            item.placedNew = true;
        }
    }

    // 5. done: the replaced originals go to the Recycle Bin
    for (const Item& item : std::as_const(items)) {
        if (!item.movedOld)
            continue;
        if (!h.trashPath(item.oldPath)) {
            result.leftovers << item.oldPath;
            qWarning() << "ZERO test copy: could not move to the Recycle Bin, left in" << item.oldPath;
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
