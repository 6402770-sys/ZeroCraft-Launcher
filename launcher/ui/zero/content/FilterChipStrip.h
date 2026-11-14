// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the removable filter chips under the toolbar (docs/zero/CONTENT.md, "ZERO Filter System" > Widgets).
//
// One 28 px chip per selected value (ContentFilter::chipsFor: "Loader: Fabric", the search text as "text"), then "Clear all" and,
// when the section has default filters that are still applied, "Show everything". Chips wrap to at most 2 lines; when they need
// more, the two lines grow wider and the strip scrolls horizontally. A chip enters and leaves with a 140 ms width + opacity
// animation (instant with motion off). The strip hides itself when there are no chips. It edits a copy of the FilterState and
// emits stateChanged() for every removal; the owner applies it (and calls setState() with the authoritative state).
#pragma once

#include <QList>
#include <QPoint>
#include <QWidget>
#include "zero/content/ContentFilter.h"

class QAbstractButton;
class QPushButton;
class QScrollArea;

namespace Zero {
namespace Content {

class FilterChipStrip : public QWidget {
    Q_OBJECT
   public:
    static constexpr int kChipHeight = 28;
    static constexpr int kMaxLines = 2;
    static constexpr int kAnimMs = 140;
    static constexpr int kGap = 6;

    /// Where the chips go: positions (top-left, in canvas coordinates), the number of lines and the canvas width.
    struct Flow {
        QList<QPoint> positions;
        int lines = 0;
        int contentWidth = 0;
    };
    /// Greedy wrap of items `widths` wide into `available` px; when that needs more than `maxLines` lines the canvas is made
    /// just wide enough for `maxLines` (contentWidth > available = the strip scrolls). Pure.
    static Flow flow(const QList<int>& widths, int available, int gap = kGap, int maxLines = kMaxLines, int rowHeight = kChipHeight);

    explicit FilterChipStrip(QWidget* parent = nullptr);

    /// The facets the chips' labels and the inactive state come from.
    void setFacets(const QList<Facet>& facets);
    /// The section defaults (default chips, Clear all, Show everything).
    void setDefaults(const FilterState& defaults);
    /// Shows the chips of `state`; emits nothing.
    void setState(const FilterState& state);
    FilterState state() const { return m_state; }

    /// The live chips (not the ones fading out), in order.
    QList<Chip> chips() const;
    QStringList chipLabels() const;
    QList<QAbstractButton*> chipButtons() const;
    QPushButton* clearAllButton() const { return m_clearAll; }
    QPushButton* showEverythingButton() const { return m_showEverything; }
    /// Number of lines the chips take now (1..2), and whether the strip scrolls horizontally.
    int lineCount() const { return m_lines; }
    bool scrolls() const { return m_scrolls; }

   signals:
    void stateChanged(const FilterState& state);

   protected:
    void resizeEvent(QResizeEvent* event) override;

   private:
    class Canvas;
    struct Item;

    void refresh();
    void relayout();
    void applyChange(const FilterState& next);
    void removeItem(Item* item);

    QList<Facet> m_facets;
    FilterState m_defaults;
    FilterState m_state;
    QScrollArea* m_scroll;
    Canvas* m_canvas;
    QPushButton* m_clearAll;
    QPushButton* m_showEverything;
    QList<Item*> m_items;
    int m_lines = 0;
    bool m_scrolls = false;
};

}  // namespace Content
}  // namespace Zero
