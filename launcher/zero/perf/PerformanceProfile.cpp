// SPDX-License-Identifier: GPL-3.0-only
#include "PerformanceProfile.h"

#include <QObject>

#include <algorithm>

#include "zero/content/ModInstall.h"
#include "zero/content/ModpackSource.h"
#include "zero/perf/JvmArgsAdvisor.h"

namespace Zero::Perf {

namespace {

constexpr int kGb = 1024;

QString lit(const char* text)
{
    return QString::fromUtf8(text);
}

/// The whole table of projects, in one place.
const QList<Project>& allProjects()
{
    static const QList<Project> projects = {
        { lit("sodium"), lit("Sodium"), QObject::tr("Faster rendering, much higher FPS"), { lit("sodium") }, lit("embeddium"), true },
        { lit("embeddium"), lit("Embeddium"), QObject::tr("Faster rendering, much higher FPS"), { lit("embeddium") }, lit("sodium"), true },
        { lit("lithium"), lit("Lithium"), QObject::tr("Smoother game logic, less lag"), { lit("lithium") }, {}, false },
        { lit("ferrite-core"), lit("FerriteCore"), QObject::tr("Uses much less memory"), { lit("ferritecore") }, {}, false },
        { lit("immediatelyfast"), lit("ImmediatelyFast"), QObject::tr("Faster menus, text and entity drawing"), { lit("immediatelyfast") }, {}, false },
        { lit("entityculling"), lit("Entity Culling"), QObject::tr("Skips drawing what you cannot see"), { lit("entityculling") }, {}, false },
        { lit("modernfix"), lit("ModernFix"), QObject::tr("Faster startup, less memory"), { lit("modernfix") }, {}, false },
    };
    return projects;
}

QString lower(const QString& text)
{
    return text.toLower();
}

int floorGb(int mb)
{
    return (mb / kGb) * kGb;
}

QString majorsText(const QList<int>& majors)
{
    const QList<int> sorted = Java::sortedMajors(majors);
    QStringList parts;
    for (int major : sorted) {
        parts << QString::number(major);
    }
    if (parts.size() <= 1) {
        return parts.join(QString());
    }
    const QString last = parts.takeLast();
    return QObject::tr("%1 or %2").arg(parts.join(QStringLiteral(", ")), last);
}

QString javaName(const Java::Runtime& runtime)
{
    if (runtime.major > 0) {
        return QObject::tr("Java %1").arg(runtime.major);
    }
    if (!runtime.versionString.isEmpty()) {
        return QObject::tr("Java %1").arg(runtime.versionString);
    }
    return QObject::tr("Java");
}

}  // namespace

// ---- loaders and the table ----

Loader loaderOf(ModPlatform::ModLoaderTypes loaders)
{
    if (loaders.testFlag(ModPlatform::NeoForge)) {
        return Loader::NeoForge;
    }
    if (loaders.testFlag(ModPlatform::Forge)) {
        return Loader::Forge;
    }
    if (loaders.testFlag(ModPlatform::Quilt)) {
        return Loader::Quilt;
    }
    if (loaders.testFlag(ModPlatform::Fabric)) {
        return Loader::Fabric;
    }
    return Loader::None;
}

QString loaderName(Loader loader)
{
    switch (loader) {
        case Loader::Fabric:
            return QStringLiteral("Fabric");
        case Loader::Quilt:
            return QStringLiteral("Quilt");
        case Loader::NeoForge:
            return QStringLiteral("NeoForge");
        case Loader::Forge:
            return QStringLiteral("Forge");
        case Loader::None:
            break;
    }
    return {};
}

Project projectFor(const QString& slug)
{
    for (const Project& project : allProjects()) {
        if (project.slug == slug) {
            return project;
        }
    }
    return {};
}

QList<Entry> curatedFor(Loader loader)
{
    switch (loader) {
        case Loader::Fabric:
        case Loader::Quilt:
            return { { lit("sodium"), {} },      { lit("lithium"), {} },       { lit("ferrite-core"), {} },
                     { lit("immediatelyfast"), {} }, { lit("entityculling"), {} }, { lit("modernfix"), {} } };
        case Loader::NeoForge:
            // Sodium has a NeoForge build for recent versions only; older ones get Embeddium, the same renderer for Forge-family loaders.
            return { { lit("sodium"), lit("embeddium") }, { lit("ferrite-core"), {} },     { lit("modernfix"), {} },
                     { lit("entityculling"), {} },        { lit("immediatelyfast"), {} } };
        case Loader::Forge:
            return { { lit("embeddium"), {} }, { lit("ferrite-core"), {} }, { lit("modernfix"), {} }, { lit("entityculling"), {} } };
        case Loader::None:
            break;
    }
    return {};
}

QStringList slugsFor(Loader loader)
{
    QStringList out;
    for (const Entry& entry : curatedFor(loader)) {
        for (const QString& slug : { entry.slug, entry.fallbackSlug }) {
            if (!slug.isEmpty() && !out.contains(slug)) {
                out.append(slug);
            }
        }
    }
    return out;
}

// ---- rows ----

bool hasOptiFine(const InstalledSnapshot& installed)
{
    const auto isOptiFine = [](const QString& text) {
        const QString t = text.toLower();
        return t.contains(QLatin1String("optifine")) || t.contains(QLatin1String("optifabric"));
    };
    return std::any_of(installed.modIds.begin(), installed.modIds.end(), isOptiFine) ||
           std::any_of(installed.names.begin(), installed.names.end(), isOptiFine);
}

bool isInstalled(const QString& slug, const QString& projectId, const InstalledSnapshot& installed)
{
    if (!projectId.isEmpty()) {
        const QString key = Zero::Content::managedKey(Zero::Content::managedPackType(Zero::Content::Provider::Modrinth), projectId);
        for (const Zero::Content::InstalledMod& mod : installed.withMetadata) {
            if (mod.key == key) {
                return true;
            }
        }
    }
    const Project project = projectFor(slug);
    for (const QString& id : installed.modIds) {
        if (project.modIds.contains(lower(id))) {
            return true;
        }
    }
    return false;
}

QList<Row> buildRows(Loader loader, const QString& mcVersion, ModPlatform::ModLoaderTypes loaders, const FoundMap& found,
                     const InstalledSnapshot& installed)
{
    using namespace Zero::Content;
    QList<Row> rows;
    const bool optiFine = hasOptiFine(installed);

    const auto projectIdOf = [&found](const QString& slug) {
        const auto it = found.constFind(slug);
        return it != found.constEnd() && it->pack ? it->pack->addonId.toString() : QString();
    };
    const auto installedProject = [&](const QString& slug) { return !slug.isEmpty() && isInstalled(slug, projectIdOf(slug), installed); };

    for (const Entry& entry : curatedFor(loader)) {
        const Project primary = projectFor(entry.slug);
        Row row;
        row.slug = entry.slug;
        row.name = primary.name;
        row.benefit = primary.benefit;

        // The mod itself, or the one that does the same job, is already there.
        if (installedProject(entry.slug)) {
            row.state = RowState::Installed;
            rows.append(row);
            continue;
        }
        if (installedProject(primary.alternativeSlug)) {
            row.state = RowState::InstalledAlternative;
            row.alternativeName = projectFor(primary.alternativeSlug).name;
            rows.append(row);
            continue;
        }

        // The first project of the entry that has a file for this target.
        const QStringList candidates = entry.fallbackSlug.isEmpty() ? QStringList{ entry.slug } : QStringList{ entry.slug, entry.fallbackSlug };
        bool picked = false;
        for (const QString& slug : candidates) {
            const auto it = found.constFind(slug);
            if (it == found.constEnd() || !it->pack) {
                continue;
            }
            const QList<ModPlatform::IndexedVersion> fitting = compatibleVersions(it->versions, mcVersion, loaders);
            const int index = defaultModVersionIndex(fitting);
            if (index < 0) {
                continue;
            }
            const Project project = projectFor(slug);
            row.slug = slug;
            row.name = project.name.isEmpty() ? row.name : project.name;
            row.benefit = project.benefit.isEmpty() ? row.benefit : project.benefit;
            row.pack = it->pack;
            row.version = fitting.at(index);
            row.state = project.rendering && optiFine ? RowState::ConflictOptiFine : RowState::Offered;
            picked = true;
            break;
        }
        if (!picked) {
            row.state = RowState::Unavailable;
        }
        rows.append(row);
    }
    return rows;
}

// ---- memory ----

int recommendedMaxMb(int ramMb, int modCount, bool modded)
{
    if (ramMb <= 0) {
        return 0;
    }
    const int half = ramMb / 2;
    if (half < 2 * kGb) {
        return std::max(kGb, floorGb(half));
    }
    const int started = modCount > 0 ? (modCount + 74) / 75 : 0;
    int wanted = (modded ? 4 : 3) * kGb + started * kGb;
    wanted = std::min(wanted, 8 * kGb);
    wanted = std::min(wanted, floorGb(half));
    return std::max(wanted, 2 * kGb);
}

int recommendedMinMb(int maxMb)
{
    return std::max(0, std::min(kGb, maxMb / 2));
}

MemoryAdvice adviseMemory(int ramMb, int modCount, bool modded, int currentMaxMb)
{
    MemoryAdvice advice;
    advice.known = ramMb > 0;
    advice.ramMb = std::max(0, ramMb);
    advice.currentMaxMb = std::max(0, currentMaxMb);
    advice.recommendedMaxMb = recommendedMaxMb(ramMb, modCount, modded);
    advice.recommendedMinMb = recommendedMinMb(advice.recommendedMaxMb);
    return advice;
}

int currentMaxMb(bool overrideMemory, int instanceMaxMb, int globalMaxMb)
{
    return overrideMemory ? instanceMaxMb : globalMaxMb;
}

// ---- Java ----

std::optional<QString> javaProblemText(const Java::InstanceJava& instance, const Java::Diagnosis& diagnosis)
{
    using Java::Reason;
    using Java::Status;
    if (diagnosis.status != Status::Warning && diagnosis.status != Status::WillFail) {
        return std::nullopt;
    }
    const QString inUse = diagnosis.effective ? javaName(*diagnosis.effective) : QObject::tr("Java");
    switch (diagnosis.reason) {
        case Reason::IncompatibleMajor:
            return QObject::tr("%1 is set; this version needs Java %2.").arg(inUse, majorsText(instance.compatibleMajors));
        case Reason::IncompatibleIgnored:
            return QObject::tr("%1 does not fit this version (Java %2); the compatibility check is off.")
                .arg(inUse, majorsText(instance.compatibleMajors));
        case Reason::ConfiguredPathMissing:
            return instance.overrideLocation ? QObject::tr("The Java set for this instance no longer exists.")
                                             : QObject::tr("The default Java does not exist.");
        case Reason::NoCompatibleInstalledDownloadOff:
            return QObject::tr("No Java %1 is installed and automatic downloads are off.").arg(majorsText(instance.compatibleMajors));
        case Reason::ThirtyTwoBitMemory:
            return QObject::tr("This is a 32-bit Java: it cannot use that much memory.");
        case Reason::PinnedPathMissing:
            return QObject::tr("The Java %1 path set in the launcher settings does not exist.").arg(diagnosis.pinnedMissingMajor);
        case Reason::Ok:
        case Reason::WillDownload:
        case Reason::RequirementUnknown:
        case Reason::RuntimeUnknown:
            break;
    }
    return QObject::tr("Java needs attention for this version.");
}

// ---- texts ----

QString gbNumber(int mb)
{
    if (mb % kGb == 0) {
        return QString::number(mb / kGb);
    }
    return QString::number(double(mb) / kGb, 'f', 1);
}

QString gbText(int mb)
{
    return QObject::tr("%1 GB").arg(gbNumber(mb));
}

QString modsTitle(Loader loader, const QString& mcVersion)
{
    const QString loaderText = loaderName(loader);
    QStringList parts;
    if (!loaderText.isEmpty()) {
        parts << loaderText;
    }
    if (!mcVersion.isEmpty()) {
        parts << mcVersion;
    }
    return parts.isEmpty() ? QObject::tr("Mods") : QObject::tr("Mods · %1").arg(parts.join(QLatin1Char(' ')));
}

QString vanillaModsText()
{
    return QObject::tr("Performance mods need a mod loader such as Fabric. Make a Fabric copy from Library › Copy.");
}

QString modpackNote()
{
    return QObject::tr("This is a modpack: a pack update may remove mods you add.");
}

QString rowStateText(const Row& row, const QString& mcVersion)
{
    switch (row.state) {
        case RowState::Installed:
            return QObject::tr("Installed");
        case RowState::InstalledAlternative:
            return QObject::tr("Installed (%1)").arg(row.alternativeName);
        case RowState::Unavailable:
            return mcVersion.isEmpty() ? QObject::tr("Not available") : QObject::tr("Not available for %1").arg(mcVersion);
        case RowState::ConflictOptiFine:
            return QObject::tr("Conflicts with OptiFine");
        case RowState::Offered:
            break;
    }
    return {};
}

QString ramText(int ramMb)
{
    // The system reports a little less than the installed RAM (16 GB shows as 15.9): round to the nearest GB.
    const int gb = std::max(1, int(double(ramMb) / kGb + 0.5));
    return QObject::tr("Your PC has %1 GB of RAM.").arg(gb);
}

QString memoryGiveText(int recommendedMb)
{
    return QObject::tr("Give Minecraft %1").arg(gbText(recommendedMb));
}

QString memoryNowText(int currentMb)
{
    return QObject::tr("now %1").arg(gbText(currentMb));
}

QString memoryFineText(int currentMb)
{
    return QObject::tr("Memory is fine (%1)").arg(gbText(currentMb));
}

QString dialogTitle(const QString& instanceName)
{
    return QObject::tr("Boost performance · %1").arg(instanceName);
}

QString summaryText(const Summary& summary)
{
    QStringList parts;
    if (summary.mods > 0) {
        QString mods = summary.mods == 1 ? QObject::tr("Adds 1 mod") : QObject::tr("Adds %1 mods").arg(summary.mods);
        if (summary.dependencies > 0) {
            mods += QObject::tr(" (+%1 they need)").arg(summary.dependencies);
        }
        parts << mods;
    }
    if (summary.memory) {
        parts << QObject::tr("Memory %1 → %2").arg(gbNumber(summary.memoryFromMb), gbText(summary.memoryToMb));
    }
    if (summary.javaArgs) {
        parts << javaArgsTunedText();
    }
    return parts.isEmpty() ? QObject::tr("Nothing to change") : parts.join(QStringLiteral(" · "));
}

QString boostedToastText(const QString& instanceName)
{
    return QObject::tr("Boosted %1").arg(instanceName);
}

QString closeFirstText(const QString& instanceName)
{
    return QObject::tr("Close %1 first").arg(instanceName);
}

}  // namespace Zero::Perf
