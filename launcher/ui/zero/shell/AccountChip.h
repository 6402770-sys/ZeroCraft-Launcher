// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the account chip at the right end of the top bar: avatar, profile name, chevron.
#pragma once

#include <QAbstractButton>
#include <QPixmap>

namespace Zero {

namespace Motion {
class StateAnimator;
}

/// Compact chip: 28 px avatar, profile name (label role, elided) and a 12 px chevron-down, on a `surfaceHover` pill while hovered.
class AccountChip : public QAbstractButton {
    Q_OBJECT
   public:
    explicit AccountChip(QWidget* parent = nullptr);

    /// `face` may be null: the generic `user` icon is drawn on an elevated circle instead. `caption` ("Microsoft account",
    /// "Offline", "No account") only appears in the tooltip.
    void setAccount(const QString& name, const QString& caption, const QPixmap& face);
    QString name() const { return m_name; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

   protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

   private:
    int textWidth() const;

    QString m_name;
    QString m_caption;
    QPixmap m_face;
    Motion::StateAnimator* m_hover;
    bool m_keyboardFocus = false;  // the focus ring only shows for Tab navigation
};

}  // namespace Zero
