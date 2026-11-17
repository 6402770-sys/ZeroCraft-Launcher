// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the network side of the Mods page's update check (docs/zero/CONTENT-MODS.md, "C5b Updates"). The page talks to two
// small seams, so a test can put fakes in their place:
//   * ModUpdateCheck: one running check (start / abort, a signal per mod that could not be checked, one `finished`); the default is
//     PrismModUpdateCheck, which runs Prism's ModrinthCheckUpdate and FlameCheckUpdate headless (no ResourceUpdateDialog, no message box);
//   * UpdateApplier: starts the downloads of the chosen updates and returns at once; the default is applyUpdatesInBackground (a
//     ConcurrentTask run as a job of the Downloads center), which calls `done(ok)` when the job ended.
#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <functional>

#include "QObjectPtr.h"
#include "minecraft/mod/Resource.h"
#include "tasks/Task.h"

class MinecraftInstance;
class ModFolderModel;
class QWidget;

namespace Zero {
namespace ModsUi {

/// One update a check found.
struct ModUpdate {
    QString key;           ///< Mods::Installed::Row::updateKey of the mod it is for
    QString name;
    QString oldHash;
    QString newVersion;
    QString versionType;   ///< "Release" | "Beta" | "Alpha" | ""
    Task::Ptr download;    ///< Prism's ResourceDownloadTask (it replaces the old file only after the new one arrived); null in tests
};

class ModUpdateCheck : public QObject {
    Q_OBJECT
   public:
    explicit ModUpdateCheck(QObject* parent = nullptr) : QObject(parent) {}

    /// Begins asynchronously: `finished` is never emitted from inside start().
    virtual void start() = 0;
    /// Stops the work. The page disconnects first, so nothing is emitted for an aborted check.
    virtual void abort() = 0;
    /// The updates found; valid after finished(true, ...).
    virtual QList<ModUpdate> updates() const = 0;

   signals:
    /// This mod could not be checked (`key` = Row::updateKey).
    void modFailed(const QString& key, const QString& reason);
    /// The whole check ended. ok = false: it failed as a whole (`error` says why).
    void finished(bool ok, const QString& error);
};

/// Creates a check for these mods. Every resource has metadata (the checkers dereference it). The check keeps the resources alive
/// (shared ownership) for as long as any of its tasks exists.
using UpdateCheckFactory = std::function<ModUpdateCheck*(const QList<Resource::Ptr>& resources, QObject* parent)>;
/// Starts the downloads of `updates` and returns at once. `done(ok)` is called exactly once, later on the GUI thread (also after the
/// window of `parent` is gone, so a caller must guard what it touches): ok = every download succeeded (false after a failure or a cancel).
/// It may also be called before the starter returned (nothing could start).
using UpdateApplier = std::function<void(QWidget* parent, const QList<ModUpdate>& updates, std::function<void(bool ok)> done)>;

/// Prism's checkers for this instance: the Minecraft version, the mod loaders and the release types are what the classic page passes.
UpdateCheckFactory prismUpdateCheckFactory(MinecraftInstance* instance, ModFolderModel* model);
/// The real UpdateApplier for the instance `instanceId`: one ConcurrentTask of `updates[i].download`, a Downloads-center job of kind Update
/// ("Update 3 mods", or the mod's name for one), the instance's mod list read again when it ended. Toasts come from startFlow.
void applyUpdatesInBackground(const QString& instanceId, QWidget* parent, const QList<ModUpdate>& updates, std::function<void(bool ok)> done);

}  // namespace ModsUi
}  // namespace Zero
