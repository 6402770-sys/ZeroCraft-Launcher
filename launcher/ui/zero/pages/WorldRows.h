// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the Worlds list: a flat model of WorldEntry rows and the delegate that paints them (one QListView, no per-row
// widgets, so hundreds of worlds stay cheap).
#pragma once

#include <QAbstractListModel>
#include <QSet>
#include <QStyledItemDelegate>
#include "zero/worlds/WorldScanner.h"

namespace Zero {

class WorldRowModel : public QAbstractListModel {
    Q_OBJECT
   public:
    enum Role { KeyRole = Qt::UserRole + 1 };

    explicit WorldRowModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(m_rows.size()); }
    QVariant data(const QModelIndex& index, int role) const override;

    /// Replaces every row (model reset).
    void setEntries(const QList<Worlds::WorldEntry>& entries);
    const Worlds::WorldEntry& entry(int row) const { return m_rows.at(row); }
    const QList<Worlds::WorldEntry>& entries() const { return m_rows; }
    int rowForKey(const QString& key) const;

    /// A size arrived: updates the row in place (no reordering).
    void setSize(const QString& key, qint64 bytes);
    /// Instances that are running right now (rows of those instances get the "In use" badge).
    void setRunning(const QSet<QString>& instanceIds);
    bool isRunning(const QString& instanceId) const { return m_running.contains(instanceId); }

   private:
    QList<Worlds::WorldEntry> m_rows;
    QSet<QString> m_running;
};

class WorldRowDelegate : public QStyledItemDelegate {
    Q_OBJECT
   public:
    static constexpr int kRowHeight = 64;
    static constexpr int kRowGap = 2;
    static constexpr int kIconSize = 40;

    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    /// The favorite star at the right end of the row (click toggles it without selecting the row).
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index) override;
};

}  // namespace Zero
