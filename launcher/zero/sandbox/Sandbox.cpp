// SPDX-License-Identifier: GPL-3.0-only
#include "Sandbox.h"

#include <QCoreApplication>

#include "BaseInstance.h"
#include "settings/SettingsObject.h"
#include "zero/profiles/InstanceProfile.h"

namespace Zero {
namespace Sandbox {

QString sandboxOf(BaseInstance* instance)
{
    return instance ? instance->settings()->get(kKey).toString().trimmed() : QString();
}

void setSandboxOf(BaseInstance* instance, const QString& originalId)
{
    if (!instance || sandboxOf(instance) == originalId.trimmed())
        return;
    instance->settings()->set(kKey, originalId.trimmed());
    emit instance->propertiesChanged();
    Profile::Notifier::instance()->notify(instance->id());
}

QString testCopyName(const QString& originalName)
{
    return QCoreApplication::translate("Zero::Sandbox", "%1 (test copy)").arg(originalName);
}

InstanceCopyPrefs copyPrefs(bool copyWorlds)
{
    InstanceCopyPrefs prefs;
    prefs.enableCopySaves(copyWorlds);
    prefs.enableCopyScreenshots(false);
    prefs.enableKeepPlaytime(false);
    prefs.enableUseSymLinks(false);
    prefs.enableLinkRecursively(false);
    prefs.enableUseHardLinks(false);
    prefs.enableDontLinkSaves(false);
    prefs.enableUseClone(false);
    return prefs;
}

}  // namespace Sandbox
}  // namespace Zero
