// SPDX-License-Identifier: GPL-3.0-only
#include "LibraryPage.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include "Application.h"
#include "InstanceList.h"
#include "FilterChipBar.h"
#include "InstanceTableModel.h"
#include "ZeroInstanceDelegate.h"
#include "settings/SettingsObject.h"
#include "ui/instanceview/InstanceProxyModel.h"
#include "ui/instanceview/InstanceView.h"
#include "ui/zero/ZeroEmptyState.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroTransition.h"
#include "zero/profiles/InstanceProfile.h"
#include "ui/zero/ZeroReveal.h"

namespace Zero {

namespace {

/// Fixed 36 px rows for the list mode.
class RowDelegate : public QStyledItemDelegate {
   public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(Tokens::navItemHeight);
        return size;
    }
};

QToolButton* makeToggle(const QString& tooltip, QWidget* parent)
{
    auto* button = new QToolButton(parent);
    button->setCheckable(true);
    button->setAutoRaise(false);
    button->setToolTip(tooltip);
    button->setFixedSize(Tokens::controlHeight, Tokens::controlHeight);
    button->setIconSize(QSize(16, 16));
    Style::setProperty(button, "zeroVariant", "ghost");
    return button;
}

}  // namespace

LibraryPage::LibraryPage(InstanceView* grid, InstanceProxyModel* proxy, QWidget* parent)
    : ShellPage(parent), m_grid(grid), m_proxy(proxy), m_table(new InstanceTableModel(proxy, this)), m_sort(new QSortFilterProxyModel(this))
{
    // ----- toolbar row (content) -----
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search instances"));
    m_search->setFixedWidth(280);
    m_search->setClearButtonEnabled(true);
    m_search->installEventFilter(this);

    m_sortCombo = new QComboBox(this);
    m_sortCombo->setToolTip(tr("Sort instances"));
    m_sortCombo->addItem(tr("Name"), QStringLiteral("Name"));
    m_sortCombo->addItem(tr("Last played"), QStringLiteral("LastLaunch"));
    m_sortCombo->addItem(tr("Most played"), QStringLiteral("Playtime"));

    m_toolbar = new QWidget(this);
    auto* toolbarRow = new QHBoxLayout(m_toolbar);
    toolbarRow->setContentsMargins(0, 0, 0, Tokens::spaceLg);
    toolbarRow->setSpacing(Tokens::spaceLg);
    m_chips = new FilterChipBar(this);
    toolbarRow->addWidget(m_search);
    toolbarRow->addWidget(m_chips, 1);
    toolbarRow->addWidget(m_sortCombo);

    // ----- top bar actions: view toggle + New instance -----
    auto* viewGroup = new QButtonGroup(this);
    viewGroup->setExclusive(true);
    m_gridButton = makeToggle(tr("Grid view"), this);
    m_listButton = makeToggle(tr("List view"), this);
    viewGroup->addButton(m_gridButton);
    viewGroup->addButton(m_listButton);
    auto* toggles = new QWidget(this);
    auto* toggleLayout = new QHBoxLayout(toggles);
    toggleLayout->setContentsMargins(0, 0, 0, 0);
    toggleLayout->setSpacing(2);
    toggleLayout->addWidget(m_gridButton);
    toggleLayout->addWidget(m_listButton);

    auto* newButton = new QPushButton(tr("New instance"), this);
    Style::setProperty(newButton, "zeroVariant", "primary");
    newButton->setIconSize(QSize(16, 16));
    m_newButton = newButton;
    connect(newButton, &QPushButton::clicked, this, &LibraryPage::newInstanceRequested);

    auto* actions = new QWidget(this);
    auto* actionsRow = new QHBoxLayout(actions);
    actionsRow->setContentsMargins(0, 0, 0, 0);
    actionsRow->setSpacing(Tokens::spaceSm);
    actionsRow->addWidget(toggles);
    actionsRow->addWidget(newButton);
    setTopBarActions(actions);

    // ----- grid -----
    m_grid->setParent(this);
    m_gridReveal = Motion::installItemReveal(m_grid);
    m_grid->setGridMetrics(ZeroInstanceDelegate::kCellWidth, Tokens::spaceLg - 4);
    m_grid->setStyleSheet(QStringLiteral("QAbstractItemView { background: transparent; border: none; }"));
    m_grid->viewport()->setAutoFillBackground(false);

    // ----- list -----
    m_sort->setSourceModel(m_table);
    m_sort->setSortRole(InstanceTableModel::SortRole);
    m_sort->setSortCaseSensitivity(Qt::CaseInsensitive);
    m_sort->setSortLocaleAware(true);

    m_list = new QTreeView(this);
    m_list->setModel(m_sort);
    m_list->setItemDelegate(new RowDelegate(m_list));
    m_listReveal = Motion::installItemReveal(m_list);
    m_list->setRootIsDecorated(false);
    m_list->setItemsExpandable(false);
    m_list->setUniformRowHeights(true);
    m_list->setAlternatingRowColors(false);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setIconSize(QSize(20, 20));
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setAttribute(Qt::WA_MacShowFocusRect, false);
    m_list->setSortingEnabled(true);
    m_list->sortByColumn(-1, Qt::AscendingOrder);  // start in the grid's order (grouped, then by the sort mode)
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(InstanceTableModel::NameColumn, QHeaderView::Stretch);
    for (int c : { InstanceTableModel::VersionColumn, InstanceTableModel::LoaderColumn, InstanceTableModel::GroupColumn,
                   InstanceTableModel::LastPlayedColumn, InstanceTableModel::PlaytimeColumn })
        m_list->header()->setSectionResizeMode(c, QHeaderView::Interactive);
    m_list->setColumnWidth(InstanceTableModel::VersionColumn, 84);
    m_list->setColumnWidth(InstanceTableModel::LoaderColumn, 90);
    m_list->setColumnWidth(InstanceTableModel::GroupColumn, 110);
    m_list->setColumnWidth(InstanceTableModel::LastPlayedColumn, 120);
    m_list->setColumnWidth(InstanceTableModel::PlaytimeColumn, 100);
    m_list->installEventFilter(this);
    connect(m_list, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const QModelIndex idx = m_list->indexAt(pos);
        if (!idx.isValid())
            return;
        m_list->setCurrentIndex(idx);
        emit listContextMenuRequested(m_list->viewport()->mapToGlobal(pos), m_table->sourceIndex(m_sort->mapToSource(idx)));
    });
    connect(m_list, &QAbstractItemView::activated, this, [this](const QModelIndex& idx) {
        emit instanceActivated(m_table->sourceIndex(m_sort->mapToSource(idx)));
    });
    connect(m_list->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current) { onListCurrentChanged(current); });

    // ----- collection stack -----
    m_collection = new QStackedWidget(this);
    m_collection->addWidget(m_grid);
    m_collection->addWidget(m_list);
    m_noMatches = new ZeroEmptyState(QStringLiteral("search"), tr("No instances match"), tr("Try a different search or clear the filters."),
                                     tr("Clear filters"), this);
    connect(m_noMatches, &ZeroEmptyState::actionTriggered, this, &LibraryPage::clearFilters);
    m_collection->addWidget(m_noMatches);

    m_details = new InstanceDetailsPanel(this);
    connect(m_details, &InstanceDetailsPanel::renameRequested, this, &LibraryPage::renameRequested);
    connect(m_details, &InstanceDetailsPanel::changeIconRequested, this, &LibraryPage::changeIconRequested);
    connect(m_details, &InstanceDetailsPanel::favoriteToggleRequested, this, &LibraryPage::favoriteToggleRequested);
    connect(m_details, &InstanceDetailsPanel::setArtworkRequested, this, &LibraryPage::setArtworkRequested);
    connect(m_details, &InstanceDetailsPanel::checkModsRequested, this, &LibraryPage::checkModsRequested);
    connect(m_details, &InstanceDetailsPanel::removeArtworkRequested, this, &LibraryPage::removeArtworkRequested);

    auto* collectionRow = new QWidget(this);
    auto* row = new QHBoxLayout(collectionRow);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(Tokens::spaceLg);
    row->addWidget(m_collection, 1);
    row->addWidget(m_details);

    auto* libraryEmpty = new ZeroEmptyState(QStringLiteral("library-big"), tr("Your library is empty"),
                                            tr("Create an instance, install a modpack or import one you already have."), tr("New instance"), this);
    connect(libraryEmpty, &ZeroEmptyState::actionTriggered, this, &LibraryPage::newInstanceRequested);
    connect(libraryEmpty->addSecondaryAction(tr("Import")), &QPushButton::clicked, this, &LibraryPage::importRequested);

    m_body = new QStackedWidget(this);
    m_body->addWidget(collectionRow);
    m_body->addWidget(libraryEmpty);

    auto* page = new QVBoxLayout(this);
    page->setContentsMargins(Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl);
    page->setSpacing(0);
    page->addWidget(m_toolbar);
    page->addWidget(m_body, 1);

    // ----- behavior -----
    // filter chips: the proxy asks this predicate for every row; groups keep working because they derive from the accepted rows
    m_proxy->setPredicate([this](MinecraftInstance* instance) {
        return m_chip.kind == LibraryFilter::Kind::All || LibraryFilter::chipMatches(m_chip, Profile::libraryFacts(instance, QString()));
    });
    m_chipTimer = new QTimer(this);
    m_chipTimer->setSingleShot(true);
    m_chipTimer->setInterval(0);
    connect(m_chipTimer, &QTimer::timeout, this, &LibraryPage::rebuildChips);
    connect(m_chips, &FilterChipBar::chipActivated, this, [this](const LibraryFilter::Chip& chip) {
        // clicking the selected chip again goes back to All
        setChip(chip == m_chip && chip.kind != LibraryFilter::Kind::All ? LibraryFilter::Chip{} : chip);
    });
    connect(Profile::Notifier::instance(), &Profile::Notifier::profileChanged, this, [this]() { scheduleChipRefresh(); });
    connect(APPLICATION->instances(), &QAbstractItemModel::rowsInserted, this, [this]() { scheduleChipRefresh(); });
    connect(APPLICATION->instances(), &QAbstractItemModel::rowsRemoved, this, [this]() { scheduleChipRefresh(); });
    connect(APPLICATION->instances(), &QAbstractItemModel::modelReset, this, [this]() { scheduleChipRefresh(); });
    connect(APPLICATION->instances(), &QAbstractItemModel::dataChanged, this, [this]() { scheduleChipRefresh(); });
    rebuildChips();

    connect(m_search, &QLineEdit::textChanged, this, [this]() { applyFilters(false); });
    auto* findShortcut = new QShortcut(QKeySequence(QKeySequence::Find), this);
    connect(findShortcut, &QShortcut::activated, this, [this]() { focusSearch(); });

    connect(m_sortCombo, &QComboBox::activated, this, [this](int i) {
        APPLICATION->settings()->set("InstSortMode", m_sortCombo->itemData(i).toString());
        m_proxy->invalidate();
        m_proxy->sort(0);
        revealItems();
    });
    syncSortFromSettings();

    connect(m_gridButton, &QAbstractButton::clicked, this, [this]() { setViewMode(false); });
    connect(m_listButton, &QAbstractButton::clicked, this, [this]() { setViewMode(true); });

    // keep counts / empty states live (instances added, removed, renamed, filtered)
    connect(m_proxy, &QAbstractItemModel::rowsInserted, this, [this]() { updateState(); });
    connect(m_proxy, &QAbstractItemModel::rowsRemoved, this, [this]() { updateState(); });
    connect(m_proxy, &QAbstractItemModel::modelReset, this, [this]() { updateState(); });
    connect(m_proxy, &QAbstractItemModel::layoutChanged, this, [this]() { updateState(); });
    connect(APPLICATION->instances(), &QAbstractItemModel::rowsInserted, this, [this]() { updateState(); });
    connect(APPLICATION->instances(), &QAbstractItemModel::rowsRemoved, this, [this]() { updateState(); });
    connect(APPLICATION->instances(), &QAbstractItemModel::modelReset, this, [this]() { updateState(); });

    // selection: the grid's selection model is the single source of truth
    connect(m_grid->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& current) {
        if (current.isValid() && !m_filtering)
            m_rememberedId = current.data(InstanceList::InstanceIDRole).toString();
        syncListFromGrid();
    });
    connect(m_table, &QAbstractItemModel::modelReset, this, [this]() { syncListFromGrid(); });
    connect(m_table, &QAbstractItemModel::layoutChanged, this, [this]() { syncListFromGrid(); });

    connect(&Theme::instance(), &Theme::changed, this, &LibraryPage::refreshIcons);
    refreshIcons();

    const bool list = APPLICATION->settings()->get("ZeroLibraryView").toString() == QLatin1String("list");
    setViewMode(list);
    updateState();
}

