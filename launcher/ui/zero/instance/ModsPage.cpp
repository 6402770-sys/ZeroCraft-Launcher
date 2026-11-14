// SPDX-License-Identifier: GPL-3.0-only
#include "ModsPage.h"

#include <QAction>
#include <QApplication>
#include <QCollator>
#include <QDateTime>
#include <QDialog>
#include <QEventLoop>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QStackedWidget>
#include <QThreadPool>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

#include "Application.h"
#include "DesktopServices.h"
#include "FileSystem.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/mod/ModFolderModel.h"
#include "ui/MainWindow.h"
#include "ui/pages/instance/ModFolderPage.h"
#include "ui/zero/ZeroEmptyState.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroSkeleton.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/content/ContentDraw.h"
#include "ui/zero/content/FilterPanel.h"
#include "ui/zero/instance/InstancePageParts.h"
#include "ui/zero/instance/ModSnapshotNotice.h"
#include "zero/mods/ModHealth.h"

namespace Zero {
namespace ModsUi {

using namespace Mods::Installed;

namespace {

QString nameOf(const Mod& mod)
{
    return rowFromMod(mod).name;
}

ModsPageEnvironment environmentFor(MinecraftInstance* inst)
{
    QPointer<MinecraftInstance> guarded(inst);
    ModsPageEnvironment env;
    env.target = [guarded]() { return guarded ? Content::targetFromInstance(guarded) : Content::TargetInstance(); };
    env.isRunning = [guarded]() { return guarded && guarded->isRunning(); };
    const QString id = inst->id();
    env.addMods = [id]() {
        if (auto* window = APPLICATION->showMainWindow()) {
            window->showDiscoverMods(id);
        }
    };
    env.findMod = [id](const QString& query) {
        if (auto* window = APPLICATION->showMainWindow()) {
            window->showDiscoverMods(id, query);
        }
    };
    env.context = [guarded]() { return guarded ? Mods::readInstanceContext(guarded->instanceRoot()) : Mods::InstanceContext(); };
    env.checkUpdates = prismUpdateCheckFactory(inst, inst->loaderModList());
    env.applyUpdates = applyUpdatesWithPrism;
    env.snapshotsDir = [id]() { return Mods::Snapshot::snapshotsDirFor(APPLICATION->dataRoot(), id); };
    env.curseForgeAvailable = []() { return APPLICATION->capabilities().testFlag(Application::SupportsFlame); };
    env.openClassic = [guarded](QWidget* parent) {
        if (!guarded) {
            return;
        }
        // Prism's own page in a plain dialog. The page is never "opened": the model is already watched by this page, and the classic
        // page's closedImpl() would stop that.
        auto* dialog = new QDialog(parent ? parent->window() : nullptr);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(QObject::tr("Mods (classic)"));
        auto* layout = new QVBoxLayout(dialog);
        layout->setContentsMargins(Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg);
        auto* page = new ModFolderPage(guarded, guarded->loaderModList(), dialog);
        page->setFilter(QStringLiteral("%1 (*.zip *.jar *.litemod *.nilmod)"));
        layout->addWidget(page);
        dialog->resize(960, 600);
        dialog->open();
    };
    return env;
}

}  // namespace

QWidget* ModsPage::banner() const
{
    return m_banner;
}

ModsPage::ModsPage(MinecraftInstance* inst, ModFolderModel* model, QWidget* parent) : ModsPage(environmentFor(inst), model, parent)
{
    connect(inst, &BaseInstance::runningStatusChanged, this, [this]() { refreshRunning(); });
}

ModsPage::ModsPage(const ModsPageEnvironment& env, ModFolderModel* model, QWidget* parent) : QWidget(parent), m_env(env), m_model(model)
{
    setObjectName(QStringLiteral("zeroModsPage"));
    if (!m_env.trash) {
        m_env.trash = [](const QString& path) { return FS::trash(path); };
    }
    if (!m_env.ask) {
        m_env.ask = askWithDialog;
    }
    if (!m_env.target) {
        m_env.target = []() { return Content::TargetInstance(); };
    }
    if (!m_env.curseForgeAvailable) {
        m_env.curseForgeAvailable = []() { return true; };
    }
    build();
    connectModel();
    m_snapshotTimer.setInterval(60 * 1000);
    connect(&m_snapshotTimer, &QTimer::timeout, this, &ModsPage::updateSnapshotNotice);
    m_snapshotTimer.start();
}

ModsPage::~ModsPage()
{
    stopCheck(false);
    if (m_model && m_startedWatching) {
        m_model->stopWatching();
    }
}

void ModsPage::build()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(Tokens::spaceMd);

    m_banner = new RunningBanner(tr("Close the game to change mods"), this);
    m_banner->hide();
    root->addWidget(m_banner);

    // ---- toolbar --------------------------------------------------------------------------------------------------------------
    m_toolbar = new QWidget(this);
    m_toolbar->setObjectName(QStringLiteral("zeroModsToolbar"));
    m_toolbar->setFixedHeight(Tokens::controlHeightLg);
    auto* row = new QHBoxLayout(m_toolbar);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(Tokens::spaceSm);

