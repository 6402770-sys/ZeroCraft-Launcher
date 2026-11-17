#pragma once

#include <QFuture>
#include <QFutureWatcher>
#include <QUrl>
#include "BaseInstance.h"
#include "BaseVersion.h"
#include "Filter.h"
#include "InstanceCopyPrefs.h"
#include "InstanceTask.h"
#include "net/NetJob.h"
#include "settings/SettingsObject.h"
#include "tasks/Task.h"

class InstanceCopyTask : public InstanceTask {
    Q_OBJECT
   public:
    explicit InstanceCopyTask(BaseInstance* origInstance, const InstanceCopyPrefs& prefs);

    // ZERO: "Try it in a copy": the copy gets ZeroSandboxOf=<id> in its instance.cfg (empty = a normal copy: the key is cleared, so copying a
    // test copy by hand gives an ordinary instance).
    void setZeroSandboxOf(const QString& originalId) { m_zeroSandboxOf = originalId; }

   protected:
    //! Entry point for tasks.
    virtual void executeTask() override;
    bool abort() override;
    void copyFinished();
    void copyAborted();

   private:
    /* data */
    BaseInstance* m_origInstance;
    QFuture<bool> m_copyFuture;
    QFutureWatcher<bool> m_copyFutureWatcher;
    Filter m_matcher;
    bool m_keepPlaytime;
    bool m_useLinks = false;
    bool m_useHardLinks = false;
    bool m_copySaves = false;
    bool m_linkRecursively = false;
    bool m_useClone = false;
    QString m_zeroSandboxOf;  // ZERO
};
