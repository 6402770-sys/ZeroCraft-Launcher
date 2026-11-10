// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroPlayButton.h"

#include <QFocusEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QtMath>
#include "ZeroIcons.h"
#include "ZeroPaint.h"
#include "ZeroStyle.h"
#include "ZeroThemeState.h"

namespace Zero {

namespace {
constexpr int kHeight = 40;
constexpr int kIconSize = 16;
constexpr int kGap = 8;
constexpr int kSweepMs = 1100;
constexpr int kXpSegments = 12;
constexpr int kShakeMs = 320;
constexpr qreal kShakeAmplitude = 4.0;
constexpr qreal kPressScale = 0.03;
constexpr qreal kBandWidth = 0.4;
constexpr qreal kBandAlpha = 0.18;
constexpr int kFailureHoldMs = 2500;

QFont buttonFont()
{
    QFont f = Style::font(Role::Label);
    f.setWeight(QFont::DemiBold);
    return f;
}

void animateTo(QVariantAnimation& anim, qreal from, qreal to, int ms, const QEasingCurve& curve, qreal& value, QWidget* w)
{
    anim.stop();
    if (ms <= 0) {
        value = to;
        w->update();
        return;
    }
    anim.setEasingCurve(curve);
    anim.setStartValue(from);
    anim.setEndValue(to);
    anim.setDuration(ms);
    anim.start();
}
}  // namespace

ZeroPlayButton::ZeroPlayButton(QWidget* parent)
    : QAbstractButton(parent), m_hover(new Motion::StateAnimator(this)), m_press(this), m_fade(this), m_sweepAnim(this), m_sweepFinish(this), m_shake(this)
{
    setFixedHeight(kHeight);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover, true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);

    connect(m_hover, &Motion::StateAnimator::progressChanged, this, qOverload<>(&QWidget::update));

    connect(&m_press, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_pressValue = v.toReal();
        update();
    });
    connect(this, &QAbstractButton::pressed, this, [this]() {
        if (interactive())
            animateTo(m_press, m_pressValue, 1.0, Motion::duration(Tokens::durPress), Motion::easeEnter(), m_pressValue, this);
    });
    connect(this, &QAbstractButton::released, this,
            [this]() { animateTo(m_press, m_pressValue, 0.0, Motion::duration(Tokens::durFast), Motion::easeRelease(), m_pressValue, this); });

    m_fade.setStartValue(0.0);
    m_fade.setEndValue(1.0);
    m_fade.setEasingCurve(Motion::easeEnter());
    connect(&m_fade, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_fadeValue = v.toReal();
        update();
    });

    // The sweep is an activity indicator: it ignores the motion setting.
    m_sweepAnim.setStartValue(0.0);
    m_sweepAnim.setEndValue(1.0);
    m_sweepAnim.setDuration(kSweepMs);
    m_sweepAnim.setLoopCount(-1);
    m_sweepAnim.setEasingCurve(QEasingCurve::Linear);
    connect(&m_sweepAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_sweepValue = v.toReal();
        update();
    });
    m_sweepFinish.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_sweepFinish, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_sweepValue = v.toReal();
        update();
    });
    connect(&m_sweepFinish, &QVariantAnimation::finished, this, [this]() {
        m_sweeping = false;
        if (m_pendingRunning)
            applyState();
        update();
    });

    m_shake.setStartValue(0.0);
    m_shake.setEndValue(1.0);
    m_shake.setDuration(kShakeMs);
    m_shake.setEasingCurve(QEasingCurve::Linear);
    connect(&m_shake, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        const qreal t = v.toReal();
        m_shakeOffset = kShakeAmplitude * qSin(t * 3.0 * 2.0 * M_PI) * (1.0 - 0.35 * t);
        update();
    });
    connect(&m_shake, &QVariantAnimation::finished, this, [this]() {
        m_shakeOffset = 0.0;
        update();
    });

    m_failureHold.setSingleShot(true);
    m_failureHold.setInterval(kFailureHoldMs);
    connect(&m_failureHold, &QTimer::timeout, this, [this]() {
        if (m_state == State::Failed)
            setState(State::Idle);
    });

    connect(this, &QAbstractButton::clicked, this, &ZeroPlayButton::handleClicked);
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));

    if (auto* app = qobject_cast<Application*>(QCoreApplication::instance())) {
        connect(app, &Application::launchPhaseChanged, this, &ZeroPlayButton::onPhaseChanged);
        connect(app, &Application::launchEnded, this, &ZeroPlayButton::onLaunchEnded);
    }
    m_from = targetLook(State::Idle);
    updateTexts();
}

