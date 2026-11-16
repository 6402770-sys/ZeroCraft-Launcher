// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the pure part of the instance window's Resource packs page (docs/zero/CONTENT-PACKS.md, "D2 Installed resource packs"):
// the facts of one pack file, the plain text of its description, whether its pack format fits the instance, search and filter, the two
// groups of the list (ON with the fixed "Default resources" row, AVAILABLE with the hidden files last), the captions, the pills and the
// texts of the toasts. No widgets and no model: the page reads `ResourcePack`s into `PackRow`s and the tests build rows by hand.
#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <utility>

#include "Version.h"
#include "zero/mods/InstalledModsModel.h"

namespace Zero {
namespace Content {
namespace Packs {

// ---- the description ------------------------------------------------------------------------------------------------------------

/// What a pack's `description()` says as one line of plain text. The input is the HTML Prism builds from the JSON text components
/// (`<span style="color: ...">`, `<u>`, `<s>`, `<a href>`; the text is not escaped) or a plain string with legacy `§` formatting codes.
/// Tags are dropped, the common entities (&amp; &lt; &gt; &quot; &apos; &#39; &nbsp;) are unescaped, `§` and the code character after it
/// are dropped, every run of white space becomes one space.
QString plainDescription(const QString& description);

// ---- the pack format ------------------------------------------------------------------------------------------------------------

using FormatMap = QMap<std::pair<int, int>, std::pair<Version, Version>>;  ///< ResourcePack::mappings(): format -> first / last Minecraft

enum class FitKind { Fits, MadeFor, Unknown };
struct PackFit {
    FitKind kind = FitKind::Unknown;
    QString madeFor;  ///< MadeFor: the first Minecraft version of the pack's format ("1.20")
};
/// Does a pack with `packFormat` suit `mcVersion`? Fits when the version lies in the range of the pack's format, MadeFor when it does
/// not. Unknown (nothing is claimed) when the pack has no format, the table does not know it or the Minecraft version, or a bound is a
/// snapshot the versions cannot be ordered against. Pure.
PackFit packFit(int packFormat, const FormatMap& mappings, const QString& mcVersion);
/// "Made for 1.20"
QString madeForText(const QString& version);

// ---- rows -----------------------------------------------------------------------------------------------------------------------

enum class Source { Local, Modrinth, CurseForge };

struct PackRow {
    QString id;        ///< the model's internal id (the file name, with ".disabled" when hidden)
    QString fileName;  ///< on disk
    QString filePath;  ///< absolute
    QString name;      ///< what the list shows: the file name without ".zip" (a folder: its name)
    QString description;  ///< plain text, may be empty
    bool folder = false;
    bool hidden = false;   ///< "x.zip.disabled": the game does not see it
    int packFormat = 0;
    PackFit fit;
    Source source = Source::Local;
    bool hasMetadata = false;
    QString projectName;   ///< the provider's name of the pack ("" without metadata)
    QString homepage;
    bool hasImage = false;
    // overlay of options.txt (overlayActive)
    bool active = false;   ///< `file/<fileName>` is in `resourcePacks`
    int activeIndex = -1;  ///< 0 = top (wins); -1 = not active
    // overlay of the pack rollback marker (zero/content/PackRollback): the pack that most likely made the game turn every pack off
    bool brokeLastLaunch = false;
    QString brokeShader;   ///< the shader it replaces that failed to compile ("minecraft:core/entity"); may be empty
};

/// "Faithful 32x.zip" / "Faithful 32x.zip.disabled" -> "Faithful 32x"; a folder keeps its name.
QString packNameOf(const QString& fileName, bool folder);
/// The name the game knows the pack by in options.txt: the file name; "" for a hidden file (it has none).
QString entryNameOf(const PackRow& row);

/// Sets `active` / `activeIndex` from the active file names, top first (PackOrder::activeFiles()). Hidden files are never active.
void overlayActive(QList<PackRow>& rows, const QStringList& activeTopFirst);

/// The names the page may put in options.txt: the visible packs (zips and folders, not the hidden files).
QStringList activatableNames(const QList<PackRow>& rows);

enum class Filter { All, On, Off };
QString filterId(Filter filter);          ///< "all" | "on" | "off"
Filter filterFromId(const QString& id);   ///< unknown -> All
QString filterLabel(Filter filter);       ///< "All" | "On" | "Off"
QList<Filter> allFilters();

bool matchesFilter(const PackRow& row, Filter filter);
/// Every space-separated word must occur (case-insensitively) in the name, the file name or the description. Empty = everything.
bool matchesSearch(const PackRow& row, const QString& query);

/// One line of the list: a group header, a pack or the fixed "Default resources" row.
struct ListEntry {
    enum class Kind { Header, Pack, Default };
    Kind kind = Kind::Pack;
    int row = -1;       ///< Pack: index into the rows
    QString text;       ///< Header: "On - top wins" / "Available"; Default: "Default resources"
    int onIndex = -1;   ///< Pack in the ON group: its place (0 = top); -1 otherwise
};
QString onHeaderText();       ///< "On - top wins" (the page paints it in capitals)
QString availableHeaderText();///< "Available"
QString defaultRowName();     ///< "Default resources"
QString defaultRowNote();     ///< "The game's own textures: the last pack in line"

/// The list. `ordered` false (Minecraft 1.12 and older, an options.txt that cannot be read): one flat list by name, no groups, no default
/// row. Otherwise ON (the active packs, top first, then "Default resources" unless Off or a search hides it) and AVAILABLE (by name, the
/// hidden files last); a group without lines has no header. The filter and the search narrow both groups.
QList<ListEntry> layoutList(const QList<PackRow>& rows, Filter filter, const QString& query, bool ordered);

struct Counts {
    int total = 0;
    int on = 0;
    int hidden = 0;
};
Counts countOf(const QList<PackRow>& rows);
/// "12 packs - 3 on", "1 pack - 0 on", "5 of 12 packs" (narrowed), "12 packs" (`ordered` false)
QString captionText(const Counts& counts, int shown, bool narrowed, bool ordered);
/// "3 selected"
QString selectionText(int selected);

/// The pills of a row in display order: the provider or "Local file", "Folder", "Hidden from the game", "Made for 1.20" (warning).
QList<Mods::Installed::PillSpec> pillsFor(const PackRow& row);
/// The tooltip of a row: the file name, the provider's name, the format note.
QString rowToolTip(const PackRow& row);

// ---- moving ---------------------------------------------------------------------------------------------------------------------

/// `names` with the entry at `from` moved so that it ends at index `to` (both 0-based, the result has the same size). Out of range = unchanged.
QStringList moved(const QStringList& names, int from, int to);

QString turnedOnText(const QString& name);        ///< "Turned on Faithful"
QString turnedOffText(const QString& name);       ///< "Turned off Faithful"
QString movedToTopText(const QString& name);      ///< "Moved Faithful to the top"
QString movedUpText(const QString& name);
QString movedDownText(const QString& name);
QString movedToBottomText(const QString& name);
QString movedText(const QString& name);           ///< "Moved Faithful" (a drag)
QString restoredText();                           ///< "Restored the previous order"
QString addedAndOnText(const QString& pack, const QString& instance);  ///< "Added Faithful to Test and turned it on"

}  // namespace Packs
}  // namespace Content
}  // namespace Zero
