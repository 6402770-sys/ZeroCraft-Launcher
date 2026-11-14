// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the list of the instance window's Resource packs page (docs/zero/CONTENT-PACKS.md, "D2 Installed resource packs"): the
// model (group headers, pack rows and the fixed "Default resources" row, as copies of the facts), the delegate that paints the 64 px rows
// (switch, drag handle, pack.png, name + description, pills, the `...` button) and the view that turns a click on the switch or the button,
// a drag of a handle and the keys into signals. Nothing here changes a pack or options.txt.
#pragma once

#include <QAbstractListModel>
#include <QListView>
#include <QPixmap>
#include <QStyledItemDelegate>
#include <QTimer>

#include <functional>

#include "zero/content/InstalledPacks.h"

namespace Zero {
namespace PacksUi {

class PackListModel : public QAbstractListModel {
    Q_OBJECT
   public:
    using Kind = Content::Packs::ListEntry::Kind;
    using Row = Content::Packs::PackRow;
    enum Roles { TokenRole = Qt::UserRole + 1, ActiveRole, KindRole, OnIndexRole };

    struct Entry {
        Kind kind = Kind::Pack;
        Row row;               ///< Pack
        QString text;          ///< Header / Default
        int onIndex = -1;      ///< Pack in the ON group: its place (0 = top)
        quintptr token = 0;    ///< identifies the Prism pack of a row (its address: compared, never dereferenced)
        int sourceRow = -1;    ///< Pack: the row in the Prism model
    };

    explicit PackListModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    /// Replaces the lines. When only the facts changed (the same tokens in the same order) they are updated in place (no reset: the scroll
    /// position and the selection stay). Returns true when the list was reset.
    bool setEntries(const QList<Entry>& entries);

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : int(m_entries.size()); }
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    const Entry& entryAt(int row) const { return m_entries.at(row); }
    Kind kindAt(int row) const { return m_entries.at(row).kind; }
    bool isPack(int row) const { return row >= 0 && row < m_entries.size() && m_entries.at(row).kind == Kind::Pack; }
    int rowOfFile(const QString& fileName) const;
    /// The list rows of the ON group's packs, top first (the drag works on these).
    QList<int> onRows() const;

   private:
    QList<Entry> m_entries;
};

class PackDelegate : public QStyledItemDelegate {
    Q_OBJECT
   public:
    static constexpr int kRowHeight = 64;
    static constexpr int kRowGap = 8;
    static constexpr int kHeaderHeight = 32;
    static constexpr int kPad = 12;
    static constexpr int kIcon = 48;
    static constexpr int kGrip = 16;
    static constexpr int kPillHeight = 20;
    static constexpr int kPillGap = 6;
    static constexpr int kSwitchWidth = 36;
    static constexpr int kSwitchHeight = 20;
    static constexpr int kMenuSize = 28;
    static constexpr int kUnhideWidth = 76;

    explicit PackDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    /// The pack.png of the row with that model id, scaled to `px` logical pixels; null when there is none (yet): an `images` tile shows.
    using IconProvider = std::function<QPixmap(const QString& id, int px, qreal dpr)>;
    void setIconProvider(IconProvider provider) { m_icons = std::move(provider); }
    /// `ordered` false (Minecraft 1.12 and older, an options.txt that cannot be read): no switch and no handle.
    void setOrdered(bool ordered) { m_ordered = ordered; }
    bool ordered() const { return m_ordered; }
    /// While the game runs the switches and handles are dimmed and do nothing; `reason` is their tooltip (empty: "Close the game to change packs").
    void setLocked(bool locked, const QString& reason = QString())
    {
        m_locked = locked;
        m_lockReason = reason;
    }
    bool locked() const { return m_locked; }
    /// The list row being dragged (painted dimmed); -1 = none.
    void setDragRow(int row) { m_dragRow = row; }

    /// The painted card of a cell (the cell minus the gap below); headers fill their cell.
    static QRect cardRect(const QRect& cell) { return QRect(cell.left(), cell.top(), cell.width(), kRowHeight); }
    static QRect switchRect(const QRect& card);
    static QRect gripRect(const QRect& card);
    static QRect menuRect(const QRect& card);
    static QRect unhideRect(const QRect& card);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    bool helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option, const QModelIndex& index) override;

   private:
    void paintIcon(QPainter* painter, const QRect& rect, const PackListModel::Entry& entry) const;

    IconProvider m_icons;
    bool m_ordered = true;
    bool m_locked = false;
    QString m_lockReason;
    int m_dragRow = -1;
};

class PackListView : public QListView {
    Q_OBJECT
   public:
    explicit PackListView(QWidget* parent = nullptr);
    PackDelegate* rowDelegate() const { return m_delegate; }
    PackListModel* packModel() const { return qobject_cast<PackListModel*>(model()); }

    /// Where a row dragged to viewport height `y` would land: the place among the ON group's packs (0 = top, up to their count), -1 when the
    /// group is empty. Public for the tests.
    int dropSlotAt(int y) const;
    /// True while a handle is being dragged.
    bool isDragging() const { return m_dragging; }
    /// The slot the drop indicator shows (-1 = none).
    int indicatorSlot() const { return m_slot; }
    /// Starts / ends a drag as the mouse would (the dev aid and the tests).
    void cancelDrag();

   signals:
    void switchClicked(int row);
    void unhideClicked(int row);
    void menuRequested(int row, const QPoint& globalPos);
    void toggleKeyPressed();                 ///< Space
    void removeKeyPressed();                 ///< Delete
    void moveKeyPressed(int delta);          ///< Alt+Up (-1) / Alt+Down (+1)
    void reorderRequested(int fromOn, int toOn);  ///< a handle was dropped: the place among the ON packs before / after the move

   protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

   private:
    void endDrag(bool drop);
    void autoScroll();
    int slotToIndex(int fromOn, int slot) const;

    PackDelegate* m_delegate;
    QTimer m_scrollTimer;
    int m_pressedSwitchRow = -1;
    int m_pressedUnhideRow = -1;
    int m_gripRow = -1;  ///< a press on a handle: becomes a drag after the pointer moved
    QPoint m_pressPos;
    bool m_dragging = false;
    int m_dragRow = -1;
    int m_dragFromOn = -1;
    int m_slot = -1;
    QPoint m_lastPos;
};

}  // namespace PacksUi
}  // namespace Zero
