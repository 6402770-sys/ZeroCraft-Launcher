// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - paints the Library grid cards (168x188). Inline rename editing is inherited from ListViewDelegate.
#pragma once

#include "ui/instanceview/InstanceDelegate.h"

namespace Zero {

class ZeroInstanceDelegate : public ListViewDelegate {
    Q_OBJECT
   public:
    static constexpr int kCellWidth = 168;
    static constexpr int kCellHeight = 188;
    static constexpr int kIconAreaHeight = 112;
    static constexpr int kIconSize = 64;

    explicit ZeroInstanceDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

}  // namespace Zero
