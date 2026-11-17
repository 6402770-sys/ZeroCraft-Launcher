// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the pure part of smart drag and drop (zero/content/DropPlan): kind mapping, what a jar says about itself, the
// captions of a mod that does not fit the instance (loader, Minecraft version, unknown = silent), the replace caption, the data pack, the
// texts, the page the toast opens and the default target. Synthetic files in a temp folder only.
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "archive/ArchiveWriter.h"
#include "zero/content/DropPlan.h"

using namespace Zero::Content;
using namespace Zero::Content::Drop;

namespace {

QString S(const char* text)
{
    return QString::fromUtf8(text);
}

TargetInstance instance(const QString& id, const QString& name, const QString& mc, ModPlatform::ModLoaderTypes loaders)
{
    TargetInstance t;
    t.id = id;
    t.name = name;
    t.mcVersion = mc;
    t.loaders = loaders;
    t.loaderText = loaderDisplayNames(loaders).join(QStringLiteral(", "));
    return t;
}

DropFile modFile(const QString& name, ModPlatform::ModLoaderTypes loaders, const QString& mcPredicate = {}, bool maven = false)
{
    DropFile file;
    file.fileName = name + S(".jar");
    file.name = name;
    file.kind = Kind::Mod;
    file.mod.displayName = name;
    file.mod.loaders = loaders;
    if (!mcPredicate.isEmpty()) {
        file.mod.minecraft.push_back({ mcPredicate, maven });
    }
    return file;
}

bool writeFabricJar(const QString& path, const QString& json)
{
    MMCZip::ArchiveWriter writer(path);
    if (!writer.open()) {
        return false;
    }
    if (!writer.addFile(QStringLiteral("fabric.mod.json"), json.toUtf8())) {
        return false;
    }
    return bool(writer.close());
}

}  // namespace

class DropPlanTest : public QObject {
    Q_OBJECT

   private slots:
    void kindMapping()
    {
        Kind kind = Kind::Mod;
        QVERIFY(kindFromResource(ModPlatform::ResourceType::Mod, &kind));
        QCOMPARE(kind, Kind::Mod);
        QVERIFY(kindFromResource(ModPlatform::ResourceType::ResourcePack, &kind));
        QCOMPARE(kind, Kind::ResourcePack);
        QVERIFY(kindFromResource(ModPlatform::ResourceType::ShaderPack, &kind));
        QCOMPARE(kind, Kind::ShaderPack);
        QVERIFY(kindFromResource(ModPlatform::ResourceType::World, &kind));
        QCOMPARE(kind, Kind::World);
        QVERIFY(kindFromResource(ModPlatform::ResourceType::DataPack, &kind));
        QCOMPARE(kind, Kind::DataPack);
        QVERIFY(kindFromResource(ModPlatform::ResourceType::TexturePack, &kind));
        QCOMPARE(kind, Kind::TexturePack);
        QVERIFY(!kindFromResource(ModPlatform::ResourceType::Modpack, &kind));
        QVERIFY(!kindFromResource(ModPlatform::ResourceType::Unknown, &kind));
        QVERIFY(!kindFromResource(ModPlatform::ResourceType::Screenshots, nullptr));

        QCOMPARE(kindLabel(Kind::Mod), S("Mod"));
        QCOMPARE(kindLabel(Kind::ResourcePack), S("Resource pack"));
        QCOMPARE(kindLabel(Kind::ShaderPack), S("Shader pack"));
        QCOMPARE(kindLabel(Kind::World), S("World"));
        QCOMPARE(kindLabel(Kind::DataPack), S("Data pack"));
        QCOMPARE(kindLabel(Kind::TexturePack), S("Texture pack"));
        QCOMPARE(pageId(Kind::Mod), S("mods"));
        QCOMPARE(pageId(Kind::ResourcePack), S("resourcepacks"));
        QCOMPARE(pageId(Kind::ShaderPack), S("shaderpacks"));
        QCOMPARE(pageId(Kind::World), S("worlds"));
        QVERIFY(pageId(Kind::DataPack).isEmpty());
    }

