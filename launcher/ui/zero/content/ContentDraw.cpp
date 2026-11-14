// SPDX-License-Identifier: GPL-3.0-only
#include "ContentDraw.h"

#include <QPainter>
#include <QPainterPath>
#include <QTextLayout>

#include "ui/zero/PillTag.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero::Content::Draw {

QColor withAlpha(QColor color, qreal alpha)
{
    color.setAlphaF(alpha);
    return color;
}

QStringList wrapLines(const QString& text, const QFont& font, int width, int maxLines)
{
    QStringList lines;
    if (text.isEmpty() || width <= 0 || maxLines <= 0) {
        return lines;
    }
    const QFontMetrics fm(font);
    QTextLayout layout(text, font);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    layout.beginLayout();
    QList<QPair<int, int>> spans;
    for (;;) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) {
            break;
        }
        line.setLineWidth(width);
        spans.append({ line.textStart(), line.textLength() });
    }
    layout.endLayout();
    for (int i = 0; i < spans.size() && i < maxLines; ++i) {
        if (i == maxLines - 1 && spans.size() > maxLines) {
            lines.append(fm.elidedText(text.mid(spans[i].first).simplified(), Qt::ElideRight, width));
        } else {
            lines.append(text.mid(spans[i].first, spans[i].second).trimmed());
        }
    }
    return lines;
}

void drawCover(QPainter* painter, const QRect& target, const QPixmap& pixmap)
{
    if (pixmap.isNull() || target.isEmpty()) {
        return;
    }
    const QSizeF source = pixmap.size();
    QSizeF cropped = source;
    const qreal want = qreal(target.width()) / target.height();
    if (source.width() / source.height() > want) {
        cropped.setWidth(source.height() * want);
    } else {
        cropped.setHeight(source.width() / want);
    }
    const QRectF from((source.width() - cropped.width()) / 2, (source.height() - cropped.height()) / 2, cropped.width(), cropped.height());
    painter->drawPixmap(QRectF(target), pixmap, from);
}

void drawContain(QPainter* painter, const QRect& target, const QPixmap& pixmap)
{
    if (pixmap.isNull() || target.isEmpty()) {
        return;
    }
    QSizeF size = pixmap.size();
    size.scale(QSizeF(target.size()), Qt::KeepAspectRatio);
    const QRectF to(target.left() + (target.width() - size.width()) / 2, target.top() + (target.height() - size.height()) / 2, size.width(),
                    size.height());
    painter->drawPixmap(to, pixmap, QRectF(pixmap.rect()));
}

void paintOverlayPill(QPainter* painter, const QRect& rect, const QString& text, bool success)
{
    const auto& t = Theme::current();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(withAlpha(t.bg, 0.78));
    painter->drawPath(Paint::panelPath(QRectF(rect), t.radiusSm));
    painter->restore();
    PillTag::paintPill(painter, QRectF(rect), text, success ? PillTag::Tone::Success : PillTag::Tone::Plain);
}

void paintCartCheck(QPainter* painter, const QRect& box, bool checked)
{
    const auto& t = Theme::current();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5);
    if (checked) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(t.accent);
        painter->drawPath(Paint::panelPath(QRectF(box), t.radiusSm));
        const int glyph = 14;
        Icons::icon(QStringLiteral("check"), t.onAccent)
            .paint(painter, QRect(box.center().x() - glyph / 2, box.center().y() - glyph / 2, glyph, glyph));
    } else {
        painter->setPen(QPen(t.textSecondary, 1));
        painter->setBrush(withAlpha(t.bg, 0.72));
        painter->drawPath(Paint::panelPath(r, t.radiusSm));
    }
    painter->restore();
}

}  // namespace Zero::Content::Draw
