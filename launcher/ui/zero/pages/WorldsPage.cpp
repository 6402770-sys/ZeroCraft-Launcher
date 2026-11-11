// SPDX-License-Identifier: GPL-3.0-only
#include "WorldsPage.h"

#include <algorithm>
#include <QAbstractItemView>
#include <QComboBox>
#include <QDeadlineTimer>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include "Application.h"
#include "InstanceList.h"
#include "WorldDetailsPanel.h"
#include "WorldIcon.h"
#include "WorldRows.h"
#include "minecraft/MinecraftInstance.h"
#include "settings/SettingsObject.h"
#include "ui/zero/ZeroEmptyState.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroToast.h"
#include "zero/backup/WorldBackupJobs.h"
#include "ui/zero/ZeroReveal.h"
#include "zero/favorites/FavoriteStore.h"

namespace Zero {

namespace {
QList<Worlds::InstanceSource> sourcesFromApplication()
{
    QList<Worlds::InstanceSource> out;
    auto* list = APPLICATION->instances();
    for (int i = 0; i < list->count(); ++i) {
        MinecraftInstance* inst = list->at(i);
        out.append({ inst->id(), inst->name(), inst->worldDir() });
    }
    return out;
}
}  // namespace

WorldsPage::WorldsPage(QWidget* parent)
    : ShellPage(parent)
    , m_scanner(new Worlds::WorldScanner(&sourcesFromApplication, this))
    , m_backupJobs(Backup::WorldBackupJobs::global())
    , m_model(new WorldRowModel(this))
{
    // ----- toolbar row (content): search left, instance filter and sort right -----
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search worlds"));
    m_search->setFixedWidth(280);
    m_search->setClearButtonEnabled(true);
    m_search->installEventFilter(this);

    m_instanceCombo = new QComboBox(this);
    m_instanceCombo->setToolTip(tr("Filter by instance"));
    m_instanceCombo->addItem(tr("All instances"), QString());

    m_sortCombo = new QComboBox(this);
    m_sortCombo->setToolTip(tr("Sort worlds"));
    m_sortCombo->addItem(tr("Last played"), Worlds::sortModeToString(Worlds::SortMode::Recent));
    m_sortCombo->addItem(tr("Name"), Worlds::sortModeToString(Worlds::SortMode::Name));
    m_sortCombo->addItem(tr("Size"), Worlds::sortModeToString(Worlds::SortMode::Size));
    m_sortCombo->addItem(tr("Instance"), Worlds::sortModeToString(Worlds::SortMode::Instance));

    m_toolbar = new QWidget(this);
    auto* toolbarRow = new QHBoxLayout(m_toolbar);
    toolbarRow->setContentsMargins(0, 0, 0, Tokens::spaceLg);
    toolbarRow->setSpacing(Tokens::spaceLg);
    toolbarRow->addWidget(m_search);
    toolbarRow->addStretch(1);
    m_favoritesOnly = new QPushButton(tr("Favorites"), this);
    Style::setProperty(m_favoritesOnly, "zeroVariant", "ghost");
    m_favoritesOnly->setCheckable(true);
    m_favoritesOnly->setIconSize(QSize(16, 16));
    m_favoritesOnly->setToolTip(tr("Show only your favorites. Click the star on a row to add one."));
    m_favoritesOnly->setVisible(Favorites::FavoriteStore::instance() != nullptr);
    toolbarRow->addWidget(m_favoritesOnly);
    toolbarRow->addWidget(m_instanceCombo);
    toolbarRow->addWidget(m_sortCombo);

    // ----- top bar action: Refresh -----
    m_refresh = new QPushButton(tr("Refresh"), this);
    Style::setProperty(m_refresh, "zeroVariant", "ghost");
    m_refresh->setIconSize(QSize(16, 16));
    m_refresh->setToolTip(tr("Rescan worlds"));
    connect(m_refresh, &QPushButton::clicked, this, &WorldsPage::rescan);
    auto* actions = new QWidget(this);
    auto* actionsRow = new QHBoxLayout(actions);
    actionsRow->setContentsMargins(0, 0, 0, 0);
    actionsRow->setSpacing(Tokens::spaceSm);
    actionsRow->addWidget(m_refresh);
    setTopBarActions(actions);

    // ----- list -----
    m_list = new QListView(this);
    m_list->setModel(m_model);
    m_list->setItemDelegate(new WorldRowDelegate(m_list));
    m_reveal = Motion::installItemReveal(m_list);
    m_list->setUniformItemSizes(true);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setMouseTracking(true);
    m_list->setAttribute(Qt::WA_MacShowFocusRect, false);
    m_list->setStyleSheet(QStringLiteral("QAbstractItemView { background: transparent; border: none; }"));
    m_list->viewport()->setAutoFillBackground(false);
    connect(m_list->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() {
        if (m_applying)
            return;
        const QModelIndexList selected = m_list->selectionModel()->selectedIndexes();
        m_selectedKey = selected.isEmpty() ? QString() : selected.first().data(WorldRowModel::KeyRole).toString();
        syncDetails();
    });

    m_noMatches = new ZeroEmptyState(QStringLiteral("earth"), tr("No worlds match"), tr("Try a different search or clear the filters."),
                                     tr("Clear filters"), this);
    connect(m_noMatches, &ZeroEmptyState::actionTriggered, this, &WorldsPage::clearFilters);

    m_collection = new QStackedWidget(this);
    m_collection->addWidget(m_list);
    m_collection->addWidget(m_noMatches);

    m_details = new WorldDetailsPanel(this);
    connect(m_details, &WorldDetailsPanel::showInstanceRequested, this, &WorldsPage::showInstanceRequested);
    m_details->setBackupJobs(m_backupJobs);
    if (m_backupJobs) {
        // automatic backups are summarised by one toast per session (MainWindow), never one per world
        connect(m_backupJobs, &Backup::WorldBackupJobs::jobFinished, this,
                [this](const QString&, bool ok, const QString& message, bool automatic) {
                    if (!automatic)
                        ZeroToast::show(window(), message, ok ? ZeroToast::Kind::Success : ZeroToast::Kind::Error);
                });
        connect(m_backupJobs, &Backup::WorldBackupJobs::worldRestored, this, &WorldsPage::rescan);
    }

    auto* collectionRow = new QWidget(this);
    auto* row = new QHBoxLayout(collectionRow);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(Tokens::spaceLg);
    row->addWidget(m_collection, 1);
    row->addWidget(m_details);

    auto* noWorlds = new ZeroEmptyState(QStringLiteral("earth"), tr("No worlds yet"),
                                        tr("Worlds appear here after you play singleplayer in any instance."), {}, this);

    m_body = new QStackedWidget(this);
    m_body->addWidget(collectionRow);
    m_body->addWidget(noWorlds);

    auto* page = new QVBoxLayout(this);
    page->setContentsMargins(Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl);
    page->setSpacing(0);
    page->addWidget(m_toolbar);
    page->addWidget(m_body, 1);

    // ----- behavior -----
    connect(m_search, &QLineEdit::textChanged, this, [this]() { applyFilters(); });
    auto* findShortcut = new QShortcut(QKeySequence(QKeySequence::Find), this);
    connect(findShortcut, &QShortcut::activated, this, &WorldsPage::focusSearch);

    m_instanceFilter = APPLICATION->settings()->get("ZeroWorldsInstance").toString();
    m_sort = Worlds::sortModeFromString(APPLICATION->settings()->get("ZeroWorldsSort").toString());
    m_sortCombo->setCurrentIndex(qMax(0, m_sortCombo->findData(Worlds::sortModeToString(m_sort))));
    connect(m_instanceCombo, &QComboBox::activated, this, [this](int i) {
        m_instanceFilter = m_instanceCombo->itemData(i).toString();
        APPLICATION->settings()->set("ZeroWorldsInstance", m_instanceFilter);
        applyFilters();
        m_reveal->start();
    });
    connect(m_sortCombo, &QComboBox::activated, this, [this](int i) {
        m_sort = Worlds::sortModeFromString(m_sortCombo->itemData(i).toString());
        APPLICATION->settings()->set("ZeroWorldsSort", Worlds::sortModeToString(m_sort));
        applyFilters();
        m_reveal->start();
    });
    m_favoritesOnly->setChecked(APPLICATION->settings()->get("ZeroWorldsFavoritesOnly").toBool());
    connect(m_favoritesOnly, &QPushButton::toggled, this, [this](bool on) {
        APPLICATION->settings()->set("ZeroWorldsFavoritesOnly", on);
        refreshFavoriteIcon();
        applyFilters();
        m_reveal->start();
    });
    if (auto* store = Favorites::FavoriteStore::instance()) {
        // a star clicked on a row or in the details panel: favorites move to the top (or out of the favorites-only view)
        connect(store, &Favorites::FavoriteStore::changed, this, [this](Favorites::Kind kind, const QString&, bool) {
            if (kind == Favorites::Kind::World)
                applyFilters();
        });
    }

    connect(m_scanner, &Worlds::WorldScanner::worldsChanged, this, &WorldsPage::onWorldsChanged);
    connect(m_scanner, &Worlds::WorldScanner::sizeReady, this, [this](const QString& key, qint64 bytes) {
        m_model->setSize(key, bytes);
        if (key == m_selectedKey)
            syncDetails();
    });
    connect(m_scanner, &Worlds::WorldScanner::scanningChanged, this, [this](bool scanning) {
        m_refresh->setEnabled(!scanning);
        if (!scanning && m_sort == Worlds::SortMode::Size)
            applyFilters();  // sizes are complete now: apply the order once instead of reordering on every size
    });

    // instances added / removed / reset: rescan (debounced); running state and names: cheap in-place updates
    m_rescanTimer = new QTimer(this);
    m_rescanTimer->setSingleShot(true);
    m_rescanTimer->setInterval(kInstanceDebounceMs);
    connect(m_rescanTimer, &QTimer::timeout, this, [this]() {
        if (isVisible())
            rescan();
        else
            m_stale = true;
    });
    auto* instances = APPLICATION->instances();
    connect(instances, &QAbstractItemModel::rowsInserted, this, &WorldsPage::scheduleRescan);
    connect(instances, &QAbstractItemModel::rowsRemoved, this, &WorldsPage::scheduleRescan);
    connect(instances, &QAbstractItemModel::modelReset, this, &WorldsPage::scheduleRescan);
    connect(instances, &QAbstractItemModel::dataChanged, this, &WorldsPage::syncRunning);

    connect(&Theme::instance(), &Theme::changed, this, &WorldsPage::refreshIcons);
    refreshIcons();
    syncRunning();
    updateState();
}

void WorldsPage::refreshFavoriteIcon()
{
    const auto& t = Theme::current();
    m_favoritesOnly->setIcon(m_favoritesOnly->isChecked() ? Icons::filledIcon(QStringLiteral("star"), t.highlight)
                                                          : Icons::icon(QStringLiteral("star"), t.textSecondary));
}

void WorldsPage::refreshIcons()
{
    refreshFavoriteIcon();
    const auto& t = Theme::current();
    m_refresh->setIcon(Icons::icon(QStringLiteral("refresh-cw"), t.textSecondary));
    const QIcon searchIcon = Icons::icon(QStringLiteral("search"), t.textTertiary);
    if (m_searchAction)
        m_searchAction->setIcon(searchIcon);
    else
        m_searchAction = m_search->addAction(searchIcon, QLineEdit::LeadingPosition);
    m_list->viewport()->update();
}

void WorldsPage::focusSearch()
{
    m_search->setFocus(Qt::ShortcutFocusReason);
    m_search->selectAll();
}

void WorldsPage::setSearchText(const QString& text)
{
    m_search->setText(text);
}

void WorldsPage::selectWorld(const QString& key)
{
    m_selectedKey = key;
    applyFilters();
}

void WorldsPage::rescan()
{
    m_scanRequested = true;
    m_stale = false;
    m_sinceScan.start();
    m_scanner->rescan();
}

void WorldsPage::scheduleRescan()
{
    if (m_scanRequested)  // never scanned (page not opened yet): the first show scans anyway
        m_rescanTimer->start();
}

void WorldsPage::callWhenIdle(const std::function<void()>& done, int maxMs)
{
    auto* timer = new QTimer(this);
    const QDeadlineTimer deadline(maxMs);
    connect(timer, &QTimer::timeout, this, [this, timer, deadline, done]() {
        if ((m_scanner->hasScanned() && !m_scanner->isScanning()) || deadline.hasExpired()) {
            timer->deleteLater();
            done();
        }
    });
    timer->start(50);
}

void WorldsPage::showEvent(QShowEvent* event)
{
    ShellPage::showEvent(event);
    if (!m_scanRequested || m_stale || m_sinceScan.hasExpired(kStaleMs))
        rescan();
}

void WorldsPage::onWorldsChanged()
{
    WorldIcon::clearCache();  // an icon.png may have been replaced
    if (!m_selectedKey.isEmpty()) {
        bool still = false;
        for (const auto& e : m_scanner->entries()) {
            if (e.key() == m_selectedKey) {
                still = true;
                break;
            }
        }
        if (!still)
            m_selectedKey.clear();  // the world vanished: nothing is selected
    }
    rebuildInstanceCombo();
    applyFilters();
}

void WorldsPage::rebuildInstanceCombo()
{
    QHash<QString, QString> names;  // id -> name, only instances that have worlds
    for (const auto& e : m_scanner->entries())
        names.insert(e.instanceId, e.instanceName);
    QList<QPair<QString, QString>> sorted;
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        sorted.append({ it.value(), it.key() });
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        const int c = a.first.localeAwareCompare(b.first);
        return c != 0 ? c < 0 : a.second < b.second;
    });

    const QSignalBlocker blocker(m_instanceCombo);
    m_instanceCombo->clear();
    m_instanceCombo->addItem(tr("All instances"), QString());
    for (const auto& p : std::as_const(sorted))
        m_instanceCombo->addItem(p.first, p.second);
    m_instanceCombo->setCurrentIndex(qMax(0, m_instanceCombo->findData(m_instanceFilter)));
}

