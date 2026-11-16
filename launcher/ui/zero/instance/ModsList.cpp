// SPDX-License-Identifier: GPL-3.0-only
#include "ModsList.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>

#include "ui/zero/PillTag.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/content/ContentDraw.h"
#include "ui/zero/instance/InstancePageParts.h"

namespace Zero {
namespace ModsUi {

using Mods::Installed::PillSpec;
using Mods::Installed::PillTone;
using Mods::Installed::Row;

// ---- model -----------------------------------------------------------------------------------------------------------------------

bool ModListModel::setRows(const QList<Row>& rows, const QList<quintptr>& tokens, const QList<int>& sourceRows)
{
    if (tokens == m_tokens) {
        m_rows = rows;
        m_sourceRows = sourceRows;
        if (!m_rows.isEmpty()) {
            emit dataChanged(index(0), index(int(m_rows.size()) - 1));
        }
        return false;
    }
    beginResetModel();
    m_rows = rows;
    m_tokens = tokens;
    m_sourceRows = sourceRows;
    endResetModel();
    return true;
}

int ModListModel::rowOfId(const QString& id) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

QVariant ModListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }
    const Row& row = m_rows.at(index.row());
    switch (role) {
        case Qt::DisplayRole:
            return row.name;
        case Qt::ToolTipRole:
            return Mods::Installed::rowToolTip(row);
        case Qt::AccessibleTextRole:
            return row.version.isEmpty() ? row.name : QStringLiteral("%1 %2").arg(row.name, row.version);
        case Qt::AccessibleDescriptionRole:
            return row.enabled ? tr("Enabled") : tr("Disabled");
        case TokenRole:
            return QVariant::fromValue<quintptr>(m_tokens.at(index.row()));
        case EnabledRole:
            return row.enabled;
        default:
            break;
    }
    return {};
}

Qt::ItemFlags ModListModel::flags(const QModelIndex& index) const
{
    return index.isValid() ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable) : Qt::NoItemFlags;
}

// ---- delegate --------------------------------------------------------------------------------------------------------------------

QRect ModDelegate::cardRect(const QRect& cell)
{
    return QRect(cell.left(), cell.top(), cell.width(), kRowHeight);
}

QRect ModDelegate::switchRect(const QRect& card)
{
    return QRect(card.left() + kPad, card.top() + (card.height() - kSwitchHeight) / 2, kSwitchWidth, kSwitchHeight);
}

QRect ModDelegate::menuRect(const QRect& card)
{
    return QRect(card.right() - kPad + 1 - kMenuSize, card.top() + (card.height() - kMenuSize) / 2, kMenuSize, kMenuSize);
}

QSize ModDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const
{
    return { option.rect.width(), kRowHeight + kRowGap };
}

QList<ModDelegate::Placed> ModDelegate::fitPills(const QList<PillSpec>& pills, int budget)
{
    QList<Placed> placed;
    const QFontMetrics metrics(Style::font(Role::Caption));
    for (const PillSpec& spec : pills) {
        QString text = spec.text;
        if (spec.maxWidth > 0 && PillTag::widthForText(text) > spec.maxWidth) {
            text = metrics.elidedText(text, Qt::ElideRight, spec.maxWidth - (PillTag::widthForText(QString())));
        }
        placed.append({ spec, PillTag::widthForText(text), text });
    }
    const auto total = [&placed]() {
        int sum = 0;
        for (const Placed& p : placed) {
            sum += p.width;
        }
        return sum + std::max(0, int(placed.size()) - 1) * kPillGap;
    };
    while (!placed.isEmpty() && total() > budget) {
        int drop = 0;
        for (int i = 1; i < placed.size(); ++i) {
            if (placed.at(i).spec.priority < placed.at(drop).spec.priority) {
                drop = i;
            }
        }
        if (placed.at(drop).spec.priority >= Mods::Installed::kKeepPriority) {
            break;  // only the pills that must stay are left: the warnings, the dangers and the update
        }
        placed.removeAt(drop);
    }
    return placed;
}

