// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Mod Intelligence: repair actions (first slice of the Repair Center; no widgets, no APPLICATION).
// Rules (docs/zero/ARCHITECTURE.md, "Mod Intelligence"):
//  - the only action is "disable a jar": rename `x.jar` to `x.jar.disabled` (Prism's own convention, undone by dropping the suffix);
//  - nothing is ever deleted and nothing is ever overwritten: a rename whose target exists is refused;
//  - every operation re-validates its input at call time (plain file name, directly in the mods folder, regular non-link file);
//  - actions are only offered when ZERO is sure which file is the culprit; otherwise the finding's suggestion stays the guidance.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "zero/mods/ModMetadata.h"

namespace Zero {
namespace Mods {

struct Finding;

struct RepairAction {
    enum class Kind { DisableJar };

    Kind kind = Kind::DisableJar;
    QString fileName;  ///< top-level jar directly inside the mods folder ("forge-only.jar")
    QString reason;    ///< user-facing English, one short sentence
};

/// Pure. The actions that fix `finding` (empty when none is safe). `enabledJars` are the parsed top-level enabled jars of the
/// folder (needed to compare the versions of duplicates). Rules:
///  - WrongLoader / MinecraftVersion / LoaderVersion, Confirmed only: disable the finding's single top-level jar;
///  - DuplicateMod, Confirmed only: disable every copy but the newest, only when all versions compare and the newest is unique;
///  - everything else: nothing.
/// A file that is not among `enabledJars` (e.g. only bundled inside another jar) is never a target.
QList<RepairAction> actionsFor(const Finding& finding, const QList<ModMetadata>& enabledJars);

struct RepairResult {
    bool ok = false;
    QString error;        ///< user-facing English, empty when ok
    QString fileName;     ///< the file the operation was asked about
    QString newFileName;  ///< the file name after the rename (empty when not ok)
};

/// `<fileName>` -> `<fileName>.disabled`. `fileName` must be a plain `*.jar` name of a regular, non-link file directly in `modsDir`;
/// refused when the target already exists.
RepairResult disableJar(const QString& modsDir, const QString& fileName);

/// `<x>.jar.disabled` / `<x>.disabled` -> the name without the trailing `.disabled`. Same validation as `disableJar`; refused when the
/// target already exists.
RepairResult enableJar(const QString& modsDir, const QString& disabledFileName);

struct RepairBatchResult {
    QList<RepairResult> results;  ///< one per action, in order
    QStringList succeeded;        ///< new file names (`x.jar.disabled`) of the renames that worked, in order
    QString firstError;           ///< error of the first failure (empty when all worked)
    bool allOk() const { return firstError.isEmpty() && !results.isEmpty(); }
};

/// Runs the actions one by one. A failure does not stop the others and nothing is rolled back: every rename that worked is
/// listed in `succeeded` and is individually reversible with `enableJar`.
RepairBatchResult applyAll(const QString& modsDir, const QList<RepairAction>& actions);

}  // namespace Mods
}  // namespace Zero
