// SPDX-License-Identifier: GPL-3.0-only
#include "ContentSection.h"

#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QLocale>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QStackedWidget>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

#include "ContentStates.h"
#include "ContentToolbar.h"
#include "DesktopServices.h"
#include "FilterChipStrip.h"
#include "FilterPanel.h"
#include "ModpackRows.h"
#include "RemoteImageCache.h"
#include "modplatform/flame/FlameAPI.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "ui/zero/ZeroFormat.h"
#include "ui/zero/ZeroSkeleton.h"
#include "ui/zero/ZeroSpinner.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero::Content {

namespace {
QString changelogKey(const ModpackItem& item, const QString& versionId)
{
    return item.key() + QLatin1Char('#') + versionId;
}

QString feedHtml(const TechnicPackDetails& details)
{
    QString html;
    for (const auto& entry : details.feed) {
        QString head = entry.user.toHtmlEscaped();
        if (entry.date > 0) {
            const QString date = QLocale(QLocale::English).toString(QDateTime::fromSecsSinceEpoch(entry.date).date(), QStringLiteral("d MMM yyyy"));
            head += head.isEmpty() ? date : QStringLiteral(" · ") + date;
        }
        html += QStringLiteral("<p><b>%1</b><br>%2</p>").arg(head, entry.content);
    }
    return html;
}
}  // namespace

ContentSection::ContentSection(const ContentEnvironment& env, const ContentSectionSpec& spec, QWidget* parent)
    : QWidget(parent)
    , m_env(env)
    , m_spec(spec)
    , m_images(env.images ? env.images : Zero::RemoteImageCache::instance())
    , m_pager(ResultPager::kDefaultPageSize)
{
    setObjectName(m_spec.objectName);

    // ----- widgets -----
    m_toolbar = new ContentToolbar(this);
    m_toolbar->setSearchPlaceholder(m_spec.searchPlaceholder);
    m_toolbar->setViewModesAvailable(m_spec.viewModes);
    m_chips = new FilterChipStrip(this);
    m_filterPanel = new FilterPanel(this);
    m_accessory = m_spec.makeAccessory ? m_spec.makeAccessory(m_images, this) : nullptr;

    m_model = new ModpackListModel(this);
    m_view = new QListView(this);
    m_view->setModel(m_model);
    m_delegate = m_spec.makeDelegate(m_images, m_view);
    m_view->setItemDelegate(m_delegate);
    m_reveal = installContentReveal(m_view, m_spec.reveal);
    m_view->setViewMode(QListView::IconMode);
    m_view->setResizeMode(QListView::Adjust);
    m_view->setMovement(QListView::Static);
    m_view->setWrapping(true);
    m_view->setFlow(QListView::LeftToRight);
    m_view->setSpacing(0);
    m_view->setUniformItemSizes(true);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setMouseTracking(true);
    m_view->setAttribute(Qt::WA_MacShowFocusRect, false);
    m_view->setStyleSheet(QStringLiteral("QAbstractItemView { background: transparent; border: none; }"));
    m_view->viewport()->setAutoFillBackground(false);
    m_view->viewport()->installEventFilter(this);
    m_view->installEventFilter(this);
    m_view->setAccessibleName(m_spec.accessibleName);
    m_view->setGridSize(m_delegate->gridSize());

    m_skeleton = new Zero::ZeroSkeleton(m_spec.skeletonShape, m_spec.skeletonText, this);
    m_skeleton->setCompactCards(m_spec.compactCards);
    m_noMatch = new NoMatchState(this);
    m_error = new NetworkErrorState(this);
    m_stack = new QStackedWidget(this);
    m_stack->addWidget(m_view);      // Page::List
    m_stack->addWidget(m_skeleton);  // Page::Skeleton
    m_stack->addWidget(m_noMatch);   // Page::NoMatch
    m_stack->addWidget(m_error);     // Page::Error

    m_footer = new QWidget(this);
    auto* footerRow = new QHBoxLayout(m_footer);
    footerRow->setContentsMargins(0, Tokens::spaceSm, 0, Tokens::spaceSm);
    footerRow->setSpacing(Tokens::spaceSm);
    footerRow->addStretch(1);
    m_footerSpinner = new Zero::ZeroSpinner(Zero::ZeroSpinner::Small, m_footer);
    m_footerLabel = new QLabel(m_footer);
    Style::applyRole(m_footerLabel, Role::Caption);
    m_footerRetry = new QPushButton(tr("Retry"), m_footer);
    Style::setProperty(m_footerRetry, "zeroVariant", "ghost");
    footerRow->addWidget(m_footerSpinner);
    footerRow->addWidget(m_footerLabel);
    footerRow->addWidget(m_footerRetry);
    footerRow->addStretch(1);
    m_footer->hide();

    m_panel = new ContentDetailsPanel(m_images, this);
    m_panel->setOverlayHost(this);

    // ----- layout: [column | panel] -----
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(Tokens::spaceXl);
    m_column = new QWidget(this);
    auto* column = new QVBoxLayout(m_column);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(Tokens::spaceMd);
    column->addWidget(m_toolbar);
    column->addWidget(m_chips);
    if (m_accessory) {
        column->addWidget(m_accessory);
    }
    column->addWidget(m_stack, 1);
    column->addWidget(m_footer);
    root->addWidget(m_column, 1);
    root->addWidget(m_panel);
}

