// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the mod install flow (docs/zero/CONTENT-MODS.md, "Mod install"): the compatible versions of a target, the
// default version, the install plan (standalone / dependency / update), the already-installed names, the summary texts and the review
// dialog with a fake dependency resolver (checking, rows, none, already installed, failure + Retry + "Add without dependencies", a
// version change that drops the stale answer, the disabled setting, Update mode, a running target). Offscreen, no network.
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QTest>
#include <functional>
#include <memory>

#include "ui/zero/content/ModInstallDialog.h"
#include "ui/zero/content/RemoteImageCache.h"
#include "zero/content/ModInstall.h"
#include "zero/content/ModpackSource.h"

using namespace Zero;
using namespace Zero::Content;

namespace {

QString S(const char* text)
{
    return QString::fromUtf8(text);
}

constexpr ModPlatform::ModLoaderTypes kFabric = ModPlatform::ModLoaderTypes(ModPlatform::Fabric);

ModPlatform::IndexedVersion ver(const QString& fileId, const QString& name, ModPlatform::IndexedVersionTypeValue type, int day,
                                const QStringList& mc = { S("1.21.1") }, ModPlatform::ModLoaderTypes loaders = kFabric,
                                const QString& url = S("https://cdn.test/mod.jar"))
{
    ModPlatform::IndexedVersion v;
    v.addonId = S("PROJ");
    v.fileId = fileId;
    v.version = name;
    v.versionNumber = name;
    v.versionType = ModPlatform::IndexedVersionType(type);
    v.mcVersion = mc;
    v.loaders = loaders;
    v.downloadUrl = url;
    v.fileName = name + S(".jar");
    v.date = S("2024-03-%1T10:00:00Z").arg(day, 2, 10, QLatin1Char('0'));
    v.fileSize = 1536 * 1024;
    return v;
}

using Release = ModPlatform::IndexedVersionTypeValue;

ModPlatform::IndexedPack::Ptr makePack(const QString& id, const QString& name, bool withLogo = false)
{
    auto pack = std::make_shared<ModPlatform::IndexedPack>();
    pack->addonId = id;
    pack->provider = ModPlatform::ResourceProvider::MODRINTH;
    pack->name = name;
    pack->slug = name.toLower().replace(QLatin1Char(' '), QLatin1Char('-'));
    pack->authors = { { S("someone"), QString() } };
    if (withLogo) {
        pack->logoUrl = S("https://example.test/icon/%1.png").arg(id);
    }
    pack->gameVersions = { S("1.21.1") };
    pack->loaderNames = { S("fabric") };
    return pack;
}

ContentItemPtr makeItem(const QList<ModPlatform::IndexedVersion>& versions, const QString& name = S("Iris Shaders"))
{
    auto item = itemFromPack(Provider::Modrinth, *makePack(S("PROJ"), name));
    item->pack->versions = versions;
    item->pack->versionsLoaded = true;
    return item;
}

PlannedMod dependency(const QString& id, const QString& name, const QString& requiredBy, const QString& fileId = S("dep-file"),
                      bool maybeInstalled = false)
{
    PlannedMod mod;
    mod.pack = makePack(id, name);
    mod.version = ver(fileId, S("9.9"), Release::Release, 5);
    mod.version.addonId = id;
    mod.reason = reasonDependency();
    mod.dependentOn = S("dependent-file");
    mod.requiredBy = { requiredBy };
    mod.maybeInstalled = maybeInstalled;
    return mod;
}

TargetInstance makeTarget(bool running = false)
{
    TargetInstance t;
    t.id = S("inst1");
    t.name = S("Fabric Fun");
    t.iconKey = S("default");
    t.mcVersion = S("1.21.1");
    t.loaders = kFabric;
    t.loaderText = S("Fabric");
    t.running = running;
    return t;
}

/// Answers by hand: records every request, and the test decides when and what it answers (with which token).
class FakeResolver : public DependencyResolver {
   public:
    explicit FakeResolver(std::shared_ptr<int> destroyedCancels) : m_cancelsOnDestroy(std::move(destroyedCancels)) {}
    ~FakeResolver() override { cancel(); }
    void resolve(quint64 token, const Request& request) override
    {
        requests.append({ token, request });
    }
    void cancel() override
    {
        ++cancels;
        if (m_cancelsOnDestroy) {
            ++*m_cancelsOnDestroy;
        }
    }
    void answer(const QList<PlannedMod>& deps) { emit resolved(requests.last().first, deps); }
    void answerFor(int request, const QList<PlannedMod>& deps) { emit resolved(requests.at(request).first, deps); }
    void fail(const QString& reason) { emit failed(requests.last().first, reason); }

