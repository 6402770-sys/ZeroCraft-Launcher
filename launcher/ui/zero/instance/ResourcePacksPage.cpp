// SPDX-License-Identifier: GPL-3.0-only
#include "ResourcePacksPage.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

#include "Application.h"
#include "DesktopServices.h"
#include "FileSystem.h"
#include "minecraft/MinecraftInstance.h"
#include "ui/MainWindow.h"
#include "ui/pages/instance/ResourcePackPage.h"
#include "ui/zero/ZeroEmptyState.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroSkeleton.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/content/FilterPanel.h"
#include "ui/zero/instance/InstancePageParts.h"

namespace Zero {
namespace PacksUi {

using namespace Content::Packs;
using ModsUi::ChoiceButton;
using ModsUi::ChoicePrompt;

namespace {

bool isPackFile(const ResourcePack& pack)
{
    if (pack.type() == ResourceType::FOLDER) {
        return true;
    }
    if (pack.type() != ResourceType::ZIPFILE) {
        return false;
    }
    QString name = pack.fileinfo().fileName();
    if (name.endsWith(QLatin1String(".disabled"), Qt::CaseInsensitive)) {
        name.chop(9);
    }
    return name.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive);  // the game lists zips and folders only
}

PackRow rowFromPack(const ResourcePack& pack, const QString& mcVersion)
{
    PackRow row;
    row.id = pack.internalId();
    row.fileName = pack.fileinfo().fileName();
    row.filePath = pack.fileinfo().absoluteFilePath();
    row.folder = pack.type() == ResourceType::FOLDER;
    row.hidden = !row.folder && !pack.enabled();
    row.name = packNameOf(row.fileName, row.folder);
    if (row.name.trimmed().isEmpty()) {
        row.name = row.fileName;
    }
    row.description = plainDescription(pack.description());
    row.packFormat = pack.packFormat();
    row.fit = packFit(row.packFormat, pack.mappings(), mcVersion);
    row.homepage = pack.homepage();
    if (const auto metadata = pack.metadata()) {
        row.hasMetadata = true;
        row.source = metadata->provider == ModPlatform::ResourceProvider::FLAME ? Source::CurseForge : Source::Modrinth;
        row.projectName = metadata->name;
    }
    return row;
}

PacksPageEnvironment environmentFor(MinecraftInstance* inst)
{
    QPointer<MinecraftInstance> guarded(inst);
    PacksPageEnvironment env;
    env.target = [guarded]() { return guarded ? Content::targetFromInstance(guarded) : Content::TargetInstance(); };
    env.isRunning = [guarded]() { return guarded && guarded->isRunning(); };
    env.optionsPath = [guarded]() { return guarded ? optionsPathFor(guarded->gameRoot()) : QString(); };
    env.historyPath = [guarded]() { return guarded ? historyPathFor(guarded->instanceRoot()) : QString(); };
    const QString id = inst->id();
    env.addPacks = [id]() {
        if (auto* window = APPLICATION->showMainWindow()) {
            window->showDiscoverPacks(id);
        }
    };
    env.shouldDisplay = [guarded]() {
        return guarded && !guarded->traits().contains("no-texturepacks") && !guarded->traits().contains("texturepacks");
    };
    env.openClassic = [guarded](QWidget* parent) {
        if (!guarded) {
            return;
        }
        // Prism's own page in a plain dialog. The page is never "opened": the model is already watched by this page, and the classic
        // page's closedImpl() would stop that.
        auto* dialog = new QDialog(parent ? parent->window() : nullptr);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(QObject::tr("Resource packs (classic)"));
        auto* layout = new QVBoxLayout(dialog);
        layout->setContentsMargins(Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg);
        // No parent here: the page is a QMainWindow, and a QMainWindow made with a parent is a separate (hidden) window that addWidget()
        // does not reparent. Without one, the layout adopts it as a child widget (as Prism's PageContainer does).
        layout->addWidget(new ResourcePackPage(guarded, guarded->resourcePackList()));
        dialog->resize(960, 600);
        dialog->open();
    };
    return env;
}

}  // namespace

QWidget* ResourcePacksPage::banner() const
{
    return m_banner;
}

ResourcePacksPage::ResourcePacksPage(MinecraftInstance* inst, ResourcePackFolderModel* model, QWidget* parent)
    : ResourcePacksPage(environmentFor(inst), model, parent)
{
    connect(inst, &BaseInstance::runningStatusChanged, this, [this]() { refreshRunning(); });
}

ResourcePacksPage::ResourcePacksPage(const PacksPageEnvironment& env, ResourcePackFolderModel* model, QWidget* parent)
    : QWidget(parent), m_env(env), m_model(model)
{
    setObjectName(QStringLiteral("zeroPacksPage"));
    if (!m_env.trash) {
        m_env.trash = [](const QString& path) { return FS::trash(path); };
    }
    if (!m_env.ask) {
        m_env.ask = ModsUi::askWithDialog;
    }
    if (!m_env.target) {
        m_env.target = []() { return Content::TargetInstance(); };
    }
    build();
    connectModel();
}

ResourcePacksPage::~ResourcePacksPage()
{
    if (m_model && m_startedWatching) {
        m_model->stopWatching();
    }
}

void ResourcePacksPage::build()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(Tokens::spaceMd);

