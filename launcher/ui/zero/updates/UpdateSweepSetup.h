// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the real application behind Zero::Updates::UpdateSweep (docs/zero/ARCHITECTURE.md, "UpdateSweep"): the instance list, the
// Mods page's update check (loads the instance's mod folder first, then runs Prism's checkers), and the applier that takes a mod snapshot
// (the same safety net as the Mods page, so the page's "Undo" works for these updates too), runs the downloads as a Downloads-center job
// and finalizes the snapshot when the job ended.
#pragma once

#include <QObject>

namespace Zero {
namespace Updates {

class UpdateSweep;

/// The engine reading `<data>/zero/updates.json`, wired to APPLICATION (instances, settings "ZeroAutoCheckUpdates", Downloads center).
/// Call start() on it to arm the automatic check.
UpdateSweep* createApplicationSweep(QObject* parent);

}  // namespace Updates
}  // namespace Zero
