// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - what Discover > Shaders needs to know about the target's shader mod (docs/zero/SHELL.md, "Discover > Shaders").
//
// Shaders do nothing without a shader mod in the instance (Iris, Oculus or OptiFine). The decision is made from real data only: the
// file names in `<game root>/mods` (a `.disabled` file does not count). Pure functions: the section reads the folder, these decide.
#pragma once

#include <QString>
#include <QStringList>

#include "modplatform/ModIndex.h"

namespace Zero::Content {

struct ShaderModFacts {
    bool hasShaderMod = false;  ///< a file name contains "iris", "oculus" or "optifine" (case-insensitive), not ending in ".disabled"
    /// The Platform facet values the shader mod runs ("iris" for Iris and Oculus, "optifine" for OptiFine), in that order.
    QStringList platforms;

    bool operator==(const ShaderModFacts&) const = default;
};

/// The facts of a mods folder given as file names (no directories).
ShaderModFacts shaderModFacts(const QStringList& modFileNames);
/// The file names (files only) of the mods folder `modsDir`; empty when it does not exist.
QStringList modFileNamesIn(const QString& modsDir);

/// The info strip above the shader results.
struct ShaderNotice {
    bool show = false;
    QString text;
    QString buttonText;   ///< empty = no button
    QString searchQuery;  ///< what the button looks for in Discover > Mods ("Iris", "Oculus")
};

/// The notice for a target: none while it has a shader mod. Fabric, Quilt and NeoForge -> Iris, Forge -> Oculus, no loader at all ->
/// "Shaders need Iris or OptiFine, which need a modded instance." without a button. Other loaders (Legacy Fabric, Rift ...) get no
/// notice: ZERO knows no shader mod for them.
ShaderNotice shaderNoticeFor(const QString& instanceName, ModPlatform::ModLoaderTypes loaders, const ShaderModFacts& facts);

}  // namespace Zero::Content
