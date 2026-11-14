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

#include "ElyYggdrasilTokenStep.h"

ElyYggdrasilTokenStep::ElyYggdrasilTokenStep(AccountData* data) : AuthStep(data) {}

QString ElyYggdrasilTokenStep::describe()
{
    return tr("Updating Yggdrasil token");
}

void ElyYggdrasilTokenStep::perform()
{
    // only the access token and its lifetime: the refresh token stays in msaToken
    Token ygg;
    ygg.token = m_data->msaToken.token;
    ygg.issueInstant = m_data->msaToken.issueInstant;
    ygg.notAfter = m_data->msaToken.notAfter;
    ygg.validity = Validity::Certain;
    m_data->yggdrasilToken = ygg;
    emit finished(AccountTaskState::STATE_WORKING, tr("Yggdrasil token update succeeded"));
}
