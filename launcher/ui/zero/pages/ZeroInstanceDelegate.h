// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - paints the Library grid cards (168x188). Inline rename editing is inherited from ListViewDelegate.
#pragma once

#include <QList>
#include <QPersistentModelIndex>
#include <QPointer>
#include "ui/instanceview/InstanceDelegate.h"

class QAbstractItemView;
class QVariantAnimation;

namespace Zero {

class ZeroInstanceDelegate : public ListViewDelegate {
    Q_OBJECT
   public:
    static constexpr int kCellWidth = 168;
    static constexpr int kCellHeight = 188;
    static constexpr int kIconAreaHeight = 112;
    static constexpr int kIconSize = 64;
    static constexpr int kBadgeSize = 28;  ///< instance icon badge on top of artwork
    static constexpr int kStarHit = 26;    ///< favorite star hit box (top-left corner of the card)

    /// The favorite star's hit box inside a card (`cell` = the item rect).
    static QRect starRect(const QRect& cell);

    explicit ZeroInstanceDelegate(QObject* parent = nullptr);

    /// Starts tracking the hovered card through `view`'s viewport (mouse move / leave) so a hovered card can lift
    /// 2 px and gain a soft shadow, animated 140 ms in and out. Each card animates independently.
    void attachTo(QAbstractItemView* view);

    /// 0..1 hover progress of `index` (0 when it is not, or no longer, hovered).
    qreal hoverProgress(const QModelIndex& index) const;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

   signals:
    /// The favorite star of a card was clicked. The click is consumed: the selection does not change.
    void favoriteToggleRequested(const QString& instanceId);

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    struct Hover {
        QPersistentModelIndex index;
        qreal progress = 0.0;
        QVariantAnimation* anim = nullptr;
    };
    Hover* find(const QModelIndex& index) const;
    void setHovered(const QModelIndex& index);
    void animate(const QModelIndex& index, bool on);
    void repaintCard(const QModelIndex& index);

    QPointer<QAbstractItemView> m_view;
    QPersistentModelIndex m_hovered;
    QPersistentModelIndex m_starHot;  // the card whose star the pointer is over
    QPersistentModelIndex m_starPressed;
    QList<Hover*> m_hovers;
};

}  // namespace Zero
