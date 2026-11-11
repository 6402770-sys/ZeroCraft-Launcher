// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the Library details panel (selected instance: artwork / icon, name, favorite, stats, Play/Stop, tags,
// description, action list).
#pragma once

#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include "minecraft/MinecraftInstance.h"
#include "ui/zero/ZeroCard.h"


class QAction;
class QCompleter;
class QLabel;
class QLayout;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedLayout;
class QStringListModel;
class QTimer;
class QToolButton;

namespace Zero {

class ArtworkHeader;
class ElidedLabel;
class FlowLayout;
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
    static constexpr int kDescriptionIdleMs = 600;

    explicit InstanceDetailsPanel(QWidget* parent = nullptr);
    ~InstanceDetailsPanel() override;

    void setActions(const LibraryActions& actions);
    /// Shows the instance, or the "Select an instance" state for nullptr. Cheap: call on every change.
    void setInstance(MinecraftInstance* instance);

    /// Replaces the title with an editable field. Emits renameRequested() on Enter / focus loss.
    void beginRename();

    /// Saves a pending description edit now (focus out, idle timer, instance change and shutdown call this).
    void flushDescription();

    // for tests / evidence
    QStringList shownTags() const { return m_shownTags; }
    QLineEdit* tagInput() const { return m_tagInput; }
    QPlainTextEdit* descriptionEdit() const { return m_desc; }
    QWidget* artworkHeader() const;
    QPushButton* removeArtworkButton() const { return m_removeArtwork; }
    QToolButton* favoriteButton() const { return m_star; }

   signals:
    void renameRequested(const QString& newName);
    void changeIconRequested();
    void favoriteToggleRequested();
    void setArtworkRequested();
    void removeArtworkRequested();
    /// "Check mods": open Tools > Mod Health for the shown instance (only offered for instances with a mod loader).
    void checkModsRequested();

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void applyInstance(MinecraftInstance* instance);  // setInstance() without the crossfade
    void refreshProfile(bool reloadEditors);
    void rebuildTags();
    void refreshTagCompletions();
    void addTag(const QString& text);
    void removeTag(const QString& tag);
    void adjustDescriptionHeight();
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
    ArtworkHeader* m_artHeader;
    ElidedLabel* m_name;
    QLineEdit* m_nameEdit;
    QToolButton* m_star;
    QLabel* m_meta;
    QLabel* m_running;
    QLabel* m_lastPlayed;
    QLabel* m_playtime;
    ElidedLabel* m_group;
    ZeroPlayButton* m_play;
    // tags
    QWidget* m_tagsHost;
    FlowLayout* m_tagFlow;
    QLineEdit* m_tagInput;
    QCompleter* m_tagCompleter;
    QStringListModel* m_tagModel;
    QVector<QWidget*> m_tagPills;
    QStringList m_shownTags;
    // description
    QPlainTextEdit* m_desc;
    QTimer* m_descTimer;
    QPointer<MinecraftInstance> m_descInstance;  // the instance whose description is in the editor
    bool m_descDirty = false;
    bool m_loadingDesc = false;
    // artwork buttons
    QPushButton* m_setArtwork = nullptr;
    QPushButton* m_removeArtwork = nullptr;
    QPushButton* m_checkMods = nullptr;

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
