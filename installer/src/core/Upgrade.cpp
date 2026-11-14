// SPDX-License-Identifier: GPL-3.0-only
#include "Upgrade.h"

#include "Brand.h"
#include "Footprint.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

namespace ZeroSetup {

QString proposedInstallDir(ISystem& sys, const QString& localAppData)
{
    const auto reg = sys.regGetString(regAppKey(), QStringLiteral("InstallDir"));
    if (reg && !reg->trimmed().isEmpty() && !QFileInfo(*reg).isRelative())
        return QDir::cleanPath(*reg);
    return defaultInstallDir(localAppData);
}

ExistingInstall detectExisting(ISystem& sys, const QString& dir)
{
    ExistingInstall e;
    const QString d = QDir::cleanPath(dir);
    if (d.isEmpty() || !QFileInfo(d).isDir())
        return e;
    e.dir = d;

    QString version;
    QStringList files;
    if (readInstallManifest(d, &version, &files)) {
        e.kind = InstallKind::V2;
        e.version = version;
        e.ownedFiles = files;
        return e;
    }
    if (!QFileInfo(d + '/' + Brand::appExe()).isFile())
        return {};

    e.kind = InstallKind::Nsis;
    // the version comes from the uninstall entry, when it belongs to this folder
    const QString k = regUninstallKey();
    const auto loc = sys.regGetString(k, QStringLiteral("InstallLocation"));
    if (!loc || QDir::cleanPath(*loc).compare(d, Qt::CaseInsensitive) == 0) {
        e.version = sys.regGetString(k, QStringLiteral("DisplayVersion")).value_or(QString());
    }
    // the layout the NSIS installer wrote (exe files, qt.conf, dlls, plugin / jar folders)
    for (const QString& f : Brand::nsisTopLevelFiles())
        if (QFileInfo(d + '/' + f).isFile())
            e.ownedFiles << f;
    for (const QFileInfo& fi : QDir(d).entryInfoList({ QStringLiteral("*.dll") }, QDir::Files))
        e.ownedFiles << fi.fileName();
    for (const QString& folder : Brand::nsisFolders()) {
        if (!QFileInfo(d + '/' + folder).isDir())
            continue;
        QDirIterator it(d + '/' + folder, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            e.ownedFiles << QDir(d).relativeFilePath(it.filePath());
        }
    }
    e.ownedFiles.removeDuplicates();
    return e;
}

}  // namespace ZeroSetup
