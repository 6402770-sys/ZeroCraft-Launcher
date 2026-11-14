// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - refreshes an Ely.by account that signed in with email/username + password: POST /auth/refresh with the stored
// access token and client token (docs/zero/ARCHITECTURE.md, "Ely.by accounts"). Ely.by says no = the session ended, sign in again
// (STATE_FAILED_HARD); no answer = STATE_OFFLINE (the account is fine, we could not check).
#pragma once

#include <QObject>

#include "minecraft/auth/AuthStep.h"
#include "zero/auth/ElyAuthserver.h"
#include "zero/auth/ElyAuthserverNet.h"

class ElyAuthserverRefreshStep : public AuthStep {
    Q_OBJECT
   public:
    explicit ElyAuthserverRefreshStep(AccountData* data,
                                      Zero::Auth::Ely::Authserver::Transport transport = Zero::Auth::Ely::Authserver::networkTransport());

    QString describe() override;
    static QString sessionEndedText();

   public slots:
    void perform() override;
    void abort() override;

   private:
    void onReply(const Zero::Auth::Ely::Authserver::HttpReply& reply);

    Zero::Auth::Ely::Authserver::Transport m_transport;
    Zero::Auth::Ely::Authserver::Cancel m_cancel;
    bool m_done = false;
};
