// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the kind-independent core of a Discover section (docs/zero/CONTENT.md, "Discover page"; Modpacks and Mods).
//
//   [ toolbar (search, Filters, caption, sort, grid/list) ]            |
//   [ filter chips ]                                                   |  details panel (400 px, hidden until a selection)
//   [ accessory (the Modpacks featured strip) ]                        |
//   [ grid of cards / list of rows, infinite scroll ]                  |
//
// The core owns the filter state (ZERO Filter System: ContentFilter), asks the providers through ModpackSource (injected, so tests
// use fakes) and shows what comes back. Every request carries a generation token: a late answer for an older search or an older
// selection is dropped. Failures are inline states with Retry; nothing here opens a dialog or blocks.
//
// A kind (ModpacksSection) subclasses it and fills in what differs: the delegate and the reveal numbers (ContentSectionSpec), the
// facets, the defaults, where the installed state comes from, the details header and the primary action. Virtual hooks are called
// after construction only: a subclass calls init() as the last line of its constructor.
#pragma once

#include <QHash>
#include <QSet>
#include <QTimer>
#include <QWidget>

#include <functional>

#include "ui/zero/ZeroSkeleton.h"
#include "ui/zero/content/ContentDelegate.h"
#include "ui/zero/content/ContentDetailsPanel.h"
#include "ui/zero/content/ContentReveal.h"
#include "zero/content/ContentFilter.h"
#include "zero/content/ModpackSource.h"

class QListView;
class QLabel;
class QPushButton;
class QStackedWidget;

namespace Zero {
class RemoteImageCache;
class ZeroSkeleton;
class ZeroSpinner;
}  // namespace Zero

namespace Zero::Content {

class ContentToolbar;
class FilterChipStrip;
class FilterPanel;
class ModpackListModel;
class NetworkErrorState;
class NoMatchState;

/// Everything every kind of section needs from the outside.
struct ContentEnvironment {
    ModpackSourceSet sources;
    Zero::RemoteImageCache* images = nullptr;  ///< null = RemoteImageCache::instance()
    /// Persisted filter (FilterState JSON without the search query) and view mode ("grid" / "list"); one settings key per kind.
    std::function<QString()> loadFilter;
    std::function<void(const QString&)> saveFilter;
    std::function<QString()> loadView;
    std::function<void(const QString&)> saveView;
};

/// What a kind fixes at construction (values and factories only: nothing here may call a virtual of the section).
struct ContentSectionSpec {
    Kind kind = Kind::Modpack;
    RevealSpec reveal = RevealSpec::modpacks();  ///< modpacks 24 px / 320 ms / 32 ms; mods 16 / 260 / 24
    QString objectName;
    QString searchPlaceholder;
    QString accessibleName;  ///< of the result list
    QString skeletonText;
    bool compactCards = false;  ///< the skeleton draws the artwork-less mod card (Mods) instead of the modpack card
    Zero::ZeroSkeleton::Shape skeletonShape = Zero::ZeroSkeleton::Shape::Cards;  ///< Resource Packs: Gallery
    bool viewModes = true;      ///< false = grid only (Resource Packs): no grid / list switch, a saved "list" is ignored
    /// The painted delegate of the list (the view's parent owns it).
    std::function<ContentDelegate*(Zero::RemoteImageCache* images, QObject* parent)> makeDelegate;
    /// Optional widget between the chips and the results (the Modpacks featured strip); null result = none.
    std::function<QWidget*(Zero::RemoteImageCache* images, QWidget* parent)> makeAccessory;
};

class ContentSection : public QWidget {
    Q_OBJECT
   public:
    /// What the main area shows.
    enum class Page { List, Skeleton, NoMatch, Error };

    static constexpr int kDetailsDelayMs = 100;   ///< a selection asks the provider only when it rests this long
    static constexpr int kSearchingDelayMs = 150;  ///< no "Searching..." spinner for answers faster than this
    static constexpr int kMaxAutoPages = 6;        ///< pages loaded on their own while the client filter hides everything
    static constexpr qreal kDimOpacity = 0.5;
    static constexpr int kVersionsTimeoutMs = 30000;  ///< the safety net behind the sources' own timeout: nothing stays "loading" forever

    // ---- filter state ----
    FilterState state() const { return m_state; }
    Provider provider() const { return m_state.provider(); }
    /// Another provider (one source at a time); an unavailable provider is ignored.
    void setProvider(Provider provider);
    void setSearchText(const QString& text);
    /// Applies a whole state (search text included) and searches when it needs a new search.
    void applyState(const FilterState& state);
    QList<Facet> facets() const { return m_facets; }
    FilterState defaults() const { return m_defaults; }
    /// The Minecraft versions of the version facet, newest first.
    void setMinecraftVersions(const QStringList& versions);
    /// "48 loaded", "12 of 48 loaded", "37 results": empty while the first page loads.
    QString resultsCaption() const { return m_caption; }
    Page page() const { return m_page; }
    /// Starts the first search (the page calls it once it is shown); later calls do nothing.
    void start();
    /// Searches again with the current state (Retry, Refresh).
    void reload();
    /// The default view: no query and no filter chip.
    bool isDefaultView() const;

