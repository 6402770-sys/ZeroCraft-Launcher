// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - indeterminate spinner.
#pragma once

#include <QVariantAnimation>
#include <QWidget>

namespace Zero {

/// Accent arc on a `border` track, one rotation per second. The animation only runs while the widget is visible.
/// With ZeroReduceMotion on, a static arc is drawn.
class ZeroSpinner : public QWidget {
    Q_OBJECT
   public:
    enum Size { Small = 16, Medium = 24 };
    explicit ZeroSpinner(Size size = Small, QWidget* parent = nullptr);

    void setSpinnerSize(Size size);
    QSize sizeHint() const override;

    /// True while the rotation timer is running (false when hidden or when motion is reduced).
    bool isAnimating() const { return m_anim.state() == QAbstractAnimation::Running; }

   protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

   private:
    void sync();

    QVariantAnimation m_anim;
    Size m_size;
    qreal m_angle = 0.0;
};

}  // namespace Zero
