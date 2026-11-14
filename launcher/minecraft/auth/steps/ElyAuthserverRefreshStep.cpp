// SPDX-License-Identifier: GPL-3.0-only
#include "ElyAuthserverRefreshStep.h"

#include <QDateTime>
#include <QPointer>

using namespace Zero::Auth::Ely::Authserver;

ElyAuthserverRefreshStep::ElyAuthserverRefreshStep(AccountData* data, Transport transport)
    : AuthStep(data), m_transport(std::move(transport))
{}

QString ElyAuthserverRefreshStep::describe()
{
    return tr("Refreshing the Ely.by account.");
}

QString ElyAuthserverRefreshStep::sessionEndedText()
{
    return tr("Your Ely.by session ended. Sign in again.");
}

void ElyAuthserverRefreshStep::perform()
{
    if (m_data->yggdrasilToken.token.isEmpty() || m_data->elyClientToken.isEmpty()) {
        m_done = true;
        emit finished(AccountTaskState::STATE_FAILED_HARD, sessionEndedText());
        return;
    }
    if (!m_transport) {
        m_done = true;
        emit finished(AccountTaskState::STATE_OFFLINE, tr("Ely.by sign-in is not available."));
        return;
    }
    m_cancel = m_transport(url(QStringLiteral("refresh")), refreshBody(m_data->yggdrasilToken.token, m_data->elyClientToken),
                           [self = QPointer<ElyAuthserverRefreshStep>(this)](HttpReply reply) {
                               if (self)
                                   self->onReply(reply);
                           });
}

void ElyAuthserverRefreshStep::abort()
{
    const bool wasDone = m_done;
    m_done = true;  // the cancelled request's answer is not an outcome
    if (m_cancel && !wasDone)
        m_cancel();
}

void ElyAuthserverRefreshStep::onReply(const HttpReply& reply)
{
    if (m_done)
        return;
    m_cancel = {};
    m_done = true;
    if (!reply.answered || reply.status >= 500 || reply.status == 429) {
        m_data->networkError = reply.error;
        const QString why = reply.answered ? tr("Ely.by answered with HTTP %1.").arg(reply.status) : reply.errorString;
        emit finished(AccountTaskState::STATE_OFFLINE, tr("Failed to refresh the Ely.by session: %1").arg(why));
        return;
    }
    const Answer answer = parseAnswer(reply.status, reply.body);
    if (!answer.ok()) {
        // Ely.by answered and said no (the token was revoked, the password changed ...): sign in again
        emit finished(AccountTaskState::STATE_FAILED_HARD, sessionEndedText());
        return;
    }

    Token ygg;
    ygg.token = answer.accessToken;
    ygg.issueInstant = QDateTime::currentDateTimeUtc();
    ygg.validity = Validity::Certain;
    m_data->yggdrasilToken = ygg;
    if (!answer.clientToken.isEmpty())
        m_data->elyClientToken = answer.clientToken;
    if (!answer.profileId.isEmpty() && !answer.profileName.isEmpty()) {
        m_data->minecraftProfile.id = answer.profileId;
        m_data->minecraftProfile.name = answer.profileName;
    }
    if (!m_data->minecraftProfile.id.isEmpty()) {
        m_data->minecraftProfile.validity = Validity::Certain;
        m_data->minecraftEntitlement.ownsMinecraft = true;
        m_data->minecraftEntitlement.canPlayMinecraft = true;
        m_data->minecraftEntitlement.validity = Validity::Certain;
    }
    emit finished(AccountTaskState::STATE_WORKING, tr("Refreshed the Ely.by session"));
}
