// SPDX-License-Identifier: GPL-3.0-only
#include "ElyAuth.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>

#include "BuildConfig.h"

namespace Zero::Auth::Ely {

Config configFromBuild()
{
    return Config{ BuildConfig.ELY_CLIENT_ID, BuildConfig.ELY_CLIENT_SECRET };
}

QString scopes()
{
    return QStringLiteral("account_info offline_access minecraft_server_session");
}

QUrl deviceCodeUrl()
{
    return QUrl(QStringLiteral("https://account.ely.by/api/oauth2/v1/devicecode"));
}

QUrl tokenUrl()
{
    return QUrl(QStringLiteral("https://account.ely.by/api/oauth2/v1/token"));
}

QUrl profileUrl()
{
    return QUrl(QStringLiteral("https://account.ely.by/api/mojang/services/minecraft/profile"));
}

namespace {
QByteArray encode(QUrlQuery& query, const Config& config)
{
    if (!config.clientSecret.isEmpty()) {
        query.addQueryItem("client_secret", config.clientSecret);
    }
    return query.query(QUrl::FullyEncoded).toUtf8();
}
}  // namespace

QByteArray deviceCodeForm(const Config& config)
{
    QUrlQuery data;
    data.addQueryItem("client_id", config.clientId);
    data.addQueryItem("scope", scopes());
    return encode(data, config);
}

QByteArray deviceTokenForm(const Config& config, const QString& deviceCode)
{
    QUrlQuery data;
    data.addQueryItem("client_id", config.clientId);
    data.addQueryItem("grant_type", "urn:ietf:params:oauth:grant-type:device_code");
    data.addQueryItem("device_code", deviceCode);
    return encode(data, config);
}

QByteArray refreshForm(const Config& config, const QString& refreshToken)
{
    QUrlQuery data;
    data.addQueryItem("client_id", config.clientId);
    data.addQueryItem("grant_type", "refresh_token");
    data.addQueryItem("refresh_token", refreshToken);
    data.addQueryItem("scope", scopes());
    return encode(data, config);
}

TokenResponse parseTokenResponse(const QByteArray& body, const QString& previousRefreshToken)
{
    TokenResponse out;
    out.refreshToken = previousRefreshToken;

    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        out.error = QStringLiteral("invalid_response");
        out.errorDescription = QStringLiteral("The server's answer could not be read.");
        return out;
    }
    const auto obj = doc.object();
    out.error = obj.value("error").toString();
    out.errorDescription = obj.value("error_description").toString();
    out.accessToken = obj.value("access_token").toString();
    const auto newRefresh = obj.value("refresh_token").toString();
    if (!newRefresh.isEmpty()) {
        out.refreshToken = newRefresh;
    }
    out.expiresIn = obj.value("expires_in").toInt();

    auto extra = obj;
    extra.remove("access_token");
    extra.remove("refresh_token");
    out.extra = extra.toVariantMap();

    if (out.error.isEmpty() && out.errorDescription.isEmpty() && out.accessToken.isEmpty()) {
        out.error = QStringLiteral("invalid_response");
        out.errorDescription = QStringLiteral("The server's answer has no access token.");
    }
    out.ok = out.error.isEmpty() && out.errorDescription.isEmpty() && !out.accessToken.isEmpty();
    return out;
}

}  // namespace Zero::Auth::Ely
