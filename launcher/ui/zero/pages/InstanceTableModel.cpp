// SPDX-License-Identifier: GPL-3.0-only
#include "InstanceTableModel.h"

#include <QIcon>
#include "BaseInstance.h"
#include "InstanceList.h"
#include "InstanceInfo.h"
#include "minecraft/MinecraftInstance.h"
#include "ui/instanceview/InstanceView.h"
#include "ui/zero/ZeroFormat.h"

namespace Zero {

InstanceTableModel::InstanceTableModel(QAbstractItemModel* source, QObject* parent) : QAbstractTableModel(parent), m_source(source)
{
    auto reset = [this]() {
        beginResetModel();
        endResetModel();
    };
    connect(source, &QAbstractItemModel::modelReset, this, reset);
    connect(source, &QAbstractItemModel::layoutChanged, this, reset);
    connect(source, &QAbstractItemModel::rowsInserted, this, reset);
    connect(source, &QAbstractItemModel::rowsRemoved, this, reset);
    connect(source, &QAbstractItemModel::rowsMoved, this, reset);
    connect(source, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex& topLeft, const QModelIndex& bottomRight) {
        emit dataChanged(index(topLeft.row(), 0), index(bottomRight.row(), ColumnCount - 1));
    });
}

int InstanceTableModel::rowCount(const QModelIndex& parent) const
{
    return (parent.isValid() || !m_source) ? 0 : m_source->rowCount();
}

int InstanceTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QModelIndex InstanceTableModel::sourceIndex(const QModelIndex& idx) const
{
    return (idx.isValid() && m_source) ? m_source->index(idx.row(), 0) : QModelIndex();
}

QModelIndex InstanceTableModel::fromSource(const QModelIndex& source) const
{
    return source.isValid() ? index(source.row(), 0) : QModelIndex();
}

MinecraftInstance* InstanceTableModel::instanceAt(int row) const
{
    if (!m_source || row < 0 || row >= m_source->rowCount())
        return nullptr;
    return static_cast<MinecraftInstance*>(m_source->data(m_source->index(row, 0), InstanceList::InstancePointerRole).value<void*>());
}

QVariant InstanceTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || !m_source)
        return {};
    const QModelIndex src = m_source->index(index.row(), 0);

    if (role == InstanceList::InstanceIDRole)
        return m_source->data(src, InstanceList::InstanceIDRole);

    auto* inst = instanceAt(index.row());
    if (!inst)
        return {};

    const bool sort = role == SortRole;
    if (role != Qt::DisplayRole && role != Qt::DecorationRole && role != Qt::ToolTipRole && !sort)
        return {};

    switch (index.column()) {
        case NameColumn:
            if (role == Qt::DecorationRole)
                return m_source->data(src, Qt::DecorationRole);
            if (role == Qt::ToolTipRole)
                return inst->instanceRoot();
            return inst->name();
        case VersionColumn:
            return role == Qt::DecorationRole ? QVariant() : QVariant(InstanceInfo::of(inst).minecraftVersion);
        case LoaderColumn: {
            if (role == Qt::DecorationRole)
                return {};
            const QString loader = InstanceInfo::of(inst).loader;
            return loader.isEmpty() ? (sort ? QString() : tr("Vanilla")) : loader;
        }
        case GroupColumn:
            return role == Qt::DecorationRole ? QVariant() : m_source->data(src, InstanceViewRoles::GroupRole);
        case LastPlayedColumn:
            if (role == Qt::DecorationRole)
                return {};
            return sort ? QVariant(inst->lastLaunch()) : QVariant(Format::relativeTime(inst->lastLaunch()));
        case PlaytimeColumn:
            if (role == Qt::DecorationRole)
                return {};
            return sort ? QVariant(qint64(inst->totalTimePlayed())) : QVariant(Format::playtime(inst->totalTimePlayed()));
        default:
            return {};
    }
}

QVariant InstanceTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
        case NameColumn:
            return tr("Name");
        case VersionColumn:
            return tr("Version");
        case LoaderColumn:
            return tr("Loader");
        case GroupColumn:
            return tr("Group");
        case LastPlayedColumn:
            return tr("Last played");
        case PlaytimeColumn:
            return tr("Playtime");
        default:
            return {};
    }
}

}  // namespace Zero
