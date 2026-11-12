// SPDX-License-Identifier: GPL-3.0-only
// ContentMetadata: the ZERO presentation fields parsed from Modrinth / CurseForge responses, category headers, the
// project-details merge, and the search URLs of the existing argument shapes (pinned so ZERO additions never change them).
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QTimeZone>

#include "Version.h"
#include "modplatform/ResourceAPI.h"
#include "modplatform/flame/FlameAPI.h"
#include "modplatform/flame/FlameModIndex.h"
#include "modplatform/flame/FlamePackIndex.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "modplatform/modrinth/ModrinthPackIndex.h"

namespace {

QUrl modrinthSearchUrl(const ResourceAPI::SearchArgs& args)
{
    return ModrinthAPI::get().searchProjects(args).first.url;
}

QUrl flameSearchUrl(const ResourceAPI::SearchArgs& args)
{
    return FlameAPI::get().searchProjects(args).first.url;
}

QByteArray fixture(const QString& name)
{
    QFile f(QFINDTESTDATA("testdata/ContentMetadata/" + name));
    if (!f.open(QIODevice::ReadOnly)) {
        qFatal("missing fixture %s", qPrintable(name));
    }
    return f.readAll();
}

QJsonObject fixtureObject(const QString& name)
{
    return QJsonDocument::fromJson(fixture(name)).object();
}

QList<ModPlatform::IndexedPack> modrinthSearch(const QByteArray& response)
{
    auto res = ModrinthAPI::get().searchProjects({ .type = ModPlatform::ResourceType::Mod }).second(response);
    if (!res) {
        qWarning() << res.error();
        return {};
    }
    return *res;
}

QList<ModPlatform::IndexedPack> flameSearch(const QByteArray& response)
{
    auto res = FlameAPI::get().searchProjects({ .type = ModPlatform::ResourceType::Mod }).second(response);
    if (!res) {
        qWarning() << res.error();
        return {};
    }
    return *res;
}

QDateTime utc(int y, int mo, int d, int h, int mi, int s, int ms = 0)
{
    return QDateTime(QDate(y, mo, d), QTime(h, mi, s, ms), QTimeZone::UTC);
}

void verifyNoPresentation(const ModPlatform::IndexedPack& pack)
{
    QCOMPARE(pack.downloads, -1);
    QCOMPARE(pack.follows, -1);
    QVERIFY(pack.categories.isEmpty());
    QVERIFY(pack.categoryIds.isEmpty());
    QVERIFY(pack.gameVersions.isEmpty());
    QVERIFY(pack.loaderNames.isEmpty());
    QVERIFY(pack.gallery.isEmpty());
    QVERIFY(pack.featuredImageUrl.isEmpty());
    QVERIFY(pack.dateCreated.isNull());
    QVERIFY(pack.dateModified.isNull());
    QVERIFY(pack.license.isEmpty());
}

QStringList idsWithHeader(const QList<ModPlatform::Category>& cats)
{
    QStringList out;
    for (const auto& c : cats) {
        out << c.id + "/" + c.header;
    }
    return out;
}

}  // namespace

class ContentMetadataTest : public QObject {
    Q_OBJECT

   private slots:
    // ---- Existing URL shapes (must stay byte-identical) ----

