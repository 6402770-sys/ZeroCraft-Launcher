// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of "Try it in a copy", the small pieces around the file moves: the `ZeroSandboxOf` link (a round trip through
// instance.cfg), the settings of the copy (InstanceCopyPrefs: no links, no screenshots, no play time, worlds optional), and the safety copy of
// the original that "Bring changes back" takes with PackSnapshot (kind and label in its meta, newestActive for an instance that is not a
// modpack, the roll back that undoes it). Temp folders only.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

#include "NullInstance.h"
#include "settings/INISettingsObject.h"
#include "settings/SettingsObject.h"
#include "zero/backup/PackSnapshot.h"
#include "zero/profiles/InstanceProfile.h"
#include "zero/sandbox/Sandbox.h"

using namespace Zero;

namespace {

QString S(const char* text)
{
    return QString::fromUtf8(text);
}

QByteArray readAll(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

/// A bare instance in `<root>/<id>` with the global settings BaseInstance overrides (the fixture of InstanceProfile_test).
struct Fixture {
    explicit Fixture(const QString& root, const QString& id = QStringLiteral("inst"))
    {
        global = std::make_unique<INISettingsObject>(root + QStringLiteral("/global.cfg"));
        for (const char* name : { "ShowGameTime", "RecordGameTime", "ShowConsole", "AutoCloseConsole", "ShowConsoleOnError", "LogPrePostOutput",
                                  "ConsoleOverflowStop" })
            global->registerSetting(name, false);
        for (const char* name : { "PreLoadCommand", "PreLaunchCommand", "WrapperCommand", "PostExitCommand" })
            global->registerSetting(name, QString());
        global->registerSetting("ConsoleMaxLines", 100000);

        dir = root + QLatin1Char('/') + id;
        QDir().mkpath(dir);
        QFile cfg(dir + QStringLiteral("/instance.cfg"));
        if (!cfg.exists() && cfg.open(QIODevice::WriteOnly))
            cfg.write("[General]\nname=Test\n");
        cfg.close();
        instance = std::make_unique<NullInstance>(global.get(), std::make_unique<INISettingsObject>(dir + QStringLiteral("/instance.cfg")), dir);
    }
    std::unique_ptr<SettingsObject> global;
    std::unique_ptr<NullInstance> instance;
    QString dir;
};

}  // namespace

class SandboxMetaTest : public QObject {
    Q_OBJECT
   private slots:
    // ---------------------------------------------------------------- ZeroSandboxOf

    void theLinkRoundTripsThroughInstanceCfg()
    {
        QTemporaryDir root;
        {
            Fixture f(root.path());
            QVERIFY(!Sandbox::isTestCopy(f.instance.get()));
            QCOMPARE(Sandbox::sandboxOf(f.instance.get()), QString());
            Sandbox::setSandboxOf(f.instance.get(), S("my-original"));
            QCOMPARE(Sandbox::sandboxOf(f.instance.get()), S("my-original"));
            QVERIFY(Sandbox::isTestCopy(f.instance.get()));
        }
        QVERIFY(readAll(root.filePath(S("inst/instance.cfg"))).contains("ZeroSandboxOf=my-original"));
        {
            Fixture again(root.path());  // read back from the file
            QCOMPARE(Sandbox::sandboxOf(again.instance.get()), S("my-original"));
            Sandbox::setSandboxOf(again.instance.get(), QString());  // "Keep as a normal instance"
            QVERIFY(!Sandbox::isTestCopy(again.instance.get()));
        }
        {
            Fixture third(root.path());
            QVERIFY(!Sandbox::isTestCopy(third.instance.get()));
        }
    }

    void settingTheLinkAnnouncesTheChangeOnlyWhenItChanged()
    {
        QTemporaryDir root;
        Fixture f(root.path());
        QSignalSpy changed(f.instance.get(), &BaseInstance::propertiesChanged);
        QSignalSpy profile(Profile::Notifier::instance(), &Profile::Notifier::profileChanged);
        Sandbox::setSandboxOf(f.instance.get(), S("a"));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(profile.count(), 1);
        Sandbox::setSandboxOf(f.instance.get(), S("a"));  // the same value
        QCOMPARE(changed.count(), 1);
        Sandbox::setSandboxOf(f.instance.get(), QString());
        QCOMPARE(changed.count(), 2);
        Sandbox::setSandboxOf(nullptr, S("x"));  // no instance: nothing happens
    }

