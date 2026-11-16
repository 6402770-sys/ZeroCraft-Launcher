// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroSmoothScroll.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScrollBar>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

#include "ZeroMotion.h"
#include "ZeroTokens.h"

namespace Zero {
namespace SmoothScroll {

namespace {

constexpr int kWheelNotch = 120;  // angleDelta units per notch

/// One running glide of one scroll bar. Lives as a child of the scroll area.
class Glide : public QVariantAnimation {
   public:
    Glide(QScrollBar* bar, QObject* parent) : QVariantAnimation(parent), m_bar(bar)
    {
        connect(this, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            if (!m_bar)
                return;
            m_applying = true;
            m_bar->setValue(v.toInt());
            m_applying = false;
        });
        // Any value change we did not cause (drag, keyboard, programmatic scroll) cancels the glide.
        connect(bar, &QAbstractSlider::valueChanged, this, [this](int) {
            if (!m_applying)
                stop();
        });
    }

    QScrollBar* bar() const { return m_bar; }
    int target() const { return m_target; }

    void glideTo(int target)
    {
        stop();
        m_target = target;
        setDuration(Motion::duration(Tokens::durBase));
        setEasingCurve(Motion::easeEnter());
        setStartValue(m_bar->value());
        setEndValue(target);
        start();
    }

   private:
    QPointer<QScrollBar> m_bar;
    int m_target = 0;
    bool m_applying = false;
};

Glide* findGlide(QAbstractScrollArea* area, QScrollBar* bar)
{
    for (QObject* child : area->children()) {
        auto* g = dynamic_cast<Glide*>(child);
        if (g && g->bar() == bar)
            return g;
    }
    return nullptr;
}

class Filter : public QObject {
   public:
    using QObject::QObject;

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() != QEvent::Wheel)
            return false;
        auto* viewport = qobject_cast<QWidget*>(watched);
        if (!viewport)
            return false;
        auto* area = qobject_cast<QAbstractScrollArea*>(viewport->parentWidget());
        if (!area || area->viewport() != viewport)
            return false;
        if (area->property(kOptOutProperty).toBool())
            return false;
        return handle(area, static_cast<QWheelEvent*>(event));
    }

   private:
    static bool handle(QAbstractScrollArea* area, QWheelEvent* e)
    {
        // Real mouse wheels only; touchpads / precision scrolling keep Qt's own handling.
        if (!e->pixelDelta().isNull() || e->phase() != Qt::NoScrollPhase)
            return false;
        if (e->modifiers().testFlag(Qt::ControlModifier))
            return false;
        const QPoint angle = e->angleDelta();
        if (angle.isNull())
            return false;

        const bool horizontal = e->modifiers().testFlag(Qt::ShiftModifier) || std::abs(angle.x()) > std::abs(angle.y());
        const int delta = horizontal && angle.x() != 0 ? angle.x() : angle.y();
        if (delta == 0)
            return false;
        QScrollBar* bar = horizontal ? area->horizontalScrollBar() : area->verticalScrollBar();
        if (!bar)
            return false;

        if (auto* view = qobject_cast<QAbstractItemView*>(area)) {
            // Pixel steps so the wheel distance means the same thing in every view.
            if (view->verticalScrollMode() != QAbstractItemView::ScrollPerPixel)
                view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
            if (view->horizontalScrollMode() != QAbstractItemView::ScrollPerPixel)
                view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        }

        Glide* glide = findGlide(area, bar);
        const bool running = glide && glide->state() == QAbstractAnimation::Running;
        const int base = running ? glide->target() : bar->value();
        // QPlainTextEdit's bars count lines, not pixels: a notch moves wheelScrollLines() lines there.
        const bool lineBased = !horizontal && qobject_cast<QPlainTextEdit*>(area);
        const double step = double(delta) / kWheelNotch * QApplication::wheelScrollLines() * (lineBased ? 1 : kPixelsPerLine);
        const int target = std::clamp(int(std::lround(base - step)), bar->minimum(), bar->maximum());

        if (target == base) {
            // Edge reached: let Qt propagate to the parent scroll area, unless a glide is still on its way there.
            if (!running)
                return false;
            e->accept();
            return true;
        }

        if (!Motion::enabled()) {
            if (running)
                glide->stop();
            bar->setValue(target);
            e->accept();
            return true;
        }

        if (!glide)
            glide = new Glide(bar, area);
        glide->glideTo(target);
        e->accept();
        return true;
    }
};

}  // namespace

QObject* install(QObject* app)
{
    static QPointer<Filter> existing;
    if (existing)
        return existing.data();
    existing = new Filter(app);
    app->installEventFilter(existing);
    return existing.data();
}

}  // namespace SmoothScroll
}  // namespace Zero
