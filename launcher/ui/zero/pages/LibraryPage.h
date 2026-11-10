// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Library page: header with search / sort / view toggle, instance grid or list, details panel.
// MainWindow stays the controller: the grid is its InstanceView + InstanceProxyModel, the actions are its QActions.
#pragma once

#include <QModelIndex>
#include <QWidget>
#include "InstanceDetailsPanel.h"

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
class QTreeView;

namespace Zero {

class InstanceTableModel;
class ZeroEmptyState;
class ZeroPageHeader;

class LibraryPage : public QWidget {
    Q_OBJECT
   public:
    LibraryPage(InstanceView* grid, InstanceProxyModel* proxy, QWidget* parent = nullptr);

    void setActions(const LibraryActions& actions);
    /// The instance MainWindow has selected (nullptr = none). Feeds the details panel.
    void setCurrentInstance(MinecraftInstance* instance);

    /// Starts inline renaming of the current instance (on the card in grid mode, in the details panel in list mode).
    void beginRename();
    void focusSearch();
    void setSearchText(const QString& text);
    /// Re-reads the sort mode from the settings (the settings dialog may have changed it).
    void syncSortFromSettings();

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

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void setViewMode(bool list);    // crossfades when the mode changes
    void applyViewMode(bool list);  // the switch itself
    void applyFilter(const QString& text);
    void updateState();
    void syncListFromGrid();
    void onListCurrentChanged(const QModelIndex& current);
    void refreshIcons();

    InstanceView* m_grid;
    InstanceProxyModel* m_proxy;
    InstanceTableModel* m_table;
    QSortFilterProxyModel* m_sort;
    QTreeView* m_list;

    ZeroPageHeader* m_header;
    QLineEdit* m_search;
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
