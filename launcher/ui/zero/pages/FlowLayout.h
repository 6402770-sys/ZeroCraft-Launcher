// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - a layout that wraps its items into rows (tag chips in the details panel).
#pragma once

#include <QLayout>
#include <QList>
#include <QStyle>
#include <QWidget>
#include <algorithm>

namespace Zero {

class FlowLayout : public QLayout {
   public:
    explicit FlowLayout(QWidget* parent = nullptr, int hSpacing = 6, int vSpacing = 6) : QLayout(parent), m_h(hSpacing), m_v(vSpacing)
    {
        setContentsMargins(0, 0, 0, 0);
    }
    ~FlowLayout() override
    {
        while (QLayoutItem* item = takeAt(0))
            delete item;
    }

    void addItem(QLayoutItem* item) override
    {
        m_items.append(item);
        invalidate();
    }
    int count() const override { return int(m_items.size()); }
    QLayoutItem* itemAt(int index) const override { return m_items.value(index, nullptr); }
    QLayoutItem* takeAt(int index) override
    {
        if (index < 0 || index >= m_items.size())
            return nullptr;
        invalidate();
        return m_items.takeAt(index);
    }

    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return doLayout(QRect(0, 0, width, 0), true); }
    void setGeometry(const QRect& rect) override
    {
        QLayout::setGeometry(rect);
        doLayout(rect, false);
    }
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override
    {
        QSize size;
        for (const QLayoutItem* item : m_items)
            size = size.expandedTo(item->minimumSize());
        const QMargins m = contentsMargins();
        return size + QSize(m.left() + m.right(), m.top() + m.bottom());
    }

   private:
    int doLayout(const QRect& rect, bool testOnly) const
    {
        const QMargins m = contentsMargins();
        const QRect effective = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
        int x = effective.x();
        int y = effective.y();
        int rowHeight = 0;
        for (QLayoutItem* item : m_items) {
            if (item->isEmpty())
                continue;
            const QSize hint = item->sizeHint();
            int w = std::min(hint.width(), effective.width());
            if (x + w > effective.right() + 1 && rowHeight > 0) {
                x = effective.x();
                y += rowHeight + m_v;
                rowHeight = 0;
            }
            if (!testOnly)
                item->setGeometry(QRect(QPoint(x, y), QSize(w, hint.height())));
            x += w + m_h;
            rowHeight = std::max(rowHeight, hint.height());
        }
        return y + rowHeight - rect.y() + m.bottom();
    }

    QList<QLayoutItem*> m_items;
    int m_h;
    int m_v;
};

}  // namespace Zero