    m_search = makeSearchField(m_toolbar, tr("Search mods"));
    m_search->installEventFilter(this);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_query = text.trimmed();
        applyView();
    });
    row->addWidget(m_search, 1);

    m_filterControl = new Content::SegmentedControl({}, m_toolbar);
    m_filterControl->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_hasUpdatesSegment = true;  // so that the first refresh lays out the four segments
    refreshFilterOptions();
    m_filterControl->setSelected(filterId(Filter::All));
    m_filterControl->setAccessibleName(tr("Filter mods"));
    m_filterControl->onActivated = [this](const QString& value) { setFilter(filterFromId(value)); };
    row->addWidget(m_filterControl, 0, Qt::AlignVCenter);

    row->addStretch(1);

    // Second row (under the toolbar, so the toolbar fits a 1100 px window): the caption and the selection bar share its left
    // slot (the bar replaces the caption while 2 or more mods are selected); the info line about mods a check could not look at
    // sits at its right.
    m_statusRow = new QWidget(this);
    m_statusRow->setObjectName(QStringLiteral("zeroModsStatus"));
    m_statusRow->setFixedHeight(Tokens::controlHeight);
    auto* status = new QHBoxLayout(m_statusRow);
    status->setContentsMargins(0, 0, 0, 0);
    status->setSpacing(Tokens::spaceMd);
    m_captionStack = new QStackedWidget(m_statusRow);
    m_caption = new QLabel(m_captionStack);
    Style::applyRole(m_caption, Role::Body);
    Style::setProperty(m_caption, "zeroTone", "secondary");
    m_caption->setAccessibleName(tr("Mods"));
    m_captionStack->addWidget(m_caption);
    m_selectionBar = new QWidget(m_captionStack);
    auto* bar = new QHBoxLayout(m_selectionBar);
    bar->setContentsMargins(0, 0, 0, 0);
    bar->setSpacing(Tokens::spaceXs);
    m_selectionLabel = new QLabel(m_selectionBar);
    Style::applyRole(m_selectionLabel, Role::Label);
    bar->addWidget(m_selectionLabel);
    bar->addSpacing(Tokens::spaceXs);
    m_selectionEnable = new QPushButton(tr("Enable"), m_selectionBar);
    m_selectionDisable = new QPushButton(tr("Disable"), m_selectionBar);
    m_selectionUpdate = new QPushButton(tr("Update"), m_selectionBar);
    m_selectionRemove = new QPushButton(tr("Remove"), m_selectionBar);
    Style::setProperty(m_selectionEnable, "zeroVariant", "ghost");
    Style::setProperty(m_selectionDisable, "zeroVariant", "ghost");
    Style::setProperty(m_selectionUpdate, "zeroVariant", "ghost");
    Style::setProperty(m_selectionRemove, "zeroVariant", "danger");
    m_selectionUpdate->hide();
    for (auto* button : { m_selectionEnable, m_selectionDisable, m_selectionUpdate, m_selectionRemove }) {
        bar->addWidget(button);
    }
    m_captionStack->addWidget(m_selectionBar);
    status->addWidget(m_captionStack, 0, Qt::AlignVCenter);
    status->addStretch(1);

    m_checkButton = new QPushButton(checkButtonText(), m_toolbar);
    m_checkButton->setObjectName(QStringLiteral("zeroModsCheck"));
    Style::setProperty(m_checkButton, "zeroVariant", "secondary");
    Style::setProperty(m_checkButton, "zeroSize", "lg");
    m_checkButton->setFixedHeight(Tokens::controlHeightLg);
    m_checkButton->setIconSize(QSize(16, 16));
    m_checkButton->setAccessibleName(checkButtonText());
    m_checkButton->hide();
    connect(m_checkButton, &QPushButton::clicked, this, [this]() {
        if (m_checking) {
            return;
        }
        if (updateCount() > 0) {
            requestUpdateAll();
        } else {
            startCheck();
        }
    });
    row->addWidget(m_checkButton);

    m_addButton = makeAddButton(tr("Add mods"), m_toolbar);
    connect(m_addButton, &QPushButton::clicked, this, [this]() {
        if (m_env.addMods) {
            m_env.addMods();
        }
        emit addModsRequested();
    });
    row->addWidget(m_addButton);

    m_overflow = makeOverflowButton(m_toolbar, QStringLiteral("zeroModsOverflow"));
    auto* menu = new QMenu(m_overflow);
    auto* openFolder = menu->addAction(tr("Open mods folder"));
    connect(openFolder, &QAction::triggered, this, &ModsPage::openMenuFolder);
    if (m_env.openClassic) {
        auto* classic = menu->addAction(tr("Open the classic mods page"));
        connect(classic, &QAction::triggered, this, [this]() { m_env.openClassic(this); });
    }
    m_checkAgain = new QAction(checkAgainText(), this);  // joins the menu while there are updates (see refreshUpdateChrome)
    connect(m_checkAgain, &QAction::triggered, this, &ModsPage::startCheck);
    m_overflow->setMenu(menu);
    row->addWidget(m_overflow);
    root->addWidget(m_toolbar);

    // After a check: the mods it could not look at.
    m_infoLine = new QWidget(m_statusRow);
    m_infoLine->setObjectName(QStringLiteral("zeroModsInfo"));
    auto* info = new QHBoxLayout(m_infoLine);
    info->setContentsMargins(0, 0, 0, 0);
    info->setSpacing(Tokens::spaceSm);
    m_infoIcon = new QLabel(m_infoLine);
    m_infoIcon->setFixedSize(14, 14);
    info->addWidget(m_infoIcon);
    m_infoText = new QLabel(m_infoLine);
    Style::applyRole(m_infoText, Role::Caption);
    Style::setProperty(m_infoText, "zeroTone", "secondary");
    info->addWidget(m_infoText, 1);
    m_infoLine->hide();
    status->addWidget(m_infoLine, 0, Qt::AlignVCenter);
    root->addWidget(m_statusRow);

    // The newest update that can be rolled back (hidden until refreshSnapshot() finds one).
    m_snapshotNotice = new ModSnapshotNotice(this);
    connect(m_snapshotNotice, &ModSnapshotNotice::rollbackRequested, this, [this]() { requestRollback(); });
    connect(m_snapshotNotice, &ModSnapshotNotice::dismissRequested, this, &ModsPage::dismissSnapshot);
    root->addWidget(m_snapshotNotice);

    // ---- body -----------------------------------------------------------------------------------------------------------------
    m_stack = new QStackedWidget(this);
    m_skeleton = new ZeroSkeleton(ZeroSkeleton::Shape::Rows, tr("Reading the mods folder…"), m_stack);
    m_stack->addWidget(m_skeleton);

    m_list = new ModListView(m_stack);
    m_listModel = new ModListModel(m_list);
    m_list->setModel(m_listModel);
    m_list->setAccessibleName(tr("Installed mods"));
    const ModDelegate::IconProvider iconProvider = [this](const QString& id, int px, qreal dpr) -> QPixmap {
        if (!m_model) {
            return {};
        }
        const Resource::Ptr resource = m_model->find(id);
        if (!resource) {
            return {};
        }
        QPixmap pixmap = std::static_pointer_cast<Mod>(resource)->icon(QSize(qRound(px * dpr), qRound(px * dpr)), Qt::KeepAspectRatio);
        pixmap.setDevicePixelRatio(dpr);
        return pixmap;
    };
    m_list->rowDelegate()->setIconProvider(iconProvider);
    m_stack->addWidget(m_list);

    m_empty = new ZeroEmptyState(QStringLiteral("blocks"), tr("No mods yet"), QString(), QString(), m_stack);
    m_stack->addWidget(m_empty);
    // The list and, when a mod is opened, the Dependency Inspector panel at its right.
    m_body = new QWidget(this);
    auto* bodyLayout = new QHBoxLayout(m_body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(Tokens::spaceMd);
    bodyLayout->addWidget(m_stack, 1);
    m_panel = new ModDependencyPanel(m_body);
    m_panel->setIconProvider(iconProvider);
    m_panel->hide();
    bodyLayout->addWidget(m_panel);
    root->addWidget(m_body, 1);

    // ---- wiring ---------------------------------------------------------------------------------------------------------------
    connect(m_list, &ModListView::switchClicked, this, &ModsPage::onSwitchClicked);
    connect(m_list, &ModListView::menuRequested, this, &ModsPage::onRowMenu);
    connect(m_list, &ModListView::toggleKeyPressed, this, &ModsPage::onToggleKey);
    connect(m_list, &ModListView::removeKeyPressed, this, [this]() { requestRemove(selectedRows()); });
    connect(m_list, &ModListView::rowClicked, this, &ModsPage::onRowClicked);
    connect(m_list, &ModListView::detailsKeyPressed, this, &ModsPage::onDetailsKey);
    connect(m_list, &ModListView::escapePressed, this, &ModsPage::closePanel);
    connect(m_panel, &ModDependencyPanel::closeRequested, this, [this]() {
        closePanel();
        m_list->setFocus(Qt::OtherFocusReason);
    });
    connect(m_panel, &ModDependencyPanel::neededByClicked, this, &ModsPage::showNeededBy);
    connect(m_panel, &ModDependencyPanel::enableRequested, this, &ModsPage::enableFromPanel);
    connect(m_panel, &ModDependencyPanel::findRequested, this, [this](const QString& id) {
        if (m_env.findMod) {
            m_env.findMod(id);
        }
    });

    m_scanner = new Mods::DependencyScanner(m_env.scanRunner, this);
    connect(m_scanner, &Mods::DependencyScanner::finished, this, &ModsPage::onScanFinished);
    m_scanTimer.setSingleShot(true);
    m_scanTimer.setInterval(std::max(0, m_env.scanDelayMs));
    connect(&m_scanTimer, &QTimer::timeout, this, &ModsPage::startScan);
    connect(m_list->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() {
        if (!m_building) {
            updateChrome();
            onSelectionChanged();
        }
    });
    connect(m_selectionEnable, &QPushButton::clicked, this, [this]() { requestToggle(selectedRows(), ToggleAction::Enable); });
    connect(m_selectionDisable, &QPushButton::clicked, this, [this]() { requestToggle(selectedRows(), ToggleAction::Disable); });
    connect(m_selectionRemove, &QPushButton::clicked, this, [this]() { requestRemove(selectedRows()); });
    connect(m_selectionUpdate, &QPushButton::clicked, this, [this]() { requestUpdate(selectedRows()); });
    connect(m_empty, &ZeroEmptyState::actionTriggered, this, &ModsPage::onEmptyAction);

    auto* find = new QShortcut(QKeySequence::Find, this);
    find->setContext(Qt::WidgetWithChildrenShortcut);
    connect(find, &QShortcut::activated, this, [this]() {
        m_search->setFocus(Qt::ShortcutFocusReason);
        m_search->selectAll();
    });

    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.setInterval(30);
    connect(&m_rebuildTimer, &QTimer::timeout, this, &ModsPage::rebuild);

    const auto refreshIcons = [this]() {
        const auto& t = Theme::current();
        m_addButton->setIcon(Icons::icon(QStringLiteral("plus"), t.onAccent));
        m_overflow->setIcon(Icons::icon(QStringLiteral("ellipsis"), t.text));
        m_infoIcon->setPixmap(Icons::icon(QStringLiteral("info"), t.textSecondary).pixmap(QSize(14, 14)));

        if (!m_search->actions().isEmpty()) {
            m_search->actions().first()->setIcon(Icons::icon(QStringLiteral("search"), t.textTertiary));
        }
    };
    m_search->addAction(Icons::icon(QStringLiteral("search"), Theme::current().textTertiary), QLineEdit::LeadingPosition);
    connect(&Theme::instance(), &Theme::changed, this, refreshIcons);
    connect(&Theme::instance(), &Theme::changed, this, [this]() { updateChrome(); });  // the check button's icon follows the theme
    refreshIcons();

    setView(View::Loading);
    updateChrome();
}

