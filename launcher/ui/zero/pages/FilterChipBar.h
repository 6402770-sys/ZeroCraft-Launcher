// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the Library filter chips (single select) in a horizontally scrollable row.
#pragma once

#include <QList>
#include <QWidget>
#include "zero/profiles/LibraryFilter.h"

class QHBoxLayout;
class QScrollArea;
class QToolButton;

namespace Zero {

class FilterChip;

/// Chips are 28 px tall, padding 12, `surface` + `border`; the active one is `elevated` + `text` with a 6 px `accent2` dot.
/// Hover / active colors animate (140 ms). setChips() reuses the widgets of chips that stay, so data changes do not flicker.
class FilterChipBar : public QWidget {
    Q_OBJECT
   public:
    static constexpr int kChipHeight = 28;
    static constexpr int kScrollStep = 220;

    explicit FilterChipBar(QWidget* parent = nullptr);

    /// Shows exactly `chips` (in this order) with `active` selected (the All chip when it is not in the list).
    void setChips(const QList<LibraryFilter::ChipInfo>& chips, const LibraryFilter::Chip& active);
    void setActive(const LibraryFilter::Chip& chip);
    LibraryFilter::Chip active() const { return m_active; }

    int chipCount() const;
    QStringList labels() const;
    /// The chip widget showing `chip`, or nullptr.
    QWidget* chipWidget(const LibraryFilter::Chip& chip) const;
    bool isChipActive(const LibraryFilter::Chip& chip) const;
    /// Horizontal scroll position and range (the row scrolls when the chips do not fit).
    int scrollValue() const;
    int scrollMaximum() const;
    /// Scrolls the row by `delta` px (animated unless motion is off); the arrow buttons call this.
    void scrollBy(int delta);
    /// The arrow buttons are visible while the row can scroll in that direction (updated on the next event loop turn).
    bool leftArrowVisible() const;
    bool rightArrowVisible() const;

   signals:
    void chipActivated(const LibraryFilter::Chip& chip);

   private:
    void relayout();
    void refreshArrowIcons();
    void scheduleArrowUpdate();

    QScrollArea* m_scroll;
    QWidget* m_content;
    QHBoxLayout* m_row;
    QList<FilterChip*> m_chips;
    LibraryFilter::Chip m_active;
    QToolButton* m_left = nullptr;
    QToolButton* m_right = nullptr;
    bool m_arrowUpdate = false;
};

}  // namespace Zero
