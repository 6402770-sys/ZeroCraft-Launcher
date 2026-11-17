// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the "Add to an instance" dialog of smart drag and drop (docs/zero/SHELL.md, "Drag and drop"): one row per dropped file, the
// target instance, and what the instance thinks of every file. The dialog installs through an injected step, so tests need no instance.
#pragma once

#include <QDialog>
#include <QList>
#include <QPair>
#include <functional>

#include "ui/zero/content/TargetInstanceButton.h"
#include "zero/content/DropPlan.h"

class QCheckBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace Zero {
class ZeroEmptyState;
class PillTag;
}  // namespace Zero

namespace Zero::Content {

struct DropAddEnvironment {
    QList<TargetInstance> targets;
    QString librarySelectedId;  ///< the Library's selected instance (the default target when it fits)
    QString savedTargetId;      ///< the last drop target ("ZeroDropTarget")
    /// Folder of `kind` inside the target instance (to spot a file that would be replaced); may be empty / unset.
    std::function<QString(const QString& targetId, Drop::Kind kind)> folderFor;
    /// Installs one file into the target; returns an error text, empty when it worked. Required.
    std::function<QString(const QString& targetId, const Drop::DropFile& file)> install;
    /// Opens the create-instance flow (the empty state's action).
    std::function<void()> createInstance;
    TargetInstanceButton::IconProvider icons;
};

class DropAddDialog : public QDialog {
    Q_OBJECT
   public:
    DropAddDialog(DropAddEnvironment env, QList<Drop::DropFile> files, QWidget* parent = nullptr);

    /// The instance the files go to (empty: none).
    QString targetId() const { return m_targetId; }
    /// The ticked files.
    QList<Drop::DropFile> chosenFiles() const;
    /// After Add: what went in, and what failed with the error of the install step.
    QList<Drop::DropFile> addedFiles() const { return m_added; }
    QList<QPair<Drop::DropFile, QString>> failures() const { return m_failures; }
    bool createInstanceRequested() const { return m_createRequested; }

    // ---- for tests
    bool isEmptyState() const { return m_empty != nullptr; }
    int rowCount() const { return m_rows.size(); }
    QCheckBox* rowBox(int row) const;
    QString rowKind(int row) const;
    QString rowName(int row) const;
    QString rowFit(int row) const;      ///< the warning caption (empty = hidden)
    QString rowReplace(int row) const;  ///< "Replaces the file with the same name"
    QString rowNote(int row) const;     ///< the data pack caption
    QString summaryText() const;
    QString titleText() const;
    QPushButton* primaryButton() const { return m_primary; }
    TargetInstanceButton* targetButton() const { return m_button; }
    void setTarget(const QString& id);

   protected:
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent* event) override;

   private:
    struct RowUi {
        Drop::DropFile file;
        QWidget* widget = nullptr;
        QCheckBox* check = nullptr;
        PillTag* pill = nullptr;
        QLabel* name = nullptr;
        QLabel* fit = nullptr;
        QLabel* replace = nullptr;
        QLabel* note = nullptr;
    };
    const TargetInstance* currentTarget() const;
    void buildUi(const QList<Drop::DropFile>& files);
    void refresh();
    void fitBody();
    void onPrimary();

    DropAddEnvironment m_env;
    QList<Drop::DropFile> m_files;
    QString m_targetId;
    QList<RowUi> m_rows;
    QList<Drop::DropFile> m_added;
    QList<QPair<Drop::DropFile, QString>> m_failures;
    bool m_createRequested = false;
    bool m_anyMod = false;

    QLabel* m_title = nullptr;
    TargetInstanceButton* m_button = nullptr;
    QWidget* m_targetRow = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_content = nullptr;
    ZeroEmptyState* m_empty = nullptr;
    QLabel* m_summary = nullptr;
    QPushButton* m_cancel = nullptr;
    QPushButton* m_primary = nullptr;
};

}  // namespace Zero::Content
