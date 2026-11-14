// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Create Instance core (docs/zero/CONTENT.md, "Create Instance"): Minecraft version rows and their filters,
// mod loader availability for a Minecraft version, loader version rows, the name suggestion and validation, and the import
// source rules. Plain values only: no widgets, no APPLICATION, no network.
//
// Metadata rules (Prism meta format, verified against meta.prismlauncher.org):
//  - Forge (`net.minecraftforge`), NeoForge (`net.neoforged`) and LiteLoader (`com.mumfrey.liteloader`) versions require
//    `net.minecraft` with `equals` = the Minecraft version they run on.
//  - Fabric (`net.fabricmc.fabric-loader`) and Quilt (`org.quiltmc.quilt-loader`) versions require only
//    `net.fabricmc.intermediary` (no Minecraft version): any loader version runs on every Minecraft version that has
//    intermediary mappings. Each intermediary version requires `net.minecraft` `equals` its own id, so the set of
//    intermediary ids is the set of Minecraft versions Fabric and Quilt support.
#pragma once

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QUrl>

#include <optional>

#include "meta/Version.h"

namespace Zero {
namespace Create {

inline constexpr int kMaxNameLength = 128;

inline const QString kMinecraftUid = QStringLiteral("net.minecraft");
inline const QString kIntermediaryUid = QStringLiteral("net.fabricmc.intermediary");

// ---------------------------------------------------------------------------------------------------------------------
// Minecraft versions
// ---------------------------------------------------------------------------------------------------------------------

/// Kind of a Minecraft version, from the meta `type` field. `old_snapshot` (2013-era snapshots) counts as Snapshot.
enum class VersionType { Release, Snapshot, Beta, Alpha, Experiment, Other };
VersionType versionTypeFromMeta(const QString& metaType);
/// Pill text: "Release", "Snapshot", "Beta", "Alpha", "Experiment"; empty for Other.
QString versionTypeLabel(VersionType type);

/// The chips above the version list. Old = beta + alpha.
enum class VersionFilter { Releases, Snapshots, Old, Experiments };
QList<VersionFilter> versionFilters();
QString versionFilterLabel(VersionFilter filter);
bool filterAccepts(VersionFilter filter, VersionType type);
/// The chip that shows versions of `type` (Releases for Other).
VersionFilter filterFor(VersionType type);

/// One `net.minecraft` version as read from the meta list.
struct MinecraftVersionFact {
    QString id;
    QString type;  ///< meta type: release, snapshot, old_snapshot, old_beta, old_alpha, experiment
    QDateTime released;
    bool recommended = false;
};

struct VersionRow {
    QString id;
    VersionType type = VersionType::Other;
    QDateTime released;
    bool recommended = false;
    bool latest = false;  ///< the newest release
};

/// Rows newest first (release time; equal times keep the input order). `latest` marks the newest release.
QList<VersionRow> buildVersionRows(const QList<MinecraftVersionFact>& facts);
/// Rows the filter accepts whose id contains `search` (trimmed, case-insensitive; empty = all of the filter).
QList<VersionRow> filterVersionRows(const QList<VersionRow>& rows, VersionFilter filter, const QString& search);
/// Index of the row to preselect: the newest recommended row, else the first row; -1 when `rows` is empty.
int preferredVersionRow(const QList<VersionRow>& rows);
/// Index of the row with `id`, or -1.
int indexOfVersion(const QList<VersionRow>& rows, const QString& id);

// ---------------------------------------------------------------------------------------------------------------------
// Mod loaders
// ---------------------------------------------------------------------------------------------------------------------

/// Display order: Fabric, Forge, NeoForge, Quilt, LiteLoader.
enum class Loader { Fabric, Forge, NeoForge, Quilt, LiteLoader };
QList<Loader> loaderOrder();
QString loaderUid(Loader loader);
/// "Fabric", "Forge", "NeoForge", "Quilt", "LiteLoader" (the names Prism's CustomPage uses in the suggested name).
QString loaderName(Loader loader);
std::optional<Loader> loaderFromUid(const QString& uid);
/// Case-insensitive name ("fabric", "neoforge", ...).
std::optional<Loader> loaderFromName(const QString& name);
/// Fabric and Quilt: availability comes from the intermediary list, not from a Minecraft requirement.
bool usesIntermediary(Loader loader);
/// Every meta list Modded needs: net.minecraft, the intermediary and the five loaders.
QStringList moddedMetaUids();

/// One loader version as read from the meta list.
struct LoaderVersionFact {
    QString uid;
    QString version;
    QString requiresMinecraft;  ///< `equals` of its net.minecraft requirement; empty when it has none (Fabric, Quilt)
    bool recommended = false;
    QDateTime released;
};

struct LoaderVersionRow {
    QString version;
    QDateTime released;
    bool recommended = false;
};

/// Index of the loader version to preselect: the newest recommended row, else the first; -1 when empty.
int preferredLoaderRow(const QList<LoaderVersionRow>& rows);

/// The loader metadata Modded works from. A loader is "known" once its list (and, for Fabric and Quilt, the intermediary
/// list) has been handed in; a loader that is not known yet is neither available nor unavailable.
class LoaderCatalog {
   public:
    struct Availability {
        QList<Loader> available;    ///< at least one version runs on the Minecraft version (display order)
        QList<Loader> unavailable;  ///< known, but nothing for this Minecraft version
        QList<Loader> unknown;      ///< metadata not loaded (yet)
    };

