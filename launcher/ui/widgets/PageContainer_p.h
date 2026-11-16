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

#include <QEvent>
#include <QListView>
#include "ui/zero/ZeroPageNav.h"
#include <QScrollBar>
#include <QStyledItemDelegate>

class BasePage;
const int pageIconSize = 24;

class PageViewDelegate : public QStyledItemDelegate {
   public:
    PageViewDelegate(QObject* parent) : QStyledItemDelegate(parent) {}
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(size.height(), 32));
        return size;
    }
};

class PageModel : public QAbstractListModel {
   public:
    PageModel(QObject* parent = 0) : QAbstractListModel(parent)
    {
        QPixmap empty(pageIconSize, pageIconSize);
        empty.fill(Qt::transparent);
        m_emptyIcon = QIcon(empty);
    }
    virtual ~PageModel() {}

    int rowCount(const QModelIndex& parent = QModelIndex()) const { return parent.isValid() ? 0 : m_pages.size(); }
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const
    {
        switch (role) {
            case Qt::DisplayRole:
                return m_pages.at(index.row())->displayName();
            case Zero::PageNavDelegate::kPageIdRole:  // ZERO: the side list shows a summary per page id
                return m_pages.at(index.row())->id();
            case Qt::DecorationRole: {
                QIcon icon = m_pages.at(index.row())->icon();
                if (icon.isNull())
                    icon = m_emptyIcon;
                // HACK: fixes icon stretching on windows. TODO: report Qt bug for this
                return QIcon(icon.pixmap(QSize(48, 48)));
            }
        }
        return QVariant();
    }

    void setPages(const QList<BasePage*>& pages)
    {
        beginResetModel();
        m_pages = pages;
        endResetModel();
    }
    const QList<BasePage*>& pages() const { return m_pages; }

    BasePage* findPageEntryById(QString id)
    {
        for (auto page : m_pages) {
            if (page->id() == id)
                return page;
        }
        return nullptr;
    }

    QList<BasePage*> m_pages;
    QIcon m_emptyIcon;
};

class PageView : public QListView {
   public:
    PageView(QWidget* parent = 0) : QListView(parent)
    {
        // ZERO: painted rows (icon, name, plain-language summary), fixed width, transparent
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        setItemDelegate(new Zero::PageNavDelegate(this));
        setProperty("zeroSound", "navigate");  // ZERO: choosing a page makes the navigate sound (Zero::Sound)
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setFrameShape(QFrame::NoFrame);
        setMouseTracking(true);
        viewport()->setAttribute(Qt::WA_Hover, true);
        setStyleSheet(QStringLiteral("QListView { background: transparent; border: none; } QScrollBar:vertical { width: 6px; }"));
        viewport()->setAutoFillBackground(false);
        setFixedWidth(Zero::PageNavDelegate::kWidth);
    }

    virtual QSize sizeHint() const { return QSize(Zero::PageNavDelegate::kWidth, 100); }

    virtual bool eventFilter(QObject* obj, QEvent* event)
    {
        if (obj == verticalScrollBar() && (event->type() == QEvent::Show || event->type() == QEvent::Hide))
            updateGeometry();
        return QListView::eventFilter(obj, event);
    }
};
