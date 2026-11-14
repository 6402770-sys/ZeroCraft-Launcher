// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the Resource packs page of the instance window (docs/zero/CONTENT-PACKS.md, "D2 Installed resource packs"). Replaces
// Prism's ResourcePackPage in InstancePageProvider::getPages() (same id "resourcepacks"). The source is the instance's `resourcePackList()`.
//
// What a pack's switch means: ON = the pack is in the `resourcePacks` line of the game's options.txt (Packs::writeOrder / undoLast, which
// edit that one line and keep everything else byte for byte, atomically, with a history of the previous lines for Undo). ON packs are
// listed top first (the top one wins) above the fixed "Default resources" row; the rest is AVAILABLE; `.zip.disabled` files (hidden from
// the game) come last with "Unhide". Prism's own enable / disable (a file rename) is used for "Unhide" only, through the base
// `ResourceFolderModel::setResourceEnabled` (never the game-running modal box).
//   * Nothing changes while the game runs (banner "Close the game to change packs"); the state is read again after every dialog.
//   * options.txt is read again whenever the page is shown, the folder reloads and the instance stops.
//   * Minecraft 1.12 and older (and an options.txt whose line this code does not understand) list the packs without switches or order.
//   * Remove moves the file to the Recycle Bin with `FS::trash` (never `deleteResources`), drops the packwiz metadata of the files that went,
//     and takes an active pack out of `resourcePacks` (same write, same history).
//   * Every change of the order writes at once and shows a toast with Undo.
#pragma once

#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <functional>

#include "minecraft/mod/ResourcePackFolderModel.h"
#include "ui/pages/BasePage.h"
#include "ui/zero/ZeroToast.h"
#include "ui/zero/instance/ModsDialogs.h"
#include "ui/zero/instance/PackList.h"
#include "zero/content/InstalledPacks.h"
#include "zero/content/ModTarget.h"
#include "zero/content/PackOrder.h"

class MinecraftInstance;
class QAction;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QStackedWidget;
class QToolButton;

namespace Zero {

class ZeroEmptyState;
class ZeroSkeleton;
namespace Content {
class SegmentedControl;
}
namespace ModsUi {
class RunningBanner;
}

namespace PacksUi {

/// What the page needs from the application besides the model (tests inject fakes; the application page fills them from the instance).
struct PacksPageEnvironment {
    std::function<Content::TargetInstance()> target;   ///< name and Minecraft version of the instance
    std::function<bool()> isRunning;                   ///< the game runs: no changes
    std::function<QString()> optionsPath;              ///< `<gameRoot>/options.txt`
    std::function<QString()> historyPath;              ///< `<instance root>/.zero/pack-order.json`
    std::function<bool(const QString&)> trash;         ///< moves a file to the Recycle Bin; default FS::trash. Never deletes for good.
    ModsUi::AskFunction ask;                           ///< default: a ChoiceDialog
    std::function<void()> addPacks;                    ///< "Add packs": Discover > Resource Packs for this instance; empty hides the button
    std::function<void(QWidget* parent)> openClassic;  ///< the classic page in a dialog; empty hides the menu entry
    std::function<bool()> shouldDisplay;               ///< the instance uses resource packs (not texture packs); default: yes
};

class ResourcePacksPage : public QWidget, public BasePage {
    Q_OBJECT
   public:
    /// The application page for `inst`.
    ResourcePacksPage(MinecraftInstance* inst, ResourcePackFolderModel* model, QWidget* parent = nullptr);
    /// With an explicit environment (tests).
    ResourcePacksPage(const PacksPageEnvironment& env, ResourcePackFolderModel* model, QWidget* parent = nullptr);
    ~ResourcePacksPage() override;

    QString id() const override { return QStringLiteral("resourcepacks"); }
    QString displayName() const override { return tr("Resource Packs"); }
    QIcon icon() const override { return QIcon::fromTheme("resourcepacks"); }
    QString helpPage() const override { return QStringLiteral("Resource-packs"); }
    bool shouldDisplay() const override { return !m_env.shouldDisplay || m_env.shouldDisplay(); }
    void openedImpl() override;
    void closedImpl() override;

    /// What the page shows below the toolbar.
    enum class View { Loading, List, Empty, NoMatches };
    View view() const { return m_view; }
    bool isLoaded() const { return m_loaded; }
    bool isLocked() const { return m_locked; }

    /// Whether the packs can be turned on and ordered: Ready; OldVersion = Minecraft 1.12 and older; Unreadable = options.txt has a
    /// `resourcePacks` line this code does not understand.
    enum class OrderState { Ready, OldVersion, Unreadable };
    OrderState orderState() const { return m_orderState; }
    bool isOrdered() const { return m_orderState == OrderState::Ready; }

    Content::Packs::Filter filter() const { return m_filter; }
    void setFilter(Content::Packs::Filter filter);
    QString searchText() const;
    void setSearchText(const QString& text);

