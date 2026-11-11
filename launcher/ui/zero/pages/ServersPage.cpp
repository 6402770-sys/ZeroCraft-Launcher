// SPDX-License-Identifier: GPL-3.0-only
#include "ServersPage.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDeadlineTimer>
#include <QDir>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include "Application.h"
#include "InstanceList.h"
#include "ServerDetailsPanel.h"
#include "ServerRows.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/launch/MinecraftTarget.h"
#include "settings/SettingsObject.h"
#include "ui/zero/ZeroEmptyState.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroToast.h"

namespace Zero {

namespace {
QList<Servers::InstanceSource> sourcesFromApplication()
{
    QList<Servers::InstanceSource> out;
    auto* list = APPLICATION->instances();
    for (int i = 0; i < list->count(); ++i) {
        MinecraftInstance* inst = list->at(i);
        out.append({ inst->id(), inst->name(), QDir(inst->gameRoot()).filePath(QStringLiteral("servers.dat")) });
    }
    return out;
}

bool isBusy(const QString& instanceId)
{
    auto* inst = APPLICATION->instances()->getInstanceById(instanceId);
    return inst && (inst->isRunning() || APPLICATION->launchPhase(instanceId) != Application::LaunchPhase::Idle);
}
}  // namespace

ServersPage::ServersPage(QWidget* parent)
    : ShellPage(parent)
    , m_scanner(new Servers::ServerVaultScanner(&sourcesFromApplication, this))
    , m_pinger(new Servers::ServerPinger(this))
    , m_model(new ServerRowModel(this))
{
    m_model->setPinger(m_pinger);

    // ----- toolbar (content): search left, instance filter and sort right; the status note underneath -----
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search servers"));
    m_search->setFixedWidth(280);
    m_search->setClearButtonEnabled(true);
    m_search->installEventFilter(this);

    m_instanceCombo = new QComboBox(this);
    m_instanceCombo->setToolTip(tr("Filter by instance"));
    m_instanceCombo->addItem(tr("All instances"), QString());

    m_sortCombo = new QComboBox(this);
    m_sortCombo->setToolTip(tr("Sort servers"));
    m_sortCombo->addItem(tr("Name"), Servers::sortModeToString(Servers::SortMode::Name));
    m_sortCombo->addItem(tr("Players"), Servers::sortModeToString(Servers::SortMode::Players));
    m_sortCombo->addItem(tr("Latency"), Servers::sortModeToString(Servers::SortMode::Latency));

    m_notice = new QLabel(this);
    m_notice->setTextFormat(Qt::PlainText);
    m_notice->setWordWrap(true);
    Style::applyRole(m_notice, Role::Caption);

    m_toolbar = new QWidget(this);
    auto* toolbarColumn = new QVBoxLayout(m_toolbar);
    toolbarColumn->setContentsMargins(0, 0, 0, Tokens::spaceLg);
    toolbarColumn->setSpacing(Tokens::spaceSm);
    auto* toolbarRow = new QHBoxLayout;
    toolbarRow->setContentsMargins(0, 0, 0, 0);
    toolbarRow->setSpacing(Tokens::spaceLg);
    toolbarRow->addWidget(m_search);
    toolbarRow->addStretch(1);
    toolbarRow->addWidget(m_instanceCombo);
    toolbarRow->addWidget(m_sortCombo);
    toolbarColumn->addLayout(toolbarRow);
    toolbarColumn->addWidget(m_notice);

    // ----- top bar action: Refresh -----
    m_refresh = new QPushButton(tr("Refresh"), this);
    Style::setProperty(m_refresh, "zeroVariant", "ghost");
    m_refresh->setIconSize(QSize(16, 16));
    m_refresh->setToolTip(tr("Rescan the servers and ping them again"));
    connect(m_refresh, &QPushButton::clicked, this, &ServersPage::refresh);
    auto* actions = new QWidget(this);
    auto* actionsRow = new QHBoxLayout(actions);
    actionsRow->setContentsMargins(0, 0, 0, 0);
    actionsRow->setSpacing(Tokens::spaceSm);
    actionsRow->addWidget(m_refresh);
    setTopBarActions(actions);

    // ----- list -----
    m_list = new QListView(this);
    m_list->setModel(m_model);
    m_list->setItemDelegate(new ServerRowDelegate(m_list));
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
        m_selectedAddress = selected.isEmpty() ? QString() : selected.first().data(ServerRowModel::AddressRole).toString();
        syncDetails();
    });

    m_noMatches = new ZeroEmptyState(QStringLiteral("compass"), tr("No servers match"), tr("Try a different search or clear the filters."),
                                     tr("Clear filters"), this);
    connect(m_noMatches, &ZeroEmptyState::actionTriggered, this, &ServersPage::clearFilters);

    m_collection = new QStackedWidget(this);
    m_collection->addWidget(m_list);
    m_collection->addWidget(m_noMatches);

    m_details = new ServerDetailsPanel(this);
    connect(m_details, &ServerDetailsPanel::showInstanceRequested, this, &ServersPage::showInstanceRequested);
    connect(m_details, &ServerDetailsPanel::playRequested, this, &ServersPage::playOn);

    auto* collectionRow = new QWidget(this);
    auto* row = new QHBoxLayout(collectionRow);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(Tokens::spaceLg);
    row->addWidget(m_collection, 1);
    row->addWidget(m_details);

    auto* none = new ZeroEmptyState(QStringLiteral("compass"), tr("No servers yet"),
                                    tr("Servers you add in Minecraft's Multiplayer menu show up here from every instance."), {}, this);
    auto* scanning = new ZeroEmptyState(QStringLiteral("compass"), tr("Looking for servers…"), {}, {}, this);

    m_body = new QStackedWidget(this);
    m_body->addWidget(collectionRow);
    m_body->addWidget(none);
    m_body->addWidget(scanning);

    auto* page = new QVBoxLayout(this);
    page->setContentsMargins(Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl);
    page->setSpacing(0);
    page->addWidget(m_toolbar);
    page->addWidget(m_body, 1);

    // ----- behavior -----
    connect(m_search, &QLineEdit::textChanged, this, [this]() { applyFilters(); });
    auto* findShortcut = new QShortcut(QKeySequence(QKeySequence::Find), this);
    connect(findShortcut, &QShortcut::activated, this, &ServersPage::focusSearch);

    m_instanceFilter = APPLICATION->settings()->get("ZeroServersInstance").toString();
    m_sort = Servers::sortModeFromString(APPLICATION->settings()->get("ZeroServersSort").toString());
    m_sortCombo->setCurrentIndex(qMax(0, m_sortCombo->findData(Servers::sortModeToString(m_sort))));
    connect(m_instanceCombo, &QComboBox::activated, this, [this](int i) {
        m_instanceFilter = m_instanceCombo->itemData(i).toString();
        APPLICATION->settings()->set("ZeroServersInstance", m_instanceFilter);
        applyFilters();
    });
    connect(m_sortCombo, &QComboBox::activated, this, [this](int i) {
        m_sort = Servers::sortModeFromString(m_sortCombo->itemData(i).toString());
        APPLICATION->settings()->set("ZeroServersSort", Servers::sortModeToString(m_sort));
        applyFilters();
    });

    connect(m_scanner, &Servers::ServerVaultScanner::listChanged, this, &ServersPage::onListChanged);
    connect(m_scanner, &Servers::ServerVaultScanner::scanningChanged, this, [this](bool scanning) { m_refresh->setEnabled(!scanning); });
    connect(m_pinger, &Servers::ServerPinger::result, this, [this](const QString& address) {
        m_model->statusChanged(address);
        if (address == m_selectedAddress)
            syncDetails();
    });
    connect(m_pinger, &Servers::ServerPinger::settled, this, [this]() {
        if (m_sort != Servers::SortMode::Name)
            applyFilters();  // the results are complete now: apply the order once instead of reordering on every ping
    });

    // instances added / removed / reset: rescan (debounced); running state and names: cheap in-place updates
    m_rescanTimer = new QTimer(this);
    m_rescanTimer->setSingleShot(true);
    m_rescanTimer->setInterval(kInstanceDebounceMs);
    connect(m_rescanTimer, &QTimer::timeout, this, [this]() {
        if (isVisible()) {
            m_scanRequested = true;
            m_stale = false;
            m_sinceScan.start();
            m_scanner->rescan();  // not a Refresh: results already known are kept, new addresses get pinged
        } else {
            m_stale = true;
        }
    });
    auto* instances = APPLICATION->instances();
    connect(instances, &QAbstractItemModel::rowsInserted, this, &ServersPage::scheduleRescan);
    connect(instances, &QAbstractItemModel::rowsRemoved, this, &ServersPage::scheduleRescan);
    connect(instances, &QAbstractItemModel::modelReset, this, &ServersPage::scheduleRescan);
    connect(instances, &QAbstractItemModel::dataChanged, this, &ServersPage::syncBusy);
    connect(APPLICATION, &Application::launchPhaseChanged, this, &ServersPage::syncBusy);
    connect(APPLICATION, &Application::launchEnded, this, &ServersPage::syncBusy);

    connect(&Theme::instance(), &Theme::changed, this, &ServersPage::refreshIcons);
    refreshIcons();
    syncBusy();
    updateState();
}

