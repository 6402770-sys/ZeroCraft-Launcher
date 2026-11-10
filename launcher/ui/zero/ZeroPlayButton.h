// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the signature Play button. Painted by hand, driven by the real launch phase of one instance
// (Application::launchPhaseChanged / launchEnded), so it is right wherever the launch was started.
#pragma once

#include <QAbstractButton>
#include <QColor>
#include <QTimer>
#include <QVariantAnimation>
#include "Application.h"
#include "ZeroMotion.h"

namespace Zero {

/// States: Idle (accent "Play") -> Preparing (sweep / XP bar, "Launching...", clicks ignored) -> Running (elevated,
/// success dot "Running"; hover morphs to a danger "Stop") -> Idle. A launch that ends without the game ever running
/// (and not aborted by the user) shows "Launch failed" for a moment, with a shake, then returns to Idle.
/// Docs: docs/zero/SHELL.md ("Play button").
class ZeroPlayButton : public QAbstractButton {
    Q_OBJECT
   public:
    enum class State { Idle, Preparing, Running, Failed };

    explicit ZeroPlayButton(QWidget* parent = nullptr);

    /// Binds the button to an instance and adopts its current launch phase without animation. Empty = unbound.
    void setInstanceId(const QString& id);
    QString instanceId() const { return m_id; }

    State state() const { return m_state; }
    /// The label of the current state ("Play", "Launching...", "Running", "Launch failed").
    QString stateText() const;

    /// Whether an Idle button may start a launch (disabled otherwise). Running / Preparing buttons stay enabled.
    void setCanLaunch(bool canLaunch);

    /// How long "Launch failed" stays before the button returns to Idle (2500 ms; tests shorten it).
    void setFailureHoldMs(int ms) { m_failureHold.setInterval(ms); }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

   public slots:
    /// Slots of the Application signals (public so tests can drive the state machine directly).
    void onPhaseChanged(const QString& instanceId, Application::LaunchPhase phase);
    void onLaunchEnded(const QString& instanceId, bool success, bool aborted, bool gameStarted);

   signals:
    void playRequested(const QString& instanceId);
    void stopRequested(const QString& instanceId);

   protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool event(QEvent* event) override;

   private:
    struct Look {
        QColor fill, border, fg;
    };

    void setState(State state);
    void applyState();  // visual transition to m_state (after the sweep finished, when entering Running)
    void startFade(int ms);
    Look targetLook(State state) const;
    Look currentLook() const;
    void drawContent(QPainter& p, State state, const QRectF& area, const QColor& fg, qreal opacity) const;
    void drawSweep(QPainter& p, const QRectF& inner) const;
    void updateTexts();
    void syncSweep();
    void handleClicked();
    bool interactive() const { return m_state != State::Preparing; }

    QString m_id;
    State m_state = State::Idle;
    State m_visState = State::Idle;   // what is painted as the target (lags m_state while the sweep finishes)
    State m_prevState = State::Idle;  // crossfade source
    bool m_canLaunch = true;
    bool m_pendingRunning = false;
    bool m_sweeping = false;
    bool m_keyboardFocus = false;

    Motion::StateAnimator* m_hover;
    QVariantAnimation m_press;
    QVariantAnimation m_fade;
    QVariantAnimation m_sweepAnim;
    QVariantAnimation m_sweepFinish;
    QVariantAnimation m_shake;
    QTimer m_failureHold;
    qreal m_pressValue = 0.0;
    qreal m_fadeValue = 1.0;
    qreal m_sweepValue = 0.0;
    qreal m_shakeOffset = 0.0;
    Look m_from;
};

}  // namespace Zero
