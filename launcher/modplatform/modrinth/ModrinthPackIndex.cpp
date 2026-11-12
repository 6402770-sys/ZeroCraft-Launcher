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

#include "ModrinthPackIndex.h"
#include "FileSystem.h"

#include <QSet>
#include <algorithm>

#include "Json.h"
#include "modplatform/ModIndex.h"
#include "modplatform/ResourceType.h"

namespace {
bool shouldDownloadOnSide(const QString& side)
{
    return side == "required" || side == "optional";
}

Result<> loadExtraPackData(ModPlatform::IndexedPack& pack, const QJsonObject& obj)
{
    pack.extraData.issuesUrl = obj["issues_url"].toString();
    if (pack.extraData.issuesUrl.endsWith('/')) {
        pack.extraData.issuesUrl.chop(1);
    }

    pack.extraData.sourceUrl = obj["source_url"].toString();
    if (pack.extraData.sourceUrl.endsWith('/')) {
        pack.extraData.sourceUrl.chop(1);
    }

    pack.extraData.wikiUrl = obj["wiki_url"].toString();
    if (pack.extraData.wikiUrl.endsWith('/')) {
        pack.extraData.wikiUrl.chop(1);
    }

    pack.extraData.discordUrl = obj["discord_url"].toString();
    if (pack.extraData.discordUrl.endsWith('/')) {
        pack.extraData.discordUrl.chop(1);
    }

    auto donateArr = obj["donation_urls"].toArray();
    for (auto d : donateArr) {
        TRY_INTO(const auto& dObj, Json::requireObject(d))

        ModPlatform::DonationData donate;

        donate.id = dObj["id"].toString();
        donate.platform = dObj["platform"].toString();
        donate.url = dObj["url"].toString();

        pack.extraData.donate.append(donate);
    }

    pack.extraData.status = obj["status"].toString();

    pack.extraData.body = obj["body"].toString().remove("<br>");

    pack.extraDataLoaded = !pack.extraData.body.isEmpty();
    return {};
}

QString mapMCVersionFromModrinth(QString v)
{
    static const QString s_preString = " Pre-Release ";
    bool pre = false;
    if (v.contains("-pre")) {
        pre = true;
        v.replace("-pre", s_preString);
    }
    v.replace("-", " ");
    if (pre) {
        v.replace(" Pre Release ", s_preString);
    }
    return v;
}

const auto g_resourceTypeMap = std::array{
    std::pair{ ModPlatform::ResourceType::Mod, "mod" },           std::pair{ ModPlatform::ResourceType::ResourcePack, "resourcepack" },
    std::pair{ ModPlatform::ResourceType::ShaderPack, "shader" }, std::pair{ ModPlatform::ResourceType::DataPack, "datapack" },
    std::pair{ ModPlatform::ResourceType::Modpack, "modpack" },
};

ModPlatform::ResourceType getResourceType(const QString& param)
{
    for (const auto& [key, value] : g_resourceTypeMap) {
        if (value == param) {
            return key;
        }
    }

    qWarning() << "Invalid resource type for Modrinth API!" << param;
    return ModPlatform::ResourceType::Unknown;
}

QString getAuthorURL(const QString& name)
{
    return "https://modrinth.com/user/" + name;
};

// ZERO: appends the strings of a JSON array to `out`, skipping non-strings, empty strings and duplicates.
void appendUniqueStrings(QStringList& out, const QJsonValue& value)
{
    for (const auto& v : value.toArray()) {
        if (const auto str = v.toString(); !str.isEmpty() && !out.contains(str)) {
            out.append(str);
        }
    }
}

// ZERO: Modrinth search hits mix category slugs and loader names in `categories` / `display_categories`; these are the
// /tag/loader names (mods, plugins, packs, shaders, resource packs) used to split them.
bool isModrinthLoaderName(const QString& name)
{
    static const QSet<QString> s_loaders = {
        "babric",     "bta-babric", "bukkit",    "bungeecord", "canvas",   "datapack", "fabric",    "folia",
        "forge",      "geyser",     "iris",      "java-agent", "legacy-fabric",        "liteloader", "minecraft",
        "modloader",  "neoforge",   "nilloader", "optifine",   "ornithe",  "paper",    "purpur",    "quilt",
        "rift",       "spigot",     "sponge",    "vanilla",    "velocity", "waterfall",
    };
    return s_loaders.contains(name);
}

// ZERO: presentation fields of a search hit (`/search` `hits[]`).
void loadHitPresentation(ModPlatform::IndexedPack& pack, const QJsonObject& obj)
{
    pack.downloads = ModPlatform::countFromJson(obj["downloads"]);
    pack.follows = ModPlatform::countFromJson(obj["follows"]);

    QStringList tags;
    appendUniqueStrings(tags, obj["categories"]);
    appendUniqueStrings(tags, obj["display_categories"]);
    for (const auto& tag : tags) {
        (isModrinthLoaderName(tag) ? pack.loaderNames : pack.categories).append(tag);
    }

    appendUniqueStrings(pack.gameVersions, obj["versions"]);

    pack.featuredImageUrl = obj["featured_gallery"].toString();
    for (const auto& v : obj["gallery"].toArray()) {
        const auto url = v.toString();
        if (url.isEmpty()) {
            continue;
        }
        pack.gallery.append({ .url = url, .thumbnailUrl = url, .title = {}, .description = {},
                              .featured = !pack.featuredImageUrl.isEmpty() && url == pack.featuredImageUrl });
    }

    pack.dateCreated = ModPlatform::dateFromJson(obj["date_created"]);
    pack.dateModified = ModPlatform::dateFromJson(obj["date_modified"]);
    pack.license = obj["license"].toString();
}

// ZERO: presentation fields of a project (`/project/<id>`, `/projects?ids=`).
void loadProjectPresentation(ModPlatform::IndexedPack& pack, const QJsonObject& obj)
{
    pack.downloads = ModPlatform::countFromJson(obj["downloads"]);
    pack.follows = ModPlatform::countFromJson(obj["followers"]);

    appendUniqueStrings(pack.categories, obj["categories"]);
    appendUniqueStrings(pack.categories, obj["additional_categories"]);
    appendUniqueStrings(pack.loaderNames, obj["loaders"]);
    appendUniqueStrings(pack.gameVersions, obj["game_versions"]);

    struct Entry {
        ModPlatform::GalleryImage image;
        int ordering = 0;
    };
    QList<Entry> entries;
    for (const auto& v : obj["gallery"].toArray()) {
        const auto g = v.toObject();
        const auto url = g["url"].toString();
        if (url.isEmpty()) {
            continue;
        }
        entries.append({ .image = { .url = url,
                                    .thumbnailUrl = url,
                                    .title = g["title"].toString(),
                                    .description = g["description"].toString(),
                                    .featured = g["featured"].toBool(false) },
                         .ordering = g["ordering"].toInt(0) });
    }
    std::ranges::stable_sort(entries, {}, &Entry::ordering);
    std::ranges::stable_partition(entries, [](const Entry& e) { return e.image.featured; });
    for (const auto& e : entries) {
        pack.gallery.append(e.image);
        if (e.image.featured && pack.featuredImageUrl.isEmpty()) {
            pack.featuredImageUrl = e.image.url;
        }
    }

    pack.dateCreated = ModPlatform::dateFromJson(obj["published"]);
    pack.dateModified = ModPlatform::dateFromJson(obj["updated"]);
    pack.license = obj["license"].toObject()["id"].toString();
}

}  // namespace