    void modrinthSearchUrl_fullOldShape()
    {
        ResourceAPI::SearchArgs args{
            .type = ModPlatform::ResourceType::Mod,
            .offset = 25,
            .search = QStringLiteral("sodium"),
            .sorting = ResourceAPI::SortingMethod{ .index = 2, .name = QStringLiteral("downloads"), .readableName = {} },
            .loaders = ModPlatform::ModLoaderTypes(ModPlatform::Fabric | ModPlatform::Quilt),
            .versions = std::vector<Version>{ Version(QStringLiteral("1.20.1")) },
            .side = ModPlatform::SideType(ModPlatform::SideType::ClientSide),
            .categoryIds = QStringList{ QStringLiteral("optimization"), QStringLiteral("utility") },
            .openSource = true,
            .excludeDisclosureTypes = { ModPlatform::DisclosureType(ModPlatform::DisclosureType::Advertisements) },
        };
        const QString expected = QStringLiteral(
            "https://api.modrinth.com/v2/search?offset=25&limit=25&query=sodium&index=downloads&facets=["
            R"(["categories:fabric","categories:quilt"],["versions:1.20.1"],)"
            R"(["environment:client_only","environment:client_only_server_optional","environment:singleplayer_only",)"
            R"("environment:client_or_server","environment:client_or_server_prefers_both"],)"
            R"(["categories:optimization"],["categories:utility"],["disclosure_types!=advertisements"],["open_source:true"],)"
            R"(["project_type:mod"]])");
        const QUrl url = modrinthSearchUrl(args);
        QCOMPARE(url.toString(), QUrl(expected).toString());
        QCOMPARE(url, QUrl(expected));
    }

    void modrinthSearchUrl_modpackPageShape()
    {
        // The shape ModrinthPage's ModpackListModel sends.
        ResourceAPI::SearchArgs args{
            .type = ModPlatform::ResourceType::Modpack,
            .offset = 0,
            .search = QString(),
            .sorting = ResourceAPI::SortingMethod{ .index = 0, .name = QStringLiteral("relevance"), .readableName = {} },
            .loaders = std::nullopt,
            .versions = std::nullopt,
            .side = ModPlatform::SideType(ModPlatform::SideType::NoSide),
            .categoryIds = std::nullopt,
            .openSource = false,
            .excludeDisclosureTypes = {},
        };
        const QString expected =
            QStringLiteral(R"(https://api.modrinth.com/v2/search?offset=0&limit=25&query=&index=relevance&facets=[["project_type:modpack"]])");
        QCOMPARE(modrinthSearchUrl(args), QUrl(expected));
    }

    void flameSearchUrl_fullOldShape()
    {
        ResourceAPI::SearchArgs args{
            .type = ModPlatform::ResourceType::Modpack,
            .offset = 50,
            .search = QStringLiteral("all the mods"),
            .sorting = ResourceAPI::SortingMethod{ .index = 2, .name = {}, .readableName = {} },
            .loaders = ModPlatform::ModLoaderTypes(ModPlatform::Forge | ModPlatform::NeoForge),
            .versions = std::vector<Version>{ Version(QStringLiteral("1.20.1")), Version(QStringLiteral("1.19.2")) },
            .side = ModPlatform::SideType(ModPlatform::SideType::NoSide),
            .categoryIds = QStringList{ QStringLiteral("4472"), QStringLiteral("4473") },
            .openSource = false,
            .excludeDisclosureTypes = {},
        };
        const QString expected = QStringLiteral(
            "https://api.curseforge.com/v1/mods/search?gameId=432&classId=4471&index=50&pageSize=25&searchFilter=all the mods"
            "&sortField=2&sortOrder=desc&modLoaderTypes=[6,1]&categoryIds=[4472,4473]&gameVersion=1.20.1");
        const QUrl url = flameSearchUrl(args);
        QCOMPARE(url.toString(), QUrl(expected).toString());
        QCOMPARE(url, QUrl(expected));
    }

    void flameSearchUrl_minimalShape()
    {
        ResourceAPI::SearchArgs args{ .type = ModPlatform::ResourceType::ShaderPack };
        QCOMPARE(flameSearchUrl(args),
                 QUrl(QStringLiteral("https://api.curseforge.com/v1/mods/search?gameId=432&classId=6552&index=0&pageSize=25&sortOrder=desc")));
    }

    // ---- ZERO additions to the search URL ----

