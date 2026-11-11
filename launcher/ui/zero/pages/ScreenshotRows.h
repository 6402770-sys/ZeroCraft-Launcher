// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the Screenshot Gallery grid: a flat model of ScreenshotEntry items and the delegate that paints the tiles
// (one QListView in icon mode, no widget per tile, so hundreds of screenshots stay cheap).
#pragma once

#include <QAbstractListModel>
#include <QStyledItemDelegate>
#include "ui/zero/ZeroTokens.h"
#include "zero/gallery/ScreenshotScanner.h"

namespace Zero {

class ThumbnailCache;

class ScreenshotModel : public QAbstractListModel {
    Q_OBJECT
   public:
    enum Role { PathRole = Qt::UserRole + 1 };

    explicit ScreenshotModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(m_rows.size()); }
    QVariant data(const QModelIndex& index, int role) const override;

    /// Replaces every item (model reset).
    void setEntries(const QList<Gallery::ScreenshotEntry>& entries);
    const QList<Gallery::ScreenshotEntry>& entries() const { return m_rows; }
    const Gallery::ScreenshotEntry& entry(int row) const { return m_rows.at(row); }
    int rowForPath(const QString& path) const;

   private:
    QList<Gallery::ScreenshotEntry> m_rows;
};

class ScreenshotDelegate : public QStyledItemDelegate {
    Q_OBJECT
   public:
    static constexpr int kMinThumbWidth = 240;  // the thumbnail never gets narrower; the grid adds columns instead
    static constexpr int kPad = Tokens::spaceSm;
    static constexpr int kGap = Tokens::spaceLg;

    /// Fluid grid: columns = as many tiles of at least kMinThumbWidth as fit, the tiles share the width evenly (first column flush
    /// left, last flush right, kGap between columns). Returns true when the geometry changed (then setGridSize(gridSize())).
    bool setViewportWidth(int width);
    /// Size of one tile (padding, 16:9 thumbnail and the two caption lines).
    QSize tileSize() const;
    /// Grid cell: the QListView needs columns * cell width <= viewport width, so the cell is the viewport width / columns (floor);
    /// the tile is shifted inside its cell by column so the last one still touches the right edge.
    QSize gridSize() const;
    int columns() const { return m_columns; }

    ScreenshotDelegate(ThumbnailCache* cache, QObject* parent = nullptr) : QStyledItemDelegate(parent), m_cache(cache) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

   private:
    ThumbnailCache* m_cache;
    int m_columns = 1;
    int m_thumbWidth = kMinThumbWidth;
    int m_cellWidth = kMinThumbWidth + 2 * kPad;
};

}  // namespace Zero
