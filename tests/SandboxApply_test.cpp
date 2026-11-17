// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of "Try it in a copy", the part that moves files (zero/sandbox/SandboxApply + SandboxTasks): replacing the chosen parts of
// the original with the copy's, the staging folder, the restore after an injected failure, the Recycle Bin (a folder: an injectable function) and
// the worlds (replaced / added / kept, safety backups before anything changes). Real folders in a temp dir; no widgets, no network.
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QTemporaryDir>
#include <QTest>
#include <functional>

#include "zero/backup/WorldBackup.h"
#include "zero/backup/WorldBackupJobs.h"
#include "zero/sandbox/SandboxApply.h"
#include "zero/sandbox/SandboxPlan.h"
#include "zero/sandbox/SandboxTasks.h"

using namespace Zero::Sandbox;

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

/// Every file below `root` (relative path -> content).
QMap<QString, QByteArray> tree(const QString& root)
{
    QMap<QString, QByteArray> out;
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    const QDir dir(root);
    while (it.hasNext()) {
        const QString path = it.next();
        out.insert(dir.relativeFilePath(path), readAll(path));
    }
    return out;
}

struct Rig {
    QTemporaryDir root;
    QString orig = root.filePath(S("instances/orig"));
    QString copy = root.filePath(S("instances/copy"));
    QString bin = root.filePath(S("bin"));
    Layout original{ orig, orig + S("/minecraft") };
    Layout copyLayout{ copy, copy + S("/minecraft") };
    QStringList trashed;                 ///< what went to the "Recycle Bin", in order
    QStringList events;                  ///< backups and renames, in order
    bool failTrash = false;
    std::function<bool(const QString&, const QString&)> failRename;  ///< true = that rename fails
    std::function<bool(const QString&)> failCopy;                     ///< true = the copy of that source fails
    QStringList backedUp;                ///< world folders that got a safety backup
    QString backupError;                 ///< non-empty = the backup hook fails with it
    qint64 now = 1'700'000'000'000;
    int binCounter = 0;
    std::function<void()> atBackup;      ///< runs inside the backup hook (to look at the files at that moment)

    Hooks hooks()
    {
        Hooks h;
        h.trashPath = [this](const QString& path) {
            if (failTrash)
                return false;
            trashed.append(path);
            QDir().mkpath(bin);
            return QDir().rename(path, bin + QLatin1Char('/') + QString::number(++binCounter) + QLatin1Char('-') + QFileInfo(path).fileName());
        };
        h.rename = [this](const QString& from, const QString& to) {
            if (failRename && failRename(from, to))
                return false;
            events << S("rename ") + QFileInfo(from).fileName() + S(" -> ") + QFileInfo(to).fileName();
            return QDir().rename(from, to);
        };
        h.copyTree = [this](const QString& from, const QString& to) {
            if (failCopy && failCopy(from))
                return false;
            return copyTree(from, to);
        };
        h.backupWorld = [this](const QString& folder, const QString& path) -> QString {
            events << S("backup ") + folder;
            if (atBackup)
                atBackup();
            if (!backupError.isEmpty())
                return backupError;
            if (!QFileInfo(path).isDir())
                return S("the world is not where it should be");
            backedUp << folder;
            return {};
        };
        h.nowMs = [this]() { return now; };
        return h;
    }

    Request request(const QList<Part>& parts) const { return { original, copyLayout, parts }; }