void ModDelegate::shrinkToFit(QList<Placed>& placed, int limit)
{
    const QFontMetrics metrics(Style::font(Role::Caption));
    const auto total = [&placed]() {
        int sum = 0;
        for (const Placed& p : placed) {
            sum += p.width;
        }
        return sum + std::max(0, int(placed.size()) - 1) * kPillGap;
    };
    constexpr int kMinPill = 56;
    while (!placed.isEmpty() && total() > limit) {
        int widest = 0;
        for (int i = 1; i < placed.size(); ++i) {
            if (placed.at(i).width > placed.at(widest).width) {
                widest = i;
            }
        }
        Placed& p = placed[widest];
        const int target = std::max(kMinPill, p.width - (total() - limit));
        if (target >= p.width) {
            break;  // nothing left to shorten
        }
        p.text = metrics.elidedText(p.spec.text, Qt::ElideRight, target - PillTag::widthForText(QString()));
        p.width = PillTag::widthForText(p.text);
    }
}

void ModDelegate::paintSwitch(QPainter* p, const QRect& rect, bool on, bool locked) const
{
    ModsUi::paintSwitch(p, rect, on, locked);
}

void ModDelegate::paintIcon(QPainter* p, const QRect& rect, const Row& row) const
{
    const auto& t = Theme::current();
    p->save();
    const QPainterPath shape = Paint::panelPath(QRectF(rect), t.radiusMd);
    p->setPen(Qt::NoPen);
    p->setBrush(t.elevated);
    p->drawPath(shape);
    QPixmap pixmap;
    if (row.hasIcon && m_icons) {
        pixmap = m_icons(row.id, rect.width(), p->device()->devicePixelRatioF());
    }
    if (pixmap.isNull()) {
        p->setPen(QPen(t.border, 1));
        p->setBrush(Qt::NoBrush);
        p->drawPath(Paint::panelPath(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusMd));
        Icons::icon(m_fallbackIcon, t.textTertiary).paint(p, QRect(rect.center().x() - 8, rect.center().y() - 8, 16, 16));
    } else {
        p->setClipPath(shape);
        Content::Draw::drawCover(p, rect, pixmap);
    }
    p->restore();
}

