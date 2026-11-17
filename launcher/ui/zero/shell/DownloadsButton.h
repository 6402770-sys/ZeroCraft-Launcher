// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the Downloads button of the top bar (docs/zero/SHELL.md, "Downloads center"). 32 x 32, between the page actions and the
// account chip. Hidden while the center has no running job and no history. While jobs run: a 2 px accent ring around the icon shows the
// overall fraction (an unknown fraction = a rotating 90 degree arc, static with reduced motion) and a count badge appears from 2 jobs on.
// The tooltip reads "Downloads" or "Downloading 2 items · 45%". When a job ends while the panel is closed the button pulses once.
#pragma once

#include <QAbstractButton>
#include <QPointer>

class QVariantAnimation;

namespace Zero {

namespace Downloads {
class DownloadCenter;
}
namespace Motion {
class StateAnimator;
}

class DownloadsButton : public QAbstractButton {
    Q_OBJECT
   public:
    static constexpr int kSize = 32;
    static constexpr int kPulseMs = 600;

    explicit DownloadsButton(Downloads::DownloadCenter* center, QWidget* parent = nullptr);

    /// While the panel is open, a finishing job does not pulse the button.
    void setPanelOpen(bool open) { m_panelOpen = open; }
    /// True while the button is shown (running job or history).
    bool available() const { return m_available; }

    /// Number of running jobs and the overall fraction (-1 = unknown) the ring shows.
    int activeCount() const { return m_count; }
    double fraction() const { return m_fraction; }
    /// True while the pulse plays.
    bool pulsing() const;
    /// Number of pulses started so far (tests).
    int pulseCount() const { return m_pulseCount; }

    QSize sizeHint() const override { return { kSize, kSize }; }
    QSize minimumSizeHint() const override { return sizeHint(); }

   signals:
    /// The button appeared or disappeared (the top bar divider follows).
    void availabilityChanged(bool available);

   protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

   private:
    void refresh();
    void syncSpin();
    void pulse();
    bool m_available = false;

    QPointer<Downloads::DownloadCenter> m_center;
    Motion::StateAnimator* m_hover;
    QVariantAnimation* m_spin;
    QVariantAnimation* m_pulse;
    int m_count = 0;
    double m_fraction = -1;
    double m_angle = 0;  ///< degrees, the rotating arc
    double m_pulseValue = 1.0;
    int m_pulseCount = 0;
    bool m_panelOpen = false;
    bool m_keyboardFocus = false;
};

}  // namespace Zero