QString WorldsPage::effectiveInstanceFilter() const
{
    return !m_instanceFilter.isEmpty() && m_instanceCombo->findData(m_instanceFilter) >= 0 ? m_instanceFilter : QString();
}

void WorldsPage::applyFilters()
{
    const auto* store = Favorites::FavoriteStore::instance();
    const bool favoritesOnly = m_favoritesOnly->isChecked();
    const QString instance = effectiveInstanceFilter();
    const QString text = m_search->text();
    QList<Worlds::WorldEntry> rows;
    for (const auto& e : m_scanner->entries()) {
        if (!instance.isEmpty() && e.instanceId != instance)
            continue;
        if (favoritesOnly && !(store && store->contains(Favorites::Kind::World, e.key())))
            continue;
        if (!Worlds::matchesSearch(e, text))
            continue;
        rows.append(e);
    }
    Worlds::sortWorlds(rows, m_sort);
    if (store)  // favorites first, each group in the chosen order
        std::stable_partition(rows.begin(), rows.end(), [store](const auto& r) { return store->contains(Favorites::Kind::World, r.key()); });

    // A rescan that found the same worlds must not reset the view (a reset paints one empty frame: the list blinks).
    if (rows == m_model->entries()) {
        updateState();
        return;
    }

    // The selected world comes back when the filter shows it again; while it is hidden nothing is highlighted.
    const int scroll = m_list->verticalScrollBar()->value();
    const bool firstFill = m_model->rowCount() == 0;
    m_applying = true;
    m_model->setEntries(rows);
    m_list->doItemsLayout();  // lay out now (no empty frame, and the scroll position below is not clamped)
    if (firstFill && !rows.isEmpty())
        m_reveal->start();
    const int row = m_selectedKey.isEmpty() ? -1 : m_model->rowForKey(m_selectedKey);
    if (row >= 0) {
        const QModelIndex index = m_model->index(row);
        m_list->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
    }
    m_list->verticalScrollBar()->setValue(scroll);
    m_applying = false;

    syncDetails();
    updateState();
}