    m_banner = new ModsUi::RunningBanner(tr("Close the game to change packs"), this);
    m_banner->hide();
    root->addWidget(m_banner);

    // ---- toolbar --------------------------------------------------------------------------------------------------------------
    m_toolbar = new QWidget(this);
    m_toolbar->setObjectName(QStringLiteral("zeroPacksToolbar"));
    m_toolbar->setFixedHeight(Tokens::controlHeightLg);
    auto* row = new QHBoxLayout(m_toolbar);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(Tokens::spaceSm);

    m_search = ModsUi::makeSearchField(m_toolbar, tr("Search packs"));
    m_search->installEventFilter(this);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_query = text.trimmed();
        applyView();
    });
    row->addWidget(m_search, 1);

    m_filterControl = new Content::SegmentedControl({}, m_toolbar);
    m_filterControl->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    QList<Content::FacetOption> options;
    for (const Filter filter : allFilters()) {
        options.append({ filterId(filter), filterLabel(filter), {} });
    }
    const QFontMetrics fm(Style::font(Role::Label));
    int widest = 0;
    for (const auto& option : options) {
        widest = std::max(widest, fm.horizontalAdvance(option.label));
    }
    m_filterControl->setOptions(options);
    m_filterControl->setFixedWidth(int(options.size()) * (widest + 2 * Tokens::spaceSm + Tokens::spaceXs));
    m_filterControl->setSelected(filterId(Filter::All));
    m_filterControl->setAccessibleName(tr("Filter packs"));
    m_filterControl->onActivated = [this](const QString& value) { setFilter(filterFromId(value)); };
    row->addWidget(m_filterControl, 0, Qt::AlignVCenter);
    row->addStretch(1);

    m_addButton = ModsUi::makeAddButton(tr("Add packs"), m_toolbar);
    connect(m_addButton, &QPushButton::clicked, this, [this]() {
        if (m_env.addPacks) {
            m_env.addPacks();
        }
        emit addPacksRequested();
    });
    row->addWidget(m_addButton);

    m_overflow = ModsUi::makeOverflowButton(m_toolbar, QStringLiteral("zeroPacksOverflow"));
    auto* menu = new QMenu(m_overflow);
    auto* openFolder = menu->addAction(tr("Open resource packs folder"));
    connect(openFolder, &QAction::triggered, this, &ResourcePacksPage::openPacksFolder);
    m_restoreAction = menu->addAction(tr("Restore previous order"));
    connect(m_restoreAction, &QAction::triggered, this, [this]() { undo(); });
    connect(menu, &QMenu::aboutToShow, this, [this]() {
        refreshRunning();
        const QString history = m_env.historyPath ? m_env.historyPath() : QString();
        m_restoreAction->setEnabled(isOrdered() && !blocked() && !readHistory(history).isEmpty());
    });
    if (m_env.openClassic) {
        auto* classic = menu->addAction(tr("Open the classic resource packs page"));
        connect(classic, &QAction::triggered, this, [this]() { m_env.openClassic(this); });
    }
    m_overflow->setMenu(menu);
    row->addWidget(m_overflow);
    root->addWidget(m_toolbar);

    // The caption under the toolbar, and the note while the packs cannot be ordered.
    m_statusRow = new QWidget(this);
    m_statusRow->setObjectName(QStringLiteral("zeroPacksStatus"));
    m_statusRow->setFixedHeight(Tokens::controlHeight);
    auto* status = new QHBoxLayout(m_statusRow);
    status->setContentsMargins(0, 0, 0, 0);
    status->setSpacing(Tokens::spaceMd);
    m_caption = new QLabel(m_statusRow);
    Style::applyRole(m_caption, Role::Body);
    Style::setProperty(m_caption, "zeroTone", "secondary");
    m_caption->setAccessibleName(tr("Resource packs"));
    status->addWidget(m_caption, 0, Qt::AlignVCenter);
    status->addStretch(1);
    root->addWidget(m_statusRow);

    m_noteRow = new QWidget(this);
    m_noteRow->setObjectName(QStringLiteral("zeroPacksNote"));
    auto* noteLayout = new QHBoxLayout(m_noteRow);
    noteLayout->setContentsMargins(0, 0, 0, 0);
    noteLayout->setSpacing(Tokens::spaceSm);
    m_noteIcon = new QLabel(m_noteRow);
    m_noteIcon->setFixedSize(14, 14);
    noteLayout->addWidget(m_noteIcon, 0, Qt::AlignTop);
    m_note = new QLabel(m_noteRow);
    Style::applyRole(m_note, Role::Caption);
    Style::setProperty(m_note, "zeroTone", "secondary");
    m_note->setWordWrap(true);
    noteLayout->addWidget(m_note, 1);
    m_noteRow->hide();
    root->addWidget(m_noteRow);

    // ---- body -----------------------------------------------------------------------------------------------------------------
    m_stack = new QStackedWidget(this);
    m_skeleton = new ZeroSkeleton(ZeroSkeleton::Shape::Rows, tr("Reading the resource packs folder…"), m_stack);
    m_stack->addWidget(m_skeleton);

    m_list = new PackListView(m_stack);
    m_listModel = new PackListModel(m_list);
    m_list->setModel(m_listModel);
    m_list->setAccessibleName(tr("Resource packs"));
    m_list->rowDelegate()->setIconProvider([this](const QString& id, int px, qreal dpr) -> QPixmap {
        if (!m_model) {
            return {};
        }
        const Resource::Ptr resource = m_model->find(id);
        if (!resource) {
            return {};
        }
        QPixmap pixmap = std::static_pointer_cast<ResourcePack>(resource)->image(QSize(qRound(px * dpr), qRound(px * dpr)), Qt::KeepAspectRatio);
        pixmap.setDevicePixelRatio(dpr);
        return pixmap;
    });
    m_stack->addWidget(m_list);

    m_empty = new ZeroEmptyState(QStringLiteral("images"), tr("No resource packs yet"), QString(), QString(), m_stack);
    m_stack->addWidget(m_empty);
    root->addWidget(m_stack, 1);

    // ---- wiring ---------------------------------------------------------------------------------------------------------------
    connect(m_list, &PackListView::switchClicked, this, &ResourcePacksPage::onSwitchClicked);
    connect(m_list, &PackListView::unhideClicked, this, [this](int listRow) { requestUnhide(listRow); });
    connect(m_list, &PackListView::menuRequested, this, &ResourcePacksPage::onRowMenu);
    connect(m_list, &PackListView::toggleKeyPressed, this, &ResourcePacksPage::onToggleKey);
    connect(m_list, &PackListView::removeKeyPressed, this, [this]() { requestRemove(selectedRow()); });
    connect(m_list, &PackListView::moveKeyPressed, this, &ResourcePacksPage::onMoveKey);
    connect(m_list, &PackListView::reorderRequested, this, [this](int from, int to) { movePack(from, to); });
    connect(m_list->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() {
        if (!m_building) {
            updateChrome();
        }
    });
    connect(m_empty, &ZeroEmptyState::actionTriggered, this, &ResourcePacksPage::onEmptyAction);

    auto* find = new QShortcut(QKeySequence::Find, this);
    find->setContext(Qt::WidgetWithChildrenShortcut);
    connect(find, &QShortcut::activated, this, [this]() {
        m_search->setFocus(Qt::ShortcutFocusReason);
        m_search->selectAll();
    });

    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.setInterval(30);
    connect(&m_rebuildTimer, &QTimer::timeout, this, &ResourcePacksPage::rebuild);

    const auto refreshIcons = [this]() {
        const auto& t = Theme::current();
        m_addButton->setIcon(Icons::icon(QStringLiteral("plus"), t.onAccent));
        m_overflow->setIcon(Icons::icon(QStringLiteral("ellipsis"), t.text));
        m_noteIcon->setPixmap(Icons::icon(QStringLiteral("info"), t.textSecondary).pixmap(QSize(14, 14)));
        if (!m_search->actions().isEmpty()) {
            m_search->actions().first()->setIcon(Icons::icon(QStringLiteral("search"), t.textTertiary));
        }
    };
    m_search->addAction(Icons::icon(QStringLiteral("search"), Theme::current().textTertiary), QLineEdit::LeadingPosition);
    connect(&Theme::instance(), &Theme::changed, this, refreshIcons);
    refreshIcons();

    setView(View::Loading);
    updateChrome();
}

