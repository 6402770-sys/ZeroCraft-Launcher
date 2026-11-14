// SPDX-License-Identifier: GPL-3.0-only
#include "ModpackInstall.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QRegularExpression>

#include "InstanceImportTask.h"
#include "icons/IconUtils.h"
#include "modplatform/technic/SingleZipPackInstallTask.h"
#include "modplatform/technic/SolderPackInstallTask.h"
#include "zero/content/CreateInstanceModel.h"

namespace Zero::Content {

namespace {
bool isTechnic(Provider provider)
{
    return provider == Provider::Technic;
}

/// The Minecraft version the Technic task wants: the pack details name it.
QString technicMinecraft(const ModpackItem& item)
{
    return item.technic ? item.technic->minecraftVersion : QString();
}
}  // namespace

bool isInstallable(const ModpackVersion& version, Provider provider)
{
    if (isTechnic(provider)) {
        return true;  // the task resolves what to download (a zip pack's version name may even be empty)
    }
    return !version.downloadUrl.trimmed().isEmpty() && !version.id.isEmpty();
}

int defaultVersionIndex(const QList<ModpackVersion>& versions, Provider provider)
{
    int newestRelease = -1;
    int first = -1;
    for (int i = 0; i < versions.size(); ++i) {
        const ModpackVersion& v = versions.at(i);
        if (!isInstallable(v, provider)) {
            continue;
        }
        if (v.recommended) {
            return i;  // Technic Solder: what Prism preselects
        }
        if (first < 0) {
            first = i;
        }
        if (v.type == ModpackVersion::Type::Release) {
            // Providers list newest first: an earlier release wins a tie, a later one only when it is really newer.
            if (newestRelease < 0 || (v.date.isValid() && versions.at(newestRelease).date.isValid() && v.date > versions.at(newestRelease).date)) {
                newestRelease = i;
            }
        }
    }
    return newestRelease >= 0 ? newestRelease : first;
}

QList<ModpackVersion> knownVersions(const ModpackItem& item, bool* loaded)
{
    if (loaded) {
        *loaded = false;
    }
    QList<ModpackVersion> out;
    if (item.pack && item.pack->versionsLoaded) {
        if (loaded) {
            *loaded = true;
        }
        for (const auto& version : item.pack->versions) {
            out.append(versionFromIndexed(version));
        }
    }
    return out;
}

std::optional<InstallPlan> planInstall(const InstallRequest& request)
{
    if (!request.item) {
        return std::nullopt;
    }
    const ModpackItem& item = *request.item;
    InstallPlan plan;
    const bool update = !request.updateInstanceId.isEmpty();

    if (isTechnic(item.provider)) {
        // TechnicPage::selectVersion(): a zip URL wins over Solder; Solder gets the build the user picked.
        if (!item.technic || !item.technic->isInstallable()) {
            return std::nullopt;
        }
        const TechnicPackDetails& details = *item.technic;
        plan.url = QUrl(details.installUrl());
        plan.technicSlug = item.slug.isEmpty() ? item.id : item.slug;
        plan.technicBuild = request.version.name.isEmpty() ? request.version.id : request.version.name;
        plan.minecraftVersion = technicMinecraft(item);
        plan.kind = details.isSolder() ? InstallPlan::Kind::TechnicSolder : InstallPlan::Kind::TechnicZip;
        plan.originalVersion = plan.technicBuild;
    } else {
        // ModrinthPage::suggestCurrent() / FlamePage::suggestCurrent(): the version's download URL and the two ids.
        if (request.version.downloadUrl.trimmed().isEmpty() || request.version.id.isEmpty()) {
            return std::nullopt;
        }
        plan.kind = InstallPlan::Kind::Import;
        plan.url = QUrl(request.version.downloadUrl);
        plan.extraInfo.insert(QStringLiteral("pack_id"), item.pack ? item.pack->addonId.toString() : item.id);
        plan.extraInfo.insert(QStringLiteral("pack_version_id"), request.version.id);
        if (update) {
            plan.extraInfo.insert(QStringLiteral("original_instance_id"), request.updateInstanceId);  // ManagedPackPage::updatePack()
        }
        // ModrinthPage passes the version; FlamePage passes none (the manifest names it). An update gives both (ManagedPackPage).
        if (item.provider == Provider::Modrinth || update) {
            plan.originalVersion = request.version.name;
        }
    }

    plan.originalName = item.name;
    plan.name = request.name.trimmed();
    if (update && !request.updateVersionName.isEmpty() && !request.version.name.isEmpty()) {
        plan.name.replace(request.updateVersionName, request.version.name);
    }
    plan.group = request.group;
    plan.targetDir = request.targetDir;
    plan.iconKey = request.iconKey.isEmpty() ? QStringLiteral("default") : request.iconKey;
    plan.copyTemplate = request.copyTemplate;
    return plan;
}

InstanceTask* createInstallTask(const InstallPlan& plan, QWidget* parent, QNetworkAccessManager* network)
{
    InstanceTask* task = nullptr;
    switch (plan.kind) {
        case InstallPlan::Kind::Import:
            task = new InstanceImportTask(plan.url, true, parent, plan.extraInfo);
            break;
        case InstallPlan::Kind::TechnicSolder:
            task = new Technic::SolderPackInstallTask(network, plan.url, plan.technicSlug, plan.technicBuild, plan.minecraftVersion);
            break;
        case InstallPlan::Kind::TechnicZip:
            task = new Technic::SingleZipPackInstallTask(plan.url, plan.minecraftVersion);
            break;
    }
    // Exactly what NewInstanceDialog::extractTask() sets.
    task->setName(plan.name);
    task->setOriginalName(plan.originalName.trimmed(), plan.originalVersion);
    task->setGroup(plan.group);
    task->setIcon(plan.iconKey);
    task->setTargetDir(plan.targetDir);
    task->setCopyTemplateDir(plan.copyTemplate);
    return task;
}

// ---- logo ----

QString logoPrefix(Provider provider)
{
    switch (provider) {
        case Provider::Modrinth:
            return QStringLiteral("modrinth_");
        case Provider::CurseForge:
            return QStringLiteral("curseforge_");
        case Provider::Technic:
            return QStringLiteral("technic_");
    }
    return {};
}

QString logoFileName(const ModpackItem& item)
{
    const QUrl url(item.iconUrl);
    if (item.iconUrl.trimmed().isEmpty() || !url.isValid() || url.host().isEmpty()) {
        return {};
    }
    QString suffix = QFileInfo(url.fileName()).suffix().toLower();
    QString base = item.pack ? item.pack->logoName : QString();
    if (!base.isEmpty() && IconUtils::isIconSuffix(QFileInfo(base).suffix().toLower())) {
        suffix = QFileInfo(base).suffix().toLower();
        base = QFileInfo(base).completeBaseName();
    }
    if (base.isEmpty()) {
        base = item.slug.isEmpty() ? item.id : item.slug;
    }
    if (!IconUtils::isIconSuffix(suffix)) {
        suffix = QStringLiteral("png");
    }
    static const QRegularExpression unsafe(QStringLiteral("[^A-Za-z0-9._-]"));
    base.replace(unsafe, QStringLiteral("_"));
    if (base.isEmpty()) {
        return {};
    }
    return base + QLatin1Char('.') + suffix;
}

QString logoIconName(const ModpackItem& item)
{
    const QString file = logoFileName(item);
    return file.isEmpty() ? QString() : logoPrefix(item.provider) + file;
}

QString logoIconKey(const ModpackItem& item)
{
    const QString name = logoIconName(item);
    return name.isEmpty() ? QStringLiteral("default") : name.left(name.lastIndexOf(QLatin1Char('.')));  // IconList's key rule
}

// ---- presentation ----

QString suggestedInstanceName(const ModpackItem& item)
{
    QString out = item.name.trimmed();
    if (out.size() > Zero::Create::kMaxNameLength) {
        int length = Zero::Create::kMaxNameLength;
        if (out.at(length - 1).isHighSurrogate()) {
            --length;
        }
        out.truncate(length);
        out = out.trimmed();
    }
    return out;
}

QString versionTypeText(ModpackVersion::Type type)
{
    switch (type) {
        case ModpackVersion::Type::Release:
            return QCoreApplication::translate("Zero::Content::ModpackInstall", "Release");
        case ModpackVersion::Type::Beta:
            return QCoreApplication::translate("Zero::Content::ModpackInstall", "Beta");
        case ModpackVersion::Type::Alpha:
            return QCoreApplication::translate("Zero::Content::ModpackInstall", "Alpha");
        case ModpackVersion::Type::Unknown:
            break;
    }
    return {};
}

QString minecraftText(const ModpackVersion& version)
{
    const QString range = minecraftRangeText(version.gameVersions);
    if (!range.isEmpty()) {
        return range;
    }
    return version.gameVersions.join(QStringLiteral(", "));
}

QString loaderText(const ModpackVersion& version)
{
    return loaderLabels(version.loaders).join(QStringLiteral(", "));
}

}  // namespace Zero::Content
