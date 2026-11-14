// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the slim strip at the top of the mod list while the newest update snapshot of the instance can still be rolled back
// ("Updated 3 mods · 2 h ago", Roll back, dismiss). Same family as ShaderModNotice (a tinted bar with an icon, `info` tone). It only
// shows and reports clicks; the decision and the work are the page's (zero/mods/ModSnapshot).
#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QToolButton;

namespace Zero {
namespace ModsUi {

class ModSnapshotNotice : public QWidget {
    Q_OBJECT
   public:
    explicit ModSnapshotNotice(QWidget* parent = nullptr);

    /// `text` is the line; `tooltip` the full date.
    void setContent(const QString& text, const QString& tooltip);
    /// Roll back is clickable; when not, `reason` is its tooltip ("Close the game first").
    void setRollbackEnabled(bool enabled, const QString& reason = {});
    QLabel* label() const { return m_label; }
    QPushButton* rollbackButton() const { return m_rollback; }
    QToolButton* dismissButton() const { return m_dismiss; }

   signals:
    void rollbackRequested();
    void dismissRequested();

   protected:
    void paintEvent(QPaintEvent*) override;

   private:
    QLabel* m_label;
    QPushButton* m_rollback;
    QToolButton* m_dismiss;
};

}  // namespace ModsUi
}  // namespace Zero
