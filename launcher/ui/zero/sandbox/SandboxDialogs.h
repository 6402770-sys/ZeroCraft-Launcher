// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - "Try it in a copy" (docs/zero/ARCHITECTURE.md, "Test copies"): the two questions. Both are the same small dialog (the frame of
// the Mods ChoiceDialog: title, text, then checkboxes with an optional caption, a note, the footer bar with the buttons): "Try it in a copy"
// (Copy worlds too) and "Bring changes back to <X>?" (one checkbox per part that differs, Worlds off, Discard afterwards on).
#pragma once

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>

#include "ui/zero/instance/ModsDialogs.h"
#include "zero/sandbox/SandboxPlan.h"

class QCheckBox;
class QPushButton;

namespace Zero {
namespace SandboxUi {

struct CheckOption {
    QString id;
    QString label;
    QString caption;  ///< a line under the checkbox, may be empty
    bool checked = true;
    bool gapBefore = false;  ///< a wider space above (separates the last group)
};

struct CheckPrompt {
    QString title;
    QString body;  ///< plain text, may be empty
    QList<CheckOption> options;
    QString note;  ///< caption under the checkboxes, may be empty
    QList<ModsUi::ChoiceButton> buttons;  ///< left to right; Esc and closing answer -1
    int defaultButton = -1;               ///< the button Enter presses; -1 = the last one
};

struct CheckAnswer {
    int button = -1;      ///< the pressed button, -1 = dismissed
    QStringList checked;  ///< ids of the options that were checked
    bool isChecked(const QString& id) const { return checked.contains(id); }
};

class CheckDialog : public QDialog {
    Q_OBJECT
   public:
    explicit CheckDialog(const CheckPrompt& prompt, QWidget* parent = nullptr);

    /// Shows the dialog modally.
    CheckAnswer choose();
    CheckAnswer answer() const;
    QPushButton* buttonAt(int index) const { return m_buttons.value(index); }
    QCheckBox* checkBox(const QString& id) const;
    QStringList optionIds() const;

   private:
    QList<QPushButton*> m_buttons;
    QList<QPair<QString, QCheckBox*>> m_checks;
    int m_chosen = -1;
};

/// The way MainWindow asks: a function so tests answer without a modal dialog.
using AskChecks = std::function<CheckAnswer(QWidget* parent, const CheckPrompt& prompt)>;
CheckAnswer askChecksWithDialog(QWidget* parent, const CheckPrompt& prompt);

// ---- the prompts (pure: texts only) ----

inline constexpr char kOptionWorlds[] = "worlds";
inline constexpr char kOptionDiscard[] = "discard";

/// "Try it in a copy": buttons 0 = Cancel, 1 = Make test copy; option "worlds" (default on).
CheckPrompt startPrompt(const QString& originalName);
/// "Bring changes back to <X>?" for the parts of `plan` that differ (option ids = partKey()s, then "discard"): buttons 0 = Cancel, 1 = Bring changes
/// back. Worlds is off by default. When nothing differs the prompt says so: no options, buttons 0 = Close, 1 = Discard copy.
CheckPrompt bringBackPrompt(const QString& originalName, const QList<Sandbox::PartChange>& plan);
/// The parts (in `allParts()` order) whose option is checked in `answer`.
QList<Sandbox::Part> chosenParts(const CheckAnswer& answer);

}  // namespace SandboxUi
}  // namespace Zero
