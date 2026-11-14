// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - small painting helpers shared by the Discover cards, rows, hero cards, details artwork and gallery.
#pragma once

#include <QColor>
#include <QFont>
#include <QPixmap>
#include <QRect>
#include <QString>
#include <QStringList>

class QPainter;

namespace Zero::Content::Draw {

QColor withAlpha(QColor color, qreal alpha);

/// `text` broken into at most `maxLines` lines of `width` px; the last line is elided when the text goes on.
QStringList wrapLines(const QString& text, const QFont& font, int width, int maxLines);

/// Draws `pixmap` scaled to cover `target` (centered crop).
void drawCover(QPainter* painter, const QRect& target, const QPixmap& pixmap);
/// Draws `pixmap` scaled to fit inside `target` (centered, aspect kept, never enlarged beyond `target`).
void drawContain(QPainter* painter, const QRect& target, const QPixmap& pixmap);

/// A pill on artwork: a `bg` scrim under it so it reads on any picture. `success` = the Installed tone.
void paintOverlayPill(QPainter* painter, const QRect& rect, const QString& text, bool success);

/// The install cart's check box (20 px, radius `radiusSm`): a ticked box is the filled accent with a `check` glyph in `onAccent`, an
/// empty one a `bg` scrim with a hairline, so it reads on artwork too.
void paintCartCheck(QPainter* painter, const QRect& box, bool checked);

}  // namespace Zero::Content::Draw
