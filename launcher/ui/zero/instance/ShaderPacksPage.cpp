// SPDX-License-Identifier: GPL-3.0-only
#include "ShaderPacksPage.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QStackedWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

#include "Application.h"
#include "DesktopServices.h"
#include "FileSystem.h"
#include "minecraft/MinecraftInstance.h"
#include "settings/SettingsObject.h"
#include "tasks/ConcurrentTask.h"
#include "ui/MainWindow.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/ProgressDialog.h"
#include "ui/dialogs/ResourceUpdateDialog.h"
#include "ui/pages/instance/ShaderPackPage.h"
#include "ui/zero/ZeroEmptyState.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroSkeleton.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/content/ShaderModNotice.h"
#include "ui/zero/instance/InstancePageParts.h"

namespace Zero {
namespace ShadersUi {

using ModsUi::ChoiceButton;
using ModsUi::ChoicePrompt;
using Mods::Installed::Row;

namespace {

bool isShaderFile(const ShaderPack& pack)
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
    return name.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive);  // the shader mods load zips and folders only
}

bool isFolderPack(const ShaderPack& pack)
{
    return pack.type() == ResourceType::FOLDER;
}

Row rowFromPack(const ShaderPack& pack)
{
    Row row;
    row.id = pack.internalId();
    row.fileName = pack.fileinfo().fileName();
    row.filePath = pack.fileinfo().absoluteFilePath();
    row.updateKey = Mods::Installed::updateKeyOf(row.id);
    row.name = pack.name().trimmed().isEmpty() ? row.fileName : pack.name().trimmed();
    row.enabled = isFolderPack(pack) || pack.enabled();
    row.homepage = pack.homepage();
    if (const auto metadata = pack.metadata()) {
        row.hasMetadata = true;
        row.source = metadata->provider == ModPlatform::ResourceProvider::FLAME ? Mods::Installed::Source::CurseForge
                                                                                 : Mods::Installed::Source::Modrinth;
    }
    // The line under the name: only what is really known, "version · size · modified".
    QStringList parts;
    if (const auto metadata = pack.metadata(); metadata && !metadata->versionNumber.trimmed().isEmpty()) {
        parts.append(metadata->versionNumber.trimmed());  // Resource::version() says "Unknown" without metadata: never shown
    }
    if (!pack.sizeStr().isEmpty()) {
        parts.append(pack.sizeStr());
    }
    if (pack.dateTimeChanged().isValid()) {
        parts.append(QLocale().toString(pack.dateTimeChanged().date(), QLocale::ShortFormat));
    }
    row.version = parts.join(QStringLiteral(" · "));
    return row;
}

ShadersPageEnvironment environmentFor(MinecraftInstance* inst)
{
    QPointer<MinecraftInstance> guarded(inst);
    ShadersPageEnvironment env;
    env.target = [guarded]() { return guarded ? Content::targetFromInstance(guarded) : Content::TargetInstance(); };
    env.isRunning = [guarded]() { return guarded && guarded->isRunning(); };
    const QString id = inst->id();
    env.addShaders = [id]() {
        if (auto* window = APPLICATION->showMainWindow()) {
            window->showDiscoverShaders(id);
        }
    };
    env.findMod = [id](const QString& query) {
        if (auto* window = APPLICATION->showMainWindow()) {
            window->showDiscoverMods(id, query);
        }
    };
    env.checkUpdates = [guarded](QWidget* parent) {
        // Prism's own checker over every shader pack (its modal dialogs; the page is locked while the game runs).
        if (!guarded || guarded->isRunning()) {
            return;
        }
        if (APPLICATION->settings()->get("ModMetadataDisabled").toBool()) {
            QMessageBox::critical(parent, QObject::tr("Error"), QObject::tr("Shader pack updates are unavailable when metadata is disabled!"));
            return;
        }
        ShaderPackFolderModel* model = guarded->shaderPackList();
        QList<Resource*> packs = model->allResources();
        if (packs.isEmpty()) {
            return;
        }
        ResourceUpdateDialog dialog(parent, guarded, model, packs, false);
        dialog.checkCandidates();
        if (dialog.aborted()) {
            return;
        }
        if (dialog.noUpdates()) {
            CustomMessageBox::selectable(parent, QObject::tr("Update checker"), QObject::tr("All shader packs are up-to-date!"))->exec();
            return;
        }
        if (dialog.exec() != 0) {
            ConcurrentTask tasks("Download Shader Packs", APPLICATION->settings()->get("NumberOfConcurrentDownloads").toInt());
            QObject::connect(&tasks, &Task::failed, parent, [parent](const QString& reason) {
                CustomMessageBox::selectable(parent, QObject::tr("Error"), reason, QMessageBox::Critical)->show();
            });
            for (const auto& task : dialog.getTasks()) {
                tasks.addTask(task);
            }
            ProgressDialog loadDialog(parent);
            loadDialog.showSkipButton();
            loadDialog.execWithTask(&tasks);
            model->update();
        }
    };
    env.openClassic = [guarded](QWidget* parent) {
        if (!guarded) {
            return;
        }
        // Prism's own page in a plain dialog. The page is never "opened": the model is already watched by this page, and the classic
        // page's closedImpl() would stop that.
        auto* dialog = new QDialog(parent ? parent->window() : nullptr);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(QObject::tr("Shader packs (classic)"));
        auto* layout = new QVBoxLayout(dialog);
        layout->setContentsMargins(Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg);
        // No parent here: the page is a QMainWindow (see ResourcePacksPage).
        layout->addWidget(new ShaderPackPage(guarded, guarded->shaderPackList()));
        dialog->resize(960, 600);
        dialog->open();
    };
    return env;
}

QString captionText(int total, int on, int shown, bool narrowed)
{
    if (narrowed) {
        return QObject::tr("%1 of %2 shown").arg(shown).arg(total);
    }
    return QObject::tr("%1 installed · %2 on").arg(total).arg(on);
}

}  // namespace