void ResourcePacksPage::connectModel()
{
    if (!m_model) {
        return;
    }
    ResourcePackFolderModel* model = m_model;
    const auto schedule = [this]() { scheduleRebuild(); };
    connect(model, &QAbstractItemModel::dataChanged, this, schedule);
    connect(model, &QAbstractItemModel::rowsInserted, this, schedule);
    connect(model, &QAbstractItemModel::rowsRemoved, this, schedule);
    connect(model, &QAbstractItemModel::modelReset, this, schedule);
    connect(model, &ResourceFolderModel::updateFinished, this, [this]() {
        m_loaded = true;
        m_rebuildTimer.stop();
        rebuild();  // the folder reloaded: options.txt is read again with it
    });
    connect(model, &ResourceFolderModel::parseFinished, this, schedule);
}

void ResourcePacksPage::openedImpl()
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
}

void ResourcePacksPage::closedImpl()
{
    m_rebuildTimer.stop();
    m_list->cancelDrag();
    if (m_model && m_startedWatching) {
        m_model->stopWatching();
    }
    m_startedWatching = false;
}

void ResourcePacksPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refreshRunning();
    rebuild();  // options.txt may have changed while the page was away
}

void ResourcePacksPage::refreshRunning()
{
    const bool running = m_env.isRunning && m_env.isRunning();
    if (running == m_locked && m_banner->isVisibleTo(this) == running) {
        return;
    }
    const bool wasLocked = m_locked;
    m_locked = running;
    m_list->rowDelegate()->setLocked(running);
    if (running) {
        m_list->cancelDrag();
    }
    m_list->viewport()->update();
    if (wasLocked && !running) {
        // the game stopped: it rewrites options.txt on exit
        if (isOpened || isVisible()) {
            m_rebuildTimer.stop();
            rebuild();
            return;
        }
        m_dirty = true;
    }
    updateChrome();
}

