// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Mod Intelligence: own parser of mod jar metadata (fabric.mod.json, quilt.mod.json, META-INF/mods.toml,
// META-INF/neoforge.mods.toml, mcmod.info, nested jars). Unlike Prism's LocalModParseTask it keeps version ranges, breaks / conflicts /
// incompatible declarations, `provides` and nested jars, which the health analysis needs.
// The whole module is read-only: jars are read through libarchive, nested jars are parsed from memory, nothing is extracted or written.
// The format parsers take the raw file contents, so they are testable without jars.
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <vector>

namespace Zero {
namespace Mods {

enum class LoaderFormat { Unknown, Fabric, Quilt, Forge, NeoForge, LegacyForge };

/// "fabric" "quilt" "forge" "neoforge" "legacy-forge" "unknown".
QString loaderFormatId(LoaderFormat format);

/// A requirement of one mod on another id. The meaning of `versionPredicate` depends on the format of the entry that holds it:
/// Fabric / Quilt predicate syntax (alternatives joined with "||") or Maven range for Forge / NeoForge / LegacyForge.
/// Empty = any version. A predicate that could not be turned into text is stored as "?" and always matches Unknown.
struct Dependency {
    QString id;
    QString versionPredicate;
    bool mandatory = true;  ///< false for entries in `recommends` that came from a Forge / NeoForge "optional" dependency
    QString side;           ///< Forge / NeoForge: "BOTH" "CLIENT" "SERVER" (upper case), empty when not stated
    QString unless;         ///< Quilt: the requirement / break is waived when a mod with this id is present
};

struct ProvidedId {
    QString id;
    QString version;  ///< empty = the version of the providing mod
};

/// One mod declared by a jar. A jar may declare several (several `[[mods]]`), and several formats (a jar that carries both a
/// fabric.mod.json and a mods.toml gives one entry per format).
struct ModEntry {
    LoaderFormat format = LoaderFormat::Unknown;
    QString id;
    QString version;  ///< empty = unknown (placeholder that could not be resolved, or absent)
    QString name;
    QList<ProvidedId> provides;
    QList<Dependency> depends;     ///< mandatory, checked by the loader
    QList<Dependency> recommends;  ///< optional / recommended (Fabric recommends, Quilt optional, Forge mandatory=false, NeoForge optional)
    QList<Dependency> breaks;      ///< Fabric / Quilt breaks, NeoForge "incompatible"
    QList<Dependency> conflicts;   ///< Fabric conflicts, NeoForge "discouraged"

    QString displayName() const { return name.isEmpty() ? id : name; }
};

/// Everything read from one jar (top level or nested).
struct ModMetadata {
    QString fileName;                       ///< file name of the jar ("sodium.jar"); for a nested jar the last component of its path
    QString path;                           ///< full path of a top-level jar; for a nested jar the path inside the parent
    QList<ModEntry> mods;                   ///< empty = no recognizable metadata
    std::vector<ModMetadata> nested;        ///< jars bundled inside (Fabric / Quilt `jars`, Forge / NeoForge jarjar), nesting depth <= 3
    QString parseError;                     ///< non-empty when a metadata file was found but could not be read, or the archive is unreadable
    QList<LoaderFormat> unparsedFormats;    ///< formats whose metadata file exists but did not parse

    /// Formats present, in the order Fabric, Quilt, NeoForge, Forge, LegacyForge.
    QList<LoaderFormat> formats() const;
    bool hasFormat(LoaderFormat f) const;
    /// First format in `formats()`; Unknown when there are no entries.
    LoaderFormat loaderFormat() const;
    /// First entry's id (convenience for tests / UI); empty when there are no entries.
    QString primaryId() const { return mods.isEmpty() ? QString() : mods.first().id; }
};

/// Result of one metadata file.
struct ParsedFile {
    QList<ModEntry> mods;
    QStringList nestedJars;  ///< paths inside the jar (Fabric / Quilt `jars`, jarjar metadata)
    QString error;           ///< empty when the file parsed (even if it declared no mod)
};

ParsedFile parseFabricModJson(const QByteArray& contents);
ParsedFile parseQuiltModJson(const QByteArray& contents);
/// `format` is Forge (META-INF/mods.toml) or NeoForge (META-INF/neoforge.mods.toml). `${file.jarVersion}` resolves to
/// `manifestImplementationVersion` (empty = unknown, the entry then has no version).
ParsedFile parseModsToml(const QByteArray& contents, LoaderFormat format, const QString& manifestImplementationVersion = QString());
ParsedFile parseMcmodInfo(const QByteArray& contents);
/// META-INF/jarjar/metadata.json: only `nestedJars` is filled.
ParsedFile parseJarJarMetadata(const QByteArray& contents);
/// Main-section `Implementation-Version` of a META-INF/MANIFEST.MF (continuation lines joined); empty when absent.
QString parseManifestImplementationVersion(const QByteArray& manifest);

/// Reads a jar from disk (read-only). Never throws; problems end up in `parseError`.
ModMetadata parseJar(const QString& path);
/// Same for jar bytes held in memory (used for nested jars). `fileName` is only stored.
ModMetadata parseJarBytes(const QByteArray& bytes, const QString& fileName = QString());

inline constexpr int kMaxNestingDepth = 3;

}  // namespace Mods
}  // namespace Zero