QString ZeroPlayButton::stateText() const
{
    switch (m_state) {
        case State::Preparing:
            return tr("Launching…");
        case State::Running:
            return tr("Running");
        case State::Failed:
            return tr("Launch failed");
        default:
            return tr("Play");
    }
}

void ZeroPlayButton::setInstanceId(const QString& id)
{
    if (id == m_id)
        return;
    m_id = id;
    m_failureHold.stop();
    m_shake.stop();
    m_shakeOffset = 0.0;
    State s = State::Idle;
    if (auto* app = qobject_cast<Application*>(QCoreApplication::instance())) {
        const auto phase = id.isEmpty() ? Application::LaunchPhase::Idle : app->launchPhase(id);
        s = phase == Application::LaunchPhase::Preparing ? State::Preparing : phase == Application::LaunchPhase::Running ? State::Running : State::Idle;
    }
    m_sweepFinish.stop();
    m_pendingRunning = false;
    m_state = m_visState = m_prevState = s;
    m_fade.stop();
    m_fadeValue = 1.0;
    m_from = targetLook(s);
    setEnabled(m_state != State::Idle || m_canLaunch);
    setCursor(interactive() ? Qt::PointingHandCursor : Qt::ArrowCursor);
    syncSweep();
    updateTexts();
    update();
}

void ZeroPlayButton::setCanLaunch(bool canLaunch)
{
    m_canLaunch = canLaunch;
    setEnabled(m_state != State::Idle || canLaunch);
}

void ZeroPlayButton::onPhaseChanged(const QString& instanceId, Application::LaunchPhase phase)
{
    if (instanceId != m_id || m_id.isEmpty())
        return;
    switch (phase) {
        case Application::LaunchPhase::Preparing:
            setState(State::Preparing);
            break;
        case Application::LaunchPhase::Running:
            setState(State::Running);
            break;
        case Application::LaunchPhase::Idle:
            // A failed launch is shown by onLaunchEnded (which follows); it leaves Failed through its timer.
            if (m_state != State::Failed)
                setState(State::Idle);
            break;
    }
}

void ZeroPlayButton::onLaunchEnded(const QString& instanceId, bool success, bool aborted, bool gameStarted)
{
    if (instanceId != m_id || m_id.isEmpty())
        return;
    if (!success && !aborted && !gameStarted) {
        setState(State::Failed);
        m_failureHold.start();
        m_shake.stop();
        if (Motion::enabled())
            m_shake.start();
    } else if (m_state != State::Idle && m_state != State::Failed) {
        setState(State::Idle);
    }
}

void ZeroPlayButton::setState(State state)
{
    if (state == m_state)
        return;
    const State old = m_state;
    m_state = state;
    if (state != State::Failed)
        m_failureHold.stop();
    if (state == State::Idle || state == State::Preparing) {
        m_shake.stop();
        m_shakeOffset = 0.0;
    }
    m_sweepFinish.stop();
    m_pendingRunning = false;

    setEnabled(m_state != State::Idle || m_canLaunch);
    setCursor(interactive() ? Qt::PointingHandCursor : Qt::ArrowCursor);
    updateTexts();

    if (old == State::Preparing && state == State::Running && m_sweeping && isVisible()) {
        // Finish the sweep first (the painted state stays Preparing), then crossfade to Running.
        m_pendingRunning = true;
        m_sweepAnim.stop();
        m_sweepFinish.setStartValue(m_sweepValue);
        m_sweepFinish.setEndValue(1.0);
        m_sweepFinish.setDuration(qMax(1, int(220 * (1.0 - m_sweepValue))));
        m_sweepFinish.start();
        return;
    }
    applyState();
}

