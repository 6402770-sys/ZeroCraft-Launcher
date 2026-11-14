// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "System.h"

#include <QString>
#include <QStringList>
#include <functional>

namespace ZeroSetup {

struct UninstallRequest
{
    QString installDir;          // absolute
    bool deleteUserData = false; // "Also move my worlds and settings to the Recycle Bin": OFF by default
    QString userDataDir;         // %APPDATA%\ZEROLauncher; only used when deleteUserData
    std::function<void(double)> onProgress;  // 0..1, optional
};

struct UninstallResult
{
    bool ok = true;                 // false: something could not be removed
    QStringList stillThere;         // files scheduled for deletion at next restart (or left)
    QStringList warnings;
    bool removedUserData = false;   // moved to the Recycle Bin
    bool userDataFailed = false;    // asked for, refused: NOTHING else was removed
    QString error;                  // when refused (e.g. unsafe user data path)
};

/// %APPDATA%\ZEROLauncher: the folder Application.cpp uses for an installed (non-portable) ZERO.
QString userDataDirFor(const QString& roamingAppData);

/// True when `dir` is acceptable as the folder to wipe: its last segment is ZEROLauncher, it is not a drive root,
/// it is not (or inside, or around) the install folder, and it is not the Prism data folder.
bool isSafeUserDataDir(const QString& dir, const QString& installDir);

/// Removes the install: registry / shortcuts first, then the files this install owns (the install manifest; an
/// install made by NSIS has none, so the NSIS layout is removed instead), empty folders, the install folder itself
/// when empty. Never touches the data folder unless asked. Files that cannot be deleted are scheduled for deletion
/// at the next restart through `sys`.
UninstallResult uninstallFiles(ISystem& sys, const UninstallRequest& req);

}  // namespace ZeroSetup
