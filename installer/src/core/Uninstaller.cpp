// SPDX-License-Identifier: GPL-3.0-only
#include "Uninstaller.h"

#include "Brand.h"
#include "Footprint.h"
#include "Installer.h"
#include "SafePath.h"
#include "Upgrade.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

namespace ZeroSetup {

QString userDataDirFor(const QString& roamingAppData)
{
    return QDir::cleanPath(roamingAppData + '/' + Brand::commonName());
}

bool isSafeUserDataDir(const QString& dir, const QString& installDir)
{
    const QString d = QDir::cleanPath(dir);
    const QString inst = QDir::cleanPath(installDir);
    if (d.isEmpty() || QFileInfo(d).isRelative())
        return false;
    if (QFileInfo(d).fileName().compare(Brand::commonName(), Qt::CaseInsensitive) != 0)
        return false;
    if (d.contains(QStringLiteral("prism"), Qt::CaseInsensitive))
        return false;
    if (QFileInfo(d).absolutePath() == d)  // a drive root
        return false;
    const auto inside = [](const QString& a, const QString& b) {  // a is b or below b
        return a.compare(b, Qt::CaseInsensitive) == 0 || a.startsWith(b + '/', Qt::CaseInsensitive);
    };
    if (!inst.isEmpty() && (inside(inst, d) || inside(d, inst)))
        return false;
    return true;
}

UninstallResult uninstallFiles(ISystem& sys, const UninstallRequest& req)
{
    UninstallResult res;
    const QString dir = QDir::cleanPath(req.installDir);
    if (dir.isEmpty() || QFileInfo(dir).isRelative() || QFileInfo(dir).absolutePath() == dir) {
        res.ok = false;
        res.error = QStringLiteral("The install folder is not valid.");
        return res;
    }

    // 0. user data first, only when asked, and only into the Recycle Bin (never a permanent delete). If that fails nothing
    //    else is removed, so the worlds are certainly still where they were.
    if (req.deleteUserData) {
        const QString data = QDir::cleanPath(req.userDataDir);
        const QString still = QStringLiteral("Your worlds and settings were not removed; they are still in %1.").arg(QDir::toNativeSeparators(data));
        if (!isSafeUserDataDir(data, dir)) {
            res.ok = false;
            res.userDataFailed = true;
            res.error = still;
            return res;
        }
        if (QFileInfo(data).isDir()) {
            if (!sys.moveToRecycleBin(data)) {
                res.ok = false;
                res.userDataFailed = true;
                res.error = still;
                return res;
            }
            res.removedUserData = true;
        }
    }

    // what this install owns, read before anything is removed
    const ExistingInstall owned = detectExisting(sys, dir);

    // 1. registry and shortcuts
    removeFootprint(sys, dir, &res.warnings);

    // 2. files
    QStringList files = owned.ownedFiles;
    files.removeAll(Brand::installManifestName());
    files << Brand::installManifestName();  // last, so an interrupted uninstall can be run again
    QSet<QString> dirs;
    int index = 0;
    for (const QString& rel : std::as_const(files)) {
        if (req.onProgress)
            req.onProgress(0.1 + 0.85 * double(index++) / double(files.size()));
        if (!isSafeRelativePath(rel))
            continue;
        const QString path = QDir::cleanPath(dir + '/' + rel);
        if (!staysInside(dir, rel) || !QFileInfo(path).isFile())
            continue;
        if (!QFile::remove(path)) {
            QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);  // read-only files
            if (!QFile::remove(path)) {
                if (sys.scheduleDeleteOnReboot(path))
                    res.stillThere << path;
                else
                    res.ok = false;
                continue;
            }
        }
        for (QString p = QFileInfo(path).absolutePath(); p.size() > dir.size(); p = QFileInfo(p).absolutePath())
            dirs.insert(p);
    }
    // 3. empty folders (deepest first), then the install folder itself when nothing of anyone else is left
    QStringList sorted = dirs.values();
    std::sort(sorted.begin(), sorted.end(), [](const QString& a, const QString& b) { return a.size() > b.size(); });
    for (const QString& d : std::as_const(sorted))
        QDir().rmdir(d);
    QDir().rmdir(dir);

    return res;
}

}  // namespace ZeroSetup
