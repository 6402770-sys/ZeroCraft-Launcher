// SPDX-License-Identifier: GPL-3.0-only
/*
 *  ElyPrismLauncher - Minecraft Launcher
 *  Copyright (c) 2025 Octol1ttle <l1ttleofficial@outlook.com>
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
 *  ZERO Launcher: ported from ElyPrismLauncher (GPL-3.0-only); adapted to ZERO (explicit configuration, optional client secret,
 *  verification_uri_complete) - see docs/zero/ARCHITECTURE.md, "Ely.by accounts".
 */

#include "ElyDeviceCodeStep.h"

ElyDeviceCodeStep::ElyDeviceCodeStep(AccountData* data, const Zero::Auth::Ely::Config& config)
    : MSADeviceCodeStep(data, config.clientId), m_configured(config.isConfigured())
{
    m_clientSecret = config.clientSecret;
    m_scopes = Zero::Auth::Ely::scopes();
    m_deviceCodeUrl = Zero::Auth::Ely::deviceCodeUrl();
    m_tokenUrl = Zero::Auth::Ely::tokenUrl();
    m_preferCompleteUri = true;  // the code comes prefilled when the user opens verification_uri_complete
    m_gotTokenMessage = tr("Got Ely.by token");
}

QString ElyDeviceCodeStep::describe()
{
    return tr("Logging in with Ely.by account (device code).");
}

void ElyDeviceCodeStep::perform()
{
    if (!m_configured) {
        emit finished(AccountTaskState::STATE_FAILED_HARD, tr("Ely.by sign-in is not available in this build."));
        return;
    }
    MSADeviceCodeStep::perform();
}