void ModsPage::connectModel()
{
    if (!m_model) {
        return;
    }
    ModFolderModel* model = m_model;
    const auto schedule = [this]() { scheduleRebuild(); };
    // Abort a running check BEFORE anything it points at can go away. The model frees a resource when it removes its row, on a reset and
    // (by swapping the object) when a changed file is read again; the last one is only visible afterwards, as a dataChanged in which a
    // checked Resource is no longer in the model (the old object is still alive then, and the check keeps it alive anyway).
    connect(model, &QAbstractItemModel::rowsAboutToBeRemoved, this, [this]() { stopCheck(true); });
    connect(model, &QAbstractItemModel::modelAboutToBeReset, this, [this]() { stopCheck(true); });
    connect(model, &QAbstractItemModel::layoutAboutToBeChanged, this, [this]() { stopCheck(true); });
    connect(model, &QAbstractItemModel::dataChanged, this, [this]() {
        if (m_checking && !checkedModsStillThere()) {
            stopCheck(true);
        }
    });
    connect(model, &QAbstractItemModel::dataChanged, this, schedule);
    connect(model, &QAbstractItemModel::rowsInserted, this, schedule);
    connect(model, &QAbstractItemModel::rowsRemoved, this, [this]() {
        m_mapsStale = true;
        scheduleRebuild();
    });
    connect(model, &QAbstractItemModel::modelReset, this, [this]() {
        m_mapsStale = true;
        scheduleRebuild();
    });
    connect(model, &ResourceFolderModel::updateFinished, this, [this]() {
        m_loaded = true;
        // Prism rebuilds its requires / required-by maps only when a parse task finishes. After a removal they may still point at
        // the removed mods: ask for the rebuild (the model's own slot runs first, ours clears the flag).
        if (m_model && m_mapsStale && !m_model->hasPendingParseTasks()) {
            emit m_model->parseFinished();
        }
        m_rebuildTimer.stop();
        rebuild();
        m_scanTimer.start();  // the Dependency Inspector reads the folder again once it has been quiet for a moment
    });
    connect(model, &ResourceFolderModel::parseFinished, this, [this]() {
        if (m_model && !m_model->hasPendingParseTasks()) {
            m_mapsStale = false;
        }
        scheduleRebuild();
    });
}

void ModsPage::openedImpl()
{
    if (!m_model) {
        return;
    }
    // startWatching() loads the folder and returns true only when it was not watched yet: then it is ours to stop in closedImpl().
    m_startedWatching = m_model->startWatching();
    if (!m_startedWatching) {
        m_model->update();
    }
    if (!m_loaded && !m_model->empty()) {
        m_loaded = true;
    }
    refreshRunning();
    rebuild();
    refreshSnapshot();
}

void ModsPage::closedImpl()
{
    stopCheck(true);
    m_rebuildTimer.stop();
    m_scanTimer.stop();
    m_scanner->cancel();  // a scan that is still running answers nobody
    if (m_model && m_startedWatching) {
        m_model->stopWatching();
    }
    m_startedWatching = false;
}

void ModsPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refreshRunning();
    if (m_dirty) {
        rebuild();
    }
}

void ModsPage::refreshRunning()
{
    const bool running = m_env.isRunning && m_env.isRunning();
    if (running == m_locked && m_banner->isVisibleTo(this) == running) {
        return;
    }
    m_locked = running;
    if (running) {
        stopCheck(false);  // the game started: no check, no update (the running lock wins)
    }
    syncLock();
    m_list->viewport()->update();
    updateChrome();
}

void ModsPage::syncLock()
{
    QString reason;  // "" = the delegate's own text ("Close the game to change mods")
    if (!m_locked && m_checking) {
        reason = tr("Wait for the update check to finish");
    } else if (!m_locked && m_applying) {
        reason = tr("Wait for the update to finish");
    }
    m_list->rowDelegate()->setLocked(changesBlocked(), reason);
    m_list->viewport()->update();
    if (m_panel && isPanelOpen()) {
        refreshPanel();  // Enable follows the lock
    }
    updateSnapshotNotice();  // Roll back follows the lock
}

void ModsPage::scheduleRebuild()
{
    if (!isOpened && !isVisible()) {
        m_dirty = true;
        return;
    }
    if (!m_rebuildTimer.isActive()) {
        m_rebuildTimer.start();
    }
}

void ModsPage::rebuildNow()
{
    m_rebuildTimer.stop();
    rebuild();
}

void ModsPage::rebuild()
{
    m_dirty = false;
    if (!m_model) {
        return;
    }
    m_rows.clear();
    m_tokens.clear();
    const QList<Mod*> mods = m_model->allMods();
    m_rows.reserve(mods.size());
    for (Mod* mod : mods) {
        m_rows.append(rowFromMod(*mod));
        m_tokens.append(reinterpret_cast<quintptr>(mod));
    }
    // What a check found stays only while the file it was about is the file that is there now.
    if (!m_checking && !m_results.entries.isEmpty() && pruneResults(m_results, m_rows)) {
        for (auto it = m_updates.begin(); it != m_updates.end();) {
            const bool alive = std::any_of(m_results.entries.begin(), m_results.entries.end(),
                                           [&it](const CheckEntry& e) { return e.key == it.key() && e.hasUpdate(); });
            it = alive ? std::next(it) : m_updates.erase(it);
        }
    }
    overlayResults(m_rows, m_results);
    overlayDependencies(m_rows, m_inspection);
    applyView();
    refreshPanel();
}

void ModsPage::applyView()
{
    // The Updates segment exists only while there is something to update.
    refreshFilterOptions();
    if (m_filter == Filter::Updates && updateCount() == 0) {
        m_filter = Filter::All;
        m_filterControl->setSelected(filterId(Filter::All));
    }
    // Remember what is selected: a reset of the list (another filter, a mod added) clears the selection model.
    QList<quintptr> keepTokens;
    QStringList keepIds;
    quintptr keepCurrent = 0;
    for (const int row : selectedRows()) {
        keepTokens.append(m_listModel->tokenAt(row));
        keepIds.append(m_listModel->rowAt(row).id);
    }
    if (const QModelIndex current = m_list->currentIndex(); current.isValid()) {
        keepCurrent = m_listModel->tokenAt(current.row());
    }

    const QList<int> visible = visibleRows(m_rows, m_filter, m_query);
    QList<Row> rows;
    QList<quintptr> tokens;
    rows.reserve(visible.size());
    for (const int i : visible) {
        rows.append(m_rows.at(i));
        tokens.append(m_tokens.at(i));
    }
    const int scroll = m_list->verticalScrollBar()->value();
    m_building = true;
    const bool reset = m_listModel->setRows(rows, tokens, visible);
    if (reset) {
        QItemSelection selection;
        for (int i = 0; i < rows.size(); ++i) {
            if (keepTokens.contains(tokens.at(i)) || keepIds.contains(rows.at(i).id)) {
                selection.select(m_listModel->index(i), m_listModel->index(i));
            }
        }
        m_list->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
        if (keepCurrent != 0) {
            const int row = m_listModel->rowOfToken(keepCurrent);
            if (row >= 0) {
                m_list->selectionModel()->setCurrentIndex(m_listModel->index(row), QItemSelectionModel::NoUpdate);
            }
        }
        m_list->verticalScrollBar()->setValue(scroll);
    }
    m_building = false;
    updateChrome();
}

void ModsPage::updateChrome()
{
    const Counts counts = countOf(m_rows);
    const bool narrowed = m_filter != Filter::All || !m_query.isEmpty();
    const Content::TargetInstance target = m_env.target();
    const bool noLoader = target.loaders == ModPlatform::ModLoaderTypes();
    const bool canAdd = target.modReady() && bool(m_env.addMods);

    View next = View::List;
    if (!m_loaded) {
        next = View::Loading;
    } else if (m_rows.isEmpty()) {
        next = noLoader ? View::NoLoader : View::Empty;
    } else if (m_listModel->rowCount() == 0) {
        next = View::NoMatches;
    }

    switch (next) {
        case View::Empty:
            m_empty->setIcon(QStringLiteral("blocks"));
            m_empty->setTitle(tr("No mods yet"));
            m_empty->setBody(target.name.isEmpty() ? tr("Find mods in Discover.") : tr("Find mods for %1 in Discover.").arg(target.name));
            m_empty->setActionText(canAdd ? tr("Add mods") : QString());
            break;
        case View::NoLoader:
            m_empty->setIcon(QStringLiteral("blocks"));
            m_empty->setTitle(tr("This instance has no mod loader"));
            m_empty->setBody(tr("Install Fabric, Forge, NeoForge or Quilt on the Version page first. Mods then show up here."));
            m_empty->setActionText(QString());
            break;
        case View::NoMatches:
            m_empty->setIcon(QStringLiteral("search"));
            m_empty->setTitle(tr("No mods match"));
            m_empty->setBody(tr("Try another search or filter."));
            m_empty->setActionText(tr("Clear filters"));
            break;
        case View::Loading:
        case View::List:
            break;
    }
    setView(next);

    const bool hasMods = next == View::List || next == View::NoMatches;
    m_search->setVisible(hasMods);
    m_filterControl->setVisible(hasMods);
    m_statusRow->setVisible(hasMods);
    m_addButton->setVisible(hasMods && canAdd);
    m_caption->setText(m_checking ? checkingCaption(m_checkedCount) : captionText(counts, m_listModel->rowCount(), narrowed));

    const QList<int> selected = selectedRows();
    m_captionStack->setCurrentIndex(selected.size() >= 2 ? 1 : 0);
    if (selected.size() >= 2) {
        bool anyOff = false;
        bool anyOn = false;
        bool anyUpdate = false;
        for (const int row : selected) {
            (m_listModel->rowAt(row).enabled ? anyOn : anyOff) = true;
            anyUpdate = anyUpdate || m_listModel->rowAt(row).hasUpdate();
        }
        m_selectionLabel->setText(selectionText(int(selected.size())));
        m_selectionEnable->setEnabled(anyOff && !changesBlocked());
        m_selectionDisable->setEnabled(anyOn && !changesBlocked());
        m_selectionUpdate->setVisible(anyUpdate && bool(m_env.applyUpdates));
        m_selectionUpdate->setEnabled(anyUpdate && !changesBlocked());
        m_selectionRemove->setEnabled(!changesBlocked());
    }
    refreshUpdateChrome(hasMods);
    m_list->rowDelegate()->setMinecraftVersion(target.mcVersion);
    m_list->viewport()->update();

    m_banner->setVisible(m_locked);
}

