// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the Resource Packs result list of the Discover page: the delegate that paints the gallery cards
// (docs/zero/CONTENT-PACKS.md, "D1 Discover > Resource Packs"). Grid only: a card is 16:9 artwork (the featured gallery image, else
// the first one, else the icon centred at 64 px on `elevated`, scaled nearest-neighbour while it is a small pixel-art icon), and under
// it the icon (32 px), name, author and a footer of pills: the resolutions (at most two and "+1"), the downloads, and at the right the
// "Installed" pill when the target instance has the pack. The provider badge sits on the artwork corner as on modpack cards. The model
// is the shared ModpackListModel.
//
// Shaders (docs/zero/SHELL.md, "Discover > Shaders") use the same card: the footer pills are the performance label (Modrinth only,
// "Lightweight", "Balanced", "High quality" or a range) and the shader platforms (Iris, OptiFine, Canvas; never "vanilla").
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "ui/zero/ZeroTokens.h"
#include "ui/zero/content/ContentDelegate.h"
#include "zero/content/ContentFilter.h"
#include "zero/content/ModpackSource.h"

class QPainter;
class QPainterPath;

namespace Zero {
class RemoteImageCache;
}

namespace Zero::Content {

class PackDelegate : public ContentDelegate {
    Q_OBJECT
   public:
    using Mode = ContentDelegate::Mode;

    static constexpr int kCardMin = 260;
    static constexpr int kCardMax = 340;
    static constexpr int kSingleColumnMax = 440;  ///< one column (the details panel is open): the card may grow
    static constexpr int kGap = Tokens::spaceLg;
    static constexpr int kPad = 12;
    static constexpr int kIcon = 32;
    static constexpr int kBodyHeight = 92;  ///< below the 16:9 artwork: padding, icon row, footer
    static constexpr int kPillHeight = 18;
    static constexpr int kArtworkIcon = 64;  ///< the icon on the artwork tile when the pack has no picture
    static constexpr int kMaxResolutionPills = 2;

    explicit PackDelegate(RemoteImageCache* images, QObject* parent = nullptr) : PackDelegate(images, Kind::ResourcePack, parent) {}
    /// `kind` is Kind::ResourcePack (resolution pills), Kind::Shader (performance + platform pills) or Kind::World (categories +
    /// "Made for 1.20.1").
    PackDelegate(RemoteImageCache* images, Kind kind, QObject* parent = nullptr);

    /// Cards only: the list mode is not offered for packs, a request for it is ignored.
    void setMode(Mode) override {}
    Mode mode() const override { return Mode::Grid; }
    void setDimmed(bool dimmed) override { m_dimmed = dimmed; }
    bool isDimmed() const override { return m_dimmed; }
    bool setViewportWidth(int width) override;
    int columns() const override { return m_columns; }
    QSize itemSize() const;
    QSize gridSize() const override;
    QRect itemRect(const QRect& cell) const;
    QRect cardRect(const QRect& cell) const override { return itemRect(cell); }
    /// The cart's check box: the top-left corner of the artwork, 8 px in (the provider badge moves right of it).
    QRect checkRect(const QRect& cell) const override;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    bool helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option, const QModelIndex& index) override;

    RemoteImageCache* images() const { return m_images; }

    /// The resolution pills of an item: the tags of its categories in canonical order, at most kMaxResolutionPills and then "+N"
    /// ("16x", "32x", "+1"). Empty when the provider sent no resolution data (CurseForge).
    static QStringList resolutionPills(const ContentItem& item);
    /// The pills of a shader card: the performance label (Modrinth items with performance tags only), then the platforms.
    static QStringList shaderPills(const ContentItem& item);
    /// The footer pills of `item` for this delegate's kind.
    QStringList pillsFor(const ContentItem& item) const;

   private:
    void paintCard(QPainter* painter, const QRect& rect, const ContentItem& item, bool installed, bool selected, bool hovered,
                   const CartLook& cart) const;
    void paintArtwork(QPainter* painter, const QRect& art, const QPainterPath& clip, const ContentItem& item) const;
    void paintIcon(QPainter* painter, const QRect& rect, const ContentItem& item) const;

    RemoteImageCache* m_images;
    Kind m_kind;
    bool m_dimmed = false;
    int m_columns = 1;
    int m_viewportWidth = 0;
    int m_cardWidth = kCardMin;
    int m_cellWidth = kCardMin + kGap;
};

}  // namespace Zero::Content