    // ---- selection ----
    void selectItem(const ContentItemPtr& item);
    void selectRow(int row);
    ContentItemPtr selectedItem() const { return m_selected; }
    /// Re-reads the installed state (instance list changed) and updates cards and the panel's action.
    void refreshInstalled();

    /// What the sources' next versions() requests carry (Mods: the target's Minecraft version and loaders; empty for modpacks).
    /// Versions already loaded are dropped, so the Versions tab asks again under the new filter.
    void setVersionFilter(const VersionFilter& filter);

    // ---- parts (tests, dev aids) ----
    ContentToolbar* toolbar() const { return m_toolbar; }
    FilterChipStrip* chipStrip() const { return m_chips; }
    FilterPanel* filterPanel() const { return m_filterPanel; }
    QListView* view() const { return m_view; }
    ModpackListModel* model() const { return m_model; }
    ContentReveal* reveal() const { return m_reveal; }
    ContentDetailsPanel* panel() const { return m_panel; }
    NoMatchState* noMatchState() const { return m_noMatch; }
    NetworkErrorState* errorState() const { return m_error; }
    Zero::RemoteImageCache* images() const { return m_images; }
    /// The source of `provider` (the install dialog asks it for the versions); null when that provider is unavailable.
    ModpackSource* source(Provider provider) const { return m_env.sources.source(provider); }
    QLabel* footerLabel() const { return m_footerLabel; }
    QPushButton* footerRetry() const { return m_footerRetry; }
    bool footerShowsSpinner() const;
    const ResultPager& pager() const { return m_pager; }
    void openFilters();
    void setVersionsTimeoutMs(int ms) { m_versionsTimeoutMs = ms; }
    /// Dev aid: shows the loaded rows again as a new result set, so the cards enter again.
    void replayEntrance();
    /// True while a search page, the open item's details or pictures (or the kind's own work) are still on their way.
    bool isBusy() const;
    /// Calls `done` once nothing is busy for ~300 ms, or after `maxMs` (dev screenshot aids).
    void callWhenIdle(const std::function<void()>& done, int maxMs);

   signals:
    void resultsCaptionChanged(const QString& caption);
    /// reload() ran (the explicit Retry / Refresh): the hosting page may ask its own failed side requests again.
    void reloaded();

   protected:
    ContentSection(const ContentEnvironment& env, const ContentSectionSpec& spec, QWidget* parent);

    /// The second half of construction (state, facets, wiring); the last line of the subclass constructor.
    void init();

    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

    // ---- hooks a kind implements ----
    /// The default chips ("Clear all" returns to them). Today's Modpacks default: Modrinth. Mods: the target's version and loaders;
    /// a kind that changes them later calls refreshDefaults().
    virtual FilterState sectionDefaults() const;
    /// The facets of this kind; `ctx` carries the provider, its categories and the Minecraft versions.
    virtual QList<Facet> sectionFacets(const FacetContext& ctx) const = 0;
    /// The managed keys (`managedKey(type, id)`) of what is installed: the "Installed" badge and facet. Called by refreshInstalled().
    virtual QSet<QString> readInstalledKeys() { return {}; }
    /// The client-side facts of an item (default: installed state only).
    virtual ItemFacts itemFacts(const ContentItem& item) const;
    /// The versions the details panel lists for `item`, from everything the provider answered (Mods: only the ones that fit the
    /// target, as a safety net behind the server-side filter). Default: all of them.
    virtual QList<ContentVersion> panelVersions(const ContentItem& item, const QList<ContentVersion>& versions) const;
    /// The details panel's header of `item`.
    virtual DetailsHeader headerFor(const ContentItem& item) const = 0;
    /// Sets the primary button (text, enabled, note) of the open item. Called when versions or the installed state change.
    virtual void updatePrimaryAction() = 0;
    /// The primary button was pressed for the open item.
    virtual void onPrimaryAction() = 0;

