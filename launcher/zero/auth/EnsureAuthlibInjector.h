// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - launch step of Ely.by accounts: make sure the authlib-injector jar is there before the game starts, then hand its
// path to the session; LauncherPartLaunch turns it into `-javaagent:<jar>=ely.by`. Only added for sessions that ask for it
// (AuthSession::wantsAuthlibInjector), so Microsoft and offline launches never see it.
#pragma once

#include <QString>
#include <memory>

#include "launch/LaunchStep.h"
#include "minecraft/auth/AuthSession.h"
#include "zero/auth/AuthlibInjector.h"

class EnsureAuthlibInjector : public LaunchStep {
    Q_OBJECT
   public:
    EnsureAuthlibInjector(LaunchTask* parent, AuthSessionPtr session);
    ~EnsureAuthlibInjector() override = default;

    bool canAbort() const override { return true; }
    bool abort() override;

   protected:
    void executeTask() override;

   private:
    AuthSessionPtr m_session;
    std::shared_ptr<Zero::Auth::AuthlibInjector::AuthlibInjectorTask> m_task;
};
