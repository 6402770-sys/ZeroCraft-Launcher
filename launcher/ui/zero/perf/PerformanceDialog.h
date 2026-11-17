// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - "Boost performance" (docs/zero/SHELL.md, "Boost performance"): one dialog that adds the curated performance mods for the
// instance's Minecraft version + loader, gives Minecraft the memory the PC can spare and points at a wrong Java.
//
// Top to bottom: "Boost performance . <instance>", then MODS . FABRIC 1.21.1 (a checklist: ticked = will be added; Installed / Not available
// for 1.21.1 / Conflicts with OptiFine rows cannot be ticked; skeleton rows while Modrinth is asked, an inline error with Retry when it fails;
// one line for vanilla instances; a note for managed modpacks), MEMORY (the PC's RAM, "Give Minecraft 6 GB" with the current value, or "Memory is
// fine (6 GB)" - it never lowers), JAVA (only when Java Doctor sees a problem; Fix closes the dialog and opens Tools > Java Doctor), and the
// footer: the summary ("Adds 4 mods (+1 they need) . Memory 2 -> 6 GB"), Cancel, Apply.
//
// The dependencies of the ticked mods are resolved in ONE `DependencyResolver` run, 300 ms after the last tick (generation token: a stale answer is
// dropped) so the summary's "+N they need" is real; Apply stays disabled until it finished. Apply hands the ticked mods and their
// dependencies to the install seam (a background job of the Downloads center: not awaited) and writes the memory and Java-argument settings at
// once, whatever the downloads do. Nothing changes without Apply.
//
// Everything the dialog needs from the application comes through `PerformanceEnvironment`, so tests build it without APPLICATION or network.
#pragma once

#include <QDialog>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "zero/content/CartInstall.h"
#include "zero/content/ModInstall.h"
#include "zero/content/ModTarget.h"
#include "zero/perf/JvmArgsAdvisor.h"
#include "zero/perf/PerformanceCatalog.h"
#include "zero/perf/PerformanceProfile.h"

class QCheckBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace Zero {
class PillTag;
class RemoteImageCache;
class ZeroSpinner;
}  // namespace Zero

namespace Zero::Content {
class DependencySkeleton;
class PackIconWidget;
}  // namespace Zero::Content

namespace Zero::Perf {

struct PerformanceEnvironment {
    Zero::Content::TargetInstance target;  ///< name, Minecraft version, loaders (mod-ready or not)
    bool managedPack = false;              ///< the instance belongs to a modpack (Modrinth / CurseForge / Technic)
    int ramMb = 0;                         ///< the PC's RAM; 0 = unknown (no memory section)
    Zero::RemoteImageCache* images = nullptr;  ///< null = RemoteImageCache::instance()

    /// Creates the Modrinth lookup as a child of `parent`; null = the lookup is impossible (shown as an error).
    std::function<PerformanceCatalog*(QObject* parent)> makeCatalog;
    /// Creates the dependency check as a child of `parent`; null = no check (shown like "turned off": no extra mods).
    std::function<Zero::Content::DependencyResolver*(QObject* parent)> makeResolver;
    /// False = the "ModDependenciesDisabled" setting is on. Null = enabled.
    std::function<bool()> dependencyCheckEnabled;
    /// Reads the mods folder (may answer at once or later, exactly once, and not at all once `context` is gone). Null = nothing is installed.
    std::function<void(QObject* context, std::function<void(const InstalledSnapshot&)>)> loadInstalled;
    /// The max heap now (the instance's own value when it overrides memory, else the launcher's).
    std::function<int()> maxMemoryMb;
    /// Looks at the Java of the instance (may answer at once or later, exactly once, and not at all once `context` is gone): the one-line problem, or nothing when it is fine.
    std::function<void(QObject* context, std::function<void(const std::optional<QString>&)>)> checkJava;
    /// The Java major the instance will run with (asked once Java Doctor's answer is in); 0 or null = unknown (no Java arguments section).
    std::function<int()> javaMajor;
    /// The PC's logical cores; null = QThread::idealThreadCount().
    std::function<int()> cpuCores;
    /// The Java arguments the instance has now (its own when it overrides them, else the launcher's); null = none.
    std::function<QString()> currentJvmArgs;
    /// Writes OverrideJavaArgs = true and JvmArgs = `args` to the instance and returns what it had (for Undo). Null = nothing is written.
    std::function<JvmArgsPrevious(const QString& args)> writeJvmArgs;
    /// Whether the instance runs right now; null = TargetInstance::running.
    std::function<bool()> isRunning;
    /// Hands `plan` to the background Downloads center and returns at once (the downloads are not awaited: Apply writes memory and Java
    /// arguments straight away, a failed download is reported by the seam's owner). Null = nothing can be installed.
    std::function<void(const Zero::Content::CartInstallPlan& plan)> install;
    /// Writes OverrideMemory = true, MaxMemAlloc and MinMemAlloc to the instance.
    std::function<void(int maxMb, int minMb)> writeMemory;
    /// The wait after a change of the ticks before the dependencies are asked again.
    int dependencyDelayMs = 300;

