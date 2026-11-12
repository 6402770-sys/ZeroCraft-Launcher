// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2022 flowln <flowlnlnln@gmail.com>
 *  Copyright (c) 2023 Trial97 <alexandru.tripon97@gmail.com>
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
 */

#include "modplatform/ModIndex.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QIODevice>
#include <QJsonValue>

#include <cmath>

namespace ModPlatform {

ModLoaderType operator|(ModLoaderType lhs, ModLoaderType rhs)
{
    return static_cast<ModLoaderType>(static_cast<std::uint16_t>(lhs) | static_cast<std::uint16_t>(rhs));
}

static const QList<ModLoaderType> loaderList = { NeoForge, Forge, Cauldron,     LiteLoader, Quilt, Fabric,
                                                 Babric,   BTA,   LegacyFabric, Ornithe,    Rift };

QList<ModLoaderType> modLoaderTypesToList(ModLoaderTypes flags)
{
    QList<ModLoaderType> flagList;
    for (auto flag : loaderList) {
        if (flags.testFlag(flag)) {
            flagList.append(flag);
        }
    }
    return flagList;
}

const char* ProviderCapabilities::name(ResourceProvider p)
{
    switch (p) {
        case ResourceProvider::MODRINTH:
            return "modrinth";
        case ResourceProvider::FLAME:
            return "curseforge";
    }
    return {};
}

QString ProviderCapabilities::readableName(ResourceProvider p)
{
    switch (p) {
        case ResourceProvider::MODRINTH:
            return "Modrinth";
        case ResourceProvider::FLAME:
            return "CurseForge";
    }
    return {};
}

QStringList ProviderCapabilities::hashType(ResourceProvider p)
{
    switch (p) {
        case ResourceProvider::MODRINTH:
            return { "sha512", "sha1" };
        case ResourceProvider::FLAME:
            // Try newer formats first, fall back to old format
            return { "sha1", "md5", "murmur2" };
    }
    return {};
}

QString getMetaURL(ResourceProvider provider, QVariant projectID)
{
    return ((provider == ModPlatform::ResourceProvider::FLAME) ? "https://www.curseforge.com/projects/" : "https://modrinth.com/mod/") +
           projectID.toString();
}

auto getModLoaderAsString(ModLoaderType type) -> const QString
{
    switch (type) {
        case NeoForge:
            return "neoforge";
        case Forge:
            return "forge";
        case Cauldron:
            return "cauldron";
        case LiteLoader:
            return "liteloader";
        case Fabric:
            return "fabric";
        case Quilt:
            return "quilt";
        case DataPack:
            return "datapack";
        case Babric:
            return "babric";
        case BTA:
            return "bta-babric";
        case LegacyFabric:
            return "legacy-fabric";
        case Ornithe:
            return "ornithe";
        case Rift:
            return "rift";
        default:
            break;
    }
    return "";
}

auto getModLoaderFromString(QString type) -> ModLoaderType
{
    if (type == "neoforge")
        return NeoForge;
    if (type == "forge")
        return Forge;
    if (type == "cauldron")
        return Cauldron;
    if (type == "liteloader")
        return LiteLoader;
    if (type == "fabric")
        return Fabric;
    if (type == "quilt")
        return Quilt;
    if (type == "babric")
        return Babric;
    if (type == "bta-babric")
        return BTA;
    if (type == "legacy-fabric")
        return LegacyFabric;
    if (type == "ornithe")
        return Ornithe;
    if (type == "rift")
        return Rift;
    return {};
}

// ZERO: keeps search-hit fields the details response does not carry (see ModIndex.h).
void mergeProjectDetails(IndexedPack& into, const IndexedPack& details)
{
    IndexedPack merged = details;
    if (merged.authors.isEmpty()) {
        merged.authors = into.authors;
    }
    if (merged.downloads < 0) {
        merged.downloads = into.downloads;
    }
    if (merged.follows < 0) {
        merged.follows = into.follows;
    }
    if (merged.categories.isEmpty()) {
        merged.categories = into.categories;
    }
    if (merged.categoryIds.isEmpty()) {
        merged.categoryIds = into.categoryIds;
    }
    if (merged.gameVersions.isEmpty()) {
        merged.gameVersions = into.gameVersions;
    }
    if (merged.loaderNames.isEmpty()) {
        merged.loaderNames = into.loaderNames;
    }
    if (merged.gallery.isEmpty()) {
        merged.gallery = into.gallery;
    }
    if (merged.featuredImageUrl.isEmpty()) {
        merged.featuredImageUrl = into.featuredImageUrl;
    }
    if (!merged.dateCreated.isValid()) {
        merged.dateCreated = into.dateCreated;
    }
    if (!merged.dateModified.isValid()) {
        merged.dateModified = into.dateModified;
    }
    if (merged.license.isEmpty()) {
        merged.license = into.license;
    }
    into = std::move(merged);
}

// ZERO: lenient readers for the presentation fields.
qint64 countFromJson(const QJsonValue& value)
{
    if (!value.isDouble()) {
        return -1;
    }
    const double d = value.toDouble();
    // 2^53: every integer below it is exact in a double; beyond it a count is not meaningful anyway.
    if (!std::isfinite(d) || d < 0 || d > 9007199254740992.0) {
        return -1;
    }
    return static_cast<qint64>(d);
}

QDateTime dateFromJson(const QJsonValue& value)
{
    if (!value.isString()) {
        return {};
    }
    auto date = QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
    return date.isValid() ? date : QDateTime();
}

}  // namespace ModPlatform
