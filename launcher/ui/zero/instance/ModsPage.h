// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the Mods page of the instance window (docs/zero/CONTENT-MODS.md, "Installed mods page"). Replaces Prism's
// ModFolderPage in InstancePageProvider::getPages() (same id "mods"). The source is the instance's `loaderModList()`; every change goes
// through ZERO's own code paths:
//   * enable / disable calls the BASE `ResourceFolderModel::setResourceEnabled` (ModFolderModel's override pops a modal box) after the
//     page asked its own "other mods are affected" question, and never while the game runs (the base would pop a modal box);
//   * Remove moves each file to the Recycle Bin with `FS::trash` (never `deleteResources`, which deletes permanently when trashing
//     fails) and drops the packwiz metadata only of the files that really went.
// The update check (C5b): "Check for updates" runs Prism's Modrinth / CurseForge checkers headless over the mods with provider info
// (never ResourceUpdateDialog, never a modal box). While it runs the rows are locked like while the game runs (no banner). Its results
// live in the page only: "Update -> x" pills, the Updates filter, "Update all N". The check keeps the checked resources alive and is
// aborted before any of them can go away (a reload of the folder, the page closing, the game starting); late answers of an aborted check
// are ignored by a generation token. Applying updates runs the checkers' `ResourceDownloadTask`s (they replace the old file only after
// the new one arrived); this page deletes nothing.
// Safe update (6a): before the downloads start, applyKeys() saves a copy of the files they replace (zero/mods/ModSnapshot; without the copy
// nothing is updated) and afterwards records what changed. A strip above the list and the toast's Undo roll the newest update back: the
// updated files go to the Recycle Bin, the saved ones come back (never overwritten in place).
// The Dependency Inspector (C6): after every reload of the folder (debounced) a read-only scan (zero/mods/DependencyScanner, generation
// token, parse cache) answers what every mod needs; the answer becomes the row pills / Issues / caption (InstalledModsModel) and fills the
// ModDependencyPanel at the right of the list, which opens on a plain click or Enter. Enable in the panel is requestToggle's own flow.
#pragma once

#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <QHash>
#include <QPair>
#include <QSet>

#include <functional>

#include "minecraft/mod/Mod.h"
#include "ui/pages/BasePage.h"
#include "ui/zero/ZeroToast.h"
#include "ui/zero/instance/ModDependencyPanel.h"
#include "ui/zero/instance/ModsDialogs.h"
#include "ui/zero/instance/ModsList.h"
#include "ui/zero/instance/ModsUpdates.h"
#include "zero/content/ModTarget.h"
#include "zero/mods/DependencyInspector.h"
#include "zero/mods/DependencyScanner.h"
#include "zero/mods/InstalledModsModel.h"
#include "zero/mods/ModSnapshot.h"

class MinecraftInstance;
class ModFolderModel;
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

/// What the page needs from the application besides the model (tests inject fakes; the application page fills them from the instance).
struct ModsPageEnvironment {
    std::function<Content::TargetInstance()> target;  ///< name, Minecraft version and loaders of the instance
    std::function<bool()> isRunning;                  ///< the game runs: no changes
    std::function<bool(const QString&)> trash;        ///< moves a file to the Recycle Bin; default FS::trash. Never deletes for good.
    AskFunction ask;                                  ///< default: a ChoiceDialog
    std::function<void()> addMods;                    ///< "Add mods": Discover > Mods for this instance; empty hides the button
    std::function<void(QWidget* parent)> openClassic; ///< the classic mods page in a dialog; empty hides the menu entry
    UpdateCheckFactory checkUpdates;                  ///< starts an update check; empty hides "Check for updates" (default: Prism's checkers)
    UpdateApplier applyUpdates;                       ///< runs the downloads of chosen updates; empty = the updates cannot be applied
    std::function<bool()> curseForgeAvailable;        ///< the application can ask CurseForge; default: yes
    // The Dependency Inspector (docs/zero/CONTENT-MODS.md).
    std::function<void(const QString& query)> findMod;  ///< "Find" of a missing dependency: Discover > Mods for this instance, `query` as the search text
    std::function<Mods::InstanceContext()> context;       ///< what the instance runs (Minecraft, loader); default: read from `target()`
    Mods::ScanRunner scanRunner;                          ///< runs one scan; empty = the global thread pool with a parse cache
    int scanDelayMs = 300;                                ///< a scan starts this long after the last reload of the folder
    // Safe update (zero/mods/ModSnapshot, docs/zero/SHELL.md "Mods page"): a copy of the files an update replaces, and one click back.
    std::function<QString()> snapshotsDir;                ///< `<data>/zero/snapshots/<instance id>`; empty = no snapshots (test seam: updates then run without a safety copy)
    Mods::Snapshot::Hooks snapshotHooks;                  ///< seams of the core; `trashPath` defaults to `trash` above
};

