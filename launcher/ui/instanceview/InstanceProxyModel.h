/* Copyright 2013-2021 MultiMC Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <QCollator>
#include <QSortFilterProxyModel>
#include <functional>

class MinecraftInstance;

class InstanceProxyModel : public QSortFilterProxyModel {
    Q_OBJECT

   public:
    InstanceProxyModel(QObject* parent = 0);

    /// ZERO: case-insensitive substring filter over name, group and Minecraft version. Empty = show everything.
    void setFilterText(const QString& text);
    QString filterText() const { return m_filterText; }

    /// ZERO: extra row predicate (the Library filter chips), combined with the text filter by AND. Groups keep working:
    /// they are derived from the rows that pass. An empty function accepts every row; setting one re-filters.
    using Predicate = std::function<bool(MinecraftInstance*)>;
    void setPredicate(Predicate predicate);
    /// Re-evaluates the predicate for every row (call when what it reads changed).
    void refilter() { invalidateFilter(); }

   protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
    bool subSortLessThan(const QModelIndex& left, const QModelIndex& right) const;

   private:
    QCollator m_naturalSort;
    QString m_filterText;
    Predicate m_predicate;
};
