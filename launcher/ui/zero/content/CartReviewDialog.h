// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the review of the Discover install cart (docs/zero/SHELL.md, "Discover > Install cart").
//
// Top to bottom: "Add 3 mods" (the cart's counts), "To <instance> . 1.21.1 Fabric", a note while the target runs, then the cart's
// entries in groups (MODS, RESOURCE PACKS, SHADERS - only the groups that have rows). A row is the item's icon and name, the version
// combo (the compatible versions of the target, the default of the single dialog preselected) and a remove X (removes the row from the
// dialog AND from the cart). A row that cannot be added says why: "Finding versions..." while its versions load, "Couldn't load
// versions" + Retry, "No version for 1.21.1 Fabric . will be skipped", "Needs a modded instance . will be skipped", and an update (the
// target has an older file) reads "Update -> 2.0".
//
// ONE dependency run covers every mod version of the dialog together (`DependencyResolver::Request::also`); it starts once no row is
// loading, and again 300 ms after any version change or removal (generation tokens: an answer for a replaced run is dropped; the
// resolver is a child of the dialog and cancelled on close). The result is merged by project (mergeDependencies): "Required by Sodium,
// Lithium", a dependency that is itself a cart row shows once (as the row), the ones the mods folder probably has go under "Already
// installed". Resource packs: "Turn them on" (1.13+ targets, checked) puts them on top of the game's list in cart order afterwards.
//
// The footer reads "Checking..." (disabled) while versions or dependencies load, then "Add N mods" / "Add N items" with N = everything
// that will download, dependencies included. The dialog only decides WHAT to install (`plan()`); MainWindow::installCart() downloads.
#pragma once

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "zero/content/CartInstall.h"
#include "zero/content/InstallCart.h"
#include "zero/content/InstalledMods.h"
#include "zero/content/ModInstall.h"
#include "zero/content/ModTarget.h"
#include "zero/content/ModpackSource.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QToolButton;
class QVBoxLayout;

namespace Zero {
class RemoteImageCache;
class ZeroSpinner;
}  // namespace Zero

namespace Zero::Content {

class DependencySkeleton;
class PackIconWidget;

/// Everything the dialog needs from the application, so tests build it without APPLICATION or network.
struct CartReviewEnvironment {
    /// The Discover source that loads the versions of an entry (the one of its section and provider); null = the versions cannot be loaded.
    std::function<ModpackSource*(const CartEntry&)> sourceFor;
    Zero::RemoteImageCache* images = nullptr;  ///< null = RemoteImageCache::instance()
    /// Creates the dependency check as a child of `parent`; null = no check is possible (shown like "turned off").
    std::function<DependencyResolver*(QObject* parent)> makeResolver;
    /// False = the "ModDependenciesDisabled" setting is on: no check. Null = enabled.
    std::function<bool()> dependencyCheckEnabled;
    /// The installed mods of the target (matched with the versions' required dependencies); null = none.
    std::function<QList<InstalledMod>()> installedMods;
    /// The file id of the installed file of an entry in the target (non-empty = the row is an update); null = nothing is installed.
    std::function<QString(const CartEntry&)> installedFileId;
    /// Whether the instance runs right now; null = TargetInstance::running.
    std::function<bool(const QString& instanceId)> isRunning;
    /// The wait after a version change or a removal before the dependencies are asked again.
    int dependencyDelayMs = 300;
    /// The longest a started dependency run may stay unanswered: then it ends as a failure with Retry (the resolver's own limit,
    /// InstanceDependencyResolver::kRunTimeoutMs, comes first; this one catches any resolver that never answers).
    int dependencyTimeoutMs = 60000;

    /// The real thing: GetModDependenciesTask as the resolver, the "ModDependenciesDisabled" setting.
    static CartReviewEnvironment fromApplication(std::function<ModpackSource*(const CartEntry&)> sourceFor,
                                                 std::function<QList<InstalledMod>()> installedMods,
                                                 std::function<QString(const CartEntry&)> installedFileId);
};

class CartReviewDialog : public QDialog {
    Q_OBJECT
   public:
    static constexpr int kWidth = 560;
    static constexpr int kMaxListHeight = 380;                     ///< px of rows before the list scrolls
    static constexpr quint64 kGenerationBase = 0x7a65726f63000000ULL;  ///< the generation tokens of the version requests count up from here

    enum class RowState { Loading, Ready, NoVersion, Failed, NeedsModded };

    /// Reviews the entries of `cart` (a snapshot at construction; the remove X removes from `cart` too) for `target`.
    CartReviewDialog(CartReviewEnvironment env, InstallCartModel* cart, const TargetInstance& target, QWidget* parent = nullptr);
    ~CartReviewDialog() override;

    const TargetInstance& target() const { return m_target; }
    /// What Add installs now.
    CartInstallPlan plan() const;
    /// Add can be pressed: nothing is loading, something will download, the target does not run (and, after a failed dependency check,
    /// "Add without dependencies" is ticked).
    bool canInstall() const;
    /// Versions or the dependency check are still on their way (the footer reads "Checking...").
    bool isChecking() const;
    /// Nothing is loading and the pictures are in: the screenshot aid waits for it.
    bool isSettled() const;
    /// Calls `done` once settled, or after `timeoutMs`.
    void callWhenSettled(const std::function<void()>& done, int timeoutMs);
    /// The resource packs go on top of the game's list afterwards (the box is there and ticked).
    bool turnThemOn() const;

