// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Server Vault page: every multiplayer server saved in any instance's servers.dat, grouped by address, with a
// status ping and a details panel. The page never writes servers.dat (adding, editing and removing servers stays in the
// per-instance Servers page); the only side effect it has is launching an instance with a quick join ("Play on server").
// A ping response only means the server answered a status request: it is never presented as "available".
#pragma once

#include <QElapsedTimer>
#include <QSet>
#include <QString>
#include <functional>
#include "ui/zero/shell/ShellPage.h"
#include "zero/servers/ServerPinger.h"
#include "zero/servers/ServerVault.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QStackedWidget;
class QTimer;
class QAction;

namespace Zero {

class ServerDetailsPanel;
class ServerRowModel;
class ZeroEmptyState;

class ServersPage : public ShellPage {
    Q_OBJECT
   public:
    /// A rescan on show is skipped when the last scan is younger than this.
    static constexpr int kStaleMs = 30000;
    static constexpr int kInstanceDebounceMs = 500;

    explicit ServersPage(QWidget* parent = nullptr);

    QString title() const override { return tr("Servers"); }
    /// "N servers from M instances", "Scanning…" during the first scan, "<shown> of N servers" while filtering.
    QString subtitle() const override { return m_subtitle; }

    void focusSearch();
    void setSearchText(const QString& text);
    /// Selects the server with this address (any spelling; it is normalized); applied as soon as the server is listed.
    void selectServer(const QString& address);
    /// Rescans the servers.dat files and pings every server again (the Refresh button).
    void refresh();
    /// Runs `done` once the first scan and every ping have finished, or after `maxMs` at the latest.
    void callWhenIdle(const std::function<void()>& done, int maxMs);

    Servers::ServerVaultScanner* scanner() const { return m_scanner; }
    Servers::ServerPinger* pinger() const { return m_pinger; }
    ServerDetailsPanel* detailsPanel() const { return m_details; }
    QListView* listView() const { return m_list; }
    ServerRowModel* rowModel() const { return m_model; }

   signals:
    /// "Show instance in Library": MainWindow selects the instance and switches pages.
    void showInstanceRequested(const QString& instanceId);

   protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void onListChanged();
    void rebuildInstanceCombo();
    QString effectiveInstanceFilter() const;
    void applyFilters();
    void updateState();
    void syncDetails();
    void syncBusy();
    void clearFilters();
    void refreshIcons();
    void scheduleRescan();
    QStringList allAddresses() const;
    void playOn(const QString& address, const QString& instanceId);

    Servers::ServerVaultScanner* m_scanner;
    Servers::ServerPinger* m_pinger;
    ServerRowModel* m_model;
    QString m_subtitle;

    QWidget* m_toolbar;
    QLineEdit* m_search;
    QAction* m_searchAction = nullptr;
    QComboBox* m_instanceCombo;
    QComboBox* m_sortCombo;
    QLabel* m_notice;
    QPushButton* m_refresh;

    QStackedWidget* m_body;        // 0 = list + details, 1 = no servers at all, 2 = scanning
    QStackedWidget* m_collection;  // 0 = list, 1 = no matches
    QListView* m_list;
    ZeroEmptyState* m_noMatches;
    ServerDetailsPanel* m_details;

    QTimer* m_rescanTimer;
    QElapsedTimer m_sinceScan;
    bool m_scanRequested = false;
    bool m_stale = false;
    bool m_applying = false;
    QString m_selectedAddress;
    QString m_instanceFilter;  // instance id, empty = all (kept even while that instance has no servers)
    Servers::SortMode m_sort = Servers::SortMode::Name;
};

}  // namespace Zero
