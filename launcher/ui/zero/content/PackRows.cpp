// SPDX-License-Identifier: GPL-3.0-only
#include "PackRows.h"

#include <QAbstractItemView>
#include <QHelpEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include <algorithm>

#include "ContentDraw.h"
#include "ModpackRows.h"
#include "RemoteImageCache.h"
#include "ui/zero/PillTag.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "zero/content/WorldInstall.h"

namespace Zero::Content {

namespace {
using Draw::drawCover;
using Draw::paintCartCheck;
using Draw::paintOverlayPill;

constexpr int kPillGap = 6;
constexpr int kDownloadIcon = 12;
constexpr int kDownloadGap = 4;
}  // namespace

PackDelegate::PackDelegate(RemoteImageCache* images, Kind kind, QObject* parent) : ContentDelegate(parent), m_images(images), m_kind(kind) {}

QStringList PackDelegate::resolutionPills(const ContentItem& item)
{
    const QStringList all = resolutionsOf(item.categoryIds);
    QStringList out = all.mid(0, kMaxResolutionPills);
    if (all.size() > kMaxResolutionPills) {
        out.append(QStringLiteral("+%1").arg(all.size() - kMaxResolutionPills));
    }
    return out;
}

QStringList PackDelegate::shaderPills(const ContentItem& item)
{
    QStringList out;
    if (item.provider == Provider::Modrinth) {
        if (const QString performance = performanceLabel(item.categoryIds); !performance.isEmpty()) {
            out.append(performance);
        }
    }
    out.append(shaderPlatformLabels(item.loaderNames));
    return out;
}

QStringList PackDelegate::pillsFor(const ContentItem& item) const
{
    if (m_kind == Kind::World) {
        return worldPills(item);  // up to two categories and "Made for 1.20.1"
    }
    return m_kind == Kind::Shader ? shaderPills(item) : resolutionPills(item);
}

bool PackDelegate::setViewportWidth(int width)
{
    int columns = 1;
    int card = kCardMin;
    int cell = kCardMin + kGap;
    if (width > 0) {
        columns = std::max(1, (width + kGap) / (kCardMin + kGap));
        cell = width / columns;
        // One column (the details panel is open on a small window): a wider card, so the strip of free space stays small.
        card = std::min(columns == 1 ? kSingleColumnMax : kCardMax, (width - (columns - 1) * kGap) / columns);
    }
    const bool changed = columns != m_columns || card != m_cardWidth || cell != m_cellWidth || width != m_viewportWidth;
    m_viewportWidth = width;
    m_columns = columns;
    m_cardWidth = card;
    m_cellWidth = cell;
    return changed;
}

QSize PackDelegate::itemSize() const
{
    return { m_cardWidth, m_cardWidth * 9 / 16 + kBodyHeight };
}

QSize PackDelegate::gridSize() const
{
    const QSize item = itemSize();
    return { m_cellWidth, item.height() + kGap };
}

QRect PackDelegate::checkRect(const QRect& cell) const
{
    if (!hasCart()) {
        return {};
    }
    const QRect rect = itemRect(cell);
    return QRect(rect.left() + kCheckInset, rect.top() + kCheckInset, kCheckSize, kCheckSize);
}

QRect PackDelegate::itemRect(const QRect& cell) const
{
    // Columns are spread evenly: the first flush left, the last flush right (when the cards are not capped in width).
    const int column = m_cellWidth > 0 ? qRound(double(cell.left()) / m_cellWidth) : 0;
    const int shift = column * (m_cardWidth + kGap - m_cellWidth);
    return QRect(cell.topLeft() + QPoint(shift, 0), itemSize());
}

QSize PackDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const
{
    return gridSize();
}

bool PackDelegate::helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem&, const QModelIndex& index)
{
    if (event->type() != QEvent::ToolTip || !index.isValid()) {
        return false;
    }
    const auto item = index.data(ModpackListModel::ItemRole).value<ModpackItemPtr>();
    if (!item) {
        return false;
    }
    QString text = item->name;
    if (!item->author.isEmpty()) {
        text += QStringLiteral("\n") + tr("by %1").arg(item->author);
    }
    if (!item->summary.isEmpty()) {
        text += QStringLiteral("\n\n") + item->summary;
    }
    QToolTip::showText(event->globalPos(), text, view);
    return true;
}

void PackDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const auto item = index.data(ModpackListModel::ItemRole).value<ModpackItemPtr>();
    if (!item) {
        return;
    }
    const bool installed = index.data(ModpackListModel::InstalledRole).toBool();
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    const CartLook cart = cartLook(*item, hovered, option.state & QStyle::State_HasFocus);
    const QRect rect = itemRect(option.rect);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (m_dimmed) {
        painter->setOpacity(painter->opacity() * 0.5);
    }
    paintCard(painter, rect, *item, installed, selected, hovered, cart);
    if (cart.showCheck) {
        paintCartCheck(painter, checkRect(option.rect), cart.inCart);
    }
    // A keyboard focus ring (selection alone already shows the accent border).
    if ((option.state & QStyle::State_HasFocus) && selected) {
        painter->setPen(QPen(Theme::current().accent2.isValid() ? Theme::current().accent2 : Theme::current().accent, 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(Paint::panelPath(QRectF(rect).adjusted(-1.5, -1.5, 1.5, 1.5), Theme::current().radiusLg + 1));
    }
    painter->restore();
}

void PackDelegate::paintArtwork(QPainter* painter, const QRect& art, const QPainterPath& clip, const ContentItem& item) const
{
    const auto& t = Theme::current();
    const qreal dpr = painter->device()->devicePixelRatioF();
    painter->save();
    painter->setClipPath(clip, Qt::IntersectClip);
    painter->setClipRect(art, Qt::IntersectClip);

    QLinearGradient base(art.topLeft(), art.bottomRight());
    base.setColorAt(0, t.elevated);
    base.setColorAt(1, t.higher);
    painter->fillRect(art, base);

    QPixmap picture;
    qreal fade = 0.0;
    const bool hasArtwork = !item.artworkUrl.isEmpty() && m_images && !m_images->hasFailed(item.artworkUrl);
    if (hasArtwork) {
        picture = m_images->request(item.artworkUrl, RemoteImageCache::bucketFor(art.width(), dpr), const_cast<PackDelegate*>(this));
        fade = picture.isNull() ? 0.0 : m_images->fadeIn(item.artworkUrl);
    }
    if (picture.isNull() || fade < 1.0) {
        // No picture (or it is still arriving): the pack icon, centred at 64 px; a small pixel-art icon is scaled nearest-neighbour.
        painter->save();
        painter->setOpacity(painter->opacity() * (1.0 - fade));
        const QRect big(art.center().x() - kArtworkIcon / 2, art.center().y() - kArtworkIcon / 2, kArtworkIcon, kArtworkIcon);
        QPixmap icon;
        if (!item.iconUrl.isEmpty() && m_images && !m_images->hasFailed(item.iconUrl)) {
            icon = m_images->request(item.iconUrl, RemoteImageCache::bucketFor(kArtworkIcon, dpr), const_cast<PackDelegate*>(this));
        }
        if (!icon.isNull()) {
            painter->save();
            painter->setClipPath(Paint::panelPath(QRectF(big), t.radiusMd), Qt::IntersectClip);
            if (icon.width() <= kArtworkIcon) {
                painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
            }
            painter->drawPixmap(big, icon);
            painter->restore();
        } else {
            Icons::icon(m_kind == Kind::World ? QStringLiteral("earth") : QStringLiteral("package"), t.textTertiary)
                .paint(painter, QRect(art.center().x() - 16, art.center().y() - 16, 32, 32));
        }
        painter->restore();
    }
    if (!picture.isNull()) {
        painter->save();
        painter->setOpacity(painter->opacity() * Motion::easeEnter().valueForProgress(fade));
        drawCover(painter, art, picture);
        painter->restore();
    }
    painter->restore();
}

void PackDelegate::paintIcon(QPainter* painter, const QRect& rect, const ContentItem& item) const
{
    const auto& t = Theme::current();
    const qreal dpr = painter->device()->devicePixelRatioF();
    painter->save();
    const QPainterPath shape = Paint::panelPath(QRectF(rect), t.radiusMd);
    painter->setPen(Qt::NoPen);
    painter->setBrush(t.elevated);
    painter->drawPath(shape);
    QPixmap pixmap;
    qreal fade = 0.0;
    if (!item.iconUrl.isEmpty() && m_images && !m_images->hasFailed(item.iconUrl)) {
        pixmap = m_images->request(item.iconUrl, RemoteImageCache::bucketFor(rect.width(), dpr), const_cast<PackDelegate*>(this));
        fade = pixmap.isNull() ? 0.0 : m_images->fadeIn(item.iconUrl);
    }
    if (pixmap.isNull() || fade < 1.0) {
        painter->setPen(QPen(t.border, 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(Paint::panelPath(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusMd));
        painter->save();
        painter->setOpacity(painter->opacity() * (1.0 - fade));
        Icons::icon(m_kind == Kind::World ? QStringLiteral("earth") : QStringLiteral("package"), t.textTertiary)
            .paint(painter, QRect(rect.center().x() - 8, rect.center().y() - 8, 16, 16));
        painter->restore();
    }
    if (!pixmap.isNull()) {
        painter->setOpacity(painter->opacity() * Motion::easeEnter().valueForProgress(fade));
        painter->setClipPath(shape);
        if (pixmap.width() <= kIcon * 2) {
            painter->setRenderHint(QPainter::SmoothPixmapTransform, false);  // pixel-art icons stay crisp
        }
        drawCover(painter, rect, pixmap);
    }
    painter->restore();
}

void PackDelegate::paintCard(QPainter* p, const QRect& rect, const ContentItem& item, bool installed, bool selected, bool hovered,
                             const CartLook& cart) const
{
    const auto& t = Theme::current();
    const int artHeight = rect.width() * 9 / 16;
    const QColor fill = selected ? t.elevated : hovered ? t.surfaceHover : t.surface;
    const qreal inset = cart.inCart ? 1.0 : 0.5;  // a ticked card has a 2 px accent border, inside the rect
    const QPainterPath shape = Paint::panelPath(QRectF(rect).adjusted(inset, inset, -inset, -inset), t.radiusLg);

    p->setPen(Qt::NoPen);
    p->setBrush(fill);
    p->drawPath(shape);
    paintArtwork(p, QRect(rect.left(), rect.top(), rect.width(), artHeight), shape, item);
    p->setPen(QPen(cart.inCart || selected ? t.accent : hovered ? t.borderStrong : t.border, cart.inCart ? 2 : 1));
    p->setBrush(Qt::NoBrush);
    p->drawPath(shape);

    // The provider badge sits on the artwork corner, as on the modpack cards; with a cart the check box has that corner and the badge
    // sits right of it.
    {
        const QString provider = providerBadge(item.provider);
        const int w = PillTag::widthForText(provider);
        const int left = rect.left() + kCheckInset + (hasCart() ? kCheckSize + 6 : 0);
        const int top = rect.top() + kCheckInset + (hasCart() ? (kCheckSize - kPillHeight) / 2 : 0);
        paintOverlayPill(p, QRect(left, top, w, kPillHeight), provider, false);
    }

    // Icon, name and author.
    const int left = rect.left() + kPad;
    const int top = rect.top() + artHeight + kPad;
    const QRect icon(left, top, kIcon, kIcon);
    paintIcon(p, icon, item);
    const int textLeft = icon.right() + kPad - 2;
    const int textWidth = rect.right() - kPad - textLeft;
    const QFont nameFont = Style::font(Role::Heading);
    const QFontMetrics nameMetrics(nameFont);
    const QFont captionFont = Style::font(Role::Caption);
    const QFontMetrics captionMetrics(captionFont);
    const bool hasAuthor = !item.author.isEmpty();
    const int blockHeight = nameMetrics.height() + (hasAuthor ? captionMetrics.height() : 0);
    int y = top + (kIcon - blockHeight) / 2;
    p->setFont(nameFont);
    p->setPen(t.text);
    p->drawText(QRect(textLeft, y, textWidth, nameMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                nameMetrics.elidedText(item.name, Qt::ElideRight, textWidth));
    y += nameMetrics.height();
    if (hasAuthor) {
        p->setFont(captionFont);
        p->setPen(t.textSecondary);
        p->drawText(QRect(textLeft, y, textWidth, captionMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    captionMetrics.elidedText(tr("by %1").arg(item.author), Qt::ElideRight, textWidth));
    }

    // Footer: the resolutions and the downloads at the left, the Installed pill at the right.
    const int footerY = rect.bottom() - kPad - kPillHeight + 1;
    int right = rect.right() - kPad;
    if (installed) {
        const QString text = tr("Installed");
        const int w = PillTag::widthForText(text);
        PillTag::paintPill(p, QRectF(right - w, footerY, w, kPillHeight), text, PillTag::Tone::Success);
        right -= w + kPillGap;
    }
    int x = left;
    for (const QString& text : pillsFor(item)) {
        const int w = PillTag::widthForText(text);
        if (x + w > right) {
            break;
        }
        PillTag::paintPill(p, QRectF(x, footerY, w, kPillHeight), text, PillTag::Tone::Plain);
        x += w + kPillGap;
    }
    if (item.downloads >= 0) {
        const QString count = formatCount(item.downloads);
        p->setFont(captionFont);
        const int textW = captionMetrics.horizontalAdvance(count);
        const int w = kDownloadIcon + kDownloadGap + textW;
        if (x + w <= right) {
            Icons::icon(QStringLiteral("download"), t.textSecondary).paint(p, QRect(x, footerY + (kPillHeight - kDownloadIcon) / 2, kDownloadIcon, kDownloadIcon));
            p->setPen(t.textSecondary);
            p->drawText(QRect(x + kDownloadIcon + kDownloadGap, footerY, textW + 1, kPillHeight), Qt::AlignLeft | Qt::AlignVCenter, count);
        }
    }
}

}  // namespace Zero::Content