void ModDelegate::paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const auto* model = qobject_cast<const ModListModel*>(index.model());
    if (!model || index.row() < 0 || index.row() >= model->rowCount()) {
        return;
    }
    const Row& row = model->rowAt(index.row());
    const auto& t = Theme::current();
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    const QRect card = cardRect(option.rect);

    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QColor fill = selected ? t.elevated : hovered ? t.surfaceHover : t.surface;
    p->setPen(QPen(selected ? t.accent : hovered ? t.borderStrong : t.border, 1));
    p->setBrush(fill);
    p->drawPath(Paint::panelPath(QRectF(card).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusLg));

    paintSwitch(p, switchRect(card), row.enabled, m_locked);

    const QRect icon(card.left() + kPad + kSwitchWidth + kPad, card.top() + (card.height() - kIcon) / 2, kIcon, kIcon);
    p->save();
    if (!row.enabled) {
        p->setOpacity(p->opacity() * 0.5);
    }
    paintIcon(p, icon, row);
    p->restore();

    // `...` button: only on the row under the pointer, the selected rows and the keyboard row, but its room is always kept.
    const QRect menu = menuRect(card);
    if (hovered || selected) {
        const QPoint mouse = option.widget ? option.widget->mapFromGlobal(QCursor::pos()) : QPoint(-1, -1);
        const bool over = menu.contains(mouse);
        if (over) {
            p->setPen(Qt::NoPen);
            p->setBrush(t.higher);
            p->drawPath(Paint::panelPath(QRectF(menu), t.radiusMd));
        }
        Icons::icon(QStringLiteral("ellipsis"), over ? t.text : t.textSecondary)
            .paint(p, QRect(menu.center().x() - 9, menu.center().y() - 9, 18, 18));
    }

    // pills
    const int textLeft = icon.right() + 1 + kPad;
    constexpr int kMinText = 160;
    constexpr int kMinName = 56;
    const int pillsRight = menu.left() - kPad;
    QList<Placed> pills = fitPills(Mods::Installed::pillsFor(row, m_mcVersion), std::max(0, pillsRight - (textLeft + kMinText)));
    shrinkToFit(pills, std::max(0, pillsRight - (textLeft + kMinName)));  // the pills that must stay never push the name out entirely
    int pillsWidth = 0;
    for (const Placed& pill : pills) {
        pillsWidth += pill.width + (pillsWidth > 0 ? kPillGap : 0);
    }
    int x = pillsRight - pillsWidth;
    const int pillY = card.top() + (card.height() - kPillHeight) / 2;
    p->save();
    if (!row.enabled) {
        p->setOpacity(p->opacity() * 0.85);
    }
    for (const Placed& pill : pills) {
        PillTag::paintPill(p, QRectF(x, pillY, pill.width, kPillHeight), pill.text,
                           pill.spec.tone == PillTone::Warning  ? PillTag::Tone::Warning
                           : pill.spec.tone == PillTone::Danger ? PillTag::Tone::Danger
                           : pill.spec.tone == PillTone::Accent ? PillTag::Tone::Accent
                                                                : PillTag::Tone::Plain);
        x += pill.width + kPillGap;
    }
    p->restore();

    // name + version
    const int textRight = (pills.isEmpty() ? pillsRight : pillsRight - pillsWidth - kPad) + 1;
    const int textWidth = std::max(40, textRight - textLeft);
    const QFont nameFont = Style::font(Role::Heading);
    const QFontMetrics nameMetrics(nameFont);
    const QFont captionFont = Style::font(Role::Caption);
    const QFontMetrics captionMetrics(captionFont);
    const bool hasVersion = !row.version.trimmed().isEmpty();
    const int block = nameMetrics.height() + (hasVersion ? captionMetrics.height() : 0);
    const int top = card.top() + std::max(4, (card.height() - block) / 2);
    p->setFont(nameFont);
    p->setPen(row.enabled ? t.text : t.textSecondary);
    p->drawText(QRect(textLeft, top, textWidth, nameMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                nameMetrics.elidedText(row.name, Qt::ElideRight, textWidth));
    if (hasVersion) {
        p->setFont(captionFont);
        p->setPen(t.textSecondary);
        p->drawText(QRect(textLeft, top + nameMetrics.height(), textWidth, captionMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    captionMetrics.elidedText(row.version.trimmed(), Qt::ElideRight, textWidth));
    }

    // keyboard focus ring on the row that has the focus inside a focused view
    if ((option.state & QStyle::State_HasFocus) && selected) {
        p->setPen(QPen(t.accent2.isValid() ? t.accent2 : t.accent, 1));
        p->setBrush(Qt::NoBrush);
        p->drawPath(Paint::panelPath(QRectF(card).adjusted(-1.5, -1.5, 1.5, 1.5), t.radiusLg + 1));
    }
    p->restore();
}

bool ModDelegate::helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option, const QModelIndex& index)
{
    if (event->type() != QEvent::ToolTip || !index.isValid()) {
        return false;
    }
    const QRect card = cardRect(option.rect);
    const QPoint pos = event->pos();
    QString text;
    if (switchRect(card).contains(pos)) {
        text = m_locked ? (m_lockReason.isEmpty() ? tr("Close the game to change mods") : m_lockReason)
                        : (index.data(ModListModel::EnabledRole).toBool() ? tr("Turn off") : tr("Turn on"));
    } else if (menuRect(card).contains(pos)) {
        text = tr("More");
    } else {
        text = index.data(Qt::ToolTipRole).toString();
    }
    if (text.isEmpty()) {
        QToolTip::hideText();
        return true;
    }
    QToolTip::showText(event->globalPos(), text, view);
    return true;
}

// ---- view ------------------------------------------------------------------------------------------------------------------------

