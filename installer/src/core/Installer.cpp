// SPDX-License-Identifier: GPL-3.0-only
#include "Installer.h"

#include "Brand.h"
#include "SafePath.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStorageInfo>

namespace ZeroSetup {

QString stagingDirFor(const QString& targetDir)
{
    return QDir::cleanPath(targetDir) + QStringLiteral(".zero-setup-tmp");
}

bool isFileLocked(const QString& path)
{
    if (!QFileInfo::exists(path))
        return false;
    QFile f(path);
    if (f.open(QIODevice::ReadWrite)) {
        f.close();
        return false;
    }
    return true;
}

bool readInstallManifest(const QString& dir, QString* version, QStringList* files)
{
    QFile f(QDir(dir).filePath(Brand::installManifestName()));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.isEmpty())
        return false;
    if (version)
        *version = o.value("version").toString();
    if (files) {
        files->clear();
        for (const QJsonValue& v : o.value("files").toArray()) {
            const QString p = v.toString();
            if (isSafeRelativePath(p))  // never trust a manifest to name paths outside the folder
                files->append(p);
        }
    }
    return true;
}

namespace {

struct Journal
{
    enum Kind { Moved, Placed, MadeDir };
    struct Op
    {
        Kind kind;
        QString a;  // Moved: original path; Placed: new path; MadeDir: path
        QString b;  // Moved: where the original went
    };
    QVector<Op> ops;
};

QString uniqueBackupDir(const QString& target)
{
    const QString base = QDir::cleanPath(target) + QStringLiteral(".zero-setup-old");
    if (!QFileInfo::exists(base))
        return base;
    for (int i = 2; i < 1000; ++i) {
        const QString c = base + QString::number(i);
        if (!QFileInfo::exists(c))
            return c;
    }
    return base;
}

bool ensureDir(const QString& dir, Journal& j)
{
    // creates dir one level at a time so each new level can be undone
    QStringList todo;
    QString cur = QDir::cleanPath(dir);
    while (!QFileInfo::exists(cur)) {
        todo.prepend(cur);
        const QString up = QFileInfo(cur).absolutePath();
        if (up == cur)
            break;
        cur = up;
    }
    if (QFileInfo::exists(cur) && !QFileInfo(cur).isDir())
        return false;
    for (const QString& d : std::as_const(todo)) {
        if (!QDir().mkdir(d))
            return false;
        j.ops.push_back({ Journal::MadeDir, d, {} });
    }
    return QFileInfo(dir).isDir();
}

void rollback(Journal& j)
{
    for (int i = int(j.ops.size()) - 1; i >= 0; --i) {
        const auto& op = j.ops[i];
        switch (op.kind) {
            case Journal::Placed:
                QFile::remove(op.a);
                break;
            case Journal::Moved:
                QFile::remove(op.a);
                QFile::rename(op.b, op.a);
                break;
            case Journal::MadeDir:
                QDir().rmdir(op.a);  // only succeeds when empty
                break;
        }
    }
}

bool moveAside(const QString& file, const QString& relFromRoot, const QString& backupDir, Journal& j)
{
    const QString dst = QDir::cleanPath(backupDir + '/' + relFromRoot);
    if (!QDir().mkpath(QFileInfo(dst).absolutePath()))
        return false;
    QFile::remove(dst);
    if (!QFile::rename(file, dst))
        return false;
    j.ops.push_back({ Journal::Moved, file, dst });
    return true;
}

QStringList listFilesRelative(const QString& root)
{
    QStringList out;
    const QDir d(root);
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        out << d.relativeFilePath(it.filePath());
    }
    out.sort();
    return out;
}

InstallResult fail(const QString& error, bool previousExisted, InstallStatus st = InstallStatus::Failed)
{
    InstallResult r;
    r.status = st;
    r.error = error;
    r.stateNote = previousExisted ? QStringLiteral("Your previous version is still installed.") : QStringLiteral("Nothing was changed.");
    return r;
}

}  // namespace

