// SPDX-License-Identifier: GPL-3.0-only
#include "ElyRefreshStep.h"

#include <QDateTime>

#include "Application.h"
#include "net/NetUtils.h"
#include "net/RawHeaderProxy.h"

ElyRefreshStep::ElyRefreshStep(AccountData* data, const Zero::Auth::Ely::Config& config) : AuthStep(data), m_config(config) {}

QString ElyRefreshStep::describe()
{
    return tr("Refreshing the Ely.by account.");
}

QByteArray ElyRefreshStep::requestPayload() const
{
    return Zero::Auth::Ely::refreshForm(m_config, m_data->msaToken.refresh_token);
}

void ElyRefreshStep::perform()
{
    if (!m_config.isConfigured()) {
        emit finished(AccountTaskState::STATE_DISABLED, tr("Ely.by sign-in is not available in this build."));
        return;
    }
    if (m_data->msaClientID != m_config.clientId) {
        emit finished(AccountTaskState::STATE_DISABLED, tr("Ely.by authentication failed - client identification has changed."));
        return;
    }
    if (m_data->msaToken.refresh_token.isEmpty()) {
        emit finished(AccountTaskState::STATE_DISABLED, tr("Ely.by authentication failed - refresh token is empty."));
        return;
    }

    auto headers = QList<Net::HeaderPair>{
        { "Content-Type", "application/x-www-form-urlencoded" },
        { "Accept", "application/json" },
    };
    auto [request, response] = Net::Request::makeByteArray(Zero::Auth::Ely::tokenUrl(), requestPayload());
    m_request = request;
    m_request->addHeaderProxy(std::make_unique<Net::RawHeaderProxy>(headers));
    m_request->enableAutoRetry(true);

    m_task.reset(new NetJob("ElyRefreshStep", APPLICATION->network()));
    m_task->setAskRetry(false);
    m_task->addNetAction(m_request);

    connect(m_task.get(), &Task::finished, this, [this, response] { onRequestDone(response); });

    m_task->start();
}

void ElyRefreshStep::abort()
{
    if (m_task) {
        m_task->abort();
    }
}

void ElyRefreshStep::onRequestDone(QByteArray* response)
{
    const bool transportFailed = !m_request->wasSuccessful() || m_request->error() != QNetworkReply::NoError;
    if (transportFailed && (response->isEmpty() || Net::isServerError(m_request->error()) || !Net::isApplicationError(m_request->error()))) {
        // no answer from Ely.by (offline, timeout, its servers are down): the account is fine, we just could not check
        qWarning() << "Ely.by token refresh failed:" << m_request->error() << m_request->errorString();
        m_data->networkError = m_request->error();
        emit finished(AccountTaskState::STATE_OFFLINE, tr("Failed to refresh token: %1").arg(m_request->errorString()));
        return;
    }

    const auto rsp = Zero::Auth::Ely::parseTokenResponse(*response, m_data->msaToken.refresh_token);
    if (!rsp.ok) {
        // Ely.by answered and said no (invalid_grant: the refresh token was revoked or expired) - sign in again
        qWarning() << "Ely.by token refresh was refused:" << rsp.error;
        emit finished(AccountTaskState::STATE_FAILED_HARD, tr("Ely.by session expired: %1").arg(rsp.errorText()));
        return;
    }

    m_data->msaToken.issueInstant = QDateTime::currentDateTimeUtc();
    m_data->msaToken.notAfter = rsp.expiresIn > 0 ? QDateTime::currentDateTime().addSecs(rsp.expiresIn) : QDateTime();
    m_data->msaToken.extra = rsp.extra;
    m_data->msaToken.refresh_token = rsp.refreshToken;  // the old one when the answer carried none
    m_data->msaToken.token = rsp.accessToken;
    emit finished(AccountTaskState::STATE_WORKING, tr("Got Ely.by token"));
}