QWidget* ShaderPacksPage::banner() const
{
    return m_banner;
}

ShaderPacksPage::ShaderPacksPage(MinecraftInstance* inst, ShaderPackFolderModel* model, QWidget* parent)
    : ShaderPacksPage(environmentFor(inst), model, parent)
{
    connect(inst, &BaseInstance::runningStatusChanged, this, [this]() { refreshRunning(); });
}

ShaderPacksPage::ShaderPacksPage(const ShadersPageEnvironment& env, ShaderPackFolderModel* model, QWidget* parent)
    : QWidget(parent), m_env(env), m_model(model)
{
    setObjectName(QStringLiteral("zeroShadersPage"));
    if (!m_env.trash) {
        m_env.trash = [](const QString& path) { return FS::trash(path); };
    }
    if (!m_env.ask) {
        m_env.ask = ModsUi::askWithDialog;
    }
    if (!m_env.target) {
        m_env.target = []() { return Content::TargetInstance(); };
    }
    if (!m_env.pickFiles) {
        m_env.pickFiles = [](QWidget* parent) {
            return QFileDialog::getOpenFileNames(parent ? parent->window() : nullptr, tr("Add shader packs"), QString(), tr("Shader packs (*.zip)"));
        };
    }
    if (!m_env.pickFolder) {
        m_env.pickFolder = [](QWidget* parent) {
            return QFileDialog::getExistingDirectory(parent ? parent->window() : nullptr, tr("Add a shader pack folder"));
        };
    }
    build();
    connectModel();
}

ShaderPacksPage::~ShaderPacksPage()
{
    if (m_model && m_startedWatching) {
        m_model->stopWatching();
    }
}

