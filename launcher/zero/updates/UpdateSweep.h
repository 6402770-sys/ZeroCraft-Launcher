// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - "Updates for everything": finds the content updates of ALL instances and updates them (docs/zero/ARCHITECTURE.md,
// "UpdateSweep"). No widgets, no APPLICATION: everything the engine needs from the application comes through `Hooks`, so a test puts
// fakes in their place. Owned by the application (APPLICATION->zeroUpdates()); Home's "Updates" section shows it.
//
//  * checks run ONE AT A TIME (never two at once), each with a timeout; an error of one instance is kept for that instance and the next
//    one is checked. Skipped: instances that run or have a running download, managed modpack instances (their content is updated
//    through the pack) and instances the factory says have nothing to check (no mod with metadata);
//  * the summary (not the tasks) is saved to `<data>/zero/updates.json` and read again at the next start, so Home can say "N updates"
//    before any check ran;
//  * automatic: once per 24 h at most, a while after start-up, never while a game runs (it waits until the game ended);
//  * updateInstance / updateAll run a FRESH check of the instance (the tasks of an older check are never reused) and hand its updates to
//    the Applier, which snapshots the mods, runs the downloads as a Downloads-center job and finalizes the snapshot. An instance's
//    entries are dropped when its job ended Done. Checks never overlap; the next instance's check starts as soon as the previous job
//    STARTED (downloads may overlap).
//
// Only mods are covered: Prism's ShaderPacksPage / ResourcePacksPage update through the modal ResourceUpdateDialog, there is no headless
// checker for them. `UpdateEntry::kind` is "mod" for now; the caption code already knows the other kinds.
#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <functional>
#include <optional>

#include "ui/zero/instance/ModsUpdates.h"

namespace Zero {
namespace Updates {

constexpr int kFormat = 1;
inline constexpr char kKindMod[] = "mod";
inline constexpr char kKindShaderPack[] = "shaderpack";
inline constexpr char kKindResourcePack[] = "resourcepack";

struct UpdateEntry {
    QString key;         ///< Mods::Installed::Row::updateKey
    QString name;
    QString newVersion;
    QString kind;        ///< kKindMod | kKindShaderPack | kKindResourcePack
    bool operator==(const UpdateEntry& other) const
    {
        return key == other.key && name == other.name && newVersion == other.newVersion && kind == other.kind;
    }
};

/// What the last check said about one instance.
struct InstanceUpdates {
    QString instanceId;
    QString instanceName;
    QList<UpdateEntry> entries;
    qint64 checkedAt = 0;  ///< ms since the epoch
    QString error;         ///< the check failed (the entries are then empty); the section does not list such an instance
    bool hasUpdates() const { return error.isEmpty() && !entries.isEmpty(); }
};

/// One instance as the engine sees it right now.
struct InstanceSource {
    QString id;
    QString name;
    bool running = false;  ///< the game runs (or its launch is under way)
    bool busy = false;     ///< a Downloads-center job writes into the instance
    bool managed = false;  ///< belongs to a modpack (Modrinth / CurseForge / Technic): updated through the pack
};

/// "5 mods", "1 mod", "5 mods · 1 shader pack": what the Updates section says under an instance's name.
QString captionFor(const QList<UpdateEntry>& entries);

class UpdateSweep : public QObject {
    Q_OBJECT
   public:
    /// A check for the instance's content: the same seam the Mods page uses. nullptr = nothing to check (no mod with metadata, the
    /// instance is gone). The check is owned by the engine (deleted after `finished`); every ModUpdate::download of a successful check
    /// must stay usable until the Applier ran.
    using CheckFactory = std::function<ModsUi::ModUpdateCheck*(const QString& instanceId, QObject* parent)>;
    /// Starts the update of `updates` of the instance (snapshot first). `started()` is called exactly once as soon as the job runs (or
    /// nothing could start); `done(ok, error)` exactly once when it ended (later, on the GUI thread; `error` = a user-facing reason
    /// when nothing could be done). `done` never comes before `started`.
    using Applier = std::function<void(const QString& instanceId, const QList<ModsUi::ModUpdate>& updates, std::function<void()> started,
                                       std::function<void(bool ok, const QString& error)> done)>;

    struct Hooks {
        std::function<QList<InstanceSource>()> instances;  ///< every instance, with its state right now
        CheckFactory checkFactory;
        Applier apply;                                     ///< empty = updates cannot be applied
        std::function<bool()> autoEnabled;                 ///< "ZeroAutoCheckUpdates" (empty = on)
        std::function<qint64()> now;                       ///< ms since the epoch (default: the clock)
    };
    struct Timing {
        int startDelayMs = 30 * 1000;               ///< the first automatic check after start()
        int retryMs = 60 * 1000;                    ///< the next look while a game runs
        int recheckMs = 60 * 60 * 1000;             ///< the next look when nothing was due
        int afterGameMs = 5 * 1000;                 ///< after a game ended while an automatic check waited for it
        int timeoutMs = 60 * 1000;                  ///< one instance's check
        qint64 intervalMs = 24LL * 60 * 60 * 1000;  ///< between two automatic checks
    };

