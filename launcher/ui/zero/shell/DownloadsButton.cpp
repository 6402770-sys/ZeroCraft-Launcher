// SPDX-License-Identifier: GPL-3.0-only
#include "DownloadsButton.h"

#include <QFocusEvent>
#include <QPainter>
#include <QVariantAnimation>
#include <algorithm>
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "zero/downloads/DownloadCenter.h"

namespace Zero {

namespace {
constexpr int kIconSize = 18;
constexpr int kRingWidth = 2;
constexpr int kBadge = 14;
constexpr int kSpinMs = 900;
constexpr double kArcDegrees = 90.0;
}  // namespace

DownloadsButton::DownloadsButton(Downloads::DownloadCenter* center, QWidget* parent)
    : QAbstractButton(parent),
      m_center(center),
      m_hover(new Motion::StateAnimator(this)),
      m_spin(new QVariantAnimation(this)),
      m_pulse(new QVariantAnimation(this))
{
    setObjectName(QStringLiteral("zeroDownloadsButton"));
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover, true);
    setFixedSize(kSize, kSize);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setAccessibleName(tr("Downloads"));
    setToolTip(tr("Downloads"));
    hide();  // nothing to show until there is a job or a history entry

    m_spin->setStartValue(0.0);
    m_spin->setEndValue(360.0);
    m_spin->setDuration(kSpinMs);
    m_spin->setEasingCurve(QEasingCurve::Linear);
    m_spin->setLoopCount(-1);
    connect(m_spin, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_angle = v.toReal();
        update();
    });

    m_pulse->setStartValue(0.0);
    m_pulse->setEndValue(1.0);
    m_pulse->setDuration(kPulseMs);
    m_pulse->setEasingCurve(Motion::easeEnter());
    connect(m_pulse, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_pulseValue = v.toReal();
        update();
    });
    connect(m_pulse, &QVariantAnimation::finished, this, [this]() {
        m_pulseValue = 1.0;
        update();
    });

    connect(m_hover, &Motion::StateAnimator::progressChanged, this, qOverload<>(&QWidget::update));
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        syncSpin();
        update();
    });

    if (m_center) {
        connect(m_center, &Downloads::DownloadCenter::jobAdded, this, &DownloadsButton::refresh);
        connect(m_center, &Downloads::DownloadCenter::jobChanged, this, &DownloadsButton::refresh);
        connect(m_center, &Downloads::DownloadCenter::historyChanged, this, &DownloadsButton::refresh);
        connect(m_center, &Downloads::DownloadCenter::activeCountChanged, this, &DownloadsButton::refresh);
        connect(m_center, &Downloads::DownloadCenter::jobFinished, this, [this]() {
            refresh();
            pulse();
        });
    }
    refresh();
}

void DownloadsButton::refresh()
{
    int count = 0;
    double fraction = -1;
    QString tip = tr("Downloads");
    bool hasHistory = false;
    if (m_center) {
        const auto jobs = m_center->activeJobs();
        count = int(jobs.size());
        fraction = Downloads::overallFraction(jobs);
        if (count > 0)
            tip = Downloads::summaryText(jobs);
        hasHistory = !m_center->history().isEmpty();
    }
    const bool show = count > 0 || hasHistory;
    if (show == isHidden())
        setVisible(show);
    if (show != m_available) {
        m_available = show;
        emit availabilityChanged(show);
    }
    if (tip != toolTip())
        setToolTip(tip);
    setAccessibleDescription(count > 0 ? tip : QString());
    if (count != m_count || fraction != m_fraction) {
        m_count = count;
        m_fraction = fraction;
        syncSpin();
        update();
    }
}

bool DownloadsButton::pulsing() const
{
    return m_pulse->state() == QAbstractAnimation::Running;
}

void DownloadsButton::syncSpin()
{
    const bool run = isVisible() && m_count > 0 && m_fraction < 0 && Motion::enabled();
    if (run && m_spin->state() != QAbstractAnimation::Running)
        m_spin->start();
    else if (!run && m_spin->state() == QAbstractAnimation::Running)
        m_spin->stop();
    if (!run)
        m_angle = 0;
}