ModListView::ModListView(QWidget* parent) : QListView(parent), m_delegate(new ModDelegate(this))
{
    setObjectName(QStringLiteral("zeroModsList"));
    setItemDelegate(m_delegate);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setUniformItemSizes(true);
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setMouseTracking(true);
    setAttribute(Qt::WA_MacShowFocusRect, false);
    setStyleSheet(QStringLiteral("QAbstractItemView { background: transparent; border: none; }"));
    viewport()->setAutoFillBackground(false);
}

void ModListView::mousePressEvent(QMouseEvent* event)
{
    m_pressedSwitchRow = -1;
    m_pressedBodyRow = -1;
    if (event->button() == Qt::LeftButton) {
        const QModelIndex index = indexAt(event->position().toPoint());
        if (index.isValid()) {
            m_pressedBodyRow = index.row();  // cleared below when the press lands on the switch or the `...` button
            m_pressedPlain = (event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) == 0;
            const QRect card = ModDelegate::cardRect(visualRect(index));
            if (ModDelegate::switchRect(card).contains(event->position().toPoint())) {
                m_pressedSwitchRow = index.row();  // acts on release; the selection stays as it is
                m_pressedBodyRow = -1;
                event->accept();
                return;
            }
            if (ModDelegate::menuRect(card).contains(event->position().toPoint())) {
                m_pressedBodyRow = -1;
                event->accept();
                emit menuRequested(index.row(), event->globalPosition().toPoint());
                return;
            }
        }
    }
    QListView::mousePressEvent(event);
}

void ModListView::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_pressedSwitchRow >= 0 && event->button() == Qt::LeftButton) {
        const int pressed = m_pressedSwitchRow;
        m_pressedSwitchRow = -1;
        const QModelIndex index = indexAt(event->position().toPoint());
        if (index.isValid() && index.row() == pressed &&
            ModDelegate::switchRect(ModDelegate::cardRect(visualRect(index))).contains(event->position().toPoint())) {
            emit switchClicked(pressed);
        }
        event->accept();
        return;
    }
    QListView::mouseReleaseEvent(event);
    if (m_pressedBodyRow >= 0 && event->button() == Qt::LeftButton) {
        const int pressed = m_pressedBodyRow;
        const bool plain = m_pressedPlain && (event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) == 0;
        m_pressedBodyRow = -1;
        const QModelIndex index = indexAt(event->position().toPoint());
        if (plain && index.isValid() && index.row() == pressed) {
            emit rowClicked(pressed);
        }
    }
}

void ModListView::mouseMoveEvent(QMouseEvent* event)
{
    QListView::mouseMoveEvent(event);
    const QModelIndex index = indexAt(event->position().toPoint());
    const int row = index.isValid() ? index.row() : -1;
    if (row != -1) {
        const QRect card = ModDelegate::cardRect(visualRect(index));
        setCursor(ModDelegate::switchRect(card).contains(event->position().toPoint()) && !m_delegate->locked() ? Qt::PointingHandCursor
                                                                                                                : Qt::ArrowCursor);
    } else {
        unsetCursor();
    }
    viewport()->update();  // the `...` button reacts to the pointer inside its row
}

void ModListView::leaveEvent(QEvent* event)
{
    QListView::leaveEvent(event);
    unsetCursor();
    viewport()->update();
}

void ModListView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space && event->modifiers() == Qt::NoModifier && !event->isAutoRepeat()) {
        emit toggleKeyPressed();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Delete && event->modifiers() == Qt::NoModifier) {
        emit removeKeyPressed();
        event->accept();
        return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && event->modifiers() == Qt::NoModifier && !event->isAutoRepeat()) {
        emit detailsKeyPressed();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier && m_escapeCloses) {
        emit escapePressed();
        event->accept();
        return;
    }
    QListView::keyPressEvent(event);
}

void ModListView::contextMenuEvent(QContextMenuEvent* event)
{
    const QModelIndex index = indexAt(event->pos());
    if (index.isValid()) {
        emit menuRequested(index.row(), event->globalPos());
        event->accept();
        return;
    }
    QListView::contextMenuEvent(event);
}

}  // namespace ModsUi
}  // namespace Zero
