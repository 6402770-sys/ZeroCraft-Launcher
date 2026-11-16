// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the application side of the pack rollback (zero/content/PackRollback): the check after the game stopped and the
// question before the next launch. Neither depends on a page being open; Application calls them from its launch hooks.
#pragma once

#include <QDateTime>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

#include "zero/content/PackOrder.h"

#include "ui/zero/instance/ModsDialogs.h"

class MinecraftInstance;
class QWidget;

namespace Zero {
namespace PacksUi {

/// What the question needs from an instance (tests fill it with temp folders).
struct RollbackEnv {
    QString instanceRoot;
    QString gameRoot;
    std::function<bool()> isRunning;
};

RollbackEnv rollbackEnvFor(MinecraftInstance* instance);

/// The game of `instance` stopped: reads its latest.log and writes or removes the rollback marker. `launchStart`: logs older than it are ignored.
void detectRollbackAfterLaunch(MinecraftInstance* instance, const QDateTime& launchStart);

/// What the question before Play is about (the "Before you play" dialog shows it as one row; `askBeforeLaunch` as its own question).
struct RollbackOffer {
    QString markerPath;
    QStringList presentSuspects;   ///< the suspect packs that are still in the folder
    QStringList order;             ///< the pack order to write for a restore (top first)
    QSet<QString> existing;        ///< the pack files in the folder
};

/// Is there something to ask? A valid marker not asked yet, options.txt readable with no `file/` pack on, and at least one pack that can
/// still come back. Reads files only.
std::optional<RollbackOffer> rollbackOffer(const RollbackEnv& env);
/// Writes the restore order (a history entry makes it undoable) and drops the marker when it worked.
Content::Packs::Outcome restoreRollback(const RollbackEnv& env, const RollbackOffer& offer);
/// The user chose to play without the packs: the marker stays but is not asked again.
void dismissRollback(const RollbackOffer& offer);

/// Before playing: when the game turned the packs off last time and options.txt has no pack on now, asks once whether to put them back
/// (Restore without X and play / Play without them / Cancel). False = the user cancelled the launch. `ask` defaults to a ChoiceDialog.
bool askBeforeLaunch(const RollbackEnv& env, QWidget* parent, const ModsUi::AskFunction& ask = {});
bool askBeforeLaunch(MinecraftInstance* instance, QWidget* parent);

}  // namespace PacksUi
}  // namespace Zero
