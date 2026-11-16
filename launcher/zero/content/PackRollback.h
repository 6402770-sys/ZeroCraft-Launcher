// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - "Minecraft turned off your resource packs" (docs/zero/CONTENT-PACKS.md, "Pack rollback").
//
// Vanilla Minecraft behaviour: when the resource reload at startup throws (typically an old pack whose core shader no longer compiles),
// the game logs `Caught error loading resourcepacks, removing all selected resourcepacks`, clears EVERY selected pack and saves
// options.txt. The launcher cannot fix a broken pack; it detects the rollback in `logs/latest.log` once the game stopped, names the pack
// that most likely caused it, and offers to put the packs back (the page's notice, a question before the next launch).
//
// This file is the pure part: no widgets, no Application. It
//   * parses the log text (`analyzeLog`): rolled back or not, the attempted list (options.txt order, bottom to top) and the shaders that
//     failed to compile (`minecraft:core/entity`);
//   * finds the suspects (`findSuspects`): for each failing shader the TOP-most attempted `file/` pack (zip or folder) that provides
//     `assets/<ns>/shaders/<path>.{vsh,fsh,glsl}` at its root or in an overlay directory that applies to the instance's resource format
//     (`pack.mcmeta` `overlays.entries`; formats as int, [min,max], {min_inclusive,max_inclusive}, min_format / max_format as int or
//     [major,minor]);
//   * keeps the marker `<instance root>/.zero/pack-rollback.json` {time, attempted, suspects, asked}.
// The marker is removed when the user restores or dismisses, and by `Packs::writeOrder` whenever the launcher writes a new pack order.
// Only the major number of a format is compared (the table ZERO knows has no minor versions of releases).
#pragma once

#include <QDateTime>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

#include "zero/content/InstalledPacks.h"

