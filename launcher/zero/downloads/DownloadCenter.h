// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the Downloads center: runs install tasks in the background (no modal dialog), tracks their progress and keeps a short
// history of the finished ones. No widgets. App-level service created by Application (APPLICATION->zeroDownloads(), global()).
//
// A caller wraps its Task (usually a ConcurrentTask of ResourceDownloadTasks) in a DownloadJob and calls start(); the task runs at
// once and start() returns an id. Progress comes from the task's stepProgress() signals (bytes summed per sub-step, plus progress()
// as a fraction when no bytes are known). When the task ends, onFinished is called exactly once with a DownloadResult and the job
// moves to the history (newest first, at most kMaxHistory entries, JSON at `<data>/zero/downloads.json`).
// While a job of an instance runs, isBusy(instanceId) is true: Application::launch refuses to start that instance.
// See docs/zero/ARCHITECTURE.md ("Downloads center").
#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <functional>
#include <memory>
#include <optional>
#include "DownloadText.h"

namespace Zero {
namespace Downloads {

constexpr int kMaxHistory = 100;

class DownloadCenter : public QObject {
    Q_OBJECT
   public:
    using Clock = std::function<qint64()>;

    /// `historyPath` = `<data>/zero/downloads.json` (loaded now; an unreadable file is moved aside and the history starts empty).
    /// `clock` returns ms since the epoch (default QDateTime::currentMSecsSinceEpoch); tests inject their own.
    explicit DownloadCenter(const QString& historyPath, Clock clock = {}, QObject* parent = nullptr);
    ~DownloadCenter() override;

    /// The process-wide instance created by the application (nullptr before that). The first created instance is global.
    static DownloadCenter* global();

    /// Starts `job.task` (non-modal) and returns its id at once. The job is listed (jobAdded) before the task starts, so a task that
    /// ends synchronously is still reported. An empty id means nothing was started (no task).
    QString start(DownloadJob job);

    /// Asks the task to abort. False when there is no such job or the task cannot abort (nothing happens then). The job ends as
    /// Cancelled when the task reports its abort.
    bool cancel(const QString& id);
    /// cancel() for every active job.
    void cancelAll();

    /// True while a job of this instance runs (never for an empty id).
    bool isBusy(const QString& instanceId) const;
    int activeCount() const { return int(m_order.size()); }
    /// Oldest first.
    QList<ActiveDownload> activeJobs() const;
    std::optional<ActiveDownload> activeJob(const QString& id) const;

    /// Newest first.
    const QList<HistoryEntry>& history() const { return m_history; }
    void clearHistory();

    qint64 now() const;
    QString historyPath() const { return m_path; }

   signals:
    void jobAdded(const QString& id);
    /// Progress, speed or status of a job changed (at most ~10 times a second per job).
    void jobChanged(const QString& id);
    /// The job ended; its history entry exists and it is no longer active.
    void jobFinished(const QString& id);
    void historyChanged();
    void activeCountChanged(int count);

   private:
    struct Step {
        qint64 current = 0;
        qint64 total = -1;
    };
    struct Sample {
        qint64 ms = 0;
        qint64 bytes = 0;
    };
    struct Job {
        QString id;
        DownloadJob spec;
        QHash<QUuid, Step> steps;
        qint64 progressCurrent = 0;
        qint64 progressTotal = -1;
        qint64 bytesDone = 0;
        qint64 bytesTotal = 0;
        double fraction = -1;
        QString status;
        QString details;
        QList<Sample> samples;
        qint64 lastEmitMs = 0;
        bool dirty = false;
        bool finished = false;
        bool cancelRequested = false;
    };

    void connectTask(const std::shared_ptr<Job>& job);
    void onStepProgress(const std::shared_ptr<Job>& job, const TaskStepProgress& step);
    void recompute(Job& job);
    void touch(Job& job);
    void finish(const QString& id, DownloadResult::State state, const QString& error);
    ActiveDownload snapshot(const Job& job) const;
    double speedOf(const Job& job) const;
    void tick();

    void load();
    void save() const;

    QString m_path;
    Clock m_clock;
    QHash<QString, std::shared_ptr<Job>> m_jobs;
    QStringList m_order;
    QList<HistoryEntry> m_history;
    QTimer m_tick;
};

}  // namespace Downloads
}  // namespace Zero
