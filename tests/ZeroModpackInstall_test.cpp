// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the modpack install flow (docs/zero/CONTENT.md, "Modpacks" > "Install"): the install plan per provider,
// the real tasks it builds (never started), the default version rules, the logo names and the install dialog (default version,
// first decision New instance / Update an instance, name validation, the logo is fetched only on Install). Offscreen, no network.
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QPushButton>
#include <QTest>
#include <functional>
#include <memory>

#include "InstanceImportTask.h"
#include "InstanceTask.h"
#include "modplatform/technic/SingleZipPackInstallTask.h"
#include "modplatform/technic/SolderPackInstallTask.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/content/InstallChoiceCard.h"
#include "ui/zero/content/ModpackInstallDialog.h"
#include "ui/zero/content/RemoteImageCache.h"
#include "zero/content/CreateInstanceModel.h"
#include "zero/content/ModpackInstall.h"
#include "zero/content/ModpackSource.h"

using namespace Zero;
using namespace Zero::Content;

namespace {

/// InstanceTask has no getter for the icon key: read the protected member through a derived class.
struct TaskPeek : InstanceTask {
    static QString icon(const InstanceTask* task) { return task->*(&TaskPeek::m_instIcon); }
    static bool copyTemplate(const InstanceTask* task) { return task->shouldCopyTemplateDir(); }
};

QString S(const char* text)
{
    return QString::fromLatin1(text);
}

ModpackItemPtr makeItem(Provider provider, const QString& id, const QString& name)
{
    ModPlatform::IndexedPack pack;
    pack.addonId = id;
    pack.provider = provider == Provider::CurseForge ? ModPlatform::ResourceProvider::FLAME : ModPlatform::ResourceProvider::MODRINTH;
    pack.name = name;
    pack.slug = name.toLower().replace(QLatin1Char(' '), QLatin1Char('-'));
    pack.authors = { { S("someone"), QString() } };
    pack.logoUrl = S("https://example.test/icon/%1.png").arg(id);
    pack.gameVersions = { S("1.20.1") };
    pack.loaderNames = { S("fabric") };
    return itemFromPack(provider, pack);
}

ModpackVersion makeVersion(const QString& id, const QString& name, ModpackVersion::Type type, int day,
                           const QString& url = S("https://example.test/dl"))
{
    ModpackVersion v;
    v.id = id;
    v.name = name;
    v.type = type;
    v.gameVersions = { S("1.20.1") };
    v.loaders = { S("fabric") };
    v.date = QDateTime(QDate(2024, 3, day), QTime(10, 0), Qt::UTC);
    v.downloadUrl = url;
    return v;
}

ModPlatform::IndexedVersion indexed(const QString& fileId, const QString& name, ModPlatform::IndexedVersionTypeValue type, int day,
                                    const QString& url)
{
    ModPlatform::IndexedVersion v;
    v.fileId = fileId;
    v.version = name;
    v.versionNumber = name;
    v.versionType = ModPlatform::IndexedVersionType(type);
    v.mcVersion = { S("1.20.1") };
    v.downloadUrl = url;
    v.date = S("2024-03-%1T10:00:00Z").arg(day, 2, 10, QLatin1Char('0'));
    return v;
}

ModpackItemPtr technicItem(bool solder)
{
    TechnicPackSummary summary;
    summary.slug = S("tech-pack");
    summary.name = S("Tech Pack");
    summary.iconUrl = S("https://example.test/t.png");
    auto item = itemFromTechnicSummary(summary);
    TechnicPackDetails details;
    details.slug = summary.slug;
    details.displayName = summary.name;
    details.minecraftVersion = S("1.12.2");
    details.version = S("2.0");
    if (solder) {
        details.solderUrl = S("https://solder.example.test/api/");
    } else {
        details.downloadUrl = S("https://example.test/pack.zip");
    }
    applyTechnicDetails(*item, details);
    return item;
}

InstallRequest requestFor(const ModpackItemPtr& item, const ModpackVersion& version)
{
    InstallRequest r;
    r.item = item;
    r.version = version;
    r.name = S("  My Instance ");
    r.group = S("Packs");
    r.targetDir = S("C:/instances");
    r.copyTemplate = true;
    r.iconKey = S("modrinth_pack");
    return r;
}

struct DialogRig {
    RemoteImageCache images{ [](const QString&, std::function<void(const RemoteImageCache::FetchResult&)> done) { done({}); } };
    QList<InstalledPack> installed;
    int fetches = 0;
    int installs = 0;
    QString installedName;
    QString fetchedFile;

