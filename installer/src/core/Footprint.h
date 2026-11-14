// SPDX-License-Identifier: GPL-3.0-only
// ZERO's system footprint: the registry keys and shortcuts the NSIS installer (program_info/win_install.nsi.in) created,
// reproduced one to one, and their removal. See installer/README.md for the table.
#pragma once

#include "System.h"

#include <QString>
#include <QStringList>

namespace ZeroSetup {

struct FootprintOptions
{
    QString installDir;        // absolute, '/' or '\' separators
    QString version;           // "0.1.0"
    bool desktopShortcut = true;
    bool startMenuShortcut = true;
    bool writeUninstallEntry = true;  // NSIS: skipped by the /NoUninstaller switch
    bool shellAssociation = false;    // NSIS: optional component "Shell Association (Open-With dialog)", off by default
    quint64 estimatedSizeBytes = 0;
};

/// "C:\Users\x\AppData\Local\Programs\ZEROLauncher" (NSIS InstallDir "$LOCALAPPDATA\Programs\<CommonName>").
QString defaultInstallDir(const QString& localAppData);

/// The command the URL handlers hold: "<dir>\zerolauncher.exe" "%1".
QString urlHandlerCommand(const QString& installDir);

// Registry keys (below HKCU).
QString regAppKey();        // Software\ZEROLauncher
QString regUninstallKey();  // Software\Microsoft\Windows\CurrentVersion\Uninstall\ZEROLauncher

/// Registry + shortcuts for one install. Individual failures are collected in `warnings` and do not stop the rest
/// (the NSIS script behaved the same way). Returns true when nothing failed.
bool applyFootprint(ISystem& sys, const FootprintOptions& o, QStringList* warnings);

/// Reverse of applyFootprint. The curseforge:// and modrinth:// handlers are removed only while they still point at
/// installDir's exe (they are shared with other launchers). Returns true when nothing failed.
bool removeFootprint(ISystem& sys, const QString& installDir, QStringList* warnings);

/// The "Open With" part on its own (used by applyFootprint and removeFootprint).
void setupShellAssociation(ISystem& sys, const QString& installDir);
void teardownShellAssociation(ISystem& sys, const QString& installDir);

}  // namespace ZeroSetup
