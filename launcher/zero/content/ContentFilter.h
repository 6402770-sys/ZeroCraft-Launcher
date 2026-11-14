// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the ZERO Filter System core (docs/zero/CONTENT.md, "ZERO Filter System"): facets per Discover section and
// provider, the filter state and its chips, the mapping of a state to ResourceAPI::SearchArgs, and the client-side predicate.
// Pure logic: no widgets, no APPLICATION, no network.
//
// Semantics: values inside one Multi facet are OR-ed, different facets are AND-ed. Server facets are sent to the provider
// (a change needs a new search); Client facets filter the loaded rows. A facet the provider cannot apply carries a
// `disabledReason`: its selected values are kept (and shown as inactive chips) but never applied.
//
// Facet values:
//  - provider: "modrinth" | "curseforge" | "technic" (Single; Technic only for modpacks; default Modrinth)
//  - mcVersion: Minecraft version strings ("1.20.1")
//  - loader (modpacks, mods): "fabric" | "forge" | "neoforge" | "quilt"
//  - platform (shaders): "iris" | "optifine" | "canvas" | "vanilla" (Modrinth loader tags)
//  - worlds (CurseForge only, no provider facet): Minecraft version + category
//  - category / feature: provider category ids (ModPlatform::Category::id: Modrinth slugs, CurseForge numeric ids)
//  - resolution: Modrinth resolution tags ("8x-", "16x", ... "512x+")
//  - performance: Modrinth performance tags ("potato", "low", "medium", "high", "screenshot")
//  - environment (mods): "client" | "server" | "both" (Single)
//  - installed / updates / hideInstalled: Toggle facets, selected when their value list holds kToggleOn
#pragma once

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

#include "modplatform/ModIndex.h"
#include "modplatform/ResourceAPI.h"

namespace Zero::Content {

enum class Kind { Modpack, Mod, ResourcePack, Shader, World };
enum class Provider { Modrinth, CurseForge, Technic };
enum class FacetKind { Single, Multi, Toggle };
enum class FacetSide { Server, Client };

namespace FacetId {
inline const QString provider = QStringLiteral("provider");
inline const QString mcVersion = QStringLiteral("mcVersion");
inline const QString loader = QStringLiteral("loader");
inline const QString category = QStringLiteral("category");
inline const QString feature = QStringLiteral("feature");
inline const QString environment = QStringLiteral("environment");
inline const QString resolution = QStringLiteral("resolution");
inline const QString platform = QStringLiteral("platform");
inline const QString performance = QStringLiteral("performance");
inline const QString installed = QStringLiteral("installed");
inline const QString updates = QStringLiteral("updates");
inline const QString hideInstalled = QStringLiteral("hideInstalled");
}  // namespace FacetId

/// The single value of a selected Toggle facet.
inline const QString kToggleOn = QStringLiteral("on");

/// Provider ids as stored in the `provider` facet ("modrinth", "curseforge", "technic").
QString providerId(Provider provider);
std::optional<Provider> providerFromId(const QString& id);
QString providerName(Provider provider);  ///< "Modrinth", "CurseForge", "Technic"

struct FacetOption {
    QString value;
    QString label;
    QString group;  ///< optional sub-heading inside the facet ("Performance impact", "Features"...)
};

struct Facet {
    QString id;
    QString label;
    FacetKind kind = FacetKind::Multi;
    FacetSide side = FacetSide::Server;
    QList<FacetOption> options;
    QString disabledReason;  ///< non-empty = shown disabled, with this text as tooltip

    bool isEnabled() const { return disabledReason.isEmpty(); }
    /// The option label of `value`, or `value` itself when it is not an option (e.g. a version not in the list).
    QString optionLabel(const QString& value) const;
};

struct FacetContext {
    Provider provider = Provider::Modrinth;
    QList<ModPlatform::Category> categories;  ///< the provider's categories for this kind (with Modrinth `header`)
    QStringList mcVersions;                   ///< newest first; the UI supplies them (releases first)
    bool hasTarget = false;                   ///< Mods / Resource packs / Shaders: a target instance is chosen
};

/// The facets of a Discover section for a provider (docs/zero/CONTENT.md, "Facets per section"), in display order.
QList<Facet> facetsFor(Kind kind, const FacetContext& ctx);

struct FilterState {
    QString query;
    QString sortId;
    QMap<QString, QStringList> selected;  ///< facet id -> selected values (an empty list = nothing selected)

    /// Equal when query, sort and every non-empty selection match (an empty list equals a missing key).
    bool operator==(const FilterState& other) const;
    bool operator!=(const FilterState& other) const { return !(*this == other); }