void ModsPage::setView(View view)
{
    m_view = view;
    switch (view) {
        case View::Loading:
            m_stack->setCurrentWidget(m_skeleton);
            break;
        case View::List:
            m_stack->setCurrentWidget(m_list);
            break;
        case View::Empty:
        case View::NoLoader:
        case View::NoMatches:
            m_stack->setCurrentWidget(m_empty);
            break;
    }
}

void ModsPage::setFilter(Filter filter)
{
    if (filter == Filter::Updates && updateCount() == 0) {
        filter = Filter::All;  // there is no such segment
    }
    m_filterControl->setSelected(filterId(filter));
    if (filter == m_filter) {
        return;
    }
    m_filter = filter;
    applyView();
}

QString ModsPage::searchText() const
{
    return m_search->text().trimmed();
}

void ModsPage::setSearchText(const QString& text)
{
    m_search->setText(text);  // textChanged applies it
}

QList<int> ModsPage::selectedRows() const
{
    QList<int> rows;
    for (const QModelIndex& index : m_list->selectionModel()->selectedRows()) {
        rows.append(index.row());
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

void ModsPage::selectRows(const QList<int>& rows)
{
    QItemSelection selection;
    for (const int row : rows) {
        if (row >= 0 && row < m_listModel->rowCount()) {
            selection.select(m_listModel->index(row), m_listModel->index(row));
        }
    }
    m_list->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
    if (!rows.isEmpty() && rows.first() >= 0 && rows.first() < m_listModel->rowCount()) {
        m_list->selectionModel()->setCurrentIndex(m_listModel->index(rows.first()), QItemSelectionModel::NoUpdate);
    }
}

bool ModsPage::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape && !m_search->text().isEmpty()) {
            m_search->clear();
            return true;
        }
        if (key->key() == Qt::Key_Down && m_listModel->rowCount() > 0) {
            m_list->setFocus(Qt::TabFocusReason);
            if (!m_list->currentIndex().isValid()) {
                m_list->setCurrentIndex(m_listModel->index(0));
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

// ---- picking mods ----------------------------------------------------------------------------------------------------------------

QList<ModsPage::Picked> ModsPage::pick(const QList<int>& rows, bool* valid) const
{
    QList<Picked> out;
    *valid = true;
    if (!m_model) {
        *valid = false;
        return out;
    }
    for (const int row : rows) {
        if (row < 0 || row >= m_listModel->rowCount()) {
            *valid = false;
            continue;
        }
        Picked picked;
        picked.listRow = row;
        picked.sourceRow = m_listModel->sourceRowAt(row);
        if (picked.sourceRow >= 0 && picked.sourceRow < m_model->size()) {
            Mod* mod = &m_model->at(picked.sourceRow);
            if (reinterpret_cast<quintptr>(mod) == m_listModel->tokenAt(row)) {
                picked.mod = mod;
            }
        }
        if (!picked.mod) {
            *valid = false;
            continue;
        }
        out.append(picked);
    }
    return out;
}

bool ModsPage::stillThere(const Picked& picked) const
{
    return m_model && picked.mod && picked.sourceRow >= 0 && picked.sourceRow < m_model->size() && &m_model->at(picked.sourceRow) == picked.mod;
}

bool ModsPage::askFor(const ChoicePrompt& prompt, int* answer)
{
    *answer = m_env.ask(this, prompt);
    return *answer >= 0;
}

void ModsPage::toast(const QString& text, ZeroToast::Kind kind)
{
    ZeroToast::show(window(), text, kind);
}

// ---- enable / disable ------------------------------------------------------------------------------------------------------------

void ModsPage::onSwitchClicked(int row)
{
    if (row < 0 || row >= m_listModel->rowCount()) {
        return;
    }
    QList<int> rows = selectedRows();
    if (rows.size() < 2 || !rows.contains(row)) {
        rows = { row };
    }
    requestToggle(rows, m_listModel->rowAt(row).enabled ? ToggleAction::Disable : ToggleAction::Enable);
}

void ModsPage::onToggleKey()
{
    QList<int> rows = selectedRows();
    if (rows.isEmpty() && m_list->currentIndex().isValid()) {
        rows = { m_list->currentIndex().row() };
    }
    if (rows.isEmpty()) {
        return;
    }
    QList<bool> flags;
    for (const int row : rows) {
        flags.append(m_listModel->rowAt(row).enabled);
    }
    requestToggle(rows, nextToggle(flags));
}

void ModsPage::requestToggle(const QList<int>& rows, ToggleAction action)
{
    // Never reach Prism's own "the game is running" box: that is a modal dialog with no parent.
    refreshRunning();
    if (!m_model || changesBlocked() || m_mapsStale || rows.isEmpty()) {
        return;
    }
    bool valid = true;
    const QList<Picked> picked = pick(rows, &valid);
    if (!valid) {
        scheduleRebuild();
        return;
    }
    toggleMods(picked, action);
}

void ModsPage::toggleMods(const QList<Picked>& picked, ToggleAction action)
{
    refreshRunning();
    if (!m_model || changesBlocked() || m_mapsStale || picked.isEmpty()) {
        return;
    }
    const bool wantEnabled = action == ToggleAction::Enable;
    QList<Picked> targets;
    for (const Picked& p : picked) {
        if (p.mod->enabled() != wantEnabled) {
            targets.append(p);
        }
    }
    if (targets.isEmpty()) {
        return;
    }
    const EnableAction prism = wantEnabled ? EnableAction::ENABLE : EnableAction::DISABLE;

    QModelIndexList targetIndexes;
    QStringList targetNames;
    for (const Picked& p : targets) {
        targetIndexes.append(m_model->index(p.sourceRow, 0));
        targetNames.append(nameOf(*p.mod));
    }

    // The mods outside the selection the change reaches (their maps were checked above: no removal is pending).
    QList<Picked> affected;
    QStringList affectedNames;
    for (const QModelIndex& index : m_model->getAffectedMods(targetIndexes, prism)) {
        const int sourceRow = index.row();
        if (sourceRow < 0 || sourceRow >= m_model->size()) {
            continue;
        }
        const bool inTargets = std::any_of(targets.begin(), targets.end(), [sourceRow](const Picked& t) { return t.sourceRow == sourceRow; });
        if (inTargets) {
            continue;
        }
        Mod* mod = &m_model->at(sourceRow);
        if (mod->enabled() == wantEnabled) {
            continue;
        }
        affected.append({ -1, sourceRow, mod });
        affectedNames.append(nameOf(*mod));
    }

    QModelIndexList finalIndexes = targetIndexes;
    const TogglePrompt prompt = togglePrompt(action, targetNames, affectedNames);
    if (prompt.needed) {
        ChoicePrompt ask;
        ask.title = prompt.title;
        ask.body = prompt.body;
        ask.buttons = { { tr("Cancel"), ChoiceButton::Kind::Ghost },
                        { prompt.onlyLabel, ChoiceButton::Kind::Secondary },
                        { prompt.allLabel, ChoiceButton::Kind::Primary } };
        ask.defaultButton = 2;
        int answer = -1;
        if (!askFor(ask, &answer) || answer == 0) {
            return;
        }
        // The dialog ran an event loop: the Prism model may have moved on, and the game may have started.
        refreshRunning();
        const bool intact = std::all_of(targets.begin(), targets.end(), [this](const Picked& p) { return stillThere(p); }) &&
                            std::all_of(affected.begin(), affected.end(), [this](const Picked& p) { return stillThere(p); });
        if (!intact || changesBlocked()) {
            scheduleRebuild();
            return;
        }
        if (answer == 2) {
            for (const Picked& p : affected) {
                finalIndexes.append(m_model->index(p.sourceRow, 0));
            }
        }
    }

    // The BASE implementation: ModFolderModel's override pops a modal box about related mods, which this page asked itself.
    m_model->ResourceFolderModel::setResourceEnabled(finalIndexes, prism);

    QStringList failed;
    for (const QModelIndex& index : finalIndexes) {
        const Mod& mod = m_model->at(index.row());
        if (mod.enabled() != wantEnabled) {
            failed.append(nameOf(mod));
        }
    }
    if (!failed.isEmpty()) {
        toast(toggleFailureText(action, failed), ZeroToast::Kind::Error);
    }
    scheduleRebuild();
}

// ---- remove ----------------------------------------------------------------------------------------------------------------------

void ModsPage::requestRemove(const QList<int>& rows)
{
    refreshRunning();
    if (!m_model || changesBlocked() || rows.isEmpty()) {
        return;
    }
    bool valid = true;
    const QList<Picked> picked = pick(rows, &valid);
    if (!valid || picked.isEmpty()) {
        scheduleRebuild();
        return;
    }
    QStringList names;
    for (const Picked& p : picked) {
        names.append(nameOf(*p.mod));
    }
    const RemoveConfirm confirm = removeConfirm(names);
    ChoicePrompt ask;
    ask.title = confirm.title;
    ask.list = confirm.listed;
    ask.more = confirm.more;
    ask.note = confirm.note;
    ask.buttons = { { tr("Cancel"), ChoiceButton::Kind::Ghost }, { confirm.confirmLabel, ChoiceButton::Kind::Danger } };
    ask.defaultButton = 0;  // Enter cancels
    int answer = -1;
    if (!askFor(ask, &answer) || answer != 1) {
        return;
    }
    // The dialog ran an event loop: check again.
    const bool intact = std::all_of(picked.begin(), picked.end(), [this](const Picked& p) { return stillThere(p); });
    refreshRunning();
    if (!intact || changesBlocked()) {
        scheduleRebuild();
        return;
    }

    // One file at a time, into the Recycle Bin. A file that cannot go there stays where it is, with its metadata: there is no
    // permanent delete in this page (Prism's deleteResources would delete for good when trashing fails).
    QStringList removed;
    QStringList notRemoved;
    QModelIndexList metadataToDrop;
    for (const Picked& p : picked) {
        const QString path = p.mod->fileinfo().absoluteFilePath();
        const bool gone = moveToRecycleBin(path, m_env.trash);
        if (gone) {
            removed.append(nameOf(*p.mod));
            if (p.mod->metadata()) {
                metadataToDrop.append(m_model->index(p.sourceRow, 0));
            }
        } else {
            notRemoved.append(nameOf(*p.mod));
        }
    }
    if (!metadataToDrop.isEmpty()) {
        m_model->deleteMetadata(metadataToDrop);  // only the packwiz entries of the files that went; it also reloads the folder
    }
    if (!removed.isEmpty()) {
        m_mapsStale = true;  // the rows are about to go: Prism's relation maps must be rebuilt before the next dependency question
        m_model->update();
    }
    const RemoveResult result = removeResult(removed, notRemoved);
    toast(result.text, result.outcome == Outcome::Success ? ZeroToast::Kind::Success
                       : result.outcome == Outcome::Warning ? ZeroToast::Kind::Warning
                                                            : ZeroToast::Kind::Error);
    scheduleRebuild();
}

// ---- the update check ------------------------------------------------------------------------------------------------------------

int ModsPage::updateCount() const
{
    return Mods::Installed::updateCount(m_results);
}

void ModsPage::refreshFilterOptions()
{
    const bool with = Mods::Installed::updateCount(m_results) > 0;
    if (with == m_hasUpdatesSegment) {
        return;
    }
    m_hasUpdatesSegment = with;
    QList<Content::FacetOption> options;
    for (const Filter filter : filtersFor(with)) {
        options.append({ filterId(filter), filterLabel(filter), {} });
    }
    // the control gives every segment the same width: the widest label decides
    const QFontMetrics fm(Style::font(Role::Label));
    int widest = 0;
    for (const auto& option : options) {
        widest = std::max(widest, fm.horizontalAdvance(option.label));
    }
    m_filterControl->setOptions(options);
    m_filterControl->setFixedWidth(int(options.size()) * (widest + 2 * Tokens::spaceSm + Tokens::spaceXs));  // fits a 1100 px window with five segments
}

void ModsPage::refreshUpdateChrome(bool hasMods)
{
    const auto& t = Theme::current();
    const int updates = Mods::Installed::updateCount(m_results);
    const bool available = bool(m_env.checkUpdates);

    m_checkButton->setVisible(hasMods && available && !m_locked);  // the running lock wins: no check while the game runs
    QString text = checkButtonText();
    bool enabled = !m_applying;
    bool withIcon = true;
    if (m_checking) {
        text = checkingButtonText();
        enabled = false;
    } else if (updates > 0) {
        text = updateAllText(updates);
        withIcon = false;
    }
    m_checkButton->setText(text);
    m_checkButton->setAccessibleName(text);
    m_checkButton->setEnabled(enabled);
    m_checkButton->setIcon(withIcon ? Icons::icon(QStringLiteral("refresh-cw"), enabled ? t.text : t.textTertiary) : QIcon());

    // "Check for updates again" is in the overflow menu only while there are updates (the button then reads "Update all N").
    if (QMenu* menu = m_overflow->menu()) {
        const bool inMenu = menu->actions().contains(m_checkAgain);
        const bool wanted = updates > 0 && available && !m_locked;
        if (wanted && !inMenu) {
            menu->addAction(m_checkAgain);
        } else if (!wanted && inMenu) {
            menu->removeAction(m_checkAgain);
        }
    }
    m_checkAgain->setEnabled(!changesBlocked());

    const int unverifiable = m_results.checked && !m_checking ? unverifiableCount(m_rows, m_env.curseForgeAvailable()) : 0;
    m_infoText->setText(unverifiableText(unverifiable));
    m_infoLine->setVisible(hasMods && unverifiable > 0);
}

bool ModsPage::checkedModsStillThere() const
{
    if (!m_model) {
        return false;
    }
    QSet<const void*> present;
    for (Resource* resource : m_model->allResources()) {
        present.insert(resource);
    }
    return std::all_of(m_checkedPtrs.begin(), m_checkedPtrs.end(), [&present](const void* p) { return present.contains(p); });
}

void ModsPage::startCheck()
{
    refreshRunning();
    if (!m_env.checkUpdates || !m_model || !m_loaded || changesBlocked()) {
        return;
    }
    rebuildNow();  // the rows are what the model holds right now

    const bool curseForge = m_env.curseForgeAvailable();
    QList<Resource::Ptr> resources;
    QHash<QString, QPair<QString, QString>> facts;
    QSet<const void*> pointers;
    for (const Row& row : std::as_const(m_rows)) {
        if (!canCheck(row, curseForge)) {
            continue;
        }
        const Resource::Ptr resource = m_model->find(row.id);
        if (!resource || !resource->metadata()) {  // the checkers dereference metadata() without looking
            continue;
        }
        resources.append(resource);
        facts.insert(row.updateKey, { row.metadataHash, row.fileStamp });
        pointers.insert(resource.get());
    }

    m_results = UpdateResults();
    m_updates.clear();
    m_failures.clear();
    m_failureOrder.clear();
    if (resources.isEmpty()) {
        m_results.checked = true;
        rebuildNow();
        toast(nothingToCheckText(), ZeroToast::Kind::Info);
        return;
    }

    m_checkedFacts = facts;
    m_checkedPtrs = pointers;
    m_checkedCount = int(resources.size());
    const int generation = ++m_checkGeneration;
    m_check = m_env.checkUpdates(resources, this);
    if (!m_check) {
        m_checkedFacts.clear();
        m_checkedPtrs.clear();
        rebuildNow();
        return;
    }
    m_checking = true;
    connect(m_check.data(), &ModUpdateCheck::modFailed, this, [this, generation](const QString& key, const QString& reason) {
        if (generation != m_checkGeneration || !m_checking || m_failures.contains(key)) {
            return;
        }
        m_failures.insert(key, reason.isEmpty() ? tr("unknown error") : reason);
        m_failureOrder.append(key);
    });
    connect(m_check.data(), &ModUpdateCheck::finished, this, [this, generation](bool ok, const QString& error) {
        if (generation == m_checkGeneration && m_checking) {
            onCheckFinished(ok, error);
        }
    });
    syncLock();
    rebuildNow();  // clears the old pills, shows "Checking N mods..."
    m_check->start();
}

void ModsPage::stopCheck(bool refresh)
{
    if (!m_checking) {
        return;
    }
    m_checking = false;
    ++m_checkGeneration;  // whatever the old check still says is ignored
    if (m_check) {
        disconnect(m_check.data(), nullptr, this, nullptr);
        m_check->abort();
        m_check->deleteLater();
        m_check = nullptr;
    }
    m_checkedFacts.clear();
    m_checkedPtrs.clear();
    m_failures.clear();
    m_failureOrder.clear();
    m_results = UpdateResults();
    m_updates.clear();
    if (refresh) {
        syncLock();
        updateChrome();
    }
}

void ModsPage::abortCheck()
{
    stopCheck(true);
}

void ModsPage::onCheckFinished(bool ok, const QString& error)
{
    QList<ModUpdate> found;
    if (m_check) {
        if (ok) {
            found = m_check->updates();
        }
        disconnect(m_check.data(), nullptr, this, nullptr);
        m_check->deleteLater();
        m_check = nullptr;
    }
    ++m_checkGeneration;
    m_checking = false;
    const QHash<QString, QPair<QString, QString>> facts = m_checkedFacts;
    const QHash<QString, QString> failures = m_failures;
    const QStringList failureOrder = m_failureOrder;
    m_checkedFacts.clear();
    m_checkedPtrs.clear();
    m_failures.clear();
    m_failureOrder.clear();
    m_results = UpdateResults();
    m_updates.clear();

    if (!ok) {
        syncLock();
        rebuildNow();
        toast(checkFailedText(error.isEmpty() ? tr("no answer") : error), ZeroToast::Kind::Error);
        return;
    }

    m_results.checked = true;
    QSet<QString> withUpdate;
    for (const ModUpdate& update : std::as_const(found)) {
        const auto fact = facts.constFind(update.key);
        if (fact == facts.constEnd() || withUpdate.contains(update.key)) {
            continue;
        }
        CheckEntry entry;
        entry.key = update.key;
        entry.metadataHash = fact->first;
        entry.fileStamp = fact->second;
        entry.newVersion = update.newVersion.isEmpty() ? tr("latest") : update.newVersion;
        entry.versionType = update.versionType;
        m_results.entries.append(entry);
        m_updates.insert(update.key, update);
        withUpdate.insert(update.key);
    }
    QString firstReason;
    for (const QString& key : failureOrder) {
        const auto fact = facts.constFind(key);
        if (fact == facts.constEnd() || withUpdate.contains(key)) {
            continue;
        }
        CheckEntry entry;
        entry.key = key;
        entry.metadataHash = fact->first;
        entry.fileStamp = fact->second;
        entry.error = failures.value(key);
        if (firstReason.isEmpty()) {
            firstReason = entry.error;
        }
        m_results.entries.append(entry);
    }
    syncLock();
    rebuildNow();

    const int updates = Mods::Installed::updateCount(m_results);
    if (everyCheckFailed(m_checkedCount, m_results)) {
        toast(checkFailedText(firstReason), ZeroToast::Kind::Error);
    } else if (updates == 0 && failureCount(m_results) == 0) {
        toast(upToDateText(), ZeroToast::Kind::Success);
    } else if (updates == 0) {
        toast(checkFailedText(firstReason), ZeroToast::Kind::Warning);
    }
}

void ModsPage::requestUpdate(const QList<int>& rows)
{
    refreshRunning();
    if (changesBlocked() || rows.isEmpty()) {
        return;
    }
    QStringList keys;
    for (const int row : rows) {
        if (row >= 0 && row < m_listModel->rowCount() && m_listModel->rowAt(row).hasUpdate()) {
            keys.append(m_listModel->rowAt(row).updateKey);
        }
    }
    applyKeys(keys);
}

void ModsPage::requestUpdateAll()
{
    refreshRunning();
    if (changesBlocked()) {
        return;
    }
    QStringList keys;
    for (const CheckEntry& entry : std::as_const(m_results.entries)) {
        if (entry.hasUpdate()) {
            keys.append(entry.key);
        }
    }
    applyKeys(keys);
}

void ModsPage::applyKeys(const QStringList& keys)
{
    if (!m_model || changesBlocked() || !m_env.applyUpdates) {
        return;
    }
    QList<ModUpdate> chosen;
    QStringList applied;
    for (const QString& key : keys) {
        const auto found = m_updates.constFind(key);
        if (found != m_updates.constEnd()) {
            chosen.append(found.value());
            applied.append(key);
        }
    }
    if (chosen.isEmpty()) {
        return;
    }
    QStringList names;
    for (const ModUpdate& update : std::as_const(chosen)) {
        names.append(update.name.isEmpty() ? update.key : update.name);
    }

    // A blocking dialog runs here: nothing else changes a mod meanwhile, and the page may be gone when it returns.
    QPointer<ModsPage> self(this);
    m_applying = true;
    syncLock();
    updateChrome();

    // The safety net first: a copy of the files this update replaces. Without it nothing is updated.
    const QString snapshots = m_env.snapshotsDir ? m_env.snapshotsDir() : QString();
    QString snapshotDir;
    QStringList before;
    if (!snapshots.isEmpty()) {
        Mods::Snapshot::CreateResult created;
        QString planError;
        const QList<Mods::Snapshot::ModFiles> plan = snapshotPlan(chosen, &planError);
        if (!planError.isEmpty()) {
            created.error = planError;
        } else {
            const QString modsDir = m_model->dir().absolutePath();
            const QString instanceId = m_env.target().id;
            const Mods::Snapshot::Hooks hooks = snapshotHooks();
            runBlocking([&]() { created = Mods::Snapshot::createSnapshot(snapshots, instanceId, modsDir, plan, hooks); });
            if (!self) {
                return;
            }
        }
        if (!created.ok) {
            m_applying = false;
            syncLock();
            updateChrome();
            toast(tr("Could not save a copy of your mods first, so nothing was updated: %1").arg(created.error), ZeroToast::Kind::Error);
            return;
        }
        snapshotDir = created.dir;
        before = created.before;
    }

    const bool ok = m_env.applyUpdates(window(), chosen);
    if (!self) {
        return;
    }

    // What the update did to the folder: the files that arrived, the mods that changed. An update that changed nothing leaves no snapshot.
    Mods::Snapshot::FinalizeResult finished;
    if (!snapshotDir.isEmpty() && m_model) {
        const QString modsDir = m_model->dir().absolutePath();
        runBlocking([&]() { finished = Mods::Snapshot::finalizeSnapshot(snapshotDir, modsDir, before, ok); });
        if (!self) {
            return;
        }
    }

    m_applying = false;
    // The downloads were used up (also after a failure): their entries leave the results. What really arrived shows up when the
    // folder is read again; for the rest, check again.
    dropEntries(m_results, applied);
    for (const QString& key : applied) {
        m_updates.remove(key);
    }
    refreshRunning();
    syncLock();
    if (m_model) {
        m_model->update();
    }
    rebuildNow();
    refreshSnapshot();

    if (finished.ok && !finished.pruned && finished.changedMods > 0) {
        // Updated (all of them, or some of them): the toast offers the way back.
        QString version;
        if (finished.changedNames.size() == 1) {
            for (const ModUpdate& update : std::as_const(chosen)) {
                if ((update.name.isEmpty() ? update.key : update.name) == finished.changedNames.first()) {
                    version = update.newVersion;
                }
            }
        }
        const QPointer<ModsPage> guard(this);
        ZeroToast::show(window(), updatedText(finished.changedNames, version), ok ? ZeroToast::Kind::Success : ZeroToast::Kind::Warning, tr("Undo"),
                        [guard, snapshotDir]() {
                            if (guard) {
                                guard->requestRollback(snapshotDir);
                            }
                        });
    } else if (ok) {
        toast(updatedText(names, chosen.size() == 1 ? chosen.first().newVersion : QString()), ZeroToast::Kind::Success);
    }
}

// ---- safe update: the snapshot and the roll back ---------------------------------------------------------------------------------

void ModsPage::runBlocking(const std::function<void()>& work)
{
    QEventLoop loop;
    QThreadPool::globalInstance()->start([&work, &loop]() {
        work();
        QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
    });
    loop.exec(QEventLoop::ExcludeUserInputEvents);
}

Mods::Snapshot::Hooks ModsPage::snapshotHooks() const
{
    Mods::Snapshot::Hooks hooks = m_env.snapshotHooks;
    if (!hooks.trashPath) {
        hooks.trashPath = m_env.trash;  // the Recycle Bin, never a permanent delete
    }
    return hooks;
}

QList<Mods::Snapshot::ModFiles> ModsPage::snapshotPlan(const QList<ModUpdate>& chosen, QString* error) const
{
    QList<Mods::Snapshot::ModFiles> plan;
    const QList<Mod*> mods = m_model ? m_model->allMods() : QList<Mod*>();
    for (const ModUpdate& update : chosen) {
        Mod* found = nullptr;
        for (Mod* mod : mods) {
            if (updateKeyOf(mod->internalId()) == update.key) {
                found = mod;
                break;
            }
        }
        const QString name = update.name.isEmpty() ? update.key : update.name;
        if (!found) {
            *error = tr("%1 is no longer in the mods folder.").arg(name);
            return {};
        }
        Mods::Snapshot::ModFiles files;
        files.name = name;
        files.oldVersion = found->version();
        if (files.oldVersion.isEmpty() && found->metadata()) {
            files.oldVersion = found->metadata()->versionNumber;
        }
        files.newVersion = update.newVersion;
        files.files.append(found->fileinfo().absoluteFilePath());
        if (const auto metadata = found->metadata()) {
            // The packwiz entry is `<slug>.pw.toml` in `.index` (Packwiz matches the name case-insensitively).
            const QString entry = metadata->slug.endsWith(QLatin1String(".pw.toml")) ? metadata->slug : metadata->slug + QStringLiteral(".pw.toml");
            const QDir index = m_model->indexDir();
            for (const QString& candidate : index.entryList(QDir::Files)) {
                if (candidate.compare(entry, Qt::CaseInsensitive) == 0) {
                    files.files.append(index.filePath(candidate));
                    break;
                }
            }
        }
        plan.append(files);
    }
    return plan;
}

void ModsPage::refreshSnapshot()
{
    const QString dir = m_env.snapshotsDir ? m_env.snapshotsDir() : QString();
    m_snapshot = dir.isEmpty() ? Mods::Snapshot::Info() : Mods::Snapshot::newestActive(dir);
    updateSnapshotNotice();
}

void ModsPage::updateSnapshotNotice()
{
    if (!m_snapshotNotice) {
        return;
    }
    if (!m_snapshot.valid) {
        m_snapshotNotice->hide();
        return;
    }
    m_snapshotNotice->setContent(Mods::Snapshot::noticeText(m_snapshot, QDateTime::currentMSecsSinceEpoch()),
                                 QLocale().toString(QDateTime::fromMSecsSinceEpoch(m_snapshot.createdMs), QLocale::LongFormat));
    QString reason;
    if (m_locked) {
        reason = tr("Close the game first");
    } else if (m_checking) {
        reason = tr("Wait for the update check to finish");
    } else if (m_applying) {
        reason = tr("Wait for the update to finish");
    }
    m_snapshotNotice->setRollbackEnabled(!changesBlocked(), reason);
    m_snapshotNotice->show();
}

void ModsPage::dismissSnapshot()
{
    if (!m_snapshot.valid) {
        return;
    }
    Mods::Snapshot::markDismissed(m_snapshot.dir);
    refreshSnapshot();
}

void ModsPage::requestRollback(const QString& snapshotDir)
{
    refreshRunning();
    const QString root = m_env.snapshotsDir ? m_env.snapshotsDir() : QString();
    if (!m_model || changesBlocked() || root.isEmpty()) {
        return;
    }
    const QString dir = snapshotDir.isEmpty() ? m_snapshot.dir : snapshotDir;
    // Only a snapshot of this instance: the folder must sit directly in the instance's snapshots folder.
    if (dir.isEmpty() || QDir::cleanPath(QFileInfo(dir).absolutePath()).compare(QDir::cleanPath(QDir(root).absolutePath()), Qt::CaseInsensitive) != 0) {
        return;
    }
    const Mods::Snapshot::Info info = Mods::Snapshot::readSnapshot(dir);
    if (!info.valid || info.rolledBack) {
        refreshSnapshot();
        ChoicePrompt problem;
        problem.title = tr("This update cannot be rolled back");
        problem.body = info.rolledBack ? tr("It was rolled back already.") : info.error;
        problem.buttons = { { tr("OK"), ChoiceButton::Kind::Primary } };
        int ignored = -1;
        askFor(problem, &ignored);
        return;
    }

    // What the question lists: the mods the update changed (all of them when it recorded none), "name  old <- new".
    QStringList lines;
    QStringList modNames;
    for (const Mods::Snapshot::ModEntry& mod : info.mods) {
        if (info.changedCount() > 0 && !mod.changed) {
            continue;
        }
        modNames.append(mod.name);
        const QString from = mod.oldVersion.isEmpty() ? tr("earlier version") : mod.oldVersion;
        lines.append(mod.newVersion.isEmpty() ? QStringLiteral("%1   ← %2").arg(mod.name, from)
                                              : QStringLiteral("%1   %2 ← %3").arg(mod.name, from, mod.newVersion));
    }
    const int count = int(modNames.size());
    ChoicePrompt ask;
    ask.title = count == 1 ? tr("Roll back %1 to the version you had before?").arg(modNames.first())
                           : tr("Roll back %1 mods to the versions you had before?").arg(count);
    ask.list = lines.mid(0, 12);
    if (lines.size() > 12) {
        ask.more = tr("and %1 more").arg(lines.size() - 12);
    }
    ask.note = tr("The updated files go to the Recycle Bin.");
    ask.buttons = { { tr("Cancel"), ChoiceButton::Kind::Ghost }, { tr("Roll back"), ChoiceButton::Kind::Primary } };
    ask.defaultButton = 0;  // Enter cancels
    int answer = -1;
    if (!askFor(ask, &answer) || answer != 1) {
        return;
    }
    // The dialog ran an event loop: check again.
    refreshRunning();
    if (!m_model || changesBlocked()) {
        return;
    }

    QPointer<ModsPage> self(this);
    m_applying = true;
    syncLock();
    updateChrome();
    Mods::Snapshot::RollbackResult result;
    const QString modsDir = m_model->dir().absolutePath();
    const Mods::Snapshot::Hooks hooks = snapshotHooks();
    runBlocking([&]() { result = Mods::Snapshot::rollback(info, modsDir, hooks); });
    if (!self) {
        return;
    }
    m_applying = false;
    refreshRunning();
    syncLock();
    if (m_model) {
        m_model->update();
    }
    rebuildNow();
    refreshSnapshot();

    if (result.ok) {
        toast(count == 1 ? tr("Rolled back %1").arg(modNames.first()) : tr("Rolled back %1 mods").arg(count),
              result.warning.isEmpty() ? ZeroToast::Kind::Success : ZeroToast::Kind::Warning);
        return;
    }
    // A failure says what state the folder is in, so it is a dialog, not a toast that goes away.
    ChoicePrompt failed;
    failed.title = tr("The roll back did not finish");
    failed.body = result.error;
    failed.buttons = { { tr("OK"), ChoiceButton::Kind::Primary } };
    int ignored = -1;
    askFor(failed, &ignored);
}

// ---- menus and the empty state ---------------------------------------------------------------------------------------------------

void ModsPage::onRowMenu(int row, const QPoint& globalPos)
{
    if (row < 0 || row >= m_listModel->rowCount()) {
        return;
    }
    QMenu menu(this);
    fillRowMenu(&menu, row);
    menu.exec(globalPos);
}

void ModsPage::fillRowMenu(QMenu* target, int row)
{
    if (!target || row < 0 || row >= m_listModel->rowCount()) {
        return;
    }
    QMenu& menu = *target;
    QList<int> rows = selectedRows();
    if (rows.size() < 2 || !rows.contains(row)) {
        rows = { row };
        selectRows(rows);
    }
    const bool many = rows.size() > 1;
    const Row& clicked = m_listModel->rowAt(row);
    const auto& t = Theme::current();

    // The update of the row (or of the selected rows) comes first.
    {
        QList<int> updatable;
        for (const int r : rows) {
            if (m_listModel->rowAt(r).hasUpdate()) {
                updatable.append(r);
            }
        }
        if (!updatable.isEmpty() && m_env.applyUpdates) {
            const QString text = updatable.size() == 1 ? updateToText(m_listModel->rowAt(updatable.first()).updateVersion)
                                                      : updateSelectedText(int(updatable.size()));
            auto* update = menu.addAction(Icons::icon(QStringLiteral("refresh-cw"), t.accent), text);
            update->setEnabled(!changesBlocked());
            connect(update, &QAction::triggered, this, [this, updatable]() { requestUpdate(updatable); });
            menu.addSeparator();
        }
    }
    ToggleAction toggle = clicked.enabled ? ToggleAction::Disable : ToggleAction::Enable;
    if (many) {
        QList<bool> flags;
        for (const int r : rows) {
            flags.append(m_listModel->rowAt(r).enabled);
        }
        toggle = nextToggle(flags);
    }
    const QString toggleText = many ? (toggle == ToggleAction::Disable ? tr("Disable %1 mods").arg(rows.size()) : tr("Enable %1 mods").arg(rows.size()))
                                    : (toggle == ToggleAction::Disable ? tr("Disable") : tr("Enable"));
    auto* toggleAction = menu.addAction(Icons::icon(QStringLiteral("power"), t.text), toggleText);
    toggleAction->setEnabled(!changesBlocked());
    connect(toggleAction, &QAction::triggered, this, [this, rows, toggle]() { requestToggle(rows, toggle); });

    if (!many && !clicked.homepage.isEmpty()) {
        auto* website = menu.addAction(Icons::icon(QStringLiteral("external-link"), t.text), tr("Open website"));
        const QString url = clicked.homepage;
        connect(website, &QAction::triggered, this, [url]() { DesktopServices::openUrl(QUrl(url)); });
    }
    if (!many) {
        auto* show = menu.addAction(Icons::icon(QStringLiteral("folder-open"), t.text), tr("Show in folder"));
        const QString folder = QFileInfo(clicked.filePath).absolutePath();
        connect(show, &QAction::triggered, this, [folder]() { DesktopServices::openPath(folder); });
    }
    menu.addSeparator();
    auto* remove = menu.addAction(Icons::icon(QStringLiteral("trash-2"), t.danger), many ? tr("Remove %1 mods").arg(rows.size()) : tr("Remove"));
    remove->setEnabled(!changesBlocked());
    connect(remove, &QAction::triggered, this, [this, rows]() { requestRemove(rows); });
}

void ModsPage::onEmptyAction()
{
    if (m_view == View::NoMatches) {
        m_search->clear();
        setFilter(Filter::All);
    } else if (m_view == View::Empty) {
        if (m_env.addMods) {
            m_env.addMods();
        }
        emit addModsRequested();
    }
}

void ModsPage::openMenuFolder()
{
    if (m_model) {
        DesktopServices::openPath(m_model->dir().absolutePath(), true);
    }
}

// ---- the Dependency Inspector ----------------------------------------------------------------------------------------------------

Mods::InstanceContext ModsPage::scanContext() const
{
    if (m_env.context) {
        return m_env.context();
    }
    // No mmc-pack.json to read (tests, odd instances): what the target says is what the mods are judged against.
    const Content::TargetInstance target = m_env.target();
    Mods::InstanceContext context;
    context.mcVersion = target.mcVersion;
    if (target.loaders.testFlag(ModPlatform::NeoForge)) {
        context.loader = Mods::InstanceLoader::NeoForge;
    } else if (target.loaders.testFlag(ModPlatform::Forge)) {
        context.loader = Mods::InstanceLoader::Forge;
    } else if (target.loaders.testFlag(ModPlatform::Quilt)) {
        context.loader = Mods::InstanceLoader::Quilt;
    } else if (target.loaders.testFlag(ModPlatform::Fabric)) {
        context.loader = Mods::InstanceLoader::Fabric;
    }
    return context;
}

void ModsPage::rescanNow()
{
    m_scanTimer.stop();
    startScan();
}

void ModsPage::startScan()
{
    if (!m_model) {
        return;
    }
    Mods::ScanRequest request;
    request.files.reserve(m_rows.size());
    for (const Row& row : m_rows) {
        request.files.append({ row.filePath, row.enabled });
    }
    if (request.files.isEmpty()) {
        m_scanner->cancel();
        m_inspection = Mods::Inspection();
        m_scanned = true;  // nothing to read: no skeleton to wait behind
        refreshPanel();
        return;
    }
    request.context = scanContext();
    m_scanner->start(request);
}

void ModsPage::onScanFinished(const Mods::Inspection& inspection)
{
    m_inspection = inspection;
    m_scanned = true;
    rebuild();  // the pills, the Issues filter, the caption and the panel read the new answer
}

int ModsPage::rowIndexOfPath(const QString& path) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).filePath == path) {
            return i;
        }
    }
    return -1;
}

