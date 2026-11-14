// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - painted version list of Create Instance (Minecraft versions, loader versions). A flat `surface` panel
// holding one of four states: loading (40 px row skeletons with the sweep), the list (painted 40 px rows: version, pills,
// date caption), an inline error with Retry (never a message box), or a one-line empty text.
#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QElapsedTimer>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QWidget>

#include "ui/zero/PillTag.h"

class QLabel;
class QListView;
class QPushButton;
class QStackedLayout;

namespace Zero {
namespace Create {

/// One painted row: title, an optional plain pill (type), an optional success pill ("Latest", "Recommended") and the date.
struct ListRow {
    QString key;
    QString title;
    QString typePill;
    QString statusPill;
    QDateTime date;
};

class VersionRowModel : public QAbstractListModel {
    Q_OBJECT
   public:
    enum Role { KeyRole = Qt::UserRole + 1 };

    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : int(m_rows.size()); }
    QVariant data(const QModelIndex& index, int role) const override;

    void setRows(const QList<ListRow>& rows);
    const ListRow& row(int i) const { return m_rows.at(i); }
    int indexOfKey(const QString& key) const;

   private:
    QList<ListRow> m_rows;
};

class VersionRowDelegate : public QStyledItemDelegate {
    Q_OBJECT
   public:
    static constexpr int kRowHeight = 40;

    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    /// The date as rows show it ("Jun 12, 2023").
    static QString dateText(const QDateTime& date);
};

/// Grey 40 px rows (title block, a pill block, a date block) with the ZeroSkeleton sweep while visible and motion is on.
class RowSkeleton : public QWidget {
    Q_OBJECT
   public:
    explicit RowSkeleton(QWidget* parent = nullptr);

   protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

   private:
    QTimer m_timer;
    QElapsedTimer m_clock;
};

class VersionListView : public QWidget {
    Q_OBJECT
   public:
    enum class State { Loading, List, Error, Empty };

    /// `accessibleName` names the list ("Minecraft versions", "Loader versions").
    explicit VersionListView(const QString& accessibleName, QWidget* parent = nullptr);

    void showLoading(const QString& caption);
    /// Inline error: `message` plus a Retry button (retryRequested()).
    void showError(const QString& message);
    /// Shows `rows`; `emptyText` when there are none. Keeps the current key when it is still listed, otherwise none.
    void setRows(const QList<ListRow>& rows, const QString& emptyText);
    State state() const { return m_state; }

    /// Selects the row with `key` (none for an unknown key) and scrolls to it; no currentKeyChanged.
    void setCurrentKey(const QString& key);
    QString currentKey() const;
    int rowCount() const { return m_model->rowCount(); }
    QListView* listView() const { return m_list; }
    QString errorText() const;
    QString emptyText() const;

   signals:
    /// The user picked another row (mouse or keyboard).
    void currentKeyChanged(const QString& key);
    void retryRequested();

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    void setState(State state);
    void refreshIcons();

    State m_state = State::Loading;
    QStackedLayout* m_stack;
    RowSkeleton* m_skeleton;
    QLabel* m_loadingCaption;
    QWidget* m_loadingPage;
    QListView* m_list;
    VersionRowModel* m_model;
    QWidget* m_errorPage;
    QLabel* m_errorIcon;
    QLabel* m_errorText;
    QPushButton* m_retry;
    QLabel* m_emptyText;
    bool m_settingCurrent = false;
};

}  // namespace Create
}  // namespace Zero
