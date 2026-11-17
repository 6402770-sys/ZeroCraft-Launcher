// SPDX-License-Identifier: GPL-3.0-only
#include "DropPlan.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include "zero/mods/ModMetadata.h"
#include "zero/mods/Versions.h"

namespace Zero {
namespace Content {
namespace Drop {

namespace {

ModPlatform::ModLoaderTypes loaderOf(Mods::LoaderFormat format)
{
    switch (format) {
        case Mods::LoaderFormat::Fabric:
            return ModPlatform::ModLoaderTypes(ModPlatform::Fabric);
        case Mods::LoaderFormat::Quilt:
            return ModPlatform::ModLoaderTypes(ModPlatform::Quilt);
        case Mods::LoaderFormat::Forge:
        case Mods::LoaderFormat::LegacyForge:
            return ModPlatform::ModLoaderTypes(ModPlatform::Forge);
        case Mods::LoaderFormat::NeoForge:
            return ModPlatform::ModLoaderTypes(ModPlatform::NeoForge);
        case Mods::LoaderFormat::Unknown:
            break;
    }
    return {};
}

bool isMavenFormat(Mods::LoaderFormat format)
{
    return format == Mods::LoaderFormat::Forge || format == Mods::LoaderFormat::NeoForge || format == Mods::LoaderFormat::LegacyForge;
}

/// A Fabric mod also runs on Quilt.
bool loadersFit(ModPlatform::ModLoaderTypes mod, ModPlatform::ModLoaderTypes instance)
{
    ModPlatform::ModLoaderTypes accepted = instance;
    if (instance.testFlag(ModPlatform::Quilt)) {
        accepted |= ModPlatform::Fabric;
    }
    return (mod & accepted).toInt() != 0;
}

}  // namespace

bool kindFromResource(ModPlatform::ResourceType type, Kind* out)
{
    Kind kind;
    switch (type) {
        case ModPlatform::ResourceType::Mod:
            kind = Kind::Mod;
            break;
        case ModPlatform::ResourceType::ResourcePack:
            kind = Kind::ResourcePack;
            break;
        case ModPlatform::ResourceType::ShaderPack:
            kind = Kind::ShaderPack;
            break;
        case ModPlatform::ResourceType::World:
            kind = Kind::World;
            break;
        case ModPlatform::ResourceType::DataPack:
            kind = Kind::DataPack;
            break;
        case ModPlatform::ResourceType::TexturePack:
            kind = Kind::TexturePack;
            break;
        default:
            return false;
    }
    if (out) {
        *out = kind;
    }
    return true;
}

QString kindLabel(Kind kind)
{
    switch (kind) {
        case Kind::Mod:
            return QCoreApplication::translate("Zero::Content::Drop", "Mod");
        case Kind::ResourcePack:
            return QCoreApplication::translate("Zero::Content::Drop", "Resource pack");
        case Kind::ShaderPack:
            return QCoreApplication::translate("Zero::Content::Drop", "Shader pack");
        case Kind::World:
            return QCoreApplication::translate("Zero::Content::Drop", "World");
        case Kind::DataPack:
            return QCoreApplication::translate("Zero::Content::Drop", "Data pack");
        case Kind::TexturePack:
            return QCoreApplication::translate("Zero::Content::Drop", "Texture pack");
    }
    return {};
}

QString pageId(Kind kind)
{
    switch (kind) {
        case Kind::Mod:
            return QStringLiteral("mods");
        case Kind::ResourcePack:
            return QStringLiteral("resourcepacks");
        case Kind::ShaderPack:
            return QStringLiteral("shaderpacks");
        case Kind::World:
            return QStringLiteral("worlds");
        case Kind::TexturePack:
            return QStringLiteral("texturepacks");
        case Kind::DataPack:
            break;
    }
    return {};
}

bool installable(Kind kind)
{
    return kind != Kind::DataPack;
}

ModInfo modInfoFrom(const Mods::ModMetadata& metadata)
{
    ModInfo info;
    for (const Mods::ModEntry& entry : metadata.mods) {
        if (info.displayName.isEmpty()) {
            info.displayName = entry.displayName();
        }
        info.loaders |= loaderOf(entry.format);
        for (const Mods::Dependency& dep : entry.depends) {
            if (dep.id == QLatin1String("minecraft")) {
                info.minecraft.push_back({ dep.versionPredicate, isMavenFormat(entry.format) });
                break;
            }
        }
    }
    return info;
}

DropFile inspectFile(const QString& path, Kind kind)
{
    const QFileInfo fi(path);
    DropFile file;
    file.path = QDir::toNativeSeparators(fi.absoluteFilePath());
    file.fileName = fi.fileName();
    file.name = file.fileName;
    file.kind = kind;
    file.isDir = fi.isDir();
    file.size = file.isDir ? -1 : fi.size();
    if (kind == Kind::Mod && !file.isDir) {
        file.mod = modInfoFrom(Mods::parseJar(file.path));
        if (!file.mod.displayName.isEmpty()) {
            file.name = file.mod.displayName;
        }
    }
    return file;
}

QString fitCaption(const DropFile& file, const TargetInstance& target)
{
    if (file.kind != Kind::Mod) {
        return {};
    }
    if (file.mod.loaders.toInt() != 0 && target.loaders.toInt() != 0 && !loadersFit(file.mod.loaders, target.loaders)) {
        return QCoreApplication::translate("Zero::Content::Drop", "Made for %1 — this instance uses %2")
            .arg(loaderDisplayNames(file.mod.loaders).join(QStringLiteral(", ")),
                 target.loaderText.isEmpty() ? loaderDisplayNames(target.loaders).join(QStringLiteral(", ")) : target.loaderText);
    }
    if (!target.mcVersion.isEmpty() && !file.mod.minecraft.isEmpty()) {
        bool allNo = true;
        for (const auto& requirement : file.mod.minecraft) {
            const Mods::Tri match = requirement.maven ? Mods::matchesMavenRange(target.mcVersion, requirement.predicate)
                                                      : Mods::matchesFabricPredicate(target.mcVersion, requirement.predicate);
            if (match != Mods::Tri::No) {
                allNo = false;
                break;
            }
        }
        if (allNo) {
            const auto& first = file.mod.minecraft.first();
            return QCoreApplication::translate("Zero::Content::Drop", "Made for Minecraft %1 — this instance is %2")
                .arg(Mods::describePredicate(first.predicate, first.maven), target.mcVersion);
        }
    }
    return {};
}

QString replaceCaption(const DropFile& file, const QString& folder)
{
    if (folder.isEmpty() || file.kind == Kind::World || file.kind == Kind::DataPack || file.fileName.isEmpty()) {
        return {};
    }
    if (QFileInfo::exists(QDir(folder).filePath(file.fileName))) {
        return QCoreApplication::translate("Zero::Content::Drop", "Replaces the file with the same name");
    }
    return {};
}

QString notInstallableCaption(const DropFile& file)
{
    if (file.kind == Kind::DataPack) {
        return QCoreApplication::translate("Zero::Content::Drop", "Data packs go inside a world. Open the world's folder to add it.");
    }
    return {};
}

QString titleText(int count)
{
    return count == 1 ? QCoreApplication::translate("Zero::Content::Drop", "Add 1 file") : QCoreApplication::translate("Zero::Content::Drop", "Add %1 files").arg(count);
}

QString summaryText(int ticked)
{
    if (ticked <= 0) {
        return QCoreApplication::translate("Zero::Content::Drop", "Nothing to add");
    }
    return ticked == 1 ? QCoreApplication::translate("Zero::Content::Drop", "Adds 1 file") : QCoreApplication::translate("Zero::Content::Drop", "Adds %1 files").arg(ticked);
}

QString primaryText(const QString& instanceName)
{
    return QCoreApplication::translate("Zero::Content::Drop", "Add to %1").arg(instanceName);
}

QString addedToastText(int count, const QString& instanceName)
{
    return count == 1 ? QCoreApplication::translate("Zero::Content::Drop", "Added 1 file to %1").arg(instanceName) : QCoreApplication::translate("Zero::Content::Drop", "Added %1 files to %2").arg(count).arg(instanceName);
}

QString failedToastText(const QString& fileName)
{
    return QCoreApplication::translate("Zero::Content::Drop", "Could not add %1").arg(fileName);
}

QString openPage(const QList<DropFile>& added)
{
    for (const DropFile& file : added) {
        if (file.kind == Kind::Mod) {
            return pageId(Kind::Mod);
        }
    }
    return added.isEmpty() ? QString() : pageId(added.first().kind);
}

}  // namespace Drop
}  // namespace Content
}  // namespace Zero
