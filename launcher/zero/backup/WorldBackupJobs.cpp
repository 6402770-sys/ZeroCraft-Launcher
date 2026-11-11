// SPDX-License-Identifier: GPL-3.0-only
#include "WorldBackupJobs.h"

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

QString WorldBackupJobs::backupDir(const Worlds::WorldEntry& entry) const
{
    return QDir(m_root).filePath(entry.instanceId + QLatin1Char('/') + entry.folderName);
}

bool WorldBackupJobs::ownsBackup(const Worlds::WorldEntry& entry, const QString& zipPath) const
{
    // only this world's own backups may be restored over it / deleted through it
    return QDir::cleanPath(QFileInfo(zipPath).absolutePath()).compare(QDir::cleanPath(QFileInfo(backupDir(entry)).absoluteFilePath()),
                                                                      Qt::CaseInsensitive) == 0;
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

void WorldBackupJobs::finish(const QString& key, const QString& instanceId, Op op, bool ok, const QString& message)
{
    if (m_active.remove(key) == 0)
        return;
    m_activeInst.remove(key);
    const bool automatic = m_activeAuto.remove(key);
    unlock(instanceId);
    emit jobFinished(key, ok, message, automatic);
    if (ok && op == Op::Restore)
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
    const QString dir = backupDir(entry);
    const QString folder = entry.folderPath;
    const BackupMeta meta = metaOf(entry);
    const Hooks hooks = m_hooks;

    // The pool thread never touches `this` or reads the QPointer: it posts to the application object and the pointer is checked
    // there, on the GUI thread.
    QPointer<WorldBackupJobs> self(this);
    QThreadPool::globalInstance()->start([self, key, instanceId, dir, folder, meta, hooks, kind]() {
        const BackupResult r = createBackup(folder, dir, kind, meta, hooks);
        if (r.ok)
            applyRetention(dir, { r.zipPath });
        auto* app = QCoreApplication::instance();
        if (!app)
            return;
        QMetaObject::invokeMethod(
            app,
            [self, key, instanceId, r]() {
                if (self)
                    self->finish(key, instanceId, Op::Backup, r.ok, r.ok ? tr("Backup created") : r.error);
            },
            Qt::QueuedConnection);
    });
    return true;
}

bool WorldBackupJobs::restore(const Worlds::WorldEntry& entry, const QString& zipPath)
{
    const QString key = entry.key();
    if (!ownsBackup(entry, zipPath))
        return refuse(key, tr("This backup does not belong to this world."));
    if (!begin(entry, Op::Restore))
        return false;
    const QString instanceId = entry.instanceId;
    const QString dir = backupDir(entry);
    const QString folder = entry.folderPath;
    const BackupMeta meta = metaOf(entry);
    const Hooks hooks = m_hooks;

    QPointer<WorldBackupJobs> self(this);
    QThreadPool::globalInstance()->start([self, key, instanceId, dir, folder, meta, hooks, zipPath]() {
        const RestoreResult r = restoreBackup(zipPath, folder, dir, meta, hooks);
        if (r.ok)
            applyRetention(dir, { r.safetyZip, zipPath });
        auto* app = QCoreApplication::instance();
        if (!app)
            return;
        QMetaObject::invokeMethod(
            app,
            [self, key, instanceId, r]() {
                if (self)
                    self->finish(key, instanceId, Op::Restore, r.ok, r.ok ? tr("World restored") : r.error);
            },
            Qt::QueuedConnection);
    });
    return true;
}

bool WorldBackupJobs::remove(const Worlds::WorldEntry& entry, const QString& zipPath)
{
    const QString key = entry.key();
    if (!ownsBackup(entry, zipPath))
        return refuse(key, tr("This backup does not belong to this world."));
    if (!begin(entry, Op::Remove))
        return false;
    const QString instanceId = entry.instanceId;
    const QString root = m_root;
    const Hooks hooks = m_hooks;

    QPointer<WorldBackupJobs> self(this);
    QThreadPool::globalInstance()->start([self, key, instanceId, root, zipPath, hooks]() {
        QString error;
        const bool ok = deleteBackup(zipPath, root, &error, hooks);
        auto* app = QCoreApplication::instance();
        if (!app)
            return;
        QMetaObject::invokeMethod(
            app,
            [self, key, instanceId, ok, error]() {
                if (self)
                    self->finish(key, instanceId, Op::Remove, ok, ok ? tr("Backup deleted") : error);
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
    const QString dir = backupDir(entry);

    QPointer<WorldBackupJobs> self(this);
    QThreadPool::globalInstance()->start([self, key, dir, generation]() {
        const QList<BackupInfo> list = listBackups(dir);
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
