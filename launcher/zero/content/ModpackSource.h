// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the provider-neutral side of the Discover > Modpacks section (docs/zero/CONTENT.md, "Modpacks").
//
//  - `ModpackItem`: one modpack as the cards and the details panel show it. Modrinth and CurseForge items wrap the
//    `ModPlatform::IndexedPack` Prism's parsers produced (details are merged into it); Technic items carry the
//    `TechnicPackDetails` of `TechnicMeta`.
//  - `ModpackSource`: the small async interface the section talks to (search / details / versions / changelog). Every request
//    carries a GENERATION token that the answer echoes, so the caller can drop an answer for a search or a selection that has
//    been replaced. Failures are signals (rendered inline), never dialogs. The production implementations over Prism's APIs
//    are in ModpackSources.h; tests derive fakes.
//  - `ResultPager`: the pure paging rules of the infinite scroll (offsets, no duplicate page, end of results).
//  - pure presentation helpers (Minecraft range text, download counts, loader labels, installed keys).
//
// No widgets, no APPLICATION, no network in this file.
#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

#include "modplatform/ModIndex.h"
#include "modplatform/ResourceAPI.h"
#include "zero/content/ContentFilter.h"
#include "zero/content/TechnicMeta.h"

namespace Zero::Content {

/// One modpack in the Discover list.
struct ModpackItem {
    Provider provider = Provider::Modrinth;
    QString id;       ///< Modrinth project id, CurseForge numeric id, Technic slug
    QString slug;     ///< Technic slug / Modrinth slug / CurseForge slug (website links)
    QString name;
    QString author;   ///< first author, empty when unknown
    QString summary;  ///< the provider's short description, plain text
    QString iconUrl;
    QString artworkUrl;  ///< featured gallery image, else the first screenshot; empty = none (the card shows the icon tile)
    QStringList gameVersions;
    QStringList loaderNames;
    QStringList categoryIds;
    qint64 downloads = -1;
    QDateTime updated;
    QString websiteUrl;
    bool detailsLoaded = false;  ///< the provider's project details were merged in (long description, gallery, license ...)

    /// Modrinth / CurseForge: every field the provider sent. Null for Technic.
    ModPlatform::IndexedPack::Ptr pack;
    /// Technic: the pack details once loaded.
    std::optional<TechnicPackDetails> technic;

    /// "modrinth:<id>" - the key used for duplicate detection and as the identity of a card.
    QString key() const;
};
using ModpackItemPtr = std::shared_ptr<ModpackItem>;
// The same types serve every kind of Discover section (Modpacks, Mods ...): new code says "content".
using ContentItem = ModpackItem;
using ContentItemPtr = ModpackItemPtr;

/// One pack version in the details' Versions tab.
struct ModpackVersion {
    QString id;       ///< provider file / version id (Technic: the build name)
    QString name;     ///< display name
    QString number;   ///< version number when different from the name
    enum class Type { Unknown, Release, Beta, Alpha } type = Type::Unknown;
    QStringList gameVersions;
    QStringList loaders;  ///< lower-case loader names
    QDateTime date;
    qint64 size = -1;
    qint64 downloads = -1;
    QString changelog;
    bool changelogLoaded = false;
    bool recommended = false;  ///< Technic Solder
    QString downloadUrl;
};

using ContentVersion = ModpackVersion;

/// Narrows the versions request of a source (Mods: the target instance's Minecraft version and loaders). Empty = every version,
/// which is what modpacks ask for.
struct VersionFilter {
    QStringList mcVersions;                ///< empty = any Minecraft version
    ModPlatform::ModLoaderTypes loaders{};  ///< none = any loader