    void aTestCopyIsNamedAfterItsOriginal()
    {
        QCOMPARE(Sandbox::testCopyName(S("Survival 1.20")), S("Survival 1.20 (test copy)"));
    }

    // ---------------------------------------------------------------- the settings of the copy

    void theCopyIsIndependentAndLeavesOutScreenshotsAndPlaytime()
    {
        const InstanceCopyPrefs withWorlds = Sandbox::copyPrefs(true);
        QVERIFY(withWorlds.isCopySavesEnabled());
        QVERIFY(withWorlds.isCopyModsEnabled());
        QVERIFY(withWorlds.isCopyGameOptionsEnabled());
        QVERIFY(withWorlds.isCopyResourcePacksEnabled());
        QVERIFY(withWorlds.isCopyShaderPacksEnabled());
        QVERIFY(withWorlds.isCopyServersEnabled());
        QVERIFY(!withWorlds.isCopyScreenshotsEnabled());
        QVERIFY(!withWorlds.isKeepPlaytimeEnabled());
        QVERIFY(!withWorlds.isUseSymLinksEnabled());
        QVERIFY(!withWorlds.isLinkRecursivelyEnabled());
        QVERIFY(!withWorlds.isUseHardLinksEnabled());
        QVERIFY(!withWorlds.isUseCloneEnabled());
        const QString filter = withWorlds.getSelectedFiltersAsRegex();
        QVERIFY(filter.contains(S("screenshots")));
        QVERIFY(!filter.contains(S("saves")));
        QVERIFY(!filter.contains(S("mods")));

        const InstanceCopyPrefs noWorlds = Sandbox::copyPrefs(false);
        QVERIFY(!noWorlds.isCopySavesEnabled());
        QVERIFY(noWorlds.getSelectedFiltersAsRegex().contains(S("saves")));
        QVERIFY(noWorlds.isCopyModsEnabled());
        QVERIFY(!noWorlds.isUseSymLinksEnabled());
    }

    // ---------------------------------------------------------------- the safety copy of the original

    void aBringBackSnapshotKeepsItsKindAndLabel()
    {
        QTemporaryDir root;
        const QString inst = root.filePath(S("instances/orig"));
        const QString snaps = root.filePath(S("data/zero/snapshots/orig"));
        QVERIFY(writeFile(inst + S("/instance.cfg"), "[General]\nname=Orig\n"));
        QVERIFY(writeFile(inst + S("/minecraft/mods/a.jar"), "a"));
        QVERIFY(writeFile(inst + S("/minecraft/saves/W/level.dat"), "world"));

        Backup::Pack::Meta meta;
        meta.instanceId = S("orig");
        meta.instanceName = S("Orig");
        meta.packName = S("Orig");
        meta.kind = QString::fromLatin1(Backup::Pack::kKindBringBack);
        meta.label = S("Before bringing back changes from Orig (test copy)");
        meta.gameDir = S("minecraft");
        const Backup::Pack::CreateResult made = Backup::Pack::createSnapshot(inst, snaps, meta);
        QVERIFY2(made.ok, qPrintable(made.error));

        const QList<Backup::Pack::Info> list = Backup::Pack::listSnapshots(snaps);
        QCOMPARE(list.size(), 1);
        QVERIFY(list.first().valid);
        QCOMPARE(list.first().meta.kind, S("bring-back"));
        QCOMPARE(list.first().meta.label, S("Before bringing back changes from Orig (test copy)"));
        QVERIFY(list.first().meta.packId.isEmpty());

        // an old snapshot (a modpack update) has no kind: it reads as an update
        Backup::Pack::Meta update;
        update.instanceId = S("orig");
        update.instanceName = S("Orig");
        update.packId = S("PACK");
        update.gameDir = S("minecraft");
        QVERIFY(Backup::Pack::createSnapshot(inst, snaps, update).ok);
        const QList<Backup::Pack::Info> both = Backup::Pack::listSnapshots(snaps);
        QCOMPARE(both.size(), 2);
        QVERIFY(both.first().meta.kind.isEmpty());  // the newest: the update
        QCOMPARE(both.last().meta.kind, S("bring-back"));
    }

