// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - "Try it in a copy": bringing the changes of a test copy back to its original (docs/zero/ARCHITECTURE.md, "Test copies").
// Service layer: synchronous, no widgets, no APPLICATION (SandboxTasks runs it on a pool thread). The safety copy of the original
// (PackSnapshot, kind "bring-back") is taken BEFORE this runs; this code only moves files.
//
// What it does for the chosen parts (SandboxPlan: Mods, Config, Resource packs, Shader packs, Game version and loader, Worlds):
//  1. Targets: every file / folder of a part whose content differs between the two instances. Worlds: the copy's worlds that are new or
//     changed; they replace the same-named world folder of the original, worlds only in the copy are added, worlds only in the original stay.
//  2. Worlds that will be replaced get a Safety world backup first (`Hooks::backupWorld`); a failure stops everything, nothing is touched.
//  3. The copy's version of every target is copied into a staging folder `<original instance>/.zero-bringback-<ts>/new/<n>` (the original is
//     still untouched; a failure here removes only that ZERO-owned folder).
//  4. The swap: per target two renames, the original's file / folder to `.../old/<name>` and the new one into its place. Any failure puts every
//     target back (reverse order) and removes the new copies.
//  5. The replaced originals go to the Recycle Bin (Hooks::trashPath, default FS::trash and NOTHING else: never a permanent delete). One that
//     cannot go stays in the staging folder, which is then left in place and named in the result.
// The only things ever deleted for good are ZERO's own staging copies (`new/`) and empty staging folders.
#pragma once

#include <QString>
#include <QStringList>
#include <functional>

#include "SandboxPlan.h"

namespace Zero {
namespace Sandbox {

/// Seams for tests; every member left empty uses the real implementation.
struct Hooks {
    /// Moves a path to the Recycle Bin. Default: FS::trash and nothing else.
    std::function<bool(const QString& path)> trashPath;
    /// Directory / file rename; must fail when the destination exists. Default: Backup::renamePath.
    std::function<bool(const QString& from, const QString& to)> rename;
    /// Copies a file or a folder tree (the destination does not exist yet), keeping modification times. Default: copyTree().
    std::function<bool(const QString& from, const QString& to)> copyTree;
    /// Safety backup of one world of the ORIGINAL before it is replaced: (folder name, absolute path) -> an error text, empty = made.
    /// Required when worlds are replaced (see safetyBackupHook()).
    std::function<QString(const QString& worldFolder, const QString& worldPath)> backupWorld;
    /// Milliseconds since the epoch (the staging folder name). Default: the clock.
    std::function<qint64()> nowMs;
};

struct Request {
    Layout original;
    Layout copy;
    QList<Part> parts;
};

struct Result {
    bool ok = false;
    QString error;
    /// Set on a failure: true when every file is where it was before (nothing changed). False = something could not be put back; the
    /// error names where the files are.
    bool restored = true;
    bool changedAnything = false;     ///< ok and at least one target was applied
    QStringList appliedParts;         ///< partKey()s that had something to apply
    QStringList replacedWorlds;       ///< world folders of the original that were replaced (their old version has a safety backup)
    QStringList addedWorlds;          ///< world folders that only the copy had
    /// Replaced originals that could not go to the Recycle Bin (still on disk, inside the staging folder).
    QStringList leftovers;
};

/// Applies `request` (see the file header). Never touches the copy.
Result apply(const Request& request, const Hooks& hooks = {});

/// Copies a file or a folder tree to `to` (must not exist), keeping modification times. Fails on a folder link (not followed).
bool copyTree(const QString& from, const QString& to);

/// A Hooks::backupWorld that makes a Safety world backup with WorldBackup's createBackup into `backupDirOf(worldFolder)` and applies the
/// retention. `backupDirOf` is called on the calling thread of the hook.
std::function<QString(const QString&, const QString&)> safetyBackupHook(const QString& instanceId, const QString& instanceName,
                                                                         std::function<QString(const QString& worldFolder)> backupDirOf);

}  // namespace Sandbox
}  // namespace Zero