    /// {"query": str, "sortId": str, "selected": {facetId: [str]}}. The caller drops the query before persisting if it
    /// should not be remembered.
    QJsonObject toJson() const;
    /// Lenient: unknown keys, wrong types, non-string and empty values are ignored.
    static FilterState fromJson(const QJsonObject& json);

    QStringList values(const QString& facetId) const;
    bool isSelected(const QString& facetId, const QString& value) const;
    /// The `provider` facet's value; Modrinth when unset or unknown.
    Provider provider() const;
};

struct Chip {
    QString facetId;  ///< empty for the query chip
    QString value;
    QString label;           ///< "Loader: Fabric", "\"text\"", "Installed"
    bool isDefault = false;  ///< the value is also part of the section defaults
    bool isQuery = false;
    bool isInactive = false;  ///< the facet is disabled for the current provider (the value is kept but not applied)
};

/// One chip per selected value of a known facet (facet order, then selection order), the query chip ("\"text\"") last.
/// The provider facet gets a chip only when it differs from the defaults' provider.
QList<Chip> chipsFor(const FilterState& state, const QList<Facet>& facets, const FilterState& defaults);
/// Removes exactly that value (or the query).
FilterState withoutChip(FilterState state, const Chip& chip);
/// As above, but a removed provider chip goes back to the defaults' provider instead of none.
FilterState withoutChip(FilterState state, const Chip& chip, const FilterState& defaults);
/// Facets and query back to the section defaults; keeps the sort.
FilterState clearAll(const FilterState& current, const FilterState& defaults);
/// Every facet and the query empty; keeps the sort and the provider.
FilterState showEverything(const FilterState& current);
/// True when the query, the sort, the provider or any Server facet differs (Client-only changes filter loaded rows).
bool needsNewSearch(const FilterState& a, const FilterState& b, const QList<Facet>& facets);

struct SortOption {
    QString id;  ///< ZERO sort id: relevance, downloads, follows, newest, updated, name, featured, popularity
    QString label;
};

/// The ZERO sort ids the provider supports, in display order (empty for Technic).
QList<SortOption> sortOptions(Kind kind, Provider provider);
/// The provider's own method for a ZERO sort id, picked from its getSortingMethods(); nullopt when it has none.
std::optional<ResourceAPI::SortingMethod> sortingFor(const QString& sortId, const QList<ResourceAPI::SortingMethod>& providerMethods);

/// The search arguments for Modrinth / CurseForge. Values a provider cannot apply are left out (see matchesClient).
/// Modrinth: loaders and versions OR-ed in their facet groups, every category-like facet one OR group in `categoryGroups`,
/// shader platforms as category tags, environment as `side`. CurseForge: only the first selected Minecraft version (its
/// search takes one), loaders as `modLoaderTypes`, categories as `categoryIds`. Technic: type, offset and query only.
/// `facets` (facetsFor for this provider) decides which category-like values are sent: a value the provider's facet does not
/// offer (picked under another provider) stays in the state, shown as inactive, but is never sent.
ResourceAPI::SearchArgs toSearchArgs(const FilterState& state,
                                     Kind kind,
                                     Provider provider,
                                     int offset,
                                     const QList<ResourceAPI::SortingMethod>& providerMethods,
                                     const QList<Facet>& facets);

/// What the client predicate knows about one loaded item.
struct ItemFacts {
    Provider provider = Provider::Modrinth;
    QStringList gameVersions;
    QStringList loaderNames;
    QStringList categories;  ///< category ids (IndexedPack::categoryIds)
    bool installed = false;
    bool updateAvailable = false;
};

/// The Client facets, plus the server facets the item's provider could not apply in full (CurseForge: Minecraft version
/// and loader, checked as "any of the selected values" - not when exactly one value was selected: the server applied it
/// and CurseForge's file index is partial). Disabled facets never filter. Unknown data (an empty list) never
/// excludes an item.
bool matchesClient(const FilterState& state, const QList<Facet>& facets, const ItemFacts& item);

/// Shader performance profile from Modrinth tags: potato/low -> "Lightweight", medium -> "Balanced", high -> "High quality",
/// screenshot -> "Cinematic"; several -> "Lightweight – High quality" (lowest – highest); none -> empty.
QString performanceLabel(const QStringList& categories);
/// The Modrinth resolution tags present in `categories`, in canonical order (8x-, 16x, 32x, 48x, 64x, 128x, 256x, 512x+).
QStringList resolutionsOf(const QStringList& categories);

}  // namespace Zero::Content
