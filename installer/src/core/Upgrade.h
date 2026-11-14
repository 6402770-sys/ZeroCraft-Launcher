// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Installer.h"
#include "System.h"

namespace ZeroSetup {

/// Where the installer proposes to install: the folder remembered in HKCU\Software\ZEROLauncher\InstallDir (like
/// NSIS InstallDirRegKey) when there is one, else the default. `localAppData` is %LOCALAPPDATA%.
QString proposedInstallDir(ISystem& sys, const QString& localAppData);

/// What is installed at `dir` (an install made by the NSIS installer, by this installer, or nothing).
/// V2 = the install manifest is present. Nsis = ZERO's exe is there without a manifest (version from the uninstall key).
ExistingInstall detectExisting(ISystem& sys, const QString& dir);

}  // namespace ZeroSetup
