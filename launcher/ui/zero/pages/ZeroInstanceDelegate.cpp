// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroInstanceDelegate.h"

#include <QFontMetrics>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QTextLayout>
#include "BaseInstance.h"
#include "InstanceInfo.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {

namespace {

/// Splits `text` into at most `maxLines` lines that fit `width`; the last line is elided when text remains.
QStringList wrapElided(const QString& text, const QFont& font, int width, int maxLines)
{
    QStringList lines;
    if (text.isEmpty() || width <= 0)
        return lines;
    const QFontMetrics fm(font);
    QTextLayout layout(text, font);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    layout.beginLayout();
    for (int i = 0; i < maxLines; ++i) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(width);
        const int start = line.textStart();
        const int len = line.textLength();
        if (i == maxLines - 1 && start + len < text.size()) {
            lines << fm.elidedText(text.mid(start), Qt::ElideRight, width);
            break;
        }
        lines << text.mid(start, len).trimmed();
    }
    layout.endLayout();
    return lines;
}

}  // namespace

ZeroInstanceDelegate::ZeroInstanceDelegate(QObject* parent) : ListViewDelegate(parent) {}

QSize ZeroInstanceDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const
{
    return { kCellWidth, kCellHeight };
}

void ZeroInstanceDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const auto& t = Theme::current();
    const bool selected = option.state & QStyle::State_Selected;
    const bool hover = option.state & QStyle::State_MouseOver;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRect cell = option.rect;
    const QRectF card = QRectF(cell).adjusted(0.5, 0.5, -0.5, -0.5);
    const QPainterPath cardPath = Paint::panelPath(card, t.radiusLg);

    const QColor bg = selected ? t.elevated : (hover ? t.surfaceHover : t.surface);
    const QColor border = selected ? t.accent : (hover ? t.borderStrong : t.border);
    painter->fillPath(cardPath, bg);

    // icon area: elevated, top corners follow the card radius
    painter->save();
    painter->setClipPath(cardPath);
    painter->fillRect(QRect(cell.left(), cell.top(), cell.width(), kIconAreaHeight), selected ? t.higher : t.elevated);
    painter->restore();

    painter->setPen(QPen(border, 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(cardPath);
    Paint::paintBevel(*painter, QRectF(cell), false);

    const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
    const QRect iconRect(cell.left() + (cell.width() - kIconSize) / 2, cell.top() + (kIconAreaHeight - kIconSize) / 2, kIconSize, kIconSize);
    if (!icon.isNull())
        icon.paint(painter, iconRect);

    auto* instance = static_cast<MinecraftInstance*>(index.data(InstanceList::InstancePointerRole).value<void*>());

    // status dot (top right): running = success, crashed / broken = warning
    if (instance) {
        QColor dot;
        if (instance->isRunning())
            dot = t.success;
        else if (instance->hasCrashed() || instance->hasVersionBroken())
            dot = t.warning;
        if (dot.isValid()) {
            const QPointF center(cell.right() - 14, cell.top() + 14);
            painter->setPen(Qt::NoPen);
            painter->setBrush(withAlpha(dot, Tokens::statusTint * 2));
            painter->drawEllipse(center, 8, 8);
            painter->setBrush(dot);
            painter->drawEllipse(center, 4, 4);
        }
    }

    // text block
    const int pad = 10;
    const int textWidth = cell.width() - 2 * pad;
    QFont nameFont = Style::font(Role::Label);
    nameFont.setWeight(QFont::DemiBold);
    const QFontMetrics nameMetrics(nameFont);
    const QStringList nameLines = wrapElided(index.data(Qt::DisplayRole).toString(), nameFont, textWidth, 2);

    painter->setFont(nameFont);
    painter->setPen(t.text);
    int y = cell.top() + kIconAreaHeight + 8;
    for (const QString& line : nameLines) {
        painter->drawText(QRect(cell.left() + pad, y, textWidth, nameMetrics.height()), Qt::AlignHCenter | Qt::AlignVCenter, line);
        y += nameMetrics.height();
    }

    const QFont captionFont = Style::font(Role::Caption);
    const QFontMetrics captionMetrics(captionFont);
    const QString caption = InstanceInfo::of(instance).summary();
    painter->setFont(captionFont);
    painter->setPen(t.textSecondary);
    painter->drawText(QRect(cell.left() + pad, cell.bottom() - 8 - captionMetrics.height(), textWidth, captionMetrics.height()),
                      Qt::AlignHCenter | Qt::AlignVCenter, captionMetrics.elidedText(caption, Qt::ElideRight, textWidth));

    painter->restore();
}

void ZeroInstanceDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex&) const
{
    editor->setGeometry(option.rect.left() + 4, option.rect.top() + kIconAreaHeight + 4, option.rect.width() - 8, 44);
}

}  // namespace Zero
