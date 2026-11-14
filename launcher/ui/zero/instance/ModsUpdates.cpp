// SPDX-License-Identifier: GPL-3.0-only
#include "ModsUpdates.h"

#include <QMessageBox>
#include <QPointer>
#include <QTimer>

#include <memory>
#include <vector>

#include "Application.h"
#include "Json.h"
#include "ResourceDownloadTask.h"
#include "Version.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/mod/ModFolderModel.h"
#include "modplatform/ModIndex.h"
#include "modplatform/flame/FlameCheckUpdate.h"
#include "modplatform/modrinth/ModrinthCheckUpdate.h"
#include "tasks/ConcurrentTask.h"
#include "tasks/SequentialTask.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/ProgressDialog.h"
#include "zero/mods/InstalledModsModel.h"

namespace Zero {
namespace ModsUi {

namespace {

QString versionTypeName(const std::optional<ModPlatform::IndexedVersionType>& type)
{
    if (!type.has_value()) {
        return {};
    }
    switch (type->value()) {
        case ModPlatform::IndexedVersionTypeValue::Release:
            return QStringLiteral("Release");
        case ModPlatform::IndexedVersionTypeValue::Beta:
            return QStringLiteral("Beta");
        case ModPlatform::IndexedVersionTypeValue::Alpha:
            return QStringLiteral("Alpha");
        default:
            break;
    }
    return {};
}

/// What the Prism checkers hold by REFERENCE (CheckUpdateTask keeps `QList<Resource*>&` and `std::vector<Version>&`) and the shared
/// ownership of the checked resources. Heap-allocated and shared with every task through a connection that dies with the task: the lists
/// and the resources outlive the last task that points at them, even a task that was aborted and is only deleted later.
struct CheckStorage {
    QList<Resource::Ptr> keep;
    QList<Resource*> modrinth;
    QList<Resource*> flame;
    std::vector<Version> versions;
};

void bindLifetime(QObject* task, const std::shared_ptr<CheckStorage>& storage)
{
    QObject::connect(task, &QObject::destroyed, task, [storage]() { (void)storage; });
}

class PrismModUpdateCheck : public ModUpdateCheck {
   public:
    PrismModUpdateCheck(const QList<Resource::Ptr>& resources, MinecraftInstance* instance, ModFolderModel* model, QObject* parent)
        : ModUpdateCheck(parent), m_storage(std::make_shared<CheckStorage>()), m_model(model)
    {
        // The same inputs the classic page gives ResourceUpdateDialog (ModFolderPage::updateMods / ResourceUpdateDialog's constructor and
        // mcVersions()): the instance's Minecraft version, its mod loaders (+ the Quilt/Fabric and NeoForge/Forge extras of
        // getModLoadersList()), and the release types of the "ModUpdateReleaseTypes" setting (empty = every type).
        if (instance) {
            if (auto* profile = instance->getPackProfile()) {
                if (auto component = profile->getComponent("net.minecraft")) {
                    m_storage->versions = { Version(component->getVersion()) };
                    m_loaders = profile->getModLoadersList();
                    m_valid = true;
                }
            }
        }
        m_releaseTypes = ModPlatform::IndexedVersionType::fromStringList(
            Json::toStringList(APPLICATION->settings()->get("ModUpdateReleaseTypes").toString()));

        const bool flameAvailable = APPLICATION->capabilities().testFlag(Application::SupportsFlame);
        for (const Resource::Ptr& resource : resources) {
            // Modrinth's checker dereferences metadata() without looking: a resource without it never gets that far.
            if (!resource || !resource->metadata()) {
                continue;
            }
            m_storage->keep.append(resource);
            switch (resource->metadata()->provider) {
                case ModPlatform::ResourceProvider::MODRINTH:
                    m_storage->modrinth.append(resource.get());
                    break;
                case ModPlatform::ResourceProvider::FLAME:
                    if (flameAvailable) {
                        m_storage->flame.append(resource.get());
                    }
                    break;
            }
        }
    }

    ~PrismModUpdateCheck() override { detach(); }

    void start() override
    {
        if (!m_valid || (m_storage->modrinth.isEmpty() && m_storage->flame.isEmpty())) {
            QTimer::singleShot(0, this, [this]() { emit finished(false, tr("Nothing to check.")); });
            return;
        }
        m_task = makeShared<SequentialTask>(tr("Checking for updates"));
        if (!m_storage->modrinth.isEmpty()) {
            m_modrinth = makeShared<ModrinthCheckUpdate>(m_storage->modrinth, m_storage->versions, m_loaders, m_model.data(), m_releaseTypes);
            bindLifetime(m_modrinth.get(), m_storage);
            connect(m_modrinth.get(), &CheckUpdateTask::checkFailed, this, &PrismModUpdateCheck::onFailed);
            m_task->addTask(m_modrinth);
        }
        if (!m_storage->flame.isEmpty()) {
            m_flame = makeShared<FlameCheckUpdate>(m_storage->flame, m_storage->versions, m_loaders, m_model.data(), m_releaseTypes);
            bindLifetime(m_flame.get(), m_storage);
            connect(m_flame.get(), &CheckUpdateTask::checkFailed, this, &PrismModUpdateCheck::onFailed);
            m_task->addTask(m_flame);
        }
        bindLifetime(m_task.get(), m_storage);
        connect(m_task.get(), &Task::succeeded, this, [this]() {
            collect();
            emit finished(true, QString());
        });
        connect(m_task.get(), &Task::failed, this, [this](const QString& reason) { emit finished(false, reason); });
        m_task->start();
    }