    bool isEmpty() const { return mcVersions.isEmpty() && loaders == ModPlatform::ModLoaderTypes(); }
    bool operator==(const VersionFilter& other) const { return mcVersions == other.mcVersions && loaders == other.loaders; }
};

// ---- item construction (pure) ----

/// A Modrinth / CurseForge search hit or project as an item.
ModpackItemPtr itemFromPack(Provider provider, const ModPlatform::IndexedPack& pack);
/// A Technic `trending` / `search` row.
ModpackItemPtr itemFromTechnicSummary(const TechnicPackSummary& summary);
/// Takes the Technic pack details into `item` (author, description, Minecraft version, icon, artwork ...).
void applyTechnicDetails(ModpackItem& item, const TechnicPackDetails& details);
/// Merges a project-details answer into `item->pack` (ModPlatform::mergeProjectDetails keeps authors and presentation fields
/// the details lack) and refreshes the item's presentation fields.
void applyProjectDetails(ModpackItem& item, const ModPlatform::IndexedPack& details);
/// The version list as the Versions tab shows it.
ModpackVersion versionFromIndexed(const ModPlatform::IndexedVersion& version);

// ---- presentation helpers (pure) ----

/// "845", "1.2K", "845K", "12.3M", "123M", "1.4B": real counts only (negative = empty string).
QString formatCount(qint64 count);
/// "1.20.1" for one release, "1.18 – 1.20.1" for a range of releases (lowest – highest); snapshots and pre-releases are ignored;
/// empty when there is no release version.
QString minecraftRangeText(const QStringList& gameVersions);
/// Display names of the pack loaders in a stable order ("Fabric", "Forge", "NeoForge", "Quilt"); other tags (iris, datapack ...) are skipped.
QStringList loaderLabels(const QStringList& loaderNames);
/// Display names of the shader platforms in a stable order ("Iris", "OptiFine", "Canvas"); "vanilla" and every other tag are skipped.
QStringList shaderPlatformLabels(const QStringList& loaderNames);
/// Provider badge text: "Modrinth", "CurseForge", "Technic".
QString providerBadge(Provider provider);
/// The managed pack type Prism stores for instances created from `provider` ("modrinth", "flame"; "technic" for Technic).
QString managedPackType(Provider provider);
/// "<type>:<id>" - what an instance's getManagedPackType() / getManagedPackID() form; installedKey(item) is the same for a card.
QString managedKey(const QString& type, const QString& id);
QString installedKey(const ModpackItem& item);
/// The pack's license as shown: the Modrinth id with the common SPDX names spelled out ("MIT", "All Rights Reserved"), empty when unknown.
QString licenseText(const QString& license);
/// The long description as HTML: Modrinth's markdown body converted, CurseForge's HTML body as is, the short summary when there is no body.
QString overviewHtml(const ModpackItem& item);

/// Client-side facts of an item for ContentFilter::matchesClient.
ItemFacts factsOf(const ModpackItem& item, const QSet<QString>& installedKeys);

// ---- paging (pure) ----

/// The infinite scroll's rules. One pager per search; a new search = reset() with a new generation, which makes every answer of
/// the old one stale. A page is accepted only for the current generation, for the offset it was asked at, and once.
class ResultPager {
   public:
    static constexpr int kDefaultPageSize = 25;

    explicit ResultPager(int pageSize = kDefaultPageSize) : m_pageSize(pageSize) {}

    /// Starts over for `generation` (nothing loaded, not loading, not ended).
    void reset(quint64 generation);
    quint64 generation() const { return m_generation; }
    int pageSize() const { return m_pageSize; }
    void setPageSize(int pageSize) { m_pageSize = pageSize > 0 ? pageSize : kDefaultPageSize; }

    /// The next page can be requested now: not loading, not ended, the previous page did not fail.
    bool canLoadMore() const { return !m_loading && !m_ended && !m_failed; }
    /// The offset the next page starts at (= the number of provider rows received so far).
    int nextOffset() const { return m_offset; }
    bool isLoading() const { return m_loading; }
    bool isEnded() const { return m_ended; }
    bool hasFailed() const { return m_failed; }
    /// A page was requested; false (and nothing changes) when canLoadMore() is false.
    bool begin();
    /// The answer for `generation` / `offset` with `rawCount` provider rows. False when it is stale (other generation), a duplicate
    /// or out-of-order page (offset != nextOffset()), or nothing was requested; the state is then untouched.
    bool accept(quint64 generation, int offset, int rawCount);
    /// The request failed (only the current generation counts). The page can be asked again with retry().
    bool fail(quint64 generation);
    /// Allows begin() again after a failure.
    void retry() { m_failed = false; }
    /// Ends the list without asking again (providers that have one page).
    void finish() { m_ended = true; m_loading = false; }

   private:
    int m_pageSize;
    quint64 m_generation = 0;
    int m_offset = 0;
    bool m_loading = false;
    bool m_ended = false;
    bool m_failed = false;
};

// ---- the source interface ----

class ModpackSource : public QObject {
    Q_OBJECT
   public:
    using QObject::QObject;
    ~ModpackSource() override = default;