void ContentSection::init()
{
    Q_ASSERT(!m_initialized);
    m_initialized = true;
    m_defaults = sectionDefaults();

    // ----- sources -----
    for (Provider p : { Provider::Modrinth, Provider::CurseForge, Provider::Technic }) {
        if (auto* source = m_env.sources.source(p)) {
            connectSource(source);
        }
    }

    // ----- state: the persisted filter (without the query), the persisted view -----
    FilterState saved;
    if (m_env.loadFilter) {
        const auto doc = QJsonDocument::fromJson(m_env.loadFilter().toUtf8());
        if (doc.isObject()) {
            saved = FilterState::fromJson(doc.object());
        }
    }
    saved.query.clear();
    m_state = sanitized(saved);
    if (m_spec.viewModes && m_env.loadView && m_env.loadView() == QLatin1String("list")) {
        m_toolbar->setViewMode(ContentToolbar::ViewMode::List);
        m_delegate->setMode(ContentDelegate::Mode::List);
    }
    buildFacets();
    syncWidgets();
    m_model->setClientFilter([this](const ContentItem& item) { return matchesClient(m_state, m_facets, itemFacts(item)); });
    refreshInstalled();

    wire();
    updatePage();
    updateFooter();
}

void ContentSection::wire()
{
    connect(m_toolbar, &ContentToolbar::searchChanged, this, [this](const QString& text) {
        FilterState next = m_state;
        next.query = text.trimmed();
        edit(next);
    });
    connect(m_toolbar, &ContentToolbar::sortChanged, this, [this](const QString& sortId) {
        FilterState next = m_state;
        next.sortId = sortId;
        edit(next);
    });
    connect(m_toolbar, &ContentToolbar::viewModeChanged, this, [this](ContentToolbar::ViewMode mode) {
        m_delegate->setMode(mode == ContentToolbar::ViewMode::List ? ContentDelegate::Mode::List : ContentDelegate::Mode::Grid);
        layoutGrid();
        m_view->setGridSize(m_delegate->gridSize());
        m_view->doItemsLayout();
        if (m_env.saveView) {
            m_env.saveView(mode == ContentToolbar::ViewMode::List ? QStringLiteral("list") : QStringLiteral("grid"));
        }
    });
    connect(m_toolbar, &ContentToolbar::filtersRequested, this, [this]() { m_filterPanel->toggle(m_toolbar->filtersButton()); });
    connect(m_chips, &FilterChipStrip::stateChanged, this, &ContentSection::edit);
    connect(m_filterPanel, &FilterPanel::stateChanged, this, &ContentSection::edit);
    connect(m_noMatch, &NoMatchState::clearAllRequested, this, [this]() { edit(clearAll(m_state, m_defaults)); });
    connect(m_noMatch, &NoMatchState::showEverythingRequested, this, [this]() { edit(showEverything(m_state)); });
    connect(m_error, &NetworkErrorState::retryRequested, this, &ContentSection::reload);
    connect(m_footerRetry, &QPushButton::clicked, this, [this]() {
        m_pager.retry();
        requestNextPage();
        updateFooter();
    });

    connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& current) {
        if (m_syncingSelection || !current.isValid()) {
            return;  // a model reset clears the current index: the open details stay
        }
        selectItem(m_model->item(current.row()));
    });
    connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this, &ContentSection::maybeLoadMore);
    connect(m_view->verticalScrollBar(), &QScrollBar::rangeChanged, this, &ContentSection::maybeLoadMore);

    connect(m_panel, &ContentDetailsPanel::closeRequested, this, [this]() { selectItem(nullptr); });
    connect(m_panel, &ContentDetailsPanel::primaryActionRequested, this, [this]() {
        if (m_selected) {
            onPrimaryAction();
        }
    });
    connect(m_panel, &ContentDetailsPanel::websiteRequested, this, [this]() {
        if (m_selected) {
            const QUrl url(m_selected->websiteUrl);
            if (url.isValid() && (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http"))) {
                DesktopServices::openUrl(url);
            }
        }
    });
    connect(m_panel, &ContentDetailsPanel::retryRequested, this, [this](ContentDetailsPanel::Tab tab) {
        if (!m_selected) {
            return;
        }
        auto* source = m_env.sources.source(m_selected->provider);
        if (!source) {
            return;
        }
        if (tab == ContentDetailsPanel::Tab::Overview) {
            m_panel->setOverview(LoadState::Loading, m_selected->summary.toHtmlEscaped());
            ++m_detailsPending;
            source->details(m_selected, m_selectionGeneration);
        } else if (tab == ContentDetailsPanel::Tab::Versions) {
            m_versions.remove(m_selected->key());
            m_panel->setVersions(LoadState::Loading, {});
            requestVersions(m_selected, m_selectionGeneration);
        }
    });
    connect(m_panel, &ContentDetailsPanel::changelogRequested, this, [this](const QString& versionId) {
        if (!m_selected) {
            return;
        }
        if (const auto cached = m_changelogs.constFind(changelogKey(*m_selected, versionId)); cached != m_changelogs.cend()) {
            m_panel->setChangelog(versionId, cached.value(), false);
            return;
        }
        if (auto* source = m_env.sources.source(m_selected->provider)) {
            source->changelog(m_selected, versionId, m_selectionGeneration);
        }
    });

    m_versionsTimer.setSingleShot(true);
    connect(&m_versionsTimer, &QTimer::timeout, this, [this]() {
        // A provider that never answers (a response that cannot be parsed): the Versions tab says so instead of loading forever.
        if (m_selected && !m_versions.contains(m_selected->key())) {
            m_detailsPending = std::max(0, m_detailsPending - 1);
            m_panel->setVersions(LoadState::Failed, {}, tr("The provider did not answer in time."));
            updatePrimaryAction();
        }
    });
    m_detailsTimer.setSingleShot(true);
    connect(&m_detailsTimer, &QTimer::timeout, this, [this]() { requestDetails(m_selectionGeneration); });
    m_searchingTimer.setSingleShot(true);
    connect(&m_searchingTimer, &QTimer::timeout, this, [this]() {
        if (m_firstPagePending) {
            m_searching = true;
            updateFooter();
        }
    });
    m_repaintTimer.setSingleShot(true);
    connect(&m_repaintTimer, &QTimer::timeout, this, &ContentSection::repaintImages);
    connect(m_images, &Zero::RemoteImageCache::imageReady, this, [this]() {
        if (!m_repaintTimer.isActive()) {
            m_repaintTimer.start(16);
        }
    });
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        m_view->viewport()->update();
        refreshAccessoryImages();
    });
}

