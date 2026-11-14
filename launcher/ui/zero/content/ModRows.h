// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the Mods result list of the Discover page: the delegate that paints mod cards (grid) and 56 px rows (list)
// (docs/zero/CONTENT-MODS.md, "Discover > Mods"). The model is the shared ModpackListModel. No artwork: a mod card is a 132 px
// high surface with the icon, name / author, two description lines and a footer of pills (loaders, environment, downloads) with
// the "Installed" state at the right. An item that does not fit the target instance (visible only after "Show everything") shows a
// `warning` line "Not for 1.20.1 Fabric" in place of the second description line.
#pragma once

#include <QList>
#include <QString>

#include <functional>

#include "ui/zero/PillTag.h"
#include "ui/zero/ZeroTokens.h"
#include "ui/zero/content/ContentDelegate.h"
#include "zero/content/ModpackSource.h"

class QPainter;

namespace Zero {
class RemoteImageCache;
}

namespace Zero::Content {

/// "Client", "Server", "Client & server" - only when the provider sent the environment (Modrinth's client_side / server_side);
/// empty otherwise (CurseForge, or a Modrinth project that supports neither side).
QString environmentLabel(const ContentItem& item);

class ModDelegate : public ContentDelegate {
    Q_OBJECT
   public:
    using Mode = ContentDelegate::Mode;

    static constexpr int kCardMin = 300;
    static constexpr int kCardMax = 420;
    static constexpr int kSingleColumnMax = 560;  ///< one column (the details panel is open): the card may grow so no strip stays empty
    static constexpr int kCardHeight = 132;
    static constexpr int kGap = Tokens::spaceLg;
    static constexpr int kPad = Tokens::spaceLg;
    static constexpr int kIcon = 48;
    static constexpr int kRowIcon = 32;
    static constexpr int kRowHeight = 56;
    static constexpr int kRowGap = Tokens::spaceSm;
    static constexpr int kPillHeight = 20;
    static constexpr int kPillGap = 6;

    /// One element of the footer / right block, in display order.
    struct Pill {
        enum class Kind { Loader, Environment, Downloads, Installed };
        Kind kind = Kind::Loader;
        QString text;
        int width = 0;
        PillTag::Tone tone = PillTag::Tone::Plain;
        int priority = 0;  ///< the lowest is dropped first when the row is too narrow
    };

    explicit ModDelegate(RemoteImageCache* images, QObject* parent = nullptr);

    /// The text of the warning line ("Not for 1.20.1 Fabric") for an item that does not fit the target; empty = it fits.
    void setFitCheck(std::function<QString(const ContentItem&)> check) { m_fit = std::move(check); }

    void setMode(Mode mode) override;
    void setDimmed(bool dimmed) override { m_dimmed = dimmed; }
    bool isDimmed() const override { return m_dimmed; }
    Mode mode() const override { return m_mode; }
    bool setViewportWidth(int width) override;
    int columns() const override { return m_columns; }
    QSize itemSize() const;
    QSize gridSize() const override;
    QRect itemRect(const QRect& cell) const;
    QRect cardRect(const QRect& cell) const override { return itemRect(cell); }
    /// The cart's check box: the top-left corner of a card (8 px in), left of the icon in a list row (the icon moves right by the box
    /// and a gap whenever the delegate has a cart).
    QRect checkRect(const QRect& cell) const override;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    bool helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option, const QModelIndex& index) override;

    RemoteImageCache* images() const { return m_images; }

    /// The pills an item shows, in display order, dropped (lowest priority first) until they fit `budget` px. Public for the tests.
    static QList<Pill> pillsFor(const ContentItem& item, bool installed, int budget);

   private:
    void paintCard(QPainter* painter, const QRect& rect, const ContentItem& item, bool installed, bool selected, bool hovered,
                   const CartLook& cart) const;
    void paintRow(QPainter* painter, const QRect& rect, const ContentItem& item, bool installed, bool selected, bool hovered,
                  const CartLook& cart) const;
    void paintIcon(QPainter* painter, const QRect& rect, const ContentItem& item) const;

    RemoteImageCache* m_images;
    std::function<QString(const ContentItem&)> m_fit;
    Mode m_mode = Mode::Grid;
    bool m_dimmed = false;
    int m_columns = 1;
    int m_viewportWidth = 0;
    int m_cardWidth = kCardMin;
    int m_cellWidth = kCardMin + kGap;
};

}  // namespace Zero::Content