void DownloadsButton::pulse()
{
    if (m_panelOpen || !Motion::enabled() || isHidden())
        return;
    ++m_pulseCount;
    m_pulse->stop();
    m_pulse->start();
}

void DownloadsButton::showEvent(QShowEvent* event)
{
    QAbstractButton::showEvent(event);
    syncSpin();
}

void DownloadsButton::hideEvent(QHideEvent* event)
{
    QAbstractButton::hideEvent(event);
    syncSpin();
}

void DownloadsButton::enterEvent(QEnterEvent* event)
{
    QAbstractButton::enterEvent(event);
    m_hover->setActive(true);
}

void DownloadsButton::leaveEvent(QEvent* event)
{
    QAbstractButton::leaveEvent(event);
    m_hover->setActive(false);
}

void DownloadsButton::focusInEvent(QFocusEvent* event)
{
    m_keyboardFocus = event->reason() == Qt::TabFocusReason || event->reason() == Qt::BacktabFocusReason;
    QAbstractButton::focusInEvent(event);
    update();
}

void DownloadsButton::focusOutEvent(QFocusEvent* event)
{
    m_keyboardFocus = false;
    QAbstractButton::focusOutEvent(event);
    update();
}

void DownloadsButton::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal h = m_hover->progress();
    if (h > 0.0) {
        p.setPen(Qt::NoPen);
        p.setBrush(withAlpha(t.surfaceHover, h));
        p.drawPath(Paint::panelPath(QRectF(rect()), t.radiusMd));
    }
    if (m_keyboardFocus && hasFocus() && !isDown()) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(t.textTertiary, 1));
        p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusMd));
    }

    // pulse: a soft accent glow behind the icon that fades out
    const bool pulsing = m_pulse->state() == QAbstractAnimation::Running;
    const QRectF ring = QRectF(rect()).adjusted(3 + kRingWidth / 2.0, 3 + kRingWidth / 2.0, -3 - kRingWidth / 2.0, -3 - kRingWidth / 2.0);
    if (pulsing) {
        p.setPen(Qt::NoPen);
        p.setBrush(withAlpha(t.accent, 0.35 * (1.0 - m_pulseValue)));
        p.drawEllipse(QRectF(rect()).adjusted(1, 1, -1, -1));
    }

    if (m_count > 0) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(withAlpha(t.border, 1.0), kRingWidth));
        p.drawEllipse(ring);
        QPen arc(t.accent, kRingWidth);
        arc.setCapStyle(Qt::RoundCap);
        p.setPen(arc);
        if (m_fraction >= 0) {
            const double f = std::clamp(m_fraction, 0.0, 1.0);
            // a hair of arc keeps 0 % visible as a dot; a full circle is drawn as one
            const int span = f >= 0.999 ? 360 * 16 : std::max(1, int(f * 360 * 16));
            p.drawArc(ring, 90 * 16, -span);
        } else {
            p.drawArc(ring, int((90.0 - m_angle) * 16), -int(kArcDegrees * 16));
        }
    }

    const QColor iconColor = m_count > 0 ? t.text : mix(t.textSecondary, t.text, h);
    Icons::icon(QStringLiteral("download"), pulsing ? mix(iconColor, t.accent, 1.0 - m_pulseValue) : iconColor)
        .paint(&p, QRect((width() - kIconSize) / 2, (height() - kIconSize) / 2, kIconSize, kIconSize));

    if (m_count >= 2) {
        const QRectF badge(width() - kBadge, 0, kBadge, kBadge);
        p.setPen(QPen(t.surfaceSolid(), 1));
        p.setBrush(t.accent);
        p.drawEllipse(badge);
        QFont f = Style::font(Role::Caption);
        f.setPixelSize(9);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(t.onAccent);
        p.drawText(badge, Qt::AlignCenter, m_count > 9 ? QStringLiteral("9+") : QString::number(m_count));
    }
}

}  // namespace Zero