    void modrinthSearchUrl_categoryGroupsAreOrInsideAndAcross()
    {
        ResourceAPI::SearchArgs args{ .type = ModPlatform::ResourceType::ResourcePack };
        args.categoryIds = QStringList{ "legacy" };
        args.categoryGroups = QList<QStringList>{ { "16x", "32x" }, {}, { "audio" } };
        const QString expected = QStringLiteral(
            "https://api.modrinth.com/v2/search?offset=0&limit=25&facets=["
            R"(["categories:legacy"],["categories:16x","categories:32x"],["categories:audio"],["project_type:resourcepack"]])");
        QCOMPARE(modrinthSearchUrl(args), QUrl(expected));
    }

    void flameSearchUrl_ignoresCategoryGroups()
    {
        ResourceAPI::SearchArgs args{ .type = ModPlatform::ResourceType::ShaderPack };
        args.categoryGroups = QList<QStringList>{ { "iris" } };
        QCOMPARE(flameSearchUrl(args),
                 QUrl(QStringLiteral("https://api.curseforge.com/v1/mods/search?gameId=432&classId=6552&index=0&pageSize=25&sortOrder=desc")));
    }

    // ---- Modrinth search hit ----

    void modrinthHit_fullFields()
    {
        const auto packs = modrinthSearch(fixture("modrinth-search.json"));
        QCOMPARE(packs.size(), 3);
        const auto& p = packs.at(0);
        QCOMPARE(p.name, QStringLiteral("Sodium"));
        QCOMPARE(p.authors.size(), 1);
        QCOMPARE(p.authors.first().name, QStringLiteral("jellysquid3"));
        QCOMPARE(p.downloads, 98765432);
        QCOMPARE(p.follows, 34567);
        // Loader names are split out; display_categories are merged without duplicates, order kept.
        QCOMPARE(p.loaderNames, (QStringList{ "fabric", "neoforge", "quilt", "iris" }));
        QCOMPARE(p.categories, (QStringList{ "optimization", "utility" }));
        QCOMPARE(p.categoryIds, p.categories);
        QCOMPARE(p.gameVersions, (QStringList{ "1.20.1", "1.20.4", "1.21" }));
        QCOMPARE(p.gallery.size(), 2);
        QCOMPARE(p.gallery.at(0).url, QStringLiteral("https://cdn.modrinth.com/data/AANobbMI/images/one_350.webp"));
        QCOMPARE(p.gallery.at(0).thumbnailUrl, p.gallery.at(0).url);
        QVERIFY(!p.gallery.at(0).featured);
        QVERIFY(p.gallery.at(1).featured);
        QCOMPARE(p.featuredImageUrl, QStringLiteral("https://cdn.modrinth.com/data/AANobbMI/images/two_350.webp"));
        QCOMPARE(p.dateCreated, utc(2020, 11, 4, 0, 53, 12, 383));
        QCOMPARE(p.dateModified, utc(2026, 10, 30, 18, 22, 7, 123));
        QCOMPARE(p.license, QStringLiteral("LicenseRef-Polyform-Shield-1.0.0"));
    }

    void modrinthHit_missingFieldsKeepDefaults()
    {
        const auto packs = modrinthSearch(fixture("modrinth-search.json"));
        QCOMPARE(packs.size(), 3);
        QCOMPARE(packs.at(1).name, QStringLiteral("Minimal pack"));
        QVERIFY(packs.at(1).authors.isEmpty());
        verifyNoPresentation(packs.at(1));
    }

    void modrinthHit_wrongTypesAndNullsKeepDefaults()
    {
        const auto packs = modrinthSearch(fixture("modrinth-search.json"));
        QCOMPARE(packs.size(), 3);
        const auto& p = packs.at(2);
        QCOMPARE(p.name, QStringLiteral("Bad types"));
        QCOMPARE(p.downloads, -1);        // a string
        QCOMPARE(p.follows, -1);          // negative
        QVERIFY(p.categories.isEmpty());  // not an array
        QVERIFY(p.loaderNames.isEmpty());
        QCOMPARE(p.gameVersions, QStringList{ "1.20.1" });  // non-strings skipped
        QVERIFY(p.gallery.isEmpty());
        QVERIFY(p.featuredImageUrl.isEmpty());
        QVERIFY(p.dateCreated.isNull());  // not a date
        QVERIFY(p.dateModified.isNull());
        QVERIFY(p.license.isEmpty());  // an object where a hit has a string
    }