    virtual Provider provider() const = 0;
    /// Rows per page (25 for Modrinth and CurseForge).
    virtual int pageSize() const { return ResultPager::kDefaultPageSize; }
    /// False when the provider has one page only (Technic): the section never asks for more.
    virtual bool pages() const { return true; }

    /// Searches with `args` (ContentFilter::toSearchArgs: offset, query, sort, facets). Answers searchFinished() or searchFailed().
    virtual void search(const ResourceAPI::SearchArgs& args, quint64 generation) = 0;
    /// Loads the long description, gallery and links of `item` and merges them into it. Answers detailsFinished() / detailsFailed().
    virtual void details(const ModpackItemPtr& item, quint64 generation) = 0;
    /// Loads the versions of `item`. Answers versionsFinished() / versionsFailed(); a provider that never answers is covered by
    /// the source's own timeout (versionsFailed "timed out").
    virtual void versions(const ModpackItemPtr& item, quint64 generation) = 0;
    /// Loads the versions of `item` under `filter` instead of the source's own versionFilter() (the install cart reviews items against
    /// ITS target instance, which need not be the one the section is filtered for). Answers versionsForFinished() with the raw
    /// versions, or versionsFailed(); it never touches `item->pack->versions` / `versionsLoaded` (that cache belongs to the section's
    /// filter). The default fails at once.
    virtual void versionsFor(const ModpackItemPtr& item, quint64 generation, const VersionFilter& filter)
    {
        Q_UNUSED(item)
        Q_UNUSED(filter)
        emit versionsFailed(generation, QStringLiteral("Not supported."));
    }
    /// Loads the changelog of one version, on demand. Answers changelogFinished() / changelogFailed().
    virtual void changelog(const ModpackItemPtr& item, const QString& versionId, quint64 generation) = 0;
    /// Loads the provider's modpack categories (once). Answers categoriesReady(); categories() holds them afterwards.
    virtual void requestCategories() {}
    QList<ModPlatform::Category> categories() const { return m_categories; }
    /// The filter the next versions() requests carry (a request already running keeps the one it started with).
    void setVersionFilter(const VersionFilter& filter) { m_versionFilter = filter; }
    const VersionFilter& versionFilter() const { return m_versionFilter; }

   signals:
    /// `offset` is the offset the page was requested at, `rawCount` the rows the provider returned (before de-duplication).
    void searchFinished(quint64 generation, int offset, const QList<Zero::Content::ModpackItemPtr>& items, int rawCount);
    void searchFailed(quint64 generation, const QString& error);
    void detailsFinished(quint64 generation, const Zero::Content::ModpackItemPtr& item);
    void detailsFailed(quint64 generation, const QString& error);
    void versionsFinished(quint64 generation, const Zero::Content::ModpackItemPtr& item, const QList<Zero::Content::ModpackVersion>& versions);
    void versionsFailed(quint64 generation, const QString& error);
    void versionsForFinished(quint64 generation, const Zero::Content::ModpackItemPtr& item, const QList<ModPlatform::IndexedVersion>& versions);
    void changelogFinished(quint64 generation, const QString& versionId, const QString& changelog);
    void changelogFailed(quint64 generation, const QString& versionId, const QString& error);
    void categoriesReady();

   protected:
    QList<ModPlatform::Category> m_categories;

   private:
    VersionFilter m_versionFilter;
};

/// The sources the section may use; a null entry = that provider is unavailable (CurseForge without an API key).
struct ModpackSourceSet {
    ModpackSource* modrinth = nullptr;
    ModpackSource* curseforge = nullptr;
    ModpackSource* technic = nullptr;

    ModpackSource* source(Provider provider) const;
    bool has(Provider provider) const { return source(provider) != nullptr; }
};

/// Facets of the Modpacks section for `provider`, with the providers `sources` does not have removed from the Provider facet
/// (no CurseForge anywhere without it).
QList<Facet> modpackFacets(const FacetContext& ctx, const ModpackSourceSet& sources);

}  // namespace Zero::Content

Q_DECLARE_METATYPE(Zero::Content::ModpackItemPtr)
Q_DECLARE_METATYPE(QList<Zero::Content::ModpackItemPtr>)
Q_DECLARE_METATYPE(Zero::Content::ModpackVersion)
Q_DECLARE_METATYPE(QList<Zero::Content::ModpackVersion>)
