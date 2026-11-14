// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Ely.by sign-in with email/username + password through Ely's Mojang-compatible authserver
// (docs/zero/ARCHITECTURE.md, "Ely.by accounts"). Needs no registered application.
//
// Pure parts only (no widgets, no APPLICATION, no network): the endpoints, the JSON bodies, the parsers and the transport seam
// the steps talk through. A body that carries a password is built here and nowhere else; nothing in this file logs.
//
//   POST https://authserver.ely.by/auth/authenticate  {username, password, clientToken, requestUser}
//   POST https://authserver.ely.by/auth/refresh       {accessToken, clientToken, requestUser}
//   POST https://authserver.ely.by/auth/invalidate    {accessToken, clientToken}
//
// Two-factor accounts: the first attempt carries the plain password; a 401 whose message says the account is protected with two-factor
// authentication means "ask the code", and the second attempt sends `password:code`.
#pragma once

#include <QByteArray>
#include <QNetworkReply>
#include <QString>
#include <QUrl>
#include <functional>

namespace Zero::Auth::Ely::Authserver {

/// https://authserver.ely.by/auth/<endpoint>
QUrl url(const QString& endpoint);

/// A new client token (a random UUID without dashes), made once per account at its first sign-in.
QString newClientToken();

/// `password` or, for a two-factor account, `password:code`.
QString passwordWithCode(const QString& password, const QString& code);

QByteArray authenticateBody(const QString& username, const QString& password, const QString& clientToken);
QByteArray refreshBody(const QString& accessToken, const QString& clientToken);
QByteArray invalidateBody(const QString& accessToken, const QString& clientToken);

/// What an authserver answer means.
enum class Kind {
    Ok,                 ///< a token and a profile came back
    TwoFactorRequired,  ///< 401: the account is protected with two-factor authentication; ask the code and retry
    WrongCredentials,   ///< 401 "Invalid credentials": a wrong password / username (or, after a code was sent, a wrong code)
    Refused,            ///< any other answer with an error message (shown as Ely.by gave it)
    Malformed,          ///< no usable JSON, or no token / profile in a success answer
};

struct Answer {
    Kind kind = Kind::Malformed;
    QString accessToken;
    QString clientToken;
    QString profileId;  ///< without dashes, as Prism keeps profile ids
    QString profileName;
    QString error;         ///< the `error` field of an error answer
    QString errorMessage;  ///< the `errorMessage` field of an error answer (empty when there was none)

    bool ok() const { return kind == Kind::Ok; }
    /// errorMessage when there is one, else error.
    QString errorText() const { return errorMessage.isEmpty() ? error : errorMessage; }
};

/// Why a password sign-in ended, in the terms the Add account dialog needs (it picks its text from this, not from the message).
enum class Failure {
    None,
    TwoFactorRequired,  ///< ask for the code and sign in again
    WrongCredentials,   ///< wrong email / username or password
    WrongCode,          ///< the two-factor code was refused
    Network,            ///< no answer from Ely.by (offline, timeout, server error)
    Refused,            ///< Ely.by said no with a message of its own (shown as given)
};

/// True for the message of an account protected with two-factor authentication ("Account protected with two factor auth.").
bool isTwoFactorMessage(const QString& errorMessage);
/// True for "Invalid credentials. Invalid username or password." (and its refresh-time cousins about the token).
bool isInvalidCredentialsMessage(const QString& errorMessage);

/// The answer of /auth/authenticate (and /auth/refresh): `status` is the HTTP status, `body` the JSON. The profile is `selectedProfile`;
/// without one the only entry of `availableProfiles` is used. Ok with empty profile fields is possible (a refresh answer may carry
/// none); the caller decides whether it needs a profile. No access token in a 200 answer is Malformed.
Answer parseAnswer(int status, const QByteArray& body);

/// `id` with dashes / braces removed and lower-cased when it is a UUID (dashed or not), else unchanged.
QString normalizeProfileId(const QString& id);

// -------------------------------------------------------------------------------------------------------------------
// Transport seam: the steps never touch the network themselves.
// -------------------------------------------------------------------------------------------------------------------

struct HttpReply {
    bool answered = false;  ///< an HTTP answer arrived (any status); false = no answer (offline, timeout, DNS, TLS ...)
    int status = 0;
    QByteArray body;
    QString errorString;  ///< why there was no answer
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
};

/// Cancels a request that is still running (its `Done` is then called with answered == false). May be empty.
using Cancel = std::function<void()>;
using Done = std::function<void(HttpReply)>;
/// POSTs `body` (JSON) to `url` and calls `done` exactly once; returns what cancels it.
using Transport = std::function<Cancel(const QUrl& url, const QByteArray& body, Done done)>;

}  // namespace Zero::Auth::Ely::Authserver