void ResourcePacksPage::scheduleRebuild()
{
    if (!isOpened && !isVisible()) {
        m_dirty = true;
        return;
    }
    if (!m_rebuildTimer.isActive()) {
        m_rebuildTimer.start();
    }
}

void ResourcePacksPage::rebuildNow()
{
    m_rebuildTimer.stop();
    rebuild();
}

void ResourcePacksPage::reloadOptions()
{
    const QString path = m_env.optionsPath ? m_env.optionsPath() : QString();
    m_options = path.isEmpty() ? Content::Packs::PackList() : readOptions(path);
    m_orderState = !supportsOrdering(m_env.target().mcVersion) ? OrderState::OldVersion
                   : m_options.state == Content::Packs::PackList::State::Unreadable ? OrderState::Unreadable
                                                                                    : OrderState::Ready;
}

void ResourcePacksPage::rebuild()
{
    m_dirty = false;
    if (!m_model) {
        return;
    }
    reloadOptions();
    const QString mc = m_env.target().mcVersion;
    m_rows.clear();
    m_tokens.clear();
    m_sourceRows.clear();
    const QList<ResourcePack*> packs = m_model->allResourcePacks();
    for (int i = 0; i < packs.size(); ++i) {
        if (!isPackFile(*packs.at(i))) {
            continue;
        }
        m_rows.append(rowFromPack(*packs.at(i), mc));
        m_tokens.append(reinterpret_cast<quintptr>(packs.at(i)));
        m_sourceRows.append(i);
    }
    if (isOrdered()) {
        overlayActive(m_rows, activeFiles(m_options.entries));
    }
    applyView();
}

void ResourcePacksPage::applyView()
{
    // Remember what is selected: a reset of the list (another filter, a pack added, a move) clears the selection model.
    quintptr keepToken = 0;
    QString keepId;
    if (const int row = selectedRow(); row >= 0) {
        keepToken = m_listModel->entryAt(row).token;
        keepId = m_listModel->entryAt(row).row.id;
    }

    const QList<ListEntry> layout = layoutList(m_rows, m_filter, m_query, isOrdered());
    QList<PackListModel::Entry> entries;
    entries.reserve(layout.size());
    int headers = 0;
    for (const ListEntry& line : layout) {
        PackListModel::Entry entry;
        entry.kind = line.kind;
        entry.text = line.text;
        entry.onIndex = line.onIndex;
        if (line.kind == ListEntry::Kind::Pack) {
            entry.row = m_rows.at(line.row);
            entry.token = m_tokens.at(line.row);
            entry.sourceRow = m_sourceRows.at(line.row);
        } else {
            entry.token = quintptr(1 + headers++);  // the headers and the default row: fixed, never a Prism pack
        }
        entries.append(entry);
    }
    const int scroll = m_list->verticalScrollBar()->value();
    m_building = true;
    m_list->rowDelegate()->setOrdered(isOrdered());
    const bool reset = m_listModel->setEntries(entries);
    if (reset) {
        int keepRow = -1;
        for (int i = 0; i < m_listModel->rowCount(); ++i) {
            if (m_listModel->isPack(i) && ((keepToken != 0 && m_listModel->entryAt(i).token == keepToken) ||
                                           (!keepId.isEmpty() && m_listModel->entryAt(i).row.id == keepId))) {
                keepRow = i;
                break;
            }
        }
        if (keepRow >= 0) {
            m_list->selectionModel()->setCurrentIndex(m_listModel->index(keepRow), QItemSelectionModel::ClearAndSelect);
        } else {
            m_list->selectionModel()->clear();
        }
        m_list->verticalScrollBar()->setValue(scroll);
    }
    m_building = false;
    updateChrome();
}

