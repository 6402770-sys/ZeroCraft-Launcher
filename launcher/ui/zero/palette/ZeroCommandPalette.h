// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the command palette (Ctrl+K). MainWindow hands it command descriptors (see PaletteTypes.h); ranking
// is Zero::Palette::rank(). Spec: docs/zero/SHELL.md, "Command palette".
#pragma once

#include <QList>
#include <QVariantAnimation>
#include <functional>

#include "PaletteOverlay.h"
#include "PaletteTypes.h"

class QLineEdit;

namespace Zero {

class PaletteInputRow;
class PaletteFooter;
class PaletteResults;

class ZeroCommandPalette : public PaletteOverlay {
    Q_OBJECT
   public:
    explicit ZeroCommandPalette(QWidget* host);

    using Provider = std::function<QList<PaletteCommand>()>;
    /// Called every time the palette opens, so the commands reflect the current enabled state of the real actions.
    void setCommandProvider(Provider provider) { m_provider = std::move(provider); }
    /// Replaces the commands directly (tests, static lists).
    void setCommands(const QList<PaletteCommand>& commands);

    QLineEdit* input() const;
    QString query() const;
    void setQuery(const QString& query);

    /// Command ids in display order (command rows only) / the selected one ("" = none).
    QStringList resultIds() const;
    QString selectedId() const;
    int selectedIndex() const;  ///< position among the command rows, -1 = none
    QStringList sectionTitles() const;
    QString footerText() const;
    /// Moves the selection by `delta` command rows, wrapping around.
    void moveSelection(int delta);
    /// Closes the palette and runs the selected command's callback (`secondary`: Ctrl+Enter action).
    void runSelected(bool secondary);
    /// Geometry of the selected row in the results view (tests / screenshots).
    PaletteResults* results() const { return m_results; }

   protected:
    void aboutToOpen() override;
    QWidget* initialFocus() override;
    int panelHeight() const override;
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void rebuild();
    void relayout();
    void updateFooter();
    void runCommand(int commandIndex, bool secondary);

    Provider m_provider;
    QList<PaletteCommand> m_commands;
    QString m_lastQuery;
    PaletteInputRow* m_inputRow;
    PaletteResults* m_results;
    PaletteFooter* m_footer;
};

/// Flat list of section headers and command rows with its own scrolling and the gliding selection highlight.
class PaletteResults : public QWidget {
    Q_OBJECT
   public:
    struct Row {
        bool header = false;
        QString text;  ///< header text
        int command = -1;
        QList<int> positions;  ///< matched title characters
    };
    static constexpr int kRowHeight = 40;
    static constexpr int kHeaderHeight = 28;
    static constexpr int kPad = 4;
    static constexpr int kMaxViewHeight = 8 * kRowHeight + 2 * kHeaderHeight + 2 * kPad;
    static constexpr int kEmptyHeight = 72;

    explicit PaletteResults(QWidget* parent = nullptr);

    void setRows(const QList<Row>& rows, const QList<PaletteCommand>* commands, const QString& emptyText);
    int viewHeight() const;
    int selectedCommand() const;  ///< index into the commands list, -1 = none
    int selectedPosition() const;  ///< among the command rows
    int commandCount() const { return m_commandRows.size(); }
    QList<int> commandOrder() const;  ///< command indices in display order
    QStringList headers() const;
    void moveSelection(int delta);
    void selectPosition(int position, bool scroll);
    QRect selectedRowRect() const;
    int scrollOffset() const { return m_scroll; }

    // Accessibility: every row (header or command) is one child of the list's accessible interface.
    int rowCount() const { return int(m_rows.size()); }
    const Row& row(int index) const { return m_rows.at(index); }
    QString rowTitle(int index) const;
    QString rowDescription(int index) const;
    QRect rowRect(int index) const;  ///< widget coordinates
    bool isSelectedRow(int index) const { return index == m_selectedRow; }
    void activateRow(int index);     ///< same as clicking a command row; headers do nothing
    quint64 generation() const { return m_generation; }  ///< bumped by setRows(); stale row interfaces become invalid

   signals:
    void selectionChanged();
    void activated(int commandIndex, bool secondary);

   protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

   private:
    int contentHeight() const;
    int rowAt(int y) const;
    void ensureVisible(int row);
    void select(int row, bool scroll, bool animate);

    QList<Row> m_rows;
    QList<int> m_tops;         // content y of every row
    QList<int> m_commandRows;  // row indices of the command rows
    const QList<PaletteCommand>* m_commands = nullptr;
    QString m_emptyText;
    int m_selectedRow = -1;
    quint64 m_generation = 0;
    int m_scroll = 0;
    qreal m_highlightY = 0;
    QVariantAnimation m_glide;
};

}  // namespace Zero