    QList<QPair<quint64, Request>> requests;
    int cancels = 0;

   private:
    std::shared_ptr<int> m_cancelsOnDestroy;
};

/// The versions a source returns when the item carries none.
class FakeSource : public ModpackSource {
   public:
    Provider provider() const override { return Provider::Modrinth; }
    void search(const ResourceAPI::SearchArgs&, quint64) override {}
    void details(const ModpackItemPtr&, quint64) override {}
    void versions(const ModpackItemPtr& item, quint64 generation) override { calls.append({ item, generation }); }
    void changelog(const ModpackItemPtr&, const QString&, quint64) override {}
    QList<QPair<ModpackItemPtr, quint64>> calls;
};

struct Rig {
    RemoteImageCache images{ [](const QString&, std::function<void(const RemoteImageCache::FetchResult&)> done) { done({}); } };
    QList<InstalledMod> installed;
    bool dependencyCheck = true;
    bool running = false;
    FakeResolver* resolver = nullptr;
    FakeSource* source = nullptr;
    std::shared_ptr<int> destroyedCancels = std::make_shared<int>(0);

    ModInstallEnvironment environment()
    {
        ModInstallEnvironment env;
        env.images = &images;
        env.source = source;
        env.makeResolver = [this](QObject* parent) -> DependencyResolver* {
            resolver = new FakeResolver(destroyedCancels);
            resolver->setParent(parent);
            return resolver;
        };
        env.dependencyCheckEnabled = [this]() { return dependencyCheck; };
        env.installedMods = [this]() { return installed; };
        env.instanceIcon = [](const QString&) { return QIcon(); };
        env.isRunning = [this](const QString&) { return running; };
        return env;
    }
};

QList<ModPlatform::IndexedVersion> twoVersions()
{
    return { ver(S("f2"), S("2.0"), Release::Release, 20), ver(S("f1"), S("1.0"), Release::Release, 10) };
}

}  // namespace

class ZeroModInstallTest : public QObject {
    Q_OBJECT

   private slots:
    void initTestCase()
    {
        qRegisterMetaType<Zero::Content::ModpackItemPtr>("Zero::Content::ModpackItemPtr");
        qRegisterMetaType<QList<Zero::Content::PlannedMod>>("QList<Zero::Content::PlannedMod>");
    }

    // ---- compatibility ----

