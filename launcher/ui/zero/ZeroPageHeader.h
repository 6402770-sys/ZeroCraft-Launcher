// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - page header (title, optional subtitle, right-aligned actions).
#pragma once

#include <QWidget>

class QHBoxLayout;
class QLabel;

namespace Zero {

/// Title (title role), optional caption subtitle, and right-aligned action widgets. Bottom margin is `xl` (24 px).
class ZeroPageHeader : public QWidget {
    Q_OBJECT
   public:
    explicit ZeroPageHeader(const QString& title = {}, const QString& subtitle = {}, QWidget* parent = nullptr);

    void setTitle(const QString& title);
    QString title() const;
    /// An empty subtitle hides the line.
    void setSubtitle(const QString& subtitle);
    QString subtitle() const;
    /// Appends a widget (usually a button with zeroVariant) to the right-hand action area. Takes ownership.
    void addAction(QWidget* action);

   private:
    QLabel* m_title;
    QLabel* m_subtitle;
    QHBoxLayout* m_actions;
};

}  // namespace Zero
