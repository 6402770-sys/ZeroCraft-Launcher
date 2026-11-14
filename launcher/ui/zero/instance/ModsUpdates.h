// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the network side of the Mods page's update check (docs/zero/CONTENT-MODS.md, "C5b Updates"). The page talks to two
// small seams, so a test can put fakes in their place:
//   * ModUpdateCheck: one running check (start / abort, a signal per mod that could not be checked, one `finished`); the default is
//     PrismModUpdateCheck, which runs Prism's ModrinthCheckUpdate and FlameCheckUpdate headless (no ResourceUpdateDialog, no message box);
//   * UpdateApplier: runs the downloads of the chosen updates; the default is applyUpdatesWithPrism (the ConcurrentTask + ProgressDialog
//     pattern of MainWindow::installMod).
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
/// Runs the downloads of `updates` (blocking, with a progress dialog parented to `parent`); true when all of them succeeded.
using UpdateApplier = std::function<bool(QWidget* parent, const QList<ModUpdate>& updates)>;

/// Prism's checkers for this instance: the Minecraft version, the mod loaders and the release types are what the classic page passes.
UpdateCheckFactory prismUpdateCheckFactory(MinecraftInstance* instance, ModFolderModel* model);
bool applyUpdatesWithPrism(QWidget* parent, const QList<ModUpdate>& updates);

}  // namespace ModsUi
}  // namespace Zero
