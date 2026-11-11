// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Worlds hub: every singleplayer world of every instance in one searchable list, with a details panel. The page
// itself only reads worlds (through Zero::Worlds::WorldScanner); the only things that ever write inside a world are the Smart
// Backup jobs (the application-wide Zero::Backup::WorldBackupJobs), started from the details panel.
#pragma once

#include <QElapsedTimer>
#include <QString>
#include <functional>
#include "ui/zero/shell/ShellPage.h"
#include "zero/worlds/WorldScanner.h"

class QComboBox;
class QLineEdit;
class QListView;
class QPushButton;
class QStackedWidget;
class QTimer;
class QAction;

namespace Zero {

namespace Backup {
class WorldBackupJobs;
}

class WorldDetailsPanel;
class WorldRowModel;
class ZeroEmptyState;

class WorldsPage : public ShellPage {
    Q_OBJECT
   public:
    /// A rescan on show is skipped when the last scan is younger than this.
    static constexpr int kStaleMs = 30000;
    static constexpr int kInstanceDebounceMs = 500;

    explicit WorldsPage(QWidget* parent = nullptr);

    QString title() const override { return tr("Worlds"); }
    /// "N worlds in M instances", "Scanning…" during the first scan, "<shown> of N worlds" while filtering.
    QString subtitle() const override { return m_subtitle; }

    void focusSearch();
    void setSearchText(const QString& text);
    /// Selects the world with this key (`<instanceId>/<folderName>`); applied as soon as the world is listed.
    void selectWorld(const QString& key);
    /// Starts a scan now.
    void rescan();
    /// Runs `done` once the first scan (including the sizes) has finished, or after `maxMs` at the latest.
    void callWhenIdle(const std::function<void()>& done, int maxMs);

    Worlds::WorldScanner* scanner() const { return m_scanner; }
    Backup::WorldBackupJobs* backupJobs() const { return m_backupJobs; }
    WorldDetailsPanel* detailsPanel() const { return m_details; }
    QListView* listView() const { return m_list; }
    WorldRowModel* rowModel() const { return m_model; }

   signals:
    /// "Show instance in Library": MainWindow selects the instance and switches pages.
    void showInstanceRequested(const QString& instanceId);

   protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void onWorldsChanged();
    void rebuildInstanceCombo();
    QString effectiveInstanceFilter() const;
    void applyFilters();
    void updateState();
    void syncDetails();
    void syncRunning();
    void clearFilters();
    void refreshIcons();
    void scheduleRescan();

    Worlds::WorldScanner* m_scanner;
    Backup::WorldBackupJobs* m_backupJobs;
    WorldRowModel* m_model;
    QString m_subtitle;

    QWidget* m_toolbar;
    QLineEdit* m_search;
    QAction* m_searchAction = nullptr;
    QComboBox* m_instanceCombo;
    QComboBox* m_sortCombo;
    QPushButton* m_refresh;

    QStackedWidget* m_body;        // 0 = list + details, 1 = no worlds at all
    QStackedWidget* m_collection;  // 0 = list, 1 = no matches
    QListView* m_list;
    ZeroEmptyState* m_noMatches;
    WorldDetailsPanel* m_details;

    QTimer* m_rescanTimer;
    QElapsedTimer m_sinceScan;
    bool m_scanRequested = false;
    bool m_stale = false;
    bool m_applying = false;
    QString m_selectedKey;
    QString m_instanceFilter;  // instance id, empty = all (kept even while that instance has no worlds)
    Worlds::SortMode m_sort = Worlds::SortMode::Recent;
};

}  // namespace Zero
