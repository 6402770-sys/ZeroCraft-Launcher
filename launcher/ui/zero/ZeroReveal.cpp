// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroReveal.h"

#include <QAbstractItemView>
#include <QHelpEvent>
#include <QPainter>
#include <QStyledItemDelegate>
#include <algorithm>
#include "ZeroMotion.h"
#include "ZeroTokens.h"

namespace Zero {
namespace Motion {

ItemReveal::ItemReveal(QAbstractItemView* view) : QObject(view), m_view(view)
{
    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, &ItemReveal::tick);
}

void ItemReveal::start()
{
    if (!m_view || !enabled() || !m_view->isVisible())
        return;
    m_clock.restart();
    m_running = true;
    m_timer.start();
    m_view->viewport()->update();
}

void ItemReveal::finish()
{
    if (!m_running)
        return;
    m_running = false;
    m_timer.stop();
    if (m_view)
        m_view->viewport()->update();
}

int ItemReveal::delayFor(const QRect& itemRect)
{
    const int order = std::max(0, itemRect.top()) / kStepY + std::max(0, itemRect.left()) / kStepX;
    return std::min(order * Tokens::durStagger, kMaxDelay);
}

qreal ItemReveal::progress(const QRect& itemRect) const
{
    if (!m_running)
        return 1.0;
    const qreal t = qreal(m_clock.elapsed() - delayFor(itemRect)) / Tokens::durReveal;
    return easeEnter().valueForProgress(std::clamp(t, 0.0, 1.0));
}

void ItemReveal::apply(QPainter* painter, const QRect& itemRect) const
{
    const qreal p = progress(itemRect);
    if (p >= 1.0)
        return;
    painter->setOpacity(painter->opacity() * p);
    painter->translate(0, (1.0 - p) * kRisePx);
}

void ItemReveal::tick()
{
    if (!m_view) {
        m_timer.stop();
        m_running = false;
        return;
    }
    if (m_clock.elapsed() > kMaxDelay + Tokens::durReveal + 16)
        finish();
    else
        m_view->viewport()->update();
}

namespace {

class RevealDelegate : public QStyledItemDelegate {
   public:
    RevealDelegate(QAbstractItemDelegate* inner, ItemReveal* reveal, QObject* parent) : QStyledItemDelegate(parent), m_inner(inner), m_reveal(reveal)
    {
        connect(inner, &QAbstractItemDelegate::sizeHintChanged, this, &QAbstractItemDelegate::sizeHintChanged);
        connect(inner, &QAbstractItemDelegate::commitData, this, &QAbstractItemDelegate::commitData);
        connect(inner, &QAbstractItemDelegate::closeEditor, this, &QAbstractItemDelegate::closeEditor);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        if (!m_inner)
            return;
        if (!m_reveal || !m_reveal->running()) {
            m_inner->paint(painter, option, index);
            return;
        }
        if (m_reveal->progress(option.rect) <= 0.0)
            return;
        painter->save();
        m_reveal->apply(painter, option.rect);
        m_inner->paint(painter, option, index);
        painter->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        return m_inner ? m_inner->sizeHint(option, index) : QSize();
    }
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index) override
    {
        return m_inner && m_inner->editorEvent(event, model, option, index);
    }
    bool helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option, const QModelIndex& index) override
    {
        return m_inner && m_inner->helpEvent(event, view, option, index);
    }
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        return m_inner ? m_inner->createEditor(parent, option, index) : nullptr;
    }
    void destroyEditor(QWidget* editor, const QModelIndex& index) const override
    {
        if (m_inner)
            m_inner->destroyEditor(editor, index);
    }
    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        if (m_inner)
            m_inner->setEditorData(editor, index);
    }
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override
    {
        if (m_inner)
            m_inner->setModelData(editor, model, index);
    }
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        if (m_inner)
            m_inner->updateEditorGeometry(editor, option, index);
    }

   private:
    QPointer<QAbstractItemDelegate> m_inner;
    QPointer<ItemReveal> m_reveal;
};

}  // namespace

ItemReveal* installItemReveal(QAbstractItemView* view)
{
    auto* reveal = new ItemReveal(view);
    QAbstractItemDelegate* inner = view->itemDelegate();
    if (inner)
        view->setItemDelegate(new RevealDelegate(inner, reveal, view));
    return reveal;
}

}  // namespace Motion
}  // namespace Zero
