// SPDX-License-Identifier: GPL-3.0-only
/*
 *  ZERO Launcher - first-run intro: a full-window overlay with the brand night scene (the installer's look) that plays the very
 *  first time ZERO opens on a data directory and ends with "Get started". docs/zero/SHELL.md, "First-run intro".
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

#include <QElapsedTimer>
#include <QPixmap>
#include <QPointer>
#include <QWidget>

#include "ui/zero/ZeroScene.h"
#include "ui/zero/onboarding/IntroGate.h"

class QAbstractButton;
class QGraphicsOpacityEffect;
class QPushButton;
class QTimer;
class SettingsObject;

namespace Zero {
namespace Onboarding {

/// The intro. Child of the main window: covers it completely (follows its size), takes the focus and blocks input to the shell until
/// the user presses "Get started". Emits finished() once, after the fade-out, and deletes itself.
class IntroOverlay : public QWidget {
    Q_OBJECT
   public:
    // Timeline (ms from the start, the Motion clock). Docs: SHELL.md "First-run intro".
    static constexpr int kSkyEndMs = 1800;        ///< sky fades in, stars twinkle, the mark builds block by block (0 .. 1.8 s)
    static constexpr int kGroundStartMs = 1200;   ///< the ground rises from the bottom (1.2 .. 2.4 s)
    static constexpr int kGroundEndMs = 2400;
    static constexpr int kWordStartMs = 1800;     ///< "ZERO" fades in and slides up kWordRisePx (1.8 .. 2.8 s)
    static constexpr int kWordEndMs = 2800;
    static constexpr int kCaptionStartMs = 2600;  ///< "Welcome to ZERO Launcher" (2.6 .. 3.4 s)
    static constexpr int kCaptionEndMs = 3400;
    static constexpr int kButtonStartMs = 3200;   ///< "Get started" fades in and gets the focus (3.2 .. 3.8 s)
    static constexpr int kButtonEndMs = 3800;
    static constexpr int kFinalMs = kButtonEndMs; ///< the final frame: Esc / Skip / a click before the button appeared jump here
    static constexpr int kFadeOutMs = 300;        ///< Get started: the overlay fades out (OutCubic)
    static constexpr int kReducedFadeMs = 200;    ///< reduced motion: one fade in, nothing else moves
    static constexpr int kWordRisePx = 12;

    /// `host` is the window to cover (the overlay becomes its child). `settings` (optional) receives ZeroIntroSeen = true when the user
    /// presses Get started.
    explicit IntroOverlay(QWidget* host, SettingsObject* settings = nullptr);
    ~IntroOverlay() override;

    /// Time on the intro's own clock: the real elapsed time (plus what a jump added), a frozen value (dev aid) or, with reduced
    /// motion, kFinalMs.
    qint64 timelineMs() const;
    /// The final frame is on screen (the button is fully in).
    bool isFinal() const { return timelineMs() >= kFinalMs; }
    /// "Get started" is shown (it appears at kButtonStartMs, at once with reduced motion).
    bool startButtonShown() const;
    /// Get started was pressed (the fade-out runs or ran).
    bool isFinishing() const { return m_finishing; }

    QPushButton* startButton() const { return m_start; }
    QAbstractButton* skipButton() const { return m_skip; }

    /// Dev aid (ZERO_SCREENSHOT_INTRO): shows the scene frozen at `ms` (no timer, no idle motion).
    void freezeAt(qint64 ms);

   public slots:
    /// Esc and the Skip button: before the button appeared it jumps to the final frame; after that it is the same as Get started.
    void skip();
    /// Get started: records ZeroIntroSeen, fades out (instant with reduced motion), then emits finished() and deletes the overlay.
    void getStarted();

   signals:
    /// Emitted once, after the fade-out, when the overlay is already hidden. The next startup step (the Add account dialog, later the
    /// guided tour) runs from here.
    void finished();

   protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void showEvent(QShowEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    bool focusNextPrevChild(bool next) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    struct Layout {
        int cell = 6;        // mark cell in px (integer so the blocks stay crisp)
        double k = 1.0;      // cell / 6: the scale of everything the installer draws at its design size
        int markTop = 0, wordTop = 0, captionTop = 0, buttonTop = 0;
        int groundTop = 0, groundH = 0, grassH = 0, block = 4;
        int starSize = 3;
    };

    void relayout();
    void tick();
    void syncChildren();
    void jumpToEnd();
    void placeChildren();
    void refreshIcon();
    void buildStars();
    QPixmap groundPixmap();

    QPointer<QWidget> m_host;
    SettingsObject* m_settings = nullptr;
    QPushButton* m_start = nullptr;
    QAbstractButton* m_skip = nullptr;
    QGraphicsOpacityEffect* m_startFx = nullptr;
    QGraphicsOpacityEffect* m_skipFx = nullptr;
    QPointer<QGraphicsOpacityEffect> m_rootFx;  // reduced motion: the one fade-in
    QPixmap m_snapshot;                         // the last frame, painted with falling opacity while Get started fades out
    double m_fadeOut = 1.0;
    QTimer* m_timer = nullptr;
    QElapsedTimer m_clock;
    qint64 m_offset = 0;   // added to the clock by a jump
    qint64 m_frozen = -1;  // >= 0: frozen at this time
    bool m_finishing = false;
    Layout m_l;
    QVector<Scene::Star> m_stars;
    QPixmap m_ground;
    qreal m_groundDpr = 0;
    QSize m_groundSize;
};

}  // namespace Onboarding
}  // namespace Zero