PanelData ModsPage::panelDataFor(const Row& row, int rowIndex) const
{
    PanelData data;
    data.key = row.updateKey;
    data.name = row.name;
    data.version = row.version;
    data.fileName = row.fileName;
    data.path = row.filePath;
    data.iconId = row.hasIcon ? row.id : QString();
    data.enabled = row.enabled;
    data.canChange = !changesBlocked();
    data.mcVersion = m_env.target().mcVersion;
    const Mods::ModInspection* mod = m_scanned ? m_inspection.find(row.filePath) : nullptr;
    data.scanning = mod == nullptr;
    if (mod) {
        data.inspection = *mod;
    }

    // Needed by: what the scan found, plus Prism's own relations (packwiz dependencies, declared dependencies by mod id).
    QSet<QString> seen;
    if (mod) {
        for (const Mods::NeededBy& n : mod->neededBy) {
            data.neededBy.append({ n.path, n.name, n.enabled });
            seen.insert(n.path);
        }
    }
    if (m_model && !row.modId.isEmpty()) {
        for (const QString& name : m_model->requiredByList(row.modId)) {
            for (int i = 0; i < m_rows.size(); ++i) {
                const Row& other = m_rows.at(i);
                if (i != rowIndex && other.name == name && !seen.contains(other.filePath)) {
                    data.neededBy.append({ other.filePath, other.name, other.enabled });
                    seen.insert(other.filePath);
                    break;
                }
            }
        }
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::stable_sort(data.neededBy.begin(), data.neededBy.end(),
                     [&collator](const PanelNeededBy& a, const PanelNeededBy& b) { return collator.compare(a.name, b.name) < 0; });
    return data;
}

void ModsPage::refreshPanel()
{
    if (m_panelKey.isEmpty()) {
        m_panel->hide();
        m_list->setEscapeCloses(false);
        return;
    }
    int index = -1;
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).updateKey == m_panelKey) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        closePanel();  // the mod is gone
        return;
    }
    m_panel->setData(panelDataFor(m_rows.at(index), index));
    m_panel->show();
    m_list->setEscapeCloses(true);
}

