// SPDX-License-Identifier: GPL-3.0-only
#include "UpdateSweepSetup.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>

#include <memory>

#include "Application.h"
#include "FileSystem.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/mod/Mod.h"
#include "minecraft/mod/ModFolderModel.h"
#include "ui/zero/instance/ModsUpdates.h"
#include "zero/downloads/DownloadCenter.h"
#include "zero/mods/InstalledModsModel.h"
#include "zero/mods/ModSnapshot.h"
#include "zero/updates/UpdateSweep.h"

namespace Zero {
namespace Updates {

namespace {

using ModsUi::ModUpdate;
using ModsUi::ModUpdateCheck;

/// The check of one instance as the engine wants it: loads the instance's mod folder (the Mods page does the same through its model),
/// picks the mods the page would check (metadata, a usable project id, not locked, CurseForge only with a key), and runs Prism's checkers
/// over them. An instance without such a mod finishes at once: ok, no updates.
class InstanceModCheck : public ModUpdateCheck {
   public:
    InstanceModCheck(MinecraftInstance* instance, QObject* parent)
        : ModUpdateCheck(parent), m_instance(instance), m_model(instance ? instance->loaderModList() : nullptr)
    {}
    ~InstanceModCheck() override { release(); }

    void start() override
    {
        if (!m_model || !m_instance) {
            QTimer::singleShot(0, this, [this]() { emit finished(false, tr("The instance is gone.")); });
            return;
        }
        // The first updateFinished is the folder read we asked for (or one that was already running: it is just as good).
        connect(m_model.data(), &ResourceFolderModel::updateFinished, this, &InstanceModCheck::onLoaded, Qt::SingleShotConnection);
        m_owned = m_model->startWatching();  // loads the folder; false = somebody (the Mods page) watches it already
        if (!m_owned) {
            m_model->update();
        }
    }

    void abort() override
    {
        m_aborted = true;
        if (m_model) {
            disconnect(m_model.data(), nullptr, this, nullptr);
        }
        if (m_inner) {
            disconnect(m_inner, nullptr, this, nullptr);
            m_inner->abort();
            m_inner->deleteLater();
            m_inner = nullptr;
        }
        release();
    }

    QList<ModUpdate> updates() const override { return m_updates; }

   private:
    void release()
    {
        if (m_owned && m_model) {
            m_model->stopWatching();  // the files stay loaded in the model; only the folder watch ends
        }
        m_owned = false;
    }

    void onLoaded()
    {
        if (m_aborted) {
            return;
        }
        if (!m_model || !m_instance) {
            release();
            emit finished(false, tr("The instance is gone."));
            return;
        }
        const bool curseForge = APPLICATION->capabilities().testFlag(Application::SupportsFlame);
        QList<Resource::Ptr> resources;
        for (Mod* mod : m_model->allMods()) {
            if (!mod || !Mods::Installed::canCheck(Mods::Installed::rowFromMod(*mod), curseForge)) {
                continue;
            }
            const Resource::Ptr resource = m_model->find(mod->internalId());
            if (resource && resource->metadata()) {  // the checkers dereference metadata() without looking
                resources.append(resource);
            }
        }
        if (resources.isEmpty()) {
            release();
            emit finished(true, QString());
            return;
        }
        m_inner = ModsUi::prismUpdateCheckFactory(m_instance.data(), m_model.data())(resources, this);
        if (!m_inner) {
            release();
            emit finished(false, tr("Nothing to check."));
            return;
        }
        connect(m_inner, &ModUpdateCheck::modFailed, this, &ModUpdateCheck::modFailed);
        connect(m_inner, &ModUpdateCheck::finished, this, [this](bool ok, const QString& error) {
            if (ok && m_inner) {
                m_updates = m_inner->updates();
            }
            if (m_inner) {
                disconnect(m_inner, nullptr, this, nullptr);
                m_inner->deleteLater();
                m_inner = nullptr;
            }
            release();
            emit finished(ok, error);
        });
        m_inner->start();
    }