void ResourcePacksPage::updateChrome()
{
    const Counts counts = countOf(m_rows);
    const bool narrowed = m_filter != Filter::All || !m_query.isEmpty();
    const Content::TargetInstance target = m_env.target();
    const bool canAdd = target.packReady() && bool(m_env.addPacks);
    int shownPacks = 0;
    for (int i = 0; i < m_listModel->rowCount(); ++i) {
        shownPacks += m_listModel->isPack(i) ? 1 : 0;
    }

    View next = View::List;
    if (!m_loaded) {
        next = View::Loading;
    } else if (m_rows.isEmpty()) {
        next = View::Empty;
    } else if (shownPacks == 0) {
        next = View::NoMatches;
    }
    switch (next) {
        case View::Empty:
            m_empty->setIcon(QStringLiteral("images"));
            m_empty->setTitle(tr("No resource packs yet"));
            m_empty->setBody(target.name.isEmpty() ? tr("Find packs in Discover.") : tr("Find packs for %1 in Discover.").arg(target.name));
            m_empty->setActionText(canAdd ? tr("Add packs") : QString());
            break;
        case View::NoMatches:
            m_empty->setIcon(QStringLiteral("search"));
            m_empty->setTitle(tr("No packs match"));
            m_empty->setBody(tr("Try another search or filter."));
            m_empty->setActionText(tr("Clear filters"));
            break;
        case View::Loading:
        case View::List:
            break;
    }
    setView(next);

    const bool hasPacks = next == View::List || next == View::NoMatches;
    m_search->setVisible(hasPacks);
    m_filterControl->setVisible(hasPacks && isOrdered());
    m_statusRow->setVisible(hasPacks);
    m_addButton->setVisible(hasPacks && canAdd);
    m_caption->setText(captionText(counts, shownPacks, narrowed, isOrdered()));

    QString note;
    if (m_orderState == OrderState::OldVersion) {
        note = tr("Turn packs on in the game for Minecraft 1.12 and older.");
    } else if (m_orderState == OrderState::Unreadable) {
        note = tr("The pack list in options.txt can't be read here. Turn packs on in the game.");
    }
    m_note->setText(note);
    m_noteRow->setVisible(hasPacks && !note.isEmpty());

    m_list->rowDelegate()->setLocked(m_locked);
    m_list->viewport()->update();
    m_banner->setVisible(m_locked);
}

void ResourcePacksPage::setView(View view)
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
        case View::NoMatches:
            m_stack->setCurrentWidget(m_empty);
            break;
    }
}

void ResourcePacksPage::setFilter(Filter filter)
{
    m_filterControl->setSelected(filterId(filter));
    if (filter == m_filter) {
        return;
    }
    m_filter = filter;
    applyView();
}

QString ResourcePacksPage::searchText() const
{
    return m_search->text().trimmed();
}

void ResourcePacksPage::setSearchText(const QString& text)
{
    m_search->setText(text);  // textChanged applies it
}

QString ResourcePacksPage::noteText() const
{
    return m_noteRow->isVisibleTo(this) ? m_note->text() : QString();
}

int ResourcePacksPage::selectedRow() const
{
    const QModelIndexList rows = m_list->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.first().row();
}

void ResourcePacksPage::selectRow(int row)
{
    if (row < 0 || row >= m_listModel->rowCount() || !m_listModel->isPack(row)) {
        m_list->selectionModel()->clear();
        return;
    }
    m_list->selectionModel()->setCurrentIndex(m_listModel->index(row), QItemSelectionModel::ClearAndSelect);
}

bool ResourcePacksPage::eventFilter(QObject* watched, QEvent* event)
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
                for (int i = 0; i < m_listModel->rowCount(); ++i) {
                    if (m_listModel->isPack(i)) {
                        m_list->setCurrentIndex(m_listModel->index(i));
                        break;
                    }
                }
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

QStringList ResourcePacksPage::activePacks() const
{
    QList<const PackRow*> active;
    for (const PackRow& row : m_rows) {
        if (row.active && row.activeIndex >= 0) {
            active.append(&row);
        }
    }
    std::sort(active.begin(), active.end(), [](const PackRow* a, const PackRow* b) { return a->activeIndex < b->activeIndex; });
    QStringList names;
    for (const PackRow* row : active) {
        names.append(row->fileName);
    }
    return names;
}