// ---- state ----

FilterState ContentSection::sanitized(FilterState state) const
{
    // Worlds exist on CurseForge only (no provider facet): that is the provider, whatever a saved filter says.
    if (m_spec.kind == Kind::World) {
        state.selected.insert(FacetId::provider, { providerId(Provider::CurseForge) });
    }
    // A provider that is not available (CurseForge without its API key) falls back to the default one.
    const Provider provider = state.provider();
    if (!m_env.sources.has(provider)) {
        state.selected.remove(FacetId::provider);
    }
    const Provider effective = state.provider();
    const auto options = sortOptions(m_spec.kind, effective);
    const bool valid = std::any_of(options.cbegin(), options.cend(), [&](const SortOption& o) { return o.id == state.sortId; });
    if (!valid) {
        state.sortId = options.isEmpty() ? QString() : options.first().id;
    }
    return state;
}

QList<ResourceAPI::SortingMethod> ContentSection::providerMethods(Provider provider) const
{
    switch (provider) {
        case Provider::Modrinth:
            return ModrinthAPI::get().getSortingMethods();
        case Provider::CurseForge:
            return FlameAPI::get().getSortingMethods();
        case Provider::Technic:
            return {};
    }
    return {};
}

void ContentSection::buildFacets()
{
    FacetContext ctx;
    ctx.provider = provider();
    if (auto* source = m_env.sources.source(ctx.provider)) {
        ctx.categories = source->categories();
    }
    ctx.mcVersions = m_mcVersions;
    ctx.hasTarget = false;
    m_facets = sectionFacets(ctx);
    m_chips->setFacets(m_facets);
    m_chips->setDefaults(m_defaults);
    m_filterPanel->setFacets(m_facets);
    m_filterPanel->setDefaults(m_defaults);
}

void ContentSection::syncWidgets()
{
    const auto chips = chipsFor(m_state, m_facets, m_defaults);
    int count = 0;
    for (const auto& chip : chips) {
        if (!chip.isQuery) {
            ++count;
        }
    }
    if (m_toolbar->searchText() != m_state.query) {
        m_toolbar->setSearchText(m_state.query);
    }
    m_toolbar->setFilterCount(count);
    m_toolbar->setSortOptions(sortOptions(m_spec.kind, provider()), m_state.sortId);
    m_chips->setState(m_state);
    m_filterPanel->setState(m_state);
}

