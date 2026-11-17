// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - "Try it in a copy": what differs between a test copy and its original, part by part (docs/zero/ARCHITECTURE.md, "Test
// copies"). Pure and read-only: it only looks at file lists, sizes, modification times and, when a size matches but the time does not, at
// the bytes. No widgets, no APPLICATION.
//
// A part is something the user can bring back on its own. Mods = `mods/` (with its `.index`), Config = `config/`, Resource packs,
// Shader packs, Game version and loader = `mmc-pack.json` + `patches/` (the instance folder, not the game folder), Worlds = the copy's
// world folders (`saves/<name>` with a `level.dat`) that are new or changed. A folder that does not exist counts as an empty one.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <functional>

namespace Zero {
namespace Sandbox {

enum class Part { Mods, Config, ResourcePacks, ShaderPacks, GameVersion, Worlds };

/// "mods" | "config" | "resourcepacks" | "shaderpacks" | "gameversion" | "worlds"
QString partKey(Part part);
/// Every part, in the order the dialog lists them (Worlds last).
QList<Part> allParts();

/// Where an instance keeps its files: the instance folder and the game folder (`<instance>/minecraft` or `<instance>/.minecraft`).
struct Layout {
    QString instanceRoot;
    QString gameRoot;
    QString savesDir() const;
};

/// One file or folder of a part: its path in the original and in the copy (the same name below each instance's own root).
struct Target {
    QString original;
    QString copy;
};
/// The files / folders of a non-world part (empty for Worlds).
QList<Target> targetsOf(Part part, const Layout& original, const Layout& copy);

/// Files modified within this many ms of each other count as the same moment (FAT stores 2 s steps).
constexpr qint64 kSameTimeMs = 2000;
/// A file bigger than this whose time differs counts as changed without reading it.
constexpr qint64 kMaxReadBytes = 64ll * 1024 * 1024;

struct CompareOptions {
    /// Polled between entries; true stops the comparison (the answer is then "differs").
    std::function<bool()> cancelled;
};

/// True when the two paths hold different content. A missing path equals an empty folder; a file against a folder differs. Folders are
/// compared by their entries (names, sizes, times; the bytes when only the time differs). Links are never followed: a link equals a link
/// with the same target. Entries named `.zero-*` (ZERO staging) and `session.lock` are ignored. Stops at the first difference.
bool treesDiffer(const QString& a, const QString& b, const CompareOptions& options = {});

/// The folder names under `savesDir` that are worlds (a folder with a `level.dat`, not named `.zero-*`), sorted.
QStringList worldFolders(const QString& savesDir);
/// The copy's worlds that are new or changed against the original, sorted. A cancelled comparison returns an empty list.
QStringList changedWorlds(const Layout& original, const Layout& copy, const CompareOptions& options = {});

struct PartChange {
    Part part = Part::Mods;
    bool differs = false;
    QStringList worlds;  ///< Worlds only: the new or changed world folders
};

/// One entry per part of `allParts()`. A cancelled comparison returns an empty list.
QList<PartChange> comparePlan(const Layout& original, const Layout& copy, const CompareOptions& options = {});
/// Only the parts that differ.
QList<Part> differingParts(const QList<PartChange>& plan);

}  // namespace Sandbox
}  // namespace Zero