void ShaderPacksPage::build()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(Tokens::spaceMd);

    m_banner = new ModsUi::RunningBanner(tr("Close the game to change shaders"), this);
    m_banner->hide();
    root->addWidget(m_banner);

    m_notice = new Content::ShaderModNotice(this);
    connect(m_notice, &Content::ShaderModNotice::findRequested, this, [this](const QString& query) {
        if (m_env.findMod) {
            m_env.findMod(query);
        }
    });
    root->addWidget(m_notice);

    // ---- toolbar --------------------------------------------------------------------------------------------------------------
    m_toolbar = new QWidget(this);
    m_toolbar->setObjectName(QStringLiteral("zeroShadersToolbar"));
    m_toolbar->setFixedHeight(Tokens::controlHeightLg);
    auto* row = new QHBoxLayout(m_toolbar);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(Tokens::spaceSm);

    m_search = ModsUi::makeSearchField(m_toolbar, tr("Search shader packs"));
    m_search->installEventFilter(this);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_query = text.trimmed();
        applyView();
    });
    row->addWidget(m_search, 1);
    row->addStretch(1);

    m_addFileButton = new QPushButton(tr("Add file"), m_toolbar);
    m_addFileButton->setObjectName(QStringLiteral("zeroShadersAddFile"));
    Style::setProperty(m_addFileButton, "zeroVariant", "secondary");
    Style::setProperty(m_addFileButton, "zeroSize", "lg");
    m_addFileButton->setFixedHeight(Tokens::controlHeightLg);
    m_addFileButton->setIconSize(QSize(16, 16));
    m_addFileButton->setAccessibleName(tr("Add file"));
    connect(m_addFileButton, &QPushButton::clicked, this, [this]() {
        refreshRunning();
        if (!blocked()) {
            addPaths(m_env.pickFiles(this));
        }
    });
    row->addWidget(m_addFileButton);

    m_downloadButton = ModsUi::makeAddButton(tr("Download shaders"), m_toolbar);
    connect(m_downloadButton, &QPushButton::clicked, this, [this]() {
        if (m_env.addShaders) {
            m_env.addShaders();
        }
        emit addShadersRequested();
    });
    row->addWidget(m_downloadButton);

    m_overflow = ModsUi::makeOverflowButton(m_toolbar, QStringLiteral("zeroShadersOverflow"));
    auto* menu = new QMenu(m_overflow);
    auto* addFolder = menu->addAction(tr("Add folder…"));
    connect(addFolder, &QAction::triggered, this, [this]() {
        refreshRunning();
        if (blocked()) {
            return;
        }
        const QString folder = m_env.pickFolder(this);
        if (!folder.isEmpty()) {
            addPaths({ folder });
        }
    });
    QAction* check = nullptr;
    if (m_env.checkUpdates) {
        check = menu->addAction(tr("Check for updates"));
        connect(check, &QAction::triggered, this, [this]() {
            refreshRunning();
            if (!blocked()) {
                m_env.checkUpdates(this);
            }
        });
    }
    menu->addSeparator();
    auto* openFolder = menu->addAction(tr("Open shader packs folder"));
    connect(openFolder, &QAction::triggered, this, &ShaderPacksPage::openShadersFolder);
    if (m_env.openClassic) {
        auto* classic = menu->addAction(tr("Open the classic shader packs page"));
        connect(classic, &QAction::triggered, this, [this]() { m_env.openClassic(this); });
    }
    connect(menu, &QMenu::aboutToShow, this, [this, addFolder, check]() {
        refreshRunning();
        addFolder->setEnabled(!blocked());
        if (check) {
            check->setEnabled(!blocked());
        }
    });
    m_overflow->setMenu(menu);
    row->addWidget(m_overflow);
    root->addWidget(m_toolbar);

    // The caption under the toolbar and the line about shader mods.
    m_statusRow = new QWidget(this);
    m_statusRow->setObjectName(QStringLiteral("zeroShadersStatus"));
    m_statusRow->setFixedHeight(Tokens::controlHeight);
    auto* status = new QHBoxLayout(m_statusRow);
    status->setContentsMargins(0, 0, 0, 0);
    status->setSpacing(Tokens::spaceMd);
    m_caption = new QLabel(m_statusRow);
    Style::applyRole(m_caption, Role::Body);
    Style::setProperty(m_caption, "zeroTone", "secondary");
    m_caption->setAccessibleName(tr("Shader packs"));
    status->addWidget(m_caption, 0, Qt::AlignVCenter);
    status->addStretch(1);
    root->addWidget(m_statusRow);

    m_subtitle = new QLabel(tr("Shaders add realistic lighting, shadows and water. They need a shader mod (Iris or OptiFine)."), this);
    m_subtitle->setObjectName(QStringLiteral("zeroShadersSubtitle"));
    Style::applyRole(m_subtitle, Role::Caption);
    Style::setProperty(m_subtitle, "zeroTone", "secondary");
    m_subtitle->setWordWrap(true);
    root->addWidget(m_subtitle);

    // ---- body -----------------------------------------------------------------------------------------------------------------
    m_stack = new QStackedWidget(this);
    m_skeleton = new ZeroSkeleton(ZeroSkeleton::Shape::Rows, tr("Reading the shader packs folder…"), m_stack);
    m_stack->addWidget(m_skeleton);

    m_list = new ModsUi::ModListView(m_stack);
    m_listModel = new ModsUi::ModListModel(m_list);
    m_list->setModel(m_listModel);
    m_list->setAccessibleName(tr("Shader packs"));
    m_list->rowDelegate()->setFallbackIcon(QStringLiteral("sun"));
    m_stack->addWidget(m_list);

    m_empty = new ZeroEmptyState(QStringLiteral("sun"), tr("No shader packs yet"), QString(), QString(), m_stack);
    m_stack->addWidget(m_empty);
    root->addWidget(m_stack, 1);

    // ---- wiring ---------------------------------------------------------------------------------------------------------------
    connect(m_list, &ModsUi::ModListView::switchClicked, this, [this](int row) { requestToggle({ row }); });
    connect(m_list, &ModsUi::ModListView::menuRequested, this, &ShaderPacksPage::onRowMenu);
    connect(m_list, &ModsUi::ModListView::toggleKeyPressed, this, [this]() { requestToggle(selectedRows()); });
    connect(m_list, &ModsUi::ModListView::removeKeyPressed, this, [this]() { requestRemove(selectedRows()); });
    connect(m_empty, &ZeroEmptyState::actionTriggered, this, &ShaderPacksPage::onEmptyAction);

    auto* find = new QShortcut(QKeySequence::Find, this);
    find->setContext(Qt::WidgetWithChildrenShortcut);
    connect(find, &QShortcut::activated, this, [this]() {
        m_search->setFocus(Qt::ShortcutFocusReason);
        m_search->selectAll();
    });

    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.setInterval(30);
    connect(&m_rebuildTimer, &QTimer::timeout, this, &ShaderPacksPage::rebuild);

    const auto refreshIcons = [this]() {
        const auto& t = Theme::current();
        m_downloadButton->setIcon(Icons::icon(QStringLiteral("download"), t.onAccent));
        m_addFileButton->setIcon(Icons::icon(QStringLiteral("plus"), t.text));
        m_overflow->setIcon(Icons::icon(QStringLiteral("ellipsis"), t.text));
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

void ShaderPacksPage::connectModel()
{
    if (!m_model) {
        return;
    }
    ShaderPackFolderModel* model = m_model;
    const auto schedule = [this]() { scheduleRebuild(); };
    connect(model, &QAbstractItemModel::dataChanged, this, schedule);
    connect(model, &QAbstractItemModel::rowsInserted, this, schedule);
    connect(model, &QAbstractItemModel::rowsRemoved, this, schedule);
    connect(model, &QAbstractItemModel::modelReset, this, schedule);
    connect(model, &ResourceFolderModel::updateFinished, this, [this]() {
        m_loaded = true;
        m_rebuildTimer.stop();
        rebuild();
    });
    connect(model, &ResourceFolderModel::parseFinished, this, schedule);
}

void ShaderPacksPage::openedImpl()
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

void ShaderPacksPage::closedImpl()
{
    m_rebuildTimer.stop();
    if (m_model && m_startedWatching) {
        m_model->stopWatching();
    }
    m_startedWatching = false;
}

void ShaderPacksPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refreshRunning();
    rebuild();  // the mods folder may have changed while the page was away (the shader mod strip)
}