    void compatibleFiltering()
    {
        const auto fits = ver(S("a"), S("a"), Release::Release, 10);
        const auto oldMc = ver(S("b"), S("b"), Release::Release, 11, { S("1.20.1") });
        const auto forgeOnly = ver(S("c"), S("c"), Release::Release, 12, { S("1.21.1") }, ModPlatform::ModLoaderTypes(ModPlatform::Forge));
        const auto anyLoader = ver(S("d"), S("d"), Release::Release, 13, { S("1.21.1") }, ModPlatform::ModLoaderTypes());
        const auto noUrl = ver(S("e"), S("e"), Release::Release, 14, { S("1.21.1") }, kFabric, QString());
        const auto multi = ver(S("f"), S("f"), Release::Release, 15, { S("1.21"), S("1.21.1") },
                               ModPlatform::ModLoaderTypes(ModPlatform::Fabric) | ModPlatform::Quilt);
        const auto quiltOnly = ver(S("g"), S("g"), Release::Release, 16, { S("1.21.1") }, ModPlatform::ModLoaderTypes(ModPlatform::Quilt));
        auto noFile = ver(S("h"), S("h"), Release::Release, 17);
        noFile.fileName.clear();

        const auto out = compatibleVersions({ fits, oldMc, forgeOnly, anyLoader, noUrl, multi, quiltOnly, noFile }, S("1.21.1"), kFabric);
        QStringList ids;
        for (const auto& v : out) {
            ids.append(v.fileId.toString());
        }
        QCOMPARE(ids, (QStringList{ S("a"), S("d"), S("f") }));  // the provider's order is kept

        // A target without loaders or version asks nothing of that part.
        QCOMPARE(compatibleVersions({ fits, oldMc }, QString(), ModPlatform::ModLoaderTypes()).size(), 2);
        QVERIFY(versionFitsTarget(fits, S("1.21.1"), ModPlatform::ModLoaderTypes()));
        QVERIFY(!versionFitsTarget(oldMc, S("1.21.1"), kFabric));
        // The 26.x scheme: a 26.3 build does not fit a 26.2 instance.
        QVERIFY(!versionFitsTarget(ver(S("y"), S("y"), Release::Release, 1, { S("26.3") }), S("26.2"), kFabric));
        // A version that names no Minecraft version fits (the Mods section's rule).
        QVERIFY(versionFitsTarget(ver(S("z"), S("z"), Release::Release, 1, {}), S("1.21.1"), kFabric));
    }

    void defaultVersion()
    {
        // newest release beats a newer beta
        QList<ModPlatform::IndexedVersion> list = { ver(S("b"), S("3.0-beta"), Release::Beta, 20), ver(S("r2"), S("2.0"), Release::Release, 15),
                                                    ver(S("r1"), S("1.0"), Release::Release, 10) };
        QCOMPARE(defaultModVersionIndex(list), 1);
        // only prereleases: the newest (first) of anything
        const QList<ModPlatform::IndexedVersion> betas = { ver(S("b2"), S("2b"), Release::Beta, 20), ver(S("a1"), S("1a"), Release::Alpha, 10) };
        QCOMPARE(defaultModVersionIndex(betas), 0);
        QCOMPARE(defaultModVersionIndex({}), -1);
    }

    // ---- plan ----

    void planStandaloneWithDependencies()
    {
        const auto pack = makePack(S("PROJ"), S("Iris Shaders"));
        const auto version = ver(S("f2"), S("2.0"), Release::Release, 20);
        const auto plan = buildInstallPlan(pack, version, false,
                                           { dependency(S("SODIUM"), S("Sodium"), S("Iris Shaders"), S("s1")),
                                             dependency(S("SODIUM"), S("Sodium again"), S("x")),  // the same project twice
                                             dependency(S("PROJ"), S("Iris Shaders"), S("x")),    // the mod itself
                                             dependency(S("FAPI"), S("Fabric API"), S("Sodium"), S("a1")) });
        QCOMPARE(plan.count(), 3);
        QCOMPARE(plan.dependencyCount(), 2);
        QCOMPARE(plan.mods.at(0).name(), S("Iris Shaders"));
        QCOMPARE(plan.mods.at(0).reason, reasonStandalone());
        QCOMPARE(plan.mods.at(0).reason, S("standalone"));
        QVERIFY(plan.mods.at(0).dependentOn.isEmpty());
        QCOMPARE(plan.mods.at(1).name(), S("Sodium"));
        QCOMPARE(plan.mods.at(1).reason, S("dependency"));
        QCOMPARE(plan.mods.at(1).dependentOn, S("dependent-file"));
        QCOMPARE(plan.mods.at(1).requiredBy, QStringList{ S("Iris Shaders") });
        QCOMPARE(plan.mods.at(1).version.fileId.toString(), S("s1"));
        QCOMPARE(plan.mods.at(2).name(), S("Fabric API"));
    }

