// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the Library details panel (selected instance: icon, name, stats, Play/Stop, action list).
#pragma once

#include <QPointer>
#include <QString>
#include <QVector>
#include <QWidget>
#include "minecraft/MinecraftInstance.h"
#include "ui/zero/ZeroCard.h"


class QAction;
class QLabel;
class QLayout;
class QLineEdit;
class QPushButton;
class QStackedLayout;

namespace Zero {

class ElidedLabel;
class IconTile;
class ZeroPlayButton;

/// The MainWindow actions the panel mirrors. Buttons trigger the action and follow its enabled state.
struct LibraryActions {
    QAction* launch = nullptr;
    QAction* kill = nullptr;
    QAction* edit = nullptr;
    QAction* folder = nullptr;
    QAction* group = nullptr;
    QAction* copy = nullptr;
    QAction* exportInstance = nullptr;  ///< carries the Zip / Modrinth / CurseForge menu
    QAction* shortcut = nullptr;
    QAction* remove = nullptr;
};

class InstanceDetailsPanel : public ZeroCard {
    Q_OBJECT
   public:
    static constexpr int kWidth = 300;

    explicit InstanceDetailsPanel(QWidget* parent = nullptr);

    void setActions(const LibraryActions& actions);
    /// Shows the instance, or the "Select an instance" state for nullptr. Cheap: call on every change.
    void setInstance(MinecraftInstance* instance);

    /// Replaces the title with an editable field. Emits renameRequested() on Enter / focus loss.
    void beginRename();

   signals:
    void renameRequested(const QString& newName);
    void changeIconRequested();

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void applyInstance(MinecraftInstance* instance);  // setInstance() without the crossfade
    struct Binding {
        QPointer<QAction> action;
        QPushButton* button;
    };

    QPushButton* addActionButton(QWidget* parent, const QString& icon, const QString& text, QAction* action, bool danger = false);
    void refreshIcons();
    void syncButtons();
    void commitRename();
    void cancelRename();
    void refreshPlayButton(bool running, bool canLaunch);

    QStackedLayout* m_stack;
    IconTile* m_icon;
    ElidedLabel* m_name;
    QLineEdit* m_nameEdit;
    QLabel* m_meta;
    QLabel* m_running;
    QLabel* m_lastPlayed;
    QLabel* m_playtime;
    ElidedLabel* m_group;
    ZeroPlayButton* m_play;
    QVector<Binding> m_bindings;
    LibraryActions m_actions;
    QPointer<MinecraftInstance> m_instance;
    QLayout* m_actionHost = nullptr;
    QString m_nameText;
    bool m_renaming = false;
    // icon name + color token per button, to re-tint on theme changes
    struct IconSpec {
        QPushButton* button;
        QString icon;
        bool danger;
    };
    QVector<IconSpec> m_iconSpecs;
};

}  // namespace Zero
