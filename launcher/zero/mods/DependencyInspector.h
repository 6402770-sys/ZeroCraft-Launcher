// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Mod Intelligence: the Dependency Inspector of the instance window's Mods page (docs/zero/CONTENT-MODS.md,
// "Dependency Inspector"). For every mod file of an instance it answers what the mod needs, what game it was made for, what it
// recommends, what it conflicts with and which installed mods need it. Pure (no widgets, no I/O): the input is the parsed jars.
//
// The rules are the ones `ModHealth::analyze` uses (both go through `ModResolver`: which jars load, provider index with `provides` and
// bundled jars, environment versions, predicate matching, Quilt `unless`, Forge `side`), so the page and Tools > Mod Health never
// disagree: a requirement Mod Health reports as Confirmed missing is `Missing` here, a version it rejects is `WrongVersion`. Whatever
// Mod Health would only call Possible (an unreadable jar may provide it, the version cannot be compared) is `Unknown` here.
#pragma once

#include <QHash>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

#include "zero/mods/ModHealth.h"
#include "zero/mods/ModMetadata.h"

namespace Zero {
namespace Mods {

enum class DepState {
    Installed,     ///< an enabled mod with an accepted version provides it
    Bundled,       ///< a jar bundled inside an enabled mod provides it
    Disabled,      ///< only a disabled file provides it
    WrongVersion,  ///< an enabled mod provides it, but its version is outside the accepted range
    Missing,       ///< nothing provides it and nothing unreadable could
    Unknown,       ///< ZERO cannot tell: the version rule or the version is not understood, or an unreadable jar may provide it
    NotInstalled   ///< optional / conflicting mods only: no enabled mod provides it
};

/// "installed" "bundled" "disabled" "wrong-version" "missing" "unknown" "not-installed".
QString depStateId(DepState state);

/// One mod a file needs, recommends or conflicts with.
struct DepEntry {
    QString id;
    QString name;       ///< the found mod's name; the id when nothing was found
    QString predicate;  ///< plain-language version rule ("1.0 or newer"); "" = any version; "?" = ZERO could not read the rule
    DepState state = DepState::Unknown;
    QString version;    ///< version of the mod that was found ("" = none found or unknown)
    QString fileName;   ///< the file that carries it: its own file, the file it is bundled in, or the disabled file ("" = none)
    QString path;       ///< full path of that file
    QString forBundled; ///< needs only: the display name of the bundled jar that asks for it ("" = the mod itself)
};

enum class GameKind { Minecraft, Loader, Java };
enum class GameState { Ok, Mismatch, Unknown };

/// What a mod asks of the game (Minecraft version, the loader, Java) and whether the instance fits.
struct GameEntry {
    GameKind kind = GameKind::Minecraft;
    QString id;         ///< "minecraft" "fabricloader" "quilt_loader" "forge" "neoforge" "java"
    QString name;       ///< "Minecraft" "Fabric Loader" "Quilt Loader" "Forge" "NeoForge" "Java"
    QString predicate;  ///< as DepEntry::predicate
    GameState state = GameState::Unknown;  ///< Java is always Unknown: ZERO does not check it
    QString version;    ///< what the instance runs ("" = unknown)
};

struct NeededBy {
    QString fileName;
    QString path;
    QString name;
    bool enabled = true;
};

/// Everything known about one mod file.
struct ModInspection {
    QString fileName;
    QString path;
    QString name;        ///< the mod's name (the file name without suffix when the jar declares none)
    QString stamp;       ///< "<size>/<mtime ms>" of the file when it was read (filled by the scanner; "" in a pure `inspect`)
    bool enabled = true;
    bool readable = true;  ///< false: no metadata / metadata that did not parse: only `neededBy` is filled
    bool loads = true;     ///< false: the jar is made for another loader (its lists are what it declares, for information)

    QList<DepEntry> needs;      ///< mandatory dependencies minus environment ids (those are in `game`)
    QList<GameEntry> game;
    QList<DepEntry> optional;   ///< recommends
    QList<DepEntry> conflicts;  ///< conflicts with INSTALLED mods only (breaks + conflicts, plus mods that declare a conflict with this one)
    QStringList otherConflicts; ///< ids of the declared conflicts with mods that are not installed (a caption, not rows)
    QList<NeededBy> neededBy;   ///< files whose mandatory dependency resolves to this file
};

struct Inspection {
    QList<ModInspection> mods;  ///< enabled files first (by file name), then disabled ones
    QHash<QString, int> byPath;  ///< path -> index into `mods`

    const ModInspection* find(const QString& path) const;
    bool isEmpty() const { return mods.isEmpty(); }
};

/// The problems of one enabled mod that the Mods page shows as pills. Names are display names, in the order of the file's lists.
struct Problems {
    QStringList missing;       ///< `needs` in state Missing
    QStringList disabled;      ///< `needs` in state Disabled
    QStringList wrongVersion;  ///< `needs` in state WrongVersion
    QStringList conflicts;     ///< `conflicts` in state Installed
    bool any() const { return !missing.isEmpty() || !disabled.isEmpty() || !wrongVersion.isEmpty() || !conflicts.isEmpty(); }
};

/// Empty for an unreadable file and for one that does not load (nothing it declares matters then).
Problems problemsOf(const ModInspection& mod);

/// Pure. `enabled` / `disabled` are the parsed top-level jars (`fileName`, `path` set by `parseJar`); `context` is what the instance runs.
Inspection inspect(const QList<ModMetadata>& enabled, const QList<ModMetadata>& disabled, const InstanceContext& context);

}  // namespace Mods
}  // namespace Zero

Q_DECLARE_METATYPE(Zero::Mods::Inspection)