int ResourcePacksPage::onIndexOfRow(int row) const
{
    return m_listModel->isPack(row) ? m_listModel->entryAt(row).onIndex : -1;
}

// ---- picking packs ---------------------------------------------------------------------------------------------------------------

ResourcePacksPage::Picked ResourcePacksPage::pick(int row) const
{
    Picked picked;
    if (!m_model || row < 0 || row >= m_listModel->rowCount() || !m_listModel->isPack(row)) {
        return picked;
    }
    picked.listRow = row;
    picked.sourceRow = m_listModel->entryAt(row).sourceRow;
    if (picked.sourceRow >= 0 && picked.sourceRow < m_model->size()) {
        ResourcePack* pack = &m_model->at(picked.sourceRow);
        if (reinterpret_cast<quintptr>(pack) == m_listModel->entryAt(row).token) {
            picked.pack = pack;
        }
    }
    return picked;
}

bool ResourcePacksPage::stillThere(const Picked& picked) const
{
    return m_model && picked.pack && picked.sourceRow >= 0 && picked.sourceRow < m_model->size() && &m_model->at(picked.sourceRow) == picked.pack;
}

void ResourcePacksPage::toast(const QString& text, ZeroToast::Kind kind, const QString& undoId)
{
    if (text.isEmpty()) {
        return;
    }
    if (undoId.isEmpty()) {
        ZeroToast::show(window(), text, kind);
        return;
    }
    QPointer<ResourcePacksPage> guard(this);
    ZeroToast::show(window(), text, kind, tr("Undo"), [guard, undoId]() {
        if (guard) {
            guard->undo(undoId);
        }
    });
}

// ---- the order -------------------------------------------------------------------------------------------------------------------

bool ResourcePacksPage::apply(const QStringList& activeTopFirst, const QString& text, const QSet<QString>* existingOverride)
{
    refreshRunning();
    if (blocked() || !isOrdered() || !m_env.optionsPath || !m_env.historyPath) {
        return false;
    }
    const QStringList visible = activatableNames(m_rows);
    const QSet<QString> existing = existingOverride ? *existingOverride : QSet<QString>(visible.begin(), visible.end());
    const Content::Packs::Outcome out = writeOrder(m_env.optionsPath(), m_env.historyPath(), activeTopFirst, existing, m_env.isRunning);
    bool changed = false;
    switch (out.result) {
        case Result::Ok:
            changed = true;
            rebuildNow();
            toast(text, ZeroToast::Kind::Success, out.id);
            break;
        case Result::NoChange:
            break;
        case Result::Running:
            refreshRunning();
            break;
        case Result::Unreadable:
            rebuildNow();
            toast(tr("Couldn't change the packs: options.txt has a pack list this launcher can't read"), ZeroToast::Kind::Error);
            break;
        case Result::IoError:
            rebuildNow();
            toast(out.error.isEmpty() ? tr("Couldn't write options.txt") : out.error, ZeroToast::Kind::Error);
            break;
        case Result::NothingToUndo:
        case Result::Stale:
            break;
    }
    return changed;
}

bool ResourcePacksPage::setPackActive(const QString& fileName, bool on)
{
    refreshRunning();
    if (blocked() || !isOrdered()) {
        return false;
    }
    const auto it = std::find_if(m_rows.begin(), m_rows.end(), [&fileName](const PackRow& row) { return row.fileName == fileName && !row.hidden; });
    if (it == m_rows.end() || it->active == on) {
        return false;
    }
    QStringList next = activePacks();
    next.removeAll(fileName);
    if (on) {
        next.prepend(fileName);  // a pack that is turned on goes to the top
    }
    return apply(next, on ? turnedOnText(it->name) : turnedOffText(it->name));
}

bool ResourcePacksPage::movePack(int fromOn, int toOn)
{
    const QStringList active = activePacks();
    if (fromOn < 0 || fromOn >= active.size() || toOn < 0 || toOn >= active.size() || fromOn == toOn) {
        return false;
    }
    QString name = active.at(fromOn);
    for (const PackRow& row : m_rows) {
        if (row.fileName == name) {
            name = row.name;
            break;
        }
    }
    return apply(moved(active, fromOn, toOn), toOn == 0 ? movedToTopText(name) : toOn == active.size() - 1 ? movedToBottomText(name) : movedText(name));
}

