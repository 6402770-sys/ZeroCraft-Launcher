// SPDX-License-Identifier: GPL-3.0-only
// ContentFilter: facets per section and provider (disabled reasons), chips, chip removal, Clear all vs Show everything,
// when a change needs a new search, the mapping to ResourceAPI::SearchArgs (Modrinth OR / AND groups, CurseForge first
// version), the client predicate (unknown data never excludes), performance / resolution helpers and the JSON round trip.
#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

#include "modplatform/flame/FlameAPI.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "zero/content/ContentFilter.h"

using namespace Zero::Content;

namespace {

const Facet* findFacet(const QList<Facet>& facets, const QString& id)
{
    for (const auto& f : facets) {
        if (f.id == id) {
            return &f;
        }
    }
    return nullptr;
}

QStringList ids(const QList<Facet>& facets)
{
    QStringList out;
    for (const auto& f : facets) {
        out << f.id;
    }
    return out;
}

QStringList optionValues(const Facet* f)
{
    QStringList out;
    if (f != nullptr) {
        for (const auto& o : f->options) {
            out << o.value;
        }
    }
    return out;
}

QStringList labels(const QList<Chip>& chips)
{
    QStringList out;
    for (const auto& c : chips) {
        out << c.label;
    }
    return out;
}

QList<ModPlatform::Category> modrinthShaderCategories()
{
    return { { .name = "atmosphere", .id = "atmosphere", .header = "features" },
             { .name = "pbr", .id = "pbr", .header = "features" },
             { .name = "cartoon", .id = "cartoon", .header = "categories" },
             { .name = "potato", .id = "potato", .header = "performance impact" },
             { .name = "screenshot", .id = "screenshot", .header = "performance impact" } };
}

QList<ModPlatform::Category> modrinthResourcePackCategories()
{
    return { { .name = "16x", .id = "16x", .header = "resolutions" },
             { .name = "audio", .id = "audio", .header = "features" },
             { .name = "vanilla-like", .id = "vanilla-like", .header = "categories" },
             { .name = "game-mechanics", .id = "game-mechanics", .header = "categories" } };
}

FacetContext ctx(Provider p, QList<ModPlatform::Category> cats = {}, bool hasTarget = true)
{
    FacetContext c;
    c.provider = p;
    c.categories = std::move(cats);
    c.mcVersions = { "1.21.1", "1.20.1", "1.19.2" };
    c.hasTarget = hasTarget;
    return c;
}

FilterState state(std::initializer_list<std::pair<QString, QStringList>> selected, const QString& query = {}, const QString& sort = {})
{
    FilterState s;
    s.query = query;
    s.sortId = sort;
    for (const auto& [k, v] : selected) {
        s.selected.insert(k, v);
    }
    return s;
}

}  // namespace

class ContentFilterTest : public QObject {
    Q_OBJECT

   private slots:
    // ---- Facets ----

    void facets_modpacks()
    {
        const auto modrinth = facetsFor(Kind::Modpack, ctx(Provider::Modrinth, { { .name = "adventure", .id = "adventure", .header = "categories" } }));
        QCOMPARE(ids(modrinth), (QStringList{ FacetId::provider, FacetId::mcVersion, FacetId::loader, FacetId::category, FacetId::installed }));
        const auto* provider = findFacet(modrinth, FacetId::provider);
        QCOMPARE(provider->kind, FacetKind::Single);
        QCOMPARE(provider->side, FacetSide::Server);
        QCOMPARE(optionValues(provider), (QStringList{ "modrinth", "curseforge", "technic" }));
        QCOMPARE(optionValues(findFacet(modrinth, FacetId::mcVersion)), (QStringList{ "1.21.1", "1.20.1", "1.19.2" }));
        QCOMPARE(optionValues(findFacet(modrinth, FacetId::loader)), (QStringList{ "fabric", "forge", "neoforge", "quilt" }));
        QCOMPARE(findFacet(modrinth, FacetId::loader)->optionLabel("neoforge"), QStringLiteral("NeoForge"));
        QCOMPARE(findFacet(modrinth, FacetId::category)->optionLabel("adventure"), QStringLiteral("Adventure"));
        const auto* installed = findFacet(modrinth, FacetId::installed);
        QCOMPARE(installed->kind, FacetKind::Toggle);
        QCOMPARE(installed->side, FacetSide::Client);
        for (const auto& f : modrinth) {
            QVERIFY2(f.isEnabled(), qPrintable(f.id));
        }

        // Technic: search by name only.
        const auto technic = facetsFor(Kind::Modpack, ctx(Provider::Technic));
        QCOMPARE(findFacet(technic, FacetId::mcVersion)->disabledReason, QStringLiteral("Technic can only be searched by name"));
        QVERIFY(!findFacet(technic, FacetId::loader)->isEnabled());
        QVERIFY(!findFacet(technic, FacetId::category)->isEnabled());
        QVERIFY(findFacet(technic, FacetId::provider)->isEnabled());
        QVERIFY(findFacet(technic, FacetId::installed)->isEnabled());

        // CurseForge: categories come with display names and numeric ids.
        const auto flame = facetsFor(Kind::Modpack, ctx(Provider::CurseForge, { { .name = "Adventure and RPG", .id = "4475", .header = {} } }));
        const auto* cat = findFacet(flame, FacetId::category);
        QVERIFY(cat->isEnabled());
        QCOMPARE(optionValues(cat), QStringList{ "4475" });
        QCOMPARE(cat->optionLabel("4475"), QStringLiteral("Adventure and RPG"));
        QVERIFY(findFacet(flame, FacetId::loader)->isEnabled());
    }