    // ---- Modrinth project ----

    void modrinthProject_fullFields()
    {
        auto res = ModrinthAPI::get().getProject("AANobbMI").second(fixture("modrinth-project.json"));
        QVERIFY(res.has_value());
        const auto& p = *res;
        QVERIFY(p.authors.isEmpty());  // /project has no author
        QCOMPARE(p.downloads, 98765433);
        QCOMPARE(p.follows, 34568);
        QCOMPARE(p.categories, (QStringList{ "optimization", "utility", "decoration" }));
        QCOMPARE(p.loaderNames, (QStringList{ "fabric", "neoforge", "quilt" }));
        QCOMPARE(p.gameVersions, (QStringList{ "1.20.1", "1.20.4", "1.21" }));  // not the version ids of `versions`
        // Featured first, then by `ordering`.
        QCOMPARE(p.gallery.size(), 4);
        QCOMPARE(p.gallery.at(0).url, QStringLiteral("https://cdn.modrinth.com/c.webp"));
        QVERIFY(p.gallery.at(0).featured);
        QCOMPARE(p.gallery.at(0).title, QStringLiteral("Hero"));
        QCOMPARE(p.gallery.at(0).description, QStringLiteral("The featured one"));
        QCOMPARE(p.gallery.at(1).url, QStringLiteral("https://cdn.modrinth.com/b.webp"));
        QVERIFY(p.gallery.at(1).title.isEmpty());  // null
        QCOMPARE(p.gallery.at(2).url, QStringLiteral("https://cdn.modrinth.com/d.webp"));
        QCOMPARE(p.gallery.at(3).url, QStringLiteral("https://cdn.modrinth.com/a.webp"));
        QCOMPARE(p.gallery.at(3).thumbnailUrl, QStringLiteral("https://cdn.modrinth.com/a.webp"));  // raw_url ignored
        QCOMPARE(p.featuredImageUrl, QStringLiteral("https://cdn.modrinth.com/c.webp"));
        QCOMPARE(p.dateCreated, utc(2020, 11, 4, 0, 53, 12, 383));
        QCOMPARE(p.dateModified, utc(2026, 10, 30, 18, 22, 7));
        QCOMPARE(p.license, QStringLiteral("LicenseRef-Polyform-Shield-1.0.0"));
    }

    void modrinthProject_missingAndWrongTypes()
    {
        ModPlatform::IndexedPack minimal;
        QVERIFY(Modrinth::Parse::loadIndexedPack(minimal, QJsonObject{ { "id", "x" }, { "title", "X" }, { "project_type", "mod" } }).has_value());
        verifyNoPresentation(minimal);

        auto obj = fixtureObject("modrinth-project.json");
        obj["downloads"] = QJsonValue::Null;
        obj["followers"] = "12";
        obj["categories"] = 5;
        obj["additional_categories"] = QJsonValue::Null;
        obj["loaders"] = QJsonArray{ 1, "fabric" };
        obj["game_versions"] = QJsonObject{};
        obj["gallery"] = QJsonArray{ QJsonObject{ { "url", 3 } }, "https://cdn.modrinth.com/string.webp" };
        obj["license"] = "MIT";  // a string where a project has an object
        obj["published"] = 17;
        obj["updated"] = "";
        ModPlatform::IndexedPack p;
        QVERIFY(Modrinth::Parse::loadIndexedPack(p, obj).has_value());
        QCOMPARE(p.downloads, -1);
        QCOMPARE(p.follows, -1);
        QVERIFY(p.categories.isEmpty());
        QCOMPARE(p.loaderNames, QStringList{ "fabric" });
        QVERIFY(p.gameVersions.isEmpty());
        QVERIFY(p.gallery.isEmpty());
        QVERIFY(p.license.isEmpty());
        QVERIFY(p.dateCreated.isNull());
        QVERIFY(p.dateModified.isNull());
    }

