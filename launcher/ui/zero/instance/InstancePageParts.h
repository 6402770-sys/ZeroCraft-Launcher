// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the pieces the Mods page and the Resource packs page of the instance window share (docs/zero/SHELL.md, "Instance window"):
// the banner shown while the game runs, the switch painter of the rows, the toolbar controls (search field, primary Add button, the `...`
// overflow button) and the one step of the Remove flow that touches the disk (a file goes to the Recycle Bin through an injectable
// function, never deleted for good). The question dialog (ModsDialogs.h), the empty states (ZeroEmptyState), the toasts and the pill
// layout of the rows (ModDelegate::fitPills) are shared the same way. Nothing here knows about mods or packs.
#pragma once

#include <QRect>
#include <QString>
#include <QWidget>

#include <functional>

class QLineEdit;
class QPainter;
class QPushButton;
class QToolButton;

namespace Zero {
namespace ModsUi {

/// The warning bar of a page whose files cannot change right now ("Close the game to change mods"). 36 px high, `warning` tint.
class RunningBanner : public QWidget {
   public:
    RunningBanner(const QString& text, QWidget* parent);

   protected:
    void paintEvent(QPaintEvent*) override;

   private:
    QString m_text;
};

/// The 36 x 20 switch of a row, painted into `rect`; dimmed while `locked`.
void paintSwitch(QPainter* painter, const QRect& rect, bool on, bool locked);

/// The search box of a toolbar (clearable, 112 - 300 px wide, lg height). The caller connects it and adds the leading icon.
QLineEdit* makeSearchField(QWidget* parent, const QString& placeholder);
/// The primary "Add ..." button of a toolbar (the caller sets its icon and connects it).
QPushButton* makeAddButton(const QString& text, QWidget* parent);
/// The ghost `...` button that opens a menu at once, without the arrow (the caller sets the icon and the menu).
QToolButton* makeOverflowButton(QWidget* parent, const QString& objectName);

/// Remove's one disk step: the file at `path` goes to the Recycle Bin with `trash`; a file that is already gone counts as removed.
/// Never deletes for good (the callers must not fall back to that).
bool moveToRecycleBin(const QString& path, const std::function<bool(const QString&)>& trash);

}  // namespace ModsUi
}  // namespace Zero