    void facets_mods()
    {
        const auto modrinth = facetsFor(Kind::Mod, ctx(Provider::Modrinth, { { .name = "game-mechanics", .id = "game-mechanics", .header = "categories" } }));
        QCOMPARE(ids(modrinth), (QStringList{ FacetId::provider, FacetId::mcVersion, FacetId::loader, FacetId::category, FacetId::environment,
                                              FacetId::installed, FacetId::updates, FacetId::hideInstalled }));
        QCOMPARE(optionValues(findFacet(modrinth, FacetId::provider)), (QStringList{ "modrinth", "curseforge" }));  // no Technic
        QCOMPARE(findFacet(modrinth, FacetId::category)->optionLabel("game-mechanics"), QStringLiteral("Game mechanics"));
        QCOMPARE(findFacet(modrinth, FacetId::environment)->kind, FacetKind::Single);
        QVERIFY(findFacet(modrinth, FacetId::environment)->isEnabled());
        QCOMPARE(findFacet(modrinth, FacetId::updates)->side, FacetSide::Client);

        const auto flame = facetsFor(Kind::Mod, ctx(Provider::CurseForge));
        QCOMPARE(findFacet(flame, FacetId::environment)->disabledReason, QStringLiteral("CurseForge has no environment data"));
        QVERIFY(findFacet(flame, FacetId::loader)->isEnabled());

        // Without a target instance, the target facets are disabled.
        const auto noTarget = facetsFor(Kind::Mod, ctx(Provider::Modrinth, {}, false));
        for (const auto& id : { FacetId::installed, FacetId::updates, FacetId::hideInstalled }) {
            QCOMPARE(findFacet(noTarget, id)->disabledReason, QStringLiteral("Choose a target instance first"));
        }
    }

    void facets_resourcePacks()
    {
        const auto modrinth = facetsFor(Kind::ResourcePack, ctx(Provider::Modrinth, modrinthResourcePackCategories()));
        QCOMPARE(ids(modrinth), (QStringList{ FacetId::provider, FacetId::mcVersion, FacetId::resolution, FacetId::category, FacetId::feature,
                                              FacetId::installed }));
        const auto* resolution = findFacet(modrinth, FacetId::resolution);
        QCOMPARE(optionValues(resolution), (QStringList{ "8x-", "16x", "32x", "48x", "64x", "128x", "256x", "512x+" }));
        QCOMPARE(resolution->optionLabel("16x"), QStringLiteral("16x"));
        QCOMPARE(resolution->optionLabel("8x-"), QStringLiteral("8x or lower"));
        QCOMPARE(resolution->options.first().group, QStringLiteral("Resolutions"));
        // Headers split the provider categories between Category and Features.
        QCOMPARE(optionValues(findFacet(modrinth, FacetId::category)), (QStringList{ "vanilla-like", "game-mechanics" }));
        QCOMPARE(findFacet(modrinth, FacetId::category)->optionLabel("vanilla-like"), QStringLiteral("Vanilla-like"));
        QCOMPARE(optionValues(findFacet(modrinth, FacetId::feature)), QStringList{ "audio" });
        QCOMPARE(findFacet(modrinth, FacetId::feature)->options.first().group, QStringLiteral("Features"));

        const auto flame = facetsFor(Kind::ResourcePack, ctx(Provider::CurseForge));
        QCOMPARE(findFacet(flame, FacetId::resolution)->disabledReason, QStringLiteral("CurseForge has no resolution data"));
        QCOMPARE(findFacet(flame, FacetId::feature)->disabledReason, QStringLiteral("CurseForge has no feature tags"));
        QVERIFY(findFacet(flame, FacetId::category)->isEnabled());
        QVERIFY(findFacet(flame, FacetId::mcVersion)->isEnabled());
    }