    QPointer<MinecraftInstance> m_instance;
    QPointer<ModFolderModel> m_model;
    ModUpdateCheck* m_inner = nullptr;
    QList<ModUpdate> m_updates;
    bool m_owned = false;
    bool m_aborted = false;
};

/// The files an update replaces, for the snapshot (the same plan as ModsPage::snapshotPlan).
QList<Mods::Snapshot::ModFiles> snapshotPlan(ModFolderModel* model, const QList<ModUpdate>& updates, QString* error)
{
    QList<Mods::Snapshot::ModFiles> plan;
    const QList<Mod*> mods = model ? model->allMods() : QList<Mod*>();
    for (const ModUpdate& update : updates) {
        Mod* found = nullptr;
        for (Mod* mod : mods) {
            if (Mods::Installed::updateKeyOf(mod->internalId()) == update.key) {
                found = mod;
                break;
            }
        }
        const QString name = update.name.isEmpty() ? update.key : update.name;
        if (!found) {
            *error = QObject::tr("%1 is no longer in the mods folder.").arg(name);
            return {};
        }
        Mods::Snapshot::ModFiles files;
        files.name = name;
        files.oldVersion = found->version();
        if (files.oldVersion.isEmpty() && found->metadata()) {
            files.oldVersion = found->metadata()->versionNumber;
        }
        files.newVersion = update.newVersion;
        files.files.append(found->fileinfo().absoluteFilePath());
        if (const auto metadata = found->metadata()) {
            // The packwiz entry is `<slug>.pw.toml` in `.index` (Packwiz matches the name case-insensitively).
            const QString entry = metadata->slug.endsWith(QLatin1String(".pw.toml")) ? metadata->slug : metadata->slug + QStringLiteral(".pw.toml");
            const QDir index = model->indexDir();
            for (const QString& candidate : index.entryList(QDir::Files)) {
                if (candidate.compare(entry, Qt::CaseInsensitive) == 0) {
                    files.files.append(index.filePath(candidate));
                    break;
                }
            }
        }
        plan.append(files);
    }
    return plan;
}

bool launchedOrRunning(MinecraftInstance* inst)
{
    return !inst || inst->isRunning() || APPLICATION->launchPhase(inst->id()) != Application::LaunchPhase::Idle;
}

/// Snapshot (pool thread) -> the downloads as a Downloads-center job -> the snapshot is finalized when the job ended (pool thread).
void applyWithSnapshot(const QString& instanceId, const QList<ModUpdate>& updates, std::function<void()> started,
                       std::function<void(bool, const QString&)> done)
{
    auto* inst = APPLICATION->instances()->getInstanceById(instanceId);
    if (!inst) {
        started();
        done(false, QString());
        return;
    }
    ModFolderModel* model = inst->loaderModList();
    QString planError;
    const QList<Mods::Snapshot::ModFiles> plan = snapshotPlan(model, updates, &planError);
    if (!planError.isEmpty()) {
        started();
        done(false, QObject::tr("%1 was not updated: %2").arg(inst->name(), planError));
        return;
    }
    const QString snapshots = Mods::Snapshot::snapshotsDirFor(APPLICATION->dataRoot(), instanceId);
    const QString modsDir = model->dir().absolutePath();
    const QString name = inst->name();
    // The tasks belong to the GUI thread: only this shared list carries them, and its last owner is the GUI-thread functor below.
    auto kept = std::make_shared<QList<ModUpdate>>(updates);

    QThreadPool::globalInstance()->start([instanceId, snapshots, modsDir, plan, name, kept, started, done]() mutable {
        Mods::Snapshot::Hooks hooks;
        hooks.trashPath = [](const QString& path) { return FS::trash(path); };
        const Mods::Snapshot::CreateResult created = Mods::Snapshot::createSnapshot(snapshots, instanceId, modsDir, plan, hooks);
        QMetaObject::invokeMethod(
            qApp,
            // `kept` is MOVED into the GUI-thread functor: the pool thread must never hold the last reference to the tasks.
            [instanceId, modsDir, name, created, kept = std::move(kept), started, done]() {
                if (!created.ok) {
                    started();
                    done(false, QObject::tr("Could not save a copy of the mods of %1 first, so nothing was updated: %2").arg(name, created.error));
                    return;
                }
                auto finalize = [modsDir, created](bool ok, std::function<void(bool)> then) {
                    QThreadPool::globalInstance()->start([modsDir, created, ok, then]() {
                        Mods::Snapshot::finalizeSnapshot(created.dir, modsDir, created.before, ok);
                        QMetaObject::invokeMethod(qApp, [then, ok]() { then(ok); }, Qt::QueuedConnection);
                    });
                };
                auto* current = APPLICATION->instances()->getInstanceById(instanceId);
                if (launchedOrRunning(current)) {
                    // launched while the copy was made: nothing may change in the folder now; the copy protects nothing and is removed
                    finalize(false, [started, done, name](bool) {
                        started();
                        done(false, QObject::tr("%1 is running, so it was not updated.").arg(name));
                    });
                    return;
                }
                ModsUi::applyUpdatesInBackground(instanceId, QApplication::activeWindow(), *kept, [finalize, done](bool ok) {
                    finalize(ok, [done](bool finished) { done(finished, QString()); });
                });
                started();
            },
            Qt::QueuedConnection);
    });
}

}  // namespace

UpdateSweep* createApplicationSweep(QObject* parent)
{
    UpdateSweep::Hooks hooks;
    hooks.instances = []() {
        QList<InstanceSource> out;
        auto* list = APPLICATION->instances();
        auto* center = APPLICATION->zeroDownloads();
        for (int i = 0; i < list->count(); ++i) {
            MinecraftInstance* inst = list->at(i);
            InstanceSource source;
            source.id = inst->id();
            source.name = inst->name();
            source.running = launchedOrRunning(inst);
            source.busy = center && center->isBusy(source.id);
            source.managed = inst->isManagedPack();
            out.append(source);
        }
        return out;
    };
    hooks.checkFactory = [](const QString& instanceId, QObject* owner) -> ModUpdateCheck* {
        auto* inst = APPLICATION->instances()->getInstanceById(instanceId);
        if (!inst || APPLICATION->settings()->get("ModMetadataDisabled").toBool()) {
            return nullptr;
        }
        return new InstanceModCheck(inst, owner);
    };
    hooks.apply = applyWithSnapshot;
    hooks.autoEnabled = []() { return APPLICATION->settings()->get("ZeroAutoCheckUpdates").toBool(); };
    return new UpdateSweep(FS::PathCombine(APPLICATION->dataRoot(), "zero", "updates.json"), std::move(hooks), parent);
}

}  // namespace Updates
}  // namespace Zero