class RunningBanner;
class ModSnapshotNotice;

class ModsPage : public QWidget, public BasePage {
    Q_OBJECT
   public:
    /// The application page for `inst`.
    ModsPage(MinecraftInstance* inst, ModFolderModel* model, QWidget* parent = nullptr);
    /// With an explicit environment (tests).
    ModsPage(const ModsPageEnvironment& env, ModFolderModel* model, QWidget* parent = nullptr);
    ~ModsPage() override;

    QString id() const override { return QStringLiteral("mods"); }
    QString displayName() const override { return tr("Mods"); }
    QIcon icon() const override { return QIcon::fromTheme("loadermods"); }
    QString helpPage() const override { return QStringLiteral("Loader-mods"); }
    void openedImpl() override;
    void closedImpl() override;

    /// What the page shows below the toolbar.
    enum class View { Loading, List, Empty, NoLoader, NoMatches };
    View view() const { return m_view; }
    bool isLoaded() const { return m_loaded; }
    bool isLocked() const { return m_locked; }

    Mods::Installed::Filter filter() const { return m_filter; }
    void setFilter(Mods::Installed::Filter filter);
    QString searchText() const;
    void setSearchText(const QString& text);

    /// The game started or stopped (also re-read from the environment).
    void refreshRunning();
    /// Reads the model into the list now (normally coalesced to the next event-loop turn).
    void rebuildNow();

    // The actions behind the switch, the keys, the selection bar and the row menu. `rows` are rows of the list.
    void requestToggle(const QList<int>& rows, Mods::Installed::ToggleAction action);
    void requestRemove(const QList<int>& rows);
    QList<int> selectedRows() const;

    // The update check. Results are kept until the next check, a change of a checked file, or the page's end.
    bool isChecking() const { return m_checking; }
    int updateCount() const;
    /// Starts a check of every mod the checkers can take. Ignored while the game runs, a check or an update runs, or nothing is loaded.
    void startCheck();
    /// Stops a running check and drops its answers.
    void abortCheck();
    /// Applies the updates found for these list rows / for every mod. Ignored while the game runs or a check runs.
    void requestUpdate(const QList<int>& rows);
    void requestUpdateAll();
    void selectRows(const QList<int>& rows);

    // Safe update: the newest snapshot that can still be rolled back is announced by a strip above the list.
    /// The snapshot the strip is about (`valid` is false when there is none).
    const Mods::Snapshot::Info& snapshot() const { return m_snapshot; }
    /// Asks, then rolls the mods of that snapshot back (empty = the newest active one). Ignored while the game runs, a check or an update runs.
    void requestRollback(const QString& snapshotDir = {});
    /// The strip's x: the snapshot stays (retention) but is no longer announced.
    void dismissSnapshot();
    /// Reads the newest snapshot again and shows or hides the strip.
    void refreshSnapshot();
    ModSnapshotNotice* snapshotNotice() const { return m_snapshotNotice; }