void ShaderPacksPage::refreshRunning()
{
    const bool running = m_env.isRunning && m_env.isRunning();
    if (running == m_locked && m_banner->isVisibleTo(this) == running) {
        return;
    }
    m_locked = running;
    m_list->rowDelegate()->setLocked(running, running ? tr("Close the game to change shaders") : QString());
    m_list->viewport()->update();
    updateChrome();
}

void ShaderPacksPage::scheduleRebuild()
{
    if (!isOpened && !isVisible()) {
        m_dirty = true;
        return;
    }
    if (!m_rebuildTimer.isActive()) {
        m_rebuildTimer.start();
    }
}

void ShaderPacksPage::rebuildNow()
{
    m_rebuildTimer.stop();
    rebuild();
}

void ShaderPacksPage::rebuild()
{
    m_dirty = false;
    if (!m_model) {
        return;
    }
    m_rows.clear();
    m_tokens.clear();
    m_sourceRows.clear();
    const QList<Resource*> all = m_model->allResources();
    for (int i = 0; i < all.size(); ++i) {
        auto* pack = static_cast<ShaderPack*>(all.at(i));
        if (!isShaderFile(*pack)) {
            continue;
        }
        m_rows.append(rowFromPack(*pack));
        m_tokens.append(reinterpret_cast<quintptr>(pack));
        m_sourceRows.append(i);
    }
    const QString modsDir = m_env.target().modsDir;
    m_shaderMod = modsDir.isEmpty() ? Content::ShaderModFacts{ true, {} } : Content::shaderModFacts(Content::modFileNamesIn(modsDir));  // unknown folder: no strip
    applyView();
}

