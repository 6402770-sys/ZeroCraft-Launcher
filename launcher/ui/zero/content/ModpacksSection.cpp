// SPDX-License-Identifier: GPL-3.0-only
#include "ModpacksSection.h"

#include <algorithm>

#include "FeaturedStrip.h"
#include "ModpackInstallDialog.h"
#include "ModpackRows.h"
#include "ui/zero/ZeroFormat.h"
#include "zero/content/ModpackInstall.h"

namespace Zero::Content {

ContentSectionSpec ModpacksSection::makeSpec()
{
    ContentSectionSpec spec;
    spec.kind = Kind::Modpack;
    spec.reveal = RevealSpec::modpacks();
    spec.objectName = QStringLiteral("zeroModpacksSection");
    spec.searchPlaceholder = tr("Search modpacks");
    spec.accessibleName = tr("Modpacks");
    spec.skeletonText = tr("Loading modpacks…");
    spec.makeDelegate = [](Zero::RemoteImageCache* images, QObject* parent) { return new ModpackDelegate(images, parent); };
    spec.makeAccessory = [](Zero::RemoteImageCache* images, QWidget* parent) { return new FeaturedStrip(images, parent); };
    return spec;
}

ModpacksSection::ModpacksSection(const ModpacksEnvironment& env, QWidget* parent)
    : ContentSection(env, makeSpec(), parent), m_modpacksEnv(env), m_featured(static_cast<FeaturedStrip*>(accessory()))
{
    connect(m_featured, &FeaturedStrip::activated, this, [this](const ModpackItemPtr& item) { selectItem(item); });
    connect(m_featured, &FeaturedStrip::installRequested, this,
            [this](const ModpackItemPtr& item) { emit installRequested(item, QString()); });
    init();
}

ModpackDelegate* ModpacksSection::delegate() const
{
    return static_cast<ModpackDelegate*>(contentDelegate());
}

// ---- hooks of the core ----

FilterState ModpacksSection::sectionDefaults() const
{
    FilterState defaults;
    defaults.selected.insert(FacetId::provider, { providerId(Provider::Modrinth) });
    return defaults;
}

QList<Facet> ModpacksSection::sectionFacets(const FacetContext& ctx) const
{
    return modpackFacets(ctx, environment().sources);
}

QSet<QString> ModpacksSection::readInstalledKeys()
{
    m_installed = m_modpacksEnv.installedPacks ? m_modpacksEnv.installedPacks() : QList<InstalledPack>();
    QSet<QString> keys;
    for (const auto& pack : std::as_const(m_installed)) {
        keys.insert(pack.key);
    }
    return keys;
}

void ModpacksSection::onInstalledChanged()
{
    m_featured->setInstalledKeys(installedKeys());
}

void ModpacksSection::onSelectionChanged(const ContentItemPtr& item)
{
    m_featured->setSelectedKey(item ? item->key() : QString());
}

void ModpacksSection::refreshAccessoryImages()
{
    m_featured->refreshImages();
}

bool ModpacksSection::hasAccessoryWork() const
{
    return m_featuredLoading || m_prefetching > 0;
}

bool ModpacksSection::isDialogGeneration(quint64 generation) const
{
    return generation == ModpackInstallDialog::kGeneration;
}

void ModpacksSection::onSearchStarted()
{
    m_prefetchQueue.clear();
}

void ModpacksSection::onViewStateChanged()
{
    updateFeatured();
}

void ModpacksSection::onReload()
{
    m_featuredRequested = false;
}

bool ModpacksSection::takesSearchAnswer(quint64 generation, bool ok, const QList<ContentItemPtr>& items)
{
    if (generation == 0 || generation != m_featuredGeneration) {
        return false;
    }
    m_featuredLoading = false;
    m_featured->setLoading(false);
    if (ok) {
        m_featured->setItems(items.mid(0, FeaturedStrip::kMaxCards));
        m_featured->setInstalledKeys(installedKeys());
    } else {
        m_featured->setItems({});  // the strip stays away; the list below shows its own errors
    }
    return true;
}

bool ModpacksSection::takesDetailsAnswer(quint64 generation, const ContentItemPtr& item)
{
    if (generation != kPrefetchGeneration) {
        return false;
    }
    m_prefetching = std::max(0, m_prefetching - 1);
    if (item) {
        model()->itemChanged(item);  // the card now shows author, description, Minecraft version
    }
    prefetchDetails();
    return true;
}

void ModpacksSection::onPageLoaded(const QList<ContentItemPtr>& items)
{
    if (provider() == Provider::Technic) {
        // Technic's list carries names and icons only: the first rows get their details in the background (a few at a time).
        for (const auto& item : items) {
            if (!item->detailsLoaded && m_prefetchQueue.size() + m_prefetching < kPrefetchMax) {
                m_prefetchQueue.append(item);
            }
        }
        prefetchDetails();
    }
}

void ModpacksSection::prefetchDetails()
{
    ModpackSource* source = environment().sources.technic;
    while (source && m_prefetching < kPrefetchParallel && !m_prefetchQueue.isEmpty()) {
        const ModpackItemPtr item = m_prefetchQueue.takeFirst();
        if (item->detailsLoaded) {
            continue;
        }
        ++m_prefetching;
        source->details(item, kPrefetchGeneration);
    }
}

// ---- featured ----

void ModpacksSection::updateFeatured()
{
    const bool defaultView = isDefaultView();
    m_featured->setCollapsed(!defaultView);
    if (defaultView && !m_featuredRequested) {
        requestFeatured();
    }
}

void ModpacksSection::requestFeatured()
{
    // CurseForge's own Featured list when it is available; otherwise Modrinth's most downloaded packs, labelled "Popular".
    const Provider p = environment().sources.curseforge ? Provider::CurseForge : Provider::Modrinth;
    ModpackSource* source = environment().sources.source(p);
    if (!source) {
        return;
    }
    m_featuredRequested = true;
    m_featuredLoading = true;
    m_featuredGeneration = nextGeneration();
    m_featured->setLabel(p == Provider::CurseForge ? tr("Featured on CurseForge") : tr("Popular on Modrinth"));
    m_featured->setLoading(true);
    FilterState featuredState;
    featuredState.sortId = p == Provider::CurseForge ? QStringLiteral("featured") : QStringLiteral("downloads");
    FacetContext ctx;
    ctx.provider = p;
    const ResourceAPI::SearchArgs args =
        toSearchArgs(featuredState, Kind::Modpack, p, 0, providerMethods(p), modpackFacets(ctx, environment().sources));
    source->search(args, m_featuredGeneration);
}

std::optional<InstalledPack> ModpacksSection::installedFor(const ModpackItem& item) const
{
    const QString key = installedKey(item);
    for (const auto& pack : m_installed) {
        if (pack.key == key) {
            return pack;
        }
    }
    return std::nullopt;
}

DetailsHeader ModpacksSection::headerFor(const ModpackItem& item) const
{
    DetailsHeader h;
    h.key = item.key();
    h.name = item.name;
    h.author = item.author;
    h.iconUrl = item.iconUrl;
    h.artworkUrl = item.artworkUrl;
    h.providerLabel = providerBadge(item.provider);
    h.websiteUrl = item.websiteUrl;
    if (const QString range = minecraftRangeText(item.gameVersions); !range.isEmpty()) {
        h.pills << range;
    }
    h.pills << loaderLabels(item.loaderNames);
    if (item.technic) {
        if (!item.technic->version.isEmpty()) {
            h.pills << tr("Version %1").arg(item.technic->version);
        }
        h.pills << item.technic->tags;
    }
    if (item.downloads >= 0) {
        h.stats.append({ QStringLiteral("download"), item.technic ? tr("%1 installs").arg(formatCount(item.downloads)) : tr("%1 downloads").arg(formatCount(item.downloads)),
                         tr("Downloads") });
    }
    if (item.technic && item.technic->runs > 0) {
        h.stats.append({ QStringLiteral("play"), tr("%1 runs").arg(formatCount(item.technic->runs)), tr("Runs") });
    }
    if (item.pack && item.pack->follows >= 0) {
        h.stats.append({ QStringLiteral("heart"), tr("%1 follows").arg(formatCount(item.pack->follows)), tr("Follows") });
    }
    if (item.updated.isValid()) {
        h.stats.append({ QStringLiteral("clock"), tr("Updated %1").arg(Format::relativeTime(item.updated.toMSecsSinceEpoch()).toLower()), tr("Last updated") });
    }
    if (item.pack) {
        if (const QString license = licenseText(item.pack->license); !license.isEmpty()) {
            h.stats.append({ QStringLiteral("file-text"), license, tr("License") });
        }
    }
    return h;
}

void ModpacksSection::onPrimaryAction()
{
    const auto installed = installedFor(*selectedItem());
    // "Update" updates the instance that manages the pack; a plain Install always makes a new one.
    const bool update = installed && panel()->primaryButton()->text() == tr("Update");
    emit installRequested(selectedItem(), update ? installed->instanceId : QString());
}

void ModpacksSection::updatePrimaryAction()
{
    if (!selectedItem()) {
        return;
    }
    const ModpackItem& item = *selectedItem();
    const auto installed = installedFor(item);
    const bool versionsKnown = hasVersions(item.key());
    const QList<ContentVersion> versions = versionsOf(item.key());
    bool enabled = false;
    QString text = tr("Install");
    QString note;
    if (item.provider == Provider::Technic) {
        if (item.technic) {
            enabled = item.technic->isInstallable();
            if (!enabled) {
                note = tr("This pack has no download.");
            }
        }
    } else if (versionsKnown) {
        enabled = !versions.isEmpty();
        if (!enabled) {
            note = tr("This provider offers no downloadable version of this pack.");
        }
    }
    if (installed) {
        const int defaultIndex = versionsKnown ? defaultVersionIndex(versions, item.provider) : -1;
        const bool newer = defaultIndex >= 0 && !installed->versionId.isEmpty() && versions.at(defaultIndex).id != installed->versionId;
        if (newer) {
            text = tr("Update");
            note = tr("%1 has %2 installed. The newest version is %3.")
                       .arg(installed->instanceName, installed->versionName.isEmpty() ? tr("an older version") : installed->versionName,
                            versions.at(defaultIndex).name);
        } else {
            note = tr("%1 is already installed. Installing again asks whether to update it or create a separate instance.").arg(installed->instanceName);
        }
    }
    panel()->setPrimaryAction(text, enabled, note);
}

}  // namespace Zero::Content