void ModsPage::closePanel()
{
    m_panelKey.clear();
    m_panel->hide();
    m_list->setEscapeCloses(false);
}

void ModsPage::openPanelForKey(const QString& key)
{
    m_panelKey = key;
    refreshPanel();
}

void ModsPage::onRowClicked(int row)
{
    if (row < 0 || row >= m_listModel->rowCount() || selectedRows().size() >= 2) {
        return;
    }
    openPanelForKey(m_listModel->rowAt(row).updateKey);
}

void ModsPage::onDetailsKey()
{
    const QModelIndex current = m_list->currentIndex();
    if (!current.isValid() || current.row() >= m_listModel->rowCount() || selectedRows().size() >= 2) {
        return;
    }
    openPanelForKey(m_listModel->rowAt(current.row()).updateKey);
}

void ModsPage::onSelectionChanged()
{
    if (!isPanelOpen()) {
        return;
    }
    const QList<int> selected = selectedRows();
    if (selected.size() >= 2) {
        closePanel();  // a Ctrl / Shift selection is about several mods, not one
    } else if (selected.size() == 1 && m_listModel->rowAt(selected.first()).updateKey != m_panelKey) {
        openPanelForKey(m_listModel->rowAt(selected.first()).updateKey);  // the panel follows a single selection
    }
}

