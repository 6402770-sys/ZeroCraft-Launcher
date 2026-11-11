// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - staggered entrance of the items of a painted item view (Library, Worlds, Screenshots, Servers).
//
//   auto* reveal = Motion::installItemReveal(m_list);  // wraps the view's delegate, keep the pointer
//   ...
//   reveal->start();  // when new content appears (first scan, filter / sort change)
//
// Every item fades in and rises 8 px into place; items further down and to the right start later, so a list or grid
// fills in a diagonal wave. With motion off start() does nothing and items paint as usual.
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QTimer>

class QAbstractItemView;
class QPainter;

namespace Zero {
namespace Motion {

class ItemReveal : public QObject {
   public:
    static constexpr int kRisePx = 8;
    static constexpr int kStepY = 56;     ///< one stagger step per this many pixels down
    static constexpr int kStepX = 220;    ///< and per this many pixels to the right
    static constexpr int kMaxDelay = 420;  ///< the last items never wait longer than this

    explicit ItemReveal(QAbstractItemView* view);

    /// Starts (or restarts) the entrance. No-op when motion is off or the view is hidden.
    void start();
    /// Stops it: every item paints fully at once.
    void finish();
    bool running() const { return m_running; }

    /// 0..1 (eased) for an item painted at `itemRect` (viewport coordinates). 1 when not running.
    qreal progress(const QRect& itemRect) const;
    /// Applies the item's opacity and rise to `painter` (call between save() and restore()).
    void apply(QPainter* painter, const QRect& itemRect) const;

    /// Delay before an item at `itemRect` starts (ms); exposed for tests.
    static int delayFor(const QRect& itemRect);

   private:
    void tick();

    QPointer<QAbstractItemView> m_view;
    QElapsedTimer m_clock;
    QTimer m_timer;
    bool m_running = false;
};

/// Wraps `view`'s current item delegate in one that paints through an ItemReveal (everything else - size hints, clicks,
/// tooltips, editors - goes to the original delegate, which stays owned by its parent). Returns the reveal, owned by
/// `view`. Call after setItemDelegate().
ItemReveal* installItemReveal(QAbstractItemView* view);

}  // namespace Motion
}  // namespace Zero
