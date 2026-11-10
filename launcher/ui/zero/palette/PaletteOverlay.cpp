// SPDX-License-Identifier: GPL-3.0-only
#include "PaletteOverlay.h"

#include <QApplication>
#include <QEasingCurve>
#include <QKeyEvent>
#include <QLayout>
#include <QMouseEvent>
#include <QPainter>

#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroTokens.h"

namespace Zero {

namespace {
constexpr qreal kBackdropAlpha = 0.55;
constexpr int kOpenMs = 180;
constexpr int kBackdropMs = 160;
constexpr int kCloseMs = 120;
constexpr int kRisePx = 8;
constexpr qreal kStartScale = 0.98;
constexpr double kTopFraction = 0.14;
constexpr int kEdgeMargin = 16;
constexpr int kOffscreen = -6000;
}  // namespace

void PalettePanel::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(t.border);
    p.drawPath(Paint::panelPath(QRectF(rect()), t.radiusXl));
    p.setBrush(t.surface);
    p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(1, 1, -1, -1), qMax(0, t.radiusXl - 1)));
}

PaletteOverlay::PaletteOverlay(QWidget* host) : QWidget(host), m_panel(new PalettePanel(this)), m_host(host)
{
    setAttribute(Qt::WA_NoSystemBackground, true);
    setFocusPolicy(Qt::NoFocus);
    hide();
    host->installEventFilter(this);
    m_anim.setStartValue(0.0);
    m_anim.setEndValue(1.0);
    m_anim.setEasingCurve(QEasingCurve::Linear);
    connect(&m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_progress = v.toReal();
        update();
    });
    connect(&m_anim, &QVariantAnimation::finished, this, [this]() {
        if (m_opening)
            finishOpen();
        else
            finishClose();
    });
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void PaletteOverlay::updatePanelGeometry()
{
    const int w = qMin(preferredWidth(), int(m_host->width() * 0.9));
    const int h = panelHeight();
    int top = int(m_host->height() * kTopFraction);
    top = qMax(kEdgeMargin, qMin(top, m_host->height() - h - kEdgeMargin));
    m_panelRect = QRect((m_host->width() - w) / 2, top, w, h);
    if (!m_animating)
        m_panel->setGeometry(m_panelRect);
    update();
}

void PaletteOverlay::open()
{
    if (m_open)
        return;
    if (m_animating) {
        m_anim.stop();
        finishClose();
    }
    m_previousFocus = QApplication::focusWidget();
    aboutToOpen();
    setGeometry(m_host->rect());
    raise();
    show();
    m_open = true;
    updatePanelGeometry();
    m_panel->show();
    if (QWidget* focus = initialFocus())
        focus->setFocus(Qt::PopupFocusReason);
    if (Motion::enabled()) {
        startAnimation(true);
    } else {
        m_progress = 1.0;
        m_animating = false;
    }
    emit opened();
}

void PaletteOverlay::closeOverlay()
{
    if (!m_open)
        return;
    m_open = false;
    if (m_previousFocus && m_previousFocus->isVisible() && m_previousFocus->isEnabled())
        m_previousFocus->setFocus(Qt::OtherFocusReason);
    if (Motion::enabled()) {
        startAnimation(false);
    } else {
        m_anim.stop();
        finishClose();
    }
}

void PaletteOverlay::startAnimation(bool opening)
{
    m_anim.stop();
    m_animating = false;
    m_panel->setGeometry(m_panelRect);
    if (m_panel->layout())
        m_panel->layout()->activate();
    m_snapshot = m_panel->grab();
    m_panel->move(kOffscreen, kOffscreen);  // keeps focus and state; the snapshot is what is painted meanwhile
    m_animating = true;
    m_opening = opening;
    m_anim.setDuration(opening ? kOpenMs : kCloseMs);
    m_progress = 0.0;
    m_anim.start();
    update();
}

void PaletteOverlay::finishOpen()
{
    m_animating = false;
    m_snapshot = QPixmap();
    m_panel->setGeometry(m_panelRect);
    update();
}

void PaletteOverlay::finishClose()
{
    m_animating = false;
    m_snapshot = QPixmap();
    hide();
    m_panel->setGeometry(m_panelRect);
    emit closed();
}

bool PaletteOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_host && event->type() == QEvent::Resize && isVisible()) {
        if (m_animating) {
            m_anim.stop();
            if (m_opening)
                finishOpen();
            else
                finishClose();
        }
        setGeometry(m_host->rect());
        updatePanelGeometry();
    }
    return QWidget::eventFilter(watched, event);
}

void PaletteOverlay::paintShadow(QPainter& p, const QRectF& rect, qreal opacity) const
{
    // Popover shadow: blur 24, offset 0 / 8, 55 % black, approximated by stacked translucent panels.
    const int layers = 12;
    const qreal perLayer = (Tokens::shadowAlpha / 255.0) * 0.118 * opacity;
    p.setPen(Qt::NoPen);
    const int radius = Theme::current().radiusXl;
    for (int i = layers; i >= 1; --i) {
        const qreal grow = Tokens::shadowBlur * (qreal(i) / layers) * 0.9;
        QColor c(0, 0, 0);
        c.setAlphaF(qMin(1.0, perLayer));
        p.setBrush(c);
        p.drawPath(Paint::panelPath(rect.translated(0, Tokens::shadowOffsetY).adjusted(-grow, -grow, grow, grow), radius + grow));
    }
}

void PaletteOverlay::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    qreal backdrop = 1.0, panelOpacity = 1.0, rise = 0.0, scale = 1.0;
    if (m_animating) {
        const qreal tt = m_progress;
        if (m_opening) {
            backdrop = qMin(1.0, tt * kOpenMs / kBackdropMs);
            const qreal e = QEasingCurve(QEasingCurve::OutQuint).valueForProgress(tt);
            panelOpacity = e;
            rise = kRisePx * (1.0 - e);
            scale = kStartScale + (1.0 - kStartScale) * e;
        } else {
            backdrop = 1.0 - tt;
            panelOpacity = 1.0 - tt;
        }
    }
    QColor bd = t.bg;
    bd.setAlphaF(kBackdropAlpha * backdrop);
    p.fillRect(rect(), bd);

    if (m_animating && !m_snapshot.isNull()) {
        const QPointF c = QRectF(m_panelRect).center() + QPointF(0, rise);
        const QSizeF size(m_panelRect.width() * scale, m_panelRect.height() * scale);
        const QRectF target(c.x() - size.width() / 2, c.y() - size.height() / 2, size.width(), size.height());
        paintShadow(p, target, panelOpacity);
        p.setOpacity(panelOpacity);
        p.drawPixmap(target, m_snapshot, QRectF(QPointF(0, 0), QSizeF(m_snapshot.size())));
    } else if (m_open) {
        paintShadow(p, QRectF(m_panelRect), 1.0);
    }
}

void PaletteOverlay::mousePressEvent(QMouseEvent* event)
{
    if (!m_panelRect.contains(event->position().toPoint()))
        closeOverlay();
    event->accept();
}

void PaletteOverlay::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        closeOverlay();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

}  // namespace Zero
