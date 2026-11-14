// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - slide-in of new rows in the Discover item views.
#include "ContentReveal.h"

#include <QAbstractItemView>
#include <QElapsedTimer>
#include <QHelpEvent>
#include <QPainter>
#include <QStyledItemDelegate>
#include <algorithm>
#include "ui/zero/ZeroMotion.h"

namespace Zero {
namespace Content {

ContentReveal::ContentReveal(QAbstractItemView* view, const RevealSpec& spec) : QObject(view), m_view(view), m_spec(spec)
{
    m_timer.setInterval(kFrameMs);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &ContentReveal::advance);
    attachModel();
}

void ContentReveal::attachModel()
{
    if (m_model)
        disconnect(m_model, nullptr, this, nullptr);
    m_entries.clear();
    m_pendingReset = false;
    m_batchOpen = false;
    m_model = m_view ? m_view->model() : nullptr;
    if (!m_model)
        return;
    connect(m_model, &QAbstractItemModel::rowsInserted, this, &ContentReveal::onRowsInserted);
    connect(m_model, &QAbstractItemModel::modelReset, this, &ContentReveal::onModelReset);
    connect(m_model, &QObject::destroyed, this, [this]() {
        m_entries.clear();
        m_pendingReset = false;
    });
}

void ContentReveal::setClock(std::function<qint64()> clock)
{
    m_clock = std::move(clock);
}

qint64 ContentReveal::now() const
{
    if (m_clock)
        return m_clock();
    static QElapsedTimer s_elapsed = []() {
        QElapsedTimer t;
        t.start();
        return t;
    }();
    return s_elapsed.elapsed();
}

bool ContentReveal::canAnimate() const
{
    return m_view && m_model && Motion::enabled() && m_view->isVisible();
}

const ContentReveal::Entry* ContentReveal::find(const QModelIndex& index) const
{
    if (m_entries.isEmpty() || !index.isValid())
        return nullptr;
    const QModelIndex first = index.siblingAtColumn(0);
    for (const Entry& e : m_entries)
        if (e.index == first)
            return &e;
    return nullptr;
}

int ContentReveal::startDelay(const QModelIndex& index) const
{
    const Entry* e = find(index);
    return e ? int(e->start - e->batchStart) : -1;
}

qreal ContentReveal::progress(const QModelIndex& index) const
{
    if (!Motion::enabled())
        return 1.0;
    if (m_pendingReset)
        return 0.0;  // the rows of the new set stay hidden until their first frame decides who is on screen
    const Entry* e = find(index);
    if (!e)
        return 1.0;
    const qint64 elapsed = now() - e->start;
    if (elapsed <= 0)
        return 0.0;  // waiting for its stagger slot
    const qreal t = std::clamp(qreal(elapsed) / std::max(1, m_spec.durationMs), 0.0, 1.0);
    return std::max(Motion::easeEnter().valueForProgress(t), 1e-4);  // started: never exactly 0
}

void ContentReveal::apply(QPainter* painter, const QRect& itemRect, const QModelIndex& index) const
{
    const qreal p = progress(index);
    if (p >= 1.0)
        return;
    painter->setOpacity(painter->opacity() * (m_spec.startOpacity + (1.0 - m_spec.startOpacity) * p));
    painter->translate((1.0 - p) * m_spec.offsetPx, 0);
    const qreal scale = m_spec.startScale + (1.0 - m_spec.startScale) * p;
    if (!qFuzzyCompare(scale, 1.0)) {
        const QPointF c = QRectF(itemRect).center();
        painter->translate(c);
        painter->scale(scale, scale);
        painter->translate(-c);
    }
}

void ContentReveal::onRowsInserted(const QModelIndex& parent, int first, int last)
{
    if (!canAnimate() || m_pendingReset)
        return;  // a pending reset picks every row anyway
    const qint64 t = now();
    if (!m_batchOpen) {  // the first insertion since the last frame opens the batch; the others of this frame continue it
        m_batchOpen = true;
        m_batchStart = t;
        m_batchCount = 0;
    }
    for (int row = first; row <= last; ++row) {
        const qint64 start = m_batchStart + qint64(std::min(m_batchCount, m_spec.maxSteps - 1)) * m_spec.staggerMs;
        ++m_batchCount;
        m_entries.append(Entry{ QPersistentModelIndex(m_model->index(row, 0, parent)), std::max(start, t), m_batchStart, false });
    }
    ensureTimer();
}

void ContentReveal::beginGeneration()
{
    ++m_generation;
    const bool had = isAnimating();
    m_entries.clear();
    m_pendingReset = false;
    m_batchOpen = false;
    if (had && m_view)
        m_view->viewport()->update();
}

void ContentReveal::onModelReset()
{
    beginGeneration();
    if (!canAnimate() || m_model->rowCount(m_view->rootIndex()) == 0)
        return;
    // The view lays the new rows out after the reset returns: the first frame decides which of them are on screen.
    m_pendingReset = true;
    ensureTimer();
}

void ContentReveal::startPendingReset()
{
    m_pendingReset = false;
    if (!canAnimate())
        return;
    const QRect viewport = m_view->viewport()->rect();
    const QModelIndex root = m_view->rootIndex();
    const int rows = m_model->rowCount(root);
    const qint64 t = now();
    m_batchStart = t;
    for (int row = 0; row < rows; ++row) {
        const QModelIndex index = m_model->index(row, 0, root);
        const QRect r = m_view->visualRect(index);
        if (!r.isValid() || !r.intersects(viewport)) {
            if (r.isValid() && r.top() > viewport.bottom())
                break;  // views lay rows out in order: nothing further down is visible
            continue;
        }
        const qint64 start = t + qint64(std::min(row, m_spec.maxSteps - 1)) * m_spec.staggerMs;
        m_entries.append(Entry{ QPersistentModelIndex(index), start, t, true });
    }
}

void ContentReveal::finish()
{
    const bool had = isAnimating();
    m_entries.clear();
    m_pendingReset = false;
    m_batchOpen = false;
    m_timer.stop();
    if (had && m_view)
        m_view->viewport()->update();
}

void ContentReveal::ensureTimer()
{
    if (!m_timer.isActive() && isAnimating())
        m_timer.start();
}

void ContentReveal::advance()
{
    if (!m_view || !m_model || !Motion::enabled()) {
        finish();
        return;
    }
    m_batchOpen = false;  // a new frame: rows inserted from now on start their own batch
    if (m_pendingReset)
        startPendingReset();
    const qint64 t = now();
    const QRect viewport = m_view->viewport()->rect();
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
                                   [&](Entry& e) {
                                       if (!e.index.isValid() || t >= e.start + m_spec.durationMs)
                                           return true;
                                       if (!e.checked && t >= e.start) {
                                           // The entrance begins now: a row that is not on screen is simply done.
                                           e.checked = true;
                                           const QRect r = m_view->visualRect(e.index);
                                           if (!r.isValid() || !r.intersects(viewport))
                                               return true;
                                       }
                                       return false;
                                   }),
                    m_entries.end());
    m_view->viewport()->update();
    if (!isAnimating())
        m_timer.stop();
}