void ServersPage::refreshIcons()
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

void ServersPage::focusSearch()
{
    m_search->setFocus(Qt::ShortcutFocusReason);
    m_search->selectAll();
}

void ServersPage::setSearchText(const QString& text)
{
    m_search->setText(text);
}

void ServersPage::selectServer(const QString& address)
{
    const Servers::HostPort hp = Servers::parseAddress(address);
    m_selectedAddress = hp.valid ? hp.toString() : address.trimmed();
    applyFilters();
}

QStringList ServersPage::allAddresses() const
{
    QStringList out;
    for (const auto& s : m_scanner->servers())
        out << s.address;
    return out;
}

void ServersPage::refresh()
{
    m_scanRequested = true;
    m_stale = false;
    m_sinceScan.start();
    m_pinger->refresh(allAddresses());  // the rows go back to "Pinging…" right away
    m_model->allStatusChanged();
    syncDetails();
    m_scanner->rescan();                // addresses that appeared in the meantime are pinged when the new list arrives
}

void ServersPage::scheduleRescan()
{
    if (m_scanRequested)  // never scanned (page not opened yet): the first show scans anyway
        m_rescanTimer->start();
}

void ServersPage::callWhenIdle(const std::function<void()>& done, int maxMs)
{
    auto* timer = new QTimer(this);
    const QDeadlineTimer deadline(maxMs);
    connect(timer, &QTimer::timeout, this, [this, timer, deadline, done]() {
        if ((m_scanner->hasScanned() && !m_scanner->isScanning() && m_pinger->pendingCount() == 0) || deadline.hasExpired()) {
            timer->deleteLater();
            done();
        }
    });
    timer->start(50);
}

