// SPDX-License-Identifier: GPL-3.0-only
#include "PackList.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QToolTip>

#include <algorithm>

#include "ui/zero/PillTag.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/content/ContentDraw.h"
#include "ui/zero/instance/InstancePageParts.h"
#include "ui/zero/instance/ModsList.h"

namespace Zero {
namespace PacksUi {

using Mods::Installed::PillTone;
using Kind = PackListModel::Kind;

// ---- model -----------------------------------------------------------------------------------------------------------------------

bool PackListModel::setEntries(const QList<Entry>& entries)
{
    bool sameShape = entries.size() == m_entries.size();
    for (int i = 0; sameShape && i < entries.size(); ++i) {
        sameShape = entries.at(i).token == m_entries.at(i).token && entries.at(i).kind == m_entries.at(i).kind;
    }
    if (sameShape) {
        m_entries = entries;
        if (!m_entries.isEmpty()) {
            emit dataChanged(index(0), index(int(m_entries.size()) - 1));
        }
        return false;
    }
    beginResetModel();
    m_entries = entries;
    endResetModel();
    return true;
}

int PackListModel::rowOfFile(const QString& fileName) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).kind == Kind::Pack && m_entries.at(i).row.fileName == fileName) {
            return i;
        }
    }
    return -1;
}

QList<int> PackListModel::onRows() const
{
    QList<int> rows;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).kind == Kind::Pack && m_entries.at(i).onIndex >= 0) {
            rows.append(i);
        }
    }
    return rows;
}

QVariant PackListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }
    const Entry& entry = m_entries.at(index.row());
    if (entry.kind != Kind::Pack) {
        switch (role) {
            case Qt::DisplayRole:
                return entry.text;
            case Qt::ToolTipRole:
                return entry.kind == Kind::Default ? Content::Packs::defaultRowNote() : QString();
            case KindRole:
                return int(entry.kind);
            default:
                return {};
        }
    }
    switch (role) {
        case Qt::DisplayRole:
            return entry.row.name;
        case Qt::ToolTipRole:
            return Content::Packs::rowToolTip(entry.row);
        case Qt::AccessibleTextRole:
            return entry.row.description.isEmpty() ? entry.row.name : QStringLiteral("%1. %2").arg(entry.row.name, entry.row.description);
        case Qt::AccessibleDescriptionRole:
            return entry.row.active ? tr("On") : tr("Off");
        case TokenRole:
            return QVariant::fromValue<quintptr>(entry.token);
        case ActiveRole:
            return entry.row.active;
        case KindRole:
            return int(entry.kind);
        case OnIndexRole:
            return entry.onIndex;
        default:
            break;
    }
    return {};
}

Qt::ItemFlags PackListModel::flags(const QModelIndex& index) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return Qt::NoItemFlags;
    }
    return m_entries.at(index.row()).kind == Kind::Pack ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable) : Qt::NoItemFlags;
}

// ---- delegate --------------------------------------------------------------------------------------------------------------------

QRect PackDelegate::switchRect(const QRect& card)
{
    return QRect(card.left() + kPad, card.top() + (card.height() - kSwitchHeight) / 2, kSwitchWidth, kSwitchHeight);
}

QRect PackDelegate::gripRect(const QRect& card)
{
    const QRect sw = switchRect(card);
    return QRect(sw.right() + 1 + Tokens::spaceSm, card.top() + (card.height() - kGrip) / 2, kGrip, kGrip);
}

QRect PackDelegate::menuRect(const QRect& card)
{
    return QRect(card.right() - kPad + 1 - kMenuSize, card.top() + (card.height() - kMenuSize) / 2, kMenuSize, kMenuSize);
}

QRect PackDelegate::unhideRect(const QRect& card)
{
    const QRect menu = menuRect(card);
    return QRect(menu.left() - kPillGap - kUnhideWidth, card.top() + (card.height() - kMenuSize) / 2, kUnhideWidth, kMenuSize);
}

QSize PackDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    if (index.data(PackListModel::KindRole).toInt() == int(Kind::Header)) {
        return { option.rect.width(), kHeaderHeight };
    }
    return { option.rect.width(), kRowHeight + kRowGap };
}