    void facets_shaders()
    {
        const auto modrinth = facetsFor(Kind::Shader, ctx(Provider::Modrinth, modrinthShaderCategories()));
        QCOMPARE(ids(modrinth), (QStringList{ FacetId::mcVersion, FacetId::platform, FacetId::performance, FacetId::feature, FacetId::category,
                                              FacetId::installed }));
        QCOMPARE(optionValues(findFacet(modrinth, FacetId::platform)), (QStringList{ "iris", "optifine", "canvas", "vanilla" }));
        QCOMPARE(findFacet(modrinth, FacetId::platform)->optionLabel("optifine"), QStringLiteral("OptiFine"));
        const auto* perf = findFacet(modrinth, FacetId::performance);
        QCOMPARE(optionValues(perf), (QStringList{ "potato", "low", "medium", "high", "screenshot" }));  // + the provider's extra tag
        QCOMPARE(perf->optionLabel("high"), QStringLiteral("High quality"));
        QCOMPARE(perf->optionLabel("screenshot"), QStringLiteral("Cinematic"));
        QCOMPARE(perf->options.at(3).group, QStringLiteral("Performance impact"));
        QCOMPARE(optionValues(findFacet(modrinth, FacetId::feature)), (QStringList{ "atmosphere", "pbr" }));
        QCOMPARE(findFacet(modrinth, FacetId::feature)->optionLabel("pbr"), QStringLiteral("PBR"));
        QCOMPARE(optionValues(findFacet(modrinth, FacetId::category)), QStringList{ "cartoon" });

        const auto flame = facetsFor(Kind::Shader, ctx(Provider::CurseForge));
        QCOMPARE(findFacet(flame, FacetId::platform)->disabledReason, QStringLiteral("CurseForge has no shader platform data"));
        QCOMPARE(findFacet(flame, FacetId::performance)->disabledReason, QStringLiteral("CurseForge has no performance data"));
    }

    // ---- Chips ----

    void chips_labelsDefaultsQueryAndProvider()
    {
        const auto facets = facetsFor(Kind::Mod, ctx(Provider::Modrinth, { { .name = "game-mechanics", .id = "game-mechanics", .header = "categories" } }));
        const auto defaults = state({ { FacetId::mcVersion, { "1.20.1" } }, { FacetId::loader, { "fabric" } } });
        auto s = state({ { FacetId::mcVersion, { "1.20.1" } },
                         { FacetId::loader, { "fabric", "quilt" } },
                         { FacetId::category, { "game-mechanics" } },
                         { FacetId::updates, { kToggleOn } },
                         { "unknownFacet", { "x" } } },
                       "  sodium ");
        auto chips = chipsFor(s, facets, defaults);
        QCOMPARE(labels(chips), (QStringList{ "Minecraft version: 1.20.1", "Loader: Fabric", "Loader: Quilt", "Category: Game mechanics",
                                              "Update available", "\"sodium\"" }));
        QVERIFY(chips.at(0).isDefault);
        QVERIFY(chips.at(1).isDefault);
        QVERIFY(!chips.at(2).isDefault);
        QVERIFY(!chips.at(3).isDefault);
        QVERIFY(chips.last().isQuery);
        QCOMPARE(chips.last().value, QStringLiteral("sodium"));
        QVERIFY(!chips.last().isDefault);

        // Provider chip only when it differs from the default.
        s.selected.insert(FacetId::provider, { "modrinth" });
        QCOMPARE(chipsFor(s, facets, defaults).size(), 6);
        s.selected.insert(FacetId::provider, { "curseforge" });
        const auto withProvider = chipsFor(s, facetsFor(Kind::Mod, ctx(Provider::CurseForge)), defaults);
        QCOMPARE(withProvider.first().label, QStringLiteral("Provider: CurseForge"));
        QCOMPARE(withProvider.first().facetId, FacetId::provider);
        QVERIFY(!withProvider.first().isDefault);
    }

    void chips_inactiveForDisabledFacets()
    {
        const auto facets = facetsFor(Kind::ResourcePack, ctx(Provider::CurseForge));
        const auto s = state({ { FacetId::provider, { "curseforge" } }, { FacetId::resolution, { "16x" } } });
        const auto chips = chipsFor(s, facets, FilterState{});
        QCOMPARE(labels(chips), (QStringList{ "Provider: CurseForge", "Resolution: 16x" }));
        QVERIFY(!chips.at(0).isInactive);
        QVERIFY(chips.at(1).isInactive);
    }