    void planUpdateAndEmpty()
    {
        const auto pack = makePack(S("PROJ"), S("Sodium"));
        const auto plan = buildInstallPlan(pack, ver(S("f"), S("1"), Release::Release, 1), true, {});
        QCOMPARE(plan.count(), 1);
        QCOMPARE(plan.mods.at(0).reason, S("update"));
        QCOMPARE(plan.dependencyCount(), 0);
        // an entry that says "standalone" in its own field is still a dependency in the plan
        PlannedMod odd = dependency(S("D"), S("D"), S("Sodium"));
        odd.reason = S("standalone");
        QCOMPARE(buildInstallPlan(pack, ver(S("f"), S("1"), Release::Release, 1), false, { odd }).mods.at(1).reason, S("dependency"));
        QVERIFY(buildInstallPlan(nullptr, ModPlatform::IndexedVersion(), false, {}).isEmpty());
    }

    // ---- already installed ----

    void alreadyInstalled()
    {
        auto version = ver(S("f"), S("1"), Release::Release, 1);
        const auto dep = [](const QString& id, ModPlatform::DependencyTypeValue type) {
            ModPlatform::Dependency d;
            d.addonId = id;
            d.type = ModPlatform::DependencyType(type);
            return d;
        };
        version.dependencies = { dep(S("FAPI"), ModPlatform::DependencyTypeValue::REQUIRED), dep(S("SODIUM"), ModPlatform::DependencyTypeValue::REQUIRED),
                                 dep(S("OPT"), ModPlatform::DependencyTypeValue::OPTIONAL), dep(QString(), ModPlatform::DependencyTypeValue::REQUIRED),
                                 dep(S("FAPI"), ModPlatform::DependencyTypeValue::REQUIRED) };
        const QList<InstalledMod> installed = { { managedKey(S("modrinth"), S("FAPI")), S("x"), S("Fabric API"), S("0.1") },
                                                { managedKey(S("modrinth"), S("OPT")), S("x"), S("Optional Thing"), QString() },
                                                { managedKey(S("flame"), S("SODIUM")), S("x"), S("Sodium (CurseForge)"), QString() } };
        // Modrinth: only the required one that is installed; optional and id-less dependencies never count; repeated ids once
        QCOMPARE(alreadyInstalledNames(ModPlatform::ResourceProvider::MODRINTH, version, installed), QStringList{ S("Fabric API") });
        // CurseForge keys differ: only SODIUM matches there
        QCOMPARE(alreadyInstalledNames(ModPlatform::ResourceProvider::FLAME, version, installed), QStringList{ S("Sodium (CurseForge)") });
        QVERIFY(alreadyInstalledNames(ModPlatform::ResourceProvider::MODRINTH, version, {}).isEmpty());
    }

    // ---- texts ----

    void texts()
    {
        QCOMPARE(primaryActionText(1, false), S("Add mod"));
        QCOMPARE(primaryActionText(3, false), S("Add 3 mods"));
        QCOMPARE(primaryActionText(1, true), S("Update"));
        QCOMPARE(primaryActionText(2, true), S("Update and add 1 mod"));
        QCOMPARE(primaryActionText(4, true), S("Update and add 3 mods"));
        QCOMPARE(addedToastText(S("Iris Shaders"), S("Fabric Fun"), 0), S("Added Iris Shaders to Fabric Fun"));
        QCOMPARE(addedToastText(S("Iris Shaders"), S("Fabric Fun"), 1), S("Added Iris Shaders to Fabric Fun and 1 dependency"));
        QCOMPARE(addedToastText(S("Iris Shaders"), S("Fabric Fun"), 2), S("Added Iris Shaders to Fabric Fun and 2 dependencies"));
        QCOMPARE(updatedToastText(S("Sodium"), S("Fabric Fun"), 0), S("Updated Sodium in Fabric Fun"));
        QCOMPARE(updatedToastText(S("Sodium"), S("Fabric Fun"), 2), S("Updated Sodium in Fabric Fun and added 2 dependencies"));
        QCOMPARE(requiredByText({ S("Sodium") }), S("Required by Sodium"));
        QCOMPARE(requiredByText({ S("Sodium"), S("Lithium") }), S("Required by Sodium, Lithium"));
        QVERIFY(requiredByText({}).isEmpty());
    }

    // ---- dialog ----