void ResourcePacksPage::requestMove(int row, Move move)
{
    const int from = onIndexOfRow(row);
    const int count = int(activePacks().size());
    if (from < 0) {
        return;
    }
    int to = from;
    switch (move) {
        case Move::Top:
            to = 0;
            break;
        case Move::Up:
            to = from - 1;
            break;
        case Move::Down:
            to = from + 1;
            break;
        case Move::Bottom:
            to = count - 1;
            break;
    }
    if (to < 0 || to >= count || to == from) {
        return;
    }
    const QString name = m_listModel->entryAt(row).row.name;
    const QString text = move == Move::Top ? movedToTopText(name) : move == Move::Up ? movedUpText(name)
                         : move == Move::Down ? movedDownText(name) : movedToBottomText(name);
    apply(moved(activePacks(), from, to), text);
}

bool ResourcePacksPage::undo(const QString& expectedId)
{
    refreshRunning();
    if (blocked() || !m_env.optionsPath || !m_env.historyPath) {
        return false;
    }
    const Content::Packs::Outcome out = undoLast(m_env.optionsPath(), m_env.historyPath(), m_env.isRunning, expectedId);
    switch (out.result) {
        case Result::Ok:
        case Result::NoChange:
            rebuildNow();
            toast(restoredText(), ZeroToast::Kind::Success);
            return true;
        case Result::NothingToUndo:
            toast(tr("There is no earlier order to restore"), ZeroToast::Kind::Info);
            return false;
        case Result::Stale:
            toast(tr("That change is no longer the latest one"), ZeroToast::Kind::Info);
            return false;
        case Result::Running:
            refreshRunning();
            return false;
        case Result::Unreadable:
        case Result::IoError:
            toast(out.error.isEmpty() ? tr("Couldn't restore options.txt") : out.error, ZeroToast::Kind::Error);
            return false;
    }
    return false;
}

// ---- toggle, move keys, unhide, remove ---------------------------------------------------------------------------------------------

void ResourcePacksPage::onSwitchClicked(int row)
{
    requestToggle(row);
}

void ResourcePacksPage::onToggleKey()
{
    requestToggle(selectedRow());
}

void ResourcePacksPage::onMoveKey(int delta)
{
    const int row = selectedRow();
    if (row < 0) {
        return;
    }
    requestMove(row, delta < 0 ? Move::Up : Move::Down);
}

void ResourcePacksPage::requestToggle(int row)
{
    refreshRunning();
    if (blocked() || !isOrdered() || !m_listModel->isPack(row)) {
        return;
    }
    const PackRow& pack = m_listModel->entryAt(row).row;
    if (pack.hidden) {
        return;
    }
    setPackActive(pack.fileName, !pack.active);
}

void ResourcePacksPage::requestUnhide(int row)
{
    refreshRunning();
    if (blocked()) {
        return;
    }
    const Picked picked = pick(row);
    if (!picked.pack || !m_listModel->entryAt(row).row.hidden) {
        scheduleRebuild();
        return;
    }
    const QString name = m_listModel->entryAt(row).row.name;
    // The BASE implementation: it renames "x.zip.disabled" to "x.zip" and nothing else (a game-running box is never reached: checked above).
    const bool unhidden = m_model->ResourceFolderModel::setResourceEnabled({ m_model->index(picked.sourceRow, 0) }, EnableAction::ENABLE);
    if (unhidden) {
        toast(tr("%1 is visible to the game again").arg(name), ZeroToast::Kind::Success);
    } else {
        toast(tr("Couldn't unhide %1").arg(name), ZeroToast::Kind::Error);
    }
    scheduleRebuild();
}

