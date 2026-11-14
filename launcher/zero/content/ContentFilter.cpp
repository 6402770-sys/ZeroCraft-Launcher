// SPDX-License-Identifier: GPL-3.0-only
#include "zero/content/ContentFilter.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonValue>

#include <algorithm>
#include <array>
#include <utility>

#include "Version.h"

namespace Zero::Content {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("Zero::Content::ContentFilter", text);
}

// Modrinth header names (the `/tag/category` `header` field).
const QString kHeaderCategories = QStringLiteral("categories");
const QString kHeaderFeatures = QStringLiteral("features");
const QString kHeaderResolutions = QStringLiteral("resolutions");
const QString kHeaderPerformance = QStringLiteral("performance impact");

// Canonical resolution order (Modrinth `resolutions` header).
const std::array<QLatin1StringView, 8> kResolutions = { QLatin1StringView("8x-"),  QLatin1StringView("16x"),  QLatin1StringView("32x"),
                                                        QLatin1StringView("48x"),  QLatin1StringView("64x"),  QLatin1StringView("128x"),
                                                        QLatin1StringView("256x"), QLatin1StringView("512x+") };

// Performance tags and their profile rank (0 Lightweight, 1 Balanced, 2 High quality, 3 Cinematic).
const std::array<std::pair<QLatin1StringView, int>, 5> kPerformance = { std::pair{ QLatin1StringView("potato"), 0 },
                                                                        std::pair{ QLatin1StringView("low"), 0 },
                                                                        std::pair{ QLatin1StringView("medium"), 1 },
                                                                        std::pair{ QLatin1StringView("high"), 2 },
                                                                        std::pair{ QLatin1StringView("screenshot"), 3 } };

QString profileLabel(int rank)
{
    switch (rank) {
        case 0:
            return tr("Lightweight");
        case 1:
            return tr("Balanced");
        case 2:
            return tr("High quality");
        default:
            return tr("Cinematic");
    }
}

QString performanceOptionLabel(const QString& tag)
{
    if (tag == QLatin1String("potato")) {
        return tr("Potato");  // Modrinth's own word; the profile label groups it with "low"
    }
    for (const auto& [name, rank] : kPerformance) {
        if (tag == name) {
            return profileLabel(rank);
        }
    }
    return QString();
}

/// "game-mechanics" -> "Game mechanics" (Modrinth slugs have no display name).
QString humanize(const QString& slug)
{
    static const QMap<QString, QString> s_special = {
        { QStringLiteral("pbr"), QStringLiteral("PBR") },
        { QStringLiteral("gui"), QStringLiteral("GUI") },
        { QStringLiteral("vanilla-like"), QStringLiteral("Vanilla-like") },
    };
    if (auto it = s_special.constFind(slug); it != s_special.constEnd()) {
        return it.value();
    }
    QString out = slug;
    out.replace(QLatin1Char('-'), QLatin1Char(' ')).replace(QLatin1Char('_'), QLatin1Char(' '));
    out = out.simplified();
    if (!out.isEmpty()) {
        out[0] = out[0].toUpper();
    }
    return out;
}

QString categoryLabel(const ModPlatform::Category& cat)
{
    return (cat.name.isEmpty() || cat.name == cat.id) ? humanize(cat.id) : cat.name;
}

QString resolutionLabel(const QString& tag)
{
    if (tag == QLatin1String("8x-")) {
        return tr("8x or lower");
    }
    if (tag == QLatin1String("512x+")) {
        return tr("512x or higher");
    }
    return tag;
}

ModPlatform::ResourceType resourceTypeOf(Kind kind)
{
    switch (kind) {
        case Kind::Modpack:
            return ModPlatform::ResourceType::Modpack;
        case Kind::Mod:
            return ModPlatform::ResourceType::Mod;
        case Kind::ResourcePack:
            return ModPlatform::ResourceType::ResourcePack;
        case Kind::Shader:
            return ModPlatform::ResourceType::ShaderPack;
        case Kind::World:
            return ModPlatform::ResourceType::World;
    }
    return ModPlatform::ResourceType::Unknown;
}

