// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - slide-in of new rows in the Discover item views (docs/zero/CONTENT.md, "Motion").
//
//   auto* reveal = Zero::Content::installContentReveal(m_view, Zero::Content::RevealSpec::modpacks());  // after setModel()
//
// New rows enter from the RIGHT: x + offsetPx, opacity 0.15, scale 0.985 -> final in durationMs (OutCubic), staggered by ROW
// ORDER (the n-th row of the batch starts n * staggerMs later; from the 14th row on they all start together). Only rows
// inserted since the last frame animate (first page, next page, a new result set): rows that merely emit dataChanged (a
// picture arrived, the installed state changed) are never touched. A model reset starts a new generation: whatever was
// still entering stops, and the rows of the new set enter. Painting happens in a delegate wrapper (no per-item widgets, no
// layout animation); one 60 Hz timer runs only while something enters. Motion off (Zero::Motion::enabled()) -> instant.
//
// Rows that are not on screen when their entrance would begin are simply final (they do not animate when scrolled to).
// Deterministic: advance() processes one frame against the clock, which tests replace with setClock().
#pragma once

#include <QList>
#include <QObject>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QTimer>
#include <functional>

class QAbstractItemModel;
class QAbstractItemView;
class QModelIndex;
class QPainter;
class QRect;

namespace Zero {
namespace Content {

struct RevealSpec {
    int offsetPx = 24;           ///< start offset to the right
    int durationMs = 320;        ///< one row's entrance
    int staggerMs = 32;          ///< delay between two consecutive rows of a batch
    int maxSteps = 14;           ///< the stagger stops growing at the 14th row
    qreal startOpacity = 0.15;
    qreal startScale = 0.985;

    static RevealSpec modpacks() { return { 24, 320, 32, 14, 0.15, 0.985 }; }
    static RevealSpec mods() { return { 16, 260, 24, 14, 0.15, 0.985 }; }
    static RevealSpec gallery() { return { 20, 300, 28, 14, 0.15, 0.985 }; }
};

class ContentReveal : public QObject {
    Q_OBJECT
   public:
    static constexpr int kFrameMs = 16;  ///< 60 Hz

    ContentReveal(QAbstractItemView* view, const RevealSpec& spec);

    const RevealSpec& spec() const { return m_spec; }

    /// (Re)connects to the view's current model and forgets every tracked row. The constructor calls it.
    void attachModel();

    /// Starts a new generation: rows of the old one stop animating (they paint final). A model reset does this itself;
    /// call it when a new search begins without a reset (the proxy model filters in place).
    void beginGeneration();
    quint64 generation() const { return m_generation; }

    /// Every row paints final at once.
    void finish();

    /// True while a row is entering, waiting for its start, or a reset waits for its first frame.
    bool isAnimating() const { return m_pendingReset || !m_entries.isEmpty(); }
    /// Rows currently tracked (entering or waiting for their start).
    int trackedCount() const { return int(m_entries.size()); }
    /// True while the 60 Hz timer runs (it stops when nothing is left to animate).
    bool timerActive() const { return m_timer.isActive(); }
    /// Start of `index`'s entrance relative to the first row of its batch (ms), -1 when it is not tracked.
    int startDelay(const QModelIndex& index) const;
    /// Eased 0..1 progress (0 = not started / hidden, 1 = final, also for untracked rows and with motion off).
    qreal progress(const QModelIndex& index) const;
    /// Opacity, offset and scale of `index` at `progress` p, applied to `painter` for an item painted at `itemRect`.
    void apply(QPainter* painter, const QRect& itemRect, const QModelIndex& index) const;

    /// One frame: closes the batch of this frame, starts a pending reset, drops finished rows and rows that are off screen
    /// when they start, repaints, and stops the timer when nothing is left. The timer calls it; tests call it directly.
    void advance();
    /// Replaces the clock (ms, monotonic) - tests only.
    void setClock(std::function<qint64()> clock);

   private:
    struct Entry {
        QPersistentModelIndex index;
        qint64 start = 0;
        qint64 batchStart = 0;
        bool checked = false;  ///< the visibility check at its start has been made
    };

    qint64 now() const;
    const Entry* find(const QModelIndex& index) const;
    bool canAnimate() const;
    void onRowsInserted(const QModelIndex& parent, int first, int last);
    void onModelReset();
    void startPendingReset();
    void ensureTimer();

    QPointer<QAbstractItemView> m_view;
    QPointer<QAbstractItemModel> m_model;
    RevealSpec m_spec;
    std::function<qint64()> m_clock;
    QTimer m_timer;
    QList<Entry> m_entries;
    quint64 m_generation = 0;
    bool m_pendingReset = false;
    bool m_batchOpen = false;
    qint64 m_batchStart = 0;
    int m_batchCount = 0;
};

/// Wraps `view`'s current item delegate in one that paints through a ContentReveal (size hints, clicks, tooltips and editors go
/// to the original delegate, which stays owned by its parent). Returns the reveal, owned by `view`. Call after
/// setItemDelegate() and setModel() (after a later setModel(), call attachModel()).
ContentReveal* installContentReveal(QAbstractItemView* view, const RevealSpec& spec);

}  // namespace Content
}  // namespace Zero