void ContentSection::persist()
{
    if (!m_env.saveFilter) {
        return;
    }
    FilterState saved = m_state;
    saved.query.clear();
    m_env.saveFilter(QString::fromUtf8(QJsonDocument(saved.toJson()).toJson(QJsonDocument::Compact)));
}

void ContentSection::edit(const FilterState& requested)
{
    const FilterState next = sanitized(requested);
    if (next == m_state) {
        syncWidgets();  // a widget showed something the state does not have (a sanitized value)
        return;
    }
    const FilterState previous = m_state;
    const bool providerChanged = next.provider() != previous.provider();
    m_state = next;
    if (providerChanged) {
        if (auto* source = m_env.sources.source(provider())) {
            source->requestCategories();
        }
        buildFacets();
    }
    syncWidgets();
    persist();
    if (!m_started) {
        return;
    }
    if (needsNewSearch(previous, m_state, m_facets)) {
        startSearch();
    } else {
        applyClientFilter();
    }
    onViewStateChanged();
}

void ContentSection::setProvider(Provider p)
{
    if (!m_env.sources.has(p)) {
        return;
    }
    FilterState next = m_state;
    next.selected.insert(FacetId::provider, { providerId(p) });
    edit(next);
}

void ContentSection::setSearchText(const QString& text)
{
    FilterState next = m_state;
    next.query = text.trimmed();
    edit(next);
}

void ContentSection::applyState(const FilterState& state)
{
    edit(state);
}

void ContentSection::refreshDefaults()
{
    m_defaults = sectionDefaults();
    buildFacets();  // the facets may depend on what changed too (Mods: CurseForge's availability follows the target's loaders); sets the defaults
    syncWidgets();
}

void ContentSection::setVersionFilter(const VersionFilter& filter)
{
    for (Provider p : { Provider::Modrinth, Provider::CurseForge, Provider::Technic }) {
        if (auto* source = m_env.sources.source(p)) {
            source->setVersionFilter(filter);
        }
    }
    m_versions.clear();
    m_view->viewport()->update();
    // A versions request still on its way was made under the previous filter: its answer is dropped (generations only grow).
    m_versionFilterGeneration = ++m_nextGeneration;
}

void ContentSection::setMinecraftVersions(const QStringList& versions)
{
    if (versions == m_mcVersions) {
        return;
    }
    m_mcVersions = versions;
    buildFacets();
    syncWidgets();
}

void ContentSection::start()
{
    Q_ASSERT(m_initialized);
    if (m_started || !canSearch()) {
        return;
    }
    m_started = true;
    if (auto* source = m_env.sources.source(provider())) {
        source->requestCategories();
    }
    startSearch();
    onViewStateChanged();
}

void ContentSection::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    start();
    layoutGrid();
}

void ContentSection::reload()
{
    if (!m_started) {
        start();
        return;
    }
    // The explicit retry: failed images, a failed accessory (Featured strip) and failed categories are asked again.
    m_images->retryFailed();
    onReload();
    if (auto* source = m_env.sources.source(provider())) {
        source->requestCategories();  // a no-op while loaded or loading
    }
    startSearch();
    onViewStateChanged();
    emit reloaded();
}

bool ContentSection::isDefaultView() const
{
    return m_state.query.trimmed().isEmpty() && chipsFor(m_state, m_facets, m_defaults).isEmpty();
}

void ContentSection::replayEntrance()
{
    m_view->verticalScrollBar()->setValue(0);
    const QList<ContentItemPtr> items = m_model->loaded();  // a copy: reset() clears the list it is given
    m_model->reset(items);
}

void ContentSection::openFilters()
{
    m_filterPanel->popup(m_toolbar->filtersButton());
}

// ---- searching ----

