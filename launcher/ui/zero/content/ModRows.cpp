// SPDX-License-Identifier: GPL-3.0-only
#include "ModRows.h"

#include <QAbstractItemView>
#include <QHelpEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include <algorithm>

#include "ContentDraw.h"
#include "ModpackRows.h"
#include "RemoteImageCache.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero::Content {

QString environmentLabel(const ContentItem& item)
{
    if (!item.pack) {
        return {};
    }
    switch (item.pack->side.value()) {
        case ModPlatform::SideTypeValue::ClientSide:
            return QObject::tr("Client");
        case ModPlatform::SideTypeValue::ServerSide:
            return QObject::tr("Server");
        case ModPlatform::SideTypeValue::UniversalSide:
            return QObject::tr("Client & server");
        case ModPlatform::SideTypeValue::NoSide:
            break;
    }
    return {};
}

namespace {
using Draw::wrapLines;

constexpr int kDownloadIcon = 12;
constexpr int kDownloadGap = 4;

int downloadsWidth(const QString& count)
{
    const QFontMetrics fm(Style::font(Role::Caption));
    return kDownloadIcon + kDownloadGap + fm.horizontalAdvance(count);
}

void paintDownloads(QPainter* p, const QRect& rect, const QString& count)
{
    const auto& t = Theme::current();
    p->save();
    p->setFont(Style::font(Role::Caption));
    p->setPen(t.textSecondary);
    const QFontMetrics fm(p->font());
    const int textW = fm.horizontalAdvance(count);
    Icons::icon(QStringLiteral("download"), t.textSecondary)
        .paint(p, QRect(rect.left(), rect.top() + (rect.height() - kDownloadIcon) / 2, kDownloadIcon, kDownloadIcon));
    p->drawText(QRect(rect.left() + kDownloadIcon + kDownloadGap, rect.top(), textW + 2, rect.height()), Qt::AlignLeft | Qt::AlignVCenter, count);
    p->restore();
}

}  // namespace

QList<ModDelegate::Pill> ModDelegate::pillsFor(const ContentItem& item, bool installed, int budget)
{
    QList<Pill> pills;
    const QStringList loaders = loaderLabels(item.loaderNames);
    for (int i = 0; i < loaders.size() && i < 2; ++i) {
        pills.append({ Pill::Kind::Loader, loaders.at(i), PillTag::widthForText(loaders.at(i)), PillTag::Tone::Plain, i == 0 ? 3 : 1 });
    }
    if (const QString env = environmentLabel(item); !env.isEmpty()) {
        pills.append({ Pill::Kind::Environment, env, PillTag::widthForText(env), PillTag::Tone::Plain, 0 });
    }
    if (item.downloads >= 0) {
        const QString count = formatCount(item.downloads);
        pills.append({ Pill::Kind::Downloads, count, downloadsWidth(count), PillTag::Tone::Plain, 2 });
    }
    if (installed) {
        const QString text = QObject::tr("Installed");
        pills.append({ Pill::Kind::Installed, text, PillTag::widthForText(text), PillTag::Tone::Success, 100 });
    }
    auto total = [&pills]() {
        int sum = 0;
        for (const auto& pill : pills) {
            sum += pill.width;
        }
        return sum + std::max(0, int(pills.size()) - 1) * kPillGap;
    };
    while (total() > budget) {
        int drop = -1;
        for (int i = 0; i < pills.size(); ++i) {
            if (pills.at(i).kind != Pill::Kind::Installed && (drop < 0 || pills.at(i).priority < pills.at(drop).priority)) {
                drop = i;
            }
        }
        if (drop < 0) {
            break;
        }
        pills.removeAt(drop);
    }
    return pills;
}

ModDelegate::ModDelegate(RemoteImageCache* images, QObject* parent) : ContentDelegate(parent), m_images(images) {}

void ModDelegate::setMode(Mode mode)
{
    if (mode == m_mode) {
        return;
    }
    m_mode = mode;
    setViewportWidth(m_viewportWidth);
}

