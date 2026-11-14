// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the pure part of the instance window's Mods page (docs/zero/CONTENT-MODS.md, "Installed mods page"): the facts of
// one row, search and filter, the captions, the pills of a row, the "other mods are affected" decision of enable / disable and the texts
// of Remove, and the facts and texts of the update check ("C5b Updates": the results kept by the page, which row they still fit, the
// Updates filter, the captions and toasts). No widgets and no model: the page reads `Mod`s into `Row`s and the tests build rows by hand.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "zero/mods/DependencyInspector.h"

class Mod;

namespace Zero {
namespace Mods {
namespace Installed {

/// What the segmented filter shows. "Updates" exists only while the update check found something (see filtersFor()).
enum class Filter { All, Enabled, Disabled, Issues, Updates };

QString filterId(Filter filter);               ///< "all" | "enabled" | "disabled" | "issues" | "updates"
Filter filterFromId(const QString& id);        ///< unknown -> All
QString filterLabel(Filter filter);            ///< "All" | "Enabled" | "Disabled" | "Issues" | "Updates"
QList<Filter> allFilters();                    ///< the four that are always there
QList<Filter> filtersFor(bool withUpdates);    ///< allFilters(), plus Updates last when `withUpdates`

enum class Source { Modrinth, CurseForge, Local };
QString sourceLabel(Source source);            ///< "Modrinth" | "CurseForge" | "Local file"

/// What a mod needs that is not there (the Dependency Inspector, docs/zero/CONTENT-MODS.md): one group per kind, in this order.
enum class DepKind { Missing, Disabled, WrongVersion, Conflict };
struct DepProblem {
    DepKind kind = DepKind::Missing;
    QStringList names;  ///< display names of the mods involved, in the order of the mod's lists
};

/// Everything the page paints and decides about one mod file. Read from a `Mod` by rowFromMod().
struct Row {
    QString id;        ///< the model's internal id (the file name; changes when the mod is toggled)
    QString fileName;  ///< on disk, with the ".disabled" suffix when disabled
    QString filePath;  ///< absolute
    QString name;      ///< what the list shows (never empty: falls back to the file name)
    QString version;   ///< "" when unknown
    QString modId;
    Source source = Source::Local;
    QString environment;  ///< "Client" | "Server" | "Client & server" when the provider said, else ""
    bool enabled = true;
    bool hasMetadata = false;  ///< a packwiz entry exists (the mod came from a provider through ZERO / Prism)
    QStringList issues;        ///< Prism's compatibility issues, translated
    int requiredBy = 0;        ///< how many installed mods need this one
    QString homepage;          ///< the provider page, else the mod's own home page; "" when none
    bool hasIcon = false;      ///< the jar declares a logo
    QList<DepProblem> depProblems;  ///< filled by overlayDependencies(): only for an enabled mod whose scan result fits its file
    bool hasDepProblems() const { return !depProblems.isEmpty(); }

