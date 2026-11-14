// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - dev aid for ZERO_SCREENSHOT_ADD_ACCOUNT (docs/zero/SHELL.md): shows the "Add account" dialog in one state,
// driven by scripted flows with a fake code and fake links (no network, no browser), saves the dialog to a PNG and quits.
#pragma once

#include <QString>

class QWidget;

namespace Zero {
namespace Accounts {

/// `state`: choose | microsoft | ely | offline | error | success | ely-password | ely-2fa | ely-wrong. Returns false (and shows nothing) for an unknown state.
bool screenshotAddAccount(QWidget* parent, const QString& state, const QString& path);

}  // namespace Accounts
}  // namespace Zero