void ShaderPacksPage::applyView()
{
    // Remember what is selected: a reset of the list (another search, a pack added) clears the selection model.
    quintptr keepToken = 0;
    if (const QList<int> rows = selectedRows(); !rows.isEmpty()) {
        keepToken = m_listModel->tokenAt(rows.first());
    }

    const QList<int> visible = Mods::Installed::visibleRows(m_rows, Mods::Installed::Filter::All, m_query);
    QList<Row> rows;
    QList<quintptr> tokens;
    QList<int> sources;
    for (const int index : visible) {
        rows.append(m_rows.at(index));
        tokens.append(m_tokens.at(index));
        sources.append(m_sourceRows.at(index));
    }
    const int scroll = m_list->verticalScrollBar()->value();
    m_building = true;
    const bool reset = m_listModel->setRows(rows, tokens, sources);
    if (reset) {
        const int keepRow = keepToken != 0 ? m_listModel->rowOfToken(keepToken) : -1;
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

void ShaderPacksPage::updateChrome()
{
    const Content::TargetInstance target = m_env.target();
    const bool canAdd = target.packReady() && bool(m_env.addShaders);
    int on = 0;
    for (const Row& row : m_rows) {
        on += row.enabled ? 1 : 0;
    }
    const int shown = m_listModel->rowCount();
    const bool narrowed = !m_query.isEmpty();

    View next = View::List;
    if (!m_loaded) {
        next = View::Loading;
    } else if (m_rows.isEmpty()) {
        next = View::Empty;
    } else if (shown == 0) {
        next = View::NoMatches;
    }
    switch (next) {
        case View::Empty:
            m_empty->setIcon(QStringLiteral("sun"));
            m_empty->setTitle(tr("No shader packs yet"));
            m_empty->setBody(target.name.isEmpty() ? tr("Find shaders in Discover.") : tr("Find shaders for %1 in Discover.").arg(target.name));
            m_empty->setActionText(canAdd ? tr("Download shaders") : QString());
            break;
        case View::NoMatches:
            m_empty->setIcon(QStringLiteral("search"));
            m_empty->setTitle(tr("No shader packs match"));
            m_empty->setBody(tr("Try another search."));
            m_empty->setActionText(tr("Clear search"));
            break;
        case View::Loading:
        case View::List:
            break;
    }
    setView(next);

    const bool hasPacks = next == View::List || next == View::NoMatches;
    m_search->setVisible(hasPacks);
    m_statusRow->setVisible(hasPacks);
    m_downloadButton->setVisible(hasPacks && canAdd);
    m_addFileButton->setVisible(next != View::Loading);
    m_subtitle->setVisible(next != View::Loading);
    m_caption->setText(captionText(int(m_rows.size()), on, shown, narrowed));

    // The strip: only once the folder is read, and only while the mods folder has no shader mod.
    Content::ShaderNotice notice;
    if (m_loaded) {
        notice = Content::shaderNoticeFor(target.name, target.loaders, m_shaderMod);
        if (!m_env.findMod) {
            notice.buttonText.clear();
        }
    }
    m_notice->setNotice(notice);

    m_list->rowDelegate()->setLocked(m_locked, m_locked ? tr("Close the game to change shaders") : QString());
    m_list->viewport()->update();
    m_banner->setVisible(m_locked);
}

void ShaderPacksPage::setView(View view)
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

QString ShaderPacksPage::searchText() const
{
    return m_search->text().trimmed();
}

void ShaderPacksPage::setSearchText(const QString& text)
{
    m_search->setText(text);  // textChanged applies it
}

bool ShaderPacksPage::noticeShown() const
{
    return m_notice && m_notice->isVisibleTo(this);
}

QList<int> ShaderPacksPage::selectedRows() const
{
    QList<int> rows;
    for (const QModelIndex& index : m_list->selectionModel()->selectedRows()) {
        rows.append(index.row());
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

void ShaderPacksPage::selectRow(int row)
{
    if (row < 0 || row >= m_listModel->rowCount()) {
        m_list->selectionModel()->clear();
        return;
    }
    m_list->selectionModel()->setCurrentIndex(m_listModel->index(row), QItemSelectionModel::ClearAndSelect);
}

bool ShaderPacksPage::eventFilter(QObject* watched, QEvent* event)
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

// ---- picking packs ---------------------------------------------------------------------------------------------------------------

ShaderPacksPage::Picked ShaderPacksPage::pick(int row) const
{
    Picked picked;
    if (!m_model || row < 0 || row >= m_listModel->rowCount()) {
        return picked;
    }
    picked.listRow = row;
    picked.sourceRow = m_listModel->sourceRowAt(row);
    if (picked.sourceRow >= 0 && picked.sourceRow < m_model->size()) {
        auto* pack = static_cast<ShaderPack*>(&m_model->at(picked.sourceRow));
        if (reinterpret_cast<quintptr>(pack) == m_listModel->tokenAt(row)) {
            picked.pack = pack;
        }
    }
    return picked;
}

bool ShaderPacksPage::stillThere(const Picked& picked) const
{
    return m_model && picked.pack && picked.sourceRow >= 0 && picked.sourceRow < m_model->size() &&
           &m_model->at(picked.sourceRow) == static_cast<Resource*>(picked.pack);
}

void ShaderPacksPage::toast(const QString& text, ZeroToast::Kind kind)
{
    if (!text.isEmpty()) {
        ZeroToast::show(window(), text, kind);
    }
}

// ---- toggle, remove, add ---------------------------------------------------------------------------------------------------------

void ShaderPacksPage::requestToggle(const QList<int>& rows)
{
    refreshRunning();
    if (blocked() || rows.isEmpty() || !m_model) {
        return;
    }
    QList<Picked> picks;
    QList<bool> flags;
    QStringList folders;
    for (const int row : rows) {
        const Picked picked = pick(row);
        if (!picked.pack) {
            scheduleRebuild();
            return;
        }
        if (isFolderPack(*picked.pack)) {
            folders.append(m_listModel->rowAt(row).name);  // Prism cannot turn a folder off
            continue;
        }
        picks.append(picked);
        flags.append(m_listModel->rowAt(row).enabled);
    }
    if (picks.isEmpty()) {
        toast(folders.size() == 1 ? tr("%1 is a folder, and folders can't be turned off here").arg(folders.first())
                                  : tr("Folders can't be turned off here"),
              ZeroToast::Kind::Info);
        return;
    }
    const Mods::Installed::ToggleAction action = Mods::Installed::nextToggle(flags);
    QModelIndexList indexes;
    QStringList names;
    for (int i = 0; i < picks.size(); ++i) {
        if (flags.at(i) == (action == Mods::Installed::ToggleAction::Enable)) {
            continue;  // already in the state wanted
        }
        indexes.append(m_model->index(picks.at(i).sourceRow, 0));
        names.append(m_listModel->rowAt(picks.at(i).listRow).name);
    }
    if (indexes.isEmpty()) {
        return;
    }
    // The BASE implementation: it renames "x.zip" <-> "x.zip.disabled" and nothing else (the game-running box is never reached: checked above).
    QStringList failed;
    for (int i = 0; i < indexes.size(); ++i) {
        const bool done = m_model->ResourceFolderModel::setResourceEnabled(
            { indexes.at(i) }, action == Mods::Installed::ToggleAction::Enable ? EnableAction::ENABLE : EnableAction::DISABLE);
        if (!done) {
            failed.append(names.at(i));
        }
    }
    if (!failed.isEmpty()) {
        toast(Mods::Installed::toggleFailureText(action, failed), ZeroToast::Kind::Error);
    }
    rebuildNow();
}

void ShaderPacksPage::requestRemove(const QList<int>& rows)
{
    refreshRunning();
    if (blocked() || rows.isEmpty() || !m_model) {
        return;
    }
    QList<Picked> picks;
    QStringList names;
    for (const int row : rows) {
        const Picked picked = pick(row);
        if (!picked.pack) {
            scheduleRebuild();
            return;
        }
        picks.append(picked);
        names.append(m_listModel->rowAt(row).name);
    }
    const Mods::Installed::RemoveConfirm confirm = Mods::Installed::removeConfirm(names, Mods::Installed::Noun::Shaders);
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
    if (blocked()) {
        return;
    }
    for (const Picked& picked : picks) {
        if (!stillThere(picked)) {
            scheduleRebuild();
            return;
        }
    }

    // Into the Recycle Bin. A file that cannot go there stays where it is, with its metadata: there is no permanent delete in this page.
    QStringList removed;
    QStringList notRemoved;
    QModelIndexList withMetadata;
    for (int i = 0; i < picks.size(); ++i) {
        const QString path = picks.at(i).pack->fileinfo().absoluteFilePath();
        if (ModsUi::moveToRecycleBin(path, m_env.trash)) {
            removed.append(names.at(i));
            if (picks.at(i).pack->metadata()) {
                withMetadata.append(m_model->index(picks.at(i).sourceRow, 0));
            }
        } else {
            notRemoved.append(names.at(i));
        }
    }
    if (!withMetadata.isEmpty()) {
        m_model->deleteMetadata(withMetadata);  // only the packwiz entries of the files that went; it also reloads the folder
    }
    if (!removed.isEmpty()) {
        m_model->update();
    }
    const auto result = Mods::Installed::removeResult(removed, notRemoved, Mods::Installed::Noun::Shaders);
    toast(result.text, result.outcome == Mods::Installed::Outcome::Success ? ZeroToast::Kind::Success
                       : result.outcome == Mods::Installed::Outcome::Warning ? ZeroToast::Kind::Warning
                                                                              : ZeroToast::Kind::Error);
    scheduleRebuild();
}

void ShaderPacksPage::addPaths(const QStringList& paths)
{
    refreshRunning();
    if (blocked() || !m_model || paths.isEmpty()) {
        return;
    }
    QStringList added;
    QStringList failed;
    for (const QString& path : paths) {
        const QFileInfo info(path);
        const QString name = info.fileName();
        if (!info.exists() || !(info.isDir() || name.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive))) {
            failed.append(name.isEmpty() ? path : name);
            continue;
        }
        const QString target = m_model->dir().filePath(name);
        if (QFileInfo(target).absoluteFilePath() == info.absoluteFilePath()) {
            continue;  // already in the folder
        }
        for (const QString& existing : { target, target + QStringLiteral(".disabled") }) {
            if (!QFileInfo::exists(existing)) {
                continue;
            }
            // Prism would delete the old pack for good; here it goes to the Recycle Bin, after a question.
            ChoicePrompt ask;
            ask.title = tr("Replace %1?").arg(name);
            ask.note = tr("The pack that is there now goes to the Recycle Bin.");
            ask.buttons = { { tr("Cancel"), ChoiceButton::Kind::Ghost }, { tr("Replace"), ChoiceButton::Kind::Danger } };
            ask.defaultButton = 0;
            if (m_env.ask(this, ask) != 1 || !ModsUi::moveToRecycleBin(existing, m_env.trash)) {
                failed.append(name);
                break;
            }
        }
        if (failed.contains(name)) {
            continue;
        }
        if (m_model->installResource(info.absoluteFilePath())) {
            added.append(name);
        } else {
            failed.append(name);
        }
    }
    m_model->update();
    if (!added.isEmpty()) {
        toast(added.size() == 1 ? tr("Added %1").arg(added.first()) : tr("Added %1 shader packs").arg(added.size()), ZeroToast::Kind::Success);
    }
    if (!failed.isEmpty()) {
        toast(failed.size() == 1 ? tr("Couldn't add %1").arg(failed.first()) : tr("Couldn't add %1 shader packs").arg(failed.size()),
              ZeroToast::Kind::Error);
    }
    scheduleRebuild();
}

// ---- menus and the empty state ---------------------------------------------------------------------------------------------------

void ShaderPacksPage::onRowMenu(int row, const QPoint& globalPos)
{
    if (row < 0 || row >= m_listModel->rowCount()) {
        return;
    }
    QMenu menu(this);
    fillRowMenu(&menu, row);
    menu.exec(globalPos);
}

void ShaderPacksPage::fillRowMenu(QMenu* target, int row)
{
    if (!target || row < 0 || row >= m_listModel->rowCount()) {
        return;
    }
    if (!selectedRows().contains(row)) {
        selectRow(row);
    }
    const QList<int> rows = selectedRows();
    const bool many = rows.size() > 1;
    QMenu& menu = *target;
    const Row& clicked = m_listModel->rowAt(row);
    const auto& t = Theme::current();

    QList<bool> flags;
    for (const int r : rows) {
        flags.append(m_listModel->rowAt(r).enabled);
    }
    const bool turnOff = Mods::Installed::nextToggle(flags) == Mods::Installed::ToggleAction::Disable;
    const QString toggleText = many ? (turnOff ? tr("Turn off %1 packs").arg(rows.size()) : tr("Turn on %1 packs").arg(rows.size()))
                                    : (turnOff ? tr("Turn off") : tr("Turn on"));
    auto* toggle = menu.addAction(Icons::icon(QStringLiteral("power"), t.text), toggleText);
    toggle->setEnabled(!blocked());
    connect(toggle, &QAction::triggered, this, [this, rows]() { requestToggle(rows); });

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
    auto* remove = menu.addAction(Icons::icon(QStringLiteral("trash-2"), t.danger), many ? tr("Remove %1 packs").arg(rows.size()) : tr("Remove"));
    remove->setEnabled(!blocked());
    connect(remove, &QAction::triggered, this, [this, rows]() { requestRemove(rows); });
}

void ShaderPacksPage::onEmptyAction()
{
    if (m_view == View::NoMatches) {
        m_search->clear();
    } else if (m_view == View::Empty) {
        if (m_env.addShaders) {
            m_env.addShaders();
        }
        emit addShadersRequested();
    }
}

void ShaderPacksPage::openShadersFolder()
{
    if (m_model) {
        DesktopServices::openPath(m_model->dir().absolutePath(), true);
    }
}

}  // namespace ShadersUi
}  // namespace Zero
