// SPDX-License-Identifier: GPL-3.0-only
#include "InstanceInfo.h"

#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "FileSystem.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"

namespace Zero {

namespace {
struct LoaderEntry {
    const char* uid;
    const char* name;
};
constexpr LoaderEntry kLoaders[] = {
    { "net.neoforged", "NeoForge" },
    { "net.minecraftforge", "Forge" },
    { "net.fabricmc.fabric-loader", "Fabric" },
    { "org.quiltmc.quilt-loader", "Quilt" },
    { "com.mumfrey.liteloader", "LiteLoader" },
};

const char* loaderName(const QString& uid)
{
    for (const auto& entry : kLoaders) {
        if (uid == QLatin1String(entry.uid))
            return entry.name;
    }
    return nullptr;
}

/// Reads versions straight from `<instance root>/mmc-pack.json`. The component list of an instance is only loaded
/// when it is first needed (selection, launch), and loading it for every instance at startup would resolve metadata
/// and rewrite files, so the pages use this read-only look instead. Cached per instance root.
InstanceInfo readPackFile(const QString& instanceRoot)
{
    static QHash<QString, InstanceInfo> cache;
    const QString packPath = FS::PathCombine(instanceRoot, QStringLiteral("mmc-pack.json"));
    // Key includes the file's mtime so external edits / re-created instances never show stale versions.
    const QString key = instanceRoot + QLatin1Char('|') + QString::number(QFileInfo(packPath).lastModified().toMSecsSinceEpoch());
    if (auto it = cache.constFind(key); it != cache.constEnd())
        return *it;

    InstanceInfo info;
    QFile file(packPath);
    if (file.open(QIODevice::ReadOnly)) {
        const auto doc = QJsonDocument::fromJson(file.readAll());
        for (const QJsonValue& value : doc.object().value(QStringLiteral("components")).toArray()) {
            const QJsonObject component = value.toObject();
            const QString uid = component.value(QStringLiteral("uid")).toString();
            const QString version = component.value(QStringLiteral("version")).toString();
            if (uid == QLatin1String("net.minecraft")) {
                info.minecraftVersion = version;
            } else if (const char* name = loaderName(uid); name && info.loader.isEmpty()) {
                info.loader = QLatin1String(name);
                info.loaderVersion = version;
            }
        }
    }
    cache.insert(key, info);
    return info;
}
}  // namespace

QString InstanceInfo::summary() const
{
    if (minecraftVersion.isEmpty())
        return loader;
    if (loader.isEmpty())
        return minecraftVersion;
    return minecraftVersion + QStringLiteral(" · ") + loader;
}

InstanceInfo InstanceInfo::of(MinecraftInstance* instance)
{
    InstanceInfo info;
    if (!instance)
        return info;

    // Loaded component list (selected / launched / edited instances): always current.
    if (auto* components = instance->getPackProfile()) {
        info.minecraftVersion = components->getComponentVersion(QStringLiteral("net.minecraft"));
        if (!info.minecraftVersion.isEmpty()) {
            for (const auto& entry : kLoaders) {
                const QString version = components->getComponentVersion(QLatin1String(entry.uid));
                if (!version.isEmpty() || components->getComponent(QLatin1String(entry.uid))) {
                    info.loader = QLatin1String(entry.name);
                    info.loaderVersion = version;
                    break;
                }
            }
            return info;
        }
    }
    return readPackFile(instance->instanceRoot());
}

}  // namespace Zero
