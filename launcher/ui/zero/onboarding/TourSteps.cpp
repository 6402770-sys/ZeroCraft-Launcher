// SPDX-License-Identifier: GPL-3.0-only
/*
 *  ZERO Launcher - guided tour content and card geometry. See TourSteps.h.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "TourSteps.h"

#include <QCoreApplication>
#include <algorithm>

namespace Zero {
namespace Onboarding {

namespace {
}  // namespace

QList<TourStepInfo> tourStepInfos()
{
    return {
        { QStringLiteral("home"), QCoreApplication::translate("Zero::Onboarding::TourSteps", "Home"), QCoreApplication::translate("Zero::Onboarding::TourSteps", "Jump back into your last game, see your favourites and what you played recently."),
          TourSide::Right },
        { QStringLiteral("library"), QCoreApplication::translate("Zero::Onboarding::TourSteps", "Library"), QCoreApplication::translate("Zero::Onboarding::TourSteps", "All your instances in one place. Star favourites, add tags and press Play."),
          TourSide::Right },
        { QStringLiteral("new-instance"), QCoreApplication::translate("Zero::Onboarding::TourSteps", "New instance"),
          QCoreApplication::translate("Zero::Onboarding::TourSteps", "Start fresh with vanilla, go modded with a loader, or install a modpack in one click."), TourSide::Below },
        { QStringLiteral("discover"), QCoreApplication::translate("Zero::Onboarding::TourSteps", "Discover"),
          QCoreApplication::translate("Zero::Onboarding::TourSteps", "Find modpacks, mods, resource packs, shaders and worlds. Add several to the cart and install them in one go."),
          TourSide::Right },
        { QStringLiteral("search"), QCoreApplication::translate("Zero::Onboarding::TourSteps", "Search anything"), QCoreApplication::translate("Zero::Onboarding::TourSteps", "Press Ctrl+K to jump to an instance, a page or an action."), TourSide::Below },
        { QStringLiteral("appearance"), QCoreApplication::translate("Zero::Onboarding::TourSteps", "Make it yours"),
          QCoreApplication::translate("Zero::Onboarding::TourSteps", "Pick a theme or build your own: colours, background, icon and font."), TourSide::Right },
    };
}

PaletteCommand tourPaletteCommand(std::function<void()> run)
{
    PaletteCommand c;
    c.id = tourCommandId();
    c.kind = Palette::Kind::Action;
    c.title = QCoreApplication::translate("Zero::Onboarding::TourSteps", "Take the tour");
    c.keywords = { QCoreApplication::translate("Zero::Onboarding::TourSteps", "tour"), QCoreApplication::translate("Zero::Onboarding::TourSteps", "help"), QCoreApplication::translate("Zero::Onboarding::TourSteps", "guide"), QCoreApplication::translate("Zero::Onboarding::TourSteps", "tutorial"), QCoreApplication::translate("Zero::Onboarding::TourSteps", "welcome"), QCoreApplication::translate("Zero::Onboarding::TourSteps", "intro") };
    c.iconName = QStringLiteral("compass");
    c.primaryLabel = QCoreApplication::translate("Zero::Onboarding::TourSteps", "Start");
    c.run = std::move(run);
    return c;
}

QRect spotlightRect(const QRect& target, const QRect& window)
{
    return target.adjusted(-kSpotlightPad, -kSpotlightPad, kSpotlightPad, kSpotlightPad).intersected(window);
}

CardPlacement placeCard(const QRect& spot, const QSize& size, const QSize& window, TourSide side)
{
    CardPlacement out;
    const int maxX = std::max(kCardMargin, window.width() - kCardMargin - size.width());
    const int maxY = std::max(kCardMargin, window.height() - kCardMargin - size.height());
    int x = 0, y = 0;
    if (side == TourSide::Right) {
        x = spot.right() + 1 + kCardGap;
        y = spot.center().y() - size.height() / 2;
    } else {
        x = spot.center().x() - size.width() / 2;
        y = spot.bottom() + 1 + kCardGap;
    }
    x = std::clamp(x, kCardMargin, maxX);
    y = std::clamp(y, kCardMargin, maxY);
    out.card = QRect(QPoint(x, y), size);
    // the pointer stays clear of the rounded corners (14 px from either end) and tracks the cut-out's centre
    constexpr int kEnd = 14 + kPointerSize;
    if (side == TourSide::Right)
        out.pointer = std::clamp(spot.center().y() - y, std::min(kEnd, size.height() / 2), std::max(size.height() - kEnd, size.height() / 2));
    else
        out.pointer = std::clamp(spot.center().x() - x, std::min(kEnd, size.width() / 2), std::max(size.width() - kEnd, size.width() / 2));
    return out;
}

}  // namespace Onboarding
}  // namespace Zero