void PackDelegate::paintIcon(QPainter* p, const QRect& rect, const PackListModel::Entry& entry) const
{
    const auto& t = Theme::current();
    p->save();
    const QPainterPath shape = Paint::panelPath(QRectF(rect), t.radiusMd);
    p->setPen(Qt::NoPen);
    p->setBrush(t.elevated);
    p->drawPath(shape);
    QPixmap pixmap;
    if (entry.kind == Kind::Pack && m_icons) {
        pixmap = m_icons(entry.row.id, rect.width(), p->device()->devicePixelRatioF());
    }
    if (pixmap.isNull()) {
        p->setPen(QPen(t.border, 1));
        p->setBrush(Qt::NoBrush);
        p->drawPath(Paint::panelPath(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusMd));
        Icons::icon(entry.kind == Kind::Default ? QStringLiteral("package") : QStringLiteral("images"), t.textTertiary)
            .paint(p, QRect(rect.center().x() - 10, rect.center().y() - 10, 20, 20));
    } else {
        p->setClipPath(shape);
        Content::Draw::drawCover(p, rect, pixmap);
    }
    p->restore();
}

void PackDelegate::paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const auto* model = qobject_cast<const PackListModel*>(index.model());
    if (!model || index.row() < 0 || index.row() >= model->rowCount()) {
        return;
    }
    const PackListModel::Entry& entry = model->entryAt(index.row());
    const auto& t = Theme::current();

    if (entry.kind == Kind::Header) {
        p->save();
        p->setFont(Style::font(Role::Overline));
        p->setPen(t.textTertiary);
        p->drawText(QRect(option.rect.left() + Tokens::spaceXs, option.rect.top(), option.rect.width() - Tokens::spaceXs, option.rect.height()),
                    Qt::AlignLeft | Qt::AlignBottom, entry.text.toUpper());
        p->restore();
        return;
    }

    const bool isPack = entry.kind == Kind::Pack;
    const bool selected = isPack && (option.state & QStyle::State_Selected);
    const bool hovered = isPack && (option.state & QStyle::State_MouseOver);
    const QRect card = cardRect(option.rect);
    const bool hidden = isPack && entry.row.hidden;

    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (index.row() == m_dragRow) {
        p->setOpacity(p->opacity() * 0.4);
    }

    const QColor fill = selected ? t.elevated : hovered ? t.surfaceHover : t.surface;
    p->setPen(QPen(selected ? t.accent : hovered ? t.borderStrong : t.border, 1));
    p->setBrush(fill);
    p->drawPath(Paint::panelPath(QRectF(card).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusLg));

    // switch and handle (their room is kept on every row so the pictures line up)
    int left = card.left() + kPad;
    if (m_ordered) {
        if (isPack && !hidden) {
            ModsUi::paintSwitch(p, switchRect(card), entry.row.active, m_locked);
        }
        if (isPack && entry.row.active) {
            const QRect grip = gripRect(card);
            const QPoint mouse = option.widget ? option.widget->mapFromGlobal(QCursor::pos()) : QPoint(-1, -1);
            p->save();
            if (m_locked) {
                p->setOpacity(p->opacity() * 0.45);
            }
            Icons::icon(QStringLiteral("grip-vertical"), grip.adjusted(-4, -4, 4, 4).contains(mouse) ? t.text : t.textTertiary).paint(p, grip);
            p->restore();
        }
        left = gripRect(card).right() + 1 + Tokens::spaceSm;
    }
    const QRect icon(left, card.top() + (card.height() - kIcon) / 2, kIcon, kIcon);
    p->save();
    if (hidden) {
        p->setOpacity(p->opacity() * 0.5);
    }
    paintIcon(p, icon, entry);
    p->restore();

    // `...` button (only on the row under the pointer and the selected row; its room is always kept) and "Unhide"
    int pillsRight = card.right() + 1 - kPad;
    if (isPack) {
        const QRect menu = menuRect(card);
        pillsRight = menu.left() - kPad;
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
        if (hidden) {
            const QRect unhide = unhideRect(card);
            const QPoint mouse = option.widget ? option.widget->mapFromGlobal(QCursor::pos()) : QPoint(-1, -1);
            p->setPen(QPen(unhide.contains(mouse) ? t.accent : t.borderStrong, 1));
            p->setBrush(unhide.contains(mouse) ? t.higher : Qt::NoBrush);
            p->drawPath(Paint::panelPath(QRectF(unhide).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusMd));
            p->setFont(Style::font(Role::Label));
            p->setPen(t.text);
            p->drawText(unhide, Qt::AlignCenter, tr("Unhide"));
            pillsRight = unhide.left() - kPad;
        }
    }

    // pills
    const int textLeft = icon.right() + 1 + kPad;
    constexpr int kMinText = 160;
    constexpr int kMinName = 56;
    QList<ModsUi::ModDelegate::Placed> pills;
    if (isPack) {
        pills = ModsUi::ModDelegate::fitPills(Content::Packs::pillsFor(entry.row), std::max(0, pillsRight - (textLeft + kMinText)));
        ModsUi::ModDelegate::shrinkToFit(pills, std::max(0, pillsRight - (textLeft + kMinName)));
    }
    int pillsWidth = 0;
    for (const auto& pill : pills) {
        pillsWidth += pill.width + (pillsWidth > 0 ? kPillGap : 0);
    }
    int x = pillsRight - pillsWidth;
    const int pillY = card.top() + (card.height() - kPillHeight) / 2;
    for (const auto& pill : pills) {
        PillTag::paintPill(p, QRectF(x, pillY, pill.width, kPillHeight), pill.text,
                           pill.spec.tone == PillTone::Warning  ? PillTag::Tone::Warning
                           : pill.spec.tone == PillTone::Danger ? PillTag::Tone::Danger
                           : pill.spec.tone == PillTone::Accent ? PillTag::Tone::Accent
                                                                : PillTag::Tone::Plain);
        x += pill.width + kPillGap;
    }

    // name + description
    const int textRight = (pills.isEmpty() ? pillsRight : pillsRight - pillsWidth - kPad) + 1;
    const int textWidth = std::max(40, textRight - textLeft);
    const QFont nameFont = Style::font(Role::Heading);
    const QFontMetrics nameMetrics(nameFont);
    const QFont captionFont = Style::font(Role::Caption);
    const QFontMetrics captionMetrics(captionFont);
    const QString name = isPack ? entry.row.name : entry.text;
    const QString description = isPack ? entry.row.description : Content::Packs::defaultRowNote();
    const bool hasDescription = !description.trimmed().isEmpty();
    const int block = nameMetrics.height() + (hasDescription ? captionMetrics.height() : 0);
    const int top = card.top() + std::max(4, (card.height() - block) / 2);
    p->setFont(nameFont);
    p->setPen(hidden ? t.textSecondary : t.text);
    p->drawText(QRect(textLeft, top, textWidth, nameMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                nameMetrics.elidedText(name, Qt::ElideRight, textWidth));
    if (hasDescription) {
        p->setFont(captionFont);
        p->setPen(t.textSecondary);
        p->drawText(QRect(textLeft, top + nameMetrics.height(), textWidth, captionMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    captionMetrics.elidedText(description, Qt::ElideRight, textWidth));
    }

    // keyboard focus ring on the row that has the focus inside a focused view
    if (isPack && (option.state & QStyle::State_HasFocus) && selected) {
        p->setPen(QPen(t.accent2.isValid() ? t.accent2 : t.accent, 1));
        p->setBrush(Qt::NoBrush);
        p->drawPath(Paint::panelPath(QRectF(card).adjusted(-1.5, -1.5, 1.5, 1.5), t.radiusLg + 1));
    }
    p->restore();
}

bool PackDelegate::helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option, const QModelIndex& index)
{
    if (event->type() != QEvent::ToolTip || !index.isValid()) {
        return false;
    }
    const auto* model = qobject_cast<const PackListModel*>(index.model());
    if (!model || model->kindAt(index.row()) == Kind::Header) {
        QToolTip::hideText();
        return true;
    }
    const QRect card = cardRect(option.rect);
    const QPoint pos = event->pos();
    QString text;
    if (model->kindAt(index.row()) == Kind::Pack) {
        const auto& row = model->entryAt(index.row()).row;
        if (m_ordered && !row.hidden && switchRect(card).contains(pos)) {
            text = m_locked ? (m_lockReason.isEmpty() ? tr("Close the game to change packs") : m_lockReason)
                            : (row.active ? tr("Turn off") : tr("Turn on"));
        } else if (m_ordered && row.active && gripRect(card).adjusted(-4, -4, 4, 4).contains(pos)) {
            text = m_locked ? (m_lockReason.isEmpty() ? tr("Close the game to change packs") : m_lockReason) : tr("Drag to change the order");
        } else if (row.hidden && unhideRect(card).contains(pos)) {
            text = tr("Show this file to the game again");
        } else if (menuRect(card).contains(pos)) {
            text = tr("More");
        }
    }
    if (text.isEmpty()) {
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

PackListView::PackListView(QWidget* parent) : QListView(parent), m_delegate(new PackDelegate(this))
{
    setObjectName(QStringLiteral("zeroPacksList"));
    setItemDelegate(m_delegate);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setUniformItemSizes(false);
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setMouseTracking(true);
    setAttribute(Qt::WA_MacShowFocusRect, false);
    setStyleSheet(QStringLiteral("QAbstractItemView { background: transparent; border: none; }"));
    viewport()->setAutoFillBackground(false);
    m_scrollTimer.setInterval(30);
    connect(&m_scrollTimer, &QTimer::timeout, this, &PackListView::autoScroll);
}

int PackListView::dropSlotAt(int y) const
{
    const auto* list = packModel();
    if (!list) {
        return -1;
    }
    const QList<int> rows = list->onRows();
    if (rows.isEmpty()) {
        return -1;
    }
    for (int k = 0; k < rows.size(); ++k) {
        const QRect cell = visualRect(list->index(rows.at(k)));
        if (y < cell.top() + PackDelegate::kRowHeight / 2) {
            return k;
        }
    }
    return int(rows.size());
}

int PackListView::slotToIndex(int fromOn, int slot) const
{
    return slot > fromOn ? slot - 1 : slot;
}

void PackListView::cancelDrag()
{
    endDrag(false);
}

void PackListView::endDrag(bool drop)
{
    const bool wasDragging = m_dragging;
    const int from = m_dragFromOn;
    const int to = wasDragging && m_slot >= 0 ? slotToIndex(from, m_slot) : -1;
    m_dragging = false;
    m_dragRow = -1;
    m_dragFromOn = -1;
    m_gripRow = -1;
    m_slot = -1;
    m_scrollTimer.stop();
    m_delegate->setDragRow(-1);
    unsetCursor();
    viewport()->update();
    if (wasDragging && drop && to >= 0 && to != from) {
        emit reorderRequested(from, to);
    }
}

void PackListView::autoScroll()
{
    if (!m_dragging) {
        m_scrollTimer.stop();
        return;
    }
    constexpr int kEdge = 28;
    constexpr int kStep = 10;
    QScrollBar* bar = verticalScrollBar();
    if (m_lastPos.y() < kEdge) {
        bar->setValue(bar->value() - kStep);
    } else if (m_lastPos.y() > viewport()->height() - kEdge) {
        bar->setValue(bar->value() + kStep);
    } else {
        return;
    }
    m_slot = dropSlotAt(m_lastPos.y());
    viewport()->update();
}

void PackListView::mousePressEvent(QMouseEvent* event)
{
    m_pressedSwitchRow = -1;
    m_pressedUnhideRow = -1;
    m_gripRow = -1;
    if (event->button() == Qt::LeftButton) {
        const QPoint pos = event->position().toPoint();
        const QModelIndex index = indexAt(pos);
        if (!index.isValid()) {
            QListView::mousePressEvent(event);
            return;
        }
        const auto* list = packModel();
        if (!list || !list->isPack(index.row())) {
            event->accept();  // a header and the "Default resources" row do nothing
            return;
        }
        const auto& row = list->entryAt(index.row()).row;
        const QRect card = PackDelegate::cardRect(visualRect(index));
        if (m_delegate->ordered() && !row.hidden && PackDelegate::switchRect(card).contains(pos)) {
            m_pressedSwitchRow = index.row();  // acts on release; the selection stays as it is
            event->accept();
            return;
        }
        if (m_delegate->ordered() && row.active && PackDelegate::gripRect(card).adjusted(-4, -4, 4, 4).contains(pos)) {
            setCurrentIndex(index);
            if (!m_delegate->locked()) {
                m_gripRow = index.row();
                m_pressPos = pos;
            }
            event->accept();
            return;
        }
        if (row.hidden && PackDelegate::unhideRect(card).contains(pos)) {
            m_pressedUnhideRow = index.row();
            event->accept();
            return;
        }
        if (PackDelegate::menuRect(card).contains(pos)) {
            event->accept();
            emit menuRequested(index.row(), event->globalPosition().toPoint());
            return;
        }
    }
    QListView::mousePressEvent(event);
}

void PackListView::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_dragging && event->button() == Qt::LeftButton) {
        m_lastPos = event->position().toPoint();
        m_slot = dropSlotAt(m_lastPos.y());
        endDrag(true);
        event->accept();
        return;
    }
    if (m_gripRow >= 0 && event->button() == Qt::LeftButton) {
        m_gripRow = -1;  // a click on a handle without a drag
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && (m_pressedSwitchRow >= 0 || m_pressedUnhideRow >= 0)) {
        const QPoint pos = event->position().toPoint();
        const QModelIndex index = indexAt(pos);
        const QRect card = index.isValid() ? PackDelegate::cardRect(visualRect(index)) : QRect();
        const int sw = m_pressedSwitchRow;
        const int uh = m_pressedUnhideRow;
        m_pressedSwitchRow = -1;
        m_pressedUnhideRow = -1;
        if (sw >= 0 && index.isValid() && index.row() == sw && PackDelegate::switchRect(card).contains(pos)) {
            emit switchClicked(sw);
        } else if (uh >= 0 && index.isValid() && index.row() == uh && PackDelegate::unhideRect(card).contains(pos)) {
            emit unhideClicked(uh);
        }
        event->accept();
        return;
    }
    QListView::mouseReleaseEvent(event);
}

