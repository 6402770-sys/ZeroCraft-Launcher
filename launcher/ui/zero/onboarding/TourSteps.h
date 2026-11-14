// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the guided tour's content and geometry, with no widgets in it: the six steps (ids, titles, texts, which side of the
// target the card sits on), where the callout card goes for a given target, and the palette entry that replays the tour.
// See TourOverlay.h and docs/zero/SHELL.md, "Guided tour".
#pragma once

#include <QList>
#include <QRect>
#include <QSize>
#include <QString>
#include <functional>

#include "ui/zero/palette/PaletteTypes.h"

namespace Zero {
namespace Onboarding {

/// Which side of the highlighted widget the callout card sits on.
enum class TourSide {
    Right,  ///< beside a rail item: right of it, vertically centred on it
    Below   ///< under a top bar control: below it, horizontally centred on it
};

/// One step as the tour shows it. `id` ties it to its target (MainWindow resolves the widget).
struct TourStepInfo {
    QString id;
    QString title;
    QString body;
    TourSide side = TourSide::Right;
};

/// The six steps, in order: home, library, new-instance, discover, search, appearance.
QList<TourStepInfo> tourStepInfos();

/// Id of the palette entry that replays the tour.
inline QString tourCommandId()
{
    return QStringLiteral("action:take-tour");
}

/// The "Take the tour" palette entry (`run` starts the tour).
PaletteCommand tourPaletteCommand(std::function<void()> run);

constexpr int kSpotlightPad = 8;  ///< px between the target and the edge of the cut-out
constexpr int kCardWidth = 300;
constexpr int kCardMargin = 16;   ///< the card never gets closer than this to the window edge
constexpr int kCardGap = 12;      ///< target (cut-out edge) to card edge; the 8 px pointer sits in it
constexpr int kPointerSize = 8;

/// The cut-out around `target` (window coordinates): padded by kSpotlightPad, kept inside `window`.
QRect spotlightRect(const QRect& target, const QRect& window);

struct CardPlacement {
    QRect card;       ///< the card (window coordinates), always inside `window` shrunk by kCardMargin (when it fits)
    int pointer = 0;  ///< where the pointer touches the card's edge facing the target, along that edge (0 = card's left / top edge)
};

/// Where the card of `size` goes for the cut-out `spot` in a window of `window` px. Right: beside the cut-out, centred on it vertically;
/// Below: under it, centred horizontally. Clamped inside the window; the pointer keeps pointing at the cut-out's centre.
CardPlacement placeCard(const QRect& spot, const QSize& size, const QSize& window, TourSide side);

}  // namespace Onboarding
}  // namespace Zero