Facet makeFacet(const QString& id, const QString& label, FacetKind kind, FacetSide side, QList<FacetOption> options = {})
{
    Facet f;
    f.id = id;
    f.label = label;
    f.kind = kind;
    f.side = side;
    f.options = std::move(options);
    return f;
}

Facet toggleFacet(const QString& id, const QString& label)
{
    return makeFacet(id, label, FacetKind::Toggle, FacetSide::Client, { { kToggleOn, label, QString() } });
}

Facet providerFacet(Kind kind)
{
    QList<FacetOption> options = { { providerId(Provider::Modrinth), providerName(Provider::Modrinth), QString() },
                                   { providerId(Provider::CurseForge), providerName(Provider::CurseForge), QString() } };
    if (kind == Kind::Modpack) {
        options.append({ providerId(Provider::Technic), providerName(Provider::Technic), QString() });
    }
    return makeFacet(FacetId::provider, tr("Provider"), FacetKind::Single, FacetSide::Server, options);
}

Facet versionFacet(const FacetContext& ctx)
{
    QList<FacetOption> options;
    for (const auto& v : ctx.mcVersions) {
        if (!v.isEmpty()) {
            options.append({ v, v, QString() });
        }
    }
    return makeFacet(FacetId::mcVersion, tr("Minecraft version"), FacetKind::Multi, FacetSide::Server, options);
}

Facet loaderFacet()
{
    return makeFacet(FacetId::loader, tr("Loader"), FacetKind::Multi, FacetSide::Server,
                     { { QStringLiteral("fabric"), QStringLiteral("Fabric"), QString() },
                       { QStringLiteral("forge"), QStringLiteral("Forge"), QString() },
                       { QStringLiteral("neoforge"), QStringLiteral("NeoForge"), QString() },
                       { QStringLiteral("quilt"), QStringLiteral("Quilt"), QString() } });
}

Facet platformFacet()
{
    return makeFacet(FacetId::platform, tr("Platform"), FacetKind::Multi, FacetSide::Server,
                     { { QStringLiteral("iris"), QStringLiteral("Iris"), QString() },
                       { QStringLiteral("optifine"), QStringLiteral("OptiFine"), QString() },
                       { QStringLiteral("canvas"), QStringLiteral("Canvas"), QString() },
                       { QStringLiteral("vanilla"), tr("Vanilla"), QString() } });
}

Facet environmentFacet()
{
    return makeFacet(FacetId::environment, tr("Environment"), FacetKind::Single, FacetSide::Server,
                     { { QStringLiteral("client"), tr("Client"), QString() },
                       { QStringLiteral("server"), tr("Server"), QString() },
                       { QStringLiteral("both"), tr("Client and server"), QString() } });
}

/// Category-like facet from the provider categories: `features` header -> the feature facet; resolutions and performance
/// have their own facets; everything else (no header, `categories`, an unknown header) -> the category facet.
Facet categoryFacet(const QString& id, const FacetContext& ctx)
{
    QList<FacetOption> options;
    for (const auto& cat : ctx.categories) {
        if (cat.id.isEmpty() || cat.header == kHeaderResolutions || cat.header == kHeaderPerformance) {
            continue;
        }
        const bool isFeature = cat.header == kHeaderFeatures;
        if (isFeature != (id == FacetId::feature)) {
            continue;
        }
        if (std::ranges::any_of(options, [&cat](const FacetOption& o) { return o.value == cat.id; })) {
            continue;
        }
        options.append({ cat.id, categoryLabel(cat), cat.header.isEmpty() ? QString() : humanize(cat.header) });
    }
    return makeFacet(id, id == FacetId::feature ? tr("Features") : tr("Category"), FacetKind::Multi, FacetSide::Server, options);
}

