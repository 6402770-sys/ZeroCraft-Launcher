// SPDX-License-Identifier: GPL-3.0-only
#include "ScreenshotsPage.h"

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
#include <QDebug>
#include <QTimer>
#include <QVBoxLayout>
#include "Application.h"
#include "InstanceList.h"
#include "ScreenshotRows.h"
#include "ScreenshotViewer.h"
#include "ThumbnailCache.h"
#include "minecraft/MinecraftInstance.h"
#include "settings/SettingsObject.h"
#include "ui/zero/ZeroEmptyState.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroReveal.h"
#include "ui/zero/ZeroSkeleton.h"

namespace Zero {

namespace {
QList<Gallery::InstanceSource> sourcesFromApplication()
{
    QList<Gallery::InstanceSource> out;
    auto* list = APPLICATION->instances();
    for (int i = 0; i < list->count(); ++i) {
        MinecraftInstance* inst = list->at(i);
        out.append({ inst->id(), inst->name(), inst->gameRoot() + QStringLiteral("/screenshots") });
    }
    return out;
}
}  // namespace

ScreenshotsPage::ScreenshotsPage(QWidget* parent)
    : ShellPage(parent)
    , m_scanner(new Gallery::ScreenshotScanner(&sourcesFromApplication, this))
    , m_cache(new ThumbnailCache(this))
    , m_model(new ScreenshotModel(this))
{
    // ----- toolbar row (content): search left, instance filter and sort right -----
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search screenshots"));
    m_search->setFixedWidth(280);
    m_search->setClearButtonEnabled(true);
    m_search->installEventFilter(this);

    m_instanceCombo = new QComboBox(this);
    m_instanceCombo->setToolTip(tr("Filter by instance"));
    m_instanceCombo->addItem(tr("All instances"), QString());

    m_sortCombo = new QComboBox(this);
    m_sortCombo->setToolTip(tr("Sort screenshots"));
    m_sortCombo->addItem(tr("Newest"), Gallery::sortModeToString(Gallery::SortMode::Newest));
    m_sortCombo->addItem(tr("Oldest"), Gallery::sortModeToString(Gallery::SortMode::Oldest));
    m_sortCombo->addItem(tr("Instance"), Gallery::sortModeToString(Gallery::SortMode::Instance));

    m_toolbar = new QWidget(this);
    auto* toolbarRow = new QHBoxLayout(m_toolbar);
    toolbarRow->setContentsMargins(0, 0, 0, Tokens::spaceLg);
    toolbarRow->setSpacing(Tokens::spaceLg);
    toolbarRow->addWidget(m_search);
    toolbarRow->addStretch(1);
    toolbarRow->addWidget(m_instanceCombo);
    toolbarRow->addWidget(m_sortCombo);

    // ----- top bar action: Refresh -----
    m_refresh = new QPushButton(tr("Refresh"), this);
    Style::setProperty(m_refresh, "zeroVariant", "ghost");
    m_refresh->setIconSize(QSize(16, 16));
    m_refresh->setToolTip(tr("Rescan screenshots"));
    connect(m_refresh, &QPushButton::clicked, this, &ScreenshotsPage::rescan);
    auto* actions = new QWidget(this);
    auto* actionsRow = new QHBoxLayout(actions);
    actionsRow->setContentsMargins(0, 0, 0, 0);
    actionsRow->setSpacing(Tokens::spaceSm);
    actionsRow->addWidget(m_refresh);
    setTopBarActions(actions);

    // ----- grid -----
    m_list = new QListView(this);
    m_list->setModel(m_model);
    m_delegate = new ScreenshotDelegate(m_cache, m_list);
    m_list->setItemDelegate(m_delegate);
    m_reveal = Motion::installItemReveal(m_list);
    m_list->viewport()->installEventFilter(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setWrapping(true);
    m_list->setFlow(QListView::LeftToRight);
    m_list->setSpacing(0);
    m_list->setGridSize(m_delegate->gridSize());
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
    connect(m_list, &QListView::activated, this, &ScreenshotsPage::openViewerAt);
    connect(m_list, &QListView::doubleClicked, this, &ScreenshotsPage::openViewerAt);
    // Thumbnails fade in when they arrive: repaint every frame while one is still fading.
    auto* fadeTimer = new QTimer(this);
    fadeTimer->setInterval(16);
    connect(fadeTimer, &QTimer::timeout, this, [this, fadeTimer]() {
        m_list->viewport()->update();
        if (!m_cache->fading())
            fadeTimer->stop();
    });
    connect(m_cache, &ThumbnailCache::thumbnailReady, this, [this, fadeTimer]() {
        m_list->viewport()->update();
        if (m_cache->fading() && !fadeTimer->isActive())
            fadeTimer->start();
    });

    m_noMatches = new ZeroEmptyState(QStringLiteral("image"), tr("No screenshots match"), tr("Try a different search or clear the filters."),
                                     tr("Clear filters"), this);
    connect(m_noMatches, &ZeroEmptyState::actionTriggered, this, &ScreenshotsPage::clearFilters);

    m_collection = new QStackedWidget(this);
    m_collection->addWidget(m_list);
    m_collection->addWidget(m_noMatches);

    auto* none = new ZeroEmptyState(QStringLiteral("image"), tr("No screenshots yet"),
                                    tr("Press F2 in game to take one. They show up here from every instance."), {}, this);
    auto* scanning = new ZeroSkeleton(ZeroSkeleton::Shape::Tiles, tr("Looking for screenshots in every instance…"), this);

    m_body = new QStackedWidget(this);
    m_body->addWidget(m_collection);
    m_body->addWidget(none);
    m_body->addWidget(scanning);

    auto* page = new QVBoxLayout(this);
    page->setContentsMargins(Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl);
    page->setSpacing(0);
    page->addWidget(m_toolbar);
    page->addWidget(m_body, 1);

    // ----- viewer overlay (a child covering the page) -----
    m_viewer = new ScreenshotViewer(m_cache, this);
    connect(m_viewer, &ScreenshotViewer::showInstanceRequested, this, [this](const QString& id) {
        m_viewer->closeViewer();
        emit showInstanceRequested(id);
    });
    connect(m_viewer, &ScreenshotViewer::fileTrashed, this, &ScreenshotsPage::rescan);
    connect(m_viewer, &ScreenshotViewer::closed, this, [this](const QString& lastPath) {
        const int row = lastPath.isEmpty() ? -1 : m_model->rowForPath(lastPath);
        if (row >= 0) {
            const QModelIndex index = m_model->index(row);
            m_list->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
            m_list->scrollTo(index);
        }
        m_list->setFocus(Qt::OtherFocusReason);
    });

    // ----- behavior -----
    connect(m_search, &QLineEdit::textChanged, this, [this]() { applyFilters(); });
    auto* findShortcut = new QShortcut(QKeySequence(QKeySequence::Find), this);
    connect(findShortcut, &QShortcut::activated, this, &ScreenshotsPage::focusSearch);

    m_instanceFilter = APPLICATION->settings()->get("ZeroScreenshotsInstance").toString();
    m_sort = Gallery::sortModeFromString(APPLICATION->settings()->get("ZeroScreenshotsSort").toString());
    m_sortCombo->setCurrentIndex(qMax(0, m_sortCombo->findData(Gallery::sortModeToString(m_sort))));
    connect(m_instanceCombo, &QComboBox::activated, this, [this](int i) {
        m_instanceFilter = m_instanceCombo->itemData(i).toString();
        APPLICATION->settings()->set("ZeroScreenshotsInstance", m_instanceFilter);
        applyFilters();
        m_reveal->start();
    });
    connect(m_sortCombo, &QComboBox::activated, this, [this](int i) {
        m_sort = Gallery::sortModeFromString(m_sortCombo->itemData(i).toString());
        APPLICATION->settings()->set("ZeroScreenshotsSort", Gallery::sortModeToString(m_sort));
        applyFilters();
        m_reveal->start();
    });

    connect(m_scanner, &Gallery::ScreenshotScanner::listChanged, this, &ScreenshotsPage::onListChanged);
    connect(m_scanner, &Gallery::ScreenshotScanner::scanningChanged, this, [this](bool scanning) { m_refresh->setEnabled(!scanning); });

    // instances added / removed / reset: rescan (debounced); renames too
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
    connect(instances, &QAbstractItemModel::rowsInserted, this, &ScreenshotsPage::scheduleRescan);
    connect(instances, &QAbstractItemModel::rowsRemoved, this, &ScreenshotsPage::scheduleRescan);
    connect(instances, &QAbstractItemModel::modelReset, this, &ScreenshotsPage::scheduleRescan);
    connect(instances, &QAbstractItemModel::dataChanged, this, &ScreenshotsPage::syncNames);

    connect(&Theme::instance(), &Theme::changed, this, &ScreenshotsPage::refreshIcons);
    refreshIcons();
    updateState();
}

void ScreenshotsPage::refreshIcons()
{
    const auto& t = Theme::current();
    m_refresh->setIcon(Icons::icon(QStringLiteral("refresh-cw"), t.textSecondary));
    const QIcon searchIcon = Icons::icon(QStringLiteral("search"), t.textTertiary);
    if (m_searchAction)
        m_searchAction->setIcon(searchIcon);
    else
        m_searchAction = m_search->addAction(searchIcon, QLineEdit::LeadingPosition);
    m_list->viewport()->update();
}

void ScreenshotsPage::focusSearch()
{
    m_search->setFocus(Qt::ShortcutFocusReason);
    m_search->selectAll();
}

void ScreenshotsPage::setSearchText(const QString& text)
{
    m_search->setText(text);
}

void ScreenshotsPage::rescan()
{
    qDebug() << "ZERO screenshots: rescan";
    m_scanRequested = true;
    m_stale = false;
    m_sinceScan.start();
    m_scanner->rescan();
}

void ScreenshotsPage::scheduleRescan()
{
    if (m_scanRequested)  // never scanned (page not opened yet): the first show scans anyway
        m_rescanTimer->start();
}

void ScreenshotsPage::syncNames()
{
    QHash<QString, QString> shown;
    for (const auto& e : m_scanner->entries())
        shown.insert(e.instanceId, e.instanceName);
    auto* list = APPLICATION->instances();
    for (int i = 0; i < list->count(); ++i) {
        MinecraftInstance* inst = list->at(i);
        const auto it = shown.constFind(inst->id());
        if (it != shown.constEnd() && it.value() != inst->name()) {
            scheduleRescan();
            return;
        }
    }
}

bool ScreenshotsPage::visibleThumbnailsReady() const
{
    const QRect view = m_list->viewport()->rect();
    for (int row = 0; row < m_model->rowCount(); ++row) {
        const QModelIndex index = m_model->index(row);
        if (!m_list->visualRect(index).intersects(view))
            continue;
        const auto& e = m_model->entry(row);
        if (!m_cache->isReady(e.path, e.modifiedMs))
            return false;
    }
    return true;
}

void ScreenshotsPage::callWhenIdle(const std::function<void()>& done, int maxMs)
{
    auto* timer = new QTimer(this);
    const QDeadlineTimer deadline(maxMs);
    connect(timer, &QTimer::timeout, this, [this, timer, deadline, done]() {
        if ((m_scanner->hasScanned() && !m_scanner->isScanning() && visibleThumbnailsReady()) || deadline.hasExpired()) {
            timer->deleteLater();
            done();
        }
    });
    timer->start(50);
}

void ScreenshotsPage::callWhenViewerReady(const std::function<void()>& done, int maxMs)
{
    auto* timer = new QTimer(this);
    const QDeadlineTimer deadline(maxMs);
    connect(timer, &QTimer::timeout, this, [this, timer, deadline, done]() {
        if (m_viewer->fullImageLoaded() || deadline.hasExpired()) {
            timer->deleteLater();
            done();
        }
    });
    timer->start(50);
}

bool ScreenshotsPage::openViewerByFileName(const QString& fileName)
{
    for (const auto& e : m_model->entries()) {
        if (e.fileName == fileName) {
            m_viewer->setEntries(m_model->entries());
            m_viewer->openPath(e.path);
            return true;
        }
    }
    return false;
}

void ScreenshotsPage::openViewerAt(const QModelIndex& index)
{
    if (!index.isValid() || m_viewer->isOpen())
        return;
    m_viewer->setEntries(m_model->entries());
    m_viewer->openPath(index.data(ScreenshotModel::PathRole).toString());
}

void ScreenshotsPage::showEvent(QShowEvent* event)
{
    ShellPage::showEvent(event);
    if (!m_scanRequested || m_stale || m_sinceScan.hasExpired(kStaleMs))
        rescan();
}

void ScreenshotsPage::onListChanged()
{
    rebuildInstanceCombo();
    applyFilters();
}

void ScreenshotsPage::rebuildInstanceCombo()
{
    QHash<QString, QString> names;  // id -> name, only instances that have screenshots
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

QString ScreenshotsPage::effectiveInstanceFilter() const
{
    return !m_instanceFilter.isEmpty() && m_instanceCombo->findData(m_instanceFilter) >= 0 ? m_instanceFilter : QString();
}

void ScreenshotsPage::applyFilters()
{
    const QString instance = effectiveInstanceFilter();
    const QString text = m_search->text();
    QList<Gallery::ScreenshotEntry> rows;
    for (const auto& e : m_scanner->entries()) {
        if (!instance.isEmpty() && e.instanceId != instance)
            continue;
        if (!Gallery::matchesSearch(e, text))
            continue;
        rows.append(e);
    }
    Gallery::sortScreenshots(rows, m_sort);

    // A rescan that found the same files (instance list signals, coming back to the page) must not reset the view: every reset
    // made the whole grid blink.
    if (rows == m_model->entries()) {
        updateState();
        return;
    }

    const int scroll = m_list->verticalScrollBar()->value();
    const QString current = m_list->currentIndex().isValid() ? m_list->currentIndex().data(ScreenshotModel::PathRole).toString() : QString();
    const bool firstFill = m_model->rowCount() == 0;
    m_model->setEntries(rows);
    // Lay the tiles out now: QListView's delayed layout paints one empty frame after a reset, and the scroll position below
    // would be clamped to the empty range.
    m_list->doItemsLayout();
    if (firstFill && !rows.isEmpty())
        m_reveal->start();
    const int row = current.isEmpty() ? -1 : m_model->rowForPath(current);
    if (row >= 0)
        m_list->selectionModel()->setCurrentIndex(m_model->index(row), QItemSelectionModel::ClearAndSelect);
    m_list->verticalScrollBar()->setValue(scroll);

    m_viewer->setEntries(rows);  // the viewer browses the current filtered order
    updateState();
}

void ScreenshotsPage::updateState()
{
    const int total = static_cast<int>(m_scanner->entries().size());
    const int shown = m_model->rowCount();
    const bool filtering = !m_search->text().trimmed().isEmpty() || !effectiveInstanceFilter().isEmpty();

    QString subtitle;
    if (!m_scanner->hasScanned()) {
        subtitle = tr("Scanning…");
    } else if (total == 0) {
        subtitle = tr("No screenshots");
    } else if (filtering) {
        subtitle = total == 1 ? tr("%1 of 1 screenshot").arg(shown) : tr("%1 of %2 screenshots").arg(shown).arg(total);
    } else {
        QSet<QString> instances;
        for (const auto& e : m_scanner->entries())
            instances.insert(e.instanceId);
        const int m = static_cast<int>(instances.size());
        subtitle = tr("%1 in %2").arg(total == 1 ? tr("1 screenshot") : tr("%1 screenshots").arg(total),
                                      m == 1 ? tr("1 instance") : tr("%1 instances").arg(m));
    }
    if (subtitle != m_subtitle) {
        m_subtitle = subtitle;
        emit chromeChanged();
    }

    const bool scanned = m_scanner->hasScanned();
    const bool empty = scanned && total == 0;
    m_toolbar->setVisible(scanned && !empty);
    m_body->setCurrentIndex(!scanned ? 2 : (empty ? 1 : 0));
    m_collection->setCurrentIndex(scanned && total > 0 && shown == 0 ? 1 : 0);
}

void ScreenshotsPage::clearFilters()
{
    {
        const QSignalBlocker blocker(m_search);
        m_search->clear();
    }
    m_instanceFilter.clear();
    APPLICATION->settings()->set("ZeroScreenshotsInstance", QString());
    {
        const QSignalBlocker blocker(m_instanceCombo);
        m_instanceCombo->setCurrentIndex(0);
    }
    applyFilters();
    m_list->setFocus();
}

bool ScreenshotsPage::eventFilter(QObject* watched, QEvent* event)
{
    if (m_list && m_delegate && watched == m_list->viewport() && event->type() == QEvent::Resize) {
        if (m_delegate->setViewportWidth(m_list->viewport()->width()))
            m_list->setGridSize(m_delegate->gridSize());  // fluid grid: columns and tile size follow the width
        return false;
    }
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
