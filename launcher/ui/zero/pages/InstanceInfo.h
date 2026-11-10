// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - display facts about an instance (Minecraft version, mod loader) for the Home and Library pages.
#pragma once

#include <QString>

class MinecraftInstance;

namespace Zero {

struct InstanceInfo {
    QString minecraftVersion;
    QString loader;  ///< "Fabric", "Forge", "NeoForge", "Quilt", "LiteLoader"; empty for vanilla
    QString loaderVersion;

    /// "1.20.1 · Fabric", or just "1.20.1" for vanilla. Empty when nothing is known.
    QString summary() const;

    static InstanceInfo of(MinecraftInstance* instance);
};

}  // namespace Zero