void ZeroPlayButton::applyState()
{
    m_pendingRunning = false;
    m_from = currentLook();
    m_prevState = m_visState;
    m_visState = m_state;
    startFade(m_visState == State::Running ? Tokens::durSlow : Tokens::durFast);
    syncSweep();
}

void ZeroPlayButton::startFade(int ms)
{
    m_fade.stop();
    const int dur = Motion::duration(ms);
    if (dur <= 0) {
        m_fadeValue = 1.0;
        update();
        return;
    }
    m_fadeValue = 0.0;
    m_fade.setDuration(dur);
    m_fade.start();
}

void ZeroPlayButton::syncSweep()
{
    const bool want = m_visState == State::Preparing && isVisible();
    if (want) {
        m_sweeping = true;
        if (m_sweepAnim.state() != QAbstractAnimation::Running)
            m_sweepAnim.start();
    } else {
        m_sweepAnim.stop();
        m_sweeping = m_sweepFinish.state() == QAbstractAnimation::Running;
    }
}

void ZeroPlayButton::updateTexts()
{
    QString tip, name;
    switch (m_state) {
        case State::Preparing:
            name = tr("Launching");
            tip = tr("The game is starting");
            break;
        case State::Running:
            name = tr("Running, stop the game");
            tip = tr("The game is running. Click to stop it.");
            break;
        case State::Failed:
            name = tr("Launch failed");
            tip = tr("The launch failed. Click to try again.");
            break;
        default:
            name = tr("Play");
            tip = tr("Launch the game");
            break;
    }
    setAccessibleName(name);
    setToolTip(tip);
    setText(stateText());
}

ZeroPlayButton::Look ZeroPlayButton::targetLook(State state) const
{
    const auto& t = Theme::current();
    const qreal h = m_hover->progress();
    Look l;
    switch (state) {
        case State::Preparing:
            l.fill = t.accentPressed;
            l.border = withAlpha(t.accentPressed, 0.0);
            l.fg = t.onAccent;
            break;
        case State::Running: {
            const QColor dangerFill = mix(t.elevated, t.danger, Tokens::statusTint);
            l.fill = mix(t.elevated, dangerFill, h);
            l.border = mix(t.border, withAlpha(t.danger, 0.5), h);
            l.fg = mix(t.text, t.danger, h);
            break;
        }
        case State::Failed:
            l.fill = mix(t.elevated, t.danger, Tokens::statusTint);
            l.border = t.danger;
            l.fg = t.danger;
            break;
        default:
            l.fill = mix(t.accent, t.accentHover, h);
            l.border = withAlpha(l.fill, 0.0);
            l.fg = t.onAccent;
            break;
    }
    return l;
}

ZeroPlayButton::Look ZeroPlayButton::currentLook() const
{
    const Look to = targetLook(m_visState);
    if (m_fadeValue >= 1.0)
        return to;
    return { mix(m_from.fill, to.fill, m_fadeValue), mix(m_from.border, to.border, m_fadeValue), mix(m_from.fg, to.fg, m_fadeValue) };
}

QSize ZeroPlayButton::sizeHint() const
{
    const QFontMetrics fm(buttonFont());
    int w = 0;
    for (const QString& s : { tr("Play"), tr("Launching…"), tr("Running"), tr("Stop"), tr("Launch failed") })
        w = qMax(w, fm.horizontalAdvance(s));
    return { qMax(120, 2 * Tokens::spaceXl + kIconSize + kGap + w), kHeight };
}

