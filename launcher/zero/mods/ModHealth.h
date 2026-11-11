// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Mod Intelligence core (no widgets, no APPLICATION, no Prism instance classes): a local health inspector for the
// mods of one instance. Rules (docs/zero/ARCHITECTURE.md, "Mod Intelligence"):
//  - read-only: jars are only read, nothing is extracted, written or moved;
//  - every finding carries a confidence. Confirmed = the loader itself refuses / cannot do it; Likely = declared by the mod but
//    only a warning for the loader; Possible = something may be wrong but ZERO cannot be sure; Unknown = ZERO cannot read it;
//  - no false conflict claims: an Unknown version comparison never becomes Confirmed, and anything that might be explained by a
//    mod ZERO could not read lowers the confidence;
//  - disabled mods are never analyzed and never satisfy a dependency.
#pragma once

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

#include "zero/mods/ModMetadata.h"
#include "zero/mods/ModRepair.h"

namespace Zero {
namespace Mods {

enum class InstanceLoader { None, Fabric, Quilt, Forge, NeoForge };

/// "none" "fabric" "quilt" "forge" "neoforge".
QString instanceLoaderId(InstanceLoader loader);

/// What the instance runs. Plain input, filled by `readInstanceContext` or by the caller.
struct InstanceContext {
    QString mcVersion;
    InstanceLoader loader = InstanceLoader::None;
    QString loaderVersion;
    int javaMajor = 0;  ///< 0 = unknown (the analysis does not use it yet, `java` requirements are never reported)
};

/// Reads `<instanceRoot>/mmc-pack.json` (uids `net.minecraft`, `net.fabricmc.fabric-loader`, `org.quiltmc.quilt-loader`,
/// `net.minecraftforge`, `net.neoforged`; disabled components are ignored). Read-only; a missing / unreadable file gives an empty context.
/// (JavaDoctor's `readRequirements` reads the same file but only keeps the Minecraft version and needs the metadata folder.)
InstanceContext readInstanceContext(const QString& instanceRoot);

enum class Confidence { Confirmed, Likely, Possible, Unknown };  ///< declaration order = sort order, strongest first

/// "confirmed" "likely" "possible" "unknown".
QString confidenceId(Confidence c);

enum class FindingKind {
    MissingDependency,        ///< a required mod is not installed
    DependencyVersion,        ///< a required mod is installed but its version is outside the accepted range
    MinecraftVersion,         ///< the mod was made for another Minecraft version
    LoaderVersion,            ///< the mod needs another mod loader version
    WrongLoader,              ///< the mod is for another mod loader
    DeclaredIncompatibility,  ///< the mod itself declares that it does not work with another installed mod
    DuplicateMod,             ///< two enabled jars carry the same mod id
    UnreadableMetadata        ///< ZERO cannot read the mod's metadata
};

/// "missing-dependency" "dependency-version" "minecraft-version" "loader-version" "wrong-loader" "declared-incompatibility"
/// "duplicate-mod" "unreadable-metadata".
QString findingKindId(FindingKind kind);

struct Finding {
    FindingKind kind = FindingKind::UnreadableMetadata;
    Confidence confidence = Confidence::Unknown;
    QStringList modIds;     ///< ids of the installed mods involved (sorted, no duplicates)
    QStringList fileNames;  ///< top-level jar file names involved (sorted, no duplicates)
    QString relatedId;      ///< the other side: the missing / mismatching / conflicting id (e.g. "fabric-api", "minecraft"); may be empty
    QStringList args;       ///< raw values behind `title` (args[0] = display name of the main mod), for callers that format their own text
    QString title;          ///< user-facing English, ready to show
    QString detail;
    QString suggestion;
    QList<RepairAction> actions;  ///< safe fixes ZERO can apply (empty from `analyze`; `scanModsFolder` fills it via `actionsFor`)
};

struct Report {
    QList<Finding> findings;  ///< sorted: Confirmed, Likely, Possible, Unknown, then by mod name
    int confirmed = 0;
    int likely = 0;
    int possible = 0;
    int unknown = 0;
    int modsScanned = 0;  ///< enabled jars that were given to the analysis

    int count(Confidence c) const;
    bool isClean() const { return findings.isEmpty(); }
    /// Every action of every finding, one per file (a file named by two findings appears once).
    QList<RepairAction> allActions() const;
};

/// Pure. `enabledJars` are the parsed enabled mods; `disabledFileNames` the disabled files (`x.jar.disabled`), which are never
/// analyzed. When a missing dependency is present as a disabled file the suggestion says to enable it: with `disabledJars`
/// (their parsed metadata, optional) the match is exact; without it ZERO only compares file names and says "may".
Report analyze(const QList<ModMetadata>& enabledJars, const InstanceContext& context, const QStringList& disabledFileNames,
               const QList<ModMetadata>& disabledJars = {});

}  // namespace Mods
}  // namespace Zero

Q_DECLARE_METATYPE(Zero::Mods::Report)