    // The Dependency Inspector: the scan behind the pills and the panel on the right of the list.
    /// Starts a scan now (normally 300 ms after a reload of the folder).
    void rescanNow();
    /// A scan has answered for the current folder (false until the first answer; the panel shows skeleton lines until then).
    bool hasScan() const { return m_scanned; }
    bool isScanning() const { return m_scanner && m_scanner->isScanning(); }
    const Mods::Inspection& inspection() const { return m_inspection; }
    Mods::DependencyScanner* scanner() const { return m_scanner; }
    ModDependencyPanel* panel() const { return m_panel; }
    bool isPanelOpen() const { return !m_panelKey.isEmpty(); }
    /// Opens the panel on the mod called `name` (selects its row); false when there is none. Also the dev aid of the screenshots.
    bool openPanelFor(const QString& name);
    void closePanel();

    // Parts, for the tests and the dev aids.
    ModListView* listView() const { return m_list; }
    ModListModel* listModel() const { return m_listModel; }
    ZeroEmptyState* emptyState() const { return m_empty; }
    ZeroSkeleton* skeleton() const { return m_skeleton; }
    QLineEdit* searchField() const { return m_search; }
    Content::SegmentedControl* filterControl() const { return m_filterControl; }
    QLabel* captionLabel() const { return m_caption; }
    QWidget* selectionBar() const { return m_selectionBar; }
    QLabel* selectionLabel() const { return m_selectionLabel; }
    QPushButton* selectionEnableButton() const { return m_selectionEnable; }
    QPushButton* selectionDisableButton() const { return m_selectionDisable; }
    QPushButton* selectionRemoveButton() const { return m_selectionRemove; }
    QPushButton* addButton() const { return m_addButton; }
    QPushButton* checkButton() const { return m_checkButton; }
    QPushButton* selectionUpdateButton() const { return m_selectionUpdate; }
    QLabel* infoLabel() const { return m_infoText; }
    QWidget* infoLine() const { return m_infoLine; }
    QAction* checkAgainAction() const { return m_checkAgain; }
    /// Fills `menu` with the `...` entries of `row` (selecting it unless it is part of a multi-selection); onRowMenu() shows it.
    void fillRowMenu(QMenu* menu, int row);
    QToolButton* overflowButton() const { return m_overflow; }
    QWidget* banner() const;

   signals:
    void addModsRequested();

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
    /// A row of the list and the Prism mod behind it, checked: `mod` is null when the Prism model moved on since the row was read.
    struct Picked {
        int listRow = -1;
        int sourceRow = -1;
        Mod* mod = nullptr;
    };
    QList<Picked> pick(const QList<int>& rows, bool* valid) const;
    bool stillThere(const Picked& picked) const;
    void onSwitchClicked(int row);
    void onToggleKey();
    void onRowMenu(int row, const QPoint& globalPos);
    void onEmptyAction();
    void openMenuFolder();
    void toast(const QString& text, ZeroToast::Kind kind);
    bool askFor(const ChoicePrompt& prompt, int* answer);

    /// No change of a mod right now: the game runs, a check reads the mods, or an update is being applied.
    bool changesBlocked() const { return m_locked || m_checking || m_applying; }
    void syncLock();
    void stopCheck(bool refresh);
    void onCheckFinished(bool ok, const QString& error);
    bool checkedModsStillThere() const;
    void applyKeys(const QStringList& keys);
    /// The files of the mods of `chosen` for the snapshot (the model's own paths and packwiz slugs); `error` is set when a mod is not found.
    QList<Mods::Snapshot::ModFiles> snapshotPlan(const QList<ModUpdate>& chosen, QString* error) const;
    void updateSnapshotNotice();
    /// Runs `work` on a pool thread and returns when it ended; the event loop runs meanwhile (no user input), so the window stays painted.
    void runBlocking(const std::function<void()>& work);
    Mods::Snapshot::Hooks snapshotHooks() const;
    void refreshFilterOptions();
    void refreshUpdateChrome(bool hasMods);