void WorldsPage::syncDetails()
{
    const int row = m_selectedKey.isEmpty() ? -1 : m_model->rowForKey(m_selectedKey);
    if (row >= 0) {
        const Worlds::WorldEntry entry = m_model->entry(row);
        m_details->setInstanceRunning(m_model->isRunning(entry.instanceId));
        m_details->setWorld(entry);
    } else {
        m_details->clear();
    }
}

void WorldsPage::updateState()
{
    const int total = static_cast<int>(m_scanner->entries().size());
    const int shown = m_model->rowCount();
    const bool filtering = !m_search->text().trimmed().isEmpty() || !effectiveInstanceFilter().isEmpty() || m_favoritesOnly->isChecked();

    QString subtitle;
    if (!m_scanner->hasScanned()) {
        subtitle = tr("Scanning…");
    } else if (total == 0) {
        subtitle = tr("No worlds");
    } else if (filtering) {
        subtitle = total == 1 ? tr("%1 of 1 world").arg(shown) : tr("%1 of %2 worlds").arg(shown).arg(total);
    } else {
        QSet<QString> instances;
        for (const auto& e : m_scanner->entries())
            instances.insert(e.instanceId);
        const int m = static_cast<int>(instances.size());
        subtitle = tr("%1 in %2").arg(total == 1 ? tr("1 world") : tr("%1 worlds").arg(total), m == 1 ? tr("1 instance") : tr("%1 instances").arg(m));
    }
    if (subtitle != m_subtitle) {
        m_subtitle = subtitle;
        emit chromeChanged();
    }

    const bool empty = m_scanner->hasScanned() && total == 0;
    m_toolbar->setVisible(!empty);
    m_body->setCurrentIndex(empty ? 1 : 0);
    m_collection->setCurrentIndex(m_scanner->hasScanned() && total > 0 && shown == 0 ? 1 : 0);
}