    void withoutChip_removesExactlyThatValue()
    {
        const auto facets = facetsFor(Kind::Mod, ctx(Provider::Modrinth));
        const auto s = state({ { FacetId::loader, { "fabric", "quilt" } }, { FacetId::mcVersion, { "1.20.1" } } }, "sodium");
        const auto chips = chipsFor(s, facets, FilterState{});

        auto r = withoutChip(s, chips.at(1));  // Loader: Fabric
        QCOMPARE(r.values(FacetId::loader), QStringList{ "quilt" });
        QCOMPARE(r.values(FacetId::mcVersion), QStringList{ "1.20.1" });
        QCOMPARE(r.query, QStringLiteral("sodium"));

        r = withoutChip(s, chips.at(0));  // the only version: the key goes away
        QVERIFY(!r.selected.contains(FacetId::mcVersion));

        r = withoutChip(s, chips.last());  // query
        QVERIFY(r.query.isEmpty());
        QCOMPARE(r.values(FacetId::loader).size(), 2);

        // Provider chip: back to none (Modrinth) or to the defaults' provider.
        const auto p = state({ { FacetId::provider, { "curseforge" } } });
        const auto providerChip = chipsFor(p, facets, FilterState{}).first();
        QCOMPARE(withoutChip(p, providerChip).provider(), Provider::Modrinth);
        const auto defaults = state({ { FacetId::provider, { "technic" } } });
        QCOMPARE(withoutChip(p, providerChip, defaults).provider(), Provider::Technic);
    }

    void clearAll_vs_showEverything()
    {
        const auto defaults = state({ { FacetId::mcVersion, { "1.20.1" } }, { FacetId::loader, { "fabric" } } });
        const auto current = state({ { FacetId::provider, { "curseforge" } },
                                     { FacetId::mcVersion, { "1.19.2" } },
                                     { FacetId::category, { "4475" } },
                                     { FacetId::installed, { kToggleOn } } },
                                   "atm", "downloads");

        const auto cleared = clearAll(current, defaults);
        QCOMPARE(cleared.values(FacetId::mcVersion), QStringList{ "1.20.1" });
        QCOMPARE(cleared.values(FacetId::loader), QStringList{ "fabric" });
        QVERIFY(cleared.values(FacetId::category).isEmpty());
        QVERIFY(cleared.values(FacetId::installed).isEmpty());
        QVERIFY(cleared.query.isEmpty());
        QCOMPARE(cleared.sortId, QStringLiteral("downloads"));
        QCOMPARE(cleared.provider(), Provider::Modrinth);  // the provider is a facet: back to the default

        const auto everything = showEverything(current);
        QCOMPARE(everything.selected.size(), 1);
        QCOMPARE(everything.provider(), Provider::CurseForge);
        QVERIFY(everything.query.isEmpty());
        QCOMPARE(everything.sortId, QStringLiteral("downloads"));
        QVERIFY(everything.values(FacetId::mcVersion).isEmpty());
    }

    void needsNewSearch_onlyForServerChanges()
    {
        const auto facets = facetsFor(Kind::Mod, ctx(Provider::Modrinth));
        const auto base = state({ { FacetId::loader, { "fabric" } } }, "sodium", "downloads");

        auto b = base;
        b.selected.insert(FacetId::installed, { kToggleOn });
        b.selected.insert(FacetId::hideInstalled, { kToggleOn });
        QVERIFY(!needsNewSearch(base, b, facets));  // client-only change

        b = base;
        b.selected.insert(FacetId::loader, {});  // empty list == missing
        b.selected[FacetId::loader] = { "fabric" };
        QVERIFY(!needsNewSearch(base, b, facets));

        b = base;
        b.selected[FacetId::loader] = { "fabric", "quilt" };
        QVERIFY(needsNewSearch(base, b, facets));

        b = base;
        b.query = "sodium extra";
        QVERIFY(needsNewSearch(base, b, facets));
        b.query = " sodium ";  // whitespace only
        QVERIFY(!needsNewSearch(base, b, facets));

        b = base;
        b.sortId = "newest";
        QVERIFY(needsNewSearch(base, b, facets));

        b = base;
        b.selected.insert(FacetId::provider, { "curseforge" });
        QVERIFY(needsNewSearch(base, b, facets));
        b.selected[FacetId::provider] = { "modrinth" };  // explicit default = same provider
        QVERIFY(!needsNewSearch(base, b, facets));

        b = base;
        b.selected.insert(FacetId::environment, { "client" });
        QVERIFY(needsNewSearch(base, b, facets));
    }

    // ---- Sorting ----