    ModpackInstallEnvironment environment()
    {
        ModpackInstallEnvironment env;
        env.images = &images;
        env.create.instanceDirs = []() { return QList<QPair<QString, QString>>{ { S("Default"), S("C:/instances") } }; };
        env.create.lastUsedDir = S("C:/instances");
        env.installedPacks = [this]() { return installed; };
        env.fetchLogo = [this](const QString&, const QString& fileName, std::function<void(const QString&)> done) {
            ++fetches;
            fetchedFile = fileName;
            done(QString());  // synchronous, no network: no logo
        };
        env.installIcon = [this](const QString&, const QString& name) {
            ++installs;
            installedName = name;
        };
        return env;
    }
};

ModpackItemPtr itemWithVersions(const QList<ModPlatform::IndexedVersion>& versions)
{
    auto item = makeItem(Provider::Modrinth, S("AABBCC"), S("Cool Pack"));
    item->pack->versions = versions;
    item->pack->versionsLoaded = true;
    return item;
}

ModPlatform::IndexedVersion oneRelease()
{
    return indexed(S("f1"), S("1.0"), ModPlatform::IndexedVersionTypeValue::Release, 10, S("https://cdn.test/1"));
}

}  // namespace

class ZeroModpackInstallTest : public QObject {
    Q_OBJECT

   private slots:
    void initTestCase() { qRegisterMetaType<Zero::Content::ModpackItemPtr>("Zero::Content::ModpackItemPtr"); }

    // ---- planInstall ----

    void planModrinth()
    {
        auto item = makeItem(Provider::Modrinth, S("AABBCC"), S("Cool Pack"));
        const auto v = makeVersion(S("v1"), S("1.2.3"), ModpackVersion::Type::Release, 10, S("https://cdn.test/a.mrpack"));
        const auto plan = planInstall(requestFor(item, v));
        QVERIFY(plan);
        QCOMPARE(plan->kind, InstallPlan::Kind::Import);
        QCOMPARE(plan->url, QUrl(S("https://cdn.test/a.mrpack")));
        QCOMPARE(plan->extraInfo.value(S("pack_id")), S("AABBCC"));
        QCOMPARE(plan->extraInfo.value(S("pack_version_id")), S("v1"));
        QVERIFY(!plan->extraInfo.contains(S("original_instance_id")));
        QVERIFY(plan->createNew);  // a new instance: the provider must not go looking for one to update
        QCOMPARE(plan->originalVersion, S("1.2.3"));
        QCOMPARE(plan->originalName, S("Cool Pack"));
        QCOMPARE(plan->name, S("My Instance"));
        QCOMPARE(plan->group, S("Packs"));
        QCOMPARE(plan->targetDir, S("C:/instances"));
        QCOMPARE(plan->iconKey, S("modrinth_pack"));
        QVERIFY(plan->copyTemplate);
    }

    void planCurseForge()
    {
        auto item = makeItem(Provider::CurseForge, S("4242"), S("Flame Pack"));
        const auto v = makeVersion(S("9001"), S("Flame 1.0"), ModpackVersion::Type::Release, 10, S("https://cdn.test/f.zip"));
        const auto plan = planInstall(requestFor(item, v));
        QVERIFY(plan);
        QCOMPARE(plan->kind, InstallPlan::Kind::Import);
        QCOMPARE(plan->url, QUrl(S("https://cdn.test/f.zip")));
        QCOMPARE(plan->extraInfo.value(S("pack_id")), S("4242"));
        QCOMPARE(plan->extraInfo.value(S("pack_version_id")), S("9001"));
        QVERIFY(plan->originalVersion.isEmpty());  // the manifest names the version
    }

