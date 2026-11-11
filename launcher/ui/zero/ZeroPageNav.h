// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - side list of a PageContainer (settings, instance window, page dialogs): one row per page with its
// icon, its name and a one-line summary of what it is for (Zero::PageHelp), an accent bar on the selected row.
#pragma once

#include <QStyledItemDelegate>

namespace Zero {

class PageNavDelegate : public QStyledItemDelegate {
    Q_OBJECT
   public:
    static constexpr int kWidth = 252;        ///< width of the side list
    static constexpr int kRowWithSummary = 60;  ///< name + up to two lines of summary
    static constexpr int kRowPlain = 40;
    static constexpr int kPageIdRole = Qt::UserRole + 40;  ///< model role with the page id (PageModel provides it)

    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

}  // namespace Zero