    void sorting_vocabularyAndMapping()
    {
        auto sortIds = [](const QList<SortOption>& options) {
            QStringList out;
            for (const auto& o : options) {
                out << o.id;
            }
            return out;
        };
        QCOMPARE(sortIds(sortOptions(Kind::Mod, Provider::Modrinth)), (QStringList{ "relevance", "downloads", "follows", "newest", "updated" }));
        QCOMPARE(sortIds(sortOptions(Kind::Modpack, Provider::CurseForge)),
                 (QStringList{ "featured", "popularity", "downloads", "updated", "name" }));
        QVERIFY(sortOptions(Kind::Modpack, Provider::Technic).isEmpty());

        const auto modrinth = ModrinthAPI::get().getSortingMethods();
        const auto flame = FlameAPI::get().getSortingMethods();
        // Every offered option maps to a real provider method.
        for (const auto& o : sortOptions(Kind::Mod, Provider::Modrinth)) {
            QVERIFY2(sortingFor(o.id, modrinth).has_value(), qPrintable(o.id));
        }
        for (const auto& o : sortOptions(Kind::Mod, Provider::CurseForge)) {
            QVERIFY2(sortingFor(o.id, flame).has_value(), qPrintable(o.id));
        }
        QCOMPARE(sortingFor("downloads", modrinth)->name, QStringLiteral("downloads"));
        QCOMPARE(sortingFor("downloads", flame)->index, 6U);  // TotalDownloads
        QCOMPARE(sortingFor("updated", flame)->index, 3U);    // LastUpdated
        QCOMPARE(sortingFor("featured", flame)->index, 1U);
        QVERIFY(!sortingFor("featured", modrinth).has_value());
        QVERIFY(!sortingFor("newest", flame).has_value());
        QVERIFY(!sortingFor("nonsense", modrinth).has_value());
    }

    // ---- Search arguments ----

    void toSearchArgs_modrinthGroups()
    {
        const auto methods = ModrinthAPI::get().getSortingMethods();
        auto s = state({ { FacetId::mcVersion, { "1.20.1", "1.19.2" } },
                         { FacetId::resolution, { "16x", "32x" } },
                         { FacetId::feature, { "audio" } },
                         { FacetId::category, { "vanilla-like" } },
                         { FacetId::installed, { kToggleOn } } },
                       " faithful ", "downloads");
        auto args = toSearchArgs(s, Kind::ResourcePack, Provider::Modrinth, 25, methods,
                                 facetsFor(Kind::ResourcePack, ctx(Provider::Modrinth, modrinthResourcePackCategories())));
        QCOMPARE(args.type, ModPlatform::ResourceType::ResourcePack);
        QCOMPARE(args.offset, 25);
        QCOMPARE(args.search.value(), QStringLiteral("faithful"));
        QCOMPARE(args.sorting->name, QStringLiteral("downloads"));
        QCOMPARE(args.versions->size(), size_t(2));
        QVERIFY(!args.loaders.has_value());
        QVERIFY(!args.categoryIds.has_value());  // categoryIds would AND every value
        QCOMPARE(args.categoryGroups.value(), (QList<QStringList>{ { "vanilla-like" }, { "audio" }, { "16x", "32x" } }));
        const QString url = QUrl::fromPercentEncoding(ModrinthAPI::get().searchProjects(args).first.url.toEncoded());
        QVERIFY2(url.contains(R"(["categories:16x","categories:32x"])"), qPrintable(url));
        QVERIFY2(url.contains(R"(["categories:audio"])"), qPrintable(url));
        QVERIFY2(url.contains(R"(["versions:1.20.1","versions:1.19.2"])"), qPrintable(url));

        // Shaders: platform and performance go as category tags.
        s = state({ { FacetId::platform, { "iris", "optifine" } }, { FacetId::performance, { "potato" } } });
        args = toSearchArgs(s, Kind::Shader, Provider::Modrinth, 0, methods, facetsFor(Kind::Shader, ctx(Provider::Modrinth, modrinthShaderCategories())));
        QCOMPARE(args.type, ModPlatform::ResourceType::ShaderPack);
        QCOMPARE(args.categoryGroups.value(), (QList<QStringList>{ { "iris", "optifine" }, { "potato" } }));
        QVERIFY(!args.loaders.has_value());
        QVERIFY(!args.search.has_value());
        QVERIFY(!args.sorting.has_value());

        // Mods: loaders as flags (one OR group in the URL), environment as side.
        s = state({ { FacetId::loader, { "fabric", "quilt", "bogus" } }, { FacetId::environment, { "client" } } }, {}, "relevance");
        args = toSearchArgs(s, Kind::Mod, Provider::Modrinth, 0, methods, facetsFor(Kind::Mod, ctx(Provider::Modrinth)));
        QCOMPARE(args.loaders.value(), ModPlatform::ModLoaderTypes(ModPlatform::Fabric | ModPlatform::Quilt));
        QCOMPARE(args.side.value(), ModPlatform::SideType(ModPlatform::SideType::ClientSide));
        QVERIFY(!args.categoryGroups.has_value());
        QCOMPARE(args.sorting->name, QStringLiteral("relevance"));

        // Values of facets that do not belong to the kind are ignored.
        s = state({ { FacetId::resolution, { "16x" } }, { FacetId::environment, { "client" } } });
        args = toSearchArgs(s, Kind::Modpack, Provider::Modrinth, 0, methods, facetsFor(Kind::Modpack, ctx(Provider::Modrinth)));
        QVERIFY(!args.categoryGroups.has_value());
        QVERIFY(!args.side.has_value());
    }

