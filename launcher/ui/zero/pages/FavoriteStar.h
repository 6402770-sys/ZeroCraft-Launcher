// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the favorite star at the right end of a Worlds / Servers row: a solid gold star on favorites, an
// outline star on the hovered row (click it to star), nothing otherwise. The row delegates reserve kColumn pixels for it.
#pragma once

#include <QPushButton>
#include <QRect>
#include <functional>
#include "zero/favorites/FavoriteStore.h"

class QEvent;
class QPainter;

namespace Zero {
namespace FavoriteStar {

constexpr int kColumn = 36;  ///< reserved at the right of a row (hit area + gap)
constexpr int kHit = 28;     ///< click target
constexpr int kGlyph = 16;

/// Click / paint area of the star in a row whose visual rectangle is `row` (right padding `padding`).
inline QRect rect(const QRect& row, int padding)
{
    return QRect(row.right() + 1 - padding - kHit, row.center().y() - kHit / 2, kHit, kHit);
}

/// Paints the star for a row (see the file comment for what shows when).
void paint(QPainter* painter, const QRect& hit, bool favorite, bool rowHovered);

/// For a delegate's editorEvent(): a left press / double click inside `hit` is swallowed (the row is not selected), a left
/// release inside it calls `toggle`. Returns true when the event was the star's.
bool handleMouse(QEvent* event, const QRect& hit, const std::function<void()>& toggle);

}  // namespace FavoriteStar

/// "Add to favorites" / "Favorite" button of a details panel; flips the key in the application's FavoriteStore and follows
/// changes made elsewhere (the row star).
class FavoriteButton : public QPushButton {
   public:
    FavoriteButton(Favorites::Kind kind, QWidget* parent = nullptr);
    void setKey(const QString& key);

   private:
    void refresh();
    Favorites::Kind m_kind;
    QString m_key;
};
}  // namespace Zero