void ContentSection::connectSource(ModpackSource* source)
{
    connect(source, &ModpackSource::searchFinished, this, &ContentSection::onSearchFinished);
    connect(source, &ModpackSource::searchFailed, this, &ContentSection::onSearchFailed);
    connect(source, &ModpackSource::categoriesReady, this, [this, source]() {
        if (source->provider() == provider()) {
            buildFacets();
            syncWidgets();
        }
    });
    connect(source, &ModpackSource::detailsFinished, this, [this](quint64 generation, const ContentItemPtr& item) {
        if (takesDetailsAnswer(generation, item)) {
            return;
        }
        if (!isDialogGeneration(generation)) {  // a dialog's own request was never counted
            m_detailsPending = std::max(0, m_detailsPending - 1);
        }
        // The item itself changed (long description, gallery, counts): its card repaints whatever is selected now.
        m_model->itemChanged(item);
        refreshAccessoryImages();
        if (generation != m_selectionGeneration || !m_selected) {
            return;  // another item is selected now: this answer is dropped
        }
        fillPanel(item);
    });
    connect(source, &ModpackSource::detailsFailed, this, [this](quint64 generation, const QString& error) {
        if (takesDetailsAnswer(generation, nullptr)) {
            return;
        }
        if (!isDialogGeneration(generation)) {
            m_detailsPending = std::max(0, m_detailsPending - 1);
        }
        if (generation != m_selectionGeneration || !m_selected) {
            return;
        }
        m_panel->setOverview(LoadState::Failed, m_selected->summary.toHtmlEscaped(), error);
    });
    connect(source, &ModpackSource::versionsFinished, this,
            [this](quint64 generation, const ContentItemPtr& item, const QList<ContentVersion>& versions) {
                if (!isDialogGeneration(generation)) {
                    m_detailsPending = std::max(0, m_detailsPending - 1);
                }
                if (generation < m_versionFilterGeneration) {
                    return;  // asked under the previous version filter (the target changed since): the wrong versions
                }
                m_versions.insert(item->key(), versions);
                m_model->itemChanged(item);  // what the card may do (the install cart's check box) depends on the versions
                if (m_selected && m_selected->key() == item->key()) {
                    m_versionsTimer.stop();
                }
                if (generation != m_selectionGeneration || !m_selected || m_selected->key() != item->key()) {
                    return;
                }
                const QList<ContentVersion> shown = panelVersions(*item, versions);
                m_panel->setVersions(LoadState::Ready, shown);
                updatePrimaryAction();
                // What's new = the newest version's changelog (Technic has its feed instead).
                if (item->provider != Provider::Technic && !shown.isEmpty()) {
                    const QString key = changelogKey(*item, shown.first().id);
                    if (const auto cached = m_changelogs.constFind(key); cached != m_changelogs.cend()) {
                        m_panel->setWhatsNew(cached.value());
                    } else if (auto* source = m_env.sources.source(item->provider)) {
                        source->changelog(item, shown.first().id, generation);
                    }
                }
            });
    connect(source, &ModpackSource::versionsFailed, this, [this](quint64 generation, const QString& error) {
        if (!isDialogGeneration(generation)) {
            m_detailsPending = std::max(0, m_detailsPending - 1);
        }
        if (generation != m_selectionGeneration || !m_selected) {
            return;
        }
        m_versionsTimer.stop();
        m_panel->setVersions(LoadState::Failed, {}, error);
        updatePrimaryAction();
    });
    connect(source, &ModpackSource::changelogFinished, this, [this](quint64 generation, const QString& versionId, const QString& html) {
        if (generation != m_selectionGeneration || !m_selected) {
            return;
        }
        m_changelogs.insert(changelogKey(*m_selected, versionId), html);
        m_panel->setChangelog(versionId, html, false);
        const auto versions = panelVersions(*m_selected, m_versions.value(m_selected->key()));
        if (!versions.isEmpty() && versions.first().id == versionId) {
            m_panel->setWhatsNew(html);
        }
    });
    connect(source, &ModpackSource::changelogFailed, this, [this](quint64 generation, const QString& versionId, const QString&) {
        if (generation != m_selectionGeneration || !m_selected) {
            return;
        }
        m_panel->setChangelog(versionId, {}, true);
    });
}

void ContentSection::startSearch()
{
    ModpackSource* source = m_env.sources.source(provider());
    const quint64 generation = ++m_nextGeneration;
    m_pager.reset(generation);
    onSearchStarted();
    if (source) {
        m_pager.setPageSize(source->pageSize());
    }
    m_firstPagePending = true;
    m_firstPageFailed = false;
    m_autoPages = 0;
    m_searching = false;
    m_errorText.clear();
    // The old rows stay, dimmed, until the answer arrives; the "Searching..." spinner waits 150 ms.
    m_delegate->setDimmed(m_model->visibleCount() > 0);
    m_view->viewport()->update();
    m_searchingTimer.start(kSearchingDelayMs);
    if (!source) {
        m_firstPagePending = false;
        m_firstPageFailed = true;
        m_delegate->setDimmed(false);
        m_error->setError(providerName(provider()), tr("This provider is not available."));
    } else {
        requestNextPage();
    }
    updatePage();
    updateFooter();
    updateCaption();
}

