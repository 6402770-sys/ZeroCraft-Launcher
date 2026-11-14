// SPDX-License-Identifier: GPL-3.0-only
// Names that make up ZERO's system footprint. They mirror program_info/CMakeLists.txt (Launcher_CommonName,
// Launcher_DisplayName, Launcher_APP_BINARY_NAME) and program_info/win_install.nsi.in, which this installer replaces.
#pragma once

#include <QString>
#include <QStringList>

namespace ZeroSetup::Brand {

inline QString commonName() { return QStringLiteral("ZEROLauncher"); }        // registry / folder name
inline QString displayName() { return QStringLiteral("ZERO Launcher"); }      // shortcuts, Add/Remove Programs
inline QString appBase() { return QStringLiteral("zerolauncher"); }           // Launcher_APP_BINARY_NAME
inline QString appExe() { return appBase() + QStringLiteral(".exe"); }
inline QString uninstallerName() { return QStringLiteral("uninstall.exe"); }  // same file name the NSIS installer used
inline QString installManifestName() { return QStringLiteral(".zero-install.json"); }
inline QString publisher() { return displayName() + QStringLiteral(" Contributors"); }
inline QString appId() { return commonName() + QStringLiteral(".App"); }      // "ZEROLauncher.App" (Open-With ProgID)

// What the NSIS installer put next to the exe (used to upgrade / uninstall an install made by it).
inline QStringList nsisTopLevelFiles()
{
    return { appExe(), appBase() + QStringLiteral("_filelink.exe"), appBase() + QStringLiteral("_updater.exe"),
             QStringLiteral("qt.conf"), QStringLiteral("qtlogging.ini"), uninstallerName() };
}
inline QStringList nsisFolders()
{
    return { QStringLiteral("iconengines"), QStringLiteral("imageformats"), QStringLiteral("jars"), QStringLiteral("multimedia"),
             QStringLiteral("platforms"),   QStringLiteral("styles"),       QStringLiteral("tls") };
}

}  // namespace ZeroSetup::Brand
