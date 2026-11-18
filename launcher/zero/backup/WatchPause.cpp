// SPDX-License-Identifier: GPL-3.0-only
#include "WatchPause.h"

#include <QPointer>
#include <memory>
#include <vector>
#include "minecraft/MinecraftInstance.h"
#include "minecraft/WorldList.h"
#include "minecraft/mod/ResourceFolderModel.h"

namespace Zero {
namespace Backup {

namespace {
struct Paused {
    std::vector<QPointer<ResourceFolderModel>> models;
    QPointer<WorldList> worlds;
    bool resumed = false;
};
}  // namespace

ResumeWatching pauseWatching(const QList<ResourceFolderModel*>& models, WorldList* worlds)
{
    auto state = std::make_shared<Paused>();
    for (ResourceFolderModel* model : models) {
        if (!model)
            continue;
        // stopWatching() is true only when the model was watching and does not any more
        if (model->stopWatching())
            state->models.emplace_back(model);
    }
    if (worlds && worlds->isWatching()) {
        worlds->stopWatching();
        state->worlds = worlds;
    }
    return [state]() {
        if (state->resumed)
            return;
        state->resumed = true;
        for (const QPointer<ResourceFolderModel>& model : state->models) {
            if (model)
                model->startWatching();  // reads the folder again too: the roll back changed it
        }
        if (state->worlds)
            state->worlds->startWatching();
    };
}

ResumeWatching pauseInstanceWatching(MinecraftInstance* instance)
{
    if (!instance)
        return {};
    // resourceLists() makes the models that do not exist yet (they do not watch until started); dataPackList() is null while data packs are off
    return pauseWatching(instance->resourceLists(), instance->worldList());
}

}  // namespace Backup
}  // namespace Zero