void LibraryPage::refreshIcons()
{
    const auto& t = Theme::current();
    m_gridButton->setIcon(Icons::icon(QStringLiteral("layout-grid"), t.textSecondary));
    m_listButton->setIcon(Icons::icon(QStringLiteral("list"), t.textSecondary));
    m_newButton->setIcon(Icons::icon(QStringLiteral("plus"), t.onAccent));
    const QIcon searchIcon = Icons::icon(QStringLiteral("search"), t.textTertiary);
    if (m_searchAction)
        m_searchAction->setIcon(searchIcon);
    else
        m_searchAction = m_search->addAction(searchIcon, QLineEdit::LeadingPosition);
}

void LibraryPage::setActions(const LibraryActions& actions)
{
    m_details->setActions(actions);
}

void LibraryPage::setCurrentInstance(MinecraftInstance* instance)
{
    m_details->setInstance(instance);
}

bool LibraryPage::isListMode() const
{
    return m_listMode;
}

QAbstractItemView* LibraryPage::activeView() const
{
    return m_listMode ? static_cast<QAbstractItemView*>(m_list) : static_cast<QAbstractItemView*>(m_grid);
}

QAbstractItemView* LibraryPage::listView() const
{
    return m_list;
}

void LibraryPage::beginRename()
{
    if (!m_listMode && m_collection->currentWidget() == m_grid) {
        if (m_grid->currentIndex().isValid())
            m_grid->edit(m_grid->currentIndex());
    } else {
        m_details->beginRename();
    }
}

