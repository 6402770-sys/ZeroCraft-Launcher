// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - snapshot-overlay transitions (docs/zero/DESIGN.md, "Motion language").
//
// A transition never animates the real widgets or their layout. The caller captures the host's pixels, changes the
// UI synchronously, then runs an overlay: a mouse-transparent child of the host that covers it, paints pixmaps and
// deletes itself when done. The live widgets underneath are interactive from the first frame. With motion off
// nothing is captured and nothing is created.
//
//   auto before = Transition::capture(host);
//   changeTheUi();
//   Transition::run(host, before, Transition::Kind::Crossfade, Tokens::durFast);
//
// Triggering again while an overlay runs replaces it cleanly: capture() includes the running overlay in the
// snapshot (so the new transition starts from what is on screen) and run() removes the old overlay first.
#pragma once

#include <QList>
#include <QPixmap>
#include <functional>
#include "ZeroTokens.h"

class QWidget;

namespace Zero {
namespace Transition {

enum class Kind {
    Crossfade,     ///< old snapshot fades out over the live (new) widgets
    Rise,          ///< old snapshot is covered by a snapshot of the new content that rises 10 px -> 0 while fading in
    PixelDissolve  ///< old snapshot is removed in 16 px blocks, in a deterministic shuffled order
};

/// Pixels of a host captured before a UI change. Null when motion is off or the host is not visible.
struct Snapshot {
    QPixmap pixmap;
    bool isNull() const { return pixmap.isNull(); }
};

/// Captures what `host` currently shows (including any overlay that is running on it).
Snapshot capture(QWidget* host);

/// Starts the overlay for a change made after capture(). `ms` <= 0 uses the kind's default duration. No-op when
/// `from` is null or motion is off. Any overlay already running on `host` is removed first.
void run(QWidget* host, const Snapshot& from, Kind kind, int ms = 0);

/// capture(), `change()`, run() in one call.
void run(QWidget* host, Kind kind, int ms, const std::function<void()>& change);

/// Removes the overlay running on `host`, if any (the live widgets show immediately).
void cancel(QWidget* host);

/// Number of overlays currently attached to `host` (0 or 1; used by tests).
int overlayCount(QWidget* host);

/// Deterministic shuffled order of the blocks of a `cols` x `rows` grid (a permutation of 0..cols*rows-1). Exposed so
/// the dissolve pattern is testable and identical from run to run.
QList<quint32> dissolveOrder(int cols, int rows);

}  // namespace Transition
}  // namespace Zero