    // What the update check needs and shows (docs/zero/CONTENT-MODS.md, "C5b Updates").
    QString updateKey;          ///< id without ".disabled": stays the same when the mod is switched on or off
    QString metadataHash;       ///< the packwiz hash of the installed file ("" without metadata)
    QString fileStamp;          ///< size and time of the file: another file under the same name is another mod
    QString projectId;          ///< the provider's project id ("" without metadata)
    QString metadataVersion;    ///< the version the packwiz entry records
    bool checkable = false;     ///< has metadata with a usable project id and is not locked: the checkers can take it
    bool updateLocked = false;  ///< the packwiz entry says "do not update": never checked, never counted as unchecked
    QString updateVersion;      ///< the update the last check found ("" = none); filled by overlayResults()
    QString updateType;         ///< "Release" | "Beta" | "Alpha" | ""
    QString checkError;         ///< why the last check could not tell ("" = it could)
    bool hasUpdate() const { return !updateVersion.isEmpty(); }
};

/// "sodium.jar.disabled" -> "sodium.jar"
QString updateKeyOf(const QString& id);

/// A row from a mod: Prism's accessors only (no file access beyond what the model already read).
Row rowFromMod(const Mod& mod);

bool matchesFilter(const Row& row, Filter filter);
/// Every space-separated word must occur (case-insensitively) in the name, the mod id or the file name. Empty = everything.
bool matchesSearch(const Row& row, const QString& query);
/// Indexes into `rows` that pass both, ordered by name (case-insensitive), then by file name.
QList<int> visibleRows(const QList<Row>& rows, Filter filter, const QString& query);

struct Counts {
    int total = 0;
    int enabled = 0;
    int disabled = 0;
    int issues = 0;     ///< rows the Issues filter shows (Prism's issues and dependency problems)
    int attention = 0;  ///< rows with a dependency problem ("2 need attention")
    int updates = 0;  ///< rows with an update (only after a check)
};
Counts countOf(const QList<Row>& rows);

/// "48 mods · 3 disabled", "1 mod", "5 of 48 mods" (while a filter or a search narrows the list).
QString captionText(const Counts& counts, int shown, bool narrowed);
/// "3 selected"
QString selectionText(int selected);

enum class PillTone { Plain, Warning, Danger, Accent };
struct PillSpec {
    QString text;
    PillTone tone = PillTone::Plain;
    int priority = 0;  ///< the lowest is dropped first when the row is too narrow
    int maxWidth = 0;  ///< px; the text is elided to fit when > 0
};
constexpr int kUpdatePillMaxWidth = 140;
constexpr int kDepPillMaxWidth = 220;
/// Pills with at least this priority (the update pill, the dependency warnings and dangers) are never dropped for lack of room.
constexpr int kKeepPriority = 8;
/// The pills of a row in display order: the dependency problems ("Needs Indium", "Needs Sodium · disabled", "Wrong Sodium version", "Conflicts with
/// OptiFabric"), Disabled, "Not for <mc>", "Required by N", the environment, the provider, "Update -> 0.6.14".
QList<PillSpec> pillsFor(const Row& row, const QString& mcVersion);
/// "Not for 1.21.1"
QString notForText(const QString& mcVersion);
/// The tooltip of a row: the file name, the update (with its type) when there is one, the issues, a failed check.
QString rowToolTip(const Row& row);

// ---- the Dependency Inspector ----------------------------------------------------------------------------------------------------

/// Puts the problems of the scan into the rows: only enabled mods, and only when the scan read the very file the row stands for
/// (same path, same size / time stamp). Every other row loses its problems.
void overlayDependencies(QList<Row>& rows, const Inspection& inspection);
/// The pill of a group: "Needs Indium" / "Needs Indium +2", "Needs Sodium · disabled", "Wrong Sodium version", "Conflicts with OptiFabric".
QString depPillText(const DepProblem& problem);
/// The same with every name ("Needs Indium, Sodium") for the row tooltip.
QString depFullText(const DepProblem& problem);
/// "2 need attention" / "1 needs attention"
QString attentionText(int count);

// ---- the update check ------------------------------------------------------------------------------------------------------------

/// What one check said about one mod: an update, or why it could not say.
struct CheckEntry {
    QString key;           ///< Row::updateKey
    QString metadataHash;  ///< Row::metadataHash when the check ran
    QString fileStamp;     ///< Row::fileStamp when the check ran
    QString newVersion;    ///< "" = no update
    QString versionType;   ///< "Release" | "Beta" | "Alpha" | ""
    QString error;         ///< "" = the check worked
    bool hasUpdate() const { return !newVersion.isEmpty(); }
};

/// The results of the last check. Kept by the page in memory only.
struct UpdateResults {
    bool checked = false;  ///< a check finished (even with nothing found)
    QList<CheckEntry> entries;
};

int updateCount(const UpdateResults& results);
int failureCount(const UpdateResults& results);
/// Drops the entries whose mod is gone or whose file or metadata hash changed since the check. Returns true when something was dropped.
bool pruneResults(UpdateResults& results, const QList<Row>& rows);
/// Puts the update / the failure of each row's entry into the row (the entry must still fit: same hash and file stamp).
void overlayResults(QList<Row>& rows, const UpdateResults& results);
/// Removes the entries with these keys (they were applied).
void dropEntries(UpdateResults& results, const QStringList& keys);

/// Which mods the checkers can take: `checkable` and, for CurseForge, only when the application can talk to it.
bool canCheck(const Row& row, bool curseForgeAvailable);
/// Mods that cannot be checked for lack of provider info (not the ones locked on purpose).
int unverifiableCount(const QList<Row>& rows, bool curseForgeAvailable);

/// Which of the checked mods an `Update` of the checkers belongs to: the same provider and project; with several, the one with the same
/// hash, then the same version, then the first one not taken yet. -1: none.
struct UpdateCandidate {
    Source source = Source::Modrinth;
    QString projectId;
    QString hash;
    QString version;
};
struct UpdateFacts {
    Source source = Source::Modrinth;
    QString projectId;
    QString oldHash;
    QString oldVersion;
};
int matchUpdate(const QList<UpdateCandidate>& candidates, const QList<bool>& taken, const UpdateFacts& facts);

QString checkButtonText();                        ///< "Check for updates"
QString checkingButtonText();                     ///< "Checking..."
QString checkingCaption(int count);               ///< "Checking 41 mods..." / "Checking 1 mod..."
QString updateAllText(int count);                 ///< "Update all 3" / "Update 1 mod"
QString updatePillText(const QString& version);   ///< "Update -> 0.6.14"
QString updateToText(const QString& version);     ///< "Update to 0.6.14" (row menu)
QString updateSelectedText(int count);          ///< "Update 1 mod" / "Update 3 mods" (row menu on a selection)
QString checkAgainText();                         ///< "Check for updates again" (overflow menu)
QString unverifiableText(int count);              ///< "1 mod has no provider info and can't be checked." / "7 mods ..."; "" for 0
QString upToDateText();                           ///< "All mods are up to date"
QString nothingToCheckText();                     ///< "None of these mods can be checked for updates"
QString checkFailedText(const QString& reason);   ///< "Couldn't check for updates: <reason>"
QString updatedText(const QStringList& names, const QString& newVersion);  ///< "Updated Sodium to 0.6.14" / "Updated 3 mods"
QString versionTypeLabel(const QString& type);    ///< "Release" | "Beta" | "Alpha" (translated), "" otherwise
/// True when something was checked and every checked mod failed (no update, no "up to date" answer).
bool everyCheckFailed(int checked, const UpdateResults& results);

// ---- enable / disable ------------------------------------------------------------------------------------------------------------

enum class ToggleAction { Enable, Disable };
/// What Space does on a selection: turn everything off when everything is on, otherwise turn everything on.
ToggleAction nextToggle(const QList<bool>& enabledFlags);

/// The ZERO dialog asked before a toggle that reaches other mods. `needed` = false: just do it.
struct TogglePrompt {
    bool needed = false;
    QString title;
    QString body;
    QString onlyLabel;  ///< "Disable only Sodium"
    QString allLabel;   ///< "Disable all 2"
    int allCount = 0;   ///< selection + affected
};
/// `selectedNames`: the mods that will change by themselves; `affectedNames`: the other mods the change reaches (Prism's
/// `getAffectedMods`: enabled mods that need a mod being disabled, disabled mods a mod being enabled needs).
TogglePrompt togglePrompt(ToggleAction action, const QStringList& selectedNames, const QStringList& affectedNames);
/// "Couldn't enable Sodium" / "Couldn't disable 2 mods"
QString toggleFailureText(ToggleAction action, const QStringList& failedNames);

// ---- remove ----------------------------------------------------------------------------------------------------------------------

constexpr int kRemoveListMax = 8;

struct RemoveConfirm {
    QString title;         ///< "Remove Sodium?" / "Remove 3 mods?"
    QStringList listed;    ///< up to kRemoveListMax names
    QString more;          ///< "and 4 more" or ""
    QString note;          ///< "They go to the Recycle Bin."
    QString confirmLabel;  ///< "Remove"
};
/// What the Remove texts call the files: the Mods page says "mods", the Resource packs page "packs" (shared code, one set of texts).
enum class Noun { Mods, Packs };
RemoveConfirm removeConfirm(const QStringList& names, Noun noun = Noun::Mods);

enum class Outcome { Success, Warning, Error };
struct RemoveResult {
    QString text;
    Outcome outcome = Outcome::Success;
};
/// "Removed 3 mods" / "Couldn't move 2 mods to the Recycle Bin" / both when some went and some did not.
RemoveResult removeResult(const QStringList& removedNames, const QStringList& failedNames, Noun noun = Noun::Mods);

/// "Sodium, Lithium and Iris" / "A, B, C and 4 more": at most three names.
QString joinNames(const QStringList& names);

}  // namespace Installed
}  // namespace Mods
}  // namespace Zero
