// SPDX-License-Identifier: GPL-3.0-only
#include "ModTarget.h"

#include <QDebug>
#include <QDir>

#include <algorithm>

#include "BaseInstance.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "modplatform/flame/FlameAPI.h"
#include "modplatform/modrinth/ModrinthAPI.h"

namespace Zero {
namespace Content {

bool TargetInstance::modReady() const
{
    if (mcVersion.isEmpty())
        return false;
    // The same checks the download dialogs use: a loader either provider accepts.
    return ModrinthAPI::validateModLoaders(loaders) || FlameAPI::validateModLoaders(loaders);
}

QString TargetInstance::summary() const
{
    if (loaderText.isEmpty())
        return mcVersion;
    return mcVersion.isEmpty() ? loaderText : mcVersion + QLatin1Char(' ') + loaderText;
}

QStringList loaderDisplayNames(ModPlatform::ModLoaderTypes loaders)
{
    static const QList<QPair<ModPlatform::ModLoaderType, QString>> known = {
        { ModPlatform::Fabric, QStringLiteral("Fabric") },
        { ModPlatform::Forge, QStringLiteral("Forge") },
        { ModPlatform::NeoForge, QStringLiteral("NeoForge") },
        { ModPlatform::Quilt, QStringLiteral("Quilt") },
        { ModPlatform::LegacyFabric, QStringLiteral("Legacy Fabric") },
        { ModPlatform::Babric, QStringLiteral("Babric") },
        { ModPlatform::BTA, QStringLiteral("BTA") },
        { ModPlatform::Ornithe, QStringLiteral("Ornithe") },
        { ModPlatform::Rift, QStringLiteral("Rift") },
        { ModPlatform::LiteLoader, QStringLiteral("LiteLoader") },
        { ModPlatform::Cauldron, QStringLiteral("Cauldron") },
    };
    QStringList out;
    for (const auto& [flag, label] : known) {
        if (loaders.testFlag(flag))
            out.append(label);
    }
    return out;
}

TargetInstance targetFromInstance(BaseInstance* instance)
{
    TargetInstance t;
    if (!instance)
        return t;
    t.id = instance->id();
    t.name = instance->name();
    t.iconKey = instance->iconKey();
    t.running = instance->isRunning();
    if (const qint64 ms = instance->lastLaunch(); ms > 0)
        t.lastLaunch = QDateTime::fromMSecsSinceEpoch(ms);
    auto* mc = dynamic_cast<MinecraftInstance*>(instance);
    if (!mc)
        return t;
    t.modsDir = QDir(mc->gameRoot()).filePath(QStringLiteral("mods"));
    auto* profile = mc->getPackProfile();
    if (!profile)
        return t;
    t.mcVersion = profile->getComponentVersion(QStringLiteral("net.minecraft"));
    // Never while the profile's own update/resolve task runs: reload() would abort it (an online update of a launch, say).
    if (t.mcVersion.isEmpty() && !profile->getCurrentTask()) {
        // The pack profile reads mmc-pack.json lazily: an instance nobody looked at yet has no components. Prism's own status bar does
        // the same (MinecraftInstance::getStatusbarDescription): load the component list offline (local files only, no network).
        if (auto res = profile->reload(Net::Mode::Offline); !res)
            qWarning() << "Failed to load the components of" << instance->name() << ":" << res.error();
        t.mcVersion = profile->getComponentVersion(QStringLiteral("net.minecraft"));
    }
    if (const auto loaders = profile->getSupportedModLoaders(); loaders.has_value())
        t.loaders = *loaders;
    t.loaderText = loaderDisplayNames(t.loaders).join(QStringLiteral(", "));
    return t;
}

QList<TargetInstance> targetsFromInstanceList(InstanceList* list)
{
    QList<TargetInstance> out;
    if (!list)
        return out;
    for (int i = 0; i < list->count(); ++i)
        out.append(targetFromInstance(list->at(i)));
    return out;
}

QString chooseTarget(const QList<TargetInstance>& targets, const QString& librarySelectedId, const QString& savedId,
                     bool needsLoader)
{
    const auto readyById = [&](const QString& id) -> const TargetInstance* {
        if (id.isEmpty())
            return nullptr;
        for (const auto& t : targets) {
            if (t.id == id)
                return t.readyFor(needsLoader) ? &t : nullptr;
        }
        return nullptr;
    };
    if (const auto* t = readyById(librarySelectedId))
        return t->id;
    if (const auto* t = readyById(savedId))
        return t->id;

    QList<const TargetInstance*> ready;
    for (const auto& t : targets) {
        if (t.readyFor(needsLoader))
            ready.append(&t);
    }
    if (ready.isEmpty())
        return {};
    const auto byName = [](const TargetInstance* a, const TargetInstance* b) {
        const int c = QString::compare(a->name, b->name, Qt::CaseInsensitive);
        return c != 0 ? c < 0 : a->id < b->id;
    };
    const TargetInstance* best = nullptr;
    for (const auto* t : ready) {
        if (t->lastLaunch.isValid() && (!best || t->lastLaunch > best->lastLaunch ||
                                        (t->lastLaunch == best->lastLaunch && byName(t, best))))
            best = t;
    }
    if (best)
        return best->id;
    return (*std::min_element(ready.begin(), ready.end(), byName))->id;
}

int countWithoutLoader(const QList<TargetInstance>& targets)
{
    return int(std::count_if(targets.begin(), targets.end(), [](const TargetInstance& t) { return !t.modReady(); }));
}

}  // namespace Content
}  // namespace Zero
