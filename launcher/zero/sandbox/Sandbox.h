// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - "Try it in a copy" (docs/zero/ARCHITECTURE.md, "Test copies"): the link between a test copy and the instance it was made
// from, and the settings of the copy. A test copy is an ordinary instance (a full, independent copy made by Prism's InstanceCopyTask) whose
// `instance.cfg` has `ZeroSandboxOf=<id of the original>`. Nothing else marks it. No widgets.
#pragma once

#include <QString>

#include "InstanceCopyPrefs.h"

class BaseInstance;

namespace Zero {
namespace Sandbox {

/// The `instance.cfg` key (registered by BaseInstance).
inline constexpr char kKey[] = "ZeroSandboxOf";

/// The id of the original instance, empty when `instance` is not a test copy.
QString sandboxOf(BaseInstance* instance);
inline bool isTestCopy(BaseInstance* instance) { return !sandboxOf(instance).isEmpty(); }
/// Sets (or, with an empty id, clears) the link. Ignores an equal value; otherwise saves, emits propertiesChanged and Profile::Notifier::profileChanged.
void setSandboxOf(BaseInstance* instance, const QString& originalId);

/// "<name> (test copy)"
QString testCopyName(const QString& originalName);

/// What a test copy takes from the original: everything but the screenshots, no play time, and never a link (an independent copy);
/// `copyWorlds` = false leaves `saves` out.
InstanceCopyPrefs copyPrefs(bool copyWorlds);

}  // namespace Sandbox
}  // namespace Zero