void PackListView::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    if (m_gripRow >= 0 && !m_dragging && (event->buttons() & Qt::LeftButton) &&
        (pos - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
        const auto* list = packModel();
        if (list && list->isPack(m_gripRow) && list->entryAt(m_gripRow).onIndex >= 0) {
            m_dragging = true;
            m_dragRow = m_gripRow;
            m_dragFromOn = list->entryAt(m_gripRow).onIndex;
            m_delegate->setDragRow(m_dragRow);
            setCursor(Qt::ClosedHandCursor);
            m_scrollTimer.start();
        }
        m_gripRow = -1;
    }
    if (m_dragging) {
        m_lastPos = pos;
        m_slot = dropSlotAt(pos.y());
        viewport()->update();
        event->accept();
        return;
    }
    QListView::mouseMoveEvent(event);
    const QModelIndex index = indexAt(pos);
    const auto* list = packModel();
    Qt::CursorShape cursor = Qt::ArrowCursor;
    if (index.isValid() && list && list->isPack(index.row()) && !m_delegate->locked()) {
        const auto& row = list->entryAt(index.row()).row;
        const QRect card = PackDelegate::cardRect(visualRect(index));
        if (m_delegate->ordered() && !row.hidden && PackDelegate::switchRect(card).contains(pos)) {
            cursor = Qt::PointingHandCursor;
        } else if (m_delegate->ordered() && row.active && PackDelegate::gripRect(card).adjusted(-4, -4, 4, 4).contains(pos)) {
            cursor = Qt::OpenHandCursor;
        } else if (row.hidden && PackDelegate::unhideRect(card).contains(pos)) {
            cursor = Qt::PointingHandCursor;
        }
    }
    if (cursor == Qt::ArrowCursor) {
        unsetCursor();
    } else {
        setCursor(cursor);
    }
    viewport()->update();  // the `...` button reacts to the pointer inside its row
}