    void dialogSettlesOnceThePicturesAreThere()
    {
        Rig rig;
        auto item = itemFromPack(Provider::Modrinth, *makePack(S("PROJ"), S("Iris Shaders"), true));
        item->pack->versions = twoVersions();
        item->pack->versionsLoaded = true;
        ModInstallDialog dialog(rig.environment(), item, makeTarget(), QString());
        auto dep = dependency(S("SODIUM"), S("Sodium"), S("Iris Shaders"));
        dep.pack->logoUrl = S("https://example.test/icon/sodium.png");
        rig.resolver->answer({ dep });
        QVERIFY(!dialog.isSettled());  // nothing was painted, so no picture was asked for yet
        dialog.show();
        QTRY_VERIFY(dialog.isSettled());  // the fake fetcher fails at once: a failed picture is settled
        int calls = 0;
        dialog.callWhenSettled([&calls]() { ++calls; }, 2000);
        QTRY_COMPARE(calls, 1);
    }

    void dialogChecksThenShowsRows()
    {
        Rig rig;
        ModInstallDialog dialog(rig.environment(), makeItem({ ver(S("f3"), S("3.0-beta"), Release::Beta, 20), ver(S("f2"), S("2.0"), Release::Release, 15) }),
                                makeTarget(), QString());
        QVERIFY(rig.resolver);
        QCOMPARE(dialog.windowTitle(), S("Add Iris Shaders"));
        QCOMPARE(dialog.modName(), S("Iris Shaders"));
        QCOMPARE(dialog.modAuthor(), S("by someone"));
        QCOMPARE(dialog.headerPills(), QStringList{ S("Modrinth") });
        QCOMPARE(dialog.targetLine(), S("Adding to Fabric Fun · 1.21.1 Fabric"));
        // the newest compatible release is preselected, the pills describe it
        QCOMPARE(dialog.versionBox()->count(), 2);
        QCOMPARE(dialog.selectedVersion()->fileId.toString(), S("f2"));
        QCOMPARE(dialog.versionPills(), (QStringList{ S("Release"), S("Minecraft 1.21.1"), S("Fabric"), S("1.5 MB") }));
        // checking: skeleton + caption, nothing to press
        QVERIFY(dialog.isChecking());
        QCOMPARE(dialog.dependencyStatus(), S("Checking dependencies…"));
        QVERIFY(!dialog.primaryButton()->isEnabled());
        QVERIFY(!dialog.isSettled());
        QCOMPARE(rig.resolver->requests.size(), 1);
        QCOMPARE(rig.resolver->requests.at(0).second.version.fileId.toString(), S("f2"));
        QCOMPARE(rig.resolver->requests.at(0).second.instanceId, S("inst1"));

        rig.resolver->answer({ dependency(S("SODIUM"), S("Sodium"), S("Iris Shaders"), S("s1")), dependency(S("FAPI"), S("Fabric API"), S("Sodium"), S("a1")) });
        QVERIFY(!dialog.isChecking());
        QCOMPARE(dialog.dependencyNames(), (QStringList{ S("Sodium"), S("Fabric API") }));
        QCOMPARE(dialog.dependencyRequiredBy(), (QStringList{ S("Required by Iris Shaders"), S("Required by Sodium") }));
        QVERIFY(dialog.dependencyStatus().isEmpty());
        QVERIFY(dialog.primaryButton()->isEnabled());
        QCOMPARE(dialog.primaryButton()->text(), S("Add 3 mods"));
        QCOMPARE(dialog.plan().count(), 3);
        QCOMPARE(dialog.plan().mods.at(0).reason, S("standalone"));
        QCOMPARE(dialog.plan().mods.at(1).reason, S("dependency"));
        QVERIFY(dialog.isSettled());
        QVERIFY(!dialog.skipDependenciesBox()->isVisibleTo(&dialog));
    }

    void dialogNoOtherModsNeeded()
    {
        Rig rig;
        ModInstallDialog dialog(rig.environment(), makeItem(twoVersions()), makeTarget(), QString());
        rig.resolver->answer({});
        QCOMPARE(dialog.dependencyStatus(), S("No other mods needed"));
        QVERIFY(dialog.dependencyNames().isEmpty());
        QVERIFY(dialog.alreadyInstalledText().isEmpty());
        QCOMPARE(dialog.primaryButton()->text(), S("Add mod"));
        QVERIFY(dialog.primaryButton()->isEnabled());
        QCOMPARE(dialog.plan().count(), 1);
    }