void LibraryPage::focusSearch()
{
    m_search->setFocus(Qt::ShortcutFocusReason);
    m_search->selectAll();
}

void LibraryPage::setSearchText(const QString& text)
{
    m_search->setText(text);
}

void LibraryPage::syncSortFromSettings()
{
    const QString mode = APPLICATION->settings()->get("InstSortMode").toString();
    int index = m_sortCombo->findData(mode);
    if (index < 0 && !mode.isEmpty()) {
        // a mode the settings dialog offers but this page does not (e.g. Playtime): show it while it is active
        m_sortCombo->addItem(mode == QLatin1String("Playtime") ? tr("Playtime") : mode, mode);
        index = m_sortCombo->count() - 1;
    }
    m_sortCombo->setCurrentIndex(index);
}

void LibraryPage::setViewMode(bool list)
{
    // grid <-> list: crossfade over the collection area (the real view is switched and usable immediately).
    const bool switching = list != m_listMode && m_collection && m_collection->isVisible();
    const Transition::Snapshot before = switching ? Transition::capture(m_collection) : Transition::Snapshot();
    applyViewMode(list);
    if (!before.isNull())
        Transition::run(m_collection, before, Transition::Kind::Crossfade, Tokens::durFast);
}

void LibraryPage::applyViewMode(bool list)
{
    m_listMode = list;
    (list ? m_listButton : m_gridButton)->setChecked(true);
    if (APPLICATION->settings()->get("ZeroLibraryView").toString() != (list ? QLatin1String("list") : QLatin1String("grid")))
        APPLICATION->settings()->set("ZeroLibraryView", list ? QStringLiteral("list") : QStringLiteral("grid"));
    updateState();
    syncListFromGrid();
    activeView()->setFocus();
}