void WorldsPage::syncRunning()
{
    QSet<QString> running;
    bool renamed = false;
    auto* list = APPLICATION->instances();
    QHash<QString, QString> shownNames;
    for (const auto& e : m_scanner->entries())
        shownNames.insert(e.instanceId, e.instanceName);
    for (int i = 0; i < list->count(); ++i) {
        MinecraftInstance* inst = list->at(i);
        if (inst->isRunning())
            running.insert(inst->id());
        const auto it = shownNames.constFind(inst->id());
        if (it != shownNames.constEnd() && it.value() != inst->name())
            renamed = true;
    }
    m_model->setRunning(running);
    {
        const int row = m_selectedKey.isEmpty() ? -1 : m_model->rowForKey(m_selectedKey);
        if (row >= 0)
            m_details->setInstanceRunning(running.contains(m_model->entry(row).instanceId));
    }
    if (renamed)
        scheduleRescan();
}

void WorldsPage::clearFilters()
{
    {
        const QSignalBlocker blocker(m_favoritesOnly);
        m_favoritesOnly->setChecked(false);
        APPLICATION->settings()->set("ZeroWorldsFavoritesOnly", false);
    }
    {
        const QSignalBlocker blocker(m_search);
        m_search->clear();
    }
    m_instanceFilter.clear();
    APPLICATION->settings()->set("ZeroWorldsInstance", QString());
    {
        const QSignalBlocker blocker(m_instanceCombo);
        m_instanceCombo->setCurrentIndex(0);
    }
    applyFilters();
    m_list->setFocus();
}

bool WorldsPage::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) {
            m_search->clear();
            m_list->setFocus();
            return true;
        }
    }
    return ShellPage::eventFilter(watched, event);
}

}  // namespace Zero
