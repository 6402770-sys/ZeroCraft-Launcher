// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - animation helpers. Everything returns instantly when the ZeroReduceMotion setting is on
// (Zero::Theme::reduceMotion()). Defaults to "motion on" when no Application/settings exist (tests).
#pragma once

#include <QEasingCurve>
#include <QObject>
#include <QVariantAnimation>
#include <functional>
#include "ZeroTokens.h"

class QWidget;

namespace Zero {
namespace Motion {

/// False when the user asked for reduced motion.
bool enabled();

/// `ms` when motion is enabled, 0 otherwise.
int duration(int ms);

/// Fades `widget` in from transparent (window-level opacity effect, removed when done). Instant if motion is off.
void fadeIn(QWidget* widget, int ms = Tokens::durPage, std::function<void()> finished = {});

/// Fades `widget` out, then hides it and calls `finished` (instant if motion is off).
void fadeOut(QWidget* widget, int ms = Tokens::durPage, std::function<void()> finished = {});

/// Drives a 0..1 progress value that follows a boolean state (hover/focus/press) with a 120 ms OutCubic ease.
/// Owner repaints on `progressChanged`. With reduced motion the value jumps.
class StateAnimator : public QObject {
    Q_OBJECT
   public:
    explicit StateAnimator(QObject* parent = nullptr, int ms = Tokens::durHover);

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