    void newestActiveWorksForAnInstanceThatIsNotAModpack()
    {
        Backup::Pack::Info plain;
        plain.valid = true;
        plain.zipPath = S("a.zip");
        plain.meta.instanceId = S("orig");
        plain.meta.kind = QString::fromLatin1(Backup::Pack::kKindBringBack);  // packId empty: any pack fits
        Backup::Pack::Info rolled = plain;
        rolled.zipPath = S("b.zip");
        rolled.rolledBack = true;
        Backup::Pack::Info other = plain;
        other.zipPath = S("c.zip");
        other.meta.packId = S("PACK-X");
        Backup::Pack::Info invalid = plain;
        invalid.valid = false;

        // an empty packId (an instance that is not a modpack) takes the newest one that was not rolled back
        QCOMPARE(Backup::Pack::newestActive({ rolled, plain }, QString()).zipPath, S("a.zip"));
        QCOMPARE(Backup::Pack::newestActive({ other, plain }, QString()).zipPath, S("c.zip"));
        // a snapshot without a pack fits a modpack too (the original may be a pack when the copy is brought back)
        QCOMPARE(Backup::Pack::newestActive({ plain }, S("PACK-Y")).zipPath, S("a.zip"));
        // a snapshot of another pack does not
        QVERIFY(!Backup::Pack::newestActive({ other }, S("PACK-Y")).valid);
        QVERIFY(!Backup::Pack::newestActive({ rolled, invalid }, QString()).valid);
        QVERIFY(!Backup::Pack::newestActive({}, QString()).valid);
    }

    void undoingABringBackRestoresTheOriginalAndKeepsTheWorlds()
    {
        QTemporaryDir root;
        const QString inst = root.filePath(S("instances/orig"));
        const QString snaps = root.filePath(S("data/zero/snapshots/orig"));
        const QString bin = root.filePath(S("bin"));
        QVERIFY(writeFile(inst + S("/instance.cfg"), "[General]\nname=Orig\n"));
        QVERIFY(writeFile(inst + S("/mmc-pack.json"), "pack old"));
        QVERIFY(writeFile(inst + S("/minecraft/mods/a.jar"), "a old"));
        QVERIFY(writeFile(inst + S("/minecraft/saves/W/level.dat"), "world as it is now"));

        Backup::Pack::Meta meta;
        meta.instanceId = S("orig");
        meta.instanceName = S("Orig");
        meta.kind = QString::fromLatin1(Backup::Pack::kKindBringBack);
        meta.label = S("Before bringing back changes from Orig (test copy)");
        meta.gameDir = S("minecraft");
        const Backup::Pack::CreateResult made = Backup::Pack::createSnapshot(inst, snaps, meta);
        QVERIFY2(made.ok, qPrintable(made.error));

        // what bringing the changes back did (and a world that was replaced)
        QVERIFY(QFile::remove(inst + S("/minecraft/mods/a.jar")));
        QVERIFY(writeFile(inst + S("/minecraft/mods/b.jar"), "b new"));
        QVERIFY(writeFile(inst + S("/mmc-pack.json"), "pack new"));
        QVERIFY(writeFile(inst + S("/minecraft/saves/W/level.dat"), "world from the copy"));

        Backup::Pack::Hooks hooks;
        hooks.trashPath = [&bin](const QString& path) {
            QDir().mkpath(bin);
            return QDir().rename(path, bin + QLatin1Char('/') + QFileInfo(path).fileName());
        };
        const QString packId;  // not a modpack
        const Backup::Pack::Info snapshot = Backup::Pack::newestActive(Backup::Pack::listSnapshots(snaps), packId);
        QVERIFY(snapshot.valid);
        const Backup::Pack::RollbackResult undone = Backup::Pack::rollback(inst, snapshot.zipPath, snaps, packId, hooks);
        QVERIFY2(undone.ok, qPrintable(undone.error));
        QCOMPARE(readAll(inst + S("/minecraft/mods/a.jar")), QByteArray("a old"));
        QVERIFY(!QFileInfo::exists(inst + S("/minecraft/mods/b.jar")));
        QCOMPARE(readAll(inst + S("/mmc-pack.json")), QByteArray("pack old"));
        // the player's worlds stay as they are now: the old ones are in the world backups, not here
        QCOMPARE(readAll(inst + S("/minecraft/saves/W/level.dat")), QByteArray("world from the copy"));
        // used once
        QVERIFY(!Backup::Pack::newestActive(Backup::Pack::listSnapshots(snaps), packId).valid);
    }
};

QTEST_GUILESS_MAIN(SandboxMetaTest)
#include "SandboxMeta_test.moc"
