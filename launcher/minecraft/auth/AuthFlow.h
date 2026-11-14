#pragma once

#include <QImage>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>

#include "minecraft/auth/AccountData.h"
#include "minecraft/auth/AuthStep.h"
#include "tasks/Task.h"
#include "zero/auth/ElyAuthserver.h"

class ElyPasswordStep;

class AuthFlow : public Task {
    Q_OBJECT

   public:
    enum class Action { Refresh, Login, DeviceCode };

    //! `elyTransport`: the network of the Ely.by authserver steps (password accounts); empty = the real one (tests pass a fake).
    explicit AuthFlow(AccountData* data, Action action = Action::Refresh, Zero::Auth::Ely::Authserver::Transport elyTransport = {});
    virtual ~AuthFlow() = default;

    void executeTask() override;

    AccountTaskState taskState() { return m_taskState; }

    //! ZERO: class names of the steps still to run, in order (tests and diagnostics)
    QStringList stepNames() const;

    //! ZERO: Ely.by password sign-in. Hands the credentials (memory only) to the sign-in step before the flow starts; `twoFactorCode`
    //! is empty on the first attempt. Does nothing for flows without such a step. The step clears the password once its request exists.
    virtual void setElyCredentials(const QString& username, const QString& password, const QString& twoFactorCode = QString());
    //! ZERO: why an Ely.by password sign-in failed (None while running, after success, and for other kinds of flows)
    Zero::Auth::Ely::Authserver::Failure elyFailure() const { return m_elyFailure; }

   public slots:
    bool abort() override;

   signals:
    void authorizeWithBrowser(const QUrl& url);
    void authorizeWithBrowserWithExtra(QString url, QString code, int expiresIn);

   protected:
    void setElyFailure(Zero::Auth::Ely::Authserver::Failure failure) { m_elyFailure = failure; }
    void succeed();
    void nextStep();

   private slots:
    // NOTE: true -> non-terminal state, false -> terminal state
    bool changeState(AccountTaskState newState, QString reason = QString());
    void stepFinished(AccountTaskState resultingState, QString message);

   private:
    AccountTaskState m_taskState = AccountTaskState::STATE_CREATED;
    QList<AuthStep::Ptr> m_steps;
    AuthStep::Ptr m_currentStep;
    AccountData* m_data = nullptr;
    QPointer<ElyPasswordStep> m_passwordStep;
    Zero::Auth::Ely::Authserver::Failure m_elyFailure = Zero::Auth::Ely::Authserver::Failure::None;
};