    void planUpdate()
    {
        auto item = makeItem(Provider::CurseForge, S("4242"), S("Flame Pack"));
        const auto v = makeVersion(S("9002"), S("2.0"), ModpackVersion::Type::Release, 11, S("https://cdn.test/f2.zip"));
        InstallRequest r = requestFor(item, v);
        r.name = S("Flame Pack 1.0");
        r.updateInstanceId = S("inst-1");
        r.updateVersionName = S("1.0");
        const auto plan = planInstall(r);
        QVERIFY(plan);
        QCOMPARE(plan->extraInfo.value(S("original_instance_id")), S("inst-1"));
        QVERIFY(!plan->createNew);
        QCOMPARE(plan->extraInfo.value(S("pack_version_id")), S("9002"));
        QCOMPARE(plan->originalVersion, S("2.0"));  // CurseForge too, on an update
        QCOMPARE(plan->name, S("Flame Pack 2.0"));

        auto modrinth = makeItem(Provider::Modrinth, S("AABBCC"), S("Cool Pack"));
        InstallRequest m = requestFor(modrinth, v);
        m.updateInstanceId = S("inst-2");
        const auto mplan = planInstall(m);
        QVERIFY(mplan);
        QCOMPARE(mplan->extraInfo.value(S("original_instance_id")), S("inst-2"));
        QCOMPARE(mplan->name, S("My Instance"));  // no managed version name: untouched
    }

    void planTechnic()
    {
        const auto v = makeVersion(S("1.5.0"), S("1.5.0"), ModpackVersion::Type::Release, 10, QString());
        const auto solder = planInstall(requestFor(technicItem(true), v));
        QVERIFY(solder);
        QCOMPARE(solder->kind, InstallPlan::Kind::TechnicSolder);
        QCOMPARE(solder->url, QUrl(S("https://solder.example.test/api")));  // no trailing slash
        QCOMPARE(solder->technicSlug, S("tech-pack"));
        QCOMPARE(solder->technicBuild, S("1.5.0"));
        QCOMPARE(solder->minecraftVersion, S("1.12.2"));
        QVERIFY(solder->extraInfo.isEmpty());

        const auto zip = planInstall(requestFor(technicItem(false), v));
        QVERIFY(zip);
        QCOMPARE(zip->kind, InstallPlan::Kind::TechnicZip);
        QCOMPARE(zip->url, QUrl(S("https://example.test/pack.zip")));
        QCOMPARE(zip->minecraftVersion, S("1.12.2"));
        QCOMPARE(zip->name, S("My Instance"));
        QCOMPARE(zip->originalName, S("Tech Pack"));
    }

    void planRefusals()
    {
        const auto v = makeVersion(S("v1"), S("1"), ModpackVersion::Type::Release, 10);
        QVERIFY(!planInstall(requestFor(nullptr, v)));

        auto item = makeItem(Provider::Modrinth, S("AABBCC"), S("Cool Pack"));
        QVERIFY(!planInstall(requestFor(item, makeVersion(S("v1"), S("1"), ModpackVersion::Type::Release, 10, QString()))));
        QVERIFY(!planInstall(requestFor(item, makeVersion(S("v1"), S("1"), ModpackVersion::Type::Release, 10, S("  ")))));

        TechnicPackSummary summary;
        summary.slug = S("p");
        summary.name = S("P");
        auto bare = itemFromTechnicSummary(summary);
        QVERIFY(!planInstall(requestFor(bare, v)));  // details not loaded
        auto empty = technicItem(true);
        empty->technic->solderUrl.clear();
        QVERIFY(!planInstall(requestFor(empty, v)));  // neither zip nor Solder
    }

    void planDefaultIcon()
    {
        auto item = makeItem(Provider::Modrinth, S("AABBCC"), S("Cool Pack"));
        InstallRequest r = requestFor(item, makeVersion(S("v1"), S("1"), ModpackVersion::Type::Release, 10));
        r.iconKey.clear();
        QCOMPARE(planInstall(r)->iconKey, S("default"));
    }

    // ---- createInstallTask ----

    void taskImport()
    {
        auto item = makeItem(Provider::Modrinth, S("AABBCC"), S("Cool Pack"));
        const auto plan = planInstall(requestFor(item, makeVersion(S("v1"), S("1.2.3"), ModpackVersion::Type::Release, 10)));
        std::unique_ptr<InstanceTask> task(createInstallTask(*plan, nullptr, nullptr));
        QVERIFY(task);
        QVERIFY(dynamic_cast<InstanceImportTask*>(task.get()));
        checkCommon(*task, S("1.2.3"));
    }