    bool putOrig(const QString& rel, const QByteArray& data) { return writeFile(orig + QLatin1Char('/') + rel, data, kT1); }
    bool putCopy(const QString& rel, const QByteArray& data) { return writeFile(copy + QLatin1Char('/') + rel, data, kT2); }
    QByteArray o(const QString& rel) const { return readAll(orig + QLatin1Char('/') + rel); }
    bool oHas(const QString& rel) const { return QFileInfo::exists(orig + QLatin1Char('/') + rel); }
    QStringList stagingFolders() const { return QDir(orig).entryList({ S(".zero-*") }, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot); }
    QStringList binNames() const { return QDir(bin).entryList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name); }

    /// An original and a copy that differ in mods (changed + removed + added), config (added), shader packs (only in the copy) and the game version.
    Rig()
    {
        putOrig(S("instance.cfg"), "[General]\nname=Orig\n");
        putOrig(S("minecraft/mods/a.jar"), "a old");
        putOrig(S("minecraft/mods/gone.jar"), "gone");
        putOrig(S("minecraft/config/keep.toml"), "same");
        putOrig(S("minecraft/resourcepacks/rp.zip"), "rp");
        putOrig(S("mmc-pack.json"), "pack old");
        putOrig(S("patches/old.json"), "patch old");
        putOrig(S("minecraft/options.txt"), "original options");

        putCopy(S("instance.cfg"), "[General]\nname=Copy\nZeroSandboxOf=orig\n");
        putCopy(S("minecraft/mods/a.jar"), "a NEW");
        putCopy(S("minecraft/mods/new.jar"), "new");
        putCopy(S("minecraft/config/keep.toml"), "same");
        putCopy(S("minecraft/config/extra.toml"), "extra");
        writeFile(copy + S("/minecraft/config/keep.toml"), "same", kT1);  // identical to the original's
        putCopy(S("minecraft/resourcepacks/rp.zip"), "rp");
        writeFile(copy + S("/minecraft/resourcepacks/rp.zip"), "rp", kT1);
        putCopy(S("minecraft/shaderpacks/bsl.zip"), "bsl");
        putCopy(S("mmc-pack.json"), "pack NEW");
        putCopy(S("patches/new.json"), "patch new");
        putCopy(S("minecraft/options.txt"), "COPY options");  // never part of any part
    }
};

}  // namespace

class SandboxApplyTest : public QObject {
    Q_OBJECT
   private slots:
    // ---------------------------------------------------------------- the parts

