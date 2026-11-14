// SPDX-License-Identifier: GPL-3.0-only
#include "CartInstall.h"

#include <QHash>
#include <QObject>

#include <algorithm>

#include "zero/content/InstallCart.h"

namespace Zero::Content {

namespace {

int rank(Kind kind)
{
    switch (kind) {
        case Kind::Mod:
            return 0;
        case Kind::ResourcePack:
            return 1;
        case Kind::Shader:
            return 2;
        case Kind::Modpack:
        case Kind::World:
            break;
    }
    return 3;
}

QString joinAnd(const QStringList& parts)
{
    if (parts.size() <= 1) {
        return parts.join(QString());
    }
    return parts.mid(0, parts.size() - 1).join(QStringLiteral(", ")) + QObject::tr(" and ") + parts.last();
}

}  // namespace

// ---- plan ----

int CartInstallPlan::countOf(Kind kind) const
{
    return int(std::count_if(downloads.cbegin(), downloads.cend(), [kind](const CartDownload& d) { return d.kind == kind; }));
}

int CartInstallPlan::dependencyCount() const
{
    return int(std::count_if(downloads.cbegin(), downloads.cend(), [](const CartDownload& d) { return d.cartKey.isEmpty(); }));
}

bool CartInstallPlan::allUpdates() const
{
    return !downloads.isEmpty() &&
           std::all_of(downloads.cbegin(), downloads.cend(), [](const CartDownload& d) { return d.mod.reason == reasonUpdate(); });
}

QStringList CartInstallPlan::cartKeys() const
{
    QStringList out;
    for (const CartDownload& d : downloads) {
        if (!d.cartKey.isEmpty()) {
            out.append(d.cartKey);
        }
    }
    return out;
}

QString CartInstallPlan::firstPageId() const
{
    if (countOf(Kind::Mod) > 0) {
        return QStringLiteral("mods");
    }
    if (countOf(Kind::ResourcePack) > 0) {
        return QStringLiteral("resourcepacks");
    }
    if (countOf(Kind::Shader) > 0) {
        return QStringLiteral("shaderpacks");
    }
    return {};
}

QList<CartDownload> CartInstallPlan::packsToTurnOn() const
{
    QList<CartDownload> out;
    for (const CartDownload& d : downloads) {
        if (d.kind == Kind::ResourcePack && d.turnOn) {
            out.append(d);
        }
    }
    return out;
}

QString projectKeyOf(const ModPlatform::IndexedPack::Ptr& pack)
{
    return pack ? QStringLiteral("%1:%2").arg(int(pack->provider)).arg(pack->addonId.toString()) : QString();
}

CartInstallPlan buildCartPlan(const QList<CartPlanRow>& rows, const QList<PlannedMod>& dependencies, bool turnPacksOn)
{
    CartInstallPlan plan;
    const auto addRows = [&](Kind kind) {
        for (const CartPlanRow& row : rows) {
            if (row.kind != kind || !row.pack || row.version.downloadUrl.trimmed().isEmpty()) {
                continue;
            }
            CartDownload download;
            download.kind = kind;
            download.cartKey = row.cartKey;
            download.mod.pack = row.pack;
            download.mod.version = row.version;
            download.mod.reason = row.update ? reasonUpdate() : reasonStandalone();
            download.turnOn = kind == Kind::ResourcePack && turnPacksOn && !row.update;
            plan.downloads.append(download);
        }
    };
    addRows(Kind::Mod);
    QSet<QString> seen;
    for (const PlannedMod& dep : dependencies) {
        const QString key = projectKeyOf(dep.pack);
        if (key.isEmpty() || seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        CartDownload download;
        download.kind = Kind::Mod;
        download.mod = dep;
        download.mod.reason = reasonDependency();
        plan.downloads.append(download);
    }
    addRows(Kind::ResourcePack);
    addRows(Kind::Shader);
    return plan;
}

MergedDependencies mergeDependencies(const QList<PlannedMod>& found, const QSet<QString>& projectsInCart)
{
    MergedDependencies out;
    QHash<QString, int> index;  // project key -> position in out.toAdd
    QSet<QString> alreadySeen;
    for (const PlannedMod& dep : found) {
        const QString key = projectKeyOf(dep.pack);
        if (key.isEmpty() || projectsInCart.contains(key)) {
            continue;
        }
        if (dep.maybeInstalled) {
            if (!alreadySeen.contains(key)) {
                alreadySeen.insert(key);
                out.alreadyInstalled.append(dep.name().isEmpty() ? dep.pack->addonId.toString() : dep.name());
            }
            continue;
        }
        const auto at = index.constFind(key);
        if (at == index.constEnd()) {
            index.insert(key, int(out.toAdd.size()));
            out.toAdd.append(dep);
            continue;
        }
        PlannedMod& kept = out.toAdd[*at];
        for (const QString& name : dep.requiredBy) {
            if (!kept.requiredBy.contains(name)) {
                kept.requiredBy.append(name);
            }
        }
        if (kept.dependentOn.isEmpty()) {
            kept.dependentOn = dep.dependentOn;
        }
    }
    return out;
}

// ---- texts ----

QString cartAddText(int mods, int resourcePacks, int shaders)
{
    const int kinds = (mods > 0) + (resourcePacks > 0) + (shaders > 0);
    if (kinds > 1) {
        return QObject::tr("Add %1 items").arg(mods + resourcePacks + shaders);
    }
    if (mods > 0) {
        return mods == 1 ? QObject::tr("Add 1 mod") : QObject::tr("Add %1 mods").arg(mods);
    }
    if (resourcePacks > 0) {
        return resourcePacks == 1 ? QObject::tr("Add 1 resource pack") : QObject::tr("Add %1 resource packs").arg(resourcePacks);
    }
    if (shaders > 0) {
        return shaders == 1 ? QObject::tr("Add 1 shader") : QObject::tr("Add %1 shaders").arg(shaders);
    }
    return QObject::tr("Add items");
}

QString cartPrimaryText(int mods, int resourcePacks, int shaders)
{
    if (mods + resourcePacks + shaders == 0) {
        return QObject::tr("Add");
    }
    return cartAddText(mods, resourcePacks, shaders);
}

QString cartCheckingText()
{
    return QObject::tr("Checking…");
}

QString cartSubtitleText(const QString& instanceName, const QString& summary)
{
    return summary.isEmpty() ? QObject::tr("To %1").arg(instanceName) : QObject::tr("To %1 · %2").arg(instanceName, summary);
}

QString cartToastText(int mods, int resourcePacks, int shaders, const QString& instanceName, bool updates)
{
    const QString what = joinAnd(InstallCart::countParts(mods, resourcePacks, shaders));
    return updates ? QObject::tr("Updated %1 in %2").arg(what, instanceName) : QObject::tr("Added %1 to %2").arg(what, instanceName);
}

QString cartCloseText(const QString& instanceName)
{
    return QObject::tr("Close %1 to add content").arg(instanceName);
}

QString cartFindingVersionsText()
{
    return QObject::tr("Finding versions…");
}

QString cartVersionsFailedText()
{
    return QObject::tr("Couldn't load versions");
}

QString cartNoVersionText(const QString& summary)
{
    return QObject::tr("No version for %1 · will be skipped").arg(summary);
}

QString cartNeedsModdedText()
{
    return QObject::tr("Needs a modded instance · will be skipped");
}

QString cartUpdateText(const QString& version)
{
    return version.isEmpty() ? QObject::tr("Update") : QObject::tr("Update → %1").arg(version);
}

}  // namespace Zero::Content