    void toSearchArgs_curseForge()
    {
        const auto methods = FlameAPI::get().getSortingMethods();
        const QList<ModPlatform::Category> cfCategories = { { .name = "a", .id = "4475", .header = "" }, { .name = "b", .id = "4484", .header = "" } };
        const auto cfModpack = facetsFor(Kind::Modpack, ctx(Provider::CurseForge, cfCategories));
        const auto s = state({ { FacetId::mcVersion, { "1.20.1", "1.19.2" } },
                               { FacetId::loader, { "forge", "neoforge" } },
                               { FacetId::category, { "4475", "4484" } },
                               { FacetId::resolution, { "16x" } },
                               { FacetId::feature, { "audio" } } },
                             "atm", "featured");
        auto args = toSearchArgs(s, Kind::Modpack, Provider::CurseForge, 50, methods, cfModpack);
        QCOMPARE(args.type, ModPlatform::ResourceType::Modpack);
        // CurseForge takes one gameVersion: with two selected none is sent (matchesClient filters the loaded rows).
        QVERIFY(!args.versions.has_value());
        {
            const auto one = state({ { FacetId::mcVersion, { "1.20.1" } } });
            const auto oneArgs = toSearchArgs(one, Kind::Modpack, Provider::CurseForge, 0, methods, cfModpack);
            QCOMPARE(oneArgs.versions->size(), size_t(1));
            QCOMPARE(oneArgs.versions->front().toString(), QStringLiteral("1.20.1"));
        }
        QCOMPARE(args.loaders.value(), ModPlatform::ModLoaderTypes(ModPlatform::Forge | ModPlatform::NeoForge));
        QCOMPARE(args.categoryIds.value(), (QStringList{ "4475", "4484" }));
        QVERIFY(!args.categoryGroups.has_value());
        QCOMPARE(args.sorting->index, 1U);
        const QString url = QUrl::fromPercentEncoding(FlameAPI::get().searchProjects(args).first.url.toEncoded());
        QVERIFY2(!url.contains("gameVersion="), qPrintable(url));

        // Resource pack resolution / features are Modrinth-only: never sent to CurseForge.
        args = toSearchArgs(s, Kind::ResourcePack, Provider::CurseForge, 0, methods,
                          facetsFor(Kind::ResourcePack, ctx(Provider::CurseForge, cfCategories)));
        QVERIFY(!args.categoryGroups.has_value());
        QCOMPARE(args.categoryIds.value(), (QStringList{ "4475", "4484" }));
    }

    void toSearchArgs_providerSwitchDropsForeignValues()
    {
        const auto methods = ModrinthAPI::get().getSortingMethods();
        const QList<ModPlatform::Category> modrinthCats = { { .name = "optimization", .id = "optimization", .header = "categories" } };
        const QList<ModPlatform::Category> cfCats = { { .name = "Adventure", .id = "4475", .header = "" } };

        // Modrinth -> CurseForge: the Modrinth category is not a CurseForge id.
        auto s = state({ { FacetId::category, { "optimization" } } });
        auto args = toSearchArgs(s, Kind::Mod, Provider::Modrinth, 0, methods, facetsFor(Kind::Mod, ctx(Provider::Modrinth, modrinthCats)));
        QCOMPARE(args.categoryGroups.value(), (QList<QStringList>{ { "optimization" } }));
        args = toSearchArgs(s, Kind::Mod, Provider::CurseForge, 0, methods, facetsFor(Kind::Mod, ctx(Provider::CurseForge, cfCats)));
        QVERIFY(!args.categoryIds.has_value());
        QVERIFY(!args.categoryGroups.has_value());

        // CurseForge -> Modrinth: the numeric id is not a Modrinth tag; an offered value next to it is kept.
        s = state({ { FacetId::category, { "4475", "optimization" } } });
        args = toSearchArgs(s, Kind::Mod, Provider::Modrinth, 0, methods, facetsFor(Kind::Mod, ctx(Provider::Modrinth, modrinthCats)));
        QCOMPARE(args.categoryGroups.value(), (QList<QStringList>{ { "optimization" } }));
        args = toSearchArgs(s, Kind::Mod, Provider::CurseForge, 0, methods, facetsFor(Kind::Mod, ctx(Provider::CurseForge, cfCats)));
        QCOMPARE(args.categoryIds.value(), (QStringList{ "4475" }));

        // Modrinth-only resolution / feature values never reach CurseForge (disabled facets), even with a matching option.
        s = state({ { FacetId::resolution, { "16x" } }, { FacetId::feature, { "audio" } } });
        args = toSearchArgs(s, Kind::ResourcePack, Provider::CurseForge, 0, methods,
                            facetsFor(Kind::ResourcePack, ctx(Provider::CurseForge, modrinthResourcePackCategories())));
        QVERIFY(!args.categoryIds.has_value());
        QVERIFY(!args.categoryGroups.has_value());

        // The values stay in the state, shown as inactive chips for a disabled facet.
        const auto chips = chipsFor(s, facetsFor(Kind::ResourcePack, ctx(Provider::CurseForge)), FilterState{});
        QVERIFY(!chips.isEmpty());
        QVERIFY(chips.constFirst().isInactive);
    }

