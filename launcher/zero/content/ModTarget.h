// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the "target instance" of Discover > Mods (docs/zero/CONTENT-MODS.md, "Target instance").
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

#include "modplatform/ModIndex.h"

class BaseInstance;
class InstanceList;

namespace Zero {
namespace Content {

/// What the mod browser needs to know about an instance. Plain data: tests build these by hand.
struct TargetInstance {
    QString id;
    QString name;
    QString iconKey;
    QString mcVersion;
    ModPlatform::ModLoaderTypes loaders;
    QString loaderText;  ///< Display names of the loaders ("Fabric", "Fabric, Quilt"); empty when there are none.
    bool running = false;
    QDateTime lastLaunch;
    QString modsDir;  ///< `<game root>/mods` (Discover > Shaders looks for a shader mod there); empty when unknown

    /// An MC version and at least one loader Modrinth or CurseForge accepts.
    bool modReady() const;
    /// Resource packs need only a Minecraft version (no loader): every instance that has one can take them.
    bool packReady() const { return !mcVersion.isEmpty(); }
    /// `needsLoader` (mods) = modReady(), else (resource packs) = packReady().
    bool readyFor(bool needsLoader) const { return needsLoader ? modReady() : packReady(); }
    /// "1.20.1 Fabric"; just the version when there is no loader text.
    QString summary() const;
    /// summary() for mods; just the Minecraft version ("1.21.1") for resource packs (the loader does not matter for them).
    QString summaryFor(bool needsLoader) const { return needsLoader ? summary() : mcVersion; }
};

/// Display names of the loader flags in a stable order (Fabric, Forge, NeoForge, Quilt, then the rest).
QStringList loaderDisplayNames(ModPlatform::ModLoaderTypes loaders);

/// Reads the instance's pack profile (GUI thread). Instances that are not Minecraft instances are not mod-ready.
TargetInstance targetFromInstance(BaseInstance* instance);
QList<TargetInstance> targetsFromInstanceList(InstanceList* list);

/// The default target: the library's selection if mod-ready, else the saved id if present and mod-ready, else the most
/// recently launched mod-ready instance, else the first mod-ready one by name (case-insensitive). Empty when none.
/// `needsLoader` false (resource packs): "ready" is packReady() instead of modReady().
QString chooseTarget(const QList<TargetInstance>& targets, const QString& librarySelectedId, const QString& savedId,
                     bool needsLoader = true);

/// Instances that cannot take mods.
int countWithoutLoader(const QList<TargetInstance>& targets);

}  // namespace Content
}  // namespace Zero
