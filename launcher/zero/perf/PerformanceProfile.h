// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - "Boost performance" without widgets (docs/zero/SHELL.md, "Boost performance"):
//
//  - the CURATED TABLE of performance mods per loader (one place: `curatedFor`), with the Modrinth slug, the display name, the
//    one-line benefit and the mod ids the jars carry,
//  - the ROWS of the dialog (`buildRows`): what the catalog found on Modrinth for this Minecraft version + loader, merged with what the
//    instance has installed -> Offered / Installed / "Installed (Embeddium)" / Unavailable / Conflicts with OptiFine,
//  - the MEMORY RULE (`recommendedMaxMb`, `adviseMemory`): base 3 GB vanilla / 4 GB modded, +1 GB per started 75 mods, at most 8 GB,
//    never above half of the PC's RAM (whole GB), at least 2 GB; it never proposes to LOWER what is set,
//  - the Java line (only when Java Doctor's diagnosis is a problem) and every text of the dialog.
//
// No widgets, no APPLICATION, no network: tests build every input by hand.
#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

#include "modplatform/ModIndex.h"
#include "zero/content/InstalledMods.h"
#include "zero/java/JavaDoctor.h"

namespace Zero::Perf {

// ---- loaders and the curated table ----

enum class Loader { None, Fabric, Quilt, NeoForge, Forge };

/// The loader the table is chosen by: NeoForge, then Forge, then Quilt, then Fabric (a Quilt instance also accepts Fabric mods, Prism
/// reports both flags); None for vanilla and for loaders without a list (LiteLoader, Cauldron).
Loader loaderOf(ModPlatform::ModLoaderTypes loaders);
/// "Fabric", "Quilt", "NeoForge", "Forge"; empty for None.
QString loaderName(Loader loader);

/// One performance mod.
struct Project {
    QString slug;             ///< Modrinth slug (the API takes slugs as well as ids)
    QString name;             ///< "Sodium"
    QString benefit;          ///< one line: "Faster rendering, much higher FPS"
    QStringList modIds;       ///< lower-case mod ids of its jars (an installed jar without metadata is recognised by them)
    QString alternativeSlug;  ///< the project that does the same job (Sodium <-> Embeddium); installing both is pointless
    bool rendering = false;   ///< replaces the renderer: conflicts with OptiFine
};

/// The project of `slug`; a default-constructed one (empty slug) when the slug is not in the table.
Project projectFor(const QString& slug);

/// One line of a loader's list: the project, and the project to use when the first one has no file for the Minecraft version
/// (NeoForge: Sodium, else Embeddium).
struct Entry {
    QString slug;
    QString fallbackSlug;
};
/// The ordered list of a loader; empty for None.
QList<Entry> curatedFor(Loader loader);
/// Every slug the catalog has to look up for `loader` (the entries and their fallbacks), once each, in order.
QStringList slugsFor(Loader loader);

// ---- the rows ----

/// What the catalog found for one project: its Modrinth pack (addonId = the project id) and the versions it has for the target's
/// Minecraft version + loaders, in the provider's order (newest first). `versions` may be empty (no file for this target).
struct Found {
    QString slug;
    ModPlatform::IndexedPack::Ptr pack;
    QList<ModPlatform::IndexedVersion> versions;
};
using FoundMap = QHash<QString, Found>;  ///< by slug

/// What the instance has in its mods folder.
struct InstalledSnapshot {
    QList<Zero::Content::InstalledMod> withMetadata;  ///< the mods with provider metadata (key = managedKey("modrinth", project id))
    QStringList modIds;                               ///< mod ids read from the jars (any case)
    QStringList names;                                ///< file names / display names (any case): OptiFine is found by them
    int count = 0;                                    ///< every mod of the folder, metadata or not
};

enum class RowState {
    Offered,               ///< can be added (ticked by default)
    Installed,             ///< the instance has it
    InstalledAlternative,  ///< the instance has the mod that does the same job (Embeddium for Sodium)
    Unavailable,           ///< no file for this Minecraft version + loader
    ConflictOptiFine       ///< OptiFine is installed: a renderer replacement would break it
};

struct Row {
    QString slug;  ///< the project that would be installed (the fallback when the first one has no file)
    QString name;
    QString benefit;
    RowState state = RowState::Unavailable;
    QString alternativeName;              ///< InstalledAlternative: the mod that is installed ("Embeddium")
    ModPlatform::IndexedPack::Ptr pack;   ///< Offered: what to download
    ModPlatform::IndexedVersion version;  ///< Offered: the preselected version

