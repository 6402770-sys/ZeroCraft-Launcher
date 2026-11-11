// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Library page: toolbar row (search, sort), instance grid or list, details panel. The view toggle and
// New instance live in the shell top bar (topBarActions).
// MainWindow stays the controller: the grid is its InstanceView + InstanceProxyModel, the actions are its QActions.
#pragma once

#include <QModelIndex>
#include <QWidget>
#include "InstanceDetailsPanel.h"
#include "ui/zero/shell/ShellPage.h"
#include "zero/profiles/LibraryFilter.h"

class InstanceProxyModel;
class InstanceView;
class MinecraftInstance;
class QAbstractButton;
class QAbstractItemView;
class QAction;
class QPushButton;
class QComboBox;
class QLineEdit;
class QSortFilterProxyModel;
class QStackedWidget;
class QTimer;
class QTreeView;

namespace Zero {

class FilterChipBar;
class InstanceTableModel;
class ZeroEmptyState;

class LibraryPage : public ShellPage {
    Q_OBJECT
   public:
    LibraryPage(InstanceView* grid, InstanceProxyModel* proxy, QWidget* parent = nullptr);

    QString title() const override { return tr("Library"); }
    /// "<N> instances" (live; "<shown> of <N>" while a search filters).
    QString subtitle() const override { return m_subtitle; }

    void setActions(const LibraryActions& actions);
    /// The instance MainWindow has selected (nullptr = none). Feeds the details panel.
    void setCurrentInstance(MinecraftInstance* instance);

    /// Starts inline renaming of the current instance (on the card in grid mode, in the details panel in list mode).
    void beginRename();
    void focusSearch();
    void setSearchText(const QString& text);
    /// Re-reads the sort mode from the settings (the settings dialog may have changed it).
    void syncSortFromSettings();

    /// The selected filter chip (All, Favorites, Running, a loader or a tag). Combined with the search text.
    LibraryFilter::Chip chip() const { return m_chip; }
    void setChip(const LibraryFilter::Chip& chip);
    /// Clears the search text and selects the All chip.
    void clearFilters();
    FilterChipBar* chipBar() const { return m_chips; }
    InstanceDetailsPanel* detailsPanel() const { return m_details; }

    bool isListMode() const;
    QAbstractItemView* activeView() const;
    QAbstractItemView* listView() const;

   signals:
    void newInstanceRequested();
    void importRequested();
    void instanceActivated(const QModelIndex& proxyIndex);
    /// List mode only (the grid uses its own customContextMenuRequested). The row is already current.
    void listContextMenuRequested(const QPoint& globalPos, const QModelIndex& proxyIndex);
    void renameRequested(const QString& newName);
    void changeIconRequested();
    void favoriteToggleRequested();
    void setArtworkRequested();
    void removeArtworkRequested();

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void setViewMode(bool list);    // crossfades when the mode changes
    void applyViewMode(bool list);  // the switch itself
    void applyFilters(bool chipChanged);
    void scheduleChipRefresh();
    void rebuildChips();
    void updateState();
    void syncListFromGrid();
    void onListCurrentChanged(const QModelIndex& current);
    void refreshIcons();

    InstanceView* m_grid;
    InstanceProxyModel* m_proxy;
    InstanceTableModel* m_table;
    QSortFilterProxyModel* m_sort;
    QTreeView* m_list;

    QString m_subtitle;
    QWidget* m_toolbar;  // search (left) and sort (right) row at the top of the content
    QLineEdit* m_search;
    FilterChipBar* m_chips;
    LibraryFilter::Chip m_chip;
    QTimer* m_chipTimer;
    ZeroEmptyState* m_noMatches;
    QComboBox* m_sortCombo;
    QAbstractButton* m_gridButton;
    QAbstractButton* m_listButton;
    QPushButton* m_newButton;
    QAction* m_searchAction = nullptr;

    QStackedWidget* m_body;  // 0 = collection, 1 = library empty
    QStackedWidget* m_collection;  // 0 grid, 1 list, 2 no matches
    InstanceDetailsPanel* m_details;

    QString m_rememberedId;
    bool m_syncing = false;
    bool m_filtering = false;
    bool m_listMode = false;
};

}  // namespace Zero
