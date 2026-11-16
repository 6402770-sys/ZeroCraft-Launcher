// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the list of the instance window's Mods page (docs/zero/CONTENT-MODS.md, "Installed mods page"): the row model (a
// copy of the facts of the visible mods, with a token per mod so the page can tell which Prism mod a row is), the delegate that paints
// the 56 px rows (toggle switch, icon, name + version, pills, the `...` button) and the view that turns a click on the switch or the
// button into a signal. Nothing here changes a mod.
#pragma once

#include <QAbstractListModel>
#include <QListView>
#include <QPixmap>
#include <QStyledItemDelegate>

#include <functional>

#include "zero/mods/InstalledModsModel.h"

namespace Zero {
namespace ModsUi {

/// The visible rows, in display order. Row facts are copies: painting never touches a Prism object that may have been replaced.
class ModListModel : public QAbstractListModel {
    Q_OBJECT
   public:
    using Row = Mods::Installed::Row;
    enum Roles { TokenRole = Qt::UserRole + 1, EnabledRole };

    explicit ModListModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    /// Replaces the rows. `tokens[i]` identifies the Prism mod of `rows[i]` (its address: compared, never dereferenced), `sourceRows[i]` is its
    /// row in the Prism model. When only the facts changed (same tokens in the same order) the rows are updated in place (no reset: the
    /// scroll position and the selection stay). Returns true when the list was reset.
    bool setRows(const QList<Row>& rows, const QList<quintptr>& tokens, const QList<int>& sourceRows);

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : int(m_rows.size()); }
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    const Row& rowAt(int row) const { return m_rows.at(row); }
    quintptr tokenAt(int row) const { return m_tokens.at(row); }
    int sourceRowAt(int row) const { return m_sourceRows.at(row); }
    int rowOfToken(quintptr token) const { return int(m_tokens.indexOf(token)); }
    int rowOfId(const QString& id) const;

   private:
    QList<Row> m_rows;
    QList<quintptr> m_tokens;
    QList<int> m_sourceRows;
};

class ModDelegate : public QStyledItemDelegate {
    Q_OBJECT
   public:
    static constexpr int kRowHeight = 56;
    static constexpr int kRowGap = 8;
    static constexpr int kPad = 12;
    static constexpr int kIcon = 32;
    static constexpr int kPillHeight = 20;
    static constexpr int kPillGap = 6;
    static constexpr int kSwitchWidth = 36;
    static constexpr int kSwitchHeight = 20;
    static constexpr int kMenuSize = 28;

    explicit ModDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    /// The mod logo of the row with that model id, scaled to `px` logical pixels; null when there is none (yet).
    using IconProvider = std::function<QPixmap(const QString& id, int px, qreal dpr)>;
    void setIconProvider(IconProvider provider) { m_icons = std::move(provider); }
    void setMinecraftVersion(const QString& version) { m_mcVersion = version; }
    /// While the game runs (or an update check reads the mods) the switches are dimmed and do nothing; `reason` is their tooltip.
    void setLocked(bool locked, const QString& reason = QString())
    {
        m_locked = locked;
        m_lockReason = reason;
    }
    bool locked() const { return m_locked; }
    /// The icon of the tile of a row without a logo (default "blocks"; the Shader packs page uses "sun").
    void setFallbackIcon(const QString& name) { m_fallbackIcon = name; }

    /// The painted card of a cell (the cell minus the gap below).
    static QRect cardRect(const QRect& cell);
    static QRect switchRect(const QRect& card);
    static QRect menuRect(const QRect& card);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    bool helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option, const QModelIndex& index) override;

    /// The pills that fit `budget` px, lowest priority dropped first. Public for the tests.
    struct Placed {
        Mods::Installed::PillSpec spec;
        int width = 0;
        QString text;  ///< spec.text, elided to spec.maxWidth when that is set
    };
    static QList<Placed> fitPills(const QList<Mods::Installed::PillSpec>& pills, int budget);
    /// Elides the widest pills until the row of pills is at most `limit` px wide (the pills that must stay may exceed the budget of fitPills()).
    static void shrinkToFit(QList<Placed>& placed, int limit);

   private:
    void paintSwitch(QPainter* painter, const QRect& rect, bool on, bool locked) const;
    void paintIcon(QPainter* painter, const QRect& rect, const Mods::Installed::Row& row) const;

    IconProvider m_icons;
    QString m_mcVersion;
    bool m_locked = false;
    QString m_lockReason;
    QString m_fallbackIcon = QStringLiteral("blocks");
};

class ModListView : public QListView {
    Q_OBJECT
   public:
    explicit ModListView(QWidget* parent = nullptr);
    ModDelegate* rowDelegate() const { return m_delegate; }
    /// While true the view takes Esc (and emits escapePressed()) instead of passing it on.
    void setEscapeCloses(bool closes) { m_escapeCloses = closes; }

   signals:
    void switchClicked(int row);
    void menuRequested(int row, const QPoint& globalPos);
    void toggleKeyPressed();   ///< Space
    void removeKeyPressed();   ///< Delete
    void rowClicked(int row);  ///< a plain click on the body of a row (not the switch, not `...`, no Ctrl / Shift)
    void detailsKeyPressed();  ///< Enter on the keyboard row
    void escapePressed();      ///< Esc, only while setEscapeCloses(true)

   protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

   private:
    ModDelegate* m_delegate;
    int m_pressedSwitchRow = -1;
    int m_pressedBodyRow = -1;
    bool m_pressedPlain = false;
    bool m_escapeCloses = false;
};

}  // namespace ModsUi
}  // namespace Zero