namespace Zero {
namespace Content {
namespace Packs {
namespace Rollback {

inline constexpr char kMarkerFileName[] = "pack-rollback.json";

// ---- the log --------------------------------------------------------------------------------------------------------------------

struct LogAnalysis {
    bool rolledBack = false;        ///< the "removing all selected resourcepacks" line is there
    QStringList attempted;          ///< options.txt entries (`vanilla`, `file/A.zip`, `ns:id`), bottom to top; mod ids dropped
    QStringList failingShaders;     ///< "minecraft:core/entity", in order of appearance, no repeats
};

/// Reads the text of a latest.log. The attempted list is the last `Reloading ResourceManager:` line before the first rollback line.
/// Bare mod ids (no `file/` prefix and no `:`) are Fabric mod resources that are always on and not options.txt entries: dropped.
LogAnalysis analyzeLog(const QString& logText);

/// The pieces of an `Reloading ResourceManager:` list, with the names that contain ", " put back together. Exposed for the tests.
QStringList splitReloadList(const QString& list);

// ---- the shaders ----------------------------------------------------------------------------------------------------------------

struct Overlay {
    QString directory;
    int minFormat = -1;       ///< -1 = no lower bound
    int maxFormat = -1;       ///< -1 = no upper bound
};

/// `overlays.entries` of a pack.mcmeta (bytes, a BOM is fine). Entries without a directory are dropped.
QList<Overlay> parseOverlays(const QByteArray& mcmeta);
/// Does the overlay apply to resource format `format`? An unknown format (nullopt) counts as applicable.
bool overlayApplies(const Overlay& overlay, std::optional<int> format);

/// The resource format of a Minecraft version from the table of pack formats; nullopt when the version is not in it (newer than the
/// table, a snapshot that cannot be ordered, empty).
std::optional<int> formatForVersion(const FormatMap& mappings, const QString& mcVersion);
/// The resource pack format of a Minecraft client jar, read from its version.json `pack_version` (an int, or an object with
/// `resource_major` / `resource`); nullopt when the jar or the field cannot be read. Exact for versions newer than the table.
std::optional<int> formatFromVersionJson(const QByteArray& versionJson);
std::optional<int> formatFromClientJar(const QString& jarPath);

struct Suspect {
    QString file;     ///< "Refined Buckets 2.4.1.zip" (without `file/`)
    QString shader;   ///< "minecraft:core/entity"
};

/// The suspects for `failingShaders`: per shader the top-most pack of `attempted` (`file/` entries; the last one wins) that provides it.
/// `resourcePacksDir` is where those files are. A pack that is gone or cannot be read is skipped. Files are read, never written.
QList<Suspect> findSuspects(const QStringList& attempted, const QStringList& failingShaders, const QString& resourcePacksDir,
                            std::optional<int> format);

// ---- the marker -----------------------------------------------------------------------------------------------------------------

struct Marker {
    bool valid = false;
    QDateTime time;
    QStringList attempted;       ///< options.txt entries, bottom to top
    QList<Suspect> suspects;
    bool asked = false;          ///< the question before launching was shown for this marker
};

QString markerPathFor(const QString& instanceRoot);
/// `<dir of the history file>/pack-rollback.json` (the history file is `<instance root>/.zero/pack-order.json`).
QString markerPathForHistory(const QString& historyPath);
Marker readMarker(const QString& markerPath);
bool writeMarker(const QString& markerPath, const Marker& marker);
bool clearMarker(const QString& markerPath);
/// Sets `asked` on the stored marker. False when there is none or it could not be written.
bool markAsked(const QString& markerPath);

/// The `file/` packs of the attempted list as file names, top first (the order Packs::writeOrder takes).
QStringList attemptedFilesTopFirst(const QStringList& attempted);
/// The distinct file names of the suspects.
QStringList suspectFiles(const QList<Suspect>& suspects);

// ---- after the game stopped -----------------------------------------------------------------------------------------------------

enum class DetectResult {
    NoLog,            ///< there is no latest.log: nothing changes
    Stale,            ///< latest.log is older than the launch start: nothing changes
    NoPacks,          ///< the game ran without `file/` packs: nothing changes (an older marker stays, the packs are still off)
    LoadedFine,       ///< the packs were loaded: an older marker is removed
    Recorded,         ///< the game turned the packs off: the marker was written
    WriteFailed,      ///< rolled back but the marker could not be written
};

struct DetectInput {
    QString instanceRoot;
    QString gameRoot;                 ///< `<gameRoot>/logs/latest.log`, `<gameRoot>/resourcepacks`
    QDateTime launchStart;            ///< latest.log must have been modified at or after this
    std::optional<int> format;        ///< the instance's resource format, when known
};

DetectResult detectAfterLaunch(const DetectInput& input);

// ---- what the user is told ------------------------------------------------------------------------------------------------------

/// "Refined Buckets 2.4.1", "A and B", "A, B and C": display names of files (".zip" dropped).
QString namesText(const QStringList& files);
QString noticeTitle();                                ///< "Minecraft turned off your resource packs"
QString noticeBody(const QStringList& suspectFiles);  ///< see the page
QString restoreWithoutText(const QStringList& suspectFiles);   ///< "Restore without Refined Buckets 2.4.1"
QString restoreAllText();                                      ///< "Restore all"
QString dismissText();                                         ///< "Dismiss"
QString brokeLastLaunchText();                                 ///< "Broke the last launch" (the pill)
QString brokeTooltip(const QString& shader);                   ///< the tooltip line of that pill
QString restoredAllText();                                     ///< the toast after Restore all
QString restoredWithoutText(const QStringList& suspectFiles);  ///< the toast after Restore without X

QString promptTitle();                                          ///< "Minecraft turned off your resource packs last time"
QString promptBody(const QStringList& suspectFiles);
QString promptRestoreAndPlayText(const QStringList& suspectFiles);
QString promptPlayWithoutText();                                ///< "Play without them"

/// The order to write for a restore: the attempted `file/` packs (minus the suspects when `withoutSuspects`) on top, then the packs that
/// are on now and not among them; only names in `existing`. Top first. Pure.
QStringList restoreOrder(const Marker& marker, bool withoutSuspects, const QStringList& activeTopFirst, const QSet<QString>& existing);

/// Should the question be asked before the next launch? There is a marker with attempted `file/` packs, it was not asked yet, and
/// options.txt has no `file/` pack on now (`activeFiles` = Packs::activeFiles of the current options).
bool shouldAskBeforeLaunch(const Marker& marker, const QStringList& activeFiles);

}  // namespace Rollback
}  // namespace Packs
}  // namespace Content
}  // namespace Zero
