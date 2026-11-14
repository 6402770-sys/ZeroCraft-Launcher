// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - what ContentSection needs from the delegate that paints its result list (grid cards / list rows). Every kind
// of Discover section (Modpacks, Mods ...) has its own painted delegate; the section only drives the geometry and the dimming.
//
// The Mods, Resource Packs and Shaders delegates also paint the install cart's check box (docs/zero/SHELL.md, "Discover > Install
// cart"); what they ask the section is CartHooks, and where the box is (checkRect) is public so the section can hit-test it.
#pragma once

#include <QRect>
#include <QSize>
#include <QStyledItemDelegate>

#include <functional>

#include "zero/content/ModpackSource.h"

namespace Zero::Content {

/// How one card paints the cart: ticked (2 px accent border + filled box) and whether the box shows at all.
struct CartLook {
    bool inCart = false;
    bool showCheck = false;
};

/// What a delegate asks the section to paint the cart's check box. An empty `eligible` = this delegate has no cart (Modpacks).
struct CartHooks {
    std::function<bool(const ContentItem&)> eligible;  ///< the item may be ticked (a target is known, not exactly installed)
    std::function<bool(const ContentItem&)> inCart;    ///< the item is ticked
    std::function<bool()> anyInCart;                   ///< the cart is not empty: every eligible card shows its box
    std::function<bool()> keyboardFocus;               ///< the focus ring of the current card comes from the keyboard
};

class ContentDelegate : public QStyledItemDelegate {
    Q_OBJECT
   public:
    enum class Mode { Grid, List };

    static constexpr int kCheckSize = 20;   ///< the cart's check box, px
    static constexpr int kCheckInset = 8;   ///< from the artwork's top-left corner (gallery cards); grid mod cards put the box top-right (see ModDelegate::checkRect)

    using QStyledItemDelegate::QStyledItemDelegate;

    virtual void setMode(Mode mode) = 0;
    virtual Mode mode() const = 0;
    /// Rows are drawn at half opacity while a new search is pending (the old rows stay until the answer arrives).
    virtual void setDimmed(bool dimmed) = 0;
    virtual bool isDimmed() const = 0;
    /// Recomputes the columns and the card size for a viewport `width` px wide; true when something changed (then call
    /// setGridSize(gridSize()) on the view).
    virtual bool setViewportWidth(int width) = 0;
    virtual int columns() const = 0;
    /// The view's cell: the card plus the gap below it.
    virtual QSize gridSize() const = 0;

    /// The cart's hooks (the section sets them once; copies are cheap).
    void setCartHooks(const CartHooks& hooks) { m_cart = hooks; }
    bool hasCart() const { return bool(m_cart.eligible); }
    const CartHooks& cartHooks() const { return m_cart; }
    /// The card (grid) or row (list) of the view cell `cell` (the QListView's visualRect), without the gap.
    virtual QRect cardRect(const QRect& cell) const { return cell; }
    /// The 20 px check box of the card whose view cell is `cell`, in the view's coordinates; a null rect = no cart in this delegate.
    virtual QRect checkRect(const QRect&) const { return {}; }

   protected:
    /// The cart look of `item`: the box shows while the item is ticked, or it is eligible and (hovered, keyboard-focused or the cart is
    /// not empty).
    CartLook cartLook(const ContentItem& item, bool hovered, bool focused) const
    {
        CartLook look;
        if (!m_cart.eligible) {
            return look;
        }
        look.inCart = m_cart.inCart && m_cart.inCart(item);
        const bool kbd = focused && m_cart.keyboardFocus && m_cart.keyboardFocus();
        look.showCheck = look.inCart || (m_cart.eligible(item) && (hovered || kbd || (m_cart.anyInCart && m_cart.anyInCart())));
        return look;
    }

    CartHooks m_cart;
};

}  // namespace Zero::Content
