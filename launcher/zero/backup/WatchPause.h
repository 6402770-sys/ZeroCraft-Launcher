// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - lets go of the folders the launcher itself watches while a roll back swaps the instance folder.
// Windows refuses to rename a folder while something below it is watched (Prism's ResourceFolderModel watches mods/ AND mods/.index, the
// world list watches saves/), and those models are alive whenever the instance window's pages, Mod Health, Before-you-play or the update
// sweep have loaded them. The roll back renames the whole instance folder, so the instance's own watchers are stopped first and started
// again afterwards. Everything here runs on the GUI thread (the watchers are QObjects); the work itself runs on a pool thread in between.
#pragma once

#include <QList>
#include <functional>

class MinecraftInstance;
class ResourceFolderModel;
class WorldList;

namespace Zero {
namespace Backup {

/// Starts watching again whatever `pauseWatching` stopped. Safe to call more than once (the second call does nothing).
using ResumeWatching = std::function<void()>;

/// Called on the GUI thread right before the roll back starts; returns what to call when it ended (success or failure).
using PauseWatching = std::function<ResumeWatching()>;

/// Stops watching on every model that is watching now (a model that is not is left alone, and is not started again later). The models are
/// guarded: one that is gone when the resume runs is skipped.
ResumeWatching pauseWatching(const QList<ResourceFolderModel*>& models, WorldList* worlds = nullptr);

/// The instance's own mod, resource pack, shader pack, data pack and world lists (see pauseWatching). Empty when the instance is null.
ResumeWatching pauseInstanceWatching(MinecraftInstance* instance);

}  // namespace Backup
}  // namespace Zero