    enum class State {
        Idle,
        Queued,    ///< waits for its turn (a check or an update)
        Checking,  ///< its check runs now (also the fresh check before an update)
        Updating,  ///< its update job was started and has not ended
    };

    /// Reads `path` now (`<data>/zero/updates.json`; an unreadable file is moved aside to `.corrupt`).
    UpdateSweep(const QString& path, Hooks hooks, QObject* parent = nullptr);
    UpdateSweep(const QString& path, Hooks hooks, Timing timing, QObject* parent = nullptr);
    ~UpdateSweep() override;

    /// Arms the automatic check (`startDelayMs` from now).
    void start();

    // ---- what is known ----
    /// The instances that have updates (no error), sorted by name; instances that no longer exist are not listed.
    QList<InstanceUpdates> results() const;
    int totalUpdates() const;
    /// When the last check of all instances ended (0 = never).
    qint64 checkedAt() const { return m_checkedAt; }
    State stateOf(const QString& instanceId) const;
    /// True while a check of all instances runs (queued or running parts).
    bool checking() const { return m_sweepActive; }
    /// True while anything is queued or running (checks, fresh checks, update preparation).
    bool working() const { return m_current.has_value() || !m_queue.isEmpty(); }
    /// The instance being checked now ("" = none).
    QString currentName() const;
    int checkedCount() const { return m_sweepDone; }
    int checkTotal() const { return m_sweepTotal; }

    // ---- actions ----
    /// Checks every eligible instance, one at a time. Does nothing while a check of all instances is running.
    void checkNow();
    /// A fresh check of the instance, then its updates go to the Applier. Does nothing while it is queued for an update or updating.
    void updateInstance(const QString& instanceId);
    /// updateInstance() for every instance that has updates (they are queued, and run before the checks of a running sweep).
    void updateAll();
    /// A game started or ended: an automatic check that waits for the end looks again soon.
    void onGameStateChanged();

   signals:
    void started();
    /// `done` instances were checked so far, `total` in this check; `currentName` is the one being checked (the one after `done`).
    void progress(int done, int total, const QString& currentName);
    void finished();
    /// The list of instances with updates (or their entries) changed.
    void resultsChanged();
    /// The state of one instance changed (queued, checking, updating, idle).
    void stateChanged(const QString& instanceId);
    /// An update could not be started or the fresh check failed: `message` is user-facing ("Fabric Fun: ...").
    void problem(const QString& instanceId, const QString& message);

   private:
    struct Work {
        enum class Kind { Check, Update };
        Kind kind = Kind::Check;
        QString id;
    };

    void load();
    void save() const;
    void pruneVanished();
    QList<InstanceSource> sources() const;
    std::optional<InstanceSource> sourceOf(const QString& id) const;
    qint64 now() const;

    void enqueue(const Work& work, bool front);
    void pump();
    bool startCheckOf(const Work& work, const InstanceSource& source);
    void onCheckFinished(bool ok, const QString& error);
    void onTimeout();
    void finishCurrent();
    void recordCheck(const QString& id, const QString& name, bool ok, const QString& error, const QList<ModsUi::ModUpdate>& found);
    void dropEntries(const QString& id);
    void runApplier(const Work& work, const QString& name, const QList<ModsUi::ModUpdate>& updates);
    void sweepStep(bool counted);
    void finishSweepIfDone();
    void autoTick();
    void scheduleAuto(int ms);
    bool anyRunning() const;
    InstanceUpdates* recordFor(const QString& id);

    QString m_path;
    Hooks m_hooks;
    Timing m_timing;

    QList<InstanceUpdates> m_records;  ///< saved: instances with updates or an error
    qint64 m_checkedAt = 0;

    QList<Work> m_queue;
    std::optional<Work> m_current;
    QString m_currentName;
    ModsUi::ModUpdateCheck* m_check = nullptr;
    QTimer m_timeout;
    QSet<QString> m_updating;

    bool m_sweepActive = false;
    int m_sweepTotal = 0;
    int m_sweepDone = 0;
    QSet<QString> m_sweepPending;  ///< instances of the running check of all that were not checked yet

    QTimer m_autoTimer;
    bool m_waitingForGame = false;
};

}  // namespace Updates
}  // namespace Zero
