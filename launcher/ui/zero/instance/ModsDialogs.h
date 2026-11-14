// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the one small question dialog of the Mods page: a title, a few lines of text, an optional list of names and a row
// of buttons (the frame of the install review dialogs: body above, footer bar with the buttons below). Used for "other mods are
// affected" (Disable only X / Disable all 2 / Cancel) and for Remove.
#pragma once

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

class QPushButton;

namespace Zero {
namespace ModsUi {

struct ChoiceButton {
    enum class Kind { Primary, Danger, Secondary, Ghost };
    QString label;
    Kind kind = Kind::Secondary;
};

struct ChoicePrompt {
    QString title;
    QString body;         ///< plain text, may be empty
    QStringList list;     ///< one name per line, may be empty
    QString more;         ///< a line under the list ("and 4 more"), may be empty
    QString note;         ///< caption under everything ("They go to the Recycle Bin."), may be empty
    QList<ChoiceButton> buttons;  ///< left to right; Esc and closing answer -1
    int defaultButton = -1;       ///< the button Enter presses; -1 = the last one
};

class ChoiceDialog : public QDialog {
    Q_OBJECT
   public:
    explicit ChoiceDialog(const ChoicePrompt& prompt, QWidget* parent = nullptr);

    /// Shows the dialog modally; the index of the pressed button, -1 when it was dismissed.
    int choose();
    QPushButton* buttonAt(int index) const { return m_buttons.value(index); }
    int chosen() const { return m_chosen; }

   private:
    QList<QPushButton*> m_buttons;
    int m_chosen = -1;
};

/// The way the page asks: a function so the tests answer without a modal dialog. Returns the pressed button, -1 = dismissed.
using AskFunction = std::function<int(QWidget* parent, const ChoicePrompt& prompt)>;
/// Shows a ChoiceDialog.
int askWithDialog(QWidget* parent, const ChoicePrompt& prompt);

}  // namespace ModsUi
}  // namespace Zero