void ContentSection::requestNextPage()
{
    ModpackSource* source = m_env.sources.source(provider());
    if (!source || !m_pager.begin()) {
        return;
    }
    const ResourceAPI::SearchArgs args =
        toSearchArgs(m_state, m_spec.kind, provider(), m_pager.nextOffset(), providerMethods(provider()), m_facets);
    source->search(args, m_pager.generation());
}

void ContentSection::onSearchFinished(quint64 generation, int offset, const QList<ContentItemPtr>& items, int rawCount)
{
    if (takesSearchAnswer(generation, true, items)) {
        return;
    }
    if (!m_pager.accept(generation, offset, rawCount)) {
        return;  // an older search, a page we did not ask for, a duplicate
    }
    ModpackSource* source = m_env.sources.source(provider());
    if (source && !source->pages()) {
        m_pager.finish();
    }
    if (offset == 0) {
        m_firstPagePending = false;
        m_searching = false;
        m_searchingTimer.stop();
        m_delegate->setDimmed(false);
        m_model->reset(items);
        m_view->verticalScrollBar()->setValue(0);
    } else {
        m_model->append(items);
    }
    layoutGrid();
    updatePage();
    updateFooter();
    updateCaption();
    QTimer::singleShot(60, this, &ContentSection::maybeLoadMore);  // after the view laid the new rows out
    onPageLoaded(items);
}

void ContentSection::onSearchFailed(quint64 generation, const QString& error)
{
    if (takesSearchAnswer(generation, false, {})) {
        return;
    }
    if (!m_pager.fail(generation)) {
        return;
    }
    if (m_pager.nextOffset() == 0) {
        m_firstPagePending = false;
        m_firstPageFailed = true;
        m_searching = false;
        m_searchingTimer.stop();
        m_delegate->setDimmed(false);
        m_errorText = error;
        m_error->setError(providerName(provider()), error);
    }
    updatePage();
    updateFooter();
    updateCaption();
}

void ContentSection::applyClientFilter()
{
    m_model->setClientFilter([this](const ContentItem& item) { return matchesClient(m_state, m_facets, itemFacts(item)); });
    m_autoPages = 0;
    updatePage();
    updateFooter();
    updateCaption();
    QTimer::singleShot(60, this, &ContentSection::maybeLoadMore);
}

void ContentSection::maybeLoadMore()
{
    if (!m_started || !m_pager.canLoadMore() || m_firstPagePending || m_firstPageFailed) {
        return;
    }
    const bool hidesAll = m_model->visibleCount() == 0;
    if (m_page != Page::List && !hidesAll) {
        return;
    }
    // Near the end of the content: measured from the rows and the cell size, not from the scroll bar's maximum, which lags behind
    // the view's (batched) layout and reads 0 for a moment after every model reset.
    QScrollBar* bar = m_view->verticalScrollBar();
    const int viewport = std::max(1, m_view->viewport()->height());
    const int columns = std::max(1, m_delegate->columns());
    const int rows = (m_model->visibleCount() + columns - 1) / columns;
    const int contentHeight = rows * m_delegate->gridSize().height();
    const bool nearEnd = contentHeight - (bar->value() + viewport) <= viewport * 3 / 2;
    if (!nearEnd) {
        return;
    }
    if (hidesAll && ++m_autoPages > kMaxAutoPages) {
        return;
    }
    requestNextPage();
    updateFooter();
}

void ContentSection::updatePage()
{
    const int visible = m_model->visibleCount();
    Page next = Page::List;
    if (m_firstPageFailed) {
        next = Page::Error;
    } else if (m_firstPagePending && m_model->loadedCount() == 0) {
        next = Page::Skeleton;
    } else if (visible == 0 && !m_firstPagePending) {
        next = m_pager.isLoading() ? Page::Skeleton : Page::NoMatch;
    }
    m_page = next;
    if (next == Page::NoMatch) {
        const auto chips = chipsFor(m_state, m_facets, m_defaults);
        QString restrictive;
        if (chips.size() == 1) {
            restrictive = chips.first().label;
            if (chips.first().isQuery) {
                restrictive = m_state.query;  // the chip reads "text" with quotes; the empty state adds its own
            }
        }
        bool defaultsExist = false;  // the kind's defaults other than the provider (Mods: the target's version and loaders)
        for (auto it = m_defaults.selected.cbegin(); it != m_defaults.selected.cend(); ++it) {
            defaultsExist = defaultsExist || (it.key() != FacetId::provider && !it.value().isEmpty());
        }
        m_noMatch->setContext(restrictive, defaultsExist);
    }
    m_stack->setCurrentIndex(int(next));
}