    /// The real thing for the instance `instanceId`: Modrinth catalog, GetModDependenciesTask, the mods folder model, HardwareInfo, Java Doctor's
    /// diagnosis, runInstallPlan() through `install`. `install` is the main window's.
    static PerformanceEnvironment fromApplication(const QString& instanceId,
                                                  std::function<void(const Zero::Content::CartInstallPlan&)> install);
};

class PerformanceDialog : public QDialog {
    Q_OBJECT
   public:
    static constexpr int kWidth = 560;
    static constexpr int kMaxBodyHeight = 520;  ///< px of sections before the body scrolls

    enum class ModsState { Vanilla, Loading, Ready, Failed };

    /// What Apply did (read after exec()).
    struct Outcome {
        bool applied = false;        ///< something was changed (mods handed to the Downloads center or memory / Java arguments written)
        int modsQueued = 0;          ///< downloads handed to the install seam (mods and dependencies); they arrive later, in the background
        bool memoryWritten = false;
        bool refusedRunning = false; ///< Apply was pressed while the instance runs: nothing was changed
        bool javaArgsWritten = false;      ///< the recommended Java arguments were written (an override of this instance)
        JvmArgsPrevious previousJvmArgs;   ///< what the instance had before: Undo puts it back (restoreJvmArgs)
    };

    explicit PerformanceDialog(PerformanceEnvironment env, QWidget* parent = nullptr);
    ~PerformanceDialog() override;

    const Outcome& outcome() const { return m_outcome; }
    /// "Fix" of the Java line was pressed: the dialog closed so the caller can open Tools > Java Doctor.
    bool javaFixRequested() const { return m_javaFixRequested; }
    /// Nothing is loading and the pictures are in: the screenshot aid waits for it.
    bool isSettled() const;
    /// Calls `done` once settled, or after `timeoutMs`.
    void callWhenSettled(const std::function<void()>& done, int timeoutMs);

    // ---- state (tests, dev aids) ----
    ModsState modsState() const { return m_modsState; }
    QString titleText() const;
    QString modsTitleText() const;
    QString modsLineText() const;  ///< vanilla: the one line
    QString errorText() const;     ///< Failed: why
    QString noteText() const;      ///< the managed modpack note ("" = none)
    QStringList rowSlugs() const;
    int rowCount() const { return int(m_rows.size()); }
    RowState rowState(const QString& slug) const;
    QString rowName(const QString& slug) const;
    QString rowPillText(const QString& slug) const;
    QCheckBox* rowCheck(const QString& slug) const;
    int skeletonRowCount() const;
    bool memorySectionVisible() const;
    QString ramLineText() const;
    QString memoryText() const;  ///< "Give Minecraft 6 GB" or "Memory is fine (6 GB)"
    QString memoryNowText() const;
    QCheckBox* memoryBox() const { return m_memoryBox; }
    bool javaSectionVisible() const;
    QString javaText() const;
    QPushButton* javaFixButton() const { return m_javaFix; }
    bool jvmArgsSectionVisible() const;
    JvmArgsState jvmArgsState() const { return m_jvm.state; }
    QCheckBox* jvmArgsBox() const { return m_jvmBox; }
    QString jvmArgsBoxText() const;      ///< the row's text ("" = no row)
    QString jvmArgsCurrentText() const;  ///< HasCustomArgs: the arguments shown (elided)
    QString jvmArgsCurrentToolTip() const;
    QString jvmArgsFineText() const;     ///< AlreadyRecommended: the calm line ("" = none)
    QPushButton* jvmArgsDetailsButton() const { return m_jvmDetailsButton; }
    bool jvmArgsDetailsVisible() const;
    QStringList jvmArgsExplanations() const;  ///< the "What they do" lines, "flag - why"
    /// The arguments Apply writes (the row is ticked and offered); empty when none.
    std::optional<JvmArgsAdvice> jvmArgsToWrite() const;
    QString summaryText() const;
    QPushButton* applyButton() const { return m_apply; }
    QPushButton* cancelButton() const { return m_cancel; }
    QPushButton* retryButton() const { return m_retry; }
    QPushButton* dependencyRetryButton() const { return m_depRetry; }
    /// The ticked mods.
    QStringList tickedSlugs() const;
    /// The dependencies that would be added (names), after the dependency check.
    QStringList dependencyNames() const;
    bool dependencyCheckPending() const { return m_depTimer.isActive() || m_depState == DepState::Checking; }
    bool dependencyCheckFailed() const { return m_depState == DepState::Failed; }
    /// What Apply installs now.
    Zero::Content::CartInstallPlan plan() const;
    /// The memory change Apply writes (toggle on and the recommendation is higher); empty when none.
    std::optional<MemoryAdvice> memoryToWrite() const;
    bool canApply() const;