/// A facet over a fixed Modrinth vocabulary, plus any extra tags of that header the provider lists.
Facet vocabularyFacet(const QString& id,
                      const QString& label,
                      const QStringList& vocabulary,
                      const QString& header,
                      const FacetContext& ctx,
                      QString (*labelOf)(const QString&))
{
    QList<FacetOption> options;
    const QString group = humanize(header);
    for (const auto& tag : vocabulary) {
        options.append({ tag, labelOf(tag), group });
    }
    for (const auto& cat : ctx.categories) {
        if (cat.header == header && !cat.id.isEmpty() && !vocabulary.contains(cat.id)) {
            const auto known = labelOf(cat.id);
            options.append({ cat.id, known.isEmpty() || known == cat.id ? categoryLabel(cat) : known, group });
        }
    }
    return makeFacet(id, label, FacetKind::Multi, FacetSide::Server, options);
}

QStringList resolutionVocabulary()
{
    QStringList out;
    for (const auto& r : kResolutions) {
        out.append(r);
    }
    return out;
}

QStringList performanceVocabulary()
{
    return { QStringLiteral("potato"), QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high") };
}

QString disabledReasonFor(const QString& facetId, Kind kind, const FacetContext& ctx)
{
    if (ctx.provider == Provider::Technic &&
        (facetId == FacetId::mcVersion || facetId == FacetId::loader || facetId == FacetId::category)) {
        return tr("Technic can only be searched by name");
    }
    if (ctx.provider == Provider::CurseForge) {
        if (facetId == FacetId::environment) {
            return tr("CurseForge has no environment data");
        }
        if (facetId == FacetId::resolution) {
            return tr("CurseForge has no resolution data");
        }
        if (facetId == FacetId::feature) {
            return tr("CurseForge has no feature tags");
        }
        if (facetId == FacetId::platform) {
            return tr("CurseForge has no shader platform data");
        }
        if (facetId == FacetId::performance) {
            return tr("CurseForge has no performance data");
        }
    }
    if (kind != Kind::Modpack && !ctx.hasTarget &&
        (facetId == FacetId::installed || facetId == FacetId::updates || facetId == FacetId::hideInstalled)) {
        return tr("Choose a target instance first");
    }
    return QString();
}

bool anyOf(const QStringList& selected, const QStringList& known)
{
    return std::ranges::any_of(selected, [&known](const QString& v) { return known.contains(v, Qt::CaseInsensitive); });
}

/// The selected values without empties, in selection order.
QStringList cleaned(const QStringList& values)
{
    QStringList out;
    for (const auto& v : values) {
        if (!v.isEmpty() && !out.contains(v)) {
            out.append(v);
        }
    }
    return out;
}

/// The selected values of `facetId` that the provider's facet offers (values picked under another provider, e.g. a Modrinth
/// category after switching to CurseForge, are kept in the state but never sent). Empty when the facet is missing or disabled.
QStringList offered(const FilterState& state, const QList<Facet>& facets, const QString& facetId)
{
    const auto it = std::ranges::find_if(facets, [&facetId](const Facet& f) { return f.id == facetId; });
    if (it == facets.cend() || !it->isEnabled()) {
        return {};
    }
    QStringList out;
    for (const auto& v : cleaned(state.values(facetId))) {
        if (std::ranges::any_of(it->options, [&v](const FacetOption& o) { return o.value == v; })) {
            out.append(v);
        }
    }
    return out;
}

QMap<QString, QStringList> normalized(const QMap<QString, QStringList>& selected)
{
    QMap<QString, QStringList> out;
    for (auto it = selected.cbegin(); it != selected.cend(); ++it) {
        if (!it.value().isEmpty()) {
            out.insert(it.key(), it.value());
        }
    }
    return out;
}

}  // namespace

QString providerId(Provider provider)
{
    switch (provider) {
        case Provider::Modrinth:
            return QStringLiteral("modrinth");
        case Provider::CurseForge:
            return QStringLiteral("curseforge");
        case Provider::Technic:
            return QStringLiteral("technic");
    }
    return QString();
}

