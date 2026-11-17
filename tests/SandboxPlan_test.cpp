// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of "Try it in a copy", the part that decides what differs between a test copy and its original (zero/sandbox/SandboxPlan):
// the folder comparison (sizes, times, bytes, missing = empty, ignored names, links) and the plan by part (mods, config, resource packs, shader
// packs, game version, worlds). Real files in a temp dir; no widgets, no network.
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include "zero/sandbox/SandboxApply.h"
#include "zero/sandbox/SandboxPlan.h"

using namespace Zero::Sandbox;

namespace {

QString S(const char* text)
{
    return QString::fromUtf8(text);
}

bool writeFile(const QString& path, const QByteArray& data, const QDateTime& time = {})
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size())
        return false;
    if (time.isValid())
        f.setFileTime(time, QFileDevice::FileModificationTime);
    return true;
}

const QDateTime kT1 = QDateTime::fromSecsSinceEpoch(1'700'000'000);
const QDateTime kT2 = QDateTime::fromSecsSinceEpoch(1'700'009'000);

/// An original and a copy (`<root>/orig`, `<root>/copy`), each with `minecraft/` as its game folder.
struct Pair {
    QTemporaryDir root;
    Layout original{ root.filePath(S("orig")), root.filePath(S("orig/minecraft")) };
    Layout copy{ root.filePath(S("copy")), root.filePath(S("copy/minecraft")) };

    /// The same file in both instances, same bytes, same time.
    void both(const QString& rel, const QByteArray& data = "x")
    {
        QVERIFY(writeFile(original.instanceRoot + QLatin1Char('/') + rel, data, kT1));
        QVERIFY(writeFile(copy.instanceRoot + QLatin1Char('/') + rel, data, kT1));
    }
    void onlyOriginal(const QString& rel, const QByteArray& data = "x") { QVERIFY(writeFile(original.instanceRoot + QLatin1Char('/') + rel, data, kT1)); }
    void onlyCopy(const QString& rel, const QByteArray& data = "x") { QVERIFY(writeFile(copy.instanceRoot + QLatin1Char('/') + rel, data, kT1)); }

    QList<Part> differing() { return differingParts(comparePlan(original, copy)); }
};

}  // namespace

class SandboxPlanTest : public QObject {
    Q_OBJECT
   private slots:
    void partKeysAreStableAndWorldsComeLast()
    {
        QCOMPARE(partKey(Part::Mods), S("mods"));
        QCOMPARE(partKey(Part::Config), S("config"));
        QCOMPARE(partKey(Part::ResourcePacks), S("resourcepacks"));
        QCOMPARE(partKey(Part::ShaderPacks), S("shaderpacks"));
        QCOMPARE(partKey(Part::GameVersion), S("gameversion"));
        QCOMPARE(partKey(Part::Worlds), S("worlds"));
        QCOMPARE(allParts().size(), 6);
        QCOMPARE(allParts().last(), Part::Worlds);
    }

    // ---------------------------------------------------------------- treesDiffer

    void sameFolderIsNotDifferent()
    {
        Pair p;
        p.both(S("minecraft/mods/a.jar"), "aaa");
        p.both(S("minecraft/mods/sub/b.jar"), "bbb");
        QVERIFY(!treesDiffer(p.original.gameRoot + S("/mods"), p.copy.gameRoot + S("/mods")));
    }

    void differentSizeContentOrNamesDiffer()
    {
        Pair p;
        p.both(S("minecraft/mods/a.jar"), "aaa");
        QVERIFY(writeFile(p.copy.gameRoot + S("/mods/a.jar"), "aaaa", kT1));  // size
        QVERIFY(treesDiffer(p.original.gameRoot + S("/mods"), p.copy.gameRoot + S("/mods")));

        Pair q;
        q.both(S("minecraft/mods/a.jar"), "aaa");
        QVERIFY(writeFile(q.copy.gameRoot + S("/mods/a.jar"), "bbb", kT2));  // same size, other bytes, other time
        QVERIFY(treesDiffer(q.original.gameRoot + S("/mods"), q.copy.gameRoot + S("/mods")));

        Pair r;
        r.both(S("minecraft/mods/a.jar"), "aaa");
        r.onlyCopy(S("minecraft/mods/extra.jar"));
        QVERIFY(treesDiffer(r.original.gameRoot + S("/mods"), r.copy.gameRoot + S("/mods")));
        QVERIFY(treesDiffer(r.copy.gameRoot + S("/mods"), r.original.gameRoot + S("/mods")));  // and the other way round
    }

    void aDifferentTimeWithTheSameBytesIsTheSameFile()
    {
        Pair p;
        p.both(S("minecraft/mods/a.jar"), "aaa");
        QVERIFY(writeFile(p.copy.gameRoot + S("/mods/a.jar"), "aaa", kT2));
        QVERIFY(!treesDiffer(p.original.gameRoot + S("/mods"), p.copy.gameRoot + S("/mods")));
    }

    void timesWithinTwoSecondsAreTheSameMoment()
    {
        Pair p;
        QVERIFY(writeFile(p.original.gameRoot + S("/mods/a.jar"), "aaa", kT1));
        QVERIFY(writeFile(p.copy.gameRoot + S("/mods/a.jar"), "aaa", kT1.addMSecs(1500)));
        QVERIFY(!treesDiffer(p.original.gameRoot + S("/mods"), p.copy.gameRoot + S("/mods")));
    }

    void aMissingFolderEqualsAnEmptyOne()
    {
        Pair p;
        QVERIFY(!treesDiffer(p.original.gameRoot + S("/mods"), p.copy.gameRoot + S("/mods")));  // both missing
        QVERIFY(QDir().mkpath(p.copy.gameRoot + S("/mods")));
        QVERIFY(!treesDiffer(p.original.gameRoot + S("/mods"), p.copy.gameRoot + S("/mods")));  // missing against empty folders
        QVERIFY(!treesDiffer(p.copy.gameRoot + S("/mods"), p.original.gameRoot + S("/mods")));
        p.onlyCopy(S("minecraft/mods/a.jar"));
        QVERIFY(treesDiffer(p.original.gameRoot + S("/mods"), p.copy.gameRoot + S("/mods")));  // missing against a folder with a file
    }

    void aFileAgainstNothingOrAFolderDiffers()
    {
        Pair p;
        p.onlyCopy(S("mmc-pack.json"), "{}");
        QVERIFY(treesDiffer(p.original.instanceRoot + S("/mmc-pack.json"), p.copy.instanceRoot + S("/mmc-pack.json")));
        QVERIFY(QDir().mkpath(p.original.instanceRoot + S("/mmc-pack.json")));  // a folder where the other has a file
        QVERIFY(treesDiffer(p.original.instanceRoot + S("/mmc-pack.json"), p.copy.instanceRoot + S("/mmc-pack.json")));
    }

    void zeroStagingAndSessionLocksAreIgnored()
    {
        Pair p;
        p.both(S("minecraft/saves/W/level.dat"), "lvl");
        p.onlyCopy(S("minecraft/saves/W/session.lock"), "lock");
        p.onlyOriginal(S("minecraft/saves/.zero-bringback-1/new/0"), "staging");
        QVERIFY(!treesDiffer(p.original.gameRoot + S("/saves/W"), p.copy.gameRoot + S("/saves/W")));
        QVERIFY(!treesDiffer(p.original.gameRoot + S("/saves"), p.copy.gameRoot + S("/saves")));
    }

    void aCancelledComparisonCountsAsDifferent()
    {
        Pair p;
        p.both(S("minecraft/mods/a.jar"));
        p.both(S("minecraft/mods/b.jar"));
        CompareOptions options;
        options.cancelled = []() { return true; };
        QVERIFY(treesDiffer(p.original.gameRoot + S("/mods"), p.copy.gameRoot + S("/mods"), options));
        QVERIFY(comparePlan(p.original, p.copy, options).isEmpty());  // and the plan is empty: it must not be read as "nothing differs"
    }

    // ---------------------------------------------------------------- the plan

    void identicalInstancesHaveNothingToBringBack()
    {
        Pair p;
        p.both(S("minecraft/mods/a.jar"));
        p.both(S("minecraft/config/x.toml"));
        p.both(S("mmc-pack.json"), "{}");
        p.both(S("patches/net.minecraftforge.json"), "{}");
        p.both(S("minecraft/saves/W/level.dat"));
        const QList<PartChange> plan = comparePlan(p.original, p.copy);
        QCOMPARE(plan.size(), 6);
        QVERIFY(differingParts(plan).isEmpty());
    }

    void everyPartIsDetectedOnItsOwn()
    {
        {
            Pair p;
            p.both(S("minecraft/mods/a.jar"));
            p.onlyCopy(S("minecraft/mods/.index/b.pw.toml"));  // the index of mods counts as mods
            QCOMPARE(p.differing(), (QList<Part>{ Part::Mods }));
        }
        {
            Pair p;
            p.onlyCopy(S("minecraft/config/sodium.json"));
            QCOMPARE(p.differing(), (QList<Part>{ Part::Config }));
        }
        {
            Pair p;
            p.onlyCopy(S("minecraft/resourcepacks/pack.zip"));
            QCOMPARE(p.differing(), (QList<Part>{ Part::ResourcePacks }));
        }
        {
            Pair p;
            p.onlyOriginal(S("minecraft/shaderpacks/bsl.zip"));  // removed in the copy
            QCOMPARE(p.differing(), (QList<Part>{ Part::ShaderPacks }));
        }
        {
            Pair p;
            p.both(S("mmc-pack.json"), "{\"a\":1}");
            QVERIFY(writeFile(p.copy.instanceRoot + S("/mmc-pack.json"), "{\"a\":2}", kT2));
            QCOMPARE(p.differing(), (QList<Part>{ Part::GameVersion }));
        }
        {
            Pair p;
            p.both(S("mmc-pack.json"), "{}");
            p.onlyCopy(S("patches/net.fabricmc.fabric-loader.json"), "{}");
            QCOMPARE(p.differing(), (QList<Part>{ Part::GameVersion }));
        }
    }

    void worldsAreListedByFolderAndOnlyWhenNewOrChanged()
    {
        Pair p;
        p.both(S("minecraft/saves/Same/level.dat"), "same");
        p.both(S("minecraft/saves/Changed/level.dat"), "old");
        QVERIFY(writeFile(p.copy.gameRoot + S("/saves/Changed/level.dat"), "newer", kT2));
        p.onlyCopy(S("minecraft/saves/Added/level.dat"), "added");
        p.onlyOriginal(S("minecraft/saves/OnlyOrig/level.dat"), "stays");  // only in the original: not part of the plan
        p.onlyCopy(S("minecraft/saves/not-a-world/readme.txt"), "no level.dat");
        p.onlyCopy(S("minecraft/saves/.zero-restore-x/level.dat"), "ours");
        QCOMPARE(worldFolders(p.copy.savesDir()), (QStringList{ "Added", "Changed", "Same" }));
        QCOMPARE(changedWorlds(p.original, p.copy), (QStringList{ "Added", "Changed" }));
        const QList<PartChange> plan = comparePlan(p.original, p.copy);
        QCOMPARE(differingParts(plan), (QList<Part>{ Part::Worlds }));
        QCOMPARE(plan.last().worlds, (QStringList{ "Added", "Changed" }));
        QVERIFY(plan.first().worlds.isEmpty());
    }

    void aWorldThatOnlyDiffersInItsSessionLockIsUnchanged()
    {
        Pair p;
        p.both(S("minecraft/saves/W/level.dat"), "lvl");
        p.onlyOriginal(S("minecraft/saves/W/session.lock"), "a");
        QVERIFY(changedWorlds(p.original, p.copy).isEmpty());
    }

    void aCopyMadeByCopyTreeHasNothingToBringBack()
    {
        // the real copy keeps modification times, so a fresh test copy compares equal without reading any bytes
        QTemporaryDir root;
        const QString src = root.filePath(S("src"));
        QVERIFY(writeFile(src + S("/mods/a.jar"), "aaaa", kT1));
        QVERIFY(writeFile(src + S("/mods/sub/b.jar"), "bbbbbbbb", kT2));
        QVERIFY(QDir().mkpath(src + S("/mods/emptydir")));
        QVERIFY(copyTree(src + S("/mods"), root.filePath(S("dst/mods"))));
        QVERIFY(QFileInfo(root.filePath(S("dst/mods/emptydir"))).isDir());
        QVERIFY(!treesDiffer(src + S("/mods"), root.filePath(S("dst/mods"))));
        QCOMPARE(QFileInfo(root.filePath(S("dst/mods/sub/b.jar"))).lastModified().toSecsSinceEpoch(), kT2.toSecsSinceEpoch());
    }

    void targetsFollowTheGameFolderOfEachInstance()
    {
        Layout a{ S("/inst/a"), S("/inst/a/.minecraft") };
        Layout b{ S("/inst/b"), S("/inst/b/minecraft") };
        const QList<Target> mods = targetsOf(Part::Mods, a, b);
        QCOMPARE(mods.size(), 1);
        QCOMPARE(mods.first().original, S("/inst/a/.minecraft/mods"));
        QCOMPARE(mods.first().copy, S("/inst/b/minecraft/mods"));
        const QList<Target> version = targetsOf(Part::GameVersion, a, b);
        QCOMPARE(version.size(), 2);
        QCOMPARE(version.at(0).original, S("/inst/a/mmc-pack.json"));
        QCOMPARE(version.at(1).copy, S("/inst/b/patches"));
        QVERIFY(targetsOf(Part::Worlds, a, b).isEmpty());
    }
};

QTEST_GUILESS_MAIN(SandboxPlanTest)
#include "SandboxPlan_test.moc"