    // ---- optional hooks ----
    /// A search answer for a generation that is not the list's (the featured request): return true when taken. `ok` false = failed.
    virtual bool takesSearchAnswer(quint64 generation, bool ok, const QList<ContentItemPtr>& items);
    /// A details answer for a generation that is not a selection's (background fetches): true when taken. `item` is null on failure.
    virtual bool takesDetailsAnswer(quint64 generation, const ContentItemPtr& item);
    /// A request carrying this generation belongs to a dialog: its answer is never counted as the section's own pending request.
    virtual bool isDialogGeneration(quint64 generation) const;
    /// False while the kind cannot search yet (Mods without a target instance): start() waits until a later call finds it true.
    virtual bool canSearch() const { return true; }
    virtual void onSearchStarted() {}
    virtual void onPageLoaded(const QList<ContentItemPtr>& items) { Q_UNUSED(items); }
    /// The query, the chips or the sort changed (or the section started / reloaded): the accessory may show or hide.
    virtual void onViewStateChanged() {}
    virtual void onReload() {}
    virtual void onInstalledChanged() {}
    /// `item` is selected now (null = the selection ended).
    virtual void onSelectionChanged(const ContentItemPtr& item) { Q_UNUSED(item); }
    /// Pictures arrived or details were merged: the accessory repaints its images.
    virtual void refreshAccessoryImages() {}
    virtual bool hasAccessoryWork() const { return false; }

    // ---- for the kind ----
    QWidget* accessory() const { return m_accessory; }
    const ContentEnvironment& environment() const { return m_env; }
    Kind kind() const { return m_spec.kind; }
    quint64 nextGeneration() { return ++m_nextGeneration; }
    const QSet<QString>& installedKeys() const { return m_installedKeys; }
    ContentDelegate* contentDelegate() const { return m_delegate; }
    QList<ResourceAPI::SortingMethod> providerMethods(Provider provider) const;
    /// The versions loaded for the item with this key, if they are.
    bool hasVersions(const QString& itemKey) const { return m_versions.contains(itemKey); }
    QList<ContentVersion> versionsOf(const QString& itemKey) const { return m_versions.value(itemKey); }
    /// The defaults changed (the target changed): the facets are built again (they may depend on the target too) and the chips and
    /// the panel show the new defaults. Does not search.
    void refreshDefaults();
    /// The widget column (toolbar, chips, results): a kind that shows something else instead hides it.
    QWidget* column() const { return m_column; }

   private:
    void buildFacets();
    void syncWidgets();
    void edit(const FilterState& next);
    void persist();
    FilterState sanitized(FilterState state) const;

    void startSearch();
    void requestNextPage();
    void onSearchFinished(quint64 generation, int offset, const QList<ContentItemPtr>& items, int rawCount);
    void onSearchFailed(quint64 generation, const QString& error);
    void applyClientFilter();
    void maybeLoadMore();
    void updatePage();
    void updateFooter();
    void updateCaption();
    void applyCompaction();
    void layoutGrid();

    void requestDetails(quint64 generation);
    void requestVersions(const ContentItemPtr& item, quint64 generation);
    void fillPanel(const ContentItemPtr& item);
    void connectSource(ModpackSource* source);
    void repaintImages();
    void wire();

    ContentEnvironment m_env;
    ContentSectionSpec m_spec;
    Zero::RemoteImageCache* m_images;
    FilterState m_state;
    FilterState m_defaults;
    QList<Facet> m_facets;
    QStringList m_mcVersions;
    ResultPager m_pager;
    quint64 m_nextGeneration = 0;
    quint64 m_selectionGeneration = 0;
    quint64 m_versionFilterGeneration = 0;  ///< versions answers for older generations were asked under another filter
    bool m_initialized = false;
    bool m_started = false;
    bool m_firstPagePending = false;
    bool m_firstPageFailed = false;
    bool m_searching = false;  ///< the "Searching..." spinner is up (the delay passed)
    QString m_errorText;
    int m_autoPages = 0;
    QString m_caption;
    Page m_page = Page::Skeleton;

    // selection
    ContentItemPtr m_selected;
    QHash<QString, QList<ContentVersion>> m_versions;  ///< by item key
    QHash<QString, QString> m_changelogs;              ///< "<item key>#<version id>" -> html
    int m_detailsPending = 0;
    QSet<QString> m_installedKeys;
    bool m_syncingSelection = false;

    // widgets
    ContentToolbar* m_toolbar;
    FilterChipStrip* m_chips;
    FilterPanel* m_filterPanel;
    QWidget* m_accessory;
    QListView* m_view;
    ModpackListModel* m_model;
    ContentDelegate* m_delegate;
    ContentReveal* m_reveal;
    QStackedWidget* m_stack;
    Zero::ZeroSkeleton* m_skeleton;
    NoMatchState* m_noMatch;
    NetworkErrorState* m_error;
    QWidget* m_footer;
    Zero::ZeroSpinner* m_footerSpinner;
    QLabel* m_footerLabel;
    QPushButton* m_footerRetry;
    ContentDetailsPanel* m_panel;
    QWidget* m_column;

    QTimer m_detailsTimer;
    QTimer m_versionsTimer;
    int m_versionsTimeoutMs = kVersionsTimeoutMs;
    QTimer m_searchingTimer;
    QTimer m_repaintTimer;
};

}  // namespace Zero::Content
