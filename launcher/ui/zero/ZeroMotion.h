// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - animation helpers. Everything returns instantly when motion is off (Zero::Theme::reduceMotion(),
// resolved from the ZeroMotion setting, see resolveEnabled()). Defaults to "motion on" when no Application/settings
// exist (tests).
#pragma once

#include <QEasingCurve>
#include <QObject>
#include <QString>
#include <QVariantAnimation>
#include <functional>
#include "ZeroTokens.h"

class QWidget;

namespace Zero {
namespace Motion {

/// False when the user asked for reduced motion.
bool enabled();

/// Pure resolution of the motion setting. `setting` is the ZeroMotion value: "on", "off", "system", or empty when the
/// user never chose (then the legacy ZeroReduceMotion flag decides: true -> off, otherwise on: animations are the default).
/// "system" follows `systemAnimations` (Windows "Animation effects").
bool resolveEnabled(const QString& setting, bool legacyReduceMotion, bool systemAnimations);

/// Windows "Animation effects" (SPI_GETCLIENTAREAANIMATION); true on other platforms or when the query fails.
bool systemAnimationsEnabled();

/// Turns Qt's native popup effects (menu / tooltip / combo fade) on or off together with motion.
void syncPopupEffects(bool motionOn);

/// Easing helpers: enter OutCubic, exit InCubic, glide/emphasis OutQuint, release OutBack (overshoot <= 1.3).
QEasingCurve easeEnter();
QEasingCurve easeExit();
QEasingCurve easeGlide();
QEasingCurve easeRelease();

/// `ms` when motion is enabled, 0 otherwise.
int duration(int ms);

/// Fades `widget` in from transparent (window-level opacity effect, removed when done). Instant if motion is off.
void fadeIn(QWidget* widget, int ms = Tokens::durPage, std::function<void()> finished = {});

/// Fades `widget` out, then hides it and calls `finished` (instant if motion is off).
void fadeOut(QWidget* widget, int ms = Tokens::durPage, std::function<void()> finished = {});

/// Drives a 0..1 progress value that follows a boolean state (hover/focus/press) with a 140 ms OutCubic ease.
/// Owner repaints on `progressChanged`. With reduced motion the value jumps.
class StateAnimator : public QObject {
    Q_OBJECT
   public:
    explicit StateAnimator(QObject* parent = nullptr, int ms = Tokens::durFast);

    qreal progress() const { return m_progress; }
    bool active() const { return m_active; }
    void setActive(bool active);

   signals:
    void progressChanged(qreal progress);

   private:
    void setProgress(qreal p);

    QVariantAnimation m_anim;
    qreal m_progress = 0.0;
    bool m_active = false;
    int m_ms;
};

}  // namespace Motion
}  // namespace Zero
