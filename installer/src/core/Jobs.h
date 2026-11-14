// SPDX-License-Identifier: GPL-3.0-only
// The whole install / uninstall as one call, used by the window (worker thread), by silent runs and by the tests.
#pragma once

#include "Installer.h"
#include "Payload.h"

#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>

namespace ZeroSetup {

struct JobResult
{
    bool ok = false;
    bool cancelled = false;
    QString error;            // one sentence
    QString stateNote;        // what state the computer is in ("Nothing was changed.")
    QStringList warnings;     // non-fatal (a shortcut that could not be made ...)
    QStringList stillThere;   // uninstall: files that go away at the next restart
    QStringList plan;         // --no-system: what the real run would have done
};

/// step text ("Copying files" ...) and overall fraction 0..1.
using JobProgress = std::function<void(const QString& step, double fraction)>;

struct InstallJob
{
    PayloadReader* payload = nullptr;
    QString exePath;               // the running setup exe (its first stubSize bytes become the uninstaller)
    QString targetDir;
    bool desktopShortcut = true;
    bool startMenuShortcut = true;
    bool shellAssoc = false;
    bool writeUninstaller = true;  // false: /NoUninstaller
    bool noSystem = false;         // --no-system: registry / shortcuts go to a recording fake and into the plan
    QString planFile;              // written when noSystem (or whenever non-empty)
    bool demo = false;             // fake slow install, no files written
    bool demoFail = false;
};

JobResult runInstallJob(const InstallJob& job, const std::atomic<bool>* cancel, const JobProgress& progress);

struct UninstallJob
{
    QString installDir;
    bool deleteUserData = false;
    QString userDataDir;
    bool noSystem = false;
    QString planFile;
    bool demo = false;
};

JobResult runUninstallJob(const UninstallJob& job, const std::atomic<bool>* cancel, const JobProgress& progress);

/// %APPDATA% / %LOCALAPPDATA% with forward slashes (empty when unset).
QString roamingAppDataDir();
QString localAppDataDir();

/// The exes of an install that Windows refuses to overwrite while ZERO runs. Empty when nothing is locked.
QStringList lockedProgramFiles(const QString& installDir);

}  // namespace ZeroSetup