    void dialogAlreadyInstalled()
    {
        Rig rig;
        auto item = makeItem(twoVersions());
        ModPlatform::Dependency d;
        d.addonId = S("FAPI");
        d.type = ModPlatform::DependencyType(ModPlatform::DependencyTypeValue::REQUIRED);
        item->pack->versions[0].dependencies = { d };
        rig.installed = { { managedKey(S("modrinth"), S("FAPI")), S("x"), S("Fabric API"), S("0.1") } };
        ModInstallDialog dialog(rig.environment(), item, makeTarget(), QString());
        rig.resolver->answer({ dependency(S("SODIUM"), S("Sodium"), S("Iris Shaders")) });
        QCOMPARE(dialog.dependencyNames(), QStringList{ S("Sodium") });
        QCOMPARE(dialog.alreadyInstalledText(), S("Already installed: Fabric API"));
        QCOMPARE(dialog.primaryButton()->text(), S("Add 2 mods"));
    }

    void dialogMaybeInstalledIsNotAdded()
    {
        Rig rig;
        ModInstallDialog dialog(rig.environment(), makeItem(twoVersions()), makeTarget(), QString());
        rig.resolver->answer({ dependency(S("SODIUM"), S("Sodium"), S("Iris Shaders"), S("s1"), true) });
        QVERIFY(dialog.dependencyNames().isEmpty());
        QCOMPARE(dialog.dependencyStatus(), S("No other mods needed"));
        QCOMPARE(dialog.alreadyInstalledText(), S("Already installed: Sodium"));
        QCOMPARE(dialog.plan().count(), 1);
    }

    void dialogFailureRetryAndSkip()
    {
        Rig rig;
        ModInstallDialog dialog(rig.environment(), makeItem(twoVersions()), makeTarget(), QString());
        rig.resolver->fail(S("network down"));
        QVERIFY(dialog.dependencyCheckFailed());
        QVERIFY(!dialog.isChecking());
        QCOMPARE(dialog.dependencyStatus(), S("Couldn't check dependencies: network down"));
        QVERIFY(dialog.retryButton()->isVisibleTo(&dialog));
        QVERIFY(dialog.skipDependenciesBox()->isVisibleTo(&dialog));
        QVERIFY(!dialog.skipDependenciesBox()->isChecked());  // never preset
        QVERIFY(!dialog.primaryButton()->isEnabled());
        QVERIFY(dialog.isSettled());

        dialog.skipDependenciesBox()->setChecked(true);
        QVERIFY(dialog.primaryButton()->isEnabled());
        QCOMPARE(dialog.plan().count(), 1);  // without dependencies
        QCOMPARE(dialog.primaryButton()->text(), S("Add mod"));
        dialog.skipDependenciesBox()->setChecked(false);
        QVERIFY(!dialog.primaryButton()->isEnabled());

        // Retry asks again; the checkbox goes away with the failure and starts unchecked next time
        dialog.skipDependenciesBox()->setChecked(true);
        dialog.retryButton()->click();
        QCOMPARE(rig.resolver->requests.size(), 2);
        QVERIFY(dialog.isChecking());
        QVERIFY(!dialog.retryButton()->isVisibleTo(&dialog));
        QVERIFY(!dialog.skipDependenciesBox()->isVisibleTo(&dialog));
        QVERIFY(!dialog.skipDependenciesBox()->isChecked());
        QVERIFY(!dialog.primaryButton()->isEnabled());
        rig.resolver->answer({ dependency(S("SODIUM"), S("Sodium"), S("Iris Shaders")) });
        QVERIFY(dialog.primaryButton()->isEnabled());
        QCOMPARE(dialog.primaryButton()->text(), S("Add 2 mods"));
    }