namespace {

class RevealDelegate : public QStyledItemDelegate {
   public:
    RevealDelegate(QAbstractItemDelegate* inner, ContentReveal* reveal, QObject* parent)
        : QStyledItemDelegate(parent), m_inner(inner), m_reveal(reveal)
    {
        connect(inner, &QAbstractItemDelegate::sizeHintChanged, this, &QAbstractItemDelegate::sizeHintChanged);
        connect(inner, &QAbstractItemDelegate::commitData, this, &QAbstractItemDelegate::commitData);
        connect(inner, &QAbstractItemDelegate::closeEditor, this, &QAbstractItemDelegate::closeEditor);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        if (!m_inner)
            return;
        if (!m_reveal || !m_reveal->isAnimating()) {
            m_inner->paint(painter, option, index);
            return;
        }
        const qreal p = m_reveal->progress(index);
        if (p <= 0.0)
            return;
        if (p >= 1.0) {
            m_inner->paint(painter, option, index);
            return;
        }
        painter->save();
        m_reveal->apply(painter, option.rect, index);
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
    QPointer<ContentReveal> m_reveal;
};

}  // namespace

ContentReveal* installContentReveal(QAbstractItemView* view, const RevealSpec& spec)
{
    auto* reveal = new ContentReveal(view, spec);
    if (QAbstractItemDelegate* inner = view->itemDelegate())
        view->setItemDelegate(new RevealDelegate(inner, reveal, view));
    return reveal;
}

}  // namespace Content
}  // namespace Zero