    void abort() override
    {
        detach();
        if (m_task && m_task->isRunning()) {
            m_task->abort();
        }
    }

    QList<ModUpdate> updates() const override { return m_updates; }

   private:
    void detach()
    {
        if (m_task) {
            disconnect(m_task.get(), nullptr, this, nullptr);
        }
        if (m_modrinth) {
            disconnect(m_modrinth.get(), nullptr, this, nullptr);
        }
        if (m_flame) {
            disconnect(m_flame.get(), nullptr, this, nullptr);
        }
    }

    void onFailed(Resource* resource, const QString& reason, const QUrl&)
    {
        if (resource) {  // alive: m_storage->keep owns it
            emit modFailed(Mods::Installed::updateKeyOf(resource->internalId()), reason);
        }
    }

    /// An `Update` carries no resource, only the facts of the old file: the project of its pack, its old hash and version. Match them to
    /// the mod they were found for.
    void take(std::vector<CheckUpdateTask::Update>&& found, Mods::Installed::Source source, QList<Mods::Installed::UpdateCandidate>& candidates,
              QList<bool>& taken)
    {
        for (auto& update : found) {
            Mods::Installed::UpdateFacts facts;
            facts.source = source;
            facts.projectId = update.download ? update.download->getPack()->addonId.toString() : QString();
            facts.oldHash = update.oldHash;
            facts.oldVersion = update.oldVersion;
            const int at = Mods::Installed::matchUpdate(candidates, taken, facts);
            if (at < 0) {
                continue;
            }
            taken[at] = true;
            ModUpdate out;
            out.key = Mods::Installed::updateKeyOf(m_storage->keep.at(at)->internalId());
            out.name = update.name;
            out.oldHash = update.oldHash;
            out.newVersion = update.newVersion;
            out.versionType = versionTypeName(update.newVersionType);
            out.download = update.download;
            m_updates.append(out);
        }
    }

    void collect()
    {
        QList<Mods::Installed::UpdateCandidate> candidates;
        for (const Resource::Ptr& resource : std::as_const(m_storage->keep)) {
            Mods::Installed::UpdateCandidate candidate;
            const auto& meta = resource->metadata();
            candidate.source = meta->provider == ModPlatform::ResourceProvider::FLAME ? Mods::Installed::Source::CurseForge
                                                                                      : Mods::Installed::Source::Modrinth;
            candidate.projectId = meta->projectId.toString();
            candidate.hash = meta->hash;
            candidate.version = meta->versionNumber;
            candidates.append(candidate);
        }
        QList<bool> taken(candidates.size(), false);
        if (m_modrinth) {
            take(std::move(m_modrinth->getUpdates()), Mods::Installed::Source::Modrinth, candidates, taken);
        }
        if (m_flame) {
            take(std::move(m_flame->getUpdates()), Mods::Installed::Source::CurseForge, candidates, taken);
        }
    }

    std::shared_ptr<CheckStorage> m_storage;  // declared first: the tasks below only hold references into it
    QPointer<ModFolderModel> m_model;
    QList<ModPlatform::ModLoaderType> m_loaders;
    std::vector<ModPlatform::IndexedVersionType> m_releaseTypes;
    bool m_valid = false;
    shared_qobject_ptr<ModrinthCheckUpdate> m_modrinth;
    shared_qobject_ptr<FlameCheckUpdate> m_flame;
    shared_qobject_ptr<SequentialTask> m_task;
    QList<ModUpdate> m_updates;
};

}  // namespace

UpdateCheckFactory prismUpdateCheckFactory(MinecraftInstance* instance, ModFolderModel* model)
{
    QPointer<MinecraftInstance> guardedInstance(instance);
    QPointer<ModFolderModel> guardedModel(model);
    return [guardedInstance, guardedModel](const QList<Resource::Ptr>& resources, QObject* parent) -> ModUpdateCheck* {
        return new PrismModUpdateCheck(resources, guardedInstance.data(), guardedModel.data(), parent);
    };
}

bool applyUpdatesWithPrism(QWidget* parent, const QList<ModUpdate>& updates)
{
    // MainWindow::installMod / ModFolderPage::updateMods: one ConcurrentTask under a ProgressDialog; Prism's own (non-blocking) error and
    // warning boxes. ResourceDownloadTask replaces the old file only once the new one arrived and was checked.
    ConcurrentTask tasks(QObject::tr("Update mods"), APPLICATION->settings()->get("NumberOfConcurrentDownloads").toInt());
    QObject::connect(&tasks, &Task::failed, parent, [parent](const QString& reason) {
        CustomMessageBox::selectable(parent, QObject::tr("Error"), reason, QMessageBox::Critical)->show();
    });
    QObject::connect(&tasks, &Task::succeeded, parent, [parent, &tasks]() {
        const QStringList warnings = tasks.warnings();
        if (warnings.count()) {
            CustomMessageBox::selectable(parent, QObject::tr("Warnings"), warnings.join('\n'), QMessageBox::Warning)->show();
        }
    });
    for (const ModUpdate& update : updates) {
        if (update.download) {
            tasks.addTask(update.download);
        }
    }
    ProgressDialog dialog(parent);
    dialog.showSkipButton();
    dialog.execWithTask(&tasks);
    return tasks.wasSuccessful();
}

}  // namespace ModsUi
}  // namespace Zero
