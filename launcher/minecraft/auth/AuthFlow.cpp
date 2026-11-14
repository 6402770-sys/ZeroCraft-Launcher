#include <QDebug>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "minecraft/auth/AccountData.h"
#include "minecraft/auth/steps/ElyAuthserverRefreshStep.h"
#include "minecraft/auth/steps/ElyDeviceCodeStep.h"
#include "minecraft/auth/steps/ElyPasswordStep.h"
#include "minecraft/auth/steps/ElyRefreshStep.h"
#include "minecraft/auth/steps/ElyYggdrasilTokenStep.h"
#include "minecraft/auth/steps/EntitlementsStep.h"
#include "minecraft/auth/steps/GetSkinStep.h"
#include "minecraft/auth/steps/LauncherLoginStep.h"
#include "minecraft/auth/steps/MSADeviceCodeStep.h"
#include "minecraft/auth/steps/MSAStep.h"
#include "minecraft/auth/steps/MinecraftProfileStep.h"
#include "minecraft/auth/steps/MinecraftProfileStepEly.h"
#include "minecraft/auth/steps/XboxAuthorizationStep.h"
#include "minecraft/auth/steps/XboxUserStep.h"
#include "tasks/Task.h"

#include "AuthFlow.h"

#include <Application.h>

AuthFlow::AuthFlow(AccountData* data, Action action, Zero::Auth::Ely::Authserver::Transport elyTransport) : Task(), m_data(data)
{
    if (data->type == AccountType::MSA) {
        if (action == Action::DeviceCode) {
            auto oauthStep = makeShared<MSADeviceCodeStep>(m_data);
            connect(oauthStep.get(), &MSADeviceCodeStep::authorizeWithBrowser, this, &AuthFlow::authorizeWithBrowserWithExtra);
            m_steps.append(oauthStep);
        } else {
            auto oauthStep = makeShared<MSAStep>(m_data, action == Action::Refresh);
            connect(oauthStep.get(), &MSAStep::authorizeWithBrowser, this, &AuthFlow::authorizeWithBrowser);
            m_steps.append(oauthStep);
        }
        m_steps.append(makeShared<XboxUserStep>(m_data));
        m_steps.append(
            makeShared<XboxAuthorizationStep>(m_data, &m_data->mojangservicesToken, "rp://api.minecraftservices.com/", "Mojang"));
        m_steps.append(makeShared<LauncherLoginStep>(m_data));
        m_steps.append(makeShared<EntitlementsStep>(m_data));
        m_steps.append(makeShared<MinecraftProfileStep>(m_data));
        m_steps.append(makeShared<GetSkinStep>(m_data));
    } else if (data->type == AccountType::Ely && data->elyAuth == ElyAuthKind::Password) {
        // ZERO: Ely.by account that signs in with email/username + password through Ely's authserver (no OAuth, no client ID).
        // Login = authenticate (it also sets the game token and the profile), Refresh = refresh. No skin step: skins live on ely.by.
        using namespace Zero::Auth::Ely::Authserver;
        if (action == Action::Refresh) {
            m_steps.append(elyTransport ? makeShared<ElyAuthserverRefreshStep>(m_data, std::move(elyTransport))
                                        : makeShared<ElyAuthserverRefreshStep>(m_data));
        } else {
            auto passwordStep = elyTransport ? makeShared<ElyPasswordStep>(m_data, std::move(elyTransport)) : makeShared<ElyPasswordStep>(m_data);
            m_passwordStep = passwordStep.get();
            connect(passwordStep.get(), &ElyPasswordStep::failure, this, [this](Failure failure) { m_elyFailure = failure; });
            m_steps.append(passwordStep);
        }
    } else if (data->type == AccountType::Ely) {
        // ZERO: Ely.by accounts. Signing in is the device code flow only (Ely.by documents no redirect flow for desktop apps), so
        // Login and DeviceCode build the same steps; Refresh uses the refresh token. No skin step: skins live on ely.by.
        if (action == Action::Refresh) {
            m_steps.append(makeShared<ElyRefreshStep>(m_data));
        } else {
            auto oauthStep = makeShared<ElyDeviceCodeStep>(m_data);
            connect(oauthStep.get(), &ElyDeviceCodeStep::authorizeWithBrowser, this, &AuthFlow::authorizeWithBrowserWithExtra);
            m_steps.append(oauthStep);
        }
        m_steps.append(makeShared<ElyYggdrasilTokenStep>(m_data));
        m_steps.append(makeShared<MinecraftProfileStepEly>(m_data));
    }
    changeState(AccountTaskState::STATE_CREATED);
}

