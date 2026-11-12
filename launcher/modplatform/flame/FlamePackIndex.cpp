// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2026 Trial97 <alexandru.tripon97@gmail.com>
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

#include "FlamePackIndex.h"

#include "Json.h"
#include "modplatform/ModIndex.h"
#include "modplatform/flame/FlameAPI.h"

namespace {
// ZERO: CurseForge `ModLoaderType` (docs.curseforge.com, tocS_ModLoaderType) to the lower-case loader names ZERO uses.
QString loaderNameFromFlame(int modLoader)
{
    switch (modLoader) {
        case 1:
            return QStringLiteral("forge");
        case 4:
            return QStringLiteral("fabric");
        case 5:
            return QStringLiteral("quilt");
        case 6:
            return QStringLiteral("neoforge");
        default:
            return {};  // 0 = any, 2 = Cauldron, 3 = LiteLoader: not shown
    }
}

// ZERO: presentation fields of a CurseForge mod (`/mods/search` `data[]`, `/mods/<id>` `data`, `/mods` POST).
void loadPresentation(ModPlatform::IndexedPack& pack, const QJsonObject& obj)
{
    pack.downloads = ModPlatform::countFromJson(obj["downloadCount"]);

    pack.categories.clear();
    pack.categoryIds.clear();
    for (const auto& v : obj["categories"].toArray()) {
        const auto cat = v.toObject();
        const auto name = cat["name"].toString();
        const auto id = ModPlatform::countFromJson(cat["id"]);
        if (name.isEmpty() || id < 0 || pack.categoryIds.contains(QString::number(id))) {
            continue;
        }
        pack.categories.append(name);
        pack.categoryIds.append(QString::number(id));
    }

    pack.gallery.clear();
    for (const auto& v : obj["screenshots"].toArray()) {
        const auto shot = v.toObject();
        const auto url = shot["url"].toString();
        if (url.isEmpty()) {
            continue;
        }
        const auto thumbnail = shot["thumbnailUrl"].toString();
        pack.gallery.append({ .url = url,
                              .thumbnailUrl = thumbnail.isEmpty() ? url : thumbnail,
                              .title = shot["title"].toString(),
                              .description = shot["description"].toString(),
                              .featured = false });
    }
    pack.featuredImageUrl.clear();  // CurseForge has no featured screenshot

    pack.gameVersions.clear();
    pack.loaderNames.clear();
    for (const auto& v : obj["latestFilesIndexes"].toArray()) {
        const auto index = v.toObject();
        if (const auto gameVersion = index["gameVersion"].toString(); !gameVersion.isEmpty() && !pack.gameVersions.contains(gameVersion)) {
            pack.gameVersions.append(gameVersion);
        }
        if (const auto loader = loaderNameFromFlame(index["modLoader"].toInt(0)); !loader.isEmpty() && !pack.loaderNames.contains(loader)) {
            pack.loaderNames.append(loader);
        }
    }

    pack.dateCreated = ModPlatform::dateFromJson(obj["dateCreated"]);
    pack.dateModified = ModPlatform::dateFromJson(obj["dateModified"]);
}
}  // namespace

namespace Flame::Parse {
Result<> loadIndexedPack(ModPlatform::IndexedPack& pack, const QJsonObject& obj)
{
    TRY_INTO(pack.addonId, Json::requireInteger(obj, "id"))
    pack.provider = ModPlatform::ResourceProvider::FLAME;
    TRY_INTO(pack.name, Json::requireString(obj, "name"))
    TRY_INTO(pack.slug, Json::requireString(obj, "slug"))
    pack.websiteUrl = obj["links"].toObject()["websiteUrl"].toString("");
    pack.description = obj["summary"].toString("");

    QJsonObject logo = obj["logo"].toObject();
    pack.logoName = logo["title"].toString();
    pack.logoUrl = logo["thumbnailUrl"].toString();
    if (pack.logoUrl.isEmpty()) {
        pack.logoUrl = logo["url"].toString();
    }

    auto authors = obj["authors"].toArray();
    if (!authors.isEmpty()) {
        pack.authors.clear();
        for (auto authorIter : authors) {
            TRY_INTO(const auto& author, Json::requireObject(authorIter))
            ModPlatform::ModpackAuthor packAuthor;
            TRY_INTO(packAuthor.name, Json::requireString(author, "name"))
            TRY_INTO(packAuthor.url, Json::requireString(author, "url"))
            pack.authors.append(packAuthor);
        }
    }

    pack.resourceType = FlameAPI::getResourceType(obj["classId"].toInt(0));
    pack.extraDataLoaded = false;

    auto linksObj = obj["links"].toObject();

    pack.extraData.issuesUrl = linksObj["issuesUrl"].toString();
    if (pack.extraData.issuesUrl.endsWith('/')) {
        pack.extraData.issuesUrl.chop(1);
    }

    pack.extraData.sourceUrl = linksObj["sourceUrl"].toString();
    if (pack.extraData.sourceUrl.endsWith('/')) {
        pack.extraData.sourceUrl.chop(1);
    }

    pack.extraData.wikiUrl = linksObj["wikiUrl"].toString();
    if (pack.extraData.wikiUrl.endsWith('/')) {
        pack.extraData.wikiUrl.chop(1);
    }

    if (!pack.extraData.body.isEmpty()) {
        pack.extraDataLoaded = true;
    }

    loadPresentation(pack, obj);  // ZERO: Discover cards and details
    return {};
}
}  // namespace Flame::Parse
