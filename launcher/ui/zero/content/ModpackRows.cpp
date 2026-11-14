// SPDX-License-Identifier: GPL-3.0-only
#include "ModpackRows.h"

#include <QAbstractItemView>
#include <QHelpEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QTextLayout>
#include <QToolTip>

#include <algorithm>

#include "ContentDraw.h"
#include "RemoteImageCache.h"
#include "ui/zero/PillTag.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero::Content {

// ---- model ----

QVariant ModpackListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visible.size()) {
        return {};
    }
    const ModpackItemPtr& item = m_visible.at(index.row());
    switch (role) {
        case Qt::DisplayRole:
            return item->name;
        case Qt::AccessibleTextRole:
            return QObject::tr("%1 by %2, %3").arg(item->name, item->author.isEmpty() ? providerBadge(item->provider) : item->author,
                                                    providerBadge(item->provider));
        case ItemRole:
            return QVariant::fromValue(item);
        case InstalledRole:
            return isInstalled(*item);
        case KeyRole:
            return item->key();
        default:
            return {};
    }
}

void ModpackListModel::reset(const QList<ModpackItemPtr>& items)
{
    beginResetModel();
    m_all.clear();
    m_visible.clear();
    m_keys.clear();
    for (const auto& item : items) {
        if (!item || m_keys.contains(item->key())) {
            continue;
        }
        m_keys.insert(item->key());
        m_all.append(item);
        if (accepts(*item)) {
            m_visible.append(item);
        }
    }
    endResetModel();
}

int ModpackListModel::append(const QList<ModpackItemPtr>& items)
{
    QList<ModpackItemPtr> shown;
    for (const auto& item : items) {
        if (!item || m_keys.contains(item->key())) {
            continue;
        }
        m_keys.insert(item->key());
        m_all.append(item);
        if (accepts(*item)) {
            shown.append(item);
        }
    }
    if (shown.isEmpty()) {
        return 0;
    }
    beginInsertRows({}, int(m_visible.size()), int(m_visible.size() + shown.size() - 1));
    m_visible.append(shown);
    endInsertRows();
    return int(shown.size());
}

void ModpackListModel::setClientFilter(std::function<bool(const ModpackItem&)> filter)
{
    m_filter = std::move(filter);
    QList<ModpackItemPtr> now;
    for (const auto& item : std::as_const(m_all)) {
        if (accepts(*item)) {
            now.append(item);
        }
    }
    if (now == m_visible) {
        return;
    }
    beginResetModel();
    m_visible = now;
    endResetModel();
}

void ModpackListModel::setInstalledKeys(const QSet<QString>& keys)
{
    if (keys == m_installed) {
        return;
    }
    const QSet<QString> before = m_installed;
    m_installed = keys;
    for (int row = 0; row < m_visible.size(); ++row) {
        const QString key = installedKey(*m_visible.at(row));
        if (before.contains(key) != keys.contains(key)) {
            emit dataChanged(index(row), index(row), { InstalledRole });
        }
    }
}

void ModpackListModel::itemChanged(const ModpackItemPtr& item)
{
    const int row = rowOf(item);
    if (row >= 0) {
        emit dataChanged(index(row), index(row));
    }
}

int ModpackListModel::rowOf(const ModpackItemPtr& item) const
{
    for (int row = 0; row < m_visible.size(); ++row) {
        if (m_visible.at(row) == item) {
            return row;
        }
    }
    return -1;
}

int ModpackListModel::rowOfKey(const QString& key) const
{
    for (int row = 0; row < m_visible.size(); ++row) {
        if (m_visible.at(row)->key() == key) {
            return row;
        }
    }
    return -1;
}

// ---- delegate ----