void ResourcePacksPage::requestRemove(int row)
{
    refreshRunning();
    if (blocked() || row < 0) {
        return;
    }
    const Picked picked = pick(row);
    if (!picked.pack) {
        scheduleRebuild();
        return;
    }
    const PackRow info = m_listModel->entryAt(row).row;
    const Mods::Installed::RemoveConfirm confirm = Mods::Installed::removeConfirm({ info.name }, Mods::Installed::Noun::Packs);
    ChoicePrompt ask;
    ask.title = confirm.title;
    ask.list = confirm.listed;
    ask.more = confirm.more;
    ask.note = confirm.note;
    ask.buttons = { { tr("Cancel"), ChoiceButton::Kind::Ghost }, { confirm.confirmLabel, ChoiceButton::Kind::Danger } };
    ask.defaultButton = 0;  // Enter cancels
    if (m_env.ask(this, ask) != 1) {
        return;
    }
    // The dialog ran an event loop: the game may have started and the model may have moved on.
    refreshRunning();
    if (!stillThere(picked) || blocked()) {
        scheduleRebuild();
        return;
    }

    // Into the Recycle Bin. A file that cannot go there stays where it is, with its metadata: there is no permanent delete in this page
    // (Prism's deleteResources would delete for good when trashing fails).
    const QStringList visibleBefore = activatableNames(m_rows);
    const QSet<QString> existingBefore(visibleBefore.begin(), visibleBefore.end());
    const QString path = picked.pack->fileinfo().absoluteFilePath();
    const bool gone = ModsUi::moveToRecycleBin(path, m_env.trash);
    QStringList removed;
    QStringList notRemoved;
    if (gone) {
        removed.append(info.name);
        if (picked.pack->metadata()) {
            m_model->deleteMetadata({ m_model->index(picked.sourceRow, 0) });  // only the packwiz entry of the file that went; it also reloads the folder
        }
        m_model->update();
        if (info.active && isOrdered()) {
            QStringList next = activePacks();
            next.removeAll(info.fileName);
            apply(next, QString(), &existingBefore);  // out of resourcePacks, with the same backup; the toast below is the one the user needs
        }
    } else {
        notRemoved.append(info.name);
    }
    const auto result = Mods::Installed::removeResult(removed, notRemoved, Mods::Installed::Noun::Packs);
    toast(result.text, result.outcome == Mods::Installed::Outcome::Success ? ZeroToast::Kind::Success
                       : result.outcome == Mods::Installed::Outcome::Warning ? ZeroToast::Kind::Warning
                                                                              : ZeroToast::Kind::Error);
    scheduleRebuild();
}

// ---- menus and the empty state ---------------------------------------------------------------------------------------------------

void ResourcePacksPage::onRowMenu(int row, const QPoint& globalPos)
{
    if (!m_listModel->isPack(row)) {
        return;
    }
    QMenu menu(this);
    fillRowMenu(&menu, row);
    menu.exec(globalPos);
}

void ResourcePacksPage::fillRowMenu(QMenu* target, int row)
{
    if (!target || !m_listModel->isPack(row)) {
        return;
    }
    selectRow(row);
    QMenu& menu = *target;
    const PackRow& clicked = m_listModel->entryAt(row).row;
    const auto& t = Theme::current();
    const int on = onIndexOfRow(row);
    const int count = int(activePacks().size());

    if (clicked.hidden) {
        auto* unhide = menu.addAction(Icons::icon(QStringLiteral("eye"), t.text), tr("Unhide"));
        unhide->setEnabled(!blocked());
        connect(unhide, &QAction::triggered, this, [this, row]() { requestUnhide(row); });
        menu.addSeparator();
    } else if (isOrdered()) {
        auto* toggle = menu.addAction(Icons::icon(QStringLiteral("power"), t.text), clicked.active ? tr("Turn off") : tr("Turn on"));
        toggle->setEnabled(!blocked());
        connect(toggle, &QAction::triggered, this, [this, row]() { requestToggle(row); });
        if (clicked.active) {
            const struct {
                const char* icon;
                QString text;
                Move move;
                bool enabled;
            } moves[] = {
                { "chevrons-up", tr("Move to top"), Move::Top, on > 0 },
                { "chevron-up", tr("Move up"), Move::Up, on > 0 },
                { "chevron-down", tr("Move down"), Move::Down, on >= 0 && on < count - 1 },
                { "chevrons-down", tr("Move to bottom"), Move::Bottom, on >= 0 && on < count - 1 },
            };
            for (const auto& entry : moves) {
                auto* action = menu.addAction(Icons::icon(QString::fromLatin1(entry.icon), t.text), entry.text);
                action->setEnabled(!blocked() && entry.enabled);
                const Move move = entry.move;
                connect(action, &QAction::triggered, this, [this, row, move]() { requestMove(row, move); });
            }
        }
        menu.addSeparator();
    }
    auto* show = menu.addAction(Icons::icon(QStringLiteral("folder-open"), t.text), tr("Show in folder"));
    const QString folder = QFileInfo(clicked.filePath).absolutePath();
    connect(show, &QAction::triggered, this, [folder]() { DesktopServices::openPath(folder); });
    menu.addSeparator();
    auto* remove = menu.addAction(Icons::icon(QStringLiteral("trash-2"), t.danger), tr("Remove"));
    remove->setEnabled(!blocked());
    connect(remove, &QAction::triggered, this, [this, row]() { requestRemove(row); });
}

void ResourcePacksPage::onEmptyAction()
{
    if (m_view == View::NoMatches) {
        m_search->clear();
        setFilter(Filter::All);
    } else if (m_view == View::Empty) {
        if (m_env.addPacks) {
            m_env.addPacks();
        }
        emit addPacksRequested();
    }
}

void ResourcePacksPage::openPacksFolder()
{
    if (m_model) {
        DesktopServices::openPath(m_model->dir().absolutePath(), true);
    }
}

}  // namespace PacksUi
}  // namespace Zero