    void dialogVersionChangeDropsStaleAnswer()
    {
        Rig rig;
        ModInstallDialog dialog(rig.environment(), makeItem(twoVersions()), makeTarget(), QString());
        QCOMPARE(rig.resolver->requests.size(), 1);
        const int cancelsBefore = rig.resolver->cancels;
        dialog.versionBox()->setCurrentIndex(1);  // 1.0
        QCOMPARE(rig.resolver->requests.size(), 2);
        QVERIFY(rig.resolver->cancels > cancelsBefore);  // the check of 2.0 was stopped
        QCOMPARE(rig.resolver->requests.at(1).second.version.fileId.toString(), S("f1"));
        QVERIFY(rig.resolver->requests.at(0).first != rig.resolver->requests.at(1).first);

        // The old run answers late: ignored, still checking.
        rig.resolver->answerFor(0, { dependency(S("OLD"), S("Old Dependency"), S("x")) });
        QVERIFY(dialog.isChecking());
        QVERIFY(dialog.dependencyNames().isEmpty());
        QVERIFY(!dialog.primaryButton()->isEnabled());
        // ... and a late failure of it as well
        emit rig.resolver->failed(rig.resolver->requests.at(0).first, S("stale"));
        QVERIFY(dialog.isChecking());

        rig.resolver->answerFor(1, { dependency(S("NEW"), S("New Dependency"), S("Iris Shaders")) });
        QCOMPARE(dialog.dependencyNames(), QStringList{ S("New Dependency") });
        QCOMPARE(dialog.plan().mods.at(0).version.fileId.toString(), S("f1"));

        // An answer of the finished run answered again is not applied twice
        rig.resolver->answerFor(1, { dependency(S("NEW2"), S("Another"), S("Iris Shaders")) });
        QCOMPARE(dialog.dependencyNames(), QStringList{ S("New Dependency") });
    }

    void dialogDisabledSetting()
    {
        Rig rig;
        rig.dependencyCheck = false;
        ModInstallDialog dialog(rig.environment(), makeItem(twoVersions()), makeTarget(), QString());
        QVERIFY(rig.resolver->requests.isEmpty());
        QVERIFY(!dialog.isChecking());
        QCOMPARE(dialog.dependencyStatus(), S("Dependency check is turned off in Settings"));
        QVERIFY(dialog.primaryButton()->isEnabled());
        QCOMPARE(dialog.primaryButton()->text(), S("Add mod"));
        QVERIFY(!dialog.skipDependenciesBox()->isVisibleTo(&dialog));
        QCOMPARE(dialog.plan().count(), 1);
    }

    void dialogUpdateMode()
    {
        Rig rig;
        ModInstallDialog dialog(rig.environment(), makeItem(twoVersions(), S("Sodium")), makeTarget(), S("f1"));
        QVERIFY(dialog.isUpdate());
        QCOMPARE(dialog.windowTitle(), S("Update Sodium"));
        QVERIFY(dialog.targetLine().startsWith(S("Updating in Fabric Fun")));
        QCOMPARE(dialog.primaryButton()->text(), S("Update"));
        rig.resolver->answer({});
        QVERIFY(dialog.primaryButton()->isEnabled());
        QCOMPARE(dialog.plan().mods.at(0).reason, S("update"));
        QCOMPARE(dialog.plan().mods.at(0).version.fileId.toString(), S("f2"));  // the newest compatible one
        // with a new dependency the button counts it
        dialog.versionBox()->setCurrentIndex(1);
        rig.resolver->answer({ dependency(S("FAPI"), S("Fabric API"), S("Sodium")) });
        QCOMPARE(dialog.primaryButton()->text(), S("Update and add 1 mod"));
        QCOMPARE(dialog.plan().mods.at(0).reason, S("update"));
    }

    void dialogRunningTarget()
    {
        Rig rig;
        rig.running = true;
        ModInstallDialog dialog(rig.environment(), makeItem(twoVersions()), makeTarget(true), QString());
        QCOMPARE(dialog.noteText(), S("Close Fabric Fun to add mods"));
        rig.resolver->answer({});
        QVERIFY(!dialog.primaryButton()->isEnabled());  // settled, but refused while it runs
        QVERIFY(!dialog.canInstall());
        rig.running = false;
        dialog.versionBox()->setCurrentIndex(1);  // any state refresh re-reads the running state
        rig.resolver->answer({});
        QVERIFY(dialog.primaryButton()->isEnabled());
        QVERIFY(dialog.noteText().isEmpty());
    }