std::optional<Provider> providerFromId(const QString& id)
{
    for (auto p : { Provider::Modrinth, Provider::CurseForge, Provider::Technic }) {
        if (providerId(p) == id) {
            return p;
        }
    }
    return std::nullopt;
}

QString providerName(Provider provider)
{
    switch (provider) {
        case Provider::Modrinth:
            return QStringLiteral("Modrinth");
        case Provider::CurseForge:
            return QStringLiteral("CurseForge");
        case Provider::Technic:
            return QStringLiteral("Technic");
    }
    return QString();
}

QString Facet::optionLabel(const QString& value) const
{
    for (const auto& o : options) {
        if (o.value == value) {
            return o.label;
        }
    }
    return value;
}

QList<Facet> facetsFor(Kind kind, const FacetContext& ctx)
{
    QList<Facet> facets;
    switch (kind) {
        case Kind::Modpack:
            facets = { providerFacet(kind), versionFacet(ctx), loaderFacet(), categoryFacet(FacetId::category, ctx),
                       toggleFacet(FacetId::installed, tr("Installed")) };
            break;
        case Kind::Mod:
            facets = { providerFacet(kind),
                       versionFacet(ctx),
                       loaderFacet(),
                       categoryFacet(FacetId::category, ctx),
                       environmentFacet(),
                       toggleFacet(FacetId::installed, tr("Installed in target")),
                       toggleFacet(FacetId::updates, tr("Update available")),
                       toggleFacet(FacetId::hideInstalled, tr("Hide installed")) };
            break;
        case Kind::ResourcePack:
            facets = { providerFacet(kind),
                       versionFacet(ctx),
                       vocabularyFacet(FacetId::resolution, tr("Resolution"), resolutionVocabulary(), kHeaderResolutions, ctx,
                                       &resolutionLabel),
                       categoryFacet(FacetId::category, ctx),
                       categoryFacet(FacetId::feature, ctx),
                       toggleFacet(FacetId::installed, tr("Installed in target")) };
            break;
        case Kind::Shader:
            // No provider facet: platform and performance data exist only on Modrinth (docs/zero/CONTENT.md).
            facets = { versionFacet(ctx),
                       platformFacet(),
                       vocabularyFacet(FacetId::performance, tr("Performance impact"), performanceVocabulary(), kHeaderPerformance,
                                       ctx, &performanceOptionLabel),
                       categoryFacet(FacetId::feature, ctx),
                       categoryFacet(FacetId::category, ctx),
                       toggleFacet(FacetId::installed, tr("Installed in target")) };
            break;
        case Kind::World:
            // CurseForge only (Modrinth hosts no worlds): no provider facet. Worlds carry no metadata in the instance, so there is
            // no "Installed" facet either. The categories are CurseForge's own (classId 17), read from the provider.
            facets = { versionFacet(ctx), categoryFacet(FacetId::category, ctx) };
            break;
    }
    for (auto& f : facets) {
        f.disabledReason = disabledReasonFor(f.id, kind, ctx);
    }
    return facets;
}

// ---- FilterState ----

bool FilterState::operator==(const FilterState& other) const
{
    return query == other.query && sortId == other.sortId && normalized(selected) == normalized(other.selected);
}

QJsonObject FilterState::toJson() const
{
    QJsonObject sel;
    for (auto it = selected.cbegin(); it != selected.cend(); ++it) {
        const auto values = cleaned(it.value());
        if (!it.key().isEmpty() && !values.isEmpty()) {
            sel.insert(it.key(), QJsonArray::fromStringList(values));
        }
    }
    QJsonObject json;
    json.insert(QStringLiteral("query"), query);
    json.insert(QStringLiteral("sortId"), sortId);
    json.insert(QStringLiteral("selected"), sel);
    return json;
}