    void taskSolder()
    {
        const auto plan = planInstall(requestFor(technicItem(true), makeVersion(S("1.5.0"), S("1.5.0"), ModpackVersion::Type::Release, 10, QString())));
        QNetworkAccessManager network;
        std::unique_ptr<InstanceTask> task(createInstallTask(*plan, nullptr, &network));
        QVERIFY(task);
        QVERIFY(dynamic_cast<Technic::SolderPackInstallTask*>(task.get()));
        checkCommon(*task, S("1.5.0"), S("Tech Pack"));
    }

    void taskZip()
    {
        const auto plan = planInstall(requestFor(technicItem(false), makeVersion(S("2.0"), S("2.0"), ModpackVersion::Type::Release, 10, QString())));
        std::unique_ptr<InstanceTask> task(createInstallTask(*plan, nullptr, nullptr));
        QVERIFY(task);
        QVERIFY(dynamic_cast<Technic::SingleZipPackInstallTask*>(task.get()));
        checkCommon(*task, S("2.0"), S("Tech Pack"));
    }

    // ---- defaultVersionIndex ----

    void defaultNewestReleaseOverBeta()
    {
        const QList<ModpackVersion> list = {
            makeVersion(S("b"), S("3.0-beta"), ModpackVersion::Type::Beta, 20),
            makeVersion(S("r2"), S("2.0"), ModpackVersion::Type::Release, 15),
            makeVersion(S("r1"), S("1.0"), ModpackVersion::Type::Release, 10),
        };
        QCOMPARE(defaultVersionIndex(list, Provider::Modrinth), 1);
    }

    void defaultRecommendedWins()
    {
        QList<ModpackVersion> list = {
            makeVersion(S("3"), S("3"), ModpackVersion::Type::Release, 20, QString()),
            makeVersion(S("2"), S("2"), ModpackVersion::Type::Release, 15, QString()),
        };
        list[1].recommended = true;
        QCOMPARE(defaultVersionIndex(list, Provider::Technic), 1);
    }

    void defaultSkipsNotInstallable()
    {
        const QList<ModpackVersion> list = {
            makeVersion(S("r3"), S("3.0"), ModpackVersion::Type::Release, 20, QString()),
            makeVersion(S("r2"), S("2.0"), ModpackVersion::Type::Release, 15),
        };
        QVERIFY(!isInstallable(list[0], Provider::Modrinth));
        QCOMPARE(defaultVersionIndex(list, Provider::Modrinth), 1);
    }

    void defaultAllBetasAndNone()
    {
        const QList<ModpackVersion> betas = {
            makeVersion(S("b2"), S("2b"), ModpackVersion::Type::Beta, 20),
            makeVersion(S("b1"), S("1b"), ModpackVersion::Type::Alpha, 10),
        };
        QCOMPARE(defaultVersionIndex(betas, Provider::Modrinth), 0);
        QCOMPARE(defaultVersionIndex({}, Provider::Modrinth), -1);
        const QList<ModpackVersion> broken = { makeVersion(S("x"), S("x"), ModpackVersion::Type::Release, 10, QString()) };
        QCOMPARE(defaultVersionIndex(broken, Provider::CurseForge), -1);
    }

    // ---- logo ----

    void logoNames()
    {
        auto item = makeItem(Provider::Modrinth, S("AABBCC"), S("Cool Pack"));
        QCOMPARE(logoFileName(*item), S("cool-pack.png"));  // slug fallback
        QCOMPARE(logoIconName(*item), S("modrinth_cool-pack.png"));
        QCOMPARE(logoIconKey(*item), S("modrinth_cool-pack"));

        item->pack->logoName = S("my logo.jpg");  // the logoName suffix wins, unsafe characters are replaced
        QCOMPARE(logoFileName(*item), S("my_logo.jpg"));
        QCOMPARE(logoIconKey(*item), S("modrinth_my_logo"));

        item->pack->logoName = S("a b:c*.webp");
        QCOMPARE(logoFileName(*item), S("a_b_c_.webp"));

        auto flame = makeItem(Provider::CurseForge, S("4242"), S("Flame"));
        QCOMPARE(logoIconName(*flame), S("curseforge_flame.png"));
        QCOMPARE(logoPrefix(Provider::Technic), S("technic_"));

        item->iconUrl.clear();
        QCOMPARE(logoFileName(*item), QString());
        QCOMPARE(logoIconName(*item), QString());
        QCOMPARE(logoIconKey(*item), S("default"));
    }

