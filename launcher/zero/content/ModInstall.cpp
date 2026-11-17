// SPDX-License-Identifier: GPL-3.0-only
#include "ModInstall.h"

#include <QCoreApplication>
#include <QSet>

#include "zero/content/ModpackInstall.h"

namespace Zero::Content {

namespace {
}  // namespace

// ---- compatibility ----

bool versionFitsTarget(const ModPlatform::IndexedVersion& version, const QString& mcVersion, ModPlatform::ModLoaderTypes loaders)
{
    if (!mcVersion.isEmpty() && !version.mcVersion.isEmpty() && !version.mcVersion.contains(mcVersion)) {
        return false;
    }
    if (loaders != ModPlatform::ModLoaderTypes() && version.loaders != ModPlatform::ModLoaderTypes() && !(version.loaders & loaders)) {
        return false;
    }
    return true;
}

QList<ModPlatform::IndexedVersion> compatibleVersions(const QList<ModPlatform::IndexedVersion>& all, const QString& mcVersion,
                                                      ModPlatform::ModLoaderTypes loaders)
{
    QList<ModPlatform::IndexedVersion> out;
    for (const auto& version : all) {
        if (version.downloadUrl.trimmed().isEmpty() || version.fileName.trimmed().isEmpty()) {
            continue;  // an opted-out CurseForge file, a version without a file: nothing to download
        }
        if (versionFitsTarget(version, mcVersion, loaders)) {
            out.append(version);
        }
    }
    return out;
}

int defaultModVersionIndex(const QList<ModPlatform::IndexedVersion>& compatible)
{
    QList<ModpackVersion> versions;
    versions.reserve(compatible.size());
    for (const auto& version : compatible) {
        versions.append(versionFromIndexed(version));
    }
    // The Discover rule (the section's "Installed" state uses it too): the newest release, else the first installable.
    return defaultVersionIndex(versions, Provider::Modrinth);
}

// ---- plan ----

QString reasonStandalone()
{
    return QStringLiteral("standalone");
}

QString reasonDependency()
{
    return QStringLiteral("dependency");
}

QString reasonUpdate()
{
    return QStringLiteral("update");
}

ModInstallPlan buildInstallPlan(const ModPlatform::IndexedPack::Ptr& pack, const ModPlatform::IndexedVersion& version, bool update,
                                const QList<PlannedMod>& dependencies)
{
    ModInstallPlan plan;
    if (!pack) {
        return plan;
    }
    PlannedMod main;
    main.pack = pack;
    main.version = version;
    main.reason = update ? reasonUpdate() : reasonStandalone();
    plan.mods.append(main);

    QSet<QString> seen{ pack->addonId.toString() };
    for (const PlannedMod& dep : dependencies) {
        if (!dep.pack) {
            continue;
        }
        const QString id = dep.pack->addonId.toString();
        if (seen.contains(id)) {
            continue;
        }
        seen.insert(id);
        PlannedMod planned = dep;
        planned.reason = reasonDependency();
        plan.mods.append(planned);
    }
    return plan;
}

ModInstallPlan buildPackInstallPlan(const ModPlatform::IndexedPack::Ptr& pack, const ModPlatform::IndexedVersion& version, bool update)
{
    return buildInstallPlan(pack, version, update, {});
}

QStringList alreadyInstalledNames(ModPlatform::ResourceProvider provider, const ModPlatform::IndexedVersion& version,
                                  const QList<InstalledMod>& installed)
{
    const QString type = managedPackType(provider == ModPlatform::ResourceProvider::FLAME ? Provider::CurseForge : Provider::Modrinth);
    QStringList names;
    QSet<QString> seen;
    for (const auto& dependency : version.dependencies) {
        if (dependency.type != ModPlatform::DependencyType::REQUIRED) {
            continue;
        }
        const QString id = dependency.addonId.toString();
        if (id.isEmpty() || seen.contains(id)) {
            continue;
        }
        seen.insert(id);
        const QString key = managedKey(type, id);
        for (const auto& mod : installed) {
            if (mod.key == key) {
                names.append(mod.name.isEmpty() ? id : mod.name);
                break;
            }
        }
    }
    return names;
}

// ---- texts ----

QString dependencyCountText(int count)
{
    return count == 1 ? QCoreApplication::translate("Zero::Content::ModInstall", "1 dependency") : QCoreApplication::translate("Zero::Content::ModInstall", "%1 dependencies").arg(count);
}

QString primaryActionText(int modCount, bool update)
{
    if (update) {
        if (modCount <= 1) {
            return QCoreApplication::translate("Zero::Content::ModInstall", "Update");
        }
        const int added = modCount - 1;
        return added == 1 ? QCoreApplication::translate("Zero::Content::ModInstall", "Update and add 1 mod") : QCoreApplication::translate("Zero::Content::ModInstall", "Update and add %1 mods").arg(added);
    }
    return modCount <= 1 ? QCoreApplication::translate("Zero::Content::ModInstall", "Add mod") : QCoreApplication::translate("Zero::Content::ModInstall", "Add %1 mods").arg(modCount);
}

QString packPrimaryActionText(bool update)
{
    return update ? QCoreApplication::translate("Zero::Content::ModInstall", "Update") : QCoreApplication::translate("Zero::Content::ModInstall", "Add pack");
}

QString shaderPrimaryActionText(bool update)
{
    return update ? QCoreApplication::translate("Zero::Content::ModInstall", "Update") : QCoreApplication::translate("Zero::Content::ModInstall", "Add shader");
}

QString addedToastText(const QString& modName, const QString& instanceName, int dependencyCount)
{
    QString text = QCoreApplication::translate("Zero::Content::ModInstall", "Added %1 to %2").arg(modName, instanceName);
    if (dependencyCount > 0) {
        text += QCoreApplication::translate("Zero::Content::ModInstall", " and %1").arg(dependencyCountText(dependencyCount));
    }
    return text;
}

QString updatedToastText(const QString& modName, const QString& instanceName, int dependencyCount)
{
    QString text = QCoreApplication::translate("Zero::Content::ModInstall", "Updated %1 in %2").arg(modName, instanceName);
    if (dependencyCount > 0) {
        text += QCoreApplication::translate("Zero::Content::ModInstall", " and added %1").arg(dependencyCountText(dependencyCount));
    }
    return text;
}

QString requiredByText(const QStringList& names)
{
    return names.isEmpty() ? QString() : QCoreApplication::translate("Zero::Content::ModInstall", "Required by %1").arg(names.join(QStringLiteral(", ")));
}

}  // namespace Zero::Content