void ModsPage::showNeededBy(const QString& path)
{
    const auto listRowOf = [this, &path]() {
        for (int i = 0; i < m_listModel->rowCount(); ++i) {
            if (m_listModel->rowAt(i).filePath == path) {
                return i;
            }
        }
        return -1;
    };
    const int index = rowIndexOfPath(path);
    if (index < 0) {
        return;
    }
    int listRow = listRowOf();
    if (listRow < 0) {
        // The filter or the search hides it: the mod was asked for, so show everything.
        m_search->clear();
        setFilter(Filter::All);
        listRow = listRowOf();
    }
    if (listRow >= 0) {
        selectRows({ listRow });
        m_list->scrollTo(m_listModel->index(listRow));
    }
    openPanelForKey(m_rows.at(index).updateKey);
}

bool ModsPage::openPanelFor(const QString& name)
{
    for (const Row& row : m_rows) {
        if (row.name.compare(name, Qt::CaseInsensitive) == 0) {
            showNeededBy(row.filePath);
            return true;
        }
    }
    return false;
}

void ModsPage::enableFromPanel(const QString& path)
{
    const int index = rowIndexOfPath(path);
    if (!m_model || index < 0 || index >= m_model->size() || m_rows.at(index).enabled) {
        return;
    }
    Mod* mod = &m_model->at(index);
    if (reinterpret_cast<quintptr>(mod) != m_tokens.value(index)) {
        scheduleRebuild();  // the Prism model moved on since the row was read
        return;
    }
    // The same path as the switch: the affected-mods question, the running lock, the base enable.
    toggleMods({ { -1, index, mod } }, ToggleAction::Enable);
}

}  // namespace ModsUi
}  // namespace Zero
