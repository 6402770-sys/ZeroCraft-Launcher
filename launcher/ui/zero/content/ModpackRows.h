// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the Modpacks result list of the Discover page: a flat model of ModpackItems and the delegate that paints them
// as grid cards or list rows (docs/zero/CONTENT.md, "Modpacks"). One QListView in icon mode, no widget per item, so a few hundred
// loaded packs stay cheap. Pictures come from the RemoteImageCache (never decoded here); a row without artwork shows the pack icon
// on a tile.
#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QSet>
#include <QStyledItemDelegate>

#include <functional>

#include "ui/zero/ZeroTokens.h"
#include "ui/zero/content/ContentDelegate.h"
#include "zero/content/ModpackSource.h"

namespace Zero {
class RemoteImageCache;
}

namespace Zero::Content {

/// The loaded packs. `append` adds pages (de-duplicated by provider:id), `setClientFilter` hides rows the client facets reject
/// without a request. Rows that are only updated (a picture arrived, details merged, installed state) emit dataChanged, never
/// rowsInserted, so the reveal animation does not replay for them.
class ModpackListModel : public QAbstractListModel {
    Q_OBJECT
   public:
    enum Role { ItemRole = Qt::UserRole + 1, InstalledRole, KeyRole };

    explicit ModpackListModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : int(m_visible.size()); }
    QVariant data(const QModelIndex& index, int role) const override;

    /// Replaces everything (model reset; the reveal starts a new generation).
    void reset(const QList<ModpackItemPtr>& items);
    /// Appends a page. Items already loaded (same provider:id) are skipped. Returns how many rows became visible.
    int append(const QList<ModpackItemPtr>& items);
    void clear() { reset({}); }

    /// Rows the predicate rejects are not shown (the loaded list is kept). Rebuilds the visible rows with a reset.
    void setClientFilter(std::function<bool(const ModpackItem&)> filter);
    /// The installed packs (`managedKey(type, id)` of every managed instance); rows whose state changed emit dataChanged.
    void setInstalledKeys(const QSet<QString>& keys);
    const QSet<QString>& installedKeys() const { return m_installed; }

    /// One item changed (details merged): its row repaints.
    void itemChanged(const ModpackItemPtr& item);

    ModpackItemPtr item(int row) const { return row >= 0 && row < m_visible.size() ? m_visible.at(row) : ModpackItemPtr(); }
    int rowOf(const ModpackItemPtr& item) const;
    int rowOfKey(const QString& key) const;
    bool isInstalled(const ModpackItem& item) const { return m_installed.contains(installedKey(item)); }

    /// Every loaded item, including the ones the client filter hides.
    const QList<ModpackItemPtr>& loaded() const { return m_all; }
    int loadedCount() const { return int(m_all.size()); }
    int visibleCount() const { return int(m_visible.size()); }

   private:
    bool accepts(const ModpackItem& item) const { return !m_filter || m_filter(item); }

    QList<ModpackItemPtr> m_all;
    QList<ModpackItemPtr> m_visible;
    QSet<QString> m_keys;
    QSet<QString> m_installed;
    std::function<bool(const ModpackItem&)> m_filter;
};

/// Paints grid cards and list rows. Geometry is fluid: columns = as many cards of at least kCardMin as fit.
class ModpackDelegate : public ContentDelegate {
    Q_OBJECT
   public:
    using Mode = ContentDelegate::Mode;

    static constexpr int kCardMin = 280;
    static constexpr int kCardMax = 340;
    static constexpr int kSingleColumnMax = 440;
    static constexpr int kGap = Tokens::spaceLg;
    static constexpr int kPad = Tokens::spaceMd;
    static constexpr int kIcon = 48;
    static constexpr int kCardBodyHeight = 150;  ///< below the 16:9 artwork
    static constexpr int kRowHeight = 72;
    static constexpr int kRowGap = Tokens::spaceSm;

    explicit ModpackDelegate(RemoteImageCache* images, QObject* parent = nullptr);

    void setMode(Mode mode) override;
    /// Rows are drawn at half opacity while a new search is pending (the old rows stay until the answer arrives).
    void setDimmed(bool dimmed) override { m_dimmed = dimmed; }
    bool isDimmed() const override { return m_dimmed; }
    Mode mode() const override { return m_mode; }
    /// Recomputes the columns and the card size for a viewport `width` px wide; true when something changed (then call
    /// setGridSize(gridSize()) on the view).
    bool setViewportWidth(int width) override;
    int columns() const override { return m_columns; }
    /// The card (grid) or row (list) itself, without the gap.
    QSize itemSize() const;
    /// The view's cell: the card plus the gap below it (the gap to the right is made by shifting cards inside their cells).
    QSize gridSize() const override;
    /// The rectangle a card / row occupies inside its cell `cell` (the columns are spread evenly across the viewport).
    QRect itemRect(const QRect& cell) const;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    bool helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option, const QModelIndex& index) override;

    /// The images this delegate shows (a delegate asks them for the visible items only).
    RemoteImageCache* images() const { return m_images; }

   private:
    void paintCard(QPainter* painter, const QRect& rect, const ModpackItem& item, bool installed, bool selected, bool hovered) const;
    void paintRow(QPainter* painter, const QRect& rect, const ModpackItem& item, bool installed, bool selected, bool hovered) const;
    void paintArtwork(QPainter* painter, const QRect& art, const QPainterPath& clip, const ModpackItem& item) const;
    void paintIcon(QPainter* painter, const QRect& rect, const ModpackItem& item, const QColor& ring) const;

    RemoteImageCache* m_images;
    Mode m_mode = Mode::Grid;
    bool m_dimmed = false;
    int m_columns = 1;
    int m_viewportWidth = 0;
    int m_cardWidth = kCardMin;
    int m_cellWidth = kCardMin + kGap;
};

}  // namespace Zero::Content