void ZeroPlayButton::handleClicked()
{
    if (m_id.isEmpty())
        return;
    if (m_state == State::Running)
        emit stopRequested(m_id);
    else if (m_state == State::Idle || m_state == State::Failed)
        emit playRequested(m_id);
}

void ZeroPlayButton::drawContent(QPainter& p, State state, const QRectF& area, const QColor& fg, qreal opacity) const
{
    if (opacity <= 0.0)
        return;
    const auto& t = Theme::current();
    const qreal dpr = devicePixelRatioF();
    const QFont font = buttonFont();
    const QFontMetrics fm(font);
    p.setFont(font);

    auto drawLine = [&](qreal alpha, const QString& label, const QString& iconName, bool dot, qreal iconDx, const QColor& color) {
        if (alpha <= 0.0)
            return;
        const bool hasIcon = !iconName.isEmpty() || dot;
        const int tw = fm.horizontalAdvance(label);
        const qreal total = (hasIcon ? kIconSize + kGap : 0) + tw;
        qreal x = area.center().x() - total / 2.0;
        const qreal cy = area.center().y();
        p.save();
        p.setOpacity(p.opacity() * alpha * opacity);
        if (dot) {
            p.setPen(Qt::NoPen);
            p.setBrush(t.success);
            p.drawEllipse(QPointF(x + kIconSize / 2.0, cy), 4.0, 4.0);
        } else if (!iconName.isEmpty()) {
            p.drawPixmap(QPointF(x + iconDx, cy - kIconSize / 2.0), Icons::pixmap(iconName, color, kIconSize, dpr));
        }
        if (hasIcon)
            x += kIconSize + kGap;
        p.setPen(color);
        p.drawText(QRectF(x, area.top(), tw + 2, area.height()), Qt::AlignVCenter | Qt::AlignLeft, label);
        p.restore();
    };

    const qreal h = m_hover->progress();
    switch (state) {
        case State::Idle:
            drawLine(1.0, tr("Play"), QStringLiteral("play"), false, 2.0 * h, fg);
            break;
        case State::Preparing:
            drawLine(1.0, tr("Launching…"), {}, false, 0, fg);
            break;
        case State::Running:
            drawLine(1.0 - h, tr("Running"), {}, true, 0, t.text);
            drawLine(h, tr("Stop"), QStringLiteral("square"), false, 0, t.danger);
            break;
        case State::Failed:
            drawLine(1.0, tr("Launch failed"), QStringLiteral("triangle-alert"), false, 0, fg);
            break;
    }
}

void ZeroPlayButton::drawSweep(QPainter& p, const QRectF& inner) const
{
    const auto& t = Theme::current();
    if (t.bevel) {
        // XP-style bar along the bottom edge: 12 segments with 1 px gaps, lighting up in sequence.
        const qreal barH = 4.0;
        const QRectF bar(inner.left() + 10.0, inner.bottom() - barH - 7.0, inner.width() - 20.0, barH);
        const qreal segW = (bar.width() - (kXpSegments - 1)) / kXpSegments;
        const int lit = qBound(0, int(m_sweepValue * (kXpSegments + 1)), kXpSegments);
        p.save();
        p.setPen(Qt::NoPen);
        for (int i = 0; i < kXpSegments; ++i) {
            p.setBrush(i < lit ? t.accentHover : withAlpha(t.onAccent, 0.22));
            p.drawRect(QRectF(bar.left() + i * (segW + 1.0), bar.top(), segW, barH));
        }
        p.restore();
        return;
    }
    const qreal w = inner.width();
    const qreal bandW = w * kBandWidth;
    const qreal x = inner.left() - bandW + m_sweepValue * (w + bandW);
    QLinearGradient g(x, 0, x + bandW, 0);
    g.setColorAt(0.0, withAlpha(Qt::white, 0.0));
    g.setColorAt(0.5, withAlpha(Qt::white, kBandAlpha));
    g.setColorAt(1.0, withAlpha(Qt::white, 0.0));
    p.save();
    p.setClipPath(Paint::panelPath(inner, t.radiusMd));
    p.fillRect(QRectF(x, inner.top(), bandW, inner.height()), g);
    p.restore();
}

