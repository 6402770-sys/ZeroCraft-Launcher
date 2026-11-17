// SPDX-License-Identifier: GPL-3.0-only
#include "ModpackSource.h"

#include <QCoreApplication>
#include <QHash>
#include <QPair>
#include <QRegularExpression>

#include <algorithm>

#include "Markdown.h"
#include "StringUtils.h"
#include "Version.h"

namespace Zero::Content {

namespace {
bool isReleaseText(const QString& version)
{
    static const QRegularExpression re(QStringLiteral("^\\d+\\.\\d+(\\.\\d+)?$"));
    return re.match(version).hasMatch();
}

QString firstNonEmpty(const QString& a, const QString& b)
{
    return a.isEmpty() ? b : a;
}

/// Presentation fields of the item from its pack. Fields the pack does not have keep the item's current value.
void fillFromPack(ModpackItem& item)
{
    const ModPlatform::IndexedPack& pack = *item.pack;
    item.id = pack.addonId.toString();
    item.slug = firstNonEmpty(pack.slug, item.slug);
    item.name = firstNonEmpty(pack.name, item.name);
    if (!pack.authors.isEmpty()) {
        item.author = pack.authors.first().name;
    }
    if (!pack.description.isEmpty()) {
        item.summary = plainText(pack.description);
    }
    item.iconUrl = firstNonEmpty(pack.logoUrl, item.iconUrl);
    // Artwork: the provider's featured image, else the gallery image marked featured, else the first one.
    QString artwork = pack.featuredImageUrl;
    if (artwork.isEmpty()) {
        for (const auto& image : pack.gallery) {
            if (image.featured && !image.url.isEmpty()) {
                artwork = image.url;
                break;
            }
        }
    }
    if (artwork.isEmpty() && !pack.gallery.isEmpty()) {
        artwork = pack.gallery.first().url;
    }
    item.artworkUrl = firstNonEmpty(artwork, item.artworkUrl);
    if (!pack.gameVersions.isEmpty()) {
        item.gameVersions = pack.gameVersions;
    }
    if (!pack.loaderNames.isEmpty()) {
        item.loaderNames = pack.loaderNames;
    }
    if (!pack.categoryIds.isEmpty()) {
        item.categoryIds = pack.categoryIds;
    }
    if (pack.downloads >= 0) {
        item.downloads = pack.downloads;
    }
    if (pack.dateModified.isValid()) {
        item.updated = pack.dateModified;
    }
    item.websiteUrl = firstNonEmpty(pack.websiteUrl, item.websiteUrl);
}
}  // namespace

QString ModpackItem::key() const
{
    return providerId(provider) + QLatin1Char(':') + id;
}

ModpackItemPtr itemFromPack(Provider provider, const ModPlatform::IndexedPack& pack)
{
    auto item = std::make_shared<ModpackItem>();
    item->provider = provider;
    item->pack = std::make_shared<ModPlatform::IndexedPack>(pack);
    fillFromPack(*item);
    return item;
}

ModpackItemPtr itemFromTechnicSummary(const TechnicPackSummary& summary)
{
    auto item = std::make_shared<ModpackItem>();
    item->provider = Provider::Technic;
    item->id = summary.slug;
    item->slug = summary.slug;
    item->name = previewTitle(nullptr, summary.name, summary.slug);
    item->iconUrl = summary.iconUrl;
    item->websiteUrl = summary.platformUrl;
    return item;
}

void applyTechnicDetails(ModpackItem& item, const TechnicPackDetails& details)
{
    item.technic = details;
    item.detailsLoaded = true;
    item.name = previewTitle(&details, item.name, item.slug);
    item.author = details.author;
    item.summary = plainTextSummary(details.description, 240);
    if (!details.iconUrl.isEmpty()) {
        item.iconUrl = details.iconUrl;
    }
    // The pack's logo is the closest thing to artwork Technic has; its background image is the wide one.
    item.artworkUrl = firstNonEmpty(details.backgroundUrl, firstNonEmpty(details.logoUrl, item.artworkUrl));
    if (!details.minecraftVersion.isEmpty()) {
        item.gameVersions = QStringList{ details.minecraftVersion };
    }
    if (details.installs > 0) {
        item.downloads = details.installs;
    }
    item.websiteUrl = firstNonEmpty(details.platformUrl, item.websiteUrl);
}

void applyProjectDetails(ModpackItem& item, const ModPlatform::IndexedPack& details)
{
    if (!item.pack) {
        item.pack = std::make_shared<ModPlatform::IndexedPack>(details);
    } else {
        // Versions and their state belong to the pack but not to the details answer.
        auto versions = std::move(item.pack->versions);
        const bool versionsLoaded = item.pack->versionsLoaded;
        ModPlatform::mergeProjectDetails(*item.pack, details);
        item.pack->versions = std::move(versions);
        item.pack->versionsLoaded = versionsLoaded;
    }
    fillFromPack(item);
    item.detailsLoaded = true;
}

ModpackVersion versionFromIndexed(const ModPlatform::IndexedVersion& version)
{
    ModpackVersion out;
    out.id = version.fileId.toString();
    out.name = version.version.isEmpty() ? version.versionNumber : version.version;
    out.number = (version.versionNumber != out.name && !version.versionNumber.isEmpty()) ? version.versionNumber : QString();
    switch (version.versionType.value()) {
        case ModPlatform::IndexedVersionType::Release:
            out.type = ModpackVersion::Type::Release;
            break;
        case ModPlatform::IndexedVersionType::Beta:
            out.type = ModpackVersion::Type::Beta;
            break;
        case ModPlatform::IndexedVersionType::Alpha:
            out.type = ModpackVersion::Type::Alpha;
            break;
        default:
            out.type = ModpackVersion::Type::Unknown;
            break;
    }
    out.gameVersions = version.mcVersion;
    for (const auto loader : ModPlatform::modLoaderTypesToList(version.loaders)) {
        const QString name = ModPlatform::getModLoaderAsString(loader);
        if (!name.isEmpty() && !out.loaders.contains(name)) {
            out.loaders.append(name);
        }
    }
    out.date = QDateTime::fromString(version.date, Qt::ISODate);
    out.size = version.fileSize;
    out.downloads = version.downloads;
    if (!version.changelog.isEmpty()) {
        out.changelog = version.changelog;
        out.changelogLoaded = true;
    }
    out.downloadUrl = version.downloadUrl;
    return out;
}

QString formatCount(qint64 count)
{
    if (count < 0) {
        return {};
    }
    auto scaled = [](qint64 value, qint64 unit, const char* suffix) {
        const double v = double(value) / double(unit);
        // One decimal below 100 ("1.2K", "12.3M"), none from 100 up ("845K", "123M"); never "1.0K".
        QString text = v < 100.0 ? QString::number(v, 'f', 1) : QString::number(qRound64(v));
        if (text.endsWith(QStringLiteral(".0"))) {
            text.chop(2);
        }
        return text + QLatin1String(suffix);
    };
    if (count < 1000) {
        return QString::number(count);
    }
    if (count < 1000000) {
        // 999,950 would round to "1000K": promote to M instead.
        if (count >= 999500) {
            return scaled(count, 1000000, "M");
        }
        return scaled(count, 1000, "K");
    }
    if (count < 1000000000) {
        if (count >= 999500000) {
            return scaled(count, 1000000000, "B");
        }
        return scaled(count, 1000000, "M");
    }
    return scaled(count, 1000000000, "B");
}

QString minecraftRangeText(const QStringList& gameVersions)
{
    QList<Version> releases;
    QStringList texts;
    for (const auto& v : gameVersions) {
        if (isReleaseText(v) && !texts.contains(v)) {
            texts.append(v);
            releases.append(Version(v));
        }
    }
    if (releases.isEmpty()) {
        return {};
    }
    int lo = 0, hi = 0;
    for (int i = 1; i < int(releases.size()); ++i) {
        if (releases[i] < releases[lo]) {
            lo = i;
        }
        if (releases[i] > releases[hi]) {
            hi = i;
        }
    }
    if (lo == hi) {
        return texts[lo];
    }
    return texts[lo] + QStringLiteral(" – ") + texts[hi];
}

QStringList loaderLabels(const QStringList& loaderNames)
{
    static const QList<QPair<QString, QString>> known = { { QStringLiteral("fabric"), QStringLiteral("Fabric") },
                                                           { QStringLiteral("forge"), QStringLiteral("Forge") },
                                                           { QStringLiteral("neoforge"), QStringLiteral("NeoForge") },
                                                           { QStringLiteral("quilt"), QStringLiteral("Quilt") } };
    QStringList out;
    for (const auto& [id, label] : known) {
        if (loaderNames.contains(id, Qt::CaseInsensitive)) {
            out.append(label);
        }
    }
    return out;
}

QStringList shaderPlatformLabels(const QStringList& loaderNames)
{
    static const QList<QPair<QString, QString>> known = { { QStringLiteral("iris"), QStringLiteral("Iris") },
                                                           { QStringLiteral("optifine"), QStringLiteral("OptiFine") },
                                                           { QStringLiteral("canvas"), QStringLiteral("Canvas") } };
    QStringList out;
    for (const auto& [id, label] : known) {
        if (loaderNames.contains(id, Qt::CaseInsensitive)) {
            out.append(label);
        }
    }
    return out;
}

QString providerBadge(Provider provider)
{
    return providerName(provider);
}

QString managedPackType(Provider provider)
{
    switch (provider) {
        case Provider::Modrinth:
            return QStringLiteral("modrinth");
        case Provider::CurseForge:
            return QStringLiteral("flame");
        case Provider::Technic:
            return QStringLiteral("technic");
    }
    return {};
}

QString managedKey(const QString& type, const QString& id)
{
    return type + QLatin1Char(':') + id;
}

QString installedKey(const ModpackItem& item)
{
    return managedKey(managedPackType(item.provider), item.id);
}

QString licenseText(const QString& license)
{
    if (license.isEmpty()) {
        return {};
    }
    static const QHash<QString, QString> spelled = {
        { QStringLiteral("LicenseRef-All-Rights-Reserved"), QCoreApplication::translate("Zero::Content::ModpackSource", "All Rights Reserved") },
        { QStringLiteral("ARR"), QCoreApplication::translate("Zero::Content::ModpackSource", "All Rights Reserved") },
        { QStringLiteral("LicenseRef-Custom"), QCoreApplication::translate("Zero::Content::ModpackSource", "Custom license") },
        { QStringLiteral("LicenseRef-Public-Domain"), QCoreApplication::translate("Zero::Content::ModpackSource", "Public domain") },
    };
    if (const auto it = spelled.constFind(license); it != spelled.cend()) {
        return it.value();
    }
    QString text = license;
    if (text.startsWith(QLatin1String("LicenseRef-"))) {
        text = text.mid(11).replace(QLatin1Char('-'), QLatin1Char(' '));
    }
    return text;
}

QString overviewHtml(const ModpackItem& item)
{
    if (item.technic) {
        return StringUtils::htmlListPatch(item.technic->description);
    }
    if (item.pack) {
        const auto& body = item.pack->extraData.body;
        if (!body.trimmed().isEmpty()) {
            if (item.provider == Provider::Modrinth) {
                return StringUtils::htmlListPatch(markdownToHTML(body));
            }
            return StringUtils::htmlListPatch(body);
        }
    }
    return item.summary.toHtmlEscaped();
}

ItemFacts factsOf(const ModpackItem& item, const QSet<QString>& installedKeys)
{
    ItemFacts facts;
    facts.provider = item.provider;
    facts.gameVersions = item.gameVersions;
    facts.loaderNames = item.loaderNames;
    facts.categories = item.categoryIds;
    facts.installed = installedKeys.contains(installedKey(item));
    return facts;
}

// ---- ResultPager ----

void ResultPager::reset(quint64 generation)
{
    m_generation = generation;
    m_offset = 0;
    m_loading = false;
    m_ended = false;
    m_failed = false;
}

bool ResultPager::begin()
{
    if (!canLoadMore()) {
        return false;
    }
    m_loading = true;
    return true;
}

bool ResultPager::accept(quint64 generation, int offset, int rawCount)
{
    if (generation != m_generation || !m_loading || offset != m_offset) {
        return false;
    }
    m_loading = false;
    m_offset += std::max(0, rawCount);
    // A short (or empty) page is the last one.
    if (rawCount < m_pageSize) {
        m_ended = true;
    }
    return true;
}

bool ResultPager::fail(quint64 generation)
{
    if (generation != m_generation || !m_loading) {
        return false;
    }
    m_loading = false;
    m_failed = true;
    return true;
}

// ---- source set ----

ModpackSource* ModpackSourceSet::source(Provider provider) const
{
    switch (provider) {
        case Provider::Modrinth:
            return modrinth;
        case Provider::CurseForge:
            return curseforge;
        case Provider::Technic:
            return technic;
    }
    return nullptr;
}

QList<Facet> modpackFacets(const FacetContext& ctx, const ModpackSourceSet& sources)
{
    QList<Facet> facets = facetsFor(Kind::Modpack, ctx);
    for (auto& facet : facets) {
        if (facet.id != FacetId::provider) {
            continue;
        }
        QList<FacetOption> kept;
        for (const auto& option : std::as_const(facet.options)) {
            const auto p = providerFromId(option.value);
            if (p && sources.has(*p)) {
                kept.append(option);
            }
        }
        facet.options = kept;
    }
    return facets;
}

}  // namespace Zero::Content