void ContentSection::updateFooter()
{
    QString text;
    bool spinner = false;
    bool retry = false;
    if (m_pager.isLoading() && m_pager.nextOffset() > 0) {
        text = tr("Loading more…");
        spinner = true;
    } else if (m_firstPagePending && m_searching && m_model->loadedCount() > 0) {
        text = tr("Searching…");
        spinner = true;
    } else if (m_pager.hasFailed() && !m_firstPageFailed) {
        text = tr("Couldn't load more results.");
        retry = true;
    } else if (m_pager.isEnded() && m_model->visibleCount() > 0 && m_page == Page::List) {
        text = tr("End of results");
    }
    m_footerLabel->setText(text);
    m_footerSpinner->setVisible(spinner);
    m_footerRetry->setVisible(retry);
    m_footer->setVisible(!text.isEmpty());
    const auto& t = Theme::current();
    m_footerLabel->setStyleSheet(QStringLiteral("color: %1;").arg((retry ? t.danger : (spinner ? t.textSecondary : t.textTertiary)).name()));
}

bool ContentSection::footerShowsSpinner() const
{
    return m_footer->isVisibleTo(const_cast<ContentSection*>(this)) && !m_footerSpinner->isHidden();
}

void ContentSection::updateCaption()
{
    QString text;
    const int loaded = m_model->loadedCount();
    const int visible = m_model->visibleCount();
    if (!m_firstPageFailed && loaded > 0) {
        const QLocale english(QLocale::English);
        const bool ended = m_pager.isEnded();
        const bool filtered = visible != loaded;
        const QString noun = ended ? (visible == 1 && !filtered ? tr("result") : tr("results")) : tr("loaded");
        text = filtered ? tr("%1 of %2 %3").arg(english.toString(visible), english.toString(loaded), noun)
                        : tr("%1 %2").arg(english.toString(visible), noun);
    }
    m_toolbar->setResultsCaption(text);
    applyCompaction();
    m_filterPanel->setResultText(text);
    if (text != m_caption) {
        m_caption = text;
        emit resultsCaptionChanged(text);
    }
}

void ContentSection::applyCompaction()
{
    // With the details panel open the column is only ~560 px wide at the minimum window size. The caption is in the top bar too, and
    // the search field may shrink below its usual 240 px so the sort combo and the view toggle keep their room.
    const bool compact = m_panel->isOpen();
    m_toolbar->searchField()->setMinimumWidth(compact ? 140 : ContentToolbar::kSearchMinWidth);
    m_toolbar->captionLabel()->setVisible(!compact && !m_toolbar->resultsCaption().isEmpty());
}

void ContentSection::layoutGrid()
{
    // QListView wraps its icons inside the view's maximum viewport width minus one scroll bar extent, whether or not the bar
    // is showing right now. Measuring that (not the viewport) keeps the columns stable when the bar comes and goes.
    const int extent = m_view->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, m_view->verticalScrollBar());
    // One pixel less: Qt wraps an icon whose right edge would touch the bound's last pixel (columns * cell must be < the width).
    const int usable = m_view->maximumViewportSize().width() - extent - 1;
    if (m_delegate->setViewportWidth(usable)) {
        m_view->setGridSize(m_delegate->gridSize());  // fluid grid: columns and card size follow the width
    }
}

bool ContentSection::eventFilter(QObject* watched, QEvent* event)
{
    if ((watched == m_view->viewport() || watched == m_view) && event->type() == QEvent::Resize) {
        layoutGrid();
        return false;
    }
    return QWidget::eventFilter(watched, event);
}

