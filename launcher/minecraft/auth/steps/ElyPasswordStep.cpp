// SPDX-License-Identifier: GPL-3.0-only
#include "ElyPasswordStep.h"

#include <QDateTime>
#include <QPointer>

using namespace Zero::Auth::Ely::Authserver;

namespace {
void wipe(QString& s)
{
    s.fill(QChar(0));  // best effort: overwrite before the buffer is released
    s.clear();
}
}  // namespace

ElyPasswordStep::ElyPasswordStep(AccountData* data, Transport transport) : AuthStep(data), m_transport(std::move(transport)) {}

ElyPasswordStep::~ElyPasswordStep() noexcept
{
    wipeCredentials();
}

QString ElyPasswordStep::describe()
{
    return tr("Signing in with Ely.by.");
}

void ElyPasswordStep::setCredentials(const QString& username, const QString& password, const QString& twoFactorCode)
{
    m_username = username.trimmed();
    m_password = password;
    m_code = twoFactorCode.trimmed();
}

void ElyPasswordStep::wipeCredentials()
{
    wipe(m_password);
    wipe(m_code);
}

void ElyPasswordStep::fail(Failure reason, AccountTaskState state, const QString& message)
{
    m_done = true;
    emit failure(reason);
    emit finished(state, message);
}

void ElyPasswordStep::perform()
{
    if (m_username.isEmpty() || m_password.isEmpty()) {
        wipeCredentials();
        fail(Failure::WrongCredentials, AccountTaskState::STATE_FAILED_HARD, tr("Enter your Ely.by email or username and your password."));
        return;
    }
    if (!m_transport) {
        wipeCredentials();
        fail(Failure::Network, AccountTaskState::STATE_OFFLINE, tr("Ely.by sign-in is not available."));
        return;
    }
    // The client token belongs to the account: made once, at its first sign-in.
    if (m_data->elyClientToken.isEmpty())
        m_data->elyClientToken = newClientToken();
    m_data->type = AccountType::Ely;
    m_data->elyAuth = ElyAuthKind::Password;

    const bool sentCode = !m_code.isEmpty();
    QByteArray body = authenticateBody(m_username, passwordWithCode(m_password, m_code), m_data->elyClientToken);
    wipeCredentials();  // the request exists now; the password is not needed any more
    QPointer<ElyPasswordStep> self(this);
    m_cancel = m_transport(url(QStringLiteral("authenticate")), body, [self, sentCode](HttpReply reply) {
        if (self)
            self->onReply(reply, sentCode);
    });
    body.fill('\0');
}

void ElyPasswordStep::abort()
{
    wipeCredentials();
    const bool wasDone = m_done;
    m_done = true;  // the cancelled request's answer is not an outcome
    if (m_cancel && !wasDone)
        m_cancel();
}

void ElyPasswordStep::onReply(const HttpReply& reply, bool sentCode)
{
    if (m_done)
        return;
    m_cancel = {};
    if (!reply.answered || reply.status >= 500 || reply.status == 429) {
        // no answer from Ely.by (offline, timeout, its servers are down): nothing is wrong with the account, we just could not ask
        m_data->networkError = reply.error;
        const QString why = reply.answered ? tr("Ely.by answered with HTTP %1.").arg(reply.status) : reply.errorString;
        fail(Failure::Network, AccountTaskState::STATE_OFFLINE, tr("Failed to reach Ely.by: %1").arg(why));
        return;
    }

    const Answer answer = parseAnswer(reply.status, reply.body);
    switch (answer.kind) {
        case Kind::TwoFactorRequired:
            fail(Failure::TwoFactorRequired, AccountTaskState::STATE_FAILED_SOFT, tr("This account is protected with two-factor authentication."));
            return;
        case Kind::WrongCredentials:
            // with a code in the password, "invalid credentials" most likely means the code was wrong
            if (sentCode)
                fail(Failure::WrongCode, AccountTaskState::STATE_FAILED_HARD, tr("That two-factor code didn't work."));
            else
                fail(Failure::WrongCredentials, AccountTaskState::STATE_FAILED_HARD, tr("Wrong email or password."));
            return;
        case Kind::Refused:
            fail(Failure::Refused, AccountTaskState::STATE_FAILED_HARD, answer.errorText());
            return;
        case Kind::Malformed:
            fail(Failure::Refused, AccountTaskState::STATE_FAILED_SOFT, tr("Ely.by sent an answer ZERO could not read."));
            return;
        case Kind::Ok:
            break;
    }
    if (answer.profileId.isEmpty() || answer.profileName.isEmpty()) {
        fail(Failure::Refused, AccountTaskState::STATE_FAILED_SOFT, tr("This Ely.by account has no Minecraft profile."));
        return;
    }

    Token ygg;
    ygg.token = answer.accessToken;
    ygg.issueInstant = QDateTime::currentDateTimeUtc();
    ygg.validity = Validity::Certain;
    m_data->yggdrasilToken = ygg;
    if (!answer.clientToken.isEmpty())
        m_data->elyClientToken = answer.clientToken;
    m_data->elyUsername = m_username;
    m_data->minecraftProfile = MinecraftProfile();
    m_data->minecraftProfile.id = answer.profileId;
    m_data->minecraftProfile.name = answer.profileName;
    m_data->minecraftProfile.validity = Validity::Certain;
    // an Ely.by account with a profile can play (Ely.by has no separate game licence to check)
    m_data->minecraftEntitlement.ownsMinecraft = true;
    m_data->minecraftEntitlement.canPlayMinecraft = true;
    m_data->minecraftEntitlement.validity = Validity::Certain;
    m_done = true;
    emit finished(AccountTaskState::STATE_WORKING, tr("Signed in with Ely.by"));
}
