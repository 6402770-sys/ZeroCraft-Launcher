// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - centered empty state.
#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

namespace Zero {

/// Centered: 40 px icon (textTertiary), heading, body (textSecondary, max 360 px wide) and an optional primary action.
class ZeroEmptyState : public QWidget {
    Q_OBJECT
   public:
    /// `iconName` is a Lucide icon name. An empty `actionText` hides the button.
    explicit ZeroEmptyState(const QString& iconName, const QString& title, const QString& body, const QString& actionText = {},
                            QWidget* parent = nullptr);

    void setIcon(const QString& iconName);
    void setTitle(const QString& title);
    void setBody(const QString& body);
    void setActionText(const QString& text);

   signals:
    void actionTriggered();

   private:
    void refreshIcon();

    QString m_icon;
    QLabel* m_iconLabel;
    QLabel* m_title;
    QLabel* m_body;
    QPushButton* m_action;
};

}  // namespace Zero