    void dataPackIsNotInstallable()
    {
        QVERIFY(!installable(Kind::DataPack));
        QVERIFY(installable(Kind::Mod));
        QVERIFY(installable(Kind::World));
        DropFile pack;
        pack.kind = Kind::DataPack;
        QCOMPARE(notInstallableCaption(pack), S("Data packs go inside a world. Open the world's folder to add it."));
        QVERIFY(notInstallableCaption(modFile(S("a"), ModPlatform::Fabric)).isEmpty());
    }

    void inspectJarReadsTheMetadata()
    {
        QTemporaryDir tmp;
        const QString jar = tmp.filePath(S("sodium-fabric-0.5.jar"));
        QVERIFY(writeFabricJar(jar, S("{\"schemaVersion\":1,\"id\":\"sodium\",\"name\":\"Sodium\",\"version\":\"0.5\",\"depends\":{\"minecraft\":\"~1.20.1\"}}")));
        const DropFile file = inspectFile(jar, Kind::Mod);
        QCOMPARE(file.name, S("Sodium"));
        QCOMPARE(file.fileName, S("sodium-fabric-0.5.jar"));
        QVERIFY(file.size > 0);
        QVERIFY(file.mod.loaders.testFlag(ModPlatform::Fabric));
        QVERIFY(!file.mod.loaders.testFlag(ModPlatform::Forge));
        QCOMPARE(file.mod.minecraft.size(), 1);
        QCOMPARE(file.mod.minecraft.first().predicate, S("~1.20.1"));
        QVERIFY(!file.mod.minecraft.first().maven);

        // a jar without metadata: the file name, nothing to compare
        const QString plain = tmp.filePath(S("plain.jar"));
        QFile f(plain);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not a zip");
        f.close();
        const DropFile unknown = inspectFile(plain, Kind::Mod);
        QCOMPARE(unknown.name, S("plain.jar"));
        QVERIFY(unknown.mod.loaders.toInt() == 0);
        QVERIFY(fitCaption(unknown, instance("a", "A", "1.20.1", ModPlatform::Fabric)).isEmpty());
    }

    void loaderMismatchCaption()
    {
        const auto fabric = instance("a", "Fabric Fun", "1.20.1", ModPlatform::Fabric);
        QCOMPARE(fitCaption(modFile(S("forgemod"), ModPlatform::Forge), fabric), S("Made for Forge — this instance uses Fabric"));
        QCOMPARE(fitCaption(modFile(S("neo"), ModPlatform::NeoForge), fabric), S("Made for NeoForge — this instance uses Fabric"));
        QVERIFY(fitCaption(modFile(S("fabricmod"), ModPlatform::Fabric), fabric).isEmpty());
        // a mod for two loaders fits either
        QVERIFY(fitCaption(modFile(S("both"), ModPlatform::ModLoaderTypes(ModPlatform::Fabric) | ModPlatform::Forge), fabric).isEmpty());
        // a Fabric mod also runs on Quilt; a Quilt mod does not run on Fabric
        const auto quilt = instance("q", "Quilt", "1.20.1", ModPlatform::Quilt);
        QVERIFY(fitCaption(modFile(S("fabricmod"), ModPlatform::Fabric), quilt).isEmpty());
        QCOMPARE(fitCaption(modFile(S("quiltmod"), ModPlatform::Quilt), fabric), S("Made for Quilt — this instance uses Fabric"));
    }

    void versionMismatchCaption()
    {
        const auto fabric = instance("a", "Fabric Fun", "1.21.1", ModPlatform::Fabric);
        const QString caption = fitCaption(modFile(S("old"), ModPlatform::Fabric, S("1.20.1")), fabric);
        QVERIFY2(caption.startsWith(S("Made for Minecraft ")), qPrintable(caption));
        QVERIFY2(caption.contains(S("1.20.1")), qPrintable(caption));
        QVERIFY2(caption.endsWith(S("— this instance is 1.21.1")), qPrintable(caption));
        // fits
        QVERIFY(fitCaption(modFile(S("ok"), ModPlatform::Fabric, S("1.21.1")), fabric).isEmpty());
        QVERIFY(fitCaption(modFile(S("ok"), ModPlatform::Fabric, S(">=1.20")), fabric).isEmpty());
        QVERIFY(fitCaption(modFile(S("any"), ModPlatform::Fabric, S("*")), fabric).isEmpty());
        // Forge range
        const auto forge = instance("f", "F", "1.20.1", ModPlatform::Forge);
        QVERIFY(!fitCaption(modFile(S("forge"), ModPlatform::Forge, S("[1.19,1.20)"), true), forge).isEmpty());
        QVERIFY(fitCaption(modFile(S("forge"), ModPlatform::Forge, S("[1.20,1.21)"), true), forge).isEmpty());
        // unknown: no requirement, no instance version, or a predicate nobody understands
        QVERIFY(fitCaption(modFile(S("none"), ModPlatform::Fabric), fabric).isEmpty());
        QVERIFY(fitCaption(modFile(S("odd"), ModPlatform::Fabric, S("${minecraft_version}")), fabric).isEmpty());
        QVERIFY(fitCaption(modFile(S("old"), ModPlatform::Fabric, S("1.20.1")), instance("n", "N", "", ModPlatform::Fabric)).isEmpty());
    }