    void replacesTheChosenPartsAndSendsTheOldFilesToTheRecycleBin()
    {
        Rig rig;
        const Result r = apply(rig.request({ Part::Mods, Part::GameVersion }), rig.hooks());
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.changedAnything);
        QCOMPARE(r.appliedParts, (QStringList{ "mods", "gameversion" }));
        // the copy's version is in place
        QCOMPARE(rig.o(S("minecraft/mods/a.jar")), QByteArray("a NEW"));
        QCOMPARE(rig.o(S("minecraft/mods/new.jar")), QByteArray("new"));
        QVERIFY(!rig.oHas(S("minecraft/mods/gone.jar")));
        QCOMPARE(rig.o(S("mmc-pack.json")), QByteArray("pack NEW"));
        QCOMPARE(rig.o(S("patches/new.json")), QByteArray("patch new"));
        QVERIFY(!rig.oHas(S("patches/old.json")));
        // parts that were not chosen are not touched, even where the copy differs
        QVERIFY(!rig.oHas(S("minecraft/config/extra.toml")));
        QVERIFY(!rig.oHas(S("minecraft/shaderpacks")));
        QCOMPARE(rig.o(S("minecraft/options.txt")), QByteArray("original options"));
        QCOMPARE(rig.o(S("instance.cfg")), QByteArray("[General]\nname=Orig\n"));  // the instance settings never move
        // the replaced originals went to the Recycle Bin (mods folder, mmc-pack.json, patches), and nothing was deleted for good
        QCOMPARE(rig.trashed.size(), 3);
        const QStringList bin = rig.binNames();
        QCOMPARE(bin.size(), 3);
        QVERIFY(tree(rig.bin).contains(S("1-mods/a.jar")));
        QCOMPARE(tree(rig.bin).value(S("1-mods/a.jar")), QByteArray("a old"));
        QCOMPARE(tree(rig.bin).value(S("1-mods/gone.jar")), QByteArray("gone"));
        QVERIFY(tree(rig.bin).contains(S("2-mmc-pack.json")));
        QVERIFY(tree(rig.bin).contains(S("3-patches/old.json")));
        QVERIFY(rig.stagingFolders().isEmpty());  // the staging folder is gone
        // the copy is never touched
        QCOMPARE(readAll(rig.copy + S("/minecraft/mods/a.jar")), QByteArray("a NEW"));
        QVERIFY(QFileInfo::exists(rig.copy + S("/minecraft/mods/new.jar")));
    }

    void unchosenAndIdenticalPartsAreLeftAlone()
    {
        Rig rig;
        // config differs (extra.toml) but resource packs do not: choosing both only moves config
        const Result r = apply(rig.request({ Part::ResourcePacks, Part::Config }), rig.hooks());
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.appliedParts, (QStringList{ "config" }));
        QCOMPARE(rig.trashed.size(), 1);  // only the old config folder
        QVERIFY(rig.trashed.first().contains(S("config")));
        QCOMPARE(rig.o(S("minecraft/config/extra.toml")), QByteArray("extra"));
        QCOMPARE(rig.o(S("minecraft/resourcepacks/rp.zip")), QByteArray("rp"));
    }

    void aPartThatOnlyTheCopyHasIsAddedWithoutTrashingAnything()
    {
        Rig rig;
        const Result r = apply(rig.request({ Part::ShaderPacks }), rig.hooks());
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(rig.o(S("minecraft/shaderpacks/bsl.zip")), QByteArray("bsl"));
        QVERIFY(rig.trashed.isEmpty());
    }

    void aPartTheCopyRemovedIsRemovedFromTheOriginalThroughTheRecycleBin()
    {
        Rig rig;
        QVERIFY(QDir(rig.copy + S("/minecraft/resourcepacks")).removeRecursively());
        const Result r = apply(rig.request({ Part::ResourcePacks }), rig.hooks());
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(!rig.oHas(S("minecraft/resourcepacks")));
        QCOMPARE(rig.trashed.size(), 1);
        QCOMPARE(tree(rig.bin).value(S("1-resourcepacks/rp.zip")), QByteArray("rp"));
    }

    void nothingChosenOrNothingDifferentChangesNothing()
    {
        Rig rig;
        const auto before = tree(rig.orig);
        Result r = apply(rig.request({}), rig.hooks());
        QVERIFY(r.ok);
        QVERIFY(!r.changedAnything);
        r = apply(rig.request({ Part::ResourcePacks }), rig.hooks());  // identical
        QVERIFY(r.ok);
        QVERIFY(!r.changedAnything);
        QCOMPARE(tree(rig.orig), before);
        QVERIFY(rig.trashed.isEmpty());
        QVERIFY(rig.stagingFolders().isEmpty());
    }

    void afterBringingEverythingBackTheTwoInstancesAreTheSame()
    {
        Rig rig;
        const Result r = apply(rig.request({ Part::Mods, Part::Config, Part::ResourcePacks, Part::ShaderPacks, Part::GameVersion }), rig.hooks());
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(differingParts(comparePlan(rig.original, rig.copyLayout)).isEmpty());
    }

    void refusesTheSameInstanceOnBothSides()
    {
        Rig rig;
        Request request{ rig.original, rig.original, { Part::Mods } };
        const Result r = apply(request, rig.hooks());
        QVERIFY(!r.ok);
        QVERIFY(r.restored);
        QVERIFY(rig.trashed.isEmpty());
    }

    // ---------------------------------------------------------------- failures: everything goes back

    void aFailedCopyChangesNothing()
    {
        Rig rig;
        const auto before = tree(rig.orig);
        rig.failCopy = [](const QString& from) { return from.endsWith(S("patches")); };  // the second thing to copy
        const Result r = apply(rig.request({ Part::Mods, Part::GameVersion }), rig.hooks());
        QVERIFY(!r.ok);
        QVERIFY(r.restored);
        QCOMPARE(tree(rig.orig), before);
        QVERIFY(rig.stagingFolders().isEmpty());  // ZERO's own copies are removed
        QVERIFY(rig.trashed.isEmpty());
        QVERIFY(r.error.contains(S("nothing was changed")));
    }

    void aFailedSwapPutsEveryTargetBack()
    {
        Rig rig;
        const auto before = tree(rig.orig);
        // mods and the old mmc-pack.json move fine; replacing patches fails
        rig.failRename = [](const QString& from, const QString&) { return QFileInfo(from).fileName() == S("patches"); };
        const Result r = apply(rig.request({ Part::Mods, Part::GameVersion }), rig.hooks());
        QVERIFY(!r.ok);
        QVERIFY(r.restored);
        QCOMPARE(tree(rig.orig), before);  // mods and mmc-pack.json were swapped before the failure, and are back
        QVERIFY(rig.stagingFolders().isEmpty());
        QVERIFY(rig.trashed.isEmpty());  // nothing reaches the Recycle Bin on a failure
        QVERIFY(r.error.contains(S("Your instance was not changed")));
    }

    void aFailedPlacementPutsTheReplacedOriginalBack()
    {
        Rig rig;
        const auto before = tree(rig.orig);
        // the original's mods folder moves aside, but the new one cannot be put in its place
        rig.failRename = [&](const QString& from, const QString& to) { return from.contains(S("/new/")) && QFileInfo(to).fileName() == S("mods"); };
        const Result r = apply(rig.request({ Part::Mods }), rig.hooks());
        QVERIFY(!r.ok);
        QVERIFY(r.restored);
        QCOMPARE(tree(rig.orig), before);
        QVERIFY(rig.stagingFolders().isEmpty());
    }

    void whenTheRestoreFailsToo_theStagingFolderIsKeptAndNamed()
    {
        Rig rig;
        int renames = 0;
        // after the first target is swapped, every rename fails: the swap of the next one fails and so does the way back
        rig.failRename = [&renames](const QString&, const QString&) { return ++renames > 4; };
        const Result r = apply(rig.request({ Part::Mods, Part::GameVersion }), rig.hooks());
        QVERIFY(!r.ok);
        QVERIFY(!r.restored);
        QVERIFY(r.error.contains(S(".zero-bringback-")));
        QCOMPARE(rig.stagingFolders().size(), 1);  // nothing was deleted: the user's files are in there
        QVERIFY(rig.trashed.isEmpty());
    }

    void aRecycleBinFailureIsReportedAndTheFilesAreKept()
    {
        Rig rig;
        rig.failTrash = true;
        const Result r = apply(rig.request({ Part::Mods }), rig.hooks());
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(rig.o(S("minecraft/mods/a.jar")), QByteArray("a NEW"));  // the change itself worked
        QCOMPARE(r.leftovers.size(), 1);
        QCOMPARE(readAll(r.leftovers.first() + S("/a.jar")), QByteArray("a old"));  // the old folder is still on disk, never deleted
        QCOMPARE(rig.stagingFolders().size(), 1);
    }

    // ---------------------------------------------------------------- worlds

    void worldsAreReplacedAddedAndKept()
    {
        Rig rig;
        rig.putOrig(S("minecraft/saves/Same/level.dat"), "same");
        rig.putOrig(S("minecraft/saves/Changed/level.dat"), "old world");
        rig.putOrig(S("minecraft/saves/OnlyOrig/level.dat"), "only in the original");
        writeFile(rig.copy + S("/minecraft/saves/Same/level.dat"), "same", kT1);
        rig.putCopy(S("minecraft/saves/Changed/level.dat"), "changed in the copy");
        rig.putCopy(S("minecraft/saves/Changed/region/r.0.0.mca"), "region");
        rig.putCopy(S("minecraft/saves/Added/level.dat"), "added");
        const Result r = apply(rig.request({ Part::Worlds }), rig.hooks());
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.replacedWorlds, (QStringList{ "Changed" }));
        QCOMPARE(r.addedWorlds, (QStringList{ "Added" }));
        QCOMPARE(rig.o(S("minecraft/saves/Changed/level.dat")), QByteArray("changed in the copy"));
        QCOMPARE(rig.o(S("minecraft/saves/Changed/region/r.0.0.mca")), QByteArray("region"));
        QCOMPARE(rig.o(S("minecraft/saves/Added/level.dat")), QByteArray("added"));
        QCOMPARE(rig.o(S("minecraft/saves/OnlyOrig/level.dat")), QByteArray("only in the original"));  // stays
        QCOMPARE(rig.o(S("minecraft/saves/Same/level.dat")), QByteArray("same"));
        // only the replaced world has a safety backup, and its old folder is in the Recycle Bin
        QCOMPARE(rig.backedUp, (QStringList{ "Changed" }));
        QCOMPARE(rig.trashed.size(), 1);
        QCOMPARE(tree(rig.bin).value(S("1-Changed/level.dat")), QByteArray("old world"));
        // the instance's other files did not move (worlds were the only part chosen)
        QCOMPARE(rig.o(S("minecraft/mods/a.jar")), QByteArray("a old"));
    }

    void safetyBackupsAreMadeBeforeAnythingIsReplaced()
    {
        Rig rig;
        rig.putOrig(S("minecraft/saves/W1/level.dat"), "w1 old");
        rig.putOrig(S("minecraft/saves/W2/level.dat"), "w2 old");
        rig.putCopy(S("minecraft/saves/W1/level.dat"), "w1 new");
        rig.putCopy(S("minecraft/saves/W2/level.dat"), "w2 new");
        QStringList seenAtBackup;
        rig.atBackup = [&]() {
            // at the moment of each backup nothing has been moved yet: the old worlds are in place and the staging folder does not exist
            seenAtBackup << QString::fromLatin1(rig.o(S("minecraft/saves/W1/level.dat"))) + S("|") + QString::fromLatin1(rig.o(S("minecraft/saves/W2/level.dat")));
            QVERIFY(rig.stagingFolders().isEmpty());
        };
        const Result r = apply(rig.request({ Part::Worlds, Part::Mods }), rig.hooks());
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(rig.backedUp, (QStringList{ "W1", "W2" }));
        QCOMPARE(seenAtBackup, (QStringList{ "w1 old|w2 old", "w1 old|w2 old" }));
        // the order of events: both backups come before the first rename
        QCOMPARE(rig.events.at(0), S("backup W1"));
        QCOMPARE(rig.events.at(1), S("backup W2"));
        QVERIFY(rig.events.at(2).startsWith(S("rename")));
    }

    void aFailedBackupStopsEverythingBeforeAnythingIsTouched()
    {
        Rig rig;
        rig.putOrig(S("minecraft/saves/W1/level.dat"), "w1 old");
        rig.putCopy(S("minecraft/saves/W1/level.dat"), "w1 new");
        const auto before = tree(rig.orig);
        rig.backupError = S("disk full");
        const Result r = apply(rig.request({ Part::Mods, Part::Worlds }), rig.hooks());
        QVERIFY(!r.ok);
        QVERIFY(r.restored);
        QVERIFY(r.error.contains(S("disk full")));
        QVERIFY(r.error.contains(S("W1")));
        QCOMPARE(tree(rig.orig), before);  // not even the mods
        QVERIFY(rig.stagingFolders().isEmpty());
        QVERIFY(rig.trashed.isEmpty());
        QVERIFY(rig.events.filter(S("rename")).isEmpty());
    }

    void replacingAWorldWithoutABackupHookIsRefused()
    {
        Rig rig;
        rig.putOrig(S("minecraft/saves/W1/level.dat"), "w1 old");
        rig.putCopy(S("minecraft/saves/W1/level.dat"), "w1 new");
        Hooks h = rig.hooks();
        h.backupWorld = nullptr;
        const auto before = tree(rig.orig);
        const Result r = apply(rig.request({ Part::Worlds }), h);
        QVERIFY(!r.ok);
        QCOMPARE(tree(rig.orig), before);
    }

    void aNewWorldNeedsNoBackup()
    {
        Rig rig;
        rig.putCopy(S("minecraft/saves/Brand New/level.dat"), "new");
        Hooks h = rig.hooks();
        h.backupWorld = nullptr;  // nothing is replaced: nothing to back up
        const Result r = apply(rig.request({ Part::Worlds }), h);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.addedWorlds, (QStringList{ "Brand New" }));
        QVERIFY(r.replacedWorlds.isEmpty());
        QVERIFY(rig.trashed.isEmpty());
    }

    void theRealSafetyBackupHookMakesAVerifiedBackup()
    {
        Rig rig;
        rig.putOrig(S("minecraft/saves/W1/level.dat"), "w1 old");
        rig.putOrig(S("minecraft/saves/W1/region/r.0.0.mca"), "region old");
        rig.putCopy(S("minecraft/saves/W1/level.dat"), "w1 new");
        const QString backups = rig.root.filePath(S("backups/orig/W1"));
        Hooks h = rig.hooks();
        h.backupWorld = safetyBackupHook(S("orig"), S("Orig"), [backups](const QString&) { return backups; });
        const Result r = apply(rig.request({ Part::Worlds }), h);
        QVERIFY2(r.ok, qPrintable(r.error));
        const QList<Zero::Backup::BackupInfo> list = Zero::Backup::listBackups(backups);
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.first().kind, Zero::Backup::Kind::Safety);
        QVERIFY(Zero::Backup::verifyBackup(list.first().zipPath).ok);
        QCOMPARE(rig.o(S("minecraft/saves/W1/level.dat")), QByteArray("w1 new"));
    }

    // ---------------------------------------------------------------- the task

    void theTaskAppliesTheRequestAndReleasesTheLocks()
    {
        Rig rig;
        BringBackTask task(S("orig"), S("copy"), rig.request({ Part::Mods }), rig.hooks());
        task.start();
        QTRY_VERIFY_WITH_TIMEOUT(task.isFinished(), 15000);
        QVERIFY2(task.wasSuccessful(), qPrintable(task.failReason()));
        QVERIFY(task.result().changedAnything);
        QCOMPARE(rig.o(S("minecraft/mods/a.jar")), QByteArray("a NEW"));
        QVERIFY(!Zero::Backup::WorldBackupJobs::isInstanceLocked(S("orig")));
        QVERIFY(!Zero::Backup::WorldBackupJobs::isInstanceLocked(S("copy")));
    }

    void theTaskRefusesWhileEitherInstanceIsBusy()
    {
        Rig rig;
        const auto before = tree(rig.orig);
        {
            BringBackTask task(S("orig"), S("copy"), rig.request({ Part::Mods }), rig.hooks(), []() { return S("Close the game first."); });
            task.start();
            QTRY_VERIFY_WITH_TIMEOUT(task.isFinished(), 5000);
            QVERIFY(!task.wasSuccessful());
            QCOMPARE(task.failReason(), S("Close the game first."));
        }
        Zero::Backup::WorldBackupJobs::lockInstance(S("copy"));  // a backup job holds the copy
        {
            BringBackTask task(S("orig"), S("copy"), rig.request({ Part::Mods }), rig.hooks());
            task.start();
            QTRY_VERIFY_WITH_TIMEOUT(task.isFinished(), 5000);
            QVERIFY(!task.wasSuccessful());
        }
        Zero::Backup::WorldBackupJobs::unlockInstance(S("copy"));
        QCOMPARE(tree(rig.orig), before);
        QVERIFY(!Zero::Backup::WorldBackupJobs::isInstanceLocked(S("orig")));
    }

    void theCompareTaskReturnsThePlan()
    {
        Rig rig;
        CompareTask task(rig.original, rig.copyLayout);
        task.start();
        QTRY_VERIFY_WITH_TIMEOUT(task.isFinished(), 15000);
        QVERIFY(task.wasSuccessful());
        QCOMPARE(differingParts(task.plan()), (QList<Part>{ Part::Mods, Part::Config, Part::ShaderPacks, Part::GameVersion }));
    }
};

QTEST_GUILESS_MAIN(SandboxApplyTest)
#include "SandboxApply_test.moc"