    void modrinthParse_reparseDoesNotDuplicate()
    {
        const auto obj = fixtureObject("modrinth-project.json");
        ModPlatform::IndexedPack p;
        QVERIFY(Modrinth::Parse::loadIndexedPack(p, obj).has_value());
        QVERIFY(Modrinth::Parse::loadIndexedPack(p, obj).has_value());
        QCOMPARE(p.categories.size(), 3);
        QCOMPARE(p.gallery.size(), 4);
    }

    // ---- Versions ----

    void modrinthVersion_sizeAndDownloads()
    {
        auto obj = fixtureObject("modrinth-version.json");
        auto res = Modrinth::loadIndexedPackVersion(obj);
        QVERIFY(res.has_value());
        QCOMPARE(res->fileName, QStringLiteral("sodium.jar"));
        QCOMPARE(res->fileSize, 1032147);  // the primary (chosen) file's size
        QCOMPARE(res->downloads, 1234567);

        obj.remove("downloads");
        auto files = obj["files"].toArray();
        auto primary = files[1].toObject();
        primary["size"] = "big";
        files[1] = primary;
        obj["files"] = files;
        res = Modrinth::loadIndexedPackVersion(obj);
        QVERIFY(res.has_value());
        QCOMPARE(res->fileSize, -1);
        QCOMPARE(res->downloads, -1);
    }

    void flameVersion_sizeAndDownloads()
    {
        auto obj = fixtureObject("flame-file.json");
        auto res = FlameMod::loadIndexedPackVersion(obj);
        QVERIFY(res.has_value());
        QCOMPARE(res->fileSize, 1234567);
        QCOMPARE(res->downloads, 8901234);

        obj.remove("fileLength");
        obj["downloadCount"] = QJsonValue::Null;
        res = FlameMod::loadIndexedPackVersion(obj);
        QVERIFY(res.has_value());
        QCOMPARE(res->fileSize, -1);
        QCOMPARE(res->downloads, -1);
    }

    // ---- CurseForge ----

    void flameSearch_fullFields()
    {
        const auto packs = flameSearch(fixture("flame-search.json"));
        QCOMPARE(packs.size(), 3);
        const auto& p = packs.at(0);
        QCOMPARE(p.downloads, 345678901);
        QCOMPARE(p.follows, -1);  // CurseForge has none
        QCOMPARE(p.categories, (QStringList{ "Map and Information", "API and Library" }));
        QCOMPARE(p.categoryIds, (QStringList{ "423", "421" }));
        QCOMPARE(p.gallery.size(), 2);
        QCOMPARE(p.gallery.at(0).url, QStringLiteral("https://media.forgecdn.net/s1.png"));
        QCOMPARE(p.gallery.at(0).thumbnailUrl, QStringLiteral("https://media.forgecdn.net/s1_thumb.png"));
        QCOMPARE(p.gallery.at(0).title, QStringLiteral("Recipes"));
        QCOMPARE(p.gallery.at(0).description, QStringLiteral("The recipe view"));
        QCOMPARE(p.gallery.at(1).thumbnailUrl, QStringLiteral("https://media.forgecdn.net/s2.png"));  // no thumbnail -> url
        QVERIFY(p.featuredImageUrl.isEmpty());
        QCOMPARE(p.gameVersions, (QStringList{ "1.21.1", "1.20.1", "1.12.2", "1.7.10" }));
        QCOMPARE(p.loaderNames, (QStringList{ "neoforge", "fabric", "forge" }));
        QCOMPARE(p.dateCreated, utc(2015, 11, 23, 19, 58, 58, 743));
        QCOMPARE(p.dateModified, utc(2026, 10, 21, 5, 40, 12));
        QVERIFY(p.license.isEmpty());
        QCOMPARE(p.authors.size(), 1);
    }

    void flameSearch_missingAndWrongTypes()
    {
        const auto packs = flameSearch(fixture("flame-search.json"));
        QCOMPARE(packs.size(), 3);
        verifyNoPresentation(packs.at(1));
        verifyNoPresentation(packs.at(2));
    }

