// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - signs an Ely.by account in with email/username + password through Ely's authserver (POST /auth/authenticate;
// docs/zero/ARCHITECTURE.md, "Ely.by accounts"). The credentials are handed in memory by the Add account dialog, used for one request
// and cleared right after the request is built; the password is never stored, logged or put in a result message.
#pragma once

#include <QObject>
#include <QString>

#include "minecraft/auth/AuthStep.h"
#include "zero/auth/ElyAuthserver.h"
#include "zero/auth/ElyAuthserverNet.h"

class ElyPasswordStep : public AuthStep {
    Q_OBJECT
   public:
    explicit ElyPasswordStep(AccountData* data,
                             Zero::Auth::Ely::Authserver::Transport transport = Zero::Auth::Ely::Authserver::networkTransport());
    ~ElyPasswordStep() noexcept override;

    QString describe() override;

    /// The credentials of this attempt. `twoFactorCode` is empty on the first attempt; with a code the password is sent as `password:code`.
    void setCredentials(const QString& username, const QString& password, const QString& twoFactorCode = QString());

   public slots:
    void perform() override;
    void abort() override;

   signals:
    /// Why the sign-in ended (never emitted on success); sent right before `finished`.
    void failure(Zero::Auth::Ely::Authserver::Failure reason);

   private:
    void onReply(const Zero::Auth::Ely::Authserver::HttpReply& reply, bool sentCode);
    void wipeCredentials();
    void fail(Zero::Auth::Ely::Authserver::Failure reason, AccountTaskState state, const QString& message);

    Zero::Auth::Ely::Authserver::Transport m_transport;
    Zero::Auth::Ely::Authserver::Cancel m_cancel;
    QString m_username;
    QString m_password;
    QString m_code;
    bool m_done = false;
};
