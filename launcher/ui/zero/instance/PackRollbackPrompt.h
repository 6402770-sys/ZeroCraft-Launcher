// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the application side of the pack rollback (zero/content/PackRollback): the check after the game stopped and the
// question before the next launch. Neither depends on a page being open; Application calls them from its launch hooks.
#pragma once

#include <QDateTime>
#include <QString>

#include <functional>

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

/// Before playing: when the game turned the packs off last time and options.txt has no pack on now, asks once whether to put them back
/// (Restore without X and play / Play without them / Cancel). False = the user cancelled the launch. `ask` defaults to a ChoiceDialog.
bool askBeforeLaunch(const RollbackEnv& env, QWidget* parent, const ModsUi::AskFunction& ask = {});
bool askBeforeLaunch(MinecraftInstance* instance, QWidget* parent);

}  // namespace PacksUi
}  // namespace Zero