namespace {
using Draw::drawCover;
using Draw::paintOverlayPill;
using Draw::wrapLines;

constexpr int kPillHeight = 18;
constexpr int kPillGap = 6;

struct PillItem {
    QString text;
    PillTag::Tone tone = PillTag::Tone::Plain;
};

QList<PillItem> factPills(const ModpackItem& item)
{
    QList<PillItem> pills;
    if (const QString range = minecraftRangeText(item.gameVersions); !range.isEmpty()) {
        pills.append({ range, PillTag::Tone::Plain });
    }
    const QStringList loaders = loaderLabels(item.loaderNames);
    for (int i = 0; i < loaders.size() && i < 2; ++i) {
        pills.append({ loaders.at(i), PillTag::Tone::Plain });
    }
    return pills;
}

}  // namespace

ModpackDelegate::ModpackDelegate(RemoteImageCache* images, QObject* parent) : ContentDelegate(parent), m_images(images) {}

void ModpackDelegate::setMode(Mode mode)
{
    if (mode == m_mode) {
        return;
    }
    m_mode = mode;
    setViewportWidth(m_viewportWidth);
}

bool ModpackDelegate::setViewportWidth(int width)
{
    int columns = 1;
    int card = kCardMin;
    int cell = kCardMin + kGap;
    if (width > 0) {
        if (m_mode == Mode::Grid) {
            columns = std::max(1, (width + kGap) / (kCardMin + kGap));
            cell = width / columns;
            // One column (the details panel is open on a small window): a wider card, so the strip of free space stays small.
            card = std::min(columns == 1 ? kSingleColumnMax : kCardMax, (width - (columns - 1) * kGap) / columns);
        } else {
            columns = 1;
            cell = width;
            card = width;
        }
    }
    const bool changed = columns != m_columns || card != m_cardWidth || cell != m_cellWidth || width != m_viewportWidth;
    m_viewportWidth = width;
    m_columns = columns;
    m_cardWidth = card;
    m_cellWidth = cell;
    return changed;
}

QSize ModpackDelegate::itemSize() const
{
    if (m_mode == Mode::List) {
        return { m_cardWidth, kRowHeight };
    }
    return { m_cardWidth, m_cardWidth * 9 / 16 + kCardBodyHeight };
}

QSize ModpackDelegate::gridSize() const
{
    const QSize item = itemSize();
    return { m_cellWidth, item.height() + (m_mode == Mode::List ? kRowGap : kGap) };
}

QRect ModpackDelegate::itemRect(const QRect& cell) const
{
    // Columns are spread evenly: the first flush left, the last flush right (when the cards are not capped in width).
    const int column = m_cellWidth > 0 ? qRound(double(cell.left()) / m_cellWidth) : 0;
    const int shift = column * (m_cardWidth + kGap - m_cellWidth);
    return QRect(cell.topLeft() + QPoint(shift, 0), itemSize());
}

QSize ModpackDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const
{
    return gridSize();
}

bool ModpackDelegate::helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem&, const QModelIndex& index)
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

void ModpackDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const auto item = index.data(ModpackListModel::ItemRole).value<ModpackItemPtr>();
    if (!item) {
        return;
    }
    const bool installed = index.data(ModpackListModel::InstalledRole).toBool();
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    const QRect rect = itemRect(option.rect);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (m_dimmed) {
        painter->setOpacity(painter->opacity() * 0.5);
    }
    if (m_mode == Mode::Grid) {
        paintCard(painter, rect, *item, installed, selected, hovered);
    } else {
        paintRow(painter, rect, *item, installed, selected, hovered);
    }
    // A keyboard focus ring (selection alone already shows the accent border).
    if ((option.state & QStyle::State_HasFocus) && selected) {
        painter->setPen(QPen(Theme::current().accent2.isValid() ? Theme::current().accent2 : Theme::current().accent, 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(Paint::panelPath(QRectF(rect).adjusted(-1.5, -1.5, 1.5, 1.5), Theme::current().radiusLg + 1));
    }
    painter->restore();
}

void ModpackDelegate::paintArtwork(QPainter* painter, const QRect& art, const QPainterPath& clip, const ModpackItem& item) const
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
        picture = m_images->request(item.artworkUrl, RemoteImageCache::bucketFor(art.width(), dpr), const_cast<ModpackDelegate*>(this));
        fade = picture.isNull() ? 0.0 : m_images->fadeIn(item.artworkUrl);
    }
    if (picture.isNull() || fade < 1.0) {
        // No artwork (or it is still arriving): the pack icon on the tile, 96 px, else a package glyph.
        painter->save();
        painter->setOpacity(painter->opacity() * (1.0 - fade));
        const QRect big(art.center().x() - 48, art.center().y() - 48, 96, 96);
        QPixmap icon;
        if (!item.iconUrl.isEmpty() && m_images && !m_images->hasFailed(item.iconUrl)) {
            icon = m_images->request(item.iconUrl, RemoteImageCache::bucketFor(96, dpr), const_cast<ModpackDelegate*>(this));
        }
        if (!icon.isNull()) {
            painter->save();
            painter->setClipPath(Paint::panelPath(QRectF(big), t.radiusLg), Qt::IntersectClip);
            drawCover(painter, big, icon);
            painter->restore();
        } else {
            const QIcon glyph = Icons::icon(QStringLiteral("package"), t.textTertiary);
            glyph.paint(painter, QRect(art.center().x() - 16, art.center().y() - 16, 32, 32));
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

void ModpackDelegate::paintIcon(QPainter* painter, const QRect& rect, const ModpackItem& item, const QColor& ring) const
{
    const auto& t = Theme::current();
    const qreal dpr = painter->device()->devicePixelRatioF();
    painter->save();
    // A ring in the card's color: the icon overlaps the artwork.
    painter->setPen(Qt::NoPen);
    painter->setBrush(ring);
    painter->drawPath(Paint::panelPath(QRectF(rect).adjusted(-3, -3, 3, 3), t.radiusMd + 2));
    const QPainterPath shape = Paint::panelPath(QRectF(rect), t.radiusMd);
    painter->setBrush(t.elevated);
    painter->drawPath(shape);
    QPixmap pixmap;
    qreal fade = 0.0;
    if (!item.iconUrl.isEmpty() && m_images && !m_images->hasFailed(item.iconUrl)) {
        pixmap = m_images->request(item.iconUrl, RemoteImageCache::bucketFor(rect.width(), dpr), const_cast<ModpackDelegate*>(this));
        fade = pixmap.isNull() ? 0.0 : m_images->fadeIn(item.iconUrl);
    }
    if (pixmap.isNull() || fade < 1.0) {
        painter->setPen(QPen(t.border, 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(Paint::panelPath(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusMd));
        painter->save();
        painter->setOpacity(painter->opacity() * (1.0 - fade));
        Icons::icon(QStringLiteral("package"), t.textTertiary)
            .paint(painter, QRect(rect.center().x() - 10, rect.center().y() - 10, 20, 20));
        painter->restore();
    }
    if (!pixmap.isNull()) {
        painter->setOpacity(painter->opacity() * Motion::easeEnter().valueForProgress(fade));
        painter->setClipPath(shape);
        drawCover(painter, rect, pixmap);
    }
    painter->restore();
}

void ModpackDelegate::paintCard(QPainter* p, const QRect& rect, const ModpackItem& item, bool installed, bool selected, bool hovered) const
{
    const auto& t = Theme::current();
    const int artHeight = rect.width() * 9 / 16;
    const QColor fill = selected ? t.elevated : hovered ? t.surfaceHover : t.surface;
    const QPainterPath shape = Paint::panelPath(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusLg);

    p->setPen(Qt::NoPen);
    p->setBrush(fill);
    p->drawPath(shape);
    paintArtwork(p, QRect(rect.left(), rect.top(), rect.width(), artHeight), shape, item);
    p->setPen(QPen(selected ? t.accent : hovered ? t.borderStrong : t.border, 1));
    p->setBrush(Qt::NoBrush);
    p->drawPath(shape);

    // Provider badge and Installed sit on the artwork corners, so the footer never runs out of room.
    {
        const QString provider = providerBadge(item.provider);
        const int w = PillTag::widthForText(provider);
        paintOverlayPill(p, QRect(rect.left() + 8, rect.top() + 8, w, kPillHeight), provider, false);
        if (installed) {
            const QString text = tr("Installed");
            const int iw = PillTag::widthForText(text);
            paintOverlayPill(p, QRect(rect.right() - 8 - iw, rect.top() + 8, iw, kPillHeight), text, true);
        }
    }

    paintIcon(p, QRect(rect.left() + kPad, rect.top() + artHeight - kIcon / 2, kIcon, kIcon), item, fill);

    const int textWidth = rect.width() - 2 * kPad;
    const int left = rect.left() + kPad;
    int y = rect.top() + artHeight + kIcon / 2 + 6;

    const QFont nameFont = Style::font(Role::Heading);
    p->setFont(nameFont);
    p->setPen(t.text);
    const QFontMetrics nameMetrics(nameFont);
    p->drawText(QRect(left, y, textWidth, nameMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                nameMetrics.elidedText(item.name, Qt::ElideRight, textWidth));
    y += nameMetrics.height() + 2;

    const QFont captionFont = Style::font(Role::Caption);
    const QFontMetrics captionMetrics(captionFont);
    if (!item.author.isEmpty()) {
        p->setFont(captionFont);
        p->setPen(t.textSecondary);
        p->drawText(QRect(left, y, textWidth, captionMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    captionMetrics.elidedText(tr("by %1").arg(item.author), Qt::ElideRight, textWidth));
    }
    y += captionMetrics.height() + 6;

    const QFont bodyFont = Style::font(Role::Body);
    const QFontMetrics bodyMetrics(bodyFont);
    p->setFont(bodyFont);
    p->setPen(t.textSecondary);
    const QStringList lines = wrapLines(item.summary, bodyFont, textWidth, 2);
    for (int i = 0; i < lines.size(); ++i) {
        p->drawText(QRect(left, y + i * bodyMetrics.lineSpacing(), textWidth, bodyMetrics.lineSpacing()), Qt::AlignLeft | Qt::AlignVCenter,
                    lines.at(i));
    }

    // Footer: Minecraft range and loaders at the left, downloads at the right.
    const int footerY = rect.bottom() - kPad - kPillHeight + 1;
    int right = rect.right() - kPad;
    int minLeftOfDownloads = right;
    if (item.downloads >= 0) {
        const QString count = formatCount(item.downloads);
        p->setFont(captionFont);
        const int textW = captionMetrics.horizontalAdvance(count);
        p->setPen(t.textSecondary);
        p->drawText(QRect(right - textW, footerY, textW, kPillHeight), Qt::AlignRight | Qt::AlignVCenter, count);
        Icons::icon(QStringLiteral("download"), t.textSecondary).paint(p, QRect(right - textW - 4 - 12, footerY + (kPillHeight - 12) / 2, 12, 12));
        minLeftOfDownloads = right - textW - 4 - 12 - kPillGap;
    }
    int x = left;
    for (const auto& pill : factPills(item)) {
        const int w = PillTag::widthForText(pill.text);
        if (x + w > minLeftOfDownloads) {
            break;
        }
        PillTag::paintPill(p, QRectF(x, footerY, w, kPillHeight), pill.text, pill.tone);
        x += w + kPillGap;
    }
}

void ModpackDelegate::paintRow(QPainter* p, const QRect& rect, const ModpackItem& item, bool installed, bool selected, bool hovered) const
{
    const auto& t = Theme::current();
    const QColor fill = selected ? t.elevated : hovered ? t.surfaceHover : t.surface;
    const QPainterPath shape = Paint::panelPath(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusLg);
    p->setPen(QPen(selected ? t.accent : hovered ? t.borderStrong : t.border, 1));
    p->setBrush(fill);
    p->drawPath(shape);

    const QRect icon(rect.left() + kPad, rect.top() + (rect.height() - kIcon) / 2, kIcon, kIcon);
    paintIcon(p, icon, item, fill);

    // Right block, built from the right edge: downloads, Installed, provider, loaders, Minecraft range.
    const QFont captionFont = Style::font(Role::Caption);
    const QFontMetrics captionMetrics(captionFont);
    const int textLeft = icon.right() + kPad + 1;
    constexpr int kMinText = 180;
    int right = rect.right() - kPad;
    const int pillY = rect.top() + (rect.height() - kPillHeight) / 2;
    auto room = [&](int w) { return right - w - kPillGap >= textLeft + kMinText; };
    if (item.downloads >= 0) {
        const QString count = formatCount(item.downloads);
        const int textW = captionMetrics.horizontalAdvance(count);
        const int w = textW + 4 + 12;
        if (room(w)) {
            p->setFont(captionFont);
            p->setPen(t.textSecondary);
            p->drawText(QRect(right - textW, pillY, textW, kPillHeight), Qt::AlignRight | Qt::AlignVCenter, count);
            Icons::icon(QStringLiteral("download"), t.textSecondary).paint(p, QRect(right - w, pillY + (kPillHeight - 12) / 2, 12, 12));
            right -= w + kPillGap + 6;
        }
    }
    QList<PillItem> pills;
    if (installed) {
        pills.append({ tr("Installed"), PillTag::Tone::Success });
    }
    pills.append({ providerBadge(item.provider), PillTag::Tone::Plain });
    pills.append(factPills(item));
    // Right to left; the first pills (Installed, provider) are the ones that matter, so drop from the end of the list.
    QList<PillItem> shown;
    int needed = 0;
    for (const auto& pill : std::as_const(pills)) {
        const int add = PillTag::widthForText(pill.text) + (shown.isEmpty() ? 0 : kPillGap);
        if (right - (needed + add) - kPad < textLeft + kMinText) {
            break;
        }
        needed += add;
        shown.append(pill);
    }
    int x = right - needed;
    for (const auto& pill : std::as_const(shown)) {
        const int w = PillTag::widthForText(pill.text);
        PillTag::paintPill(p, QRectF(x, pillY, w, kPillHeight), pill.text, pill.tone);
        x += w + kPillGap;
    }
    const int textRight = (shown.isEmpty() ? right : right - needed) - kPad;

    const int textWidth = std::max(40, textRight - textLeft);
    const QFont nameFont = Style::font(Role::Heading);
    const QFontMetrics nameMetrics(nameFont);
    p->setFont(nameFont);
    p->setPen(t.text);
    const QString author = item.author.isEmpty() ? QString() : tr("by %1").arg(item.author);
    const int authorW = author.isEmpty() ? 0 : captionMetrics.horizontalAdvance(author) + 8;
    const int nameW = std::min(nameMetrics.horizontalAdvance(item.name), std::max(60, textWidth - authorW));
    const int top = rect.top() + 9;
    p->drawText(QRect(textLeft, top, nameW, nameMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                nameMetrics.elidedText(item.name, Qt::ElideRight, nameW));
    if (!author.isEmpty() && textWidth - nameW - 8 > 40) {
        p->setFont(captionFont);
        p->setPen(t.textSecondary);
        p->drawText(QRect(textLeft + nameW + 8, top, textWidth - nameW - 8, nameMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    captionMetrics.elidedText(author, Qt::ElideRight, textWidth - nameW - 8));
    }
    const QFont bodyFont = Style::font(Role::Body);
    const QFontMetrics bodyMetrics(bodyFont);
    p->setFont(bodyFont);
    p->setPen(t.textSecondary);
    const int descTop = top + nameMetrics.height() + 1;
    const int maxLines = std::max(1, (rect.bottom() - 7 - descTop) / bodyMetrics.lineSpacing());
    const QStringList lines = wrapLines(item.summary, bodyFont, textWidth, std::min(2, maxLines));
    for (int i = 0; i < lines.size(); ++i) {
        p->drawText(QRect(textLeft, descTop + i * bodyMetrics.lineSpacing(), textWidth, bodyMetrics.lineSpacing()),
                    Qt::AlignLeft | Qt::AlignVCenter, lines.at(i));
    }
}

}  // namespace Zero::Content
