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

#pragma once

#include "minecraft/auth/AuthStep.h"

//! The game session token of an Ely.by account is its OAuth access token (authlib-injector hands it to Ely.by's session server).
class ElyYggdrasilTokenStep : public AuthStep {
    Q_OBJECT

   public:
    explicit ElyYggdrasilTokenStep(AccountData* data);
    virtual ~ElyYggdrasilTokenStep() noexcept = default;

    void perform() override;

    QString describe() override;
};
