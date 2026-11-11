// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroPageNav.h"

#include <QFontMetrics>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include "ZeroPageHelp.h"
#include "ZeroPaint.h"
#include "ZeroStyle.h"
#include "ZeroThemeState.h"

namespace Zero {

namespace {
constexpr int kInset = 8;      // row pill inside the list
constexpr int kIcon = 22;
constexpr int kPad = 12;       // inside the pill
constexpr int kIndicator = 3;  // accent bar of the selected row
constexpr int kSummaryLines = 2;

/// Word-wraps `text` into at most `maxLines` lines of `width`, eliding the last one.
QStringList wrap(const QString& text, const QFontMetrics& fm, int width, int maxLines)
{
    QStringList lines;
    QString current;
    const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (int i = 0; i < words.size(); ++i) {
        const QString candidate = current.isEmpty() ? words[i] : current + QLatin1Char(' ') + words[i];
        if (fm.horizontalAdvance(candidate) <= width || current.isEmpty()) {
            current = candidate;
            continue;
        }
        if (lines.size() == maxLines - 1) {
            current = candidate + QLatin1Char(' ') + QStringList(words.mid(i + 1)).join(QLatin1Char(' '));
            break;
        }
        lines << current;
        current = words[i];
    }
    if (!current.isEmpty())
        lines << fm.elidedText(current, Qt::ElideRight, width);
    return lines;
}
}  // namespace

QSize PageNavDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex& index) const
{
    const QString summary = PageHelp::forPage(index.data(kPageIdRole).toString()).summary;
    return { kWidth - 4, summary.isEmpty() ? kRowPlain : kRowWithSummary };
}

void PageNavDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const auto& t = Theme::current();
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    const bool enabled = option.state & QStyle::State_Enabled;
    const QString title = index.data(Qt::DisplayRole).toString();
    const QString summary = PageHelp::forPage(index.data(kPageIdRole).toString()).summary;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QRectF pill = QRectF(option.rect).adjusted(kInset, 1, -kInset, -1);
    if (selected || hovered) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(selected ? t.elevated : t.surfaceHover);
        painter->drawPath(Paint::panelPath(pill, t.radiusMd));
    }
    if (selected) {
        painter->setBrush(t.accent);
        const qreal h = std::min<qreal>(pill.height() - 16, 24);
        painter->drawRoundedRect(QRectF(pill.left(), pill.center().y() - h / 2, kIndicator, h), 1.5, 1.5);
    }

    const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
    const QRect iconRect(int(pill.left()) + kPad, int(pill.center().y()) - kIcon / 2, kIcon, kIcon);
    if (!icon.isNull())
        icon.paint(painter, iconRect, Qt::AlignCenter, enabled ? QIcon::Normal : QIcon::Disabled);

    const int textLeft = iconRect.right() + 1 + 12;
    const int textWidth = int(pill.right()) - kPad / 2 - textLeft;
    QFont titleFont = Style::font(Role::Label);
    titleFont.setWeight(selected ? QFont::DemiBold : QFont::Medium);
    const QFontMetrics tm(titleFont);
    const QFont captionFont = Style::font(Role::Caption);
    const QFontMetrics cm(captionFont);
    if (summary.isEmpty()) {
        painter->setFont(titleFont);
        painter->setPen(selected ? t.text : t.textSecondary);
        painter->drawText(QRect(textLeft, int(pill.top()), textWidth, int(pill.height())), Qt::AlignLeft | Qt::AlignVCenter,
                          tm.elidedText(title, Qt::ElideRight, textWidth));
    } else {
        const QStringList lines = wrap(summary, cm, textWidth, kSummaryLines);
        const int block = tm.height() + 2 + int(lines.size()) * cm.height();
        const int top = int(pill.center().y()) - block / 2;
        painter->setFont(titleFont);
        painter->setPen(selected || hovered ? t.text : t.textSecondary);
        painter->drawText(QRect(textLeft, top, textWidth, tm.height()), Qt::AlignLeft | Qt::AlignVCenter, tm.elidedText(title, Qt::ElideRight, textWidth));
        painter->setFont(captionFont);
        painter->setPen(t.textTertiary);
        int y = top + tm.height() + 2;
        for (const QString& line : lines) {
            painter->drawText(QRect(textLeft, y, textWidth, cm.height()), Qt::AlignLeft | Qt::AlignVCenter, line);
            y += cm.height();
        }
    }
    painter->restore();
}

}  // namespace Zero