bool ModDelegate::setViewportWidth(int width)
{
    int columns = 1;
    int card = kCardMin;
    int cell = kCardMin + kGap;
    if (width > 0) {
        if (m_mode == Mode::Grid) {
            columns = std::max(1, (width + kGap) / (kCardMin + kGap));
            cell = width / columns;
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

QSize ModDelegate::itemSize() const
{
    return { m_cardWidth, m_mode == Mode::List ? kRowHeight : kCardHeight };
}

QSize ModDelegate::gridSize() const
{
    const QSize item = itemSize();
    return { m_cellWidth, item.height() + (m_mode == Mode::List ? kRowGap : kGap) };
}

QRect ModDelegate::checkRect(const QRect& cell) const
{
    if (!hasCart()) {
        return {};
    }
    const QRect rect = itemRect(cell);
    if (m_mode == Mode::List) {
        return QRect(rect.left() + Tokens::spaceMd, rect.top() + (rect.height() - kCheckSize) / 2, kCheckSize, kCheckSize);
    }
    // Grid cards have no artwork, so the box sits in the top-right corner, centred on the title line (never over the icon).
    const int titleHeight = QFontMetrics(Style::font(Role::Heading)).height();
    return QRect(rect.right() - kPad - kCheckSize + 1, rect.top() + kPad + (titleHeight - kCheckSize) / 2, kCheckSize, kCheckSize);
}

QRect ModDelegate::itemRect(const QRect& cell) const
{
    // Columns are spread evenly: the first flush left, the last flush right (when the cards are not capped in width).
    const int column = m_cellWidth > 0 ? qRound(double(cell.left()) / m_cellWidth) : 0;
    const int shift = column * (m_cardWidth + kGap - m_cellWidth);
    return QRect(cell.topLeft() + QPoint(shift, 0), itemSize());
}

QSize ModDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const
{
    return gridSize();
}

bool ModDelegate::helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem&, const QModelIndex& index)
{
    if (event->type() != QEvent::ToolTip || !index.isValid()) {
        return false;
    }
    const auto item = index.data(ModpackListModel::ItemRole).value<ContentItemPtr>();
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

void ModDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const auto item = index.data(ModpackListModel::ItemRole).value<ContentItemPtr>();
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
    if (m_mode == Mode::Grid) {
        paintCard(painter, rect, *item, installed, selected, hovered, cart);
    } else {
        paintRow(painter, rect, *item, installed, selected, hovered, cart);
    }
    if (cart.showCheck) {
        Draw::paintCartCheck(painter, checkRect(option.rect), cart.inCart);
    }
    // A keyboard focus ring (selection alone already shows the accent border).
    if ((option.state & QStyle::State_HasFocus) && selected) {
        painter->setPen(QPen(Theme::current().accent2.isValid() ? Theme::current().accent2 : Theme::current().accent, 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(Paint::panelPath(QRectF(rect).adjusted(-1.5, -1.5, 1.5, 1.5), Theme::current().radiusLg + 1));
    }
    painter->restore();
}

void ModDelegate::paintIcon(QPainter* painter, const QRect& rect, const ContentItem& item) const
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
        pixmap = m_images->request(item.iconUrl, RemoteImageCache::bucketFor(rect.width(), dpr), const_cast<ModDelegate*>(this));
        fade = pixmap.isNull() ? 0.0 : m_images->fadeIn(item.iconUrl);
    }
    if (pixmap.isNull() || fade < 1.0) {
        // The placeholder tile: `elevated` with a hairline and the `blocks` glyph (24 px at card size, 16 px in a row).
        painter->setPen(QPen(t.border, 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(Paint::panelPath(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusMd));
        painter->save();
        painter->setOpacity(painter->opacity() * (1.0 - fade));
        const int glyph = rect.width() >= kIcon ? 24 : 16;
        Icons::icon(QStringLiteral("blocks"), t.textTertiary)
            .paint(painter, QRect(rect.center().x() - glyph / 2, rect.center().y() - glyph / 2, glyph, glyph));
        painter->restore();
    }
    if (!pixmap.isNull()) {
        painter->setOpacity(painter->opacity() * Motion::easeEnter().valueForProgress(fade));
        painter->setClipPath(shape);
        Draw::drawCover(painter, rect, pixmap);
    }
    painter->restore();
}

void ModDelegate::paintCard(QPainter* p, const QRect& rect, const ContentItem& item, bool installed, bool selected, bool hovered,
                            const CartLook& cart) const
{
    const auto& t = Theme::current();
    const QColor fill = selected ? t.elevated : hovered ? t.surfaceHover : t.surface;
    // A ticked card has a 2 px accent border (inside the rect, so neighbours never overlap).
    const qreal inset = cart.inCart ? 1.0 : 0.5;
    const QPainterPath shape = Paint::panelPath(QRectF(rect).adjusted(inset, inset, -inset, -inset), t.radiusLg);
    p->setPen(QPen(cart.inCart || selected ? t.accent : hovered ? t.borderStrong : t.border, cart.inCart ? 2 : 1));
    p->setBrush(fill);
    p->drawPath(shape);

    paintIcon(p, QRect(rect.left() + kPad, rect.top() + kPad, kIcon, kIcon), item);

    const int textLeft = rect.left() + kPad + kIcon + Tokens::spaceMd;
    const int textWidth = std::max(40, rect.right() - kPad - textLeft + 1);
    // The title leaves room for the check box (top-right) so it never jumps when the box appears.
    const int titleWidth = std::max(40, textWidth - (hasCart() ? kCheckSize + Tokens::spaceSm : 0));
    int y = rect.top() + kPad;

    const QFont nameFont = Style::font(Role::Heading);
    const QFontMetrics nameMetrics(nameFont);
    p->setFont(nameFont);
    p->setPen(t.text);
    p->drawText(QRect(textLeft, y, titleWidth, nameMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                nameMetrics.elidedText(item.name, Qt::ElideRight, titleWidth));
    y += nameMetrics.height();

    const QFont captionFont = Style::font(Role::Caption);
    const QFontMetrics captionMetrics(captionFont);
    if (!item.author.isEmpty()) {
        p->setFont(captionFont);
        p->setPen(t.textSecondary);
        p->drawText(QRect(textLeft, y, textWidth, captionMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    captionMetrics.elidedText(tr("by %1").arg(item.author), Qt::ElideRight, textWidth));
    }
    y += captionMetrics.height() + 4;

    // Description: two lines; for an item that does not fit the target the second line is the warning.
    const int footerTop = rect.bottom() - kPad - kPillHeight + 1;
    const QFont bodyFont = Style::font(Role::Body);
    const QFontMetrics bodyMetrics(bodyFont);
    const QString notFor = m_fit ? m_fit(item) : QString();
    const int room = std::max(1, (footerTop - 2 - y) / bodyMetrics.lineSpacing());
    const int descLines = std::min(notFor.isEmpty() ? 2 : 1, room);
    p->setFont(bodyFont);
    p->setPen(t.textSecondary);
    const QStringList lines = wrapLines(item.summary, bodyFont, textWidth, descLines);
    for (int i = 0; i < lines.size(); ++i) {
        p->drawText(QRect(textLeft, y + i * bodyMetrics.lineSpacing(), textWidth, bodyMetrics.lineSpacing()), Qt::AlignLeft | Qt::AlignVCenter,
                    lines.at(i));
    }
    if (!notFor.isEmpty()) {
        p->setFont(captionFont);
        p->setPen(t.warning);
        const int wy = y + int(lines.size()) * bodyMetrics.lineSpacing();
        p->drawText(QRect(textLeft, wy, textWidth, bodyMetrics.lineSpacing()), Qt::AlignLeft | Qt::AlignVCenter,
                    captionMetrics.elidedText(notFor, Qt::ElideRight, textWidth));
    }

    // Footer: loaders, environment, downloads at the left; the state ("Installed") flush right.
    const int innerWidth = rect.width() - 2 * kPad;
    const QList<Pill> pills = pillsFor(item, installed, innerWidth);
    int x = rect.left() + kPad;
    for (const auto& pill : pills) {
        if (pill.kind == Pill::Kind::Installed) {
            PillTag::paintPill(p, QRectF(rect.right() - kPad - pill.width + 1, footerTop, pill.width, kPillHeight), pill.text, pill.tone);
        } else if (pill.kind == Pill::Kind::Downloads) {
            paintDownloads(p, QRect(x, footerTop, pill.width, kPillHeight), pill.text);
            x += pill.width + kPillGap;
        } else {
            PillTag::paintPill(p, QRectF(x, footerTop, pill.width, kPillHeight), pill.text, pill.tone);
            x += pill.width + kPillGap;
        }
    }
}

void ModDelegate::paintRow(QPainter* p, const QRect& rect, const ContentItem& item, bool installed, bool selected, bool hovered,
                           const CartLook& cart) const
{
    const auto& t = Theme::current();
    const QColor fill = selected ? t.elevated : hovered ? t.surfaceHover : t.surface;
    const qreal inset = cart.inCart ? 1.0 : 0.5;
    const QPainterPath shape = Paint::panelPath(QRectF(rect).adjusted(inset, inset, -inset, -inset), t.radiusLg);
    p->setPen(QPen(cart.inCart || selected ? t.accent : hovered ? t.borderStrong : t.border, cart.inCart ? 2 : 1));
    p->setBrush(fill);
    p->drawPath(shape);

    const int pad = Tokens::spaceMd;
    const int gutter = hasCart() ? kCheckSize + Tokens::spaceSm : 0;  // the check box sits left of the icon
    const QRect icon(rect.left() + pad + gutter, rect.top() + (rect.height() - kRowIcon) / 2, kRowIcon, kRowIcon);
    paintIcon(p, icon, item);

    constexpr int kMinText = 180;
    const int textLeft = icon.right() + pad + 1;
    const int pillY = rect.top() + (rect.height() - kPillHeight) / 2;
    const int budget = std::max(0, rect.right() - pad - (textLeft + kMinText) - kPillGap);
    const QList<Pill> pills = pillsFor(item, installed, budget);
    int needed = 0;
    for (const auto& pill : pills) {
        needed += pill.width + (needed > 0 ? kPillGap : 0);
    }
    int x = rect.right() - pad + 1 - needed;
    for (const auto& pill : pills) {
        if (pill.kind == Pill::Kind::Downloads) {
            paintDownloads(p, QRect(x, pillY, pill.width, kPillHeight), pill.text);
        } else {
            PillTag::paintPill(p, QRectF(x, pillY, pill.width, kPillHeight), pill.text, pill.tone);
        }
        x += pill.width + kPillGap;
    }
    const int textRight = (pills.isEmpty() ? rect.right() - pad : rect.right() - pad - needed - pad) + 1;
    const int textWidth = std::max(40, textRight - textLeft);

    const QFont captionFont = Style::font(Role::Caption);
    const QFontMetrics captionMetrics(captionFont);
    const QFont nameFont = Style::font(Role::Heading);
    const QFontMetrics nameMetrics(nameFont);
    const QFont bodyFont = Style::font(Role::Body);
    const QFontMetrics bodyMetrics(bodyFont);
    const int block = nameMetrics.height() + bodyMetrics.height();
    const int top = rect.top() + std::max(4, (rect.height() - block) / 2);

    p->setFont(nameFont);
    p->setPen(t.text);
    const QString author = item.author.isEmpty() ? QString() : tr("by %1").arg(item.author);
    const int authorW = author.isEmpty() ? 0 : captionMetrics.horizontalAdvance(author) + 8;
    const int nameW = std::min(nameMetrics.horizontalAdvance(item.name), std::max(60, textWidth - authorW));
    p->drawText(QRect(textLeft, top, nameW, nameMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                nameMetrics.elidedText(item.name, Qt::ElideRight, nameW));
    if (!author.isEmpty() && textWidth - nameW - 8 > 40) {
        p->setFont(captionFont);
        p->setPen(t.textSecondary);
        p->drawText(QRect(textLeft + nameW + 8, top, textWidth - nameW - 8, nameMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    captionMetrics.elidedText(author, Qt::ElideRight, textWidth - nameW - 8));
    }
    // One description line; the warning replaces it for an item that does not fit the target.
    const QString notFor = m_fit ? m_fit(item) : QString();
    const QRect descRect(textLeft, top + nameMetrics.height(), textWidth, bodyMetrics.height());
    if (!notFor.isEmpty()) {
        p->setFont(captionFont);
        p->setPen(t.warning);
        p->drawText(descRect, Qt::AlignLeft | Qt::AlignVCenter, captionMetrics.elidedText(notFor, Qt::ElideRight, textWidth));
    } else {
        p->setFont(bodyFont);
        p->setPen(t.textSecondary);
        p->drawText(descRect, Qt::AlignLeft | Qt::AlignVCenter, bodyMetrics.elidedText(item.summary.simplified(), Qt::ElideRight, textWidth));
    }
}

}  // namespace Zero::Content
