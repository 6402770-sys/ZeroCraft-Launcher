// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Ely.by account sign-in: the parts that need no widgets, no APPLICATION and no network
// (docs/zero/ARCHITECTURE.md, "Ely.by accounts").
//
//  - `Config`: the OAuth client of ZERO's Ely.by application (client ID, optional secret). An empty ID means "not configured"; every
//    Ely.by entry point is hidden then.
//  - the endpoints and scopes of Ely's OAuth device flow,
//  - the form bodies of the token requests (`client_secret` is sent only when a secret is configured and is never logged),
//  - `parseTokenResponse`: the answer of the token endpoint, including a refresh answer that omits `refresh_token`.
//
// The sign-in steps (minecraft/auth/steps/Ely*.cpp) and the tests share these functions.
#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QVariantMap>

namespace Zero::Auth::Ely {

struct Config {
    QString clientId;
    QString clientSecret;  ///< optional; sent only when not empty

    bool isConfigured() const { return !clientId.isEmpty(); }
};

/// The configuration compiled into this build (BuildConfig.ELY_CLIENT_ID / ELY_CLIENT_SECRET).
Config configFromBuild();

/// "account_info offline_access minecraft_server_session"
QString scopes();
QUrl deviceCodeUrl();  ///< https://account.ely.by/api/oauth2/v1/devicecode
QUrl tokenUrl();       ///< https://account.ely.by/api/oauth2/v1/token
QUrl profileUrl();     ///< https://account.ely.by/api/mojang/services/minecraft/profile

/// application/x-www-form-urlencoded body of the device authorization request: client_id, scope (+ client_secret).
QByteArray deviceCodeForm(const Config& config);
/// Body of the polling request: client_id, grant_type=urn:ietf:params:oauth:grant-type:device_code, device_code (+ client_secret).
QByteArray deviceTokenForm(const Config& config, const QString& deviceCode);
/// Body of the refresh request: client_id, grant_type=refresh_token, refresh_token, scope (+ client_secret).
QByteArray refreshForm(const Config& config, const QString& refreshToken);

struct TokenResponse {
    bool ok = false;  ///< an access token came back and no error was reported
    QString accessToken;
    QString refreshToken;  ///< the old one when the answer did not carry a new one
    int expiresIn = 0;     ///< seconds; 0 = not reported
    QString error;
    QString errorDescription;
    QVariantMap extra;  ///< the rest of the answer, without the two tokens

    /// error_description when there is one, else error.
    QString errorText() const { return errorDescription.isEmpty() ? error : errorDescription; }
};

/// Parses the JSON answer of the token endpoint. `previousRefreshToken` is kept when the answer has no refresh_token (Ely's refresh
/// answer may omit it). Garbage or an empty body gives ok == false with an error text.
TokenResponse parseTokenResponse(const QByteArray& body, const QString& previousRefreshToken = QString());

}  // namespace Zero::Auth::Ely