void LibraryPage::setChip(const LibraryFilter::Chip& chip)
{
    if (chip == m_chip)
        return;
    m_chip = chip;
    m_chips->setActive(chip);
    applyFilters(true);
}

void LibraryPage::clearFilters()
{
    m_chip = LibraryFilter::Chip{};
    m_chips->setActive(m_chip);
    {
        const QSignalBlocker blocker(m_search);
        m_search->clear();
    }
    applyFilters(true);
    activeView()->setFocus();
}

void LibraryPage::scheduleChipRefresh()
{
    m_chipTimer->start();
}

/// Chips follow the library: loaders and tags appear and disappear with the instances, and an active chip that vanished
/// (the last instance with that tag lost it) falls back to All.
void LibraryPage::rebuildChips()
{
    QList<LibraryFilter::Facts> facts;
    auto* list = APPLICATION->instances();
    for (int i = 0; i < list->count(); ++i)
        facts << Profile::libraryFacts(list->at(i), QString());
    const auto chips = LibraryFilter::buildChips(facts);
    const LibraryFilter::Chip resolved = LibraryFilter::resolveChip(m_chip, chips);
    const bool vanished = resolved != m_chip;
    m_chip = resolved;
    m_chips->setChips(chips, m_chip);
    if (vanished)
        applyFilters(true);
}