FilterState FilterState::fromJson(const QJsonObject& json)
{
    FilterState state;
    state.query = json.value(QStringLiteral("query")).toString();
    state.sortId = json.value(QStringLiteral("sortId")).toString();
    const auto sel = json.value(QStringLiteral("selected")).toObject();
    for (auto it = sel.constBegin(); it != sel.constEnd(); ++it) {
        QStringList values;
        for (const auto& v : it.value().toArray()) {
            if (v.isString()) {
                values.append(v.toString());
            }
        }
        values = cleaned(values);
        if (!it.key().isEmpty() && !values.isEmpty()) {
            state.selected.insert(it.key(), values);
        }
    }
    return state;
}

QStringList FilterState::values(const QString& facetId) const
{
    return selected.value(facetId);
}

bool FilterState::isSelected(const QString& facetId, const QString& value) const
{
    return selected.value(facetId).contains(value);
}

Provider FilterState::provider() const
{
    const auto values = selected.value(FacetId::provider);
    if (values.isEmpty()) {
        return Provider::Modrinth;
    }
    return providerFromId(values.constFirst()).value_or(Provider::Modrinth);
}

// ---- Chips ----

QList<Chip> chipsFor(const FilterState& state, const QList<Facet>& facets, const FilterState& defaults)
{
    QList<Chip> chips;
    for (const auto& f : facets) {
        const bool inactive = !f.isEnabled();
        if (f.id == FacetId::provider) {
            if (state.provider() != defaults.provider()) {
                const auto p = state.provider();
                chips.append({ .facetId = f.id,
                               .value = providerId(p),
                               .label = QStringLiteral("%1: %2").arg(f.label, providerName(p)),
                               .isDefault = false,
                               .isQuery = false,
                               .isInactive = inactive });
            }
            continue;
        }
        const auto defaultValues = defaults.values(f.id);
        for (const auto& value : cleaned(state.values(f.id))) {
            const QString label = f.kind == FacetKind::Toggle ? f.label : QStringLiteral("%1: %2").arg(f.label, f.optionLabel(value));
            chips.append({ .facetId = f.id,
                           .value = value,
                           .label = label,
                           .isDefault = defaultValues.contains(value),
                           .isQuery = false,
                           .isInactive = inactive });
        }
    }
    const auto query = state.query.trimmed();
    if (!query.isEmpty()) {
        chips.append({ .facetId = QString(),
                       .value = query,
                       .label = QStringLiteral("\"%1\"").arg(query),
                       .isDefault = query == defaults.query.trimmed(),
                       .isQuery = true,
                       .isInactive = false });
    }
    return chips;
}

FilterState withoutChip(FilterState state, const Chip& chip)
{
    if (chip.isQuery) {
        state.query.clear();
        return state;
    }
    auto it = state.selected.find(chip.facetId);
    if (it == state.selected.end()) {
        return state;
    }
    it.value().removeAll(chip.value);
    if (it.value().isEmpty()) {
        state.selected.erase(it);
    }
    return state;
}

FilterState withoutChip(FilterState state, const Chip& chip, const FilterState& defaults)
{
    if (!chip.isQuery && chip.facetId == FacetId::provider) {
        const auto values = defaults.values(FacetId::provider);
        if (values.isEmpty()) {
            state.selected.remove(FacetId::provider);
        } else {
            state.selected.insert(FacetId::provider, values);
        }
        return state;
    }
    return withoutChip(std::move(state), chip);
}

FilterState clearAll(const FilterState& current, const FilterState& defaults)
{
    FilterState out = defaults;
    out.sortId = current.sortId;
    return out;
}

FilterState showEverything(const FilterState& current)
{
    FilterState out;
    out.sortId = current.sortId;
    if (const auto provider = current.values(FacetId::provider); !provider.isEmpty()) {
        out.selected.insert(FacetId::provider, provider);
    }
    return out;
}

