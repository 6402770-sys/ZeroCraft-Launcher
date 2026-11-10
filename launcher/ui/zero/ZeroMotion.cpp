// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroMotion.h"

#include <QApplication>
#include <QGraphicsOpacityEffect>
#include <QPointer>
#include <QPropertyAnimation>
#include <QTimer>
#include <QWidget>
#include "ZeroThemeState.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace Zero {
namespace Motion {

bool enabled()
{
    return !Theme::reduceMotion();
}

int duration(int ms)
{
    return enabled() ? ms : 0;
}

bool resolveEnabled(const QString& setting, bool legacyReduceMotion, bool systemAnimations)
{
    if (setting == QLatin1String("on"))
        return true;
    if (setting == QLatin1String("off"))
        return false;
    if (setting == QLatin1String("system"))
        return systemAnimations;
    // Never chosen (or unknown value): an old ZeroReduceMotion=true keeps meaning "off", otherwise follow the system.
    return legacyReduceMotion ? false : systemAnimations;
}

bool systemAnimationsEnabled()
{
#ifdef Q_OS_WIN
    BOOL on = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0))
        return on != FALSE;
#endif
    return true;
}

void syncPopupEffects(bool motionOn)
{
    QApplication::setEffectEnabled(Qt::UI_FadeMenu, motionOn);
    QApplication::setEffectEnabled(Qt::UI_FadeTooltip, motionOn);
    QApplication::setEffectEnabled(Qt::UI_AnimateCombo, motionOn);
}

QEasingCurve easeEnter()
{
    return QEasingCurve(QEasingCurve::OutCubic);
}

QEasingCurve easeExit()
{
    return QEasingCurve(QEasingCurve::InCubic);
}

QEasingCurve easeGlide()
{
    return QEasingCurve(QEasingCurve::OutQuint);
}

QEasingCurve easeRelease()
{
    QEasingCurve c(QEasingCurve::OutBack);
    c.setOvershoot(1.3);
    return c;
}

namespace {

// Runs an opacity animation on `widget`. Only installs its own effect when the widget has none, so widgets that
// already carry an effect (e.g. a shadow) are never replaced; those fall back to an instant change.
void runOpacity(QWidget* widget, qreal from, qreal to, int ms, std::function<void()> finished)
{
    if (!widget)
        return;
    if (duration(ms) == 0 || (widget->graphicsEffect() && !qobject_cast<QGraphicsOpacityEffect*>(widget->graphicsEffect()))) {
        if (finished)
            finished();
        return;
    }
    auto* effect = qobject_cast<QGraphicsOpacityEffect*>(widget->graphicsEffect());
    if (!effect) {
        effect = new QGraphicsOpacityEffect(widget);
        widget->setGraphicsEffect(effect);
    }
    effect->setOpacity(from);
    auto* anim = new QPropertyAnimation(effect, "opacity", effect);
    anim->setDuration(ms);
    anim->setStartValue(from);
    anim->setEndValue(to);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    QPointer<QWidget> guard(widget);
    QObject::connect(anim, &QPropertyAnimation::finished, effect, [guard, effect, finished]() {
        if (finished)
            finished();
        // Remove the effect afterwards (deferred: we are inside the animation's own signal) so the widget does
        // not stay on the slower offscreen render path, and so a faded-out widget is opaque when shown again.
        if (guard) {
            QTimer::singleShot(0, guard.data(), [guard, effect]() {
                if (guard && guard->graphicsEffect() == effect)
                    guard->setGraphicsEffect(nullptr);
            });
        }
    });
    anim->start();
}

}  // namespace

void fadeIn(QWidget* widget, int ms, std::function<void()> finished)
{
    runOpacity(widget, 0.0, 1.0, ms, std::move(finished));
}

void fadeOut(QWidget* widget, int ms, std::function<void()> finished)
{
    QPointer<QWidget> guard(widget);
    runOpacity(widget, 1.0, 0.0, ms, [guard, finished]() {
        if (guard)
            guard->hide();
        if (finished)
            finished();
    });
}

StateAnimator::StateAnimator(QObject* parent, int ms) : QObject(parent), m_anim(this), m_ms(ms)
{
    m_anim.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { setProgress(v.toReal()); });
}

void StateAnimator::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    const qreal target = active ? 1.0 : 0.0;
    m_anim.stop();
    if (duration(m_ms) == 0) {
        setProgress(target);
        return;
    }
    m_anim.setDuration(duration(m_ms));
    m_anim.setStartValue(m_progress);
    m_anim.setEndValue(target);
    m_anim.start();
}

void StateAnimator::setProgress(qreal p)
{
    if (qFuzzyCompare(m_progress + 1.0, p + 1.0))
        return;
    m_progress = p;
    emit progressChanged(p);
}

}  // namespace Motion
}  // namespace Zero