    // ---- dialog ----

    void dialogDefaultsToNewestRelease()
    {
        DialogRig rig;
        auto item = itemWithVersions({
            indexed(S("f3"), S("3.0-beta"), ModPlatform::IndexedVersionTypeValue::Beta, 20, S("https://cdn.test/3")),
            indexed(S("f2"), S("2.0"), ModPlatform::IndexedVersionTypeValue::Release, 15, S("https://cdn.test/2")),
            indexed(S("f1"), S("1.0"), ModPlatform::IndexedVersionTypeValue::Release, 10, S("https://cdn.test/1")),
        });
        ModpackInstallDialog dialog(rig.environment(), item, QString(), QString());
        QCOMPARE(dialog.versionBox()->currentIndex(), 1);
        QCOMPARE(dialog.selectedVersion()->name, S("2.0"));
        QCOMPARE(dialog.nameField()->text(), S("Cool Pack"));
        QVERIFY(dialog.canInstall());
        QVERIFY(dialog.installButton()->isEnabled());
        QVERIFY(!dialog.instanceListVisible());
        QVERIFY(dialog.versionPills().contains(S("Release")));
    }

    // ---- the first decision ----

    void dialogNoManagedInstanceDisablesUpdate()
    {
        DialogRig rig;
        auto item = itemWithVersions({ oneRelease() });
        // another provider's pack with the same id does not count
        rig.installed = { { managedKey(S("curseforge"), S("AABBCC")), S("x"), S("Other"), QString(), QString() } };
        ModpackInstallDialog dialog(rig.environment(), item, QString(), QString());
        QVERIFY(dialog.newCard()->isChecked());
        QVERIFY(!dialog.updateCard()->isEnabled());
        QVERIFY(!dialog.updateCard()->isChecked());
        QCOMPARE(dialog.newCard()->title(), S("New instance"));
        QCOMPARE(dialog.newCard()->caption(), S("A separate copy"));
        QCOMPARE(dialog.updateCard()->title(), S("Update an instance"));
        QCOMPARE(dialog.updateCard()->caption(), S("None of your instances has this pack"));
        QVERIFY(!dialog.isUpdate());
        QVERIFY(dialog.updateInstanceId().isEmpty());
        QVERIFY(dialog.instanceRows().isEmpty());
        QVERIFY(!dialog.instanceListVisible());
        QVERIFY(!dialog.keepVisible());
        QVERIFY(!dialog.keepGameSettings());
        QCOMPARE(dialog.installButton()->text(), S("Install"));
        QVERIFY(dialog.nameField()->isVisibleTo(&dialog));
        QVERIFY(dialog.canInstall());
        // asking for an update of an instance that does not exist changes nothing
        ModpackInstallDialog ghost(rig.environment(), item, S("nope"), QString());
        QVERIFY(ghost.newCard()->isChecked());
        QVERIFY(!ghost.isUpdate());
    }

    void dialogManagedInstancePreselectsUpdate()
    {
        DialogRig rig;
        auto item = itemWithVersions({ oneRelease() });
        rig.installed = { { installedKey(*item), S("inst-1"), S("My Cool Pack"), S("f0"), S("0.9"), 0 } };
        ModpackInstallDialog dialog(rig.environment(), item, QString(), QString());  // no id from Discover: Update is still the default
        QVERIFY(dialog.updateCard()->isEnabled());
        QVERIFY(dialog.updateCard()->isChecked());
        QCOMPARE(dialog.updateCard()->caption(), S("One you already have"));
        QVERIFY(dialog.isUpdate());
        QCOMPARE(dialog.updateInstanceId(), S("inst-1"));
        QVERIFY(dialog.instanceListVisible());
        QCOMPARE(dialog.instanceRows().size(), 1);
        QCOMPARE(dialog.instanceRows().first()->title(), S("My Cool Pack"));
        QCOMPARE(dialog.instanceRows().first()->caption(), S("Version 0.9  ") + QChar(0xb7) + S("  Never played"));
        QVERIFY(dialog.instanceRows().first()->isChecked());
        // update hides what only a new instance needs and shows "Keep my game settings", on
        QVERIFY(!dialog.nameField()->isVisibleTo(&dialog));
        QVERIFY(!dialog.groupBox()->isVisibleTo(&dialog));
        QVERIFY(!dialog.templateBox()->isVisibleTo(&dialog));
        QVERIFY(dialog.keepVisible());
        QVERIFY(dialog.keepGameSettings());
        QCOMPARE(dialog.keepBox()->text(), S("Keep my game settings"));
        QCOMPARE(dialog.installButton()->text(), S("Update"));
        QCOMPARE(dialog.windowTitle(), S("Update Cool Pack"));
        QVERIFY(dialog.canInstall());  // the name does not matter to an update
        QVERIFY(dialog.versionBox()->isVisibleTo(&dialog));

        dialog.keepBox()->setChecked(false);
        QVERIFY(!dialog.keepGameSettings());
    }