void PackListView::leaveEvent(QEvent* event)
{
    QListView::leaveEvent(event);
    if (!m_dragging) {
        unsetCursor();
    }
    viewport()->update();
}

void PackListView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && m_dragging) {
        endDrag(false);
        event->accept();
        return;
    }
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
    if ((event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) && event->modifiers() == Qt::AltModifier) {
        emit moveKeyPressed(event->key() == Qt::Key_Up ? -1 : 1);
        event->accept();
        return;
    }
    QListView::keyPressEvent(event);
}

void PackListView::contextMenuEvent(QContextMenuEvent* event)
{
    const QModelIndex index = indexAt(event->pos());
    const auto* list = packModel();
    if (index.isValid() && list && list->isPack(index.row())) {
        emit menuRequested(index.row(), event->globalPos());
        event->accept();
        return;
    }
    QListView::contextMenuEvent(event);
}

void PackListView::paintEvent(QPaintEvent* event)
{
    QListView::paintEvent(event);
    if (!m_dragging || m_slot < 0) {
        return;
    }
    const auto* list = packModel();
    const QList<int> rows = list ? list->onRows() : QList<int>();
    if (rows.isEmpty()) {
        return;
    }
    int y = 0;
    if (m_slot < rows.size()) {
        y = visualRect(list->index(rows.at(m_slot))).top() - PackDelegate::kRowGap / 2;
    } else {
        y = visualRect(list->index(rows.last())).top() + PackDelegate::kRowHeight + PackDelegate::kRowGap / 2;
    }
    const auto& t = Theme::current();
    QPainter p(viewport());
    p.setPen(Qt::NoPen);
    p.setBrush(t.accent);
    p.drawRoundedRect(QRect(0, y - 1, viewport()->width(), 2), 1, 1);  // 2 px, `accent`
}

}  // namespace PacksUi
}  // namespace Zero
