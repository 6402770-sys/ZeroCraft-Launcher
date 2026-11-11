// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Library filter chips and text search as pure logic (no widgets, no instances). See docs/zero/SHELL.md.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace Zero {
namespace LibraryFilter {

enum class Kind { All, Favorites, Running, Loader, Tag };

/// The single-select chip. `value` is the loader ("Vanilla", "Fabric", ...) or the tag, empty for the other kinds.
struct Chip {
    Kind kind = Kind::All;
    QString value;
    bool operator==(const Chip& other) const;
    bool operator!=(const Chip& other) const { return !(*this == other); }
};

/// What the filter knows about one instance.
struct Facts {
    QString name;
    QString group;
    QString version;  ///< Minecraft version
    QString loader;   ///< empty = vanilla
    QStringList tags;
    bool favorite = false;
    bool running = false;
};

/// Loader name as shown on a chip: "Vanilla" for an empty loader.
QString loaderLabel(const QString& loader);

/// Case-insensitive substring search over name, group, version and tags. An empty (or blank) text matches everything.
bool textMatches(const QString& text, const Facts& facts);
bool chipMatches(const Chip& chip, const Facts& facts);
/// Both, combined with AND.
bool matches(const Chip& chip, const QString& text, const Facts& facts);

struct ChipInfo {
    Chip chip;
    QString label;  ///< "All", "Favorites", "Running", "Fabric", "#pvp"
    int count = 0;  ///< instances the chip would show
};

/// Chips for the current library: All, Favorites and Running always; then one per loader present (Vanilla, Fabric, Forge,
/// NeoForge, Quilt, then any other loader alphabetically) and one per tag present (alphabetical, spelled as first seen).
/// Counts are over `all`.
QList<ChipInfo> buildChips(const QList<Facts>& all);

/// `active` when the chip list still offers it, otherwise the All chip (e.g. the last instance with a tag lost it).
Chip resolveChip(const Chip& active, const QList<ChipInfo>& chips);

}  // namespace LibraryFilter
}  // namespace Zero
