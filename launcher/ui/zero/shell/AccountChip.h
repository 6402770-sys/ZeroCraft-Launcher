// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the account chip at the bottom of the nav rail: avatar, profile name, account type.
#pragma once

#include <QAbstractButton>
#include <QPixmap>

namespace Zero {

namespace Motion {
class StateAnimator;
}

class AccountChip : public QAbstractButton {
    Q_OBJECT
   public:
    explicit AccountChip(QWidget* parent = nullptr);

    /// `face` may be null: the generic `user` icon is drawn on an elevated circle instead.
    void setAccount(const QString& name, const QString& caption, const QPixmap& face);

    QSize sizeHint() const override;

   protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

   private:
    QString m_name;
    QString m_caption;
    QPixmap m_face;
    Motion::StateAnimator* m_hover;
};

}  // namespace Zero
