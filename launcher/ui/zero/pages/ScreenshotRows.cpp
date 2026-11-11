// SPDX-License-Identifier: GPL-3.0-only
#include "ScreenshotRows.h"
#include "ui/zero/ZeroMotion.h"

#include <QAbstractItemView>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include "ThumbnailCache.h"
#include "ui/zero/ZeroFormat.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {

namespace {
constexpr int kPad = ScreenshotDelegate::kPad;  // inside the tile, around the thumbnail and the caption
constexpr int kCaptionGap = Tokens::spaceSm;
constexpr int kLineGap = 2;
constexpr int kPlaceholderIcon = 28;

QFont labelFont()
{
    return Style::font(Role::Label);
}
}  // namespace

QVariant ScreenshotModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Gallery::ScreenshotEntry& e = m_rows.at(index.row());
    switch (role) {
        case Qt::DisplayRole:
        case Qt::AccessibleTextRole:
            return e.fileName;
        case Qt::ToolTipRole:
            return QStringLiteral("%1\n%2").arg(e.fileName, e.instanceName);
        case PathRole:
            return e.path;
        default:
            return {};
    }
}

void ScreenshotModel::setEntries(const QList<Gallery::ScreenshotEntry>& entries)
{
    beginResetModel();
    m_rows = entries;
    endResetModel();
}

int ScreenshotModel::rowForPath(const QString& path) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).path == path)
            return i;
    }
    return -1;
}

bool ScreenshotDelegate::setViewportWidth(int width)
{
    const int minTile = kMinThumbWidth + 2 * kPad;
    const int columns = qMax(1, (width + kGap) / (minTile + kGap));
    const int cell = width > 0 ? width / columns : minTile;
    const int tile = width > 0 ? (width - (columns - 1) * kGap) / columns : minTile;
    const int thumb = qMax(kMinThumbWidth, tile - 2 * kPad);
    const bool changed = columns != m_columns || thumb != m_thumbWidth || cell != m_cellWidth;
    m_columns = columns;
    m_thumbWidth = thumb;
    m_cellWidth = cell;
    return changed;
}

QSize ScreenshotDelegate::tileSize() const
{
    const int labelH = QFontMetrics(labelFont()).height();
    const int captionH = QFontMetrics(Style::font(Role::Caption)).height();
    const int thumbH = m_thumbWidth * 9 / 16;
    return { m_thumbWidth + 2 * kPad, kPad + thumbH + kCaptionGap + labelH + kLineGap + captionH + kPad };
}

QSize ScreenshotDelegate::gridSize() const
{
    return { qMax(m_cellWidth, 1), tileSize().height() + kGap };
}

QSize ScreenshotDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const
{
    return gridSize();
}

void ScreenshotDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const auto* model = qobject_cast<const ScreenshotModel*>(index.model());
    if (!model || index.row() >= model->rowCount())
        return;
    const Gallery::ScreenshotEntry& e = model->entry(index.row());
    const auto& t = Theme::current();
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;

    // column of this cell -> shift inside the cell so the columns are evenly spaced and the last one is flush right
    const int column = m_cellWidth > 0 ? qRound(double(option.rect.left()) / m_cellWidth) : 0;
    const int shift = m_columns > 1 ? column * kGap / m_columns : 0;
    const QRect tile(option.rect.topLeft() + QPoint(shift, 0), tileSize());
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    // card: hover surfaceHover, selected elevated + 1 px accent border (like the Worlds rows)
    if (selected || hovered) {
        const QRectF frame = QRectF(tile).adjusted(0.5, 0.5, -0.5, -0.5);
        painter->setPen(selected ? QPen(t.accent, 1) : QPen(Qt::NoPen));
        painter->setBrush(selected ? t.elevated : t.surfaceHover);
        painter->drawPath(Paint::panelPath(frame, t.radiusMd));
    }

    // thumbnail (cover-cropped to 16:9) or the neutral placeholder
    const int thumbWidth = m_thumbWidth;
    const int thumbHeight = m_thumbWidth * 9 / 16;
    const QRect thumb(tile.left() + kPad, tile.top() + kPad, thumbWidth, thumbHeight);
    const QPainterPath clip = Paint::panelPath(QRectF(thumb), t.radiusMd);
    const QPixmap pixmap = m_cache ? m_cache->thumbnail(e.path, e.modifiedMs) : QPixmap();
    const qreal fade = pixmap.isNull() ? 0.0 : m_cache->fadeIn(e.path, e.modifiedMs);
    if (pixmap.isNull() || fade < 1.0) {
        // placeholder (also under a thumbnail that is still fading in)
        painter->setPen(QPen(t.border, 1));
        painter->setBrush(t.surface);
        painter->drawPath(Paint::panelPath(QRectF(thumb).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusMd));
        const QIcon icon = Icons::icon(QStringLiteral("image"), t.textTertiary);
        const QRect iconRect(thumb.center().x() - kPlaceholderIcon / 2, thumb.center().y() - kPlaceholderIcon / 2, kPlaceholderIcon,
                             kPlaceholderIcon);
        painter->save();
        painter->setOpacity(painter->opacity() * (1.0 - fade));
        icon.paint(painter, iconRect);
        painter->restore();
    }
    if (!pixmap.isNull()) {
        painter->save();
        painter->setOpacity(painter->opacity() * Motion::easeEnter().valueForProgress(fade));
        painter->setClipPath(clip);
        const QSizeF source = pixmap.size();
        QSizeF cropped = source;
        const qreal want = qreal(thumbWidth) / thumbHeight;
        if (source.width() / source.height() > want)
            cropped.setWidth(source.height() * want);
        else
            cropped.setHeight(source.width() / want);
        const QRectF from((source.width() - cropped.width()) / 2, (source.height() - cropped.height()) / 2, cropped.width(), cropped.height());
        painter->drawPixmap(QRectF(thumb), pixmap, from);
        painter->restore();
    }

    // caption: instance name over the relative date
    const QFont label = labelFont();
    const QFont caption = Style::font(Role::Caption);
    const QFontMetrics labelMetrics(label);
    const QFontMetrics captionMetrics(caption);
    const int textLeft = thumb.left();
    const int textWidth = thumbWidth;
    const int labelTop = thumb.bottom() + 1 + kCaptionGap;
    painter->setFont(label);
    painter->setPen(t.text);
    painter->drawText(QRect(textLeft, labelTop, textWidth, labelMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                      labelMetrics.elidedText(e.instanceName, Qt::ElideRight, textWidth));
    painter->setFont(caption);
    painter->setPen(t.textSecondary);
    painter->drawText(QRect(textLeft, labelTop + labelMetrics.height() + kLineGap, textWidth, captionMetrics.height()),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      captionMetrics.elidedText(Format::relativeTime(e.modifiedMs), Qt::ElideRight, textWidth));
    painter->restore();
}

}  // namespace Zero
