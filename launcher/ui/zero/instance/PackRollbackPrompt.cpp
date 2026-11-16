// SPDX-License-Identifier: GPL-3.0-only
#include "PackRollbackPrompt.h"

#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QSet>

#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/LaunchProfile.h"
#include "minecraft/Library.h"
#include "minecraft/mod/ResourcePack.h"
#include "zero/content/ModTarget.h"
#include "zero/content/PackOrder.h"
#include "zero/content/PackRollback.h"

namespace Zero {
namespace PacksUi {

using namespace Content::Packs;

RollbackEnv rollbackEnvFor(MinecraftInstance* instance)
{
    RollbackEnv env;
    env.instanceRoot = instance->instanceRoot();
    env.gameRoot = instance->gameRoot();
    QPointer<MinecraftInstance> guarded(instance);
    env.isRunning = [guarded]() { return guarded && guarded->isRunning(); };
    return env;
}

void detectRollbackAfterLaunch(MinecraftInstance* instance, const QDateTime& launchStart)
{
    if (!instance) {
        return;
    }
    Rollback::DetectInput input;
    input.instanceRoot = instance->instanceRoot();
    input.gameRoot = instance->gameRoot();
    input.launchStart = launchStart;
    // the resource format of the instance's Minecraft version; unknown (newer than the table) = every overlay counts as applicable
    // exact: the client jar's own version.json; else the table (unknown = every overlay counts as applicable)
    if (auto profile = instance->getPackProfile()->getProfile()) {
        if (auto jar = profile->getMainJar()) {
            QStringList jars, native32, native64, other, nativeOther;
            jar->getApplicableFiles(instance->runtimeContext(), jars, native32, native64, other, instance->getLocalLibraryPath());
            if (!jars.isEmpty()) {
                input.format = Rollback::formatFromClientJar(jars.first());
            }
        }
    }
    if (!input.format) {
        input.format = Rollback::formatForVersion(ResourcePack(QFileInfo()).mappings(), Content::targetFromInstance(instance).mcVersion);
    }
    Rollback::detectAfterLaunch(input);
}

std::optional<RollbackOffer> rollbackOffer(const RollbackEnv& env)
{
    const QString markerPath = Rollback::markerPathFor(env.instanceRoot);
    const Rollback::Marker marker = Rollback::readMarker(markerPath);
    if (!marker.valid || marker.asked) {
        return std::nullopt;
    }
    const PackList options = readOptions(optionsPathFor(env.gameRoot));
    if (options.state == PackList::State::Unreadable || !Rollback::shouldAskBeforeLaunch(marker, activeFiles(options.entries))) {
        return std::nullopt;
    }
    // only what is still in the folder can come back; nothing left = nothing to ask
    RollbackOffer offer;
    offer.markerPath = markerPath;
    offer.existing = packFilesIn(QDir(env.gameRoot).filePath(QStringLiteral("resourcepacks")));
    const QStringList suspects = Rollback::suspectFiles(marker.suspects);
    for (const QString& name : suspects) {
        if (offer.existing.contains(name)) {
            offer.presentSuspects.append(name);
        }
    }
    offer.order = Rollback::restoreOrder(marker, !offer.presentSuspects.isEmpty(), {}, offer.existing);
    if (offer.order.isEmpty()) {
        return std::nullopt;
    }
    return offer;
}

Outcome restoreRollback(const RollbackEnv& env, const RollbackOffer& offer)
{
    const Outcome outcome = writeOrder(optionsPathFor(env.gameRoot), historyPathFor(env.instanceRoot), offer.order, offer.existing, env.isRunning);
    if (outcome.ok()) {
        Rollback::clearMarker(offer.markerPath);  // (writeOrder clears it on a change; a "no change" leaves nothing to restore either)
    }
    return outcome;
}

void dismissRollback(const RollbackOffer& offer)
{
    Rollback::markAsked(offer.markerPath);
}

bool askBeforeLaunch(const RollbackEnv& env, QWidget* parent, const ModsUi::AskFunction& askOverride)
{
    const std::optional<RollbackOffer> found = rollbackOffer(env);
    if (!found) {
        return true;
    }
    const RollbackOffer& offer = *found;
    const QStringList& presentSuspects = offer.presentSuspects;

    ModsUi::ChoicePrompt prompt;
    prompt.title = Rollback::promptTitle();
    prompt.body = Rollback::promptBody(presentSuspects);
    prompt.buttons = { { Rollback::promptRestoreAndPlayText(presentSuspects), ModsUi::ChoiceButton::Kind::Primary },
                       { Rollback::promptPlayWithoutText(), ModsUi::ChoiceButton::Kind::Secondary },
                       { QObject::tr("Cancel"), ModsUi::ChoiceButton::Kind::Ghost } };
    prompt.defaultButton = 0;
    const int answer = (askOverride ? askOverride : ModsUi::AskFunction(ModsUi::askWithDialog))(parent, prompt);
    if (answer == 1) {
        dismissRollback(offer);  // the user chose to play without them: not asked again for this marker
        return true;
    }
    if (answer != 0) {
        return false;  // Cancel / closed: nothing changed, the question comes back with the next Play
    }
    const Outcome outcome = restoreRollback(env, offer);
    if (outcome.ok()) {
        return true;
    }
    ModsUi::ChoicePrompt failed;
    failed.title = QObject::tr("Couldn't turn your resource packs back on");
    failed.body = outcome.error.isEmpty() ? QObject::tr("options.txt could not be changed. You can still play without the packs.") : outcome.error;
    failed.buttons = { { QObject::tr("Play without them"), ModsUi::ChoiceButton::Kind::Primary },
                       { QObject::tr("Cancel"), ModsUi::ChoiceButton::Kind::Ghost } };
    failed.defaultButton = 0;
    return (askOverride ? askOverride : ModsUi::AskFunction(ModsUi::askWithDialog))(parent, failed) == 0;
}

bool askBeforeLaunch(MinecraftInstance* instance, QWidget* parent)
{
    return askBeforeLaunch(rollbackEnvFor(instance), parent);
}

}  // namespace PacksUi
}  // namespace Zero
