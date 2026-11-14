// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Jobs.h"

#include <QThread>
#include <atomic>
#include <functional>

namespace ZeroSetup {

/// Runs one install / uninstall job off the UI thread. progress() is delivered queued to the receiver.
class Worker : public QThread
{
    Q_OBJECT
   public:
    using Job = std::function<JobResult(const std::atomic<bool>* cancel, const JobProgress& progress)>;
    Worker(Job job, QObject* parent = nullptr) : QThread(parent), m_job(std::move(job)) { }

    void requestCancel() { m_cancel = true; }
    const JobResult& result() const { return m_result; }

   signals:
    void progress(const QString& step, double fraction);

   protected:
    void run() override
    {
        m_result = m_job(&m_cancel, [this](const QString& s, double f) { emit progress(s, f); });
    }

   private:
    Job m_job;
    std::atomic<bool> m_cancel{ false };
    JobResult m_result;
};

}  // namespace ZeroSetup