    void toSearchArgs_technicIsQueryOnly()
    {
        const auto s = state({ { FacetId::mcVersion, { "1.12.2" } }, { FacetId::loader, { "forge" } } }, "tekkit", "downloads");
        const auto args = toSearchArgs(s, Kind::Modpack, Provider::Technic, 0, {}, facetsFor(Kind::Modpack, ctx(Provider::Technic)));
        QCOMPARE(args.search.value(), QStringLiteral("tekkit"));
        QVERIFY(!args.sorting.has_value());
        QVERIFY(!args.versions.has_value());
        QVERIFY(!args.loaders.has_value());
    }

    // ---- Client predicate ----

    void matchesClient_clientFacets()
    {
        const auto facets = facetsFor(Kind::Mod, ctx(Provider::Modrinth));
        ItemFacts installedWithUpdate{ .provider = Provider::Modrinth, .gameVersions = {}, .loaderNames = {}, .categories = {},
                                       .installed = true, .updateAvailable = true };
        ItemFacts notInstalled{ .provider = Provider::Modrinth, .gameVersions = {}, .loaderNames = {}, .categories = {},
                                .installed = false, .updateAvailable = false };

        QVERIFY(matchesClient(FilterState{}, facets, notInstalled));
        auto s = state({ { FacetId::installed, { kToggleOn } } });
        QVERIFY(matchesClient(s, facets, installedWithUpdate));
        QVERIFY(!matchesClient(s, facets, notInstalled));
        s = state({ { FacetId::updates, { kToggleOn } } });
        QVERIFY(matchesClient(s, facets, installedWithUpdate));
        QVERIFY(!matchesClient(s, facets, notInstalled));
        s = state({ { FacetId::hideInstalled, { kToggleOn } } });
        QVERIFY(!matchesClient(s, facets, installedWithUpdate));
        QVERIFY(matchesClient(s, facets, notInstalled));

        // Disabled facets never filter (no target instance).
        const auto noTarget = facetsFor(Kind::Mod, ctx(Provider::Modrinth, {}, false));
        QVERIFY(matchesClient(state({ { FacetId::installed, { kToggleOn } } }), noTarget, notInstalled));

        // Server facets Modrinth applied are not re-checked on the client.
        const auto server = state({ { FacetId::mcVersion, { "1.20.1" } }, { FacetId::loader, { "fabric" } } });
        ItemFacts other{ .provider = Provider::Modrinth, .gameVersions = { "1.12.2" }, .loaderNames = { "forge" } };
        QVERIFY(matchesClient(server, facets, other));
    }

    void matchesClient_curseForgeEnforcesVersionAndLoader()
    {
        const auto facets = facetsFor(Kind::Mod, ctx(Provider::CurseForge));
        const auto s = state({ { FacetId::mcVersion, { "1.20.1", "1.19.2" } }, { FacetId::loader, { "fabric", "quilt" } } });

        ItemFacts match{ .provider = Provider::CurseForge, .gameVersions = { "1.21.1", "1.19.2" }, .loaderNames = { "neoforge", "quilt" } };
        QVERIFY(matchesClient(s, facets, match));

        ItemFacts wrongVersion = match;
        wrongVersion.gameVersions = { "1.21.1" };
        QVERIFY(!matchesClient(s, facets, wrongVersion));

        ItemFacts wrongLoader = match;
        wrongLoader.loaderNames = { "Forge" };
        QVERIFY(!matchesClient(s, facets, wrongLoader));
        wrongLoader.loaderNames = { "FABRIC" };  // provider casing does not matter
        QVERIFY(matchesClient(s, facets, wrongLoader));

        // Exactly one value was sent to (and applied by) the server: not re-checked on the client.
        const auto single = state({ { FacetId::mcVersion, { "1.20.1" } }, { FacetId::loader, { "fabric" } } });
        ItemFacts partial{ .provider = Provider::CurseForge, .gameVersions = { "1.21.1" }, .loaderNames = { "forge" } };
        QVERIFY(matchesClient(single, facets, partial));

        // Unknown data (empty lists) never excludes.
        ItemFacts unknown{ .provider = Provider::CurseForge };
        QVERIFY(matchesClient(s, facets, unknown));
    }

