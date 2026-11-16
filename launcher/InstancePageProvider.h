#pragma once
#include <FileSystem.h>
#include <ui/pages/instance/DataPackPage.h>
#include "minecraft/MinecraftInstance.h"
#include "ui/pages/BasePage.h"
#include "ui/pages/BasePageProvider.h"
#include "ui/pages/instance/InstanceSettingsPage.h"
#include "ui/pages/instance/LogPage.h"
#include "ui/pages/instance/ManagedPackPage.h"
#include "ui/pages/instance/ModFolderPage.h"
#include "ui/pages/instance/NotesPage.h"
#include "ui/pages/instance/OtherLogsPage.h"
#include "ui/pages/instance/ResourcePackPage.h"
#include "ui/pages/instance/ScreenshotsPage.h"
#include "ui/pages/instance/ServersPage.h"
#include "ui/pages/instance/ShaderPackPage.h"
#include "ui/pages/instance/TexturePackPage.h"
#include "ui/pages/instance/VersionPage.h"
#include "ui/zero/instance/ModsPage.h"  // ZERO
#include "ui/zero/instance/ResourcePacksPage.h"  // ZERO
#include "ui/zero/instance/ShaderPacksPage.h"    // ZERO
#include "ui/pages/instance/WorldListPage.h"

class InstancePageProvider : protected QObject, public BasePageProvider {
    Q_OBJECT
   public:
    explicit InstancePageProvider(MinecraftInstance* parent) { inst = parent; }

    virtual ~InstancePageProvider() = default;
    virtual QList<BasePage*> getPages() override
    {
        QList<BasePage*> values;
        values.append(new LogPage(inst));
        values.append(new VersionPage(inst));
        values.append(ManagedPackPage::createPage(inst));
        values.append(new Zero::ModsUi::ModsPage(inst, inst->loaderModList()));  // ZERO: replaces ModFolderPage (same id "mods")
        values.append(new CoreModFolderPage(inst, inst->coreModList()));
        values.append(new NilModFolderPage(inst, inst->nilModList()));
        values.append(new Zero::PacksUi::ResourcePacksPage(inst, inst->resourcePackList()));  // ZERO: replaces ResourcePackPage (same id "resourcepacks")
        values.append(new GlobalDataPackPage(inst));
        values.append(new TexturePackPage(inst, inst->texturePackList()));
        values.append(new Zero::ShadersUi::ShaderPacksPage(inst, inst->shaderPackList()));  // ZERO: replaces ShaderPackPage (same id "shaderpacks")
        values.append(new NotesPage(inst));
        values.append(new WorldListPage(inst, inst->worldList()));
        values.append(new ServersPage(inst));
        values.append(new ScreenshotsPage(FS::PathCombine(inst->gameRoot(), "screenshots")));
        values.append(new InstanceSettingsPage(inst));
        values.append(new OtherLogsPage("logs", tr("Other Logs"), "Other-Logs", inst));
        return values;
    }

    virtual QString dialogTitle() override { return tr("Edit Instance (%1)").arg(inst->name()); }

   protected:
    MinecraftInstance* inst;
};