InstallResult installFiles(PayloadReader& payload, const InstallRequest& req, const std::atomic<bool>* cancel, const StepProgress& progress)
{
    const QString target = QDir::cleanPath(req.targetDir);
    const bool hadPrev = req.existing.kind != InstallKind::None;
    const PayloadInfo& info = payload.info();
    const QString step = QStringLiteral("Copying files");
    auto isCancelled = [&] { return cancel && cancel->load(); };
    auto report = [&](quint64 done) {
        if (progress)
            progress(step, done, info.totalSize);
    };

    if (target.isEmpty() || QFileInfo(target).isRelative())
        return fail(QStringLiteral("The install folder is not valid."), hadPrev);
    if (QFileInfo::exists(target) && !QFileInfo(target).isDir())
        return fail(QStringLiteral("The install folder is a file."), hadPrev);

    report(0);
    // 1. verify the payload as a whole before writing anything
    if (req.verifyPayloadHash) {
        QString err;
        if (!payload.verifyHash(cancel, &err))
            return isCancelled() ? fail(QStringLiteral("Cancelled."), hadPrev, InstallStatus::Cancelled)
                                 : fail(QStringLiteral("The installer file is damaged: ") + err, hadPrev);
    }

    // 2. space check (the parent folder must be writable too: the staging folder is its sibling)
    const QString parent = QFileInfo(target).absolutePath();
    if (!QDir().mkpath(parent))
        return fail(QStringLiteral("Cannot create the folder ") + QDir::toNativeSeparators(parent) + '.', hadPrev);
    {
        const QStorageInfo si(parent);
        const qint64 need = qint64(double(info.totalSize) * 1.05) + (64 << 20);
        if (si.isValid() && si.bytesAvailable() >= 0 && si.bytesAvailable() < need)
            return fail(QStringLiteral("Not enough free disk space (about %1 MB needed).").arg(need >> 20), hadPrev);
    }

    // 3. extract into the staging folder
    const QString staging = stagingDirFor(target);
    QDir(staging).removeRecursively();  // leftovers of an earlier, interrupted run (this name is ours)
    if (!QDir().mkpath(staging))
        return fail(QStringLiteral("Cannot create a working folder next to the install folder."), hadPrev);
    auto cleanup = [&] { QDir(staging).removeRecursively(); };

    QString err;
    bool cancelledFlag = false;
    if (!payload.extractAll(staging, cancel, [&](quint64 done) { report(done); }, &err, &cancelledFlag)) {
        cleanup();
        return cancelledFlag ? fail(QStringLiteral("Cancelled."), hadPrev, InstallStatus::Cancelled) : fail(err, hadPrev);
    }
    if (isCancelled()) {
        cleanup();
        return fail(QStringLiteral("Cancelled."), hadPrev, InstallStatus::Cancelled);
    }

    // 4. uninstaller image + install manifest, staged like every other file
    if (!req.uninstallerSource.isEmpty()) {
        QFile src(req.uninstallerSource);
        QFile dst(staging + '/' + Brand::uninstallerName());
        if (!src.open(QIODevice::ReadOnly) || !dst.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            cleanup();
            return fail(QStringLiteral("Cannot prepare the uninstaller."), hadPrev);
        }
        quint64 left = req.uninstallerBytes ? req.uninstallerBytes : quint64(src.size());
        while (left > 0) {
            const QByteArray b = src.read(qint64(qMin<quint64>(left, 4 << 20)));
            if (b.isEmpty() || dst.write(b) != b.size()) {
                cleanup();
                return fail(QStringLiteral("Cannot prepare the uninstaller."), hadPrev);
            }
            left -= quint64(b.size());
        }
    }
    QStringList owned;
    for (const auto& e : info.files)
        owned << e.path;
    if (!req.uninstallerSource.isEmpty())
        owned << Brand::uninstallerName();
    owned << Brand::installManifestName();
    {
        QJsonArray arr;
        for (const QString& p : std::as_const(owned))
            arr.append(p);
        QJsonObject o;
        o["version"] = req.version.isEmpty() ? info.version : req.version;
        o["files"] = arr;
        QFile m(staging + '/' + Brand::installManifestName());
        if (!m.open(QIODevice::WriteOnly | QIODevice::Truncate) || m.write(QJsonDocument(o).toJson(QJsonDocument::Indented)) < 0) {
            cleanup();
            return fail(QStringLiteral("Cannot write the install manifest."), hadPrev);
        }
    }

    // 5. nothing past this point can be cancelled; every step is a rename recorded in a journal
    if (progress)
        progress(QStringLiteral("Copying files"), info.totalSize, info.totalSize);
    const QString backup = uniqueBackupDir(target);
    Journal j;
    const QStringList staged = listFilesRelative(staging);
    QString commitError;
    bool ok = ensureDir(target, j);
    if (ok) {
        // remember directories we create below the (maybe new) target
        for (const QString& rel : staged) {
            const QString dst = QDir::cleanPath(target + '/' + rel);
            if (!staysInside(target, rel)) {
                ok = false;
                commitError = QStringLiteral("A path points outside the install folder.");
                break;
            }
            if (!ensureDir(QFileInfo(dst).absolutePath(), j)) {
                ok = false;
                commitError = QStringLiteral("Cannot create a folder for ") + rel + '.';
                break;
            }
            if (QFileInfo(dst).isDir()) {
                ok = false;
                commitError = QStringLiteral("A folder is in the way of ") + rel + '.';
                break;
            }
            if (QFileInfo::exists(dst) && !moveAside(dst, rel, backup, j)) {
                ok = false;
                commitError = QStringLiteral("Cannot replace %1 (is ZERO still running?).").arg(rel);
                break;
            }
            if (!QFile::rename(staging + '/' + rel, dst)) {
                ok = false;
                commitError = QStringLiteral("Cannot write ") + rel + '.';
                break;
            }
            j.ops.push_back({ Journal::Placed, dst, {} });
        }
    } else {
        commitError = QStringLiteral("Cannot create the install folder.");
    }
    if (ok) {
        // files of the previous install that the new one does not have (renamed away, so a rollback can bring them back)
        QSet<QString> now;
        for (const QString& p : std::as_const(owned))
            now.insert(p.toLower());
        for (const QString& old : req.existing.ownedFiles) {
            if (now.contains(old.toLower()) || !isSafeRelativePath(old))
                continue;
            const QString path = QDir::cleanPath(target + '/' + old);
            if (QFileInfo(path).isFile() && !moveAside(path, old, backup, j)) {
                ok = false;
                commitError = QStringLiteral("Cannot remove the old file ") + old + '.';
                break;
            }
        }
    }
    if (!ok) {
        rollback(j);
        cleanup();
        QDir(backup).removeRecursively();
        return fail(commitError, hadPrev);
    }

    // 6. success: drop the backup and the staging folder, tidy empty folders the old version left
    QDir(backup).removeRecursively();
    cleanup();
    for (const QString& old : req.existing.ownedFiles) {
        QString d = QFileInfo(QDir::cleanPath(target + '/' + old)).absolutePath();
        while (d.size() > target.size() && QDir().rmdir(d))
            d = QFileInfo(d).absolutePath();
    }
    if (progress)
        progress(QStringLiteral("Copying files"), info.totalSize, info.totalSize);
    InstallResult r;
    r.installedFiles = owned;
    return r;
}

}  // namespace ZeroSetup