    void otherKindsNeverWarn()
    {
        DropFile pack;
        pack.kind = Kind::ResourcePack;
        pack.fileName = S("pack.zip");
        QVERIFY(fitCaption(pack, instance("a", "A", "1.20.1", ModPlatform::Fabric)).isEmpty());
    }

    void replaceCaptionTest()
    {
        QTemporaryDir tmp;
        QDir dir(tmp.path());
        QFile f(dir.filePath(S("sodium.jar")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.close();
        QCOMPARE(replaceCaption(modFile(S("sodium"), ModPlatform::Fabric), tmp.path()), S("Replaces the file with the same name"));
        QVERIFY(replaceCaption(modFile(S("lithium"), ModPlatform::Fabric), tmp.path()).isEmpty());
        QVERIFY(replaceCaption(modFile(S("sodium"), ModPlatform::Fabric), QString()).isEmpty());
        DropFile world = modFile(S("sodium"), ModPlatform::Fabric);
        world.kind = Kind::World;
        QVERIFY(replaceCaption(world, tmp.path()).isEmpty());
    }

    void texts()
    {
        QCOMPARE(titleText(1), S("Add 1 file"));
        QCOMPARE(titleText(3), S("Add 3 files"));
        QCOMPARE(summaryText(1), S("Adds 1 file"));
        QCOMPARE(summaryText(3), S("Adds 3 files"));
        QCOMPARE(summaryText(0), S("Nothing to add"));
        QCOMPARE(primaryText(S("Fabric Fun")), S("Add to Fabric Fun"));
        QCOMPARE(addedToastText(3, S("Fabric Fun")), S("Added 3 files to Fabric Fun"));
        QCOMPARE(addedToastText(1, S("Fabric Fun")), S("Added 1 file to Fabric Fun"));
        QCOMPARE(failedToastText(S("sodium.jar")), S("Could not add sodium.jar"));
    }

    void openPageIsModsWhenAnyModWasAdded()
    {
        DropFile pack;
        pack.kind = Kind::ResourcePack;
        DropFile shader;
        shader.kind = Kind::ShaderPack;
        QCOMPARE(openPage({ pack, modFile(S("m"), ModPlatform::Fabric) }), S("mods"));
        QCOMPARE(openPage({ pack, shader }), S("resourcepacks"));
        QCOMPARE(openPage({ shader }), S("shaderpacks"));
        QVERIFY(openPage({}).isEmpty());
    }

    void defaultTargetFollowsTheLibraryThenTheSavedOne()
    {
        const QList<TargetInstance> targets = { instance("vanilla", "Vanilla", "1.21.1", {}),
                                                instance("fab", "Fabric Fun", "1.21.1", ModPlatform::Fabric),
                                                instance("forge", "Forge Fun", "1.20.1", ModPlatform::Forge) };
        // mods: the library selection when it can take mods
        QCOMPARE(chooseTarget(targets, S("forge"), S("fab"), true), S("forge"));
        // the selection is vanilla: the saved drop target
        QCOMPARE(chooseTarget(targets, S("vanilla"), S("fab"), true), S("fab"));
        // packs need only a Minecraft version, so vanilla is fine
        QCOMPARE(chooseTarget(targets, S("vanilla"), S("fab"), false), S("vanilla"));
        QVERIFY(chooseTarget({ instance("vanilla", "Vanilla", "1.21.1", {}) }, QString(), QString(), true).isEmpty());
    }
};

QTEST_MAIN(DropPlanTest)
#include "DropPlan_test.moc"