namespace Modrinth::Parse {

// https://docs.modrinth.com/api/operations/getproject/
Result<> loadIndexedPack(ModPlatform::IndexedPack& pack, const QJsonObject& obj)
{
    pack.addonId = obj["project_id"].toString();
    if (pack.addonId.toString().isEmpty()) {
        TRY_INTO(pack.addonId, Json::requireString(obj, "id"))
    }

    pack.provider = ModPlatform::ResourceProvider::MODRINTH;
    TRY_INTO(pack.name, Json::requireString(obj, "title"))
    pack.resourceType = getResourceType(obj["project_type"].toString());
    if ((obj.contains("loaders") && obj.value("loaders").toArray({}).contains("datapack")) ||
        (obj.contains("all_project_types") && obj.value("all_project_types").toArray({}).contains("datapack"))) {
        pack.resourceType = ModPlatform::ResourceType::DataPack;
    }

    pack.slug = obj["slug"].toString("");
    if (!pack.slug.isEmpty()) {
        pack.websiteUrl = "https://modrinth.com/mod/" + pack.slug;
    } else {
        pack.websiteUrl = "";
    }

    pack.description = obj["description"].toString("");

    pack.logoUrl = obj["icon_url"].toString("");
    pack.logoName = QString("%1.%2").arg(obj["slug"].toString(), QFileInfo(QUrl(pack.logoUrl).fileName()).suffix());

    if (obj.contains("author")) {
        ModPlatform::ModpackAuthor modAuthor;
        modAuthor.name = obj["author"].toString();
        modAuthor.url = getAuthorURL(modAuthor.name);
        pack.authors = { modAuthor };
    }

    auto client = shouldDownloadOnSide(obj["client_side"].toString());
    auto server = shouldDownloadOnSide(obj["server_side"].toString());

    if (server && client) {
        pack.side = ModPlatform::SideType::UniversalSide;
    } else if (server) {
        pack.side = ModPlatform::SideType::ServerSide;
    } else if (client) {
        pack.side = ModPlatform::SideType::ClientSide;
    }

    // ZERO: presentation fields. A search hit carries `project_id`; a project object (`/project`, `/projects`) only `id`.
    pack.categories.clear();
    pack.loaderNames.clear();
    pack.gameVersions.clear();
    pack.gallery.clear();
    pack.featuredImageUrl.clear();
    if (obj.contains("project_id")) {
        loadHitPresentation(pack, obj);
    } else {
        loadProjectPresentation(pack, obj);
    }
    pack.categoryIds = pack.categories;

    // Modrinth can have more data than what's provided by the basic search :)
    pack.extraDataLoaded = false;
    return loadExtraPackData(pack, obj);
}

QString resourceTypeParameter(ModPlatform::ResourceType type)
{
    for (const auto& [key, value] : g_resourceTypeMap) {
        if (key == type) {
            return value;
        }
    }

    qWarning() << "Invalid resource type for Modrinth API!" << static_cast<std::uint8_t>(type);
    return "";
}
}  // namespace Modrinth::Parse