void ZeroPlayButton::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    if (!isEnabled())
        p.setOpacity(0.4);

    const Look look = currentLook();
    const bool block = t.bevel;
    const qreal press = qBound(0.0, m_pressValue, 1.0);

    QRectF outer = QRectF(rect());
    if (m_keyboardFocus) {
        // Focus ring: 2 px accent2 around the button, the fill steps in by 3 px.
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(t.accent2, 2));
        p.drawPath(Paint::panelPath(outer.adjusted(1, 1, -1, -1), t.radiusMd));
        outer = outer.adjusted(3, 3, -3, -3);
    }
    const QRectF fillRect = outer.adjusted(0.5, 0.5, -0.5, -0.5);
    p.setBrush(look.fill);
    p.setPen(look.border.alpha() > 0 ? QPen(look.border, 1) : QPen(Qt::NoPen));
    p.drawPath(Paint::panelPath(fillRect, t.radiusMd));
    if (block && m_visState != State::Running && m_visState != State::Failed)
        Paint::paintBevel(p, outer, press > 0.5);

    if (m_sweeping)
        drawSweep(p, outer);

    // Content: scales to 0.97 on press (Obsidian) or drops 2 px (Minecraft block press).
    QRectF area = outer;
    if (m_sweeping && block)
        area.setBottom(area.bottom() - 6.0);
    p.save();
    p.translate(m_shakeOffset, 0);
    if (block) {
        p.translate(0, 2.0 * press);
    } else {
        const qreal s = 1.0 - kPressScale * m_pressValue;
        p.translate(area.center());
        p.scale(s, s);
        p.translate(-area.center());
    }
    if (m_fadeValue < 1.0) {
        drawContent(p, m_prevState, area, m_from.fg, 1.0 - m_fadeValue);
        drawContent(p, m_visState, area, look.fg, m_fadeValue);
    } else {
        drawContent(p, m_visState, area, look.fg, 1.0);
    }
    p.restore();
}

void ZeroPlayButton::enterEvent(QEnterEvent* e)
{
    m_hover->setActive(interactive());
    QAbstractButton::enterEvent(e);
}

void ZeroPlayButton::leaveEvent(QEvent* e)
{
    m_hover->setActive(false);
    QAbstractButton::leaveEvent(e);
}

void ZeroPlayButton::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        if (interactive())
            click();
        return;
    }
    QAbstractButton::keyPressEvent(e);
}

void ZeroPlayButton::focusInEvent(QFocusEvent* e)
{
    m_keyboardFocus = e->reason() == Qt::TabFocusReason || e->reason() == Qt::BacktabFocusReason || e->reason() == Qt::ShortcutFocusReason;
    QAbstractButton::focusInEvent(e);
    update();
}

void ZeroPlayButton::focusOutEvent(QFocusEvent* e)
{
    m_keyboardFocus = false;
    QAbstractButton::focusOutEvent(e);
    update();
}

void ZeroPlayButton::showEvent(QShowEvent* e)
{
    QAbstractButton::showEvent(e);
    syncSweep();
}

void ZeroPlayButton::hideEvent(QHideEvent* e)
{
    QAbstractButton::hideEvent(e);
    syncSweep();
}

bool ZeroPlayButton::event(QEvent* e)
{
    // While Preparing the button swallows presses (no press feedback, no click).
    if (!interactive() && (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::MouseButtonDblClick)) {
        e->accept();
        return true;
    }
    return QAbstractButton::event(e);
}

}  // namespace Zero
