// SPDX-License-Identifier: GPL-3.0-only
#include "DropOverlay.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QFontMetrics>
#include <QMimeData>
#include <QPainter>
#include <QPen>

#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {

namespace {
constexpr int kIconSize = 40;
constexpr double kVeilAlpha = 0.88;
}  // namespace

DropOverlay::DropOverlay(QWidget* window) : QWidget(window)
{
    setObjectName(QStringLiteral("ZeroDropOverlay"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
    setAcceptDrops(false);
    hide();
    if (window) {
        setGeometry(window->rect());
    }
    qApp->installEventFilter(this);  // Resize of the window and every drag event of its widgets

    m_leaveTimer.setSingleShot(true);
    m_leaveTimer.setInterval(kLeaveGraceMs);
    connect(&m_leaveTimer, &QTimer::timeout, this, [this]() { hideOverlay(); });

    m_anim.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_opacity = value.toReal();
        update();
    });
    connect(&m_anim, &QVariantAnimation::finished, this, [this]() {
        if (!m_shown) {
            hide();
        }
    });
    connect(&Theme::instance(), &Theme::changed, this, [this]() { update(); });
}

DropOverlay::~DropOverlay()
{
    if (qApp) {
        qApp->removeEventFilter(this);
    }
}

void DropOverlay::animateTo(qreal target)
{
    m_anim.stop();
    const int ms = Motion::duration(kFadeMs);
    if (ms == 0) {
        m_opacity = target;
        update();
        if (target <= 0.0) {
            hide();
        }
        return;
    }
    m_anim.setDuration(ms);
    m_anim.setStartValue(m_opacity);
    m_anim.setEndValue(target);
    m_anim.start();
}

void DropOverlay::showOverlay()
{
    m_leaveTimer.stop();
    if (m_shown) {
        return;
    }
    m_shown = true;
    if (QWidget* window = parentWidget()) {
        setGeometry(window->rect());
    }
    show();
    raise();
    animateTo(1.0);
}

void DropOverlay::hideOverlay()
{
    m_leaveTimer.stop();
    if (!m_shown) {
        return;
    }
    m_shown = false;
    animateTo(0.0);
}

bool DropOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        setGeometry(parentWidget()->rect());
        return false;
    }
    switch (event->type()) {
        case QEvent::DragEnter:
        case QEvent::DragMove:
        case QEvent::DragLeave:
        case QEvent::Drop:
            break;
        default:
            return false;
    }
    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget || widget == this || widget->window() != parentWidget()) {
        return false;
    }
    switch (event->type()) {
        case QEvent::DragEnter:
            if (static_cast<QDragEnterEvent*>(event)->mimeData()->hasUrls()) {
                showOverlay();
            }
            break;
        case QEvent::DragMove:
            if (m_shown) {
                m_leaveTimer.stop();
            }
            break;
        case QEvent::DragLeave:
            if (m_shown) {
                m_leaveTimer.start();
            }
            break;
        case QEvent::Drop:
            hideOverlay();
            break;
        default:
            break;
    }
    return false;
}

void DropOverlay::paintEvent(QPaintEvent*)
{
    if (m_opacity <= 0.0) {
        return;
    }
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setOpacity(m_opacity);

    QColor veil = t.bg;
    veil.setAlphaF(kVeilAlpha);
    p.fillRect(rect(), veil);

    const QRectF frame = QRectF(rect()).adjusted(Tokens::spaceXl, Tokens::spaceXl, -Tokens::spaceXl, -Tokens::spaceXl);
    QPen pen(t.accent, 2.0, Qt::DashLine);
    pen.setCapStyle(Qt::FlatCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(frame.adjusted(1, 1, -1, -1), t.radiusLg, t.radiusLg);

    const QString titleText = tr("Drop to add");
    const QString captionText = tr("Mods, resource packs, shaders, worlds and modpacks");
    const QFont titleFont = Style::font(Role::Title);
    const QFont captionFont = Style::font(Role::Body);
    const QFontMetrics titleMetrics(titleFont);
    const QFontMetrics captionMetrics(captionFont);
    const int gap = Tokens::spaceMd;
    const int total = kIconSize + gap + titleMetrics.height() + Tokens::spaceXs + captionMetrics.height();
    int y = int(frame.center().y() - total / 2.0);
    const qreal dpr = devicePixelRatioF();
    const QPixmap icon = Icons::pixmap(QStringLiteral("upload"), t.accent, kIconSize, dpr);
    p.drawPixmap(int(frame.center().x() - kIconSize / 2.0), y, icon);
    y += kIconSize + gap;

    p.setFont(titleFont);
    p.setPen(t.text);
    p.drawText(QRectF(frame.left(), y, frame.width(), titleMetrics.height()), Qt::AlignHCenter | Qt::AlignVCenter, titleText);
    y += titleMetrics.height() + Tokens::spaceXs;
    p.setFont(captionFont);
    p.setPen(t.textSecondary);
    p.drawText(QRectF(frame.left(), y, frame.width(), captionMetrics.height()), Qt::AlignHCenter | Qt::AlignVCenter,
               captionMetrics.elidedText(captionText, Qt::ElideRight, int(frame.width()) - 2 * Tokens::spaceXl));
}

}  // namespace Zero
