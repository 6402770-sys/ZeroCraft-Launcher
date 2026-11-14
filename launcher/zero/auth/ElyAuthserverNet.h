// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the real transport of the Ely.by authserver steps (Prism's Net::Request / NetJob through APPLICATION->network()),
// and the best-effort /auth/invalidate that runs when an Ely.by account is removed. Needs the running Application; the steps and the
// tests use the `Transport` seam of ElyAuthserver.h instead.
#pragma once

#include <QString>

#include "ElyAuthserver.h"

namespace Zero::Auth::Ely::Authserver {

/// POSTs JSON through `APPLICATION->network()`. An answer with any HTTP status is `answered`; the body of an error answer is kept.
/// No automatic retry, nothing is logged by this function (and the body, which may carry a password, is never copied anywhere).
Transport networkTransport();

/// Tells Ely.by the token is no longer wanted. Best effort: failures are ignored, nothing blocks, nothing is shown. Does nothing when
/// no Application is running (unit tests) or when `accessToken` is empty.
void invalidateInBackground(const QString& accessToken, const QString& clientToken, Transport transport = {});

}  // namespace Zero::Auth::Ely::Authserver
