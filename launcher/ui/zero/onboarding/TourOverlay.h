// SPDX-License-Identifier: GPL-3.0-only
/*
 *  ZERO Launcher - guided tour: a full-window overlay that dims the shell, cuts a spotlight around one control at a time and shows a
 *  callout card (step n of N, title, text, Skip tour / Back / Next). It plays after the first-run intro and can be replayed from the
 *  command palette ("Take the tour"). docs/zero/SHELL.md, "Guided tour".
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

#include <QList>
#include <QPixmap>
#include <QPointer>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QVariantAnimation>
#include <QWidget>
#include <functional>

#include "ui/zero/onboarding/TourSteps.h"

class QGraphicsOpacityEffect;
class QLabel;
class QPushButton;
class QTimer;
class SettingsObject;

namespace Zero {
namespace Onboarding {

class TourCard;

/// One step as the overlay runs it: the text and side from TourStepInfo plus how to find its target.
struct TourStep {
    QString id;
    QString title;
    QString body;
    TourSide side = TourSide::Right;
    /// The widget to highlight (resolved every time it is needed: it may move, or be hidden by a page change). Null = no target.
    std::function<QWidget*()> target;
    /// Optional: is the step usable at all (e.g. its page exists)? Default: `target()` is non-null and visible. Asked once, when the tour starts.
    std::function<bool()> available;
    /// Optional: runs when the step becomes current / stops being current (e.g. show another page for this step only, then back).
    std::function<void()> enter;
    std::function<void()> leave;
};

/// The tour. Child of the main window: covers it completely (follows its size), takes the keys and blocks input to the shell except its own
/// card. Steps without a usable target are dropped when it starts. Finish and Skip write ZeroTourDone = true, fade the overlay out and
/// delete it; finished(completed) is emitted once, after the fade.
class TourOverlay : public QWidget {
    Q_OBJECT
   public:
    static constexpr int kGlideMs = 220;     ///< the spotlight glides to the next target (OutCubic)
    static constexpr int kCardMs = 220;      ///< the card fades in and slides kSlidePx while the old one fades out
    static constexpr int kSlidePx = 8;
    static constexpr int kFadeOutMs = 200;   ///< Finish / Skip: the overlay fades out
    static constexpr int kPollMs = 100;      ///< safety net: the target geometry is re-read even when no move event came
    static constexpr int kDimAlpha = 140;    ///< theme bg at 55 %

    TourOverlay(QWidget* host, const QList<TourStep>& steps, SettingsObject* settings = nullptr);
    ~TourOverlay() override;

    /// Steps that survived (target present and visible, or `available`).
    int stepCount() const { return int(m_steps.size()); }
    /// Index of the current step in that list (0-based); -1 before the first is shown.
    int currentStep() const { return m_index; }
    QString currentStepId() const;
    QStringList stepIds() const;

    /// The cut-out as painted now (it glides between steps) and where it ends up.
    QRect spotlight() const { return m_spot.toAlignedRect(); }
    QRect spotlightTarget() const { return m_spotTo; }
    /// The visible card (without its shadow margin), window coordinates.
    QRect cardRect() const;
    bool isFinishing() const { return m_finishing; }

    QPushButton* nextButton() const { return m_next; }
    QPushButton* backButton() const { return m_back; }
    QPushButton* skipButton() const { return m_skipButton; }
    QString overlineText() const;
    QString titleText() const;
    QString bodyText() const;
    /// Number of text lines the body needs at the card's width.
    int bodyLines() const;

    /// Shows step `index` (0-based, clamped). `animate` false: no glide / fade (dev aid, tests). Steps whose target vanished are skipped.
    void showStep(int index, bool animate = true);

   public slots:
    void next();    ///< the next step; on the last one it is Finish
    void back();
    void skip();    ///< Skip tour (also Esc): ends the tour, nothing more
    void finish();  ///< ends the tour as completed

   signals:
    /// Emitted once, after the fade-out, when the overlay is already hidden. `completed`: the user went through to Finish.
    void finished(bool completed);

   protected:
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    bool focusNextPrevChild(bool next) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void end(bool completed);
    QWidget* liveTarget(int index) const;          ///< the widget of step `index` when it can be highlighted right now (visible, inside the host)
    QRect mapped(const QWidget* target) const;     ///< its rectangle in host coordinates
    void refreshTarget(bool force = false);
    void placeCardNow();
    void applyCardOffset();
    void setCardProgress(double p);
    bool inOurWindow(QObject* o) const;

    QPointer<QWidget> m_host;
    SettingsObject* m_settings = nullptr;
    QList<TourStep> m_steps;
    int m_index = -1;
    bool m_finishing = false;
    bool m_shown = false;  // a step was on screen (only then does ending the tour write the setting)

    TourCard* m_card = nullptr;
    QPushButton* m_next = nullptr;
    QPushButton* m_back = nullptr;
    QPushButton* m_skipButton = nullptr;
    QPointer<QWidget> m_target;
    TourSide m_side = TourSide::Right;

    QRectF m_spot;       // painted cut-out
    QRectF m_spotFrom;
    QRect m_spotTo;      // where it ends (window coordinates, padded)
    QVariantAnimation m_glide;

    CardPlacement m_placement;
    QVariantAnimation m_cardAnim;
    double m_cardProgress = 1.0;
    QPixmap m_oldCard;   // the previous step's card, fading out while the new one fades in
    QPoint m_oldCardPos;
    QPointer<QGraphicsOpacityEffect> m_cardFx;

    QTimer* m_poll = nullptr;
};

}  // namespace Onboarding
}  // namespace Zero