bool needsNewSearch(const FilterState& a, const FilterState& b, const QList<Facet>& facets)
{
    if (a.query.trimmed() != b.query.trimmed() || a.sortId != b.sortId || a.provider() != b.provider()) {
        return true;
    }
    return std::ranges::any_of(facets, [&a, &b](const Facet& f) {
        return f.side == FacetSide::Server && f.id != FacetId::provider && cleaned(a.values(f.id)) != cleaned(b.values(f.id));
    });
}

// ---- Sorting ----

QList<SortOption> sortOptions(Kind /*kind*/, Provider provider)
{
    switch (provider) {
        case Provider::Modrinth:
            return { { QStringLiteral("relevance"), tr("Relevance") },
                     { QStringLiteral("downloads"), tr("Downloads") },
                     { QStringLiteral("follows"), tr("Follows") },
                     { QStringLiteral("newest"), tr("Newest") },
                     { QStringLiteral("updated"), tr("Recently updated") } };
        case Provider::CurseForge:
            return { { QStringLiteral("featured"), tr("Featured") },
                     { QStringLiteral("popularity"), tr("Popularity") },
                     { QStringLiteral("downloads"), tr("Downloads") },
                     { QStringLiteral("updated"), tr("Recently updated") },
                     { QStringLiteral("name"), tr("Name") } };
        case Provider::Technic:
            return {};
    }
    return {};
}

std::optional<ResourceAPI::SortingMethod> sortingFor(const QString& sortId, const QList<ResourceAPI::SortingMethod>& providerMethods)
{
    // ZERO sort id -> the provider method names (ModrinthAPI / FlameAPI::getSortingMethods) that implement it.
    static const QMap<QString, QStringList> s_names = {
        { QStringLiteral("relevance"), { QStringLiteral("relevance") } },
        { QStringLiteral("downloads"), { QStringLiteral("downloads"), QStringLiteral("TotalDownloads") } },
        { QStringLiteral("follows"), { QStringLiteral("follows") } },
        { QStringLiteral("newest"), { QStringLiteral("newest") } },
        { QStringLiteral("updated"), { QStringLiteral("updated"), QStringLiteral("LastUpdated") } },
        { QStringLiteral("name"), { QStringLiteral("Name") } },
        { QStringLiteral("featured"), { QStringLiteral("Featured") } },
        { QStringLiteral("popularity"), { QStringLiteral("Popularity") } },
    };
    const auto names = s_names.value(sortId);
    for (const auto& method : providerMethods) {
        if (names.contains(method.name)) {
            return method;
        }
    }
    return std::nullopt;
}

// ---- Search arguments ----

