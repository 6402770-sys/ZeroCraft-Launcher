// SPDX-License-Identifier: GPL-3.0-only
#include "InstalledMods.h"

#include <QDir>
#include <QFileInfo>

#include "Application.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/mod/Mod.h"
#include "minecraft/mod/ModFolderModel.h"
#include "minecraft/mod/ResourcePackFolderModel.h"
#include "minecraft/mod/ShaderPackFolderModel.h"
#include "modplatform/packwiz/Packwiz.h"
#include "modplatform/ModIndex.h"
#include "zero/content/ModpackSource.h"

namespace Zero::Content {

InstanceModsSource::InstanceModsSource(QObject* parent, InstalledFolder folder) : InstalledModsSource(parent), m_folder(folder)
{
    // Rows come and go in bursts while the folder loads: one signal for a burst.
    m_changedTimer.setSingleShot(true);
    m_changedTimer.setInterval(60);
    connect(&m_changedTimer, &QTimer::timeout, this, &InstalledModsSource::changed);
}

InstanceModsSource::~InstanceModsSource()
{
    detach();
}

void InstanceModsSource::setInstance(const QString& instanceId)
{
    if (instanceId == m_instanceId) {
        return;
    }
    detach();
    m_instanceId = instanceId;
    if (m_active) {
        attach();
    }
}

void InstanceModsSource::setActive(bool active)
{
    if (active == m_active) {
        return;
    }
    m_active = active;
    if (active) {
        attach();
    } else {
        detach();
    }
}

void InstanceModsSource::attach()
{
    if (m_model || m_instanceId.isEmpty() || !APPLICATION || !APPLICATION->instances()) {
        return;
    }
    auto* instance = APPLICATION->instances()->getInstanceById(m_instanceId);
    if (!instance) {
        return;
    }
    switch (m_folder) {
        case InstalledFolder::Mods:
            m_model = instance->loaderModList();
            break;
        case InstalledFolder::ResourcePacks:
            m_model = instance->resourcePackList();
            break;
        case InstalledFolder::ShaderPacks:
            m_model = instance->shaderPackList();
            break;
    }
    if (!m_model) {
        return;
    }
    auto schedule = [this]() { m_changedTimer.start(); };
    connect(m_model, &ResourceFolderModel::updateFinished, this, schedule);
    connect(m_model, &QAbstractItemModel::rowsInserted, this, schedule);
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, schedule);
    connect(m_model, &QAbstractItemModel::modelReset, this, schedule);
    // startWatching() loads the folder (update()) and is true only when it was not watched yet; when someone else already watches
    // it, only a fresh update() is asked for and the watching stays theirs.
    m_startedWatching = m_model->startWatching();
    if (!m_startedWatching) {
        m_model->update();
    }
}

void InstanceModsSource::detach()
{
    m_changedTimer.stop();
    if (m_model) {
        m_model->disconnect(this);
        if (m_startedWatching) {
            m_model->stopWatching();
        }
    }
    m_startedWatching = false;
    m_model = nullptr;
}

namespace {
InstalledMod entryOf(const Packwiz::V1::Mod& metadata)
{
    const QString type = managedPackType(metadata.provider == ModPlatform::ResourceProvider::FLAME ? Provider::CurseForge : Provider::Modrinth);
    return { managedKey(type, metadata.projectId.toString()), metadata.fileId.toString(), metadata.name, metadata.versionNumber };
}
}  // namespace

QList<InstalledMod> installedFromIndex(const QDir& indexDir, const QDir& filesDir)
{
    QList<InstalledMod> out;
    static const QString suffix = QStringLiteral(".pw.toml");
    for (const QString& entry : indexDir.entryList({ QStringLiteral("*") + suffix }, QDir::Files, QDir::Name)) {
        const auto metadata = Packwiz::V1::getIndexForMod(indexDir, entry.chopped(suffix.size()));
        if (!metadata.isValid()) {
            continue;
        }
        // An entry whose file is gone (deleted outside the launcher) is not installed.
        if (!metadata.filename.isEmpty() && !QFileInfo::exists(filesDir.absoluteFilePath(metadata.filename)) &&
            !QFileInfo::exists(filesDir.absoluteFilePath(metadata.filename + QStringLiteral(".disabled")))) {
            continue;
        }
        out.append(entryOf(metadata));
    }
    return out;
}

QList<InstalledMod> InstanceModsSource::mods() const
{
    QList<InstalledMod> out;
    if (!m_model) {
        return out;
    }
    if (m_folder != InstalledFolder::Mods) {
        return installedFromIndex(m_model->indexDir(), m_model->dir());
    }
    for (Resource* resource : m_model->allResources()) {
        const auto metadata = resource->metadata();
        if (!metadata || !metadata->isValid()) {
            continue;
        }
        out.append(entryOf(*metadata));
    }
    return out;
}

}  // namespace Zero::Content
