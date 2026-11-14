// SPDX-License-Identifier: GPL-3.0-only
// The file side of an install: verify -> extract into "<target>.zero-setup-tmp" -> swap in with a journal.
// A failed or cancelled install leaves the previous install exactly as it was.
#pragma once

#include "Payload.h"

#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>

namespace ZeroSetup {

enum class InstallKind { None, Nsis, V2 };

/// What is installed at a folder right now (see detectExisting in Upgrade.h).
struct ExistingInstall
{
    InstallKind kind = InstallKind::None;
    QString dir;
    QString version;           // may be empty when unknown
    QStringList ownedFiles;    // V2: from the install manifest; Nsis: the NSIS layout found on disk
};

struct InstallRequest
{
    QString targetDir;         // absolute
    QString version;           // written to the install manifest
    QString uninstallerSource; // exe whose first `uninstallerBytes` bytes become uninstall.exe; empty = no uninstaller
    quint64 uninstallerBytes = 0;  // 0 = the whole file
    bool verifyPayloadHash = true; // tests turn it off to exercise the per-file checks
    ExistingInstall existing;      // files of the previous install that are not in the new one get removed (and rolled back)
};

enum class InstallStatus { Ok, Cancelled, Failed };

struct InstallResult
{
    InstallStatus status = InstallStatus::Ok;
    QString error;           // one sentence, empty on success
    QString stateNote;       // "Nothing was changed." / "Your previous version is still installed."
    QStringList installedFiles;  // relative paths now owned by this install (payload files + uninstaller + manifest)
};

/// step is the user-facing step text ("Copying files", "Finishing"); done/total count uncompressed bytes.
using StepProgress = std::function<void(const QString& step, quint64 done, quint64 total)>;

InstallResult installFiles(PayloadReader& payload, const InstallRequest& req, const std::atomic<bool>* cancel, const StepProgress& progress);

/// Folders next to the target used while installing.
QString stagingDirFor(const QString& targetDir);

/// Reads "<dir>/.zero-install.json"; empty list when absent or unreadable.
bool readInstallManifest(const QString& dir, QString* version, QStringList* files);

/// True when a file at `path` exists and cannot be opened for writing (a running exe / loaded DLL).
bool isFileLocked(const QString& path);

}  // namespace ZeroSetup