    void dialogSwitchesBetweenNewAndUpdate()
    {
        DialogRig rig;
        auto item = itemWithVersions({ oneRelease() });
        rig.installed = { { installedKey(*item), S("inst-1"), S("My Cool Pack"), S("f0"), S("0.9") } };
        ModpackInstallDialog dialog(rig.environment(), item, S("inst-1"), QString());
        QVERIFY(dialog.isUpdate());

        dialog.newCard()->click();
        QVERIFY(!dialog.isUpdate());
        QVERIFY(dialog.newCard()->isChecked());
        QVERIFY(!dialog.updateCard()->isChecked());
        QVERIFY(dialog.updateInstanceId().isEmpty());
        QVERIFY(!dialog.instanceListVisible());
        QVERIFY(!dialog.keepVisible());
        QVERIFY(!dialog.keepGameSettings());
        QVERIFY(dialog.nameField()->isVisibleTo(&dialog));
        QCOMPARE(dialog.nameField()->text(), S("Cool Pack"));  // the suggested name stays
        QVERIFY(dialog.groupBox()->isVisibleTo(&dialog));
        QCOMPARE(dialog.installButton()->text(), S("Install"));
        QCOMPARE(dialog.windowTitle(), S("Install Cool Pack"));
        dialog.nameField()->clear();
        QVERIFY(!dialog.canInstall());  // the New form is validated again

        dialog.updateCard()->click();
        QVERIFY(dialog.isUpdate());
        QCOMPARE(dialog.updateInstanceId(), S("inst-1"));
        QVERIFY(dialog.canInstall());  // ... and an update ignores the name
        QCOMPARE(dialog.installButton()->text(), S("Update"));
    }

    void dialogListsEveryManagedInstance()
    {
        DialogRig rig;
        auto item = itemWithVersions({ oneRelease() });
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const qint64 hour = 3600 * 1000;
        rig.installed = {
            { installedKey(*item), S("old"), S("Old Copy 0.5"), S("f0"), S("0.5"), now - 48 * hour },
            { installedKey(*item), S("recent"), S("Recent Copy"), S("f1"), S("1.0"), now - 5 * 60 * 1000 },
            { managedKey(S("curseforge"), S("AABBCC")), S("alien"), S("Other Provider"), S("f1"), S("1.0"), now },
            { installedKey(*item), S("never"), S("Never Played"), QString(), QString(), 0 },
        };
        {
            // no id from Discover: the most recently played first
            ModpackInstallDialog dialog(rig.environment(), item, QString(), QString());
            const auto rows = dialog.instanceRows();
            QCOMPARE(rows.size(), 3);  // the other provider's instance is not a target
            QCOMPARE(rows.at(0)->title(), S("Recent Copy"));
            QCOMPARE(rows.at(1)->title(), S("Old Copy 0.5"));
            QCOMPARE(rows.at(2)->title(), S("Never Played"));
            QCOMPARE(rows.at(0)->caption(), S("Version 1.0  ") + QChar(0xb7) + S("  Played 5 minutes ago"));
            QCOMPARE(rows.at(2)->caption(), S("Never played"));  // no version name: just the play time
            QVERIFY(rows.at(0)->isChecked());
            QCOMPARE(dialog.updateInstanceId(), S("recent"));

            // choosing another row changes the instance the update targets (radio behaviour)
            rows.at(1)->click();
            QVERIFY(rows.at(1)->isChecked());
            QVERIFY(!rows.at(0)->isChecked());
            QCOMPARE(dialog.updateInstanceId(), S("old"));
            std::unique_ptr<InstanceTask> task(dialog.extractTask());
            QVERIFY(task);
            QCOMPARE(task->name(), S("Old Copy 1.0"));  // renamed like ManagedPackPage ("0.5" -> the new version name)
        }
        {
            // the instance Discover passed goes first, whatever its play time
            ModpackInstallDialog dialog(rig.environment(), item, S("never"), QString());
            const auto rows = dialog.instanceRows();
            QCOMPARE(rows.size(), 3);
            QCOMPARE(rows.at(0)->title(), S("Never Played"));
            QCOMPARE(rows.at(1)->title(), S("Recent Copy"));
            QVERIFY(rows.at(0)->isChecked());
            QCOMPARE(dialog.updateInstanceId(), S("never"));
        }
    }