    // ---- parts (tests, dev aids) ----
    QString titleText() const;
    QString subtitleText() const;
    QString noteText() const;
    QStringList groupTitles() const;  ///< the visible groups, in order ("Mods", "Resource packs", "Shaders"; shown upper-case)
    int rowCount() const { return int(m_rows.size()); }
    QStringList rowKeys() const;
    bool hasRow(const QString& key) const { return row(key) != nullptr; }
    RowState rowState(const QString& key) const;
    QString rowName(const QString& key) const;
    /// The line under the name: "by Author", "Update -> 2.0", "Finding versions...", the reason a row is skipped.
    QString rowStatus(const QString& key) const;
    QComboBox* versionBox(const QString& key) const;
    QToolButton* removeButton(const QString& key) const;
    QPushButton* retryButton(const QString& key) const;
    /// Removes the row as its X does.
    void removeRow(const QString& key);
    QCheckBox* turnOnBox() const { return m_turnOn; }
    QCheckBox* skipDependenciesBox() const { return m_skip; }
    QPushButton* primaryButton() const { return m_primary; }
    QPushButton* cancelButton() const { return m_cancel; }
    QPushButton* dependencyRetryButton() const { return m_depRetry; }
    QStringList dependencyNames() const;
    QStringList dependencyRequiredBy() const;
    QString dependencyStatus() const;
    QString alreadyInstalledText() const;
    /// The dependencies block is shown (at least one mod row can be added).
    bool dependencyBlockVisible() const;
    bool dependencyCheckFailed() const { return m_depState == DepState::Failed; }
    /// The dependency check is waiting for its 300 ms or running.
    bool dependencyPending() const { return m_depTimer.isActive() || m_depState == DepState::Checking; }

   public slots:
    void accept() override;

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    enum class DepState { Idle, Checking, Done, Failed, Disabled };

    struct Row {
        CartEntry entry;
        QString installedFileId;
        RowState state = RowState::Loading;
        QList<ModPlatform::IndexedVersion> versions;  ///< the compatible ones, in the provider's order
        quint64 generation = 0;                       ///< of the version request in flight
        QWidget* widget = nullptr;
        PackIconWidget* icon = nullptr;
        QLabel* name = nullptr;
        QLabel* status = nullptr;
        Zero::ZeroSpinner* spinner = nullptr;
        QComboBox* combo = nullptr;
        QPushButton* retry = nullptr;
        QToolButton* remove = nullptr;
    };

    void buildUi();
    QWidget* buildGroup(Kind kind, QVBoxLayout** rowsLayout);
    void buildRow(Row& row);
    void requestVersions(Row& row);
    void connectSource(ModpackSource* source);
    void onVersionsFinished(quint64 generation, const ContentItemPtr& item);
    void onVersionsForFinished(quint64 generation, const ContentItemPtr& item, const QList<ModPlatform::IndexedVersion>& versions);
    void onVersionsFailed(quint64 generation);
    void setRowVersions(Row& row, const QList<ModPlatform::IndexedVersion>& all);
    void setRowState(Row& row, RowState state);
    void refreshRowText(Row& row);
    void onVersionChanged(Row& row);
    std::optional<ModPlatform::IndexedVersion> chosenVersion(const Row& row) const;
    Row* row(const QString& key) const;
    Row* rowByGeneration(quint64 generation) const;

    void scheduleDependencyCheck(int delayMs);
    void runDependencyCheck();
    /// Starts the run that runDependencyCheck() postponed because a row was loading, once no row loads any more.
    void resumeDependencyCheck();
    void startDependencyCheckNow();
    void onResolved(quint64 token, const QList<PlannedMod>& dependencies);
    void onResolveFailed(quint64 token, const QString& reason);
    void setDependencyState(DepState state, const QString& message = {});
    bool anyLoading() const;
    int countOf(Kind kind) const;
    bool hasMods() const;

    void refreshState();
    void refreshTexts();
    void refreshIcons();
    void fitList();
    bool targetRunning() const;

    CartReviewEnvironment m_env;
    InstallCartModel* m_cart;
    TargetInstance m_target;
    std::vector<std::unique_ptr<Row>> m_rows;
    QList<ModpackSource*> m_connected;
    quint64 m_nextGeneration = kGenerationBase;

    DepState m_depState = DepState::Idle;
    quint64 m_token = 0;  ///< generation of the dependency check: an answer for another token is dropped
    DependencyResolver* m_resolver = nullptr;
    QTimer m_depTimer;
    QTimer m_depWatchdog;               ///< dependencyTimeoutMs after a run started: it ends as a failure with Retry
    bool m_depAwaitingVersions = false;  ///< the due run found a row loading: the last row to settle starts it
    QList<PlannedMod> m_deps;       ///< the dependencies that would be added
    QStringList m_alreadyInstalled;  ///< required dependencies the target already has

    QLabel* m_title = nullptr;
    QLabel* m_subtitle = nullptr;
    QWidget* m_noteRow = nullptr;
    QLabel* m_noteIcon = nullptr;
    QLabel* m_note = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_content = nullptr;
    QWidget* m_groups[3] = { nullptr, nullptr, nullptr };  // mods, resource packs, shaders
    QVBoxLayout* m_groupRows[3] = { nullptr, nullptr, nullptr };
    QWidget* m_depBlock = nullptr;
    QLabel* m_depStatus = nullptr;
    DependencySkeleton* m_depSkeleton = nullptr;
    QWidget* m_depRows = nullptr;
    QVBoxLayout* m_depRowsLayout = nullptr;
    QLabel* m_depInstalled = nullptr;
    QPushButton* m_depRetry = nullptr;
    QCheckBox* m_skip = nullptr;
    QCheckBox* m_turnOn = nullptr;
    QWidget* m_footer = nullptr;
    QPushButton* m_cancel = nullptr;
    QPushButton* m_primary = nullptr;
};

}  // namespace Zero::Content