ResourceAPI::SearchArgs toSearchArgs(const FilterState& state,
                                     Kind kind,
                                     Provider provider,
                                     int offset,
                                     const QList<ResourceAPI::SortingMethod>& providerMethods,
                                     const QList<Facet>& facets)
{
    ResourceAPI::SearchArgs args;
    args.type = resourceTypeOf(kind);
    args.offset = offset;
    if (const auto query = state.query.trimmed(); !query.isEmpty()) {
        args.search = query;
    }
    if (!state.sortId.isEmpty()) {
        args.sorting = sortingFor(state.sortId, providerMethods);
    }
    if (provider == Provider::Technic) {
        return args;
    }

    // Minecraft version: Modrinth takes them all (one OR group). CurseForge accepts a single gameVersion: with exactly one
    // selected it is sent; with several none is sent (sending the first would never fetch items that only match another
    // one) and matchesClient() keeps the items matching any of them.
    if (const auto versions = cleaned(state.values(FacetId::mcVersion)); !versions.isEmpty()) {
        if (provider != Provider::CurseForge || versions.size() == 1) {
            std::vector<Version> list;
            for (const auto& v : versions) {
                list.emplace_back(v);
            }
            args.versions = std::move(list);
        }
    }

    if (kind == Kind::Modpack || kind == Kind::Mod) {
        ModPlatform::ModLoaderTypes loaders;
        for (const auto& name : cleaned(state.values(FacetId::loader))) {
            const auto type = ModPlatform::getModLoaderFromString(name);
            if (type == ModPlatform::Fabric || type == ModPlatform::Forge || type == ModPlatform::NeoForge || type == ModPlatform::Quilt) {
                loaders |= type;
            }
        }
        if (loaders != 0) {
            args.loaders = loaders;
        }
    }

    const auto categories = offered(state, facets, FacetId::category);
    if (provider == Provider::CurseForge) {
        if (!categories.isEmpty()) {
            args.categoryIds = categories;
        }
        return args;
    }

    // Modrinth: every category-like facet is one OR group; groups are AND-ed.
    QList<QStringList> groups;
    auto addGroup = [&groups](const QStringList& values) {
        if (!values.isEmpty()) {
            groups.append(values);
        }
    };
    addGroup(categories);
    if (kind == Kind::ResourcePack || kind == Kind::Shader) {
        addGroup(offered(state, facets, FacetId::feature));
    }
    if (kind == Kind::ResourcePack) {
        addGroup(offered(state, facets, FacetId::resolution));
    }
    if (kind == Kind::Shader) {
        addGroup(offered(state, facets, FacetId::platform));
        addGroup(offered(state, facets, FacetId::performance));
    }
    if (!groups.isEmpty()) {
        args.categoryGroups = groups;
    }

    if (kind == Kind::Mod) {
        if (const auto env = offered(state, facets, FacetId::environment); !env.isEmpty()) {
            if (const auto side = ModPlatform::SideType::fromString(env.constFirst()); side.isValid()) {
                args.side = side;
            }
        }
    }
    return args;
}

// ---- Client predicate ----

bool matchesClient(const FilterState& state, const QList<Facet>& facets, const ItemFacts& item)
{
    for (const auto& f : facets) {
        if (!f.isEnabled()) {
            continue;
        }
        const auto values = cleaned(state.values(f.id));
        if (values.isEmpty()) {
            continue;
        }
        if (f.side == FacetSide::Client) {
            if (!values.contains(kToggleOn)) {
                continue;
            }
            if (f.id == FacetId::installed && !item.installed) {
                return false;
            }
            if (f.id == FacetId::updates && !item.updateAvailable) {
                return false;
            }
            if (f.id == FacetId::hideInstalled && item.installed) {
                return false;
            }
            continue;
        }
        // Server facets CurseForge cannot apply in full (one Minecraft version per search): "any of" on the item's data.
        if (item.provider == Provider::CurseForge) {
            // A single value was sent to the server, which applied it: CurseForge's latestFilesIndexes is partial, so
            // re-checking it here would hide valid hits. Only values the server did not apply are checked.
            if (values.size() == 1) {
                continue;
            }
            if (f.id == FacetId::mcVersion && !item.gameVersions.isEmpty() && !anyOf(values, item.gameVersions)) {
                return false;
            }
            if (f.id == FacetId::loader && !item.loaderNames.isEmpty() && !anyOf(values, item.loaderNames)) {
                return false;
            }
        }
    }
    return true;
}

// ---- Presentation helpers ----

QString performanceLabel(const QStringList& categories)
{
    int low = -1;
    int high = -1;
    for (const auto& [tag, rank] : kPerformance) {
        if (categories.contains(tag)) {
            low = low < 0 ? rank : std::min(low, rank);
            high = std::max(high, rank);
        }
    }
    if (low < 0) {
        return QString();
    }
    if (low == high) {
        return profileLabel(low);
    }
    return QStringLiteral("%1 %2 %3").arg(profileLabel(low), QChar(0x2013), profileLabel(high));
}

QStringList resolutionsOf(const QStringList& categories)
{
    QStringList out;
    for (const auto& r : kResolutions) {
        if (categories.contains(r)) {
            out.append(r);
        }
    }
    return out;
}

}  // namespace Zero::Content