    void dialogRowsAreOneRadioGroup()
    {
        DialogRig rig;
        auto item = itemWithVersions({ oneRelease() });
        rig.installed = { { installedKey(*item), S("a"), S("A"), QString(), QString() }, { installedKey(*item), S("b"), S("B"), QString(), QString() } };
        ModpackInstallDialog dialog(rig.environment(), item, QString(), QString());
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        const auto rows = dialog.instanceRows();
        rows.at(0)->setFocus();
        QTest::keyClick(rows.at(0), Qt::Key_Down);  // arrows move the choice, as for radio buttons
        QVERIFY(rows.at(1)->isChecked());
        QCOMPARE(dialog.updateInstanceId(), S("b"));
        rows.at(1)->click();  // clicking the chosen one keeps it
        QVERIFY(rows.at(1)->isChecked());
    }

    void dialogTechnicCannotUpdate()
    {
        DialogRig rig;
        auto item = technicItem(true);
        rig.installed = { { installedKey(*item), S("t"), S("Tech Copy"), QString(), QString() } };
        ModpackInstallDialog dialog(rig.environment(), item, S("t"), QString());
        QVERIFY(!dialog.updateCard()->isEnabled());
        QCOMPARE(dialog.updateCard()->caption(), S("Technic packs can't be updated here"));
        QVERIFY(dialog.newCard()->isChecked());
        QVERIFY(!dialog.isUpdate());
        QVERIFY(dialog.updateInstanceId().isEmpty());
        QVERIFY(!canUpdateInPlace(Provider::Technic));
        QVERIFY(canUpdateInPlace(Provider::Modrinth));
        QVERIFY(canUpdateInPlace(Provider::CurseForge));
    }

    void dialogTasksNeverAskAgain()
    {
        DialogRig rig;
        auto item = itemWithVersions({ oneRelease() });
        rig.installed = { { installedKey(*item), S("inst-1"), S("My Cool Pack"), S("f0"), S("0.9") } };
        ModpackInstallDialog dialog(rig.environment(), item, QString(), QString());
        {
            // Update: the provider updates inst-1 without its own "Similar modpack was found!" question
            std::unique_ptr<InstanceTask> task(dialog.extractTask());
            QVERIFY(dynamic_cast<InstanceImportTask*>(task.get()));
            QVERIFY(!task->shouldConfirmUpdate());
            QVERIFY(!task->shouldAlwaysCreateNew());
            QCOMPARE(task->name(), S("My Cool Pack"));  // no version in its name: unchanged
            QCOMPARE(TaskPeek::icon(task.get()), S("default"));  // the instance keeps its icon (the fake environment gives none)
        }
        dialog.newCard()->click();
        {
            // New: no question either, and the provider is told not to look for the instance it would otherwise update
            std::unique_ptr<InstanceTask> task(dialog.extractTask());
            QVERIFY(task);
            QVERIFY(!task->shouldConfirmUpdate());
            QVERIFY(task->shouldAlwaysCreateNew());
            QCOMPARE(task->name(), S("Cool Pack"));
        }
    }

    void dialogLogoOnlyOnAccept()
    {
        DialogRig rig;
        auto item = itemWithVersions({ oneRelease() });
        ModpackInstallDialog dialog(rig.environment(), item, QString(), QString());
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QCOMPARE(rig.fetches, 0);
        QCOMPARE(rig.installs, 0);
        dialog.accept();
        QCOMPARE(rig.fetches, 1);
        QCOMPARE(rig.fetchedFile, S("cool-pack.png"));
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(rig.installs, 0);  // the fake gave no file: no icon is installed, the default one stays
        QCOMPARE(dialog.iconKey(), S("default"));
    }