QStringList AuthFlow::stepNames() const
{
    QStringList names;
    for (const auto& step : m_steps) {
        names << QString::fromLatin1(step->metaObject()->className());
    }
    return names;
}

void AuthFlow::setElyCredentials(const QString& username, const QString& password, const QString& twoFactorCode)
{
    if (m_passwordStep)
        m_passwordStep->setCredentials(username, password, twoFactorCode);
}

void AuthFlow::succeed()
{
    m_data->validity_ = Validity::Certain;
    changeState(AccountTaskState::STATE_SUCCEEDED, tr("Finished all authentication steps"));
}

void AuthFlow::executeTask()
{
    changeState(AccountTaskState::STATE_WORKING, tr("Initializing"));
    nextStep();
}

void AuthFlow::nextStep()
{
    if (!Task::isRunning()) {
        return;
    }
    if (m_steps.size() == 0) {
        // we got to the end without an incident... assume this is all.
        m_currentStep.reset();
        succeed();
        return;
    }
    m_currentStep = m_steps.front();
    qDebug() << "AuthFlow:" << m_currentStep->describe();
    setStatus(m_currentStep->describe());
    m_steps.pop_front();
    connect(m_currentStep.get(), &AuthStep::finished, this, &AuthFlow::stepFinished);

    m_currentStep->perform();
}

void AuthFlow::stepFinished(AccountTaskState resultingState, QString message)
{
    if (changeState(resultingState, message))
        nextStep();
}

bool AuthFlow::changeState(AccountTaskState newState, QString reason)
{
    m_taskState = newState;
    setDetails(reason);
    switch (newState) {
        case AccountTaskState::STATE_CREATED: {
            setStatus(tr("Waiting..."));
            m_data->errorString.clear();
            return true;
        }
        case AccountTaskState::STATE_WORKING: {
            if (!m_currentStep) {
                setStatus(tr("Preparing to log in..."));
            }
            m_data->accountState = AccountState::Working;
            return true;
        }
        case AccountTaskState::STATE_SUCCEEDED: {
            setStatus(tr("Authentication task succeeded."));
            m_data->accountState = AccountState::Online;
            emitSucceeded();
            return false;
        }
        case AccountTaskState::STATE_OFFLINE: {
            setStatus(tr("Failed to contact the authentication server."));
            m_data->errorString = reason;
            m_data->accountState = AccountState::Offline;
            emitFailed(reason);
            return false;
        }
        case AccountTaskState::STATE_DISABLED: {
            setStatus(tr("Client ID has changed. New session needs to be created."));
            m_data->errorString = reason;
            m_data->accountState = AccountState::Disabled;
            emitFailed(reason);
            return false;
        }
        case AccountTaskState::STATE_FAILED_SOFT: {
            setStatus(tr("Encountered an error during authentication."));
            m_data->errorString = reason;
            m_data->accountState = AccountState::Errored;
            emitFailed(reason);
            return false;
        }
        case AccountTaskState::STATE_FAILED_HARD: {
            setStatus(tr("Failed to authenticate. The session has expired."));
            m_data->errorString = reason;
            m_data->accountState = AccountState::Expired;
            emitFailed(reason);
            return false;
        }
        case AccountTaskState::STATE_FAILED_GONE: {
            setStatus(tr("Failed to authenticate. The account no longer exists."));
            m_data->errorString = reason;
            m_data->accountState = AccountState::Gone;
            emitFailed(reason);
            return false;
        }
        default: {
            setStatus(tr("..."));
            QString error = tr("Unknown account task state: %1").arg(int(newState));
            m_data->accountState = AccountState::Errored;
            emitFailed(error);
            return false;
        }
    }
}
bool AuthFlow::abort()
{
    if (m_currentStep)
        m_currentStep->abort();
    emitAborted();
    return true;
}
