// SPDX-License-Identifier: GPL-3.0-only
#include "ElyAuthserver.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUuid>

namespace Zero::Auth::Ely::Authserver {

namespace {

QByteArray toJson(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QString trimmedString(const QJsonObject& object, const char* key)
{
    return object.value(QLatin1String(key)).toString().trimmed();
}

}  // namespace

QUrl url(const QString& endpoint)
{
    return QUrl(QStringLiteral("https://authserver.ely.by/auth/") + endpoint);
}

QString newClientToken()
{
    return QUuid::createUuid().toString(QUuid::Id128);
}

QString passwordWithCode(const QString& password, const QString& code)
{
    return code.isEmpty() ? password : password + QLatin1Char(':') + code;
}

QByteArray authenticateBody(const QString& username, const QString& password, const QString& clientToken)
{
    return toJson(QJsonObject{ { "username", username },
                               { "password", password },
                               { "clientToken", clientToken },
                               { "requestUser", true } });
}

QByteArray refreshBody(const QString& accessToken, const QString& clientToken)
{
    return toJson(QJsonObject{ { "accessToken", accessToken }, { "clientToken", clientToken }, { "requestUser", true } });
}

QByteArray invalidateBody(const QString& accessToken, const QString& clientToken)
{
    return toJson(QJsonObject{ { "accessToken", accessToken }, { "clientToken", clientToken } });
}

bool isTwoFactorMessage(const QString& errorMessage)
{
    const QString m = errorMessage.toLower();
    return m.contains(QLatin1String("two factor")) || m.contains(QLatin1String("two-factor")) || m.contains(QLatin1String("2fa")) ||
           m.contains(QLatin1String("two step")) || m.contains(QLatin1String("two-step"));
}

bool isInvalidCredentialsMessage(const QString& errorMessage)
{
    const QString m = errorMessage.toLower();
    return m.contains(QLatin1String("invalid credentials")) || m.contains(QLatin1String("invalid username or password"));
}

QString normalizeProfileId(const QString& id)
{
    QString compact = id.trimmed();
    compact.remove(QLatin1Char('-')).remove(QLatin1Char('{')).remove(QLatin1Char('}'));
    static const QRegularExpression hex32(QStringLiteral("\\A[0-9a-fA-F]{32}\\z"));
    if (hex32.match(compact).hasMatch())
        return compact.toLower();
    return id.trimmed();
}

Answer parseAnswer(int status, const QByteArray& body)
{
    Answer out;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    const bool isObject = parseError.error == QJsonParseError::NoError && doc.isObject();
    const QJsonObject root = isObject ? doc.object() : QJsonObject();

    if (status != 200) {
        out.error = trimmedString(root, "error");
        out.errorMessage = trimmedString(root, "errorMessage");
        if (out.error.isEmpty() && out.errorMessage.isEmpty())
            out.error = QStringLiteral("HTTP %1").arg(status);
        if (isTwoFactorMessage(out.errorMessage))
            out.kind = Kind::TwoFactorRequired;
        else if (isInvalidCredentialsMessage(out.errorMessage))
            out.kind = Kind::WrongCredentials;
        else
            out.kind = Kind::Refused;
        return out;
    }

    if (!isObject) {
        out.error = QStringLiteral("unreadable answer");
        return out;  // Malformed
    }
    // an answer with an error and status 200 would be odd, but treat it as the refusal it says it is
    if (root.contains(QLatin1String("error"))) {
        out.error = trimmedString(root, "error");
        out.errorMessage = trimmedString(root, "errorMessage");
        out.kind = isTwoFactorMessage(out.errorMessage) ? Kind::TwoFactorRequired
                   : isInvalidCredentialsMessage(out.errorMessage) ? Kind::WrongCredentials
                                                                    : Kind::Refused;
        return out;
    }

    out.accessToken = trimmedString(root, "accessToken");
    out.clientToken = trimmedString(root, "clientToken");
    QJsonObject profile = root.value(QLatin1String("selectedProfile")).toObject();
    if (profile.isEmpty()) {
        const QJsonArray available = root.value(QLatin1String("availableProfiles")).toArray();
        if (available.size() == 1)
            profile = available.first().toObject();
    }
    out.profileId = normalizeProfileId(trimmedString(profile, "id"));
    out.profileName = trimmedString(profile, "name");
    if (out.accessToken.isEmpty()) {
        out.error = QStringLiteral("no access token");
        return out;  // Malformed
    }
    out.kind = Kind::Ok;  // a refresh answer may carry no profile: the caller decides whether it needs one
    return out;
}

}  // namespace Zero::Auth::Ely::Authserver
