// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *  Copyright (C) 2023 Tayou <git@tayou.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "VisualGroup.h"

#include <QApplication>
#include <QDebug>
#include <QModelIndex>
#include <QPainter>
#include <QtMath>
#include <utility>

#include "Application.h"

#include "InstanceList.h"
#include "InstanceView.h"
#include "icons/IconList.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace {
constexpr int kHeaderHeight = 36;
}

VisualGroup::VisualGroup(QString text, InstanceView* view) : view(view), text(std::move(text)), collapsed(false) {}

VisualGroup::VisualGroup(const VisualGroup* other) : view(other->view), text(other->text), collapsed(other->collapsed) {}

void VisualGroup::update()
{
    auto temp_items = items();
    auto itemsPerRow = view->itemsPerRow();

    int numRows = qMax(1, qCeil((qreal)temp_items.size() / (qreal)itemsPerRow));
    rows = QList<VisualRow>(numRows);

    int maxRowHeight = 0;
    int positionInRow = 0;
    int currentRow = 0;
    int offsetFromTop = 0;
    for (auto item : temp_items) {
        if (positionInRow == itemsPerRow) {
            rows[currentRow].height = maxRowHeight;
            rows[currentRow].top = offsetFromTop;
            currentRow++;
            if (currentRow >= rows.size()) {
                currentRow = rows.size() - 1;
            }
            offsetFromTop += maxRowHeight + view->spacing();
            positionInRow = 0;
            maxRowHeight = 0;
        }
        QStyleOptionViewItem viewItemOption;
        view->initViewItemOption(&viewItemOption);

        auto itemHeight = view->itemDelegate()->sizeHint(viewItemOption, item).height();
        if (itemHeight > maxRowHeight) {
            maxRowHeight = itemHeight;
        }
        rows[currentRow].items.append(item);
        positionInRow++;
    }
    rows[currentRow].height = maxRowHeight;
    rows[currentRow].top = offsetFromTop;
}

QPair<int, int> VisualGroup::positionOf(const QModelIndex& index) const
{
    int y = 0;
    for (auto& row : rows) {
        for (auto x = 0; x < row.items.size(); x++) {
            if (row.items[x] == index) {
                return qMakePair(x, y);
            }
        }
        y++;
    }
    qWarning() << "Item" << index.row() << index.data(Qt::DisplayRole).toString() << "not found in visual group" << text;
    return qMakePair(0, 0);
}

int VisualGroup::rowTopOf(const QModelIndex& index) const
{
    auto position = positionOf(index);
    return rows[position.second].top;
}

int VisualGroup::rowHeightOf(const QModelIndex& index) const
{
    auto position = positionOf(index);
    return rows[position.second].height;
}

VisualGroup::HitResults VisualGroup::hitScan(const QPoint& pos) const
{
    VisualGroup::HitResults results = VisualGroup::NoHit;
    int y_start = verticalPosition();
    int body_start = y_start + headerHeight();
    int body_end = body_start + contentHeight();
    int y = pos.y();
    // int x = pos.x();
    if (y < y_start) {
        results = VisualGroup::NoHit;
    } else if (y < body_start) {
        results = VisualGroup::HeaderHit;
        int collapseSize = headerHeight() - 4;

        // the icon
        QRect iconRect = QRect(view->m_leftMargin + 2, 2 + y_start, view->width() - 4, collapseSize);
        if (iconRect.contains(pos)) {
            results |= VisualGroup::CheckboxHit;
        }
    } else if (y < body_end) {
        results |= VisualGroup::BodyHit;
    }
    return results;
}

void VisualGroup::drawHeader(QPainter* painter, const QStyleOptionViewItem& option) const
{
    if (headerHeight() == 0)
        return;

    const auto& theme = Zero::Theme::current();
    const QString& iconKey = APPLICATION->instances()->getGroupIconKey(text);
    const bool hasIcon = !iconKey.isEmpty();

    const QRect optRect = option.rect;
    const QRect row(optRect.left(), optRect.top(), optRect.width(), kHeaderHeight - 8);
    const int centerY = row.center().y();

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    // chevron (collapsible as before)
    constexpr int chevron = 14;
    int x = row.left();
    Zero::Icons::icon(collapsed ? QStringLiteral("chevron-right") : QStringLiteral("chevron-down"), theme.textTertiary)
        .paint(painter, QRect(x, centerY - chevron / 2, chevron, chevron));
    x += chevron + 6;

    if (hasIcon) {
        constexpr int iconSize = 16;
        APPLICATION->icons()->getIcon(iconKey).paint(painter, QRect(x, centerY - iconSize / 2, iconSize, iconSize));
        x += iconSize + 6;
    }

    // overline-styled name + count
    int itemCount = 0;
    for (const auto& r : rows)
        itemCount += r.size();

    const QString title = text.isEmpty() ? QObject::tr("Ungrouped") : text;
    const QFont overline = Zero::Style::font(Zero::Role::Overline);
    painter->setFont(overline);
    painter->setPen(theme.textTertiary);
    const QFontMetrics om(overline);
    const int titleWidth = om.horizontalAdvance(title.toUpper());
    painter->drawText(QRect(x, row.top(), titleWidth + 4, row.height()), Qt::AlignLeft | Qt::AlignVCenter, title);
    x += titleWidth + 10;

    const QFont caption = Zero::Style::font(Zero::Role::Caption);
    painter->setFont(caption);
    painter->setPen(theme.textTertiary);
    const QString count = QString::number(itemCount);
    painter->drawText(QRect(x, row.top(), 40, row.height()), Qt::AlignLeft | Qt::AlignVCenter, count);
    x += QFontMetrics(caption).horizontalAdvance(count) + 12;

    // separator line to the right edge
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setPen(theme.border);
    painter->drawLine(QPoint(x, centerY), QPoint(row.right(), centerY));
    painter->restore();
}

int VisualGroup::totalHeight() const
{
    return headerHeight() + contentHeight();
}

int VisualGroup::headerHeight() const
{
    // A single unnamed group needs no header; showing "UNGROUPED 6" above everything is just noise.
    if (text.isEmpty() && view && view->groupCount() <= 1)
        return 0;
    return kHeaderHeight;
}

int VisualGroup::contentHeight() const
{
    if (collapsed) {
        return 0;
    }
    auto last = rows[numRows() - 1];
    return last.top + last.height;
}

int VisualGroup::numRows() const
{
    return (int)rows.size();
}

int VisualGroup::verticalPosition() const
{
    return m_verticalPosition;
}

QList<QModelIndex> VisualGroup::items() const
{
    QList<QModelIndex> indices;
    for (int i = 0; i < view->model()->rowCount(); ++i) {
        const QModelIndex index = view->model()->index(i, 0);
        if (index.data(InstanceViewRoles::GroupRole).toString() == text) {
            indices.append(index);
        }
    }
    return indices;
}