void LibraryPage::applyFilters(bool chipChanged)
{
    const QString keep = m_rememberedId;
    m_filtering = true;  // Qt moves the current index to a neighbor when its row is filtered out: do not remember that
    m_proxy->setFilterText(m_search->text());
    if (chipChanged) {
        m_proxy->refilter();
        revealItems();
    }
    // The selected instance comes back when the filter shows it again; while it is hidden nothing is selected
    // (the details panel would otherwise show an instance that is not highlighted anywhere).
    const QModelIndex source = keep.isEmpty() ? QModelIndex() : APPLICATION->instances()->getInstanceIndexById(keep);
    const QModelIndex mapped = source.isValid() ? m_proxy->mapFromSource(source) : QModelIndex();
    if (mapped.isValid()) {
        if (m_grid->currentIndex() != mapped || !m_grid->selectionModel()->isSelected(mapped))
            m_grid->selectionModel()->setCurrentIndex(mapped, QItemSelectionModel::ClearAndSelect);
    } else if (m_grid->currentIndex().isValid()) {
        m_grid->selectionModel()->clear();  // clears the selection and the current index
    }
    m_filtering = false;
    updateState();
}

void LibraryPage::revealItems()
{
    if (Motion::ItemReveal* reveal = m_listMode ? m_listReveal : m_gridReveal)
        reveal->start();
}

void LibraryPage::updateState()
{
    const int total = APPLICATION->instances()->count();
    const int shown = m_proxy->rowCount();
    const bool filtering = !m_proxy->filterText().isEmpty() || m_chip.kind != LibraryFilter::Kind::All;

    QString subtitle;
    if (total == 0)
        subtitle = tr("No instances");
    else if (filtering)
        subtitle = total == 1 ? tr("%1 of 1 instance").arg(shown) : tr("%1 of %2 instances").arg(shown).arg(total);
    else
        subtitle = total == 1 ? tr("1 instance") : tr("%1 instances").arg(total);
    if (subtitle != m_subtitle) {
        m_subtitle = subtitle;
        emit chromeChanged();
    }

    // nothing to search, sort or switch views for in an empty library
    m_toolbar->setVisible(total > 0);
    m_gridButton->parentWidget()->setVisible(total > 0);

    if (total == 0) {
        m_body->setCurrentIndex(1);
        return;
    }
    m_body->setCurrentIndex(0);
    m_details->setVisible(true);
    if (shown == 0)
        m_collection->setCurrentIndex(2);
    else
        m_collection->setCurrentIndex(m_listMode ? 1 : 0);
}

void LibraryPage::syncListFromGrid()
{
    if (m_syncing)
        return;
    m_syncing = true;
    const QModelIndex current = m_grid->currentIndex();
    if (current.isValid()) {
        const QModelIndex mapped = m_sort->mapFromSource(m_table->fromSource(current));
        if (mapped.isValid() && m_list->currentIndex() != mapped) {
            m_list->selectionModel()->setCurrentIndex(mapped, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            m_list->scrollTo(mapped);
        }
    } else {
        m_list->selectionModel()->clearSelection();
        m_list->setCurrentIndex(QModelIndex());
    }
    m_syncing = false;
}

void LibraryPage::onListCurrentChanged(const QModelIndex& current)
{
    if (m_syncing || !current.isValid())
        return;
    m_syncing = true;
    const QModelIndex source = m_table->sourceIndex(m_sort->mapToSource(current));
    if (source.isValid() && m_grid->currentIndex() != source)
        m_grid->selectionModel()->setCurrentIndex(source, QItemSelectionModel::ClearAndSelect);
    m_syncing = false;
}

bool LibraryPage::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) {
            m_search->clear();
            activeView()->setFocus();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

}  // namespace Zero