    void setLoaderVersions(Loader loader, const QList<LoaderVersionFact>& facts);
    void clearLoader(Loader loader);
    /// Minecraft versions that have intermediary mappings.
    void setIntermediaryVersions(const QSet<QString>& minecraftIds);
    void clearIntermediary();

    bool isKnown(Loader loader) const;
    /// Versions of `loader` that run on `minecraft`, newest first. Empty when unknown or unavailable.
    QList<LoaderVersionRow> versionsFor(Loader loader, const QString& minecraft) const;
    bool hasVersionsFor(Loader loader, const QString& minecraft) const;
    Availability availability(const QString& minecraft) const;

   private:
    QMap<Loader, QList<LoaderVersionFact>> m_facts;
    QSet<QString> m_intermediary;
    bool m_intermediaryKnown = false;
};

/// "Not available for 1.2.5: Fabric, Quilt"; empty when `loaders` is empty.
QString unavailableCaption(const QString& minecraft, const QList<Loader>& loaders);

// ---------------------------------------------------------------------------------------------------------------------
// Meta adapters: plain facts from Prism's meta versions
// ---------------------------------------------------------------------------------------------------------------------

QList<MinecraftVersionFact> minecraftFacts(const QList<Meta::Version::Ptr>& versions);
QList<LoaderVersionFact> loaderFacts(const QList<Meta::Version::Ptr>& versions);
/// Minecraft ids of the intermediary versions (their net.minecraft `equals`, or the version id when there is none).
QSet<QString> intermediaryMinecraftIds(const QList<Meta::Version::Ptr>& versions);

// ---------------------------------------------------------------------------------------------------------------------
// Name
// ---------------------------------------------------------------------------------------------------------------------

/// "1.20.1" or "1.20.1 Fabric" (CustomPage's rule); empty without a Minecraft version.
QString suggestName(const QString& minecraft, std::optional<Loader> loader = std::nullopt);
/// Empty when `name` is valid (trimmed: not empty, at most kMaxNameLength characters), otherwise the inline error.
QString validateName(const QString& name);

// ---------------------------------------------------------------------------------------------------------------------
// Import (ImportPage's rules)
// ---------------------------------------------------------------------------------------------------------------------

struct ImportSource {
    enum class Kind {
        None,            ///< nothing entered
        LocalFile,       ///< an existing .zip (by MIME type) or .mrpack
        RemoteUrl,       ///< an http(s) URL
        CurseForgeLink,  ///< curseforge:// (needs the classic importer's lookup)
        Invalid          ///< see `error`
    };
    Kind kind = Kind::None;
    QUrl url;               ///< what InstanceImportTask receives
    QString suggestedName;  ///< file base name
    QString fileName;       ///< file name shown in the preview
    QString error;

    bool isImportable() const { return kind == Kind::LocalFile || kind == Kind::RemoteUrl; }
};

/// A local file: it must exist and be a zip (by MIME type, so non-latin names work) or a .mrpack.
ImportSource resolveLocalFile(const QString& path);
/// Text of the URL field: http(s) (a CurseForge `?client=y` download link becomes `/file`), curseforge:// or invalid.
ImportSource resolveUrl(const QString& text);
/// Format named by the file name alone: "Modrinth pack" for .mrpack, otherwise empty (detected when importing).
QString detectedFormat(const QString& fileName);

}  // namespace Create
}  // namespace Zero