   public slots:
    void accept() override;

   protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

   private:
    enum class DepState { Idle, Checking, Done, Failed };

    struct RowUi {
        Row row;
        QWidget* widget = nullptr;
        QCheckBox* check = nullptr;
        Zero::Content::PackIconWidget* icon = nullptr;
        QLabel* name = nullptr;
        QLabel* benefit = nullptr;
        PillTag* pill = nullptr;
    };

    void buildUi();
    void startCatalog();
    void startInstalled();
    void startJava();
    void onInstalled(const InstalledSnapshot& installed);
    void onCatalogLoaded(quint64 token, const FoundMap& found);
    void onCatalogFailed(quint64 token, const QString& reason);
    void setModsState(ModsState state, const QString& error = {});
    void tryBuildRows();
    void buildRows();
    void clearRows();
    RowUi* rowUi(const QString& slug) const;
    void onTicksChanged();
    void afterTicksChanged(int delayMs);
    void refreshMemory();
    void refreshJvmArgs();
    void refreshSummary();
    void refreshIcons();
    void fitBody();
    int tickedCount() const;

    void scheduleDependencyCheck(int delayMs);
    void runDependencyCheck();
    void onResolved(quint64 token, const QList<Zero::Content::PlannedMod>& dependencies);
    void onResolveFailed(quint64 token, const QString& reason);
    Zero::Perf::Summary currentSummary() const;
    bool instanceRunning() const;

    PerformanceEnvironment m_env;
    Loader m_loader = Loader::None;
    ModsState m_modsState = ModsState::Loading;
    QString m_error;

    PerformanceCatalog* m_catalog = nullptr;
    quint64 m_catalogToken = 0;
    bool m_catalogReady = false;
    FoundMap m_found;
    bool m_installedReady = false;
    InstalledSnapshot m_installed;

    std::vector<std::unique_ptr<RowUi>> m_rows;

    DepState m_depState = DepState::Done;
    quint64 m_depToken = 0;
    Zero::Content::DependencyResolver* m_resolver = nullptr;
    QTimer m_depTimer;
    QList<Zero::Content::PlannedMod> m_deps;

    MemoryAdvice m_advice;
    bool m_memoryTouched = false;  ///< the user decided the toggle: it no longer follows the recommendation
    bool m_javaFixRequested = false;
    bool m_javaChecked = false;
    Outcome m_outcome;

    QLabel* m_title = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_content = nullptr;
    // mods section
    QLabel* m_modsTitle = nullptr;
    QWidget* m_skeleton = nullptr;
    QWidget* m_rowsHost = nullptr;
    QVBoxLayout* m_rowsLayout = nullptr;
    QLabel* m_modsLine = nullptr;  ///< vanilla line
    QWidget* m_errorRow = nullptr;
    QLabel* m_errorLabel = nullptr;
    QPushButton* m_retry = nullptr;
    QWidget* m_depRow = nullptr;
    Zero::ZeroSpinner* m_depSpinner = nullptr;
    QLabel* m_depLabel = nullptr;
    QPushButton* m_depRetry = nullptr;
    QWidget* m_noteRow = nullptr;
    QLabel* m_noteIcon = nullptr;
    QLabel* m_note = nullptr;
    // memory section
    QWidget* m_memorySection = nullptr;
    QLabel* m_ramLabel = nullptr;
    QWidget* m_memoryRow = nullptr;
    QCheckBox* m_memoryBox = nullptr;
    QLabel* m_memoryNow = nullptr;
    QLabel* m_memoryFine = nullptr;
    // java section
    QWidget* m_javaSection = nullptr;
    QLabel* m_javaLabel = nullptr;
    QPushButton* m_javaFix = nullptr;
    // Java arguments section
    JvmArgsAdvice m_jvm;
    bool m_jvmTouched = false;  ///< the user decided the tick: it no longer follows the state
    QWidget* m_jvmSection = nullptr;
    QWidget* m_jvmRow = nullptr;
    QCheckBox* m_jvmBox = nullptr;
    QLabel* m_jvmCurrent = nullptr;
    QLabel* m_jvmFine = nullptr;
    QPushButton* m_jvmDetailsButton = nullptr;
    QWidget* m_jvmDetails = nullptr;
    QVBoxLayout* m_jvmDetailsLayout = nullptr;
    // footer
    QLabel* m_summary = nullptr;
    QPushButton* m_cancel = nullptr;
    QPushButton* m_apply = nullptr;
};

}  // namespace Zero::Perf