Result<ModPlatform::IndexedVersion> Modrinth::loadIndexedPackVersion(const QJsonObject& obj,
                                                                     const QString& preferredHashType,
                                                                     const QString& preferredFileName)
{
    ModPlatform::IndexedVersion file;

    TRY_INTO(file.addonId, Json::requireString(obj, "project_id"))
    TRY_INTO(file.fileId, Json::requireString(obj, "id"))
    TRY_INTO(file.date, Json::requireString(obj, "date_published"))
    TRY_INTO(const auto& versionArray, Json::requireArray(obj, "game_versions"))
    if (versionArray.empty()) {
        return {};
    }
    for (auto mcVer : versionArray) {
        file.mcVersion.append(
            { mapMCVersionFromModrinth(mcVer.toString()), mcVer.toString() });  // double this so we can check both strings when filtering
    }
    TRY_INTO(const auto& loaders, Json::requireArray(obj, "loaders"))
    for (auto loader : loaders) {
        if (loader == "neoforge") {
            file.loaders |= ModPlatform::NeoForge;
        } else if (loader == "forge") {
            file.loaders |= ModPlatform::Forge;
        } else if (loader == "cauldron") {
            file.loaders |= ModPlatform::Cauldron;
        } else if (loader == "liteloader") {
            file.loaders |= ModPlatform::LiteLoader;
        } else if (loader == "fabric") {
            file.loaders |= ModPlatform::Fabric;
        } else if (loader == "quilt") {
            file.loaders |= ModPlatform::Quilt;
        }
    }
    TRY_INTO(file.version, Json::requireString(obj, "name"))
    TRY_INTO(file.versionNumber, Json::requireString(obj, "version_number"))
    TRY_INTO(const auto& versionType, Json::requireString(obj, "version_type"))
    file.versionType = ModPlatform::IndexedVersionType::fromString(versionType);

    if (obj.contains("changelog")) {
        TRY_INTO(file.changelog, Json::requireString(obj, "changelog"))
    }

    auto dependencies = obj["dependencies"].toArray();
    for (auto d : dependencies) {
        auto dep = d.toObject();
        ModPlatform::Dependency dependency;
        dependency.addonId = dep["project_id"].toString();
        dependency.version = dep["version_id"].toString();
        TRY_INTO(const auto& depType, Json::requireString(dep, "dependency_type"))

        if (depType == "required") {
            dependency.type = ModPlatform::DependencyType::REQUIRED;
        } else if (depType == "optional") {
            dependency.type = ModPlatform::DependencyType::OPTIONAL;
        } else if (depType == "incompatible") {
            dependency.type = ModPlatform::DependencyType::INCOMPATIBLE;
        } else if (depType == "embedded") {
            dependency.type = ModPlatform::DependencyType::EMBEDDED;
        } else {
            dependency.type = ModPlatform::DependencyType::UNKNOWN;
        }

        file.dependencies.append(dependency);
    }

    TRY_INTO(const auto& files, Json::requireArray(obj, "files"))
    int i = 0;

    if (files.empty()) {
        // This should not happen normally, but check just in case
        qWarning() << "Modrinth returned an unexpected empty list of files:" << obj;
        return {};
    }

    // Find correct file (needed in cases where one version may have multiple files)
    // Will default to the last one if there's no primary (though I think Modrinth requires that
    // at least one file is primary, idk)
    // NOTE: files.count() is 1-indexed, so we need to subtract 1 to become 0-indexed
    while (i < files.count() - 1) {
        auto parent = files[i].toObject();
        TRY_INTO(const auto& fileName, Json::requireString(parent, "filename"))

        if (!preferredFileName.isEmpty() && fileName.contains(preferredFileName)) {
            file.isPreferred = true;
            break;
        }

        TRY_INTO(const auto& primary, Json::requireBoolean(parent, "primary"))
        // Grab the primary file, if available
        if (primary) {
            break;
        }

        i++;
    }

    auto parent = files[i].toObject();
    if (parent.contains("url")) {
        TRY_INTO(file.downloadUrl, Json::requireString(parent, "url"))
        TRY_INTO(file.fileName, Json::requireString(parent, "filename"))
        file.fileName = FS::RemoveInvalidPathChars(file.fileName);
        file.fileSize = ModPlatform::countFromJson(parent["size"]);  // ZERO: shown in the version list
        file.downloads = ModPlatform::countFromJson(obj["downloads"]);  // ZERO: shown in the version list
        TRY_INTO(const auto& primary, Json::requireBoolean(parent, "primary"))
        file.isPreferred = primary || (files.count() == 1);
        auto hashList = Json::requireObject(parent, "hashes");
        TRY(hashList)

        if (hashList->contains(preferredHashType)) {
            TRY_INTO(file.hash, Json::requireString(hashList.value(), preferredHashType))
            file.hashType = preferredHashType;
        } else {
            auto hashTypes = ModPlatform::ProviderCapabilities::hashType(ModPlatform::ResourceProvider::MODRINTH);
            for (auto& hashType : hashTypes) {
                if (hashList->contains(hashType)) {
                    TRY_INTO(file.hash, Json::requireString(hashList.value(), hashType))
                    file.hashType = hashType;
                    break;
                }
            }
        }

        return file;
    }

    return {};
}