    // ---- Presentation helpers ----

    void performanceLabel_ranges()
    {
        QCOMPARE(performanceLabel({}), QString());
        QCOMPARE(performanceLabel({ "cartoon", "iris" }), QString());
        QCOMPARE(performanceLabel({ "potato" }), QStringLiteral("Lightweight"));
        QCOMPARE(performanceLabel({ "low" }), QStringLiteral("Lightweight"));
        QCOMPARE(performanceLabel({ "potato", "low" }), QStringLiteral("Lightweight"));
        QCOMPARE(performanceLabel({ "medium" }), QStringLiteral("Balanced"));
        QCOMPARE(performanceLabel({ "high" }), QStringLiteral("High quality"));
        QCOMPARE(performanceLabel({ "screenshot" }), QStringLiteral("Cinematic"));
        QCOMPARE(performanceLabel({ "high", "iris", "low", "medium" }), QString::fromUtf8("Lightweight – High quality"));
        QCOMPARE(performanceLabel({ "medium", "high" }), QString::fromUtf8("Balanced – High quality"));
        QCOMPARE(performanceLabel({ "screenshot", "potato" }), QString::fromUtf8("Lightweight – Cinematic"));
    }

    void resolutionsOf_canonicalOrder()
    {
        QCOMPARE(resolutionsOf({ "512x+", "audio", "16x", "8x-", "64x" }), (QStringList{ "8x-", "16x", "64x", "512x+" }));
        QVERIFY(resolutionsOf({ "audio" }).isEmpty());
    }

    // ---- State ----

    void state_jsonRoundTripAndUnknownKeys()
    {
        const auto s = state({ { FacetId::provider, { "curseforge" } },
                               { FacetId::mcVersion, { "1.20.1", "1.19.2" } },
                               { FacetId::installed, { kToggleOn } },
                               { FacetId::category, {} } },
                             "all the mods", "downloads");
        const auto json = s.toJson();
        QVERIFY(!json.value("selected").toObject().contains(FacetId::category));  // empty lists are not written
        const auto back = FilterState::fromJson(QJsonDocument::fromJson(QJsonDocument(json).toJson()).object());
        QCOMPARE(back, s);
        QCOMPARE(back.values(FacetId::mcVersion), (QStringList{ "1.20.1", "1.19.2" }));  // order kept
        QCOMPARE(back.provider(), Provider::CurseForge);

        QJsonObject messy = json;
        messy.insert("version", 7);
        messy.insert("viewMode", "grid");
        auto sel = messy.value("selected").toObject();
        sel.insert("loader", QJsonArray{ "fabric", 3, QJsonValue::Null, "", "fabric", "quilt" });
        sel.insert("category", "not-an-array");
        sel.insert("", QJsonArray{ "x" });
        messy.insert("selected", sel);
        const auto lenient = FilterState::fromJson(messy);
        QCOMPARE(lenient.values(FacetId::loader), (QStringList{ "fabric", "quilt" }));
        QVERIFY(!lenient.selected.contains(FacetId::category));
        QVERIFY(!lenient.selected.contains(QString()));
        QCOMPARE(lenient.query, s.query);

        const auto wrong = FilterState::fromJson(QJsonObject{ { "query", 5 }, { "sortId", QJsonArray{} }, { "selected", "x" } });
        QCOMPARE(wrong, FilterState{});
    }

    void state_equalityAndProvider()
    {
        auto a = state({ { FacetId::loader, { "fabric" } } });
        auto b = a;
        b.selected.insert(FacetId::category, {});
        QVERIFY(a == b);  // empty list == missing
        b.selected[FacetId::loader] = { "quilt" };
        QVERIFY(a != b);

        QCOMPARE(FilterState{}.provider(), Provider::Modrinth);
        QCOMPARE(state({ { FacetId::provider, { "nope" } } }).provider(), Provider::Modrinth);
        QCOMPARE(state({ { FacetId::provider, { "technic" } } }).provider(), Provider::Technic);
        QVERIFY(a.isSelected(FacetId::loader, "fabric"));
        QVERIFY(!a.isSelected(FacetId::loader, "forge"));
        QCOMPARE(providerFromId(providerId(Provider::CurseForge)), std::optional<Provider>(Provider::CurseForge));
        QVERIFY(!providerFromId("other").has_value());
    }
};

QTEST_GUILESS_MAIN(ContentFilterTest)

#include "ContentFilter_test.moc"
