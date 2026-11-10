// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - table view of the instances for the Library list mode.
#pragma once

#include <QAbstractTableModel>
#include <QPointer>

class MinecraftInstance;

namespace Zero {

/// Flattens the (filtered, sorted, grouped) instance proxy model used by the grid into rows with columns.
/// Row N of this model is row N of the source, so the grid's selection model stays the single source of truth.
class InstanceTableModel : public QAbstractTableModel {
    Q_OBJECT
   public:
    enum Column { NameColumn, VersionColumn, LoaderColumn, GroupColumn, LastPlayedColumn, PlaytimeColumn, ColumnCount };
    enum { SortRole = Qt::UserRole + 100 };

    explicit InstanceTableModel(QAbstractItemModel* source, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    /// Index in the source model (column 0) for a row of this model.
    QModelIndex sourceIndex(const QModelIndex& index) const;
    /// Index in this model (column 0) for a source index.
    QModelIndex fromSource(const QModelIndex& source) const;

   private:
    MinecraftInstance* instanceAt(int row) const;

    QPointer<QAbstractItemModel> m_source;
};

}  // namespace Zero
