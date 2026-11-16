// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the warning strip at the top of the Resource packs page after the game turned every pack off
// (zero/content/PackRollback): a title, one or two lines, "Restore without <pack>" / "Restore all" and a dismiss button. Same family as
// ModSnapshotNotice (a tinted bar with an icon) in the `warning` tone. It only shows and reports clicks; the decision and the work are the
// page's.
#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QToolButton;

namespace Zero {
namespace PacksUi {

class PackRollbackNotice : public QWidget {
    Q_OBJECT
   public:
    explicit PackRollbackNotice(QWidget* parent = nullptr);

    void setTitleAndBody(const QString& title, const QString& body);
    /// `withoutText` non-empty: "Restore without X" is the primary button and "Restore all" the secondary one; empty: only "Restore all"
    /// (the primary one).
    void setActions(const QString& withoutText, const QString& allText);
    /// The restore buttons are clickable; when not, `reason` is their tooltip ("Close the game first").
    void setRestoreEnabled(bool enabled, const QString& reason = {});

    QLabel* titleLabel() const { return m_title; }
    QLabel* bodyLabel() const { return m_body; }
    QPushButton* restoreWithoutButton() const { return m_restoreWithout; }
    QPushButton* restoreAllButton() const { return m_restoreAll; }
    QToolButton* dismissButton() const { return m_dismiss; }

   signals:
    void restoreWithoutRequested();
    void restoreAllRequested();
    void dismissRequested();

   protected:
    void paintEvent(QPaintEvent*) override;

   private:
    QLabel* m_title;
    QLabel* m_body;
    QPushButton* m_restoreWithout;
    QPushButton* m_restoreAll;
    QToolButton* m_dismiss;
};

}  // namespace PacksUi
}  // namespace Zero