    void flameMod_detailsEndpoint()
    {
        auto res = FlameAPI::get().getProject("238222").second(fixture("flame-mod.json"));
        QVERIFY(res.has_value());
        QCOMPARE(res->downloads, 345678902);
        QCOMPARE(res->categories, QStringList{ "Map and Information" });
        QCOMPARE(res->categoryIds, QStringList{ "423" });
        QVERIFY(res->gallery.isEmpty());
        QCOMPARE(res->gameVersions, QStringList{ "1.21.1" });
        QCOMPARE(res->loaderNames, QStringList{ "quilt" });
        QCOMPARE(res->dateCreated, utc(2015, 11, 23, 19, 58, 58, 743));
        QVERIFY(res->dateModified.isNull());  // "not a date"
    }

    // ---- Categories ----

    void modrinthCategories_perTypeWithHeader()
    {
        const auto response = fixture("modrinth-categories.json");
        QCOMPARE(idsWithHeader(ModrinthAPI::loadCategories(response, ModPlatform::ResourceType::Modpack)),
                 (QStringList{ "adventure/categories", "kitchen-sink/categories" }));
        QCOMPARE(idsWithHeader(ModrinthAPI::loadCategories(response, ModPlatform::ResourceType::Mod)),
                 (QStringList{ "game-mechanics/categories", "optimization/categories" }));
        QCOMPARE(idsWithHeader(ModrinthAPI::loadCategories(response, ModPlatform::ResourceType::ResourcePack)),
                 (QStringList{ "16x/resolutions", "8x-/resolutions", "audio/features", "vanilla-like/categories" }));
        QCOMPARE(idsWithHeader(ModrinthAPI::loadCategories(response, ModPlatform::ResourceType::ShaderPack)),
                 (QStringList{ "atmosphere/features", "pbr/features", "cartoon/categories", "potato/performance impact",
                               "high/performance impact", "screenshot/performance impact" }));
        // The existing string overload and the mod list the Prism pages use are unchanged (plus the header).
        QCOMPARE(ModrinthAPI::loadCategories(response, QStringLiteral("modpack")).size(), 2);
        const auto mod = ModrinthAPI::get().loadModCategories(response);
        QCOMPARE(idsWithHeader(mod), (QStringList{ "game-mechanics/categories", "optimization/categories" }));
        QCOMPARE(mod.first().name, QStringLiteral("game-mechanics"));
    }

    void flameCategories_parse()
    {
        const auto cats = FlameAPI::loadCategories(fixture("flame-categories.json"));
        QCOMPARE(cats.size(), 2);
        QCOMPARE(cats.at(0).id, QStringLiteral("4475"));
        QCOMPARE(cats.at(0).name, QStringLiteral("Adventure and RPG"));
        QVERIFY(cats.at(0).header.isEmpty());
        QCOMPARE(FlameAPI::get().loadModCategories(fixture("flame-categories.json")).size(), 2);
    }

    // ---- Details merge ----

    void merge_keepsSearchHitFieldsTheDetailsLack()
    {
        const auto hits = modrinthSearch(fixture("modrinth-search.json"));
        QCOMPARE(hits.size(), 3);
        ModPlatform::IndexedPack into = hits.at(0);

        // A details answer without any presentation field and without an author.
        ModPlatform::IndexedPack details;
        QVERIFY(Modrinth::Parse::loadIndexedPack(
                    details, QJsonObject{ { "id", "AANobbMI" }, { "title", "Sodium (details)" }, { "project_type", "mod" }, { "body", "Long text" } })
                    .has_value());
        ModPlatform::mergeProjectDetails(into, details);
        QCOMPARE(into.name, QStringLiteral("Sodium (details)"));
        QCOMPARE(into.extraData.body, QStringLiteral("Long text"));
        QCOMPARE(into.authors.size(), 1);
        QCOMPARE(into.authors.first().name, QStringLiteral("jellysquid3"));
        QCOMPARE(into.downloads, 98765432);
        QCOMPARE(into.follows, 34567);
        QCOMPARE(into.categories, (QStringList{ "optimization", "utility" }));
        QCOMPARE(into.categoryIds, into.categories);
        QCOMPARE(into.loaderNames, (QStringList{ "fabric", "neoforge", "quilt", "iris" }));
        QCOMPARE(into.gameVersions.size(), 3);
        QCOMPARE(into.gallery.size(), 2);
        QVERIFY(!into.featuredImageUrl.isEmpty());
        QVERIFY(into.dateCreated.isValid());
        QVERIFY(into.dateModified.isValid());
        QVERIFY(!into.license.isEmpty());
    }

