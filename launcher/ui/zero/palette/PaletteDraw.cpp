// SPDX-License-Identifier: GPL-3.0-only
#include "PaletteDraw.h"

#include <QFontMetrics>
#include <QKeySequence>

#include "PaletteTypes.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {

QStringList keyCaps(const QKeySequence& sequence)
{
    const QString text = sequence.toString(QKeySequence::PortableText);  // English key names, whatever the Windows language
    QStringList caps;
    QString current;
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('+') && !current.isEmpty()) {
            caps.append(current);
            current.clear();
        } else {
            current.append(text.at(i));
        }
    }
    if (!current.isEmpty())
        caps.append(current);
    return caps;
}

namespace PaletteDraw {

namespace {
constexpr int kPadX = 6;
constexpr int kPadY = 2;
constexpr int kGap = 4;
}  // namespace

int keycapHeight()
{
    return QFontMetrics(Style::font(Role::Caption)).height() + 2 * kPadY;
}

int keycapWidth(const QString& text)
{
    return QFontMetrics(Style::font(Role::Caption)).horizontalAdvance(text) + 2 * kPadX;
}

int paintKeycap(QPainter& p, int x, int centerY, const QString& text, const QColor& color)
{
    const auto& t = Theme::current();
    const int w = keycapWidth(text);
    const int h = keycapHeight();
    const QRectF box(x, centerY - h / 2.0, w, h);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(t.elevated);
    p.drawPath(Paint::panelPath(box, t.radiusSm));
    p.setFont(Style::font(Role::Caption));
    p.setPen(color.isValid() ? color : t.textSecondary);
    p.drawText(box.toRect(), Qt::AlignCenter, text);
    p.restore();
    return w;
}

int keycapsWidth(const QStringList& keys)
{
    int w = 0;
    for (int i = 0; i < keys.size(); ++i)
        w += keycapWidth(keys.at(i)) + (i ? kGap : 0);
    return w;
}

int paintKeycapsRight(QPainter& p, int rightX, int centerY, const QStringList& keys)
{
    int x = rightX - keycapsWidth(keys);
    const int left = x;
    for (const QString& key : keys)
        x += paintKeycap(p, x, centerY, key) + kGap;
    return left;
}

}  // namespace PaletteDraw
}  // namespace Zero