    /// The game started or stopped (also re-read from the environment). When it stopped, options.txt is read again.
    void refreshRunning();
    /// Reads the model and options.txt into the list now (normally coalesced to the next event-loop turn).
    void rebuildNow();

    /// The active packs the folder has, top first.
    QStringList activePacks() const;

    // The actions behind the switch, the keys, the drag and the row menu. `row` is a row of the list.
    void requestToggle(int row);
    void requestRemove(int row);
    void requestUnhide(int row);
    enum class Move { Top, Up, Down, Bottom };
    void requestMove(int row, Move move);
    /// Moves the active pack at `fromOn` (0 = top) so that it ends at `toOn`: what a drop does. False when nothing was written.
    bool movePack(int fromOn, int toOn);
    /// Turns the pack on (at the top) or off. False when nothing was written.
    bool setPackActive(const QString& fileName, bool on);
    /// Puts back what options.txt said before the newest change (the toast's Undo, "Restore previous order"). `expectedId`: only when that
    /// change is still the newest. False when nothing was restored.
    bool undo(const QString& expectedId = QString());
    int selectedRow() const;
    void selectRow(int row);

    // Parts, for the tests and the dev aids.
    PackListView* listView() const { return m_list; }
    PackListModel* listModel() const { return m_listModel; }
    ZeroEmptyState* emptyState() const { return m_empty; }
    ZeroSkeleton* skeleton() const { return m_skeleton; }
    QLineEdit* searchField() const { return m_search; }
    Content::SegmentedControl* filterControl() const { return m_filterControl; }
    QLabel* captionLabel() const { return m_caption; }
    QPushButton* addButton() const { return m_addButton; }
    QToolButton* overflowButton() const { return m_overflow; }
    QAction* restoreAction() const { return m_restoreAction; }
    /// The note under the toolbar (old Minecraft, unreadable options.txt); empty = hidden.
    QString noteText() const;
    QWidget* banner() const;
    /// Fills `menu` with the `...` entries of `row` (selecting it); onRowMenu() shows it.
    void fillRowMenu(QMenu* menu, int row);

   signals:
    void addPacksRequested();

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

   private:
    void build();
    void connectModel();
    void scheduleRebuild();
    void rebuild();
    void applyView();
    void updateChrome();
    void setView(View view);
    void reloadOptions();
    /// A row of the list and the Prism pack behind it, checked: `pack` is null when the Prism model moved on since the row was read.
    struct Picked {
        int listRow = -1;
        int sourceRow = -1;
        ResourcePack* pack = nullptr;
    };
    Picked pick(int row) const;
    bool stillThere(const Picked& picked) const;
    void onSwitchClicked(int row);
    void onToggleKey();
    void onMoveKey(int delta);
    void onRowMenu(int row, const QPoint& globalPos);
    void onEmptyAction();
    void openPacksFolder();
    void toast(const QString& text, ZeroToast::Kind kind, const QString& undoId = QString());
    bool blocked() const { return m_locked; }
    /// Writes `activeTopFirst` as the order of the active packs (Packs::writeOrder). `existing`: the names options.txt entries may refer
    /// to (defaults to the visible packs). Shows `text` with Undo on success, the reason on a failure.
    bool apply(const QStringList& activeTopFirst, const QString& text, const QSet<QString>* existing = nullptr);
    int onIndexOfRow(int row) const;

    PacksPageEnvironment m_env;
    QPointer<ResourcePackFolderModel> m_model;

    // chrome
    ModsUi::RunningBanner* m_banner = nullptr;
    QWidget* m_toolbar = nullptr;
    QLineEdit* m_search = nullptr;
    Content::SegmentedControl* m_filterControl = nullptr;
    QWidget* m_statusRow = nullptr;
    QLabel* m_caption = nullptr;
    QWidget* m_noteRow = nullptr;
    QLabel* m_noteIcon = nullptr;
    QLabel* m_note = nullptr;
    QPushButton* m_addButton = nullptr;
    QToolButton* m_overflow = nullptr;
    QAction* m_restoreAction = nullptr;
    QStackedWidget* m_stack = nullptr;
    ZeroSkeleton* m_skeleton = nullptr;
    PackListView* m_list = nullptr;
    PackListModel* m_listModel = nullptr;
    ZeroEmptyState* m_empty = nullptr;

    // state
    QList<Content::Packs::PackRow> m_rows;  ///< every pack, in the Prism model's order
    QList<quintptr> m_tokens;               ///< the Prism packs of m_rows (compared, never dereferenced)
    QList<int> m_sourceRows;
    Content::Packs::PackList m_options;
    OrderState m_orderState = OrderState::Ready;
    Content::Packs::Filter m_filter = Content::Packs::Filter::All;
    QString m_query;
    View m_view = View::Loading;
    QTimer m_rebuildTimer;
    bool m_loaded = false;
    bool m_dirty = true;
    bool m_locked = false;
    bool m_startedWatching = false;
    bool m_building = false;
};

}  // namespace PacksUi
}  // namespace Zero
