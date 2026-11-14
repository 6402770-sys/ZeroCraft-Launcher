// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>
#include <QStringList>

namespace ZeroSetup {

/// Command line of the setup exe. "--name" and the NSIS style "/Name" (case-insensitive) both work, so an updater or
/// script written for the NSIS installer keeps working: /S (silent), /D=<dir> (install dir, must be last),
/// /NoShortcuts, /NoUninstaller, plus the NSIS uninstaller's "_?=<dir>" (run in place).
struct CmdLine
{
    bool silent = false;
    bool uninstall = false;
    bool noShortcuts = false;
    bool noUninstaller = false;
    bool shellAssoc = false;          // --shell-assoc: the optional "Open With" component
    bool noSystem = false;            // --no-system
    bool demo = false;                // --demo (fake slow install)
    bool demoFail = false;            // --demo-fail (the fake install fails half way)
    bool deleteUserData = false;      // --delete-data (uninstall)
    bool inPlace = false;             // _?=<dir>, or --no-relaunch: do not copy the uninstaller to %TEMP% first
    bool launchAfter = true;          // --no-launch
    QString target;                   // --target <dir> | /D=<dir>
    QString planFile;                 // --plan <file>
    QString screenshotScreen;         // --screenshot <screen> <png>
    QString screenshotPath;
    double scale = 1.0;               // --scale 1.5 (screenshots)
    QString stubDir;                  // --uninstall-from-temp <dir>: set by the relaunch (the real install dir)
    QStringList errors;               // unknown / malformed arguments
};

CmdLine parseCommandLine(const QStringList& args);  // args without argv[0]

}  // namespace ZeroSetup
