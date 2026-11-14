// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - refreshes an Ely.by account: grant_type=refresh_token against Ely's OAuth token endpoint
// (docs/zero/ARCHITECTURE.md, "Ely.by accounts"). Signing in uses ElyDeviceCodeStep; there is no browser redirect flow.
#pragma once

#include <QObject>

#include "minecraft/auth/AuthStep.h"
#include "net/NetJob.h"
#include "net/Request.h"
#include "zero/auth/ElyAuth.h"

class ElyRefreshStep : public AuthStep {
    Q_OBJECT
   public:
    explicit ElyRefreshStep(AccountData* data, const Zero::Auth::Ely::Config& config = Zero::Auth::Ely::configFromBuild());
    virtual ~ElyRefreshStep() noexcept = default;

    void perform() override;

    QString describe() override;

    //! The request body (public for tests): client_id, grant_type=refresh_token, refresh_token, scope (+ client_secret when set).
    QByteArray requestPayload() const;

   public slots:
    void abort() override;

   private slots:
    void onRequestDone(QByteArray* response);

   private:
    Zero::Auth::Ely::Config m_config;
    Net::Request::Ptr m_request;
    NetJob::Ptr m_task;
};