    void dialogNoCompatibleVersion()
    {
        Rig rig;
        ModInstallDialog dialog(rig.environment(),
                                makeItem({ ver(S("old"), S("1.0"), Release::Release, 10, { S("1.20.1") }),
                                           ver(S("forge"), S("1.1"), Release::Release, 11, { S("1.21.1") }, ModPlatform::ModLoaderTypes(ModPlatform::Forge)) }),
                                makeTarget(), QString());
        QCOMPARE(dialog.versionStatus(), S("No version of this mod fits 1.21.1 Fabric."));
        QVERIFY(!dialog.selectedVersion());
        QVERIFY(rig.resolver->requests.isEmpty());
        QVERIFY(!dialog.primaryButton()->isEnabled());
        QVERIFY(dialog.dependencyStatus().isEmpty());
        QVERIFY(dialog.plan().isEmpty());
    }

    void dialogAsksTheSourceForMissingVersions()
    {
        Rig rig;
        FakeSource source;
        rig.source = &source;
        auto item = itemFromPack(Provider::Modrinth, *makePack(S("PROJ"), S("Lithium")));  // no versions loaded
        ModInstallDialog dialog(rig.environment(), item, makeTarget(), QString());
        QCOMPARE(source.calls.size(), 1);
        QCOMPARE(source.calls.at(0).second, ModInstallDialog::kGeneration);
        QVERIFY(!dialog.selectedVersion());
        QVERIFY(!dialog.isSettled());
        QVERIFY(rig.resolver->requests.isEmpty());

        // an answer of another generation (the section's own request) is not ours
        item->pack->versions = twoVersions();
        item->pack->versionsLoaded = true;
        emit source.versionsFinished(ModInstallDialog::kGeneration + 1, item, {});
        QVERIFY(!dialog.selectedVersion());

        emit source.versionsFinished(ModInstallDialog::kGeneration, item, {});
        QVERIFY(dialog.selectedVersion());
        QCOMPARE(rig.resolver->requests.size(), 1);

        // a failure is inline, with Retry for the versions
        Rig rig2;
        FakeSource source2;
        rig2.source = &source2;
        ModInstallDialog failing(rig2.environment(), itemFromPack(Provider::Modrinth, *makePack(S("P2"), S("Other"))), makeTarget(), QString());
        emit source2.versionsFailed(ModInstallDialog::kGeneration, S("timed out"));
        QCOMPARE(failing.versionStatus(), S("Could not load the versions: timed out"));
        QVERIFY(!failing.primaryButton()->isEnabled());
    }

    void dialogDestructionCancelsTheCheck()
    {
        Rig rig;
        auto dialog = std::make_unique<ModInstallDialog>(rig.environment(), makeItem(twoVersions()), makeTarget(), QString());
        QVERIFY(dialog->isChecking());
        FakeResolver* resolver = rig.resolver;
        QPointer<FakeResolver> guard(resolver);
        const int before = *rig.destroyedCancels;
        dialog.reset();  // the check is cancelled and the resolver goes with the dialog
        QVERIFY(*rig.destroyedCancels > before);
        QVERIFY(guard.isNull());
    }

    void dialogCancelAndAccept()
    {
        Rig rig;
        ModInstallDialog dialog(rig.environment(), makeItem(twoVersions()), makeTarget(), QString());
        dialog.accept();  // not settled: nothing happens
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        rig.resolver->answer({});
        dialog.primaryButton()->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));

        Rig rig2;
        ModInstallDialog second(rig2.environment(), makeItem(twoVersions()), makeTarget(), QString());
        second.cancelButton()->click();
        QCOMPARE(second.result(), int(QDialog::Rejected));
    }
};

QTEST_MAIN(ZeroModInstallTest)

#include "ZeroModInstall_test.moc"
