// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2024 Trial97 <alexandru.tripon97@gmail.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#pragma once
#include <QByteArray>
#include <QObject>
#include <QTimer>
#include <QUrl>

#include "minecraft/auth/AuthStep.h"
#include "net/NetJob.h"
#include "net/Request.h"

class MSADeviceCodeStep : public AuthStep {
    Q_OBJECT
   public:
    explicit MSADeviceCodeStep(AccountData* data);
    virtual ~MSADeviceCodeStep() noexcept = default;

    void perform() override;

    QString describe() override;

    // ZERO: the bodies of the two requests, public so that tests can check them without the network
    QByteArray deviceCodePayload() const;
    QByteArray tokenPayload() const;

   public slots:
    void abort() override;

   signals:
    void authorizeWithBrowser(QString url, QString code, int expiresIn);

   private slots:
    void deviceAuthorizationFinished(QByteArray* response);
    void startPoolTimer();
    void authenticateUser();
    void authenticationFinished(QByteArray* response);

   protected:
    // ZERO: lets the Ely.by step reuse this flow with its own client and endpoints (RFC 8628 is the same on both sides)
    MSADeviceCodeStep(AccountData* data, const QString& clientId);

    QString m_clientId;
    QString m_clientSecret;  // optional; sent only when not empty
    QString m_scopes = "XboxLive.SignIn XboxLive.offline_access";
    QUrl m_deviceCodeUrl{ "https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode" };
    QUrl m_tokenUrl{ "https://login.microsoftonline.com/consumers/oauth2/v2.0/token" };
    // show/open verification_uri_complete (the code comes prefilled) when the server sends it
    bool m_preferCompleteUri = false;
    QString m_gotTokenMessage;

   private:
    QString m_device_code;
    bool m_is_aborted = false;
    int interval = 5;

    QTimer m_pool_timer;
    QTimer m_expiration_timer;

    Net::Request::Ptr m_request;
    NetJob::Ptr m_task;
};