    bool canTick() const { return state == RowState::Offered; }
    bool tickedByDefault() const { return state == RowState::Offered; }
};

/// OptiFine (or OptiFabric) is among the installed mods.
bool hasOptiFine(const InstalledSnapshot& installed);
/// The mod of `slug` is installed: by its Modrinth project id (`projectId`, from the catalog; may be empty) in the metadata, else by mod id.
bool isInstalled(const QString& slug, const QString& projectId, const InstalledSnapshot& installed);

/// The rows of `loader`'s list. `found` holds the catalog's answer; a project missing from it counts as "no file". `mcVersion` and
/// `loaders` are the target's (the versions are checked with Zero::Content::compatibleVersions again, so a catalog that over-delivers
/// does no harm). Order = the table's.
QList<Row> buildRows(Loader loader, const QString& mcVersion, ModPlatform::ModLoaderTypes loaders, const FoundMap& found,
                     const InstalledSnapshot& installed);

// ---- memory ----

/// Max heap to recommend, in MB (a whole number of GB). `modded` = the instance has a mod loader; `modCount` = installed + ticked mods.
/// 3 GB vanilla / 4 GB modded, + 1 GB per STARTED 75 mods (1..75 = +1), at most 8 GB, never above half of `ramMb` (floored to whole GB),
/// at least 2 GB; when half of the RAM is below 2 GB the answer is that half (whole GB, at least 1 GB). 0 when `ramMb` is unknown (<= 0).
int recommendedMaxMb(int ramMb, int modCount, bool modded);
/// Min heap for a max of `maxMb`: 1 GB, or half of the max when that is smaller.
int recommendedMinMb(int maxMb);

struct MemoryAdvice {
    bool known = false;  ///< the PC's RAM is known (else the section is not shown)
    int ramMb = 0;
    int currentMaxMb = 0;
    int recommendedMaxMb = 0;
    int recommendedMinMb = 0;
    /// The recommendation is higher than what is set: the toggle is offered, on by default. Otherwise "Memory is fine" (never lowers).
    bool needsChange() const { return known && currentMaxMb < recommendedMaxMb; }
};
MemoryAdvice adviseMemory(int ramMb, int modCount, bool modded, int currentMaxMb);

/// The max heap an instance has now: its own value when it overrides memory, else the launcher's.
int currentMaxMb(bool overrideMemory, int instanceMaxMb, int globalMaxMb);

// ---- Java ----

/// The one-line problem of Java Doctor's diagnosis ("Java 17 is set; this version needs Java 21."), or nothing when the Java is fine
/// (Ready) or unknown (the version does not say what it needs).
std::optional<QString> javaProblemText(const Zero::Java::InstanceJava& instance, const Zero::Java::Diagnosis& diagnosis);

// ---- texts ----

/// "16 GB" / "5.5 GB" (whole GB without decimals).
QString gbText(int mb);
/// "16" / "5.5": gbText() without the unit (for "2 -> 6 GB").
QString gbNumber(int mb);
/// "Mods · Fabric 1.21.1" (the dialog upper-cases it as an overline).
QString modsTitle(Loader loader, const QString& mcVersion);
/// The vanilla line.
QString vanillaModsText();
/// The note under the list of a managed modpack.
QString modpackNote();
/// "Installed" / "Installed (Embeddium)" / "Not available for 1.21.1" / "Conflicts with OptiFine"; empty for Offered.
QString rowStateText(const Row& row, const QString& mcVersion);
/// "Your PC has 16 GB of RAM."
QString ramText(int ramMb);
/// "Give Minecraft 6 GB"
QString memoryGiveText(int recommendedMb);
/// "now 2 GB"
QString memoryNowText(int currentMb);
/// "Memory is fine (6 GB)"
QString memoryFineText(int currentMb);
/// "Boost performance · Fabric Fun" (the dialog's title).
QString dialogTitle(const QString& instanceName);

struct Summary {
    int mods = 0;          ///< ticked mods
    int dependencies = 0;  ///< the ones they need
    bool memory = false;   ///< the memory toggle is on and changes something
    int memoryFromMb = 0;
    int memoryToMb = 0;
    bool javaArgs = false;  ///< the recommended Java arguments will be written

    bool isEmpty() const { return mods == 0 && !memory && !javaArgs; }
};
/// "Adds 4 mods (+1 they need) · Memory 2 → 6 GB · Java arguments tuned"; parts without content are left out; "Nothing to change" when empty.
QString summaryText(const Summary& summary);
/// "Boosted Fabric Fun"
QString boostedToastText(const QString& instanceName);
/// "Close Fabric Fun first"
QString closeFirstText(const QString& instanceName);

}  // namespace Zero::Perf