    void merge_detailsWinWhenPresent()
    {
        const auto hits = modrinthSearch(fixture("modrinth-search.json"));
        QCOMPARE(hits.size(), 3);
        ModPlatform::IndexedPack into = hits.at(0);
        auto res = ModrinthAPI::get().getProject("AANobbMI").second(fixture("modrinth-project.json"));
        QVERIFY(res.has_value());
        ModPlatform::mergeProjectDetails(into, *res);
        QCOMPARE(into.authors.first().name, QStringLiteral("jellysquid3"));  // kept: /project has no author
        QCOMPARE(into.downloads, 98765433);
        QCOMPARE(into.follows, 34568);
        QCOMPARE(into.categories, (QStringList{ "optimization", "utility", "decoration" }));
        QCOMPARE(into.loaderNames, (QStringList{ "fabric", "neoforge", "quilt" }));
        QCOMPARE(into.gallery.size(), 4);
        QCOMPARE(into.featuredImageUrl, QStringLiteral("https://cdn.modrinth.com/c.webp"));
        QCOMPARE(into.dateModified, utc(2026, 10, 30, 18, 22, 7));

        // Authors of the details replace the hit's when present (CurseForge /mods/<id>).
        ModPlatform::IndexedPack withAuthor = *res;
        withAuthor.authors = { { .name = "other", .url = "u" } };
        ModPlatform::mergeProjectDetails(into, withAuthor);
        QCOMPARE(into.authors.size(), 1);
        QCOMPARE(into.authors.first().name, QStringLiteral("other"));
    }

    void merge_leavesVersionsToTheCaller()
    {
        ModPlatform::IndexedPack into;
        into.versionsLoaded = true;
        into.versions = { ModPlatform::IndexedVersion{} };
        ModPlatform::IndexedPack details;
        ModPlatform::mergeProjectDetails(into, details);
        // Same as the old `*project = *response`: the callers restore the versions themselves.
        QVERIFY(!into.versionsLoaded);
        QVERIFY(into.versions.isEmpty());
    }

    void lenientReaders()
    {
        QCOMPARE(ModPlatform::countFromJson(QJsonValue(42)), 42);
        QCOMPARE(ModPlatform::countFromJson(QJsonValue(0)), 0);
        QCOMPARE(ModPlatform::countFromJson(QJsonValue(3.0e9)), 3000000000LL);
        QCOMPARE(ModPlatform::countFromJson(QJsonValue(-1)), -1);
        QCOMPARE(ModPlatform::countFromJson(QJsonValue("5")), -1);
        QCOMPARE(ModPlatform::countFromJson(QJsonValue()), -1);
        QCOMPARE(ModPlatform::countFromJson(QJsonValue(QJsonValue::Undefined)), -1);
        QCOMPARE(ModPlatform::dateFromJson(QJsonValue("2024-02-29T10:00:00Z")), utc(2024, 2, 29, 10, 0, 0));
        QVERIFY(ModPlatform::dateFromJson(QJsonValue("2024-13-01T00:00:00Z")).isNull());
        QVERIFY(ModPlatform::dateFromJson(QJsonValue(true)).isNull());
    }
};

QTEST_GUILESS_MAIN(ContentMetadataTest)

#include "ContentMetadata_test.moc"