    // the Dependency Inspector
    void startScan();
    void onScanFinished(const Mods::Inspection& inspection);
    Mods::InstanceContext scanContext() const;
    void refreshPanel();
    void openPanelForKey(const QString& key);
    void onRowClicked(int row);
    void onDetailsKey();
    void onSelectionChanged();
    void showNeededBy(const QString& path);
    void enableFromPanel(const QString& path);
    PanelData panelDataFor(const Mods::Installed::Row& row, int rowIndex) const;
    int rowIndexOfPath(const QString& path) const;  ///< index into m_rows
    /// The mods of `picked` (Prism's, checked) are switched; the shared part of requestToggle() and the panel's Enable.
    void toggleMods(const QList<Picked>& picked, Mods::Installed::ToggleAction action);

    ModsPageEnvironment m_env;
    QPointer<ModFolderModel> m_model;

    // chrome
    RunningBanner* m_banner = nullptr;
    QWidget* m_toolbar = nullptr;
    QLineEdit* m_search = nullptr;
    Content::SegmentedControl* m_filterControl = nullptr;
    QWidget* m_statusRow = nullptr;  ///< under the toolbar: caption / selection bar, info line
    QStackedWidget* m_captionStack = nullptr;
    QLabel* m_caption = nullptr;
    QWidget* m_selectionBar = nullptr;
    QLabel* m_selectionLabel = nullptr;
    QPushButton* m_selectionEnable = nullptr;
    QPushButton* m_selectionDisable = nullptr;
    QPushButton* m_selectionRemove = nullptr;
    QPushButton* m_selectionUpdate = nullptr;
    QPushButton* m_checkButton = nullptr;
    QPushButton* m_addButton = nullptr;
    QAction* m_checkAgain = nullptr;
    QWidget* m_infoLine = nullptr;
    QLabel* m_infoIcon = nullptr;
    QLabel* m_infoText = nullptr;
    QToolButton* m_overflow = nullptr;
    QStackedWidget* m_stack = nullptr;
    ZeroSkeleton* m_skeleton = nullptr;
    ModListView* m_list = nullptr;
    ModListModel* m_listModel = nullptr;
    ZeroEmptyState* m_empty = nullptr;

    // state
    QList<Mods::Installed::Row> m_rows;  ///< every mod, in the Prism model's order
    QList<quintptr> m_tokens;            ///< the Prism mods of m_rows (compared, never dereferenced)
    Mods::Installed::Filter m_filter = Mods::Installed::Filter::All;
    QString m_query;
    View m_view = View::Loading;
    QTimer m_rebuildTimer;
    bool m_loaded = false;
    bool m_dirty = true;
    bool m_locked = false;
    bool m_startedWatching = false;
    bool m_mapsStale = false;  ///< a mod was removed: Prism's requires / required-by maps may point at it until they are rebuilt
    bool m_building = false;

    // update check
    Mods::Installed::UpdateResults m_results;
    QHash<QString, ModUpdate> m_updates;               ///< key -> what to download, for the entries of m_results that have an update
    QPointer<ModUpdateCheck> m_check;
    int m_checkGeneration = 0;                         ///< answers of an older check are ignored
    bool m_checking = false;
    bool m_applying = false;
    int m_checkedCount = 0;                            ///< mods handed to the checkers
    QHash<QString, QPair<QString, QString>> m_checkedFacts;  ///< key -> (metadata hash, file stamp) when the check started
    QSet<const void*> m_checkedPtrs;                   ///< the checked Resource objects (compared, never dereferenced)
    QHash<QString, QString> m_failures;                ///< key -> why this mod could not be checked
    QStringList m_failureOrder;
    bool m_hasUpdatesSegment = false;

    // safe update
    ModSnapshotNotice* m_snapshotNotice = nullptr;
    Mods::Snapshot::Info m_snapshot;
    QTimer m_snapshotTimer;  ///< keeps "2 h ago" current

    // the Dependency Inspector
    QWidget* m_body = nullptr;  ///< the list stack and the panel side by side
    ModDependencyPanel* m_panel = nullptr;
    Mods::DependencyScanner* m_scanner = nullptr;
    QTimer m_scanTimer;
    Mods::Inspection m_inspection;
    bool m_scanned = false;
    QString m_panelKey;  ///< Row::updateKey of the mod the panel shows ("" = closed)
};

}  // namespace ModsUi
}  // namespace Zero
