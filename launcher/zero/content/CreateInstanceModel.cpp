// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Create Instance core (see CreateInstanceModel.h).
#include "CreateInstanceModel.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QMimeDatabase>

#include <algorithm>

namespace Zero {
namespace Create {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("Zero::Create", text);
}

/// `equals` of the net.minecraft requirement, empty when there is none.
QString minecraftRequirement(const Meta::Version& version)
{
    for (const Meta::Require& req : version.requiredSet())
        if (req.uid == kMinecraftUid)
            return req.equalsVersion;
    return {};
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Minecraft versions
// ---------------------------------------------------------------------------------------------------------------------

VersionType versionTypeFromMeta(const QString& metaType)
{
    if (metaType == QLatin1String("release"))
        return VersionType::Release;
    if (metaType == QLatin1String("snapshot") || metaType == QLatin1String("old_snapshot"))
        return VersionType::Snapshot;
    if (metaType == QLatin1String("old_beta"))
        return VersionType::Beta;
    if (metaType == QLatin1String("old_alpha"))
        return VersionType::Alpha;
    if (metaType == QLatin1String("experiment"))
        return VersionType::Experiment;
    return VersionType::Other;
}

QString versionTypeLabel(VersionType type)
{
    switch (type) {
        case VersionType::Release:
            return tr("Release");
        case VersionType::Snapshot:
            return tr("Snapshot");
        case VersionType::Beta:
            return tr("Beta");
        case VersionType::Alpha:
            return tr("Alpha");
        case VersionType::Experiment:
            return tr("Experiment");
        case VersionType::Other:
            break;
    }
    return {};
}

QList<VersionFilter> versionFilters()
{
    return { VersionFilter::Releases, VersionFilter::Snapshots, VersionFilter::Old, VersionFilter::Experiments };
}

QString versionFilterLabel(VersionFilter filter)
{
    switch (filter) {
        case VersionFilter::Releases:
            return tr("Releases");
        case VersionFilter::Snapshots:
            return tr("Snapshots");
        case VersionFilter::Old:
            return tr("Old");
        case VersionFilter::Experiments:
            return tr("Experiments");
    }
    return {};
}

bool filterAccepts(VersionFilter filter, VersionType type)
{
    switch (filter) {
        case VersionFilter::Releases:
            return type == VersionType::Release;
        case VersionFilter::Snapshots:
            return type == VersionType::Snapshot;
        case VersionFilter::Old:
            return type == VersionType::Beta || type == VersionType::Alpha;
        case VersionFilter::Experiments:
            return type == VersionType::Experiment;
    }
    return false;
}

VersionFilter filterFor(VersionType type)
{
    switch (type) {
        case VersionType::Snapshot:
            return VersionFilter::Snapshots;
        case VersionType::Beta:
        case VersionType::Alpha:
            return VersionFilter::Old;
        case VersionType::Experiment:
            return VersionFilter::Experiments;
        case VersionType::Release:
        case VersionType::Other:
            break;
    }
    return VersionFilter::Releases;
}

QList<VersionRow> buildVersionRows(const QList<MinecraftVersionFact>& facts)
{
    QList<VersionRow> rows;
    rows.reserve(facts.size());
    for (const MinecraftVersionFact& f : facts) {
        VersionRow row;
        row.id = f.id;
        row.type = versionTypeFromMeta(f.type);
        row.released = f.released;
        row.recommended = f.recommended;
        rows.append(row);
    }
    std::stable_sort(rows.begin(), rows.end(), [](const VersionRow& a, const VersionRow& b) { return a.released > b.released; });
    for (VersionRow& row : rows) {
        if (row.type == VersionType::Release) {
            row.latest = true;
            break;
        }
    }
    return rows;
}

QList<VersionRow> filterVersionRows(const QList<VersionRow>& rows, VersionFilter filter, const QString& search)
{
    const QString needle = search.trimmed();
    QList<VersionRow> out;
    for (const VersionRow& row : rows) {
        if (!filterAccepts(filter, row.type))
            continue;
        if (!needle.isEmpty() && !row.id.contains(needle, Qt::CaseInsensitive))
            continue;
        out.append(row);
    }
    return out;
}

int preferredVersionRow(const QList<VersionRow>& rows)
{
    for (int i = 0; i < rows.size(); ++i)
        if (rows.at(i).recommended)
            return i;
    return rows.isEmpty() ? -1 : 0;
}

int indexOfVersion(const QList<VersionRow>& rows, const QString& id)
{
    for (int i = 0; i < rows.size(); ++i)
        if (rows.at(i).id == id)
            return i;
    return -1;
}

// ---------------------------------------------------------------------------------------------------------------------
// Mod loaders
// ---------------------------------------------------------------------------------------------------------------------

QList<Loader> loaderOrder()
{
    return { Loader::Fabric, Loader::Forge, Loader::NeoForge, Loader::Quilt, Loader::LiteLoader };
}

QString loaderUid(Loader loader)
{
    switch (loader) {
        case Loader::Fabric:
            return QStringLiteral("net.fabricmc.fabric-loader");
        case Loader::Forge:
            return QStringLiteral("net.minecraftforge");
        case Loader::NeoForge:
            return QStringLiteral("net.neoforged");
        case Loader::Quilt:
            return QStringLiteral("org.quiltmc.quilt-loader");
        case Loader::LiteLoader:
            return QStringLiteral("com.mumfrey.liteloader");
    }
    return {};
}

QString loaderName(Loader loader)
{
    switch (loader) {
        case Loader::Fabric:
            return QStringLiteral("Fabric");
        case Loader::Forge:
            return QStringLiteral("Forge");
        case Loader::NeoForge:
            return QStringLiteral("NeoForge");
        case Loader::Quilt:
            return QStringLiteral("Quilt");
        case Loader::LiteLoader:
            return QStringLiteral("LiteLoader");
    }
    return {};
}

std::optional<Loader> loaderFromUid(const QString& uid)
{
    for (Loader l : loaderOrder())
        if (loaderUid(l) == uid)
            return l;
    return std::nullopt;
}

std::optional<Loader> loaderFromName(const QString& name)
{
    for (Loader l : loaderOrder())
        if (loaderName(l).compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return l;
    return std::nullopt;
}

bool usesIntermediary(Loader loader)
{
    return loader == Loader::Fabric || loader == Loader::Quilt;
}

QStringList moddedMetaUids()
{
    QStringList uids{ kMinecraftUid, kIntermediaryUid };
    for (Loader l : loaderOrder())
        uids.append(loaderUid(l));
    return uids;
}

int preferredLoaderRow(const QList<LoaderVersionRow>& rows)
{
    for (int i = 0; i < rows.size(); ++i)
        if (rows.at(i).recommended)
            return i;
    return rows.isEmpty() ? -1 : 0;
}

void LoaderCatalog::setLoaderVersions(Loader loader, const QList<LoaderVersionFact>& facts)
{
    m_facts.insert(loader, facts);
}

void LoaderCatalog::clearLoader(Loader loader)
{
    m_facts.remove(loader);
}

void LoaderCatalog::setIntermediaryVersions(const QSet<QString>& minecraftIds)
{
    m_intermediary = minecraftIds;
    m_intermediaryKnown = true;
}

void LoaderCatalog::clearIntermediary()
{
    m_intermediary.clear();
    m_intermediaryKnown = false;
}

bool LoaderCatalog::isKnown(Loader loader) const
{
    return m_facts.contains(loader) && (!usesIntermediary(loader) || m_intermediaryKnown);
}

QList<LoaderVersionRow> LoaderCatalog::versionsFor(Loader loader, const QString& minecraft) const
{
    QList<LoaderVersionRow> rows;
    if (minecraft.isEmpty() || !isKnown(loader))
        return rows;
    const bool intermediary = usesIntermediary(loader);
    if (intermediary && !m_intermediary.contains(minecraft))
        return rows;
    for (const LoaderVersionFact& f : m_facts.value(loader)) {
        if (!intermediary && f.requiresMinecraft != minecraft)
            continue;
        rows.append({ f.version, f.released, f.recommended });
    }
    std::stable_sort(rows.begin(), rows.end(),
                     [](const LoaderVersionRow& a, const LoaderVersionRow& b) { return a.released > b.released; });
    return rows;
}

bool LoaderCatalog::hasVersionsFor(Loader loader, const QString& minecraft) const
{
    if (minecraft.isEmpty() || !isKnown(loader))
        return false;
    if (usesIntermediary(loader))
        return m_intermediary.contains(minecraft) && !m_facts.value(loader).isEmpty();
    const auto& facts = m_facts.value(loader);
    return std::any_of(facts.begin(), facts.end(), [&](const LoaderVersionFact& f) { return f.requiresMinecraft == minecraft; });
}

LoaderCatalog::Availability LoaderCatalog::availability(const QString& minecraft) const
{
    Availability a;
    for (Loader l : loaderOrder()) {
        if (!isKnown(l))
            a.unknown.append(l);
        else if (hasVersionsFor(l, minecraft))
            a.available.append(l);
        else
            a.unavailable.append(l);
    }
    return a;
}

QString unavailableCaption(const QString& minecraft, const QList<Loader>& loaders)
{
    if (loaders.isEmpty())
        return {};
    QStringList names;
    for (Loader l : loaders)
        names.append(loaderName(l));
    return tr("Not available for %1: %2").arg(minecraft, names.join(QStringLiteral(", ")));
}

// ---------------------------------------------------------------------------------------------------------------------
// Meta adapters
// ---------------------------------------------------------------------------------------------------------------------

QList<MinecraftVersionFact> minecraftFacts(const QList<Meta::Version::Ptr>& versions)
{
    QList<MinecraftVersionFact> out;
    out.reserve(versions.size());
    for (const Meta::Version::Ptr& v : versions) {
        if (!v)
            continue;
        out.append({ v->version(), v->type(), v->time(), v->isRecommended() });
    }
    return out;
}

QList<LoaderVersionFact> loaderFacts(const QList<Meta::Version::Ptr>& versions)
{
    QList<LoaderVersionFact> out;
    out.reserve(versions.size());
    for (const Meta::Version::Ptr& v : versions) {
        if (!v)
            continue;
        out.append({ v->uid(), v->version(), minecraftRequirement(*v), v->isRecommended(), v->time() });
    }
    return out;
}

QSet<QString> intermediaryMinecraftIds(const QList<Meta::Version::Ptr>& versions)
{
    QSet<QString> ids;
    for (const Meta::Version::Ptr& v : versions) {
        if (!v)
            continue;
        const QString mc = minecraftRequirement(*v);
        ids.insert(mc.isEmpty() ? v->version() : mc);
    }
    return ids;
}

// ---------------------------------------------------------------------------------------------------------------------
// Name
// ---------------------------------------------------------------------------------------------------------------------

QString suggestName(const QString& minecraft, std::optional<Loader> loader)
{
    if (minecraft.isEmpty())
        return {};
    if (!loader)
        return minecraft;
    return QStringLiteral("%1 %2").arg(minecraft, loaderName(*loader));
}

QString validateName(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return tr("Enter a name for the instance.");
    if (trimmed.size() > kMaxNameLength)
        return tr("Use at most %1 characters.").arg(kMaxNameLength);
    return {};
}

// ---------------------------------------------------------------------------------------------------------------------
// Import
// ---------------------------------------------------------------------------------------------------------------------

ImportSource resolveLocalFile(const QString& path)
{
    ImportSource s;
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
        return s;
    const QFileInfo fi(trimmed);
    s.fileName = fi.fileName();
    if (!fi.exists() || !fi.isFile()) {
        s.kind = ImportSource::Kind::Invalid;
        s.error = tr("This file does not exist.");
        return s;
    }
    const QUrl url = QUrl::fromLocalFile(fi.absoluteFilePath());
    // Same test as ImportPage: zip by MIME type (non-latin names work), mrpack by suffix.
    const bool isZip = QMimeDatabase().mimeTypeForUrl(url).suffixes().contains(QStringLiteral("zip"));
    const bool isMrpack = fi.suffix().compare(QLatin1String("mrpack"), Qt::CaseInsensitive) == 0;
    if (!isZip && !isMrpack) {
        s.kind = ImportSource::Kind::Invalid;
        s.error = tr("Choose a .zip or .mrpack file.");
        return s;
    }
    s.kind = ImportSource::Kind::LocalFile;
    s.url = url;
    s.suggestedName = fi.completeBaseName();
    return s;
}

ImportSource resolveUrl(const QString& text)
{
    ImportSource s;
    QString input = text.trimmed();
    if (input.isEmpty())
        return s;
    const QUrl probe(input);
    const QString scheme = probe.scheme().toLower();
    if (scheme == QLatin1String("curseforge")) {
        s.kind = ImportSource::Kind::CurseForgeLink;
        s.url = probe;
        return s;
    }
    if ((scheme != QLatin1String("http") && scheme != QLatin1String("https")) || !probe.isValid() || probe.host().isEmpty()) {
        s.kind = ImportSource::Kind::Invalid;
        s.error = tr("Enter a link that starts with http:// or https://.");
        return s;
    }
    // ImportPage: a CurseForge "?client=y" link is rewritten to the direct "/file" download.
    if (input.endsWith(QLatin1String("?client=y"))) {
        input.chop(9);
        input.append(QLatin1String("/file"));
    }
    s.kind = ImportSource::Kind::RemoteUrl;
    s.url = QUrl::fromUserInput(input);
    s.fileName = s.url.fileName();
    s.suggestedName = QFileInfo(s.fileName).completeBaseName();
    return s;
}

QString detectedFormat(const QString& fileName)
{
    if (QFileInfo(fileName).suffix().compare(QLatin1String("mrpack"), Qt::CaseInsensitive) == 0)
        return tr("Modrinth pack");
    return {};
}

}  // namespace Create
}  // namespace Zero