    void dialogInstallsLogoWhenFetched()
    {
        DialogRig rig;
        auto env = rig.environment();
        env.fetchLogo = [&rig](const QString&, const QString&, std::function<void(const QString&)> done) {
            ++rig.fetches;
            done(S("C:/tmp/logo.png"));
        };
        auto item = itemWithVersions({ oneRelease() });
        ModpackInstallDialog dialog(env, item, QString(), QString());
        dialog.accept();
        QCOMPARE(rig.fetches, 1);
        QCOMPARE(rig.installs, 1);
        QCOMPARE(rig.installedName, S("modrinth_cool-pack.png"));
        QCOMPARE(dialog.iconKey(), S("modrinth_cool-pack"));
    }

    void dialogNameValidation()
    {
        DialogRig rig;
        auto item = itemWithVersions({ oneRelease() });
        ModpackInstallDialog dialog(rig.environment(), item, QString(), QString());
        QVERIFY(dialog.canInstall());
        QVERIFY(dialog.nameError().isEmpty());

        dialog.nameField()->setText(S("x"));
        dialog.nameField()->setCursorPosition(1);
        QTest::keyClick(dialog.nameField(), Qt::Key_Backspace);  // the user empties the field (a programmatic clear shows no error yet)
        QVERIFY(dialog.nameField()->text().isEmpty());
        QVERIFY(!dialog.canInstall());
        QVERIFY(!dialog.installButton()->isEnabled());
        QCOMPARE(dialog.nameError(), Create::validateName(QString()));

        dialog.nameField()->setText(QString(Create::kMaxNameLength + 1, QLatin1Char('a')));
        QVERIFY(!dialog.canInstall());
        QVERIFY(!dialog.installButton()->isEnabled());
        QVERIFY(!dialog.nameError().isEmpty());

        dialog.nameField()->setText(QString(Create::kMaxNameLength, QLatin1Char('a')));
        QVERIFY(dialog.canInstall());
        QVERIFY(dialog.nameError().isEmpty());

        dialog.nameField()->clear();
        dialog.accept();  // refused while the name is invalid
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QCOMPARE(rig.fetches, 0);
    }

    void dialogExtractTask()
    {
        DialogRig rig;
        auto item = itemWithVersions({
            indexed(S("f2"), S("2.0"), ModPlatform::IndexedVersionTypeValue::Release, 15, S("https://cdn.test/2")),
            indexed(S("f1"), S("1.0"), ModPlatform::IndexedVersionTypeValue::Release, 10, S("https://cdn.test/1")),
        });
        ModpackInstallDialog dialog(rig.environment(), item, QString(), S("Packs"));
        dialog.versionBox()->setCurrentIndex(1);
        dialog.nameField()->setText(S("Chosen"));
        std::unique_ptr<InstanceTask> task(dialog.extractTask());
        QVERIFY(task);
        QVERIFY(dynamic_cast<InstanceImportTask*>(task.get()));
        QCOMPARE(task->name(), S("Chosen"));
        QCOMPARE(task->originalName(), S("Cool Pack"));
        QCOMPARE(task->version(), S("1.0"));
        QCOMPARE(task->group(), S("Packs"));
        QCOMPARE(task->targetDir(), S("C:/instances"));
        QCOMPARE(TaskPeek::icon(task.get()), S("default"));
    }

   private:
    static void checkCommon(const InstanceTask& task, const QString& version, const QString& originalName = S("Cool Pack"))
    {
        QCOMPARE(task.name(), S("My Instance"));
        QCOMPARE(task.originalName(), originalName);
        QCOMPARE(task.version(), version);
        QCOMPARE(task.group(), S("Packs"));
        QCOMPARE(task.targetDir(), S("C:/instances"));
        QCOMPARE(TaskPeek::icon(&task), S("modrinth_pack"));
        QVERIFY(TaskPeek::copyTemplate(&task));
    }
};

QTEST_MAIN(ZeroModpackInstallTest)

#include "ZeroModpackInstall_test.moc"
