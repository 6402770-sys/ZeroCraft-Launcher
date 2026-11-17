// SPDX-License-Identifier: GPL-3.0-only
#include "WorldBackupJobs.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>

namespace Zero {
namespace Backup {

namespace {
WorldBackupJobs* g_global = nullptr;

QHash<QString, int>& lockedInstances()  // instance id -> number of active jobs; GUI thread only
{
    static QHash<QString, int> locks;
    return locks;
}

void unlock(const QString& instanceId)
{
    auto& locks = lockedInstances();
    if (--locks[instanceId] <= 0)
        locks.remove(instanceId);
}

BackupMeta metaOf(const Worlds::WorldEntry& e)
{
    return { e.instanceId, e.instanceName, e.folderName, e.name };
}
}  // namespace

bool WorldBackupJobs::isInstanceLocked(const QString& instanceId)
{
    return lockedInstances().value(instanceId, 0) > 0;
}

void WorldBackupJobs::lockInstance(const QString& instanceId)
{
    ++lockedInstances()[instanceId];
}

void WorldBackupJobs::unlockInstance(const QString& instanceId)
{
    unlock(instanceId);
}

WorldBackupJobs* WorldBackupJobs::global()
{
    return g_global;
}

WorldBackupJobs::WorldBackupJobs(QString backupsRootDir, RunningCheck isRunning, QObject* parent)
    : QObject(parent), m_root(std::move(backupsRootDir)), m_isRunning(std::move(isRunning))
{
    if (!g_global)
        g_global = this;
}

WorldBackupJobs::~WorldBackupJobs()
{
    if (g_global == this)
        g_global = nullptr;
    // jobs still running on the pool can no longer report back: release their launch locks
    for (auto it = m_activeInst.cbegin(); it != m_activeInst.cend(); ++it)
        unlock(it.value());
}

QString WorldBackupJobs::activeRoot() const
{
    return Zero::Backup::activeRoot(m_root, chosenFolder());
}

QStringList WorldBackupJobs::allRoots() const
{
    return mergeRoots(m_root, m_dest.knownRoots ? m_dest.knownRoots() : QStringList(), activeRoot());
}

QStringList WorldBackupJobs::backupDirs(const Worlds::WorldEntry& entry) const
{
    QStringList dirs;
    for (const QString& root : allRoots())
        dirs << worldBackupDirIn(root, entry.instanceId, entry.folderName);
    return dirs;
}

void WorldBackupJobs::rememberRoot(const QString& root)
{
    if (m_dest.rootUsed && !root.isEmpty())
        m_dest.rootUsed(root);
}

QString WorldBackupJobs::backupDir(const Worlds::WorldEntry& entry) const
{
    const QString active = worldBackupDirIn(activeRoot(), entry.instanceId, entry.folderName);
    if (QFileInfo::exists(active))
        return active;
    for (const QString& dir : backupDirs(entry)) {
        if (QFileInfo::exists(dir))
            return dir;
    }
    return active;
}

QString WorldBackupJobs::rootOwning(const Worlds::WorldEntry& entry, const QString& zipPath) const
{
    // only this world's own backups, in a root ZERO has used, may be restored over it / deleted through it
    const QString zipDir = QDir::cleanPath(QFileInfo(zipPath).absolutePath());
    for (const QString& root : allRoots()) {
        const QString dir = QDir::cleanPath(QFileInfo(worldBackupDirIn(root, entry.instanceId, entry.folderName)).absoluteFilePath());
        if (zipDir.compare(dir, Qt::CaseInsensitive) == 0)
            return root;
    }
    return {};
}

bool WorldBackupJobs::refuse(const QString& key, const QString& reason, bool automatic)
{
    emit jobFinished(key, false, reason, automatic);
    return false;
}

bool WorldBackupJobs::begin(const Worlds::WorldEntry& entry, Op op, bool automatic)
{
    const QString key = entry.key();
    if (entry.folderPath.isEmpty() || entry.instanceId.isEmpty() || entry.folderName.isEmpty())
        return refuse(key, tr("This world cannot be backed up."), automatic);
    if (m_active.contains(key))
        return refuse(key, tr("Another backup action for this world is still running."), automatic);
    if (m_isRunning && m_isRunning(entry.instanceId))
        return refuse(key, tr("Close the game to back up or restore this world."), automatic);
    m_active.insert(key, op);
    m_activeInst.insert(key, entry.instanceId);
    if (automatic)
        m_activeAuto.insert(key);
    ++lockedInstances()[entry.instanceId];
    emit jobStarted(key);
    return true;
}

void WorldBackupJobs::finish(const QString& key, const QString& instanceId, Op op, const Outcome& outcome)
{
    if (m_active.remove(key) == 0)
        return;
    m_activeInst.remove(key);
    const bool automatic = m_activeAuto.remove(key);
    unlock(instanceId);
    QString message = outcome.message;
    if (outcome.ok && outcome.fellBackFrom.isEmpty() && !outcome.usedRoot.isEmpty() &&
        QDir::cleanPath(outcome.usedRoot).compare(QDir::cleanPath(m_root), Qt::CaseInsensitive) != 0)
        rememberRoot(outcome.usedRoot);  // the default root is always known
    if (outcome.ok && !outcome.fellBackFrom.isEmpty()) {
        message += QStringLiteral(". ") + fallbackMessage(outcome.fellBackFrom);
        emit fellBackToDefault(key, outcome.fellBackFrom);
    }
    emit jobFinished(key, outcome.ok, message, automatic);
    if (outcome.ok && op == Op::Restore)
        emit worldRestored(key);
    emit backupsChanged(key);
}

bool WorldBackupJobs::backup(const Worlds::WorldEntry& entry, Kind kind)
{
    if (kind == Kind::Safety)
        return refuse(entry.key(), tr("This world cannot be backed up."));
    if (!begin(entry, Op::Backup, kind == Kind::Auto))
        return false;
    const QString key = entry.key();
    const QString instanceId = entry.instanceId;
    const QString defaultRoot = m_root;
    const QString chosen = chosenFolder();  // read now, on the GUI thread
    const QString folderName = entry.folderName;
    const QString folder = entry.folderPath;
    const BackupMeta meta = metaOf(entry);
    const Hooks hooks = m_hooks;

    // The pool thread never touches `this` or reads the QPointer: it posts to the application object and the pointer is checked
    // there, on the GUI thread. The destination is probed on the pool too (a cloud or network folder can be slow).
    QPointer<WorldBackupJobs> self(this);
    QThreadPool::globalInstance()->start([self, key, instanceId, defaultRoot, chosen, folderName, folder, meta, hooks, kind]() {
        const Resolved target = resolveRoot(defaultRoot, chosen);
        const QString dir = worldBackupDirIn(target.root, instanceId, folderName);
        const BackupResult r = createBackup(folder, dir, kind, meta, hooks);
        if (r.ok)
            applyRetention(dir, { r.zipPath });
        Outcome outcome;
        outcome.ok = r.ok;
        outcome.message = r.ok ? tr("Backup created") : r.error;
        outcome.usedRoot = target.root;
        outcome.fellBackFrom = target.fellBack ? target.chosenFolder : QString();
        auto* app = QCoreApplication::instance();
        if (!app)
            return;
        QMetaObject::invokeMethod(
            app,
            [self, key, instanceId, outcome]() {
                if (self)
                    self->finish(key, instanceId, Op::Backup, outcome);
            },
            Qt::QueuedConnection);
    });
    return true;
}

bool WorldBackupJobs::restore(const Worlds::WorldEntry& entry, const QString& zipPath)
{
    const QString key = entry.key();
    if (rootOwning(entry, zipPath).isEmpty())
        return refuse(key, tr("This backup does not belong to this world."));
    if (!begin(entry, Op::Restore))
        return false;
    const QString instanceId = entry.instanceId;
    const QString defaultRoot = m_root;
    const QString chosen = chosenFolder();
    const QString folderName = entry.folderName;
    const QString folder = entry.folderPath;
    const BackupMeta meta = metaOf(entry);
    const Hooks hooks = m_hooks;

    // The backup may come from any known root; its safety backup (and the retention) belong to the destination in use.
    QPointer<WorldBackupJobs> self(this);
    QThreadPool::globalInstance()->start([self, key, instanceId, defaultRoot, chosen, folderName, folder, meta, hooks, zipPath]() {
        const Resolved target = resolveRoot(defaultRoot, chosen);
        const QString dir = worldBackupDirIn(target.root, instanceId, folderName);
        const RestoreResult r = restoreBackup(zipPath, folder, dir, meta, hooks);
        if (r.ok)
            applyRetention(dir, { r.safetyZip, zipPath });
        Outcome outcome;
        outcome.ok = r.ok;
        outcome.message = r.ok ? tr("World restored") : r.error;
        outcome.usedRoot = target.root;
        outcome.fellBackFrom = target.fellBack ? target.chosenFolder : QString();
        auto* app = QCoreApplication::instance();
        if (!app)
            return;
        QMetaObject::invokeMethod(
            app,
            [self, key, instanceId, outcome]() {
                if (self)
                    self->finish(key, instanceId, Op::Restore, outcome);
            },
            Qt::QueuedConnection);
    });
    return true;
}

bool WorldBackupJobs::remove(const Worlds::WorldEntry& entry, const QString& zipPath)
{
    const QString key = entry.key();
    const QString root = rootOwning(entry, zipPath);
    if (root.isEmpty())
        return refuse(key, tr("This backup does not belong to this world."));
    if (!begin(entry, Op::Remove))
        return false;
    const QString instanceId = entry.instanceId;
    const Hooks hooks = m_hooks;

    QPointer<WorldBackupJobs> self(this);
    QThreadPool::globalInstance()->start([self, key, instanceId, root, zipPath, hooks]() {
        QString error;
        const bool ok = deleteBackup(zipPath, root, &error, hooks);
        Outcome outcome;
        outcome.ok = ok;
        outcome.message = ok ? tr("Backup deleted") : error;
        auto* app = QCoreApplication::instance();
        if (!app)
            return;
        QMetaObject::invokeMethod(
            app,
            [self, key, instanceId, outcome]() {
                if (self)
                    self->finish(key, instanceId, Op::Remove, outcome);
            },
            Qt::QueuedConnection);
    });
    return true;
}

void WorldBackupJobs::refreshList(const Worlds::WorldEntry& entry)
{
    const QString key = entry.key();
    const quint64 generation = ++m_nextGen;
    m_listGen.insert(key, generation);
    const QStringList dirs = backupDirs(entry);  // every root ever used: old backups stay visible after the destination changed

    QPointer<WorldBackupJobs> self(this);
    QThreadPool::globalInstance()->start([self, key, dirs, generation]() {
        QList<BackupInfo> list;
        for (const QString& dir : dirs)
            list.append(listBackups(dir));
        std::stable_sort(list.begin(), list.end(), [](const BackupInfo& a, const BackupInfo& b) { return a.createdMs > b.createdMs; });
        auto* app = QCoreApplication::instance();
        if (!app)
            return;
        QMetaObject::invokeMethod(
            app,
            [self, key, generation, list]() {
                if (self && self->m_listGen.value(key) == generation)
                    emit self->backupsListed(key, list);
            },
            Qt::QueuedConnection);
    });
}

}  // namespace Backup
}  // namespace Zero
