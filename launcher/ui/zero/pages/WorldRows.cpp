// SPDX-License-Identifier: GPL-3.0-only
#include "WorldRows.h"

#include <QAbstractItemView>
#include <QCoreApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include "WorldIcon.h"
#include "ui/zero/ZeroFormat.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {

namespace {
constexpr int kPadding = Tokens::spaceMd;   // inside the row, left and right
constexpr int kGap = Tokens::spaceMd;       // between icon, text, badges and the right column
constexpr int kBadgeGap = Tokens::spaceSm - 2;
constexpr int kBadgePad = Tokens::spaceSm;
constexpr int kDot = 6;
constexpr int kMinNameWidth = 96;

QString tr(const char* text)
{
    return QCoreApplication::translate("Zero::WorldRows", text);
}

struct Badge {
    QString text;
    QColor textColor;
    bool dot = false;
    int width = 0;
};
}  // namespace

QVariant WorldRowModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Worlds::WorldEntry& e = m_rows.at(index.row());
    switch (role) {
        case Qt::DisplayRole:
        case Qt::AccessibleTextRole:
            return e.name;
        case Qt::ToolTipRole:
            return e.name == e.folderName ? e.folderPath : QStringLiteral("%1\n%2").arg(e.name, e.folderPath);
        case KeyRole:
            return e.key();
        default:
            return {};
    }
}

void WorldRowModel::setEntries(const QList<Worlds::WorldEntry>& entries)
{
    beginResetModel();
    m_rows = entries;
    endResetModel();
}

int WorldRowModel::rowForKey(const QString& key) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).key() == key)
            return i;
    }
    return -1;
}

void WorldRowModel::setSize(const QString& key, qint64 bytes)
{
    const int row = rowForKey(key);
    if (row < 0 || m_rows.at(row).sizeBytes == bytes)
        return;
    m_rows[row].sizeBytes = bytes;
    emit dataChanged(index(row), index(row));
}

void WorldRowModel::setRunning(const QSet<QString>& instanceIds)
{
    if (instanceIds == m_running)
        return;
    m_running = instanceIds;
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(static_cast<int>(m_rows.size()) - 1));
}

QSize WorldRowDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const
{
    return { 0, kRowHeight + kRowGap };
}

void WorldRowDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const auto* model = qobject_cast<const WorldRowModel*>(index.model());
    if (!model || index.row() >= model->rowCount())
        return;
    const Worlds::WorldEntry& e = model->entry(index.row());
    const auto& t = Theme::current();
    const qreal dpr = option.widget ? option.widget->devicePixelRatioF() : 1.0;
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;

    const QRect row = QRect(option.rect.left(), option.rect.top() + kRowGap / 2, option.rect.width(), kRowHeight);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // background: hover surfaceHover, selected elevated + 1 px accent border
    if (selected || hovered) {
        const QRectF frame = QRectF(row).adjusted(0.5, 0.5, -0.5, -0.5);
        painter->setPen(selected ? QPen(t.accent, 1) : QPen(Qt::NoPen));
        painter->setBrush(selected ? t.elevated : t.surfaceHover);
        painter->drawPath(Paint::panelPath(frame, t.radiusMd));
    }

    const int centerY = row.center().y();
    const QRect inner = row.adjusted(kPadding, 0, -kPadding, 0);

    // icon tile
    const QRectF tile(inner.left(), centerY - kIconSize / 2.0 + 0.5, kIconSize, kIconSize);
    WorldIcon::paintTile(painter, tile, e.iconPath, 20, dpr);

    // right column: last played over size
    const QFont captionFont = Style::font(Role::Caption);
    const QFontMetrics captionMetrics(captionFont);
    const QString played = Format::relativeTime(e.lastPlayedMs);
    const QString size = e.sizeBytes >= 0 ? Format::bytes(e.sizeBytes) : QStringLiteral("…");
    const int rightWidth = qMax(captionMetrics.horizontalAdvance(played), captionMetrics.horizontalAdvance(size));
    const int rightLeft = inner.right() + 1 - rightWidth;
    const int lineHeight = captionMetrics.height();
    painter->setFont(captionFont);
    painter->setPen(t.textTertiary);
    painter->drawText(QRect(rightLeft, centerY - lineHeight - 1, rightWidth, lineHeight), Qt::AlignRight | Qt::AlignVCenter, played);
    painter->drawText(QRect(rightLeft, centerY + 1, rightWidth, lineHeight), Qt::AlignRight | Qt::AlignVCenter, size);

    // badges (right-aligned, just before the right column): In use, Hardcore, game mode (not Survival)
    QList<Badge> badges;
    if (model->isRunning(e.instanceId))
        badges.append({ tr("In use"), t.text, true, 0 });
    if (e.hardcore)
        badges.append({ tr("Hardcore"), t.danger, false, 0 });
    if (e.gameTypeCode > 0 && !e.gameMode.isEmpty())  // Creative, Adventure, Spectator
        badges.append({ e.gameMode, t.textSecondary, false, 0 });

    const int nameLeft = int(tile.right()) + 1 + kGap;
    const int badgesRight = rightLeft - kGap;
    const int available = badgesRight - nameLeft;
    int badgesLeft = badgesRight;
    QList<Badge> shown;
    for (Badge b : std::as_const(badges)) {  // most important first; drop the ones that would squeeze the name
        b.width = captionMetrics.horizontalAdvance(b.text) + 2 * kBadgePad + (b.dot ? kDot + Tokens::spaceXs : 0);
        const int needed = (badgesRight - badgesLeft) + b.width + (shown.isEmpty() ? 0 : kBadgeGap);
        if (available - needed < kMinNameWidth)
            break;
        badgesLeft = badgesRight - needed;
        shown.append(b);
    }
    int x = badgesRight;
    const int badgeHeight = Tokens::badgeHeight;
    for (const Badge& b : std::as_const(shown)) {
        const QRectF pill(x - b.width + 0.5, centerY - badgeHeight / 2.0 + 0.5, b.width - 1, badgeHeight - 1);
        painter->setPen(QPen(t.border, 1));
        painter->setBrush(t.surface);
        painter->drawPath(Paint::panelPath(pill, t.radiusSm));
        qreal textLeft = pill.left() + kBadgePad;
        if (b.dot) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(t.success);
            painter->drawEllipse(QRectF(textLeft, pill.center().y() - kDot / 2.0, kDot, kDot));
            textLeft += kDot + Tokens::spaceXs;
        }
        painter->setPen(b.textColor);
        painter->drawText(QRectF(textLeft, pill.top(), pill.right() - textLeft, pill.height()), Qt::AlignLeft | Qt::AlignVCenter, b.text);
        x -= b.width + kBadgeGap;
    }

    // name + "<instance> · <version>"
    QFont nameFont = Style::font(Role::Label);
    nameFont.setWeight(QFont::DemiBold);
    const QFontMetrics nameMetrics(nameFont);
    const int textWidth = (shown.isEmpty() ? badgesRight : badgesLeft - kGap) - nameLeft;
    const int blockHeight = nameMetrics.height() + 2 + lineHeight;
    const int top = centerY - blockHeight / 2;
    painter->setFont(nameFont);
    painter->setPen(t.text);
    painter->drawText(QRect(nameLeft, top, textWidth, nameMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                      nameMetrics.elidedText(e.name, Qt::ElideRight, textWidth));
    const QString meta = e.versionName.isEmpty() ? e.instanceName : QStringLiteral("%1 · %2").arg(e.instanceName, e.versionName);
    painter->setFont(captionFont);
    painter->setPen(t.textSecondary);
    painter->drawText(QRect(nameLeft, top + nameMetrics.height() + 2, textWidth, lineHeight), Qt::AlignLeft | Qt::AlignVCenter,
                      captionMetrics.elidedText(meta, Qt::ElideRight, textWidth));
    painter->restore();
}

}  // namespace Zero