void ServersPage::showEvent(QShowEvent* event)
{
    ShellPage::showEvent(event);
    if (!m_scanRequested || m_stale || m_sinceScan.hasExpired(kStaleMs)) {
        m_scanRequested = true;
        m_stale = false;
        m_sinceScan.start();
        m_scanner->rescan();  // pings start when the list arrives, once per session (see onListChanged)
    }
}

void ServersPage::onListChanged()
{
    ServerIcon::clearCache();
    if (!m_selectedAddress.isEmpty()) {
        bool still = false;
        for (const auto& s : m_scanner->servers()) {
            if (s.address == m_selectedAddress) {
                still = true;
                break;
            }
        }
        if (!still)
            m_selectedAddress.clear();  // the server vanished: nothing is selected
    }
    rebuildInstanceCombo();
    applyFilters();
    m_pinger->pingAll(allAddresses());  // only the addresses without a result: a rescan alone never pings again
    m_model->allStatusChanged();
    syncDetails();
}

void ServersPage::rebuildInstanceCombo()
{
    QHash<QString, QString> names;  // id -> name, only instances that have servers
    for (const auto& s : m_scanner->servers()) {
        for (const auto& ref : s.instances)
            names.insert(ref.id, ref.name);
    }
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

QString ServersPage::effectiveInstanceFilter() const
{
    return !m_instanceFilter.isEmpty() && m_instanceCombo->findData(m_instanceFilter) >= 0 ? m_instanceFilter : QString();
}

void ServersPage::applyFilters()
{
    const QString instance = effectiveInstanceFilter();
    const QString text = m_search->text();
    QList<Servers::VaultServer> rows;
    for (const auto& s : m_scanner->servers()) {
        if (!instance.isEmpty() && !s.inInstance(instance))
            continue;
        if (!Servers::matchesSearch(s, text))
            continue;
        rows.append(s);
    }
    Servers::sortServers(rows, m_sort, m_pinger->results());

    // The selected server comes back when the filter shows it again; while it is hidden nothing is highlighted.
    const int scroll = m_list->verticalScrollBar()->value();
    m_applying = true;
    m_model->setServers(rows);
    const int row = m_selectedAddress.isEmpty() ? -1 : m_model->rowForAddress(m_selectedAddress);
    if (row >= 0) {
        const QModelIndex index = m_model->index(row);
        m_list->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
    }
    m_list->verticalScrollBar()->setValue(scroll);
    m_applying = false;

    syncDetails();
    updateState();
}

void ServersPage::syncDetails()
{
    const int row = m_selectedAddress.isEmpty() ? -1 : m_model->rowForAddress(m_selectedAddress);
    if (row < 0) {
        m_details->clear();
        return;
    }
    const Servers::VaultServer& server = m_model->server(row);
    const auto it = m_pinger->results().constFind(server.address);
    const Servers::PingResult* result = it != m_pinger->results().constEnd() ? &it.value() : nullptr;
    m_details->setServer(server, result, m_pinger->isPending(server.address));
}

void ServersPage::updateState()
{
    const int total = static_cast<int>(m_scanner->servers().size());
    const int shown = m_model->rowCount();
    const bool filtering = !m_search->text().trimmed().isEmpty() || !effectiveInstanceFilter().isEmpty();

    QString subtitle;
    if (!m_scanner->hasScanned()) {
        subtitle = tr("Scanning…");
    } else if (total == 0) {
        subtitle = tr("No servers");
    } else if (filtering) {
        subtitle = total == 1 ? tr("%1 of 1 server").arg(shown) : tr("%1 of %2 servers").arg(shown).arg(total);
    } else {
        const int m = m_scanner->instanceCount();
        subtitle = tr("%1 from %2").arg(total == 1 ? tr("1 server") : tr("%1 servers").arg(total),
                                        m == 1 ? tr("1 instance") : tr("%1 instances").arg(m));
    }
    if (subtitle != m_subtitle) {
        m_subtitle = subtitle;
        emit chromeChanged();
    }

    QString notice = serverStatusDisclaimer();
    if (const qsizetype broken = m_scanner->errors().size(); broken > 0) {
        QStringList names;
        for (const auto& e : m_scanner->errors())
            names << e.instanceName;
        notice += QLatin1Char(' ') + (broken == 1 ? tr("The servers.dat of %1 could not be read.").arg(names.first())
                                                   : tr("The servers.dat of %1 instances could not be read: %2.").arg(broken).arg(names.join(QStringLiteral(", "))));
    }
    m_notice->setText(notice);

    const bool scanned = m_scanner->hasScanned();
    const bool empty = scanned && total == 0;
    m_toolbar->setVisible(scanned && !empty);
    m_body->setCurrentIndex(!scanned ? 2 : (empty ? 1 : 0));
    m_collection->setCurrentIndex(scanned && total > 0 && shown == 0 ? 1 : 0);
}

void ServersPage::syncBusy()
{
    QSet<QString> busy;
    for (const auto& s : m_scanner->servers()) {
        for (const auto& ref : s.instances) {
            if (isBusy(ref.id))
                busy.insert(ref.id);
        }
    }
    m_details->setBusyInstances(busy);
}

void ServersPage::playOn(const QString& address, const QString& instanceId)
{
    auto* instance = APPLICATION->instances()->getInstanceById(instanceId);
    if (!instance) {
        ZeroToast::show(window(), tr("That instance no longer exists"), ZeroToast::Kind::Warning);
        return;
    }
    if (isBusy(instanceId)) {
        ZeroToast::show(window(), tr("%1 is already running").arg(instance->name()), ZeroToast::Kind::Warning);
        return;
    }
    // Exactly what the per-instance Servers page does for "Join".
    APPLICATION->launch(instance, LaunchMode::Normal, std::make_shared<MinecraftTarget>(MinecraftTarget::parse(address, false)));
}

void ServersPage::clearFilters()
{
    {
        const QSignalBlocker blocker(m_search);
        m_search->clear();
    }
    m_instanceFilter.clear();
    APPLICATION->settings()->set("ZeroServersInstance", QString());
    {
        const QSignalBlocker blocker(m_instanceCombo);
        m_instanceCombo->setCurrentIndex(0);
    }
    applyFilters();
    m_list->setFocus();
}

bool ServersPage::eventFilter(QObject* watched, QEvent* event)
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