void ContentSection::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && m_panel->isOpen()) {
        selectItem(nullptr);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

// ---- selection and details ----

// ---- default hooks ----

FilterState ContentSection::sectionDefaults() const
{
    FilterState defaults;
    defaults.selected.insert(FacetId::provider, { providerId(Provider::Modrinth) });
    return defaults;
}

ItemFacts ContentSection::itemFacts(const ContentItem& item) const
{
    return factsOf(item, m_installedKeys);
}

QList<ContentVersion> ContentSection::panelVersions(const ContentItem&, const QList<ContentVersion>& versions) const
{
    return versions;
}

bool ContentSection::takesSearchAnswer(quint64, bool, const QList<ContentItemPtr>&)
{
    return false;
}

bool ContentSection::takesDetailsAnswer(quint64, const ContentItemPtr&)
{
    return false;
}

bool ContentSection::isDialogGeneration(quint64) const
{
    return false;
}

void ContentSection::refreshInstalled()
{
    m_installedKeys = readInstalledKeys();
    m_model->setInstalledKeys(m_installedKeys);
    onInstalledChanged();
    if (m_selected) {
        updatePrimaryAction();
    }
    if (m_started) {
        // The Installed filter facet depends on it.
        applyClientFilter();
    }
}

void ContentSection::selectRow(int row)
{
    if (row >= 0 && row < m_model->visibleCount()) {
        m_view->setCurrentIndex(m_model->index(row));
    }
}

void ContentSection::selectItem(const ContentItemPtr& item)
{
    m_selected = item;
    m_selectionGeneration = ++m_nextGeneration;
    m_detailsTimer.stop();
    m_versionsTimer.stop();
    m_syncingSelection = true;
    if (!item) {
        m_view->selectionModel()->clear();
    } else {
        const int row = m_model->rowOf(item);
        if (row >= 0) {
            m_view->selectionModel()->setCurrentIndex(m_model->index(row), QItemSelectionModel::ClearAndSelect);
        } else {
            m_view->selectionModel()->clear();
        }
    }
    m_syncingSelection = false;
    if (!item) {
        m_panel->clear();
        onSelectionChanged(nullptr);
        applyCompaction();
        return;
    }
    onSelectionChanged(item);
    m_panel->showItem(headerFor(*item));
    applyCompaction();
    fillPanel(item);
    m_detailsTimer.start(kDetailsDelayMs);
}

void ContentSection::fillPanel(const ContentItemPtr& item)
{
    if (!item || m_selected != item) {
        return;
    }
    m_panel->showItem(headerFor(*item));  // same key: refreshes the header only
    if (item->detailsLoaded) {
        m_panel->setOverview(LoadState::Ready, overviewHtml(*item));
    } else {
        m_panel->setOverview(LoadState::Loading, item->summary.toHtmlEscaped());
    }
    QList<GalleryEntry> gallery;
    if (item->pack) {
        for (const auto& image : item->pack->gallery) {
            if (!image.url.isEmpty()) {
                gallery.append({ image.url, image.title, image.description, image.thumbnailUrl == image.url ? QString() : image.thumbnailUrl });
            }
        }
    }
    m_panel->setGallery(gallery);
    if (item->technic) {
        m_panel->setWhatsNew(feedHtml(*item->technic));
    }
    if (const auto cached = m_versions.constFind(item->key()); cached != m_versions.cend()) {
        m_panel->setVersions(LoadState::Ready, panelVersions(*item, cached.value()));
    } else {
        m_panel->setVersions(LoadState::Loading, {});
    }
    updatePrimaryAction();
}

void ContentSection::requestDetails(quint64 generation)
{
    if (generation != m_selectionGeneration || !m_selected) {
        return;
    }
    const ContentItemPtr item = m_selected;
    ModpackSource* source = m_env.sources.source(item->provider);
    if (!source) {
        m_panel->setOverview(LoadState::Failed, item->summary.toHtmlEscaped(), tr("This provider is not available."));
        m_panel->setVersions(LoadState::Failed, {}, tr("This provider is not available."));
        return;
    }
    if (!item->detailsLoaded) {
        ++m_detailsPending;
        source->details(item, generation);
    }
    if (!m_versions.contains(item->key())) {
        requestVersions(item, generation);
    } else if (item->provider != Provider::Technic) {
        const auto versions = panelVersions(*item, m_versions.value(item->key()));
        if (!versions.isEmpty()) {
            const QString key = changelogKey(*item, versions.first().id);
            if (const auto cached = m_changelogs.constFind(key); cached != m_changelogs.cend()) {
                m_panel->setWhatsNew(cached.value());
            } else {
                source->changelog(item, versions.first().id, generation);
            }
        }
    }
}

void ContentSection::requestVersions(const ContentItemPtr& item, quint64 generation)
{
    if (auto* source = m_env.sources.source(item->provider)) {
        ++m_detailsPending;
        m_versionsTimer.start(m_versionsTimeoutMs);
        source->versions(item, generation);
    }
}

// ---- idle / repaint ----

void ContentSection::repaintImages()
{
    m_view->viewport()->update();
    refreshAccessoryImages();
    m_panel->refreshImages();
    if (m_images->fading()) {
        m_repaintTimer.start(16);
    }
}

bool ContentSection::isBusy() const
{
    return !m_started || m_pager.isLoading() || m_firstPagePending || m_detailsPending > 0 || hasAccessoryWork() ||
           m_images->inFlightCount() > 0 || m_images->queuedCount() > 0 || m_images->fading();
}

void ContentSection::callWhenIdle(const std::function<void()>& done, int maxMs)
{
    auto* timer = new QTimer(this);
    auto idleTicks = std::make_shared<int>(0);
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();
    timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, [this, timer, idleTicks, clock, done, maxMs]() {
        *idleTicks = isBusy() ? 0 : *idleTicks + 1;
        if (*idleTicks >= 3 || clock->elapsed() >= maxMs) {
            timer->stop();
            timer->deleteLater();
            done();
        }
    });
    timer->start();
}

}  // namespace Zero::Content
