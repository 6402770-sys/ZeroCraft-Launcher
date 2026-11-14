// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the safety net of modpack updates (zero/backup/PackSnapshot + PackSnapshotTasks): the copy of the instance taken
// before an update and the roll back. Real folders in a temp dir; the Recycle Bin is a folder (an injectable trash function), so every
// "went to the Recycle Bin" is a folder that can be looked at. No widgets, no network.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QProcess>
#include <atomic>
#include <stdexcept>
#include <QTest>

#include "zero/backup/PackSnapshot.h"
#include "zero/backup/PackSnapshotTasks.h"
#include "zero/backup/WorldBackup.h"
#include "zero/backup/WorldBackupJobs.h"

using namespace Zero::Backup;
using namespace Zero::Backup::Pack;

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

const char kOldCfg[] =
    "[General]\n"
    "InstanceType=OneSix\n"
    "ManagedPack=true\n"
    "ManagedPackType=modrinth\n"
    "ManagedPackID=PACKID\n"
    "ManagedPackName=Sodium Plus\n"
    "ManagedPackVersionID=v1\n"
    "ManagedPackVersionName=2.3.7\n"
    "name=Inst\n"
    "totalTimePlayed=100\n"
    "JavaPath=C:/java/old\n";

struct Rig {
    QTemporaryDir root;
    QString instances = root.filePath(S("instances"));
    QString inst = instances + S("/inst");
    QString snaps = root.filePath(S("data/zero/snapshots/inst"));
    QString bin = root.filePath(S("bin"));
    QStringList trashed;                            ///< absolute paths handed to the Recycle Bin, in order
    bool failTrash = false;
    bool crashOnTrash = false;
    QSet<QString> linkNames;  ///< folder names the roll back sees as links (empty = the real check)
    std::function<bool(const QString& from, const QString& to)> failRename;  ///< true = that rename fails
    qint64 now = 1'700'000'000'000;
    qint64 free = -1;
    int binCounter = 0;

    Rig() { build(); }

    Pack::Hooks hooks()
    {
        Pack::Hooks h;
        if (!linkNames.isEmpty())
            h.isLink = [this](const QString& path) { return linkNames.contains(QFileInfo(path).fileName()); };
        h.trashPath = [this](const QString& path) {
            if (crashOnTrash)
                throw std::runtime_error("crash");
            if (failTrash)
                return false;
            trashed.append(path);
            QDir().mkpath(bin);
            return QDir().rename(path, bin + QLatin1Char('/') + QString::number(++binCounter) + QLatin1Char('-') + QFileInfo(path).fileName());
        };
        h.rename = [this](const QString& from, const QString& to) {
            if (failRename && failRename(from, to))
                return false;
            return QDir().rename(from, to);
        };
        h.freeBytes = [this](const QString&) { return free; };
        h.nowMs = [this]() { return now; };
        return h;
    }

    QString p(const QString& rel) const { return inst + QLatin1Char('/') + rel; }
    bool has(const QString& rel) const { return QFileInfo::exists(p(rel)); }
    QByteArray content(const QString& rel) const { return readAll(p(rel)); }
    bool put(const QString& rel, const QByteArray& data) { return writeFile(p(rel), data); }

    Meta meta() const
    {
        Meta m;
        m.instanceId = S("inst");
        m.instanceName = S("Inst");
        m.packId = S("PACKID");
        m.packName = S("Sodium Plus");
        m.oldVersion = S("2.3.7");
        m.newVersion = S("2.4.0");
        m.gameDir = S(".minecraft");
        return m;
    }

    /// An instance as Prism lays it out, with the player's data next to the pack's files.
    void build()
    {
        QVERIFY2(put(S("instance.cfg"), kOldCfg), "instance.cfg");
        put(S("mmc-pack.json"), "pack old");
        put(S(".minecraft/mods/a.jar"), "a old");
        put(S(".minecraft/config/x.toml"), "x old");
        put(S(".minecraft/options.txt"), "options old");
        put(S(".minecraft/saves/World1/level.dat"), "world one");
        put(S(".minecraft/screenshots/s1.png"), "shot one");
        put(S(".minecraft/logs/latest.log"), "log one");
        put(S(".minecraft/crash-reports/crash-1.txt"), "crash one");
        put(S(".minecraft/.cache/blob"), "cache");
        put(S(".zero-restore-x/leftover.txt"), "ours");
        QDir().mkpath(snaps);
    }

    /// What Prism's update does to the instance.
    void doUpdate()
    {
        QFile::remove(p(S(".minecraft/mods/a.jar")));
        put(S(".minecraft/mods/b.jar"), "b new");
        put(S(".minecraft/config/x.toml"), "x new");
        put(S("mmc-pack.json"), "pack new");
        QString cfg = QString::fromLatin1(kOldCfg);
        cfg.replace(S("ManagedPackVersionID=v1"), S("ManagedPackVersionID=v2"));
        cfg.replace(S("ManagedPackVersionName=2.3.7"), S("ManagedPackVersionName=2.4.0"));
        cfg.replace(S("totalTimePlayed=100"), S("totalTimePlayed=500"));
        cfg.replace(S("JavaPath=C:/java/old"), S("JavaPath=C:/java/new"));
        put(S("instance.cfg"), cfg.toLatin1());
        // the player played in between
        put(S(".minecraft/saves/World2/level.dat"), "world two");
        put(S(".minecraft/screenshots/s2.png"), "shot two");
        put(S(".minecraft/logs/latest.log"), "log two");
    }

    CreateResult snapshot() { return createSnapshot(inst, snaps, meta(), hooks()); }

    QStringList zips() const { return QDir(snaps).entryList({ S("*.zip") }, QDir::Files, QDir::Name); }
    QStringList leftovers() const
    {
        QStringList out;
        for (const QString& name : QDir(instances).entryList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot))
            if (name.startsWith(S(".zero-")))
                out.append(name);
        return out;
    }
};

/// Every file below `dir` ("a/b.txt" -> its content), to compare two trees.
QHash<QString, QByteArray> tree(const QString& dir)
{
    QHash<QString, QByteArray> out;
    QDir base(dir);
    QStringList pending{ dir };
    while (!pending.isEmpty()) {
        const QString d = pending.takeLast();
        for (const QFileInfo& fi : QDir(d).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)) {
            if (fi.isDir())
                pending.append(fi.absoluteFilePath());
            else
                out.insert(base.relativeFilePath(fi.absoluteFilePath()), readAll(fi.absoluteFilePath()));
        }
    }
    return out;
}

/// True when `fn` throws: the tests simulate a crash by throwing out of an injected hook (the Qt exception macros are not built here).
template <typename Fn>
bool throwsCrash(Fn fn)
{
    try {
        fn();
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

/// A roll back that dies (the process ends) when its `crashAt` rename runs; every other rename runs for real. True = it crashed.
bool crashDuring(Rig& rig, const QString& zip, std::function<bool(const QString&, const QString&)> crashAt)
{
    rig.failRename = [crashAt](const QString& from, const QString& to) {
        if (crashAt(from, to))
            throw std::runtime_error("crash");
        return false;
    };
    const bool crashed = throwsCrash([&]() { rollback(rig.inst, zip, rig.snaps, S("PACKID"), rig.hooks()); });
    rig.failRename = nullptr;
    return crashed;
}

}  // namespace

class PackSnapshotTest : public QObject {
    Q_OBJECT

   private slots:
    // ---------------------------------------------------------------- pure

    void excludeRules()
    {
        const QString g = S(".minecraft");
        for (const char* rel : { ".minecraft/saves", ".minecraft/saves/W/level.dat", ".minecraft/screenshots/a.png", ".minecraft/logs/latest.log",
                                 ".minecraft/crash-reports/c.txt", ".minecraft/.cache/x", ".zero-restore-a/x", ".minecraft/mods/.zero-tmp/a",
                                 ".MINECRAFT/SAVES/W" })
            QVERIFY2(isExcluded(S(rel), g), rel);
        for (const char* rel : { ".minecraft/mods/a.jar", ".minecraft/config/saves.toml", ".minecraft/options.txt", "saves/W/level.dat",
                                 ".minecraft/mods/saves/x", "instance.cfg", ".minecraft/resourcepacks/logs.zip", ".minecraft" })
            QVERIFY2(!isExcluded(S(rel), g), rel);
        // a game folder that is the instance folder itself
        QVERIFY(isExcluded(S("saves/W/level.dat"), QString()));
        QVERIFY(!isExcluded(S("mods/saves"), QString()));
        // the other game folder name
        QVERIFY(isExcluded(S("minecraft/screenshots/a.png"), S("minecraft")));
        QVERIFY(!isExcluded(S(".minecraft/screenshots/a.png"), S("minecraft")));
    }

    void relativeGameDirAndPlayFolders()
    {
        QTemporaryDir d;
        const QString root = d.filePath(S("inst"));
        QCOMPARE(relativeGameDir(root, root + S("/.minecraft")), S(".minecraft"));
        QCOMPARE(relativeGameDir(root, root + S("/minecraft")), S("minecraft"));
        QCOMPARE(relativeGameDir(root, root), QString());
        QCOMPARE(relativeGameDir(root, d.filePath(S("elsewhere"))), QString());
        QCOMPARE(playDataFolders(S(".minecraft")), QStringList({ S(".minecraft/saves"), S(".minecraft/screenshots"), S(".minecraft/logs"),
                                                                 S(".minecraft/crash-reports") }));
        QCOMPARE(playDataFolders(QString()).first(), S("saves"));
    }

    void cfgMergeKeepsTheRestAndTakesManagedPackKeys()
    {
        const QString current = S("[General]\nManagedPack=true\nManagedPackVersionID=v2\nManagedPackVersionName=2.4.0\nname=Inst\ntotalTimePlayed=500\n");
        const QString snapshot = S("[General]\nManagedPack=true\nManagedPackVersionID=v1\nManagedPackVersionName=2.3.7\nname=Old\ntotalTimePlayed=100\n");
        const QString merged = mergeInstanceCfg(current, snapshot);
        QVERIFY(merged.contains(S("ManagedPackVersionID=v1\n")));
        QVERIFY(merged.contains(S("ManagedPackVersionName=2.3.7\n")));
        QVERIFY(!merged.contains(S("v2")));
        QVERIFY(!merged.contains(S("2.4.0")));
        QVERIFY(merged.contains(S("name=Inst\n")));
        QVERIFY(merged.contains(S("totalTimePlayed=500\n")));
        QVERIFY(merged.endsWith(S("\n")));
        QCOMPARE(merged.count(S("ManagedPack=true")), 1);
    }

    void cfgMergeKeepsLineEndingsAndOtherSections()
    {
        const QString current = S("[General]\r\nManagedPackVersionName=2.4.0\r\nname=Inst\r\n\r\n[Other]\r\nManagedPackVersionName=keep\r\nk=v\r\n");
        const QString snapshot = S("[General]\r\nManagedPackVersionName=2.3.7\r\n[Other]\r\nManagedPackVersionName=ignored\r\n");
        const QString merged = mergeInstanceCfg(current, snapshot);
        QCOMPARE(merged, S("[General]\r\nname=Inst\r\nManagedPackVersionName=2.3.7\r\n\r\n[Other]\r\nManagedPackVersionName=keep\r\nk=v\r\n"));
    }

    void cfgMergeDropsKeysTheSnapshotDoesNotHaveAndAddsMissingOnes()
    {
        QCOMPARE(mergeInstanceCfg(S("[General]\nManagedPack=true\nname=A\n"), S("[General]\nname=B\n")), S("[General]\nname=A\n"));
        QCOMPARE(mergeInstanceCfg(S("[General]\nname=A\n"), S("[General]\nManagedPackName=P\n")), S("[General]\nname=A\nManagedPackName=P\n"));
    }

    // ---------------------------------------------------------------- snapshot

    void snapshotKeepsPackFilesAndLeavesPlayDataOut()
    {
        Rig rig;
        const CreateResult r = rig.snapshot();
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(QFileInfo(r.zipPath).isFile());
        QCOMPARE(QFileInfo(r.zipPath).fileName(), S("pack-20231114-221320.zip"));
        QVERIFY(verifyBackup(r.zipPath).ok);

        QTemporaryDir out;
        const QString dir = out.filePath(S("x"));
        QVERIFY(extractBackup(r.zipPath, dir).ok);
        const auto files = tree(dir);
        const QSet<QString> got(files.keyBegin(), files.keyEnd());
        const QSet<QString> want = { S("instance.cfg"), S("mmc-pack.json"), S(".minecraft/mods/a.jar"), S(".minecraft/config/x.toml"),
                                     S(".minecraft/options.txt") };
        QCOMPARE(got, want);
        // the instance itself is untouched
        QVERIFY(rig.has(S(".minecraft/saves/World1/level.dat")));
        QVERIFY(rig.has(S(".minecraft/.cache/blob")));
        QVERIFY(rig.has(S(".zero-restore-x/leftover.txt")));
    }

    void manifestAndSidecarCarryTheMeta()
    {
        Rig rig;
        const CreateResult r = rig.snapshot();
        QVERIFY(r.ok);
        const ManifestInfo m = readBackupInfo(r.zipPath);
        QVERIFY(m.ok);
        QCOMPARE(m.extra.value("instanceId").toString(), S("inst"));
        QCOMPARE(m.extra.value("packName").toString(), S("Sodium Plus"));
        QCOMPARE(m.extra.value("oldVersion").toString(), S("2.3.7"));
        QCOMPARE(m.extra.value("newVersion").toString(), S("2.4.0"));
        QCOMPARE(m.extra.value("gameDir").toString(), S(".minecraft"));
        QCOMPARE(qint64(m.extra.value("createdMs").toDouble()), rig.now);

        QList<Info> list = listSnapshots(rig.snaps);
        QCOMPARE(list.size(), 1);
        QVERIFY(list.first().valid);
        QCOMPARE(list.first().outcome, S("pending"));
        QVERIFY(!list.first().rolledBack);
        QCOMPARE(list.first().meta.packName, S("Sodium Plus"));
        QCOMPARE(list.first().meta.oldVersion, S("2.3.7"));
        QCOMPARE(list.first().createdMs, rig.now);

        QVERIFY(markOutcome(r.zipPath, S("failed")));
        list = listSnapshots(rig.snaps);
        QCOMPARE(list.first().outcome, S("failed"));
        QVERIFY(markOutcome(r.zipPath, S("ok")));
        QCOMPARE(listSnapshots(rig.snaps).first().outcome, S("ok"));
        QVERIFY(!listSnapshots(rig.snaps).first().rolledBack);

        // a missing sidecar falls back on the manifest
        QVERIFY(QFile::remove(r.zipPath.left(r.zipPath.size() - 4) + S(".json")));
        list = listSnapshots(rig.snaps);
        QCOMPARE(list.size(), 1);
        QVERIFY(list.first().valid);
        QCOMPARE(list.first().meta.oldVersion, S("2.3.7"));
        QCOMPARE(list.first().outcome, S("pending"));
    }

    void newestActiveSkipsRolledBackAndOtherPacks()
    {
        Rig rig;
        QVERIFY(rig.snapshot().ok);
        rig.now += 5000;
        Meta other = rig.meta();
        other.packId = S("OTHER");
        other.oldVersion = S("9");
        const CreateResult second = createSnapshot(rig.inst, rig.snaps, other, rig.hooks());
        QVERIFY(second.ok);
        const QList<Info> list = listSnapshots(rig.snaps);
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.first().meta.oldVersion, S("9"));  // newest first
        QCOMPARE(newestActive(list, S("PACKID")).meta.oldVersion, S("2.3.7"));
        QCOMPARE(newestActive(list, S("OTHER")).meta.oldVersion, S("9"));
        QCOMPARE(newestActive(list, QString()).meta.oldVersion, S("9"));
        QVERIFY(markRolledBack(second.zipPath));
        QCOMPARE(newestActive(listSnapshots(rig.snaps), QString()).meta.oldVersion, S("2.3.7"));
        QVERIFY(!newestActive({}, QString()).valid);
    }

    void retentionKeepsTheNewestTwo()
    {
        Rig rig;
        QStringList made;
        for (int i = 0; i < 4; ++i) {
            const CreateResult r = rig.snapshot();
            QVERIFY(r.ok);
            made.append(r.zipPath);
            rig.now += 2000;
            // each retention run (inside createSnapshot) leaves at most kKeep
            QVERIFY(rig.zips().size() <= kKeep);
        }
        QCOMPARE(rig.zips().size(), 2);
        QVERIFY(!QFileInfo::exists(made.at(0)));
        QVERIFY(!QFileInfo::exists(made.at(1)));
        QVERIFY(QFileInfo::exists(made.at(2)));
        QVERIFY(QFileInfo::exists(made.at(3)));
        QVERIFY(!QFileInfo::exists(made.at(0).left(made.at(0).size() - 4) + S(".json")));  // sidecars follow
        QCOMPARE(QDir(rig.snaps).entryList({ S("*.json") }, QDir::Files).size(), 2);
        // not ours: untouched
        QVERIFY(writeFile(rig.snaps + S("/notes.zip"), "mine"));
        QVERIFY(writeFile(rig.snaps + S("/20200101-000000/snapshot.json"), "{}"));  // a mod snapshot folder (6a)
        QCOMPARE(Pack::applyRetention(rig.snaps, {}, 1), 1);
        QVERIFY(QFileInfo::exists(rig.snaps + S("/notes.zip")));
        QVERIFY(QFileInfo::exists(rig.snaps + S("/20200101-000000/snapshot.json")));
    }

    void twoSnapshotsInOneSecondGetDifferentNames()
    {
        Rig rig;
        const CreateResult a = rig.snapshot();
        const CreateResult b = rig.snapshot();  // same fake clock
        QVERIFY(a.ok && b.ok);
        QVERIFY(a.zipPath != b.zipPath);
        QCOMPARE(listSnapshots(rig.snaps).size(), 2);
        QCOMPARE(QFileInfo(listSnapshots(rig.snaps).first().zipPath).fileName(), S("pack-20231114-221320-2.zip"));
    }

    void notEnoughSpaceLeavesNothingBehind()
    {
        Rig rig;
        rig.free = 1000;
        const CreateResult r = rig.snapshot();
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
        QVERIFY(r.zipPath.isEmpty());
        QVERIFY(QDir(rig.snaps).entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty());
    }

    void instanceThatDoesNotExistFails()
    {
        Rig rig;
        QVERIFY(!createSnapshot(rig.root.filePath(S("nope")), rig.snaps, rig.meta(), rig.hooks()).ok);
    }

    // ---------------------------------------------------------------- roll back

    void rollbackBringsBackThePackAndKeepsThePlayersData()
    {
        Rig rig;
        const CreateResult snap = rig.snapshot();
        QVERIFY(snap.ok);
        rig.doUpdate();

        const RollbackResult r = rollback(rig.inst, snap.zipPath, rig.snaps, S("PACKID"), rig.hooks());
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.leftoverPath.isEmpty());

        // the pack is the old one
        QCOMPARE(rig.content(S("mmc-pack.json")), QByteArray("pack old"));
        QCOMPARE(rig.content(S(".minecraft/mods/a.jar")), QByteArray("a old"));
        QVERIFY(!rig.has(S(".minecraft/mods/b.jar")));
        QCOMPARE(rig.content(S(".minecraft/config/x.toml")), QByteArray("x old"));
        QCOMPARE(rig.content(S(".minecraft/options.txt")), QByteArray("options old"));
        // the player's data is the current one
        QCOMPARE(rig.content(S(".minecraft/saves/World1/level.dat")), QByteArray("world one"));
        QCOMPARE(rig.content(S(".minecraft/saves/World2/level.dat")), QByteArray("world two"));
        QCOMPARE(rig.content(S(".minecraft/screenshots/s1.png")), QByteArray("shot one"));
        QCOMPARE(rig.content(S(".minecraft/screenshots/s2.png")), QByteArray("shot two"));
        QCOMPARE(rig.content(S(".minecraft/logs/latest.log")), QByteArray("log two"));
        QCOMPARE(rig.content(S(".minecraft/crash-reports/crash-1.txt")), QByteArray("crash one"));
        // instance.cfg: play time and Java stay, the ManagedPack* keys are the snapshot's
        const QString cfg = QString::fromLatin1(rig.content(S("instance.cfg")));
        QVERIFY(cfg.contains(S("totalTimePlayed=500")));
        QVERIFY(cfg.contains(S("JavaPath=C:/java/new")));
        QVERIFY(cfg.contains(S("ManagedPackVersionName=2.3.7")));
        QVERIFY(cfg.contains(S("ManagedPackVersionID=v1")));
        QVERIFY(!cfg.contains(S("2.4.0")));
        QVERIFY(!cfg.contains(S("v2")));
        // what was replaced is in the Recycle Bin (never deleted), no staging folder is left
        QCOMPARE(rig.trashed.size(), 1);
        QVERIFY(QFileInfo(rig.trashed.first()).fileName().startsWith(S(".zero-replaced-inst-")));
        const QString binned = rig.bin + S("/1-") + QFileInfo(rig.trashed.first()).fileName();
        QCOMPARE(readAll(binned + S("/.minecraft/mods/b.jar")), QByteArray("b new"));
        QCOMPARE(readAll(binned + S("/.minecraft/.cache/blob")), QByteArray("cache"));
        QVERIFY(!QFileInfo::exists(binned + S("/.minecraft/saves")));  // carried over, not trashed
        QVERIFY(rig.leftovers().isEmpty());                            // no staging / replaced folder is left next to the instance
    }

    void rollbackMarksTheSnapshotAndRefusesAgain()
    {
        Rig rig;
        const CreateResult snap = rig.snapshot();
        rig.doUpdate();
        QVERIFY(rollback(rig.inst, snap.zipPath, rig.snaps, S("PACKID"), rig.hooks()).ok);
        QVERIFY(listSnapshots(rig.snaps).first().rolledBack);
        QVERIFY(!newestActive(listSnapshots(rig.snaps), S("PACKID")).valid);
        rig.doUpdate();
        const auto before = tree(rig.inst);
        const RollbackResult again = rollback(rig.inst, snap.zipPath, rig.snaps, S("PACKID"), rig.hooks());
        QVERIFY(!again.ok);
        QCOMPARE(tree(rig.inst), before);
    }

    void rollbackRefusesAnotherPackAnotherInstanceAndForeignFiles()
    {
        Rig rig;
        const CreateResult snap = rig.snapshot();
        rig.doUpdate();
        const auto before = tree(rig.inst);
        QVERIFY(!rollback(rig.inst, snap.zipPath, rig.snaps, S("SOMETHING-ELSE"), rig.hooks()).ok);
        // the same snapshot against a differently named instance folder
        const QString other = rig.instances + S("/other");
        QVERIFY(writeFile(other + S("/instance.cfg"), kOldCfg));
        QVERIFY(!rollback(other, snap.zipPath, rig.snaps, S("PACKID"), rig.hooks()).ok);
        QVERIFY(readAll(other + S("/instance.cfg")) == QByteArray(kOldCfg));
        // a zip outside the instance's snapshots folder
        const QString stray = rig.root.filePath(S("pack-20200101-000000.zip"));
        QVERIFY(QFile::copy(snap.zipPath, stray));
        QVERIFY(!rollback(rig.inst, stray, rig.snaps, S("PACKID"), rig.hooks()).ok);
        QCOMPARE(tree(rig.inst), before);
        QVERIFY(rig.trashed.isEmpty());
        QVERIFY(rig.leftovers().isEmpty());
    }

    void rollbackWithADamagedZipChangesNothing()
    {
        Rig rig;
        const CreateResult snap = rig.snapshot();
        rig.doUpdate();
        const auto before = tree(rig.inst);
        // cut the zip: its central directory is gone
        QByteArray bytes = readAll(snap.zipPath);
        QVERIFY(bytes.size() > 100);
        QVERIFY(writeFile(snap.zipPath, bytes.left(bytes.size() * 6 / 10)));
        const RollbackResult r = rollback(rig.inst, snap.zipPath, rig.snaps, S("PACKID"), rig.hooks());
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
        QCOMPARE(tree(rig.inst), before);
        QVERIFY(rig.trashed.isEmpty());
        QVERIFY(rig.leftovers().isEmpty());
        QVERIFY(!listSnapshots(rig.snaps).first().rolledBack);
    }

    void rollbackWithAFlippedByteChangesNothing()
    {
        Rig rig;
        QByteArray noise(300000, 0);  // incompressible, so the damage lands in the data
        QRandomGenerator gen(42);
        for (char& c : noise)
            c = char(gen.generate());
        QVERIFY(rig.put(S(".minecraft/mods/big.jar"), noise));
        const CreateResult snap = rig.snapshot();
        QVERIFY(snap.ok);
        rig.doUpdate();
        const auto before = tree(rig.inst);
        QByteArray bytes = readAll(snap.zipPath);
        const int at = bytes.indexOf(QByteArray("a old"));  // the entry may be stored or deflated: damage the whole middle in any case
        for (int i = bytes.size() / 3; i < bytes.size() / 3 + 40; ++i)
            bytes[i] = char(bytes[i] ^ 0x5a);
        Q_UNUSED(at);
        QVERIFY(writeFile(snap.zipPath, bytes));
        QVERIFY(!rollback(rig.inst, snap.zipPath, rig.snaps, S("PACKID"), rig.hooks()).ok);
        QCOMPARE(tree(rig.inst), before);
        QVERIFY(rig.leftovers().isEmpty());
    }

    void notEnoughSpaceForTheRollbackChangesNothing()
    {
        Rig rig;
        const CreateResult snap = rig.snapshot();
        rig.doUpdate();
        const auto before = tree(rig.inst);
        rig.free = 1000;
        QVERIFY(!rollback(rig.inst, snap.zipPath, rig.snaps, S("PACKID"), rig.hooks()).ok);
        QCOMPARE(tree(rig.inst), before);
        QVERIFY(rig.leftovers().isEmpty());
    }

    void failureWhileCarryingPlayDataPutsItBack()
    {
        Rig rig;
        const CreateResult snap = rig.snapshot();
        rig.doUpdate();
        const auto before = tree(rig.inst);
        // saves moved fine, screenshots cannot move (for instance something keeps it open)
        rig.failRename = [](const QString& from, const QString&) { return QFileInfo(from).fileName() == QLatin1String("screenshots"); };
        const RollbackResult r = rollback(rig.inst, snap.zipPath, rig.snaps, S("PACKID"), rig.hooks());
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(S("not changed")));
        QCOMPARE(tree(rig.inst), before);  // saves are back, nothing else moved
        QVERIFY(rig.trashed.isEmpty());
        QVERIFY(rig.leftovers().isEmpty());  // the staging folder went too
        QVERIFY(!listSnapshots(rig.snaps).first().rolledBack);
    }

    void failureToStepAsideChangesNothing()
    {
        Rig rig;
        const CreateResult snap = rig.snapshot();
        rig.doUpdate();
        const auto before = tree(rig.inst);
        // the instance folder itself cannot be renamed (open in another program)
        rig.failRename = [&rig](const QString& from, const QString&) { return QDir::cleanPath(from) == QDir::cleanPath(rig.inst); };
        const RollbackResult r = rollback(rig.inst, snap.zipPath, rig.snaps, S("PACKID"), rig.hooks());
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(S("not changed")));
        QCOMPARE(tree(rig.inst), before);  // including saves / screenshots / logs / crash-reports that had been moved and came back
        QVERIFY(rig.trashed.isEmpty());
        QVERIFY(rig.leftovers().isEmpty());
        QVERIFY(!listSnapshots(rig.snaps).first().rolledBack);
    }

    void failureToMoveTheRestoredFolderIntoPlacePutsEverythingBack()
    {
        Rig rig;
        const CreateResult snap = rig.snapshot();
        rig.doUpdate();
        const auto before = tree(rig.inst);
        // the first rename of a ".zero-restore-*" folder (staging -> instance) fails
        rig.failRename = [](const QString& from, const QString&) { return QFileInfo(from).fileName().startsWith(QLatin1String(".zero-restore-")); };
        const RollbackResult r = rollback(rig.inst, snap.zipPath, rig.snaps, S("PACKID"), rig.hooks());
        QVERIFY(!r.ok);
        QCOMPARE(tree(rig.inst), before);
        QVERIFY(rig.trashed.isEmpty());
        QVERIFY(rig.leftovers().isEmpty());
        QVERIFY(!listSnapshots(rig.snaps).first().rolledBack);
    }

    void failureToPutTheInstanceBackTellsWhereEverythingIs()
    {
        Rig rig;
        const CreateResult snap = rig.snapshot();
        rig.doUpdate();
        // staging -> instance fails AND the way back (replaced -> instance) fails
        rig.failRename = [](const QString& from, const QString&) {
            const QString n = QFileInfo(from).fileName();
            return n.startsWith(QLatin1String(".zero-restore-")) || n.startsWith(QLatin1String(".zero-replaced-"));
        };
        const RollbackResult r = rollback(rig.inst, snap.zipPath, rig.snaps, S("PACKID"), rig.hooks());
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(S(".zero-replaced-inst-")));
        QVERIFY(r.error.contains(S(".zero-restore-inst-")));
        // nothing was deleted: the instance is in the replaced folder, the play data in the staging folder
        QVERIFY(rig.trashed.isEmpty());
        QCOMPARE(rig.leftovers().size(), 2);
        bool foundSaves = false;
        bool foundMods = false;
        for (const QString& name : rig.leftovers()) {
            foundSaves |= QFileInfo::exists(rig.instances + QLatin1Char('/') + name + S("/.minecraft/saves/World2/level.dat"));
            foundMods |= QFileInfo::exists(rig.instances + QLatin1Char('/') + name + S("/.minecraft/mods/b.jar"));
        }
        QVERIFY(foundSaves);
        QVERIFY(foundMods);
    }

    void trashFailureAfterTheSwapKeepsTheRestoredInstanceAndLeavesTheOldFolder()
    {
        Rig rig;
        const CreateResult snap = rig.snapshot();
        rig.doUpdate();
        rig.failTrash = true;
        const RollbackResult r = rollback(rig.inst, snap.zipPath, rig.snaps, S("PACKID"), rig.hooks());
        QVERIFY2(r.ok, qPrintable(r.error));  // the instance is restored
        QVERIFY(!r.leftoverPath.isEmpty());
        QVERIFY(QFileInfo(r.leftoverPath).isDir());  // left where it is, never deleted
        QVERIFY(QFileInfo(r.leftoverPath).fileName().startsWith(S(".zero-leftover-inst-")));  // renamed, so recovery cannot take it for a cut-off roll back
        QCOMPARE(readAll(r.leftoverPath + S("/.minecraft/mods/b.jar")), QByteArray("b new"));
        QCOMPARE(rig.content(S(".minecraft/mods/a.jar")), QByteArray("a old"));
        QCOMPARE(rig.content(S(".minecraft/saves/World2/level.dat")), QByteArray("world two"));
        QVERIFY(listSnapshots(rig.snaps).first().rolledBack);
    }

    void aSnapshotOfAnInstanceWithoutPlayDataRollsBack()
    {
        Rig rig;
        QDir(rig.p(S(".minecraft/saves"))).removeRecursively();
        QDir(rig.p(S(".minecraft/screenshots"))).removeRecursively();
        QDir(rig.p(S(".minecraft/logs"))).removeRecursively();
        QDir(rig.p(S(".minecraft/crash-reports"))).removeRecursively();
        const CreateResult snap = rig.snapshot();
        rig.doUpdate();
        QDir(rig.p(S(".minecraft/saves"))).removeRecursively();
        const RollbackResult r = rollback(rig.inst, snap.zipPath, rig.snaps, S("PACKID"), rig.hooks());
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(rig.content(S(".minecraft/mods/a.jar")), QByteArray("a old"));
        QVERIFY(!rig.has(S(".minecraft/saves")));
        QCOMPARE(rig.content(S(".minecraft/screenshots/s2.png")), QByteArray("shot two"));
    }

    void snapshotMissingFromTheInstanceFolderIsRemovedOnlyWhenOurs()
    {
        Rig rig;
        const CreateResult snap = rig.snapshot();
        QVERIFY(!removeSnapshot(rig.root.filePath(S("pack-20200101-000000.zip")), rig.snaps));  // not in the snapshots folder
        QVERIFY(writeFile(rig.snaps + S("/notes.zip"), "mine"));
        QVERIFY(!removeSnapshot(rig.snaps + S("/notes.zip"), rig.snaps));  // not ZERO's name
        QVERIFY(QFileInfo::exists(rig.snaps + S("/notes.zip")));
        QVERIFY(removeSnapshot(snap.zipPath, rig.snaps));
        QVERIFY(!QFileInfo::exists(snap.zipPath));
        QVERIFY(listSnapshots(rig.snaps).isEmpty());
    }

    // ---------------------------------------------------------------- wording, links

    void aFileWindowsCannotCopyGetsTheInstanceWording()
    {
        Rig rig;
        // " con.json" (leading space) is a legal Windows name that the safe-path check rejects as the reserved name CON
        if (!rig.put(S(".minecraft/config/ con.json"), "x"))
            QSKIP("this file system cannot hold the test file");
        const CreateResult r = rig.snapshot();
        QVERIFY(!r.ok);
        QVERIFY2(r.error.startsWith(S("This instance contains a file Windows cannot copy: .minecraft/config/ con.json.")), qPrintable(r.error));
        QVERIFY(r.error.contains(S("Rename or remove it, then update again.")));
        QVERIFY(!r.error.contains(S("world")));
        QVERIFY(rig.zips().isEmpty());
    }

    void worldsKeepTheirOwnWordingForUnsafeNames()
    {
        QTemporaryDir d;
        if (!writeFile(d.filePath(S("w/ con.json")), "x"))
            QSKIP("this file system cannot hold the test file");
        const BackupResult r = createBackup(d.filePath(S("w")), d.filePath(S("b")), Kind::Manual, { S("i"), S("I"), S("w"), S("W") });
        QVERIFY(!r.ok);
        QVERIFY(r.error.startsWith(S("This world contains a file name that cannot be backed up safely")));
    }

    void linksTheSnapshotDoesNotHoldAreCarriedOver()
    {
        Rig rig;
        const CreateResult snap = rig.snapshot();
        rig.doUpdate();
        QVERIFY(rig.put(S(".minecraft/shaderpacks/pack.zip"), "linked shaders"));  // not in the snapshot: its place is free
        rig.linkNames = { S("shaderpacks") };
        const RollbackResult r = rollback(rig.inst, snap.zipPath, rig.snaps, S("PACKID"), rig.hooks());
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(rig.content(S(".minecraft/shaderpacks/pack.zip")), QByteArray("linked shaders"));
        QCOMPARE(rig.content(S(".minecraft/mods/a.jar")), QByteArray("a old"));
        const QString binned = rig.bin + S("/1-") + QFileInfo(rig.trashed.first()).fileName();
        QVERIFY(!QFileInfo::exists(binned + S("/.minecraft/shaderpacks")));  // moved, not trashed
    }

    void aLinkWhoseSpaceTheSnapshotHoldsStaysWithTheOldInstance()
    {
        Rig rig;
        const CreateResult snap = rig.snapshot();  // the snapshot has a real mods folder
        rig.doUpdate();
        rig.linkNames = { S("mods") };  // ... which is a link now
        const RollbackResult r = rollback(rig.inst, snap.zipPath, rig.snaps, S("PACKID"), rig.hooks());
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(rig.content(S(".minecraft/mods/a.jar")), QByteArray("a old"));
        QVERIFY(!rig.has(S(".minecraft/mods/b.jar")));
        const QString binned = rig.bin + S("/1-") + QFileInfo(rig.trashed.first()).fileName();
        QCOMPARE(readAll(binned + S("/.minecraft/mods/b.jar")), QByteArray("b new"));
    }

    void failureToMoveALinkPutsEverythingBack()
    {
        Rig rig;
        const CreateResult snap = rig.snapshot();
        rig.doUpdate();
        QVERIFY(rig.put(S(".minecraft/shaderpacks/pack.zip"), "linked shaders"));
        rig.linkNames = { S("shaderpacks") };
        const auto before = tree(rig.inst);
        rig.failRename = [](const QString& from, const QString&) { return QFileInfo(from).fileName() == QLatin1String("shaderpacks"); };
        const RollbackResult r = rollback(rig.inst, snap.zipPath, rig.snaps, S("PACKID"), rig.hooks());
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(S("not changed")));
        QCOMPARE(tree(rig.inst), before);  // saves etc. that had moved are back
        QVERIFY(rig.leftovers().isEmpty());
        QVERIFY(rig.trashed.isEmpty());
    }

    void aRealJunctionIsCarriedOverAndStaysALink()
    {
        Rig rig;
        const QString target = rig.root.filePath(S("shared-mods"));
        QVERIFY(writeFile(target + S("/shared.jar"), "shared"));
        QDir(rig.p(S(".minecraft/mods"))).removeRecursively();
        const QString link = rig.p(S(".minecraft/mods"));
        if (QProcess::execute(S("cmd"), { S("/c"), S("mklink"), S("/J"), QDir::toNativeSeparators(link), QDir::toNativeSeparators(target) }) != 0)
            QSKIP("cannot create a junction here");
        struct Unlink {  // removes the junction itself (never its target) before the temp dir is cleaned
            QString path;
            ~Unlink() { QDir().rmdir(path); }
        } unlink{ link };
        QVERIFY(QFileInfo(link).isJunction());

        const CreateResult snap = rig.snapshot();
        QVERIFY(snap.ok);
        QVERIFY(rig.put(S(".minecraft/config/x.toml"), "x new"));
        const RollbackResult r = rollback(rig.inst, snap.zipPath, rig.snaps, S("PACKID"), rig.hooks());
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(rig.content(S(".minecraft/config/x.toml")), QByteArray("x old"));
        QVERIFY(QFileInfo(link).isJunction());  // the link itself came along
        QCOMPARE(readAll(link + S("/shared.jar")), QByteArray("shared"));
        QCOMPARE(readAll(target + S("/shared.jar")), QByteArray("shared"));
    }

    // ---------------------------------------------------------------- recovery of a cut-off roll back

    void recoveryAfterACrashWhilePlayDataWasMoved()
    {
        Rig rig;
        const CreateResult snap = rig.snapshot();
        rig.doUpdate();
        const auto before = tree(rig.inst);
        QVERIFY(crashDuring(rig, snap.zipPath, [&rig](const QString& from, const QString&) { return QDir::cleanPath(from) == QDir::cleanPath(rig.inst); }));
        // the worlds are in the hidden staging folder, the instance has none
        QVERIFY(!rig.has(S(".minecraft/saves")));
        QCOMPARE(rig.leftovers().size(), 1);

        const RecoverResult rec = recoverInterrupted(rig.instances, rig.hooks());
        QVERIFY2(rec.problems.isEmpty(), qPrintable(rec.problems.join(S("; "))));
        QVERIFY(!rec.actions.isEmpty());
        QCOMPARE(tree(rig.inst), before);
        QVERIFY(rig.leftovers().isEmpty());
        QVERIFY(rig.trashed.isEmpty());  // the extracted copy is ZERO's own, it is removed, nothing is trashed
    }

    void recoveryAfterACrashBetweenTheTwoRenames()
    {
        Rig rig;
        const CreateResult snap = rig.snapshot();
        rig.doUpdate();
        const auto before = tree(rig.inst);
        QVERIFY(crashDuring(rig, snap.zipPath, [](const QString& from, const QString&) { return QFileInfo(from).fileName().startsWith(QLatin1String(".zero-restore-")); }));
        QVERIFY(!QFileInfo::exists(rig.inst));  // the instance is only in the replaced folder
        QCOMPARE(rig.leftovers().size(), 2);

        const RecoverResult rec = recoverInterrupted(rig.instances, rig.hooks());
        QVERIFY2(rec.problems.isEmpty(), qPrintable(rec.problems.join(S("; "))));
        QCOMPARE(tree(rig.inst), before);  // instance back, play data back
        QVERIFY(rig.leftovers().isEmpty());
        QVERIFY(rig.trashed.isEmpty());
    }

    void recoveryAfterACrashBeforeTheReplacedFolderWasTrashed()
    {
        Rig rig;
        const CreateResult snap = rig.snapshot();
        rig.doUpdate();
        rig.crashOnTrash = true;
        QVERIFY(throwsCrash([&]() { rollback(rig.inst, snap.zipPath, rig.snaps, S("PACKID"), rig.hooks()); }));
        rig.crashOnTrash = false;
        const auto restored = tree(rig.inst);
        QCOMPARE(rig.content(S(".minecraft/mods/a.jar")), QByteArray("a old"));
        QCOMPARE(rig.leftovers().size(), 1);

        const RecoverResult rec = recoverInterrupted(rig.instances, rig.hooks());
        QVERIFY2(rec.problems.isEmpty(), qPrintable(rec.problems.join(S("; "))));
        QCOMPARE(tree(rig.inst), restored);  // the instance is untouched
        QCOMPARE(rig.trashed.size(), 1);     // the replaced folder went to the Recycle Bin
        QVERIFY(QFileInfo(rig.trashed.first()).fileName().startsWith(S(".zero-replaced-inst-")));
        QVERIFY(rig.leftovers().isEmpty());
    }

    void recoveryKeepsTheStagingFolderWhenPlayDataCannotMoveBack()
    {
        Rig rig;
        const CreateResult snap = rig.snapshot();
        rig.doUpdate();
        QVERIFY(crashDuring(rig, snap.zipPath, [&rig](const QString& from, const QString&) { return QDir::cleanPath(from) == QDir::cleanPath(rig.inst); }));
        rig.failRename = [](const QString& from, const QString&) { return QFileInfo(from).fileName() == QLatin1String("screenshots"); };
        const RecoverResult rec = recoverInterrupted(rig.instances, rig.hooks());
        QVERIFY(!rec.problems.isEmpty());
        QCOMPARE(rig.content(S(".minecraft/saves/World2/level.dat")), QByteArray("world two"));  // the others came back
        QCOMPARE(rig.leftovers().size(), 1);                                                      // the staging folder is kept
        const QString staging = rig.instances + QLatin1Char('/') + rig.leftovers().first();
        QCOMPARE(readAll(staging + S("/.minecraft/screenshots/s2.png")), QByteArray("shot two"));
        QVERIFY(rig.trashed.isEmpty());
        // once the folder can move, the next start finishes the job
        rig.failRename = nullptr;
        const RecoverResult again = recoverInterrupted(rig.instances, rig.hooks());
        QVERIFY2(again.problems.isEmpty(), qPrintable(again.problems.join(S("; "))));
        QCOMPARE(rig.content(S(".minecraft/screenshots/s2.png")), QByteArray("shot two"));
        QVERIFY(rig.leftovers().isEmpty());
    }

    void recoveryDoesNotOverwriteWhatTheInstanceHasAgain()
    {
        Rig rig;
        const CreateResult snap = rig.snapshot();
        rig.doUpdate();
        QVERIFY(crashDuring(rig, snap.zipPath, [&rig](const QString& from, const QString&) { return QDir::cleanPath(from) == QDir::cleanPath(rig.inst); }));
        QVERIFY(rig.put(S(".minecraft/saves/NewWorld/level.dat"), "new"));  // the game made a new saves folder meanwhile
        const RecoverResult rec = recoverInterrupted(rig.instances, rig.hooks());
        QVERIFY(!rec.problems.isEmpty());
        QCOMPARE(rig.content(S(".minecraft/saves/NewWorld/level.dat")), QByteArray("new"));
        QVERIFY(!rig.has(S(".minecraft/saves/World2/level.dat")));
        QCOMPARE(rig.leftovers().size(), 1);  // the old saves are still in the staging folder
        QVERIFY(QFileInfo::exists(rig.instances + QLatin1Char('/') + rig.leftovers().first() + S("/.minecraft/saves/World2/level.dat")));
    }

    void recoveryMovesLinksBackToo()
    {
        Rig rig;
        const CreateResult snap = rig.snapshot();
        rig.doUpdate();
        QVERIFY(rig.put(S(".minecraft/shaderpacks/pack.zip"), "linked shaders"));
        rig.linkNames = { S("shaderpacks") };
        const auto before = tree(rig.inst);
        QVERIFY(crashDuring(rig, snap.zipPath, [&rig](const QString& from, const QString&) { return QDir::cleanPath(from) == QDir::cleanPath(rig.inst); }));
        QVERIFY(!rig.has(S(".minecraft/shaderpacks")));
        const RecoverResult rec = recoverInterrupted(rig.instances, rig.hooks());
        QVERIFY2(rec.problems.isEmpty(), qPrintable(rec.problems.join(S("; "))));
        QCOMPARE(tree(rig.inst), before);
        QVERIFY(rig.leftovers().isEmpty());
    }

    void aLeftoverIsOfferedToTheRecycleBinAgainAndNeverBringsAnInstanceBack()
    {
        Rig rig;
        const CreateResult snap = rig.snapshot();
        rig.doUpdate();
        rig.failTrash = true;
        const RollbackResult r = rollback(rig.inst, snap.zipPath, rig.snaps, S("PACKID"), rig.hooks());
        QVERIFY(r.ok);
        QVERIFY(QFileInfo(r.leftoverPath).isDir());
        // the player deletes the instance, then starts the launcher: the old leftover must not come back as the instance
        QVERIFY(QDir(rig.inst).removeRecursively());
        rig.failTrash = false;
        const RecoverResult rec = recoverInterrupted(rig.instances, rig.hooks());
        QVERIFY(!QFileInfo::exists(rig.inst));
        QVERIFY(rec.problems.isEmpty());
        QCOMPARE(rig.trashed.size(), 0);  // the instance does not exist: nothing is recovered and the leftover is not touched
        QVERIFY(QFileInfo::exists(r.leftoverPath));
    }

    void aLeftoverOfAnExistingInstanceGoesToTheRecycleBinAtStartup()
    {
        Rig rig;
        const CreateResult snap = rig.snapshot();
        rig.doUpdate();
        rig.failTrash = true;
        const RollbackResult r = rollback(rig.inst, snap.zipPath, rig.snaps, S("PACKID"), rig.hooks());
        QVERIFY(r.ok);
        rig.failTrash = false;
        const RecoverResult rec = recoverInterrupted(rig.instances, rig.hooks());
        QVERIFY(rec.problems.isEmpty());
        QCOMPARE(rig.trashed.size(), 1);
        QVERIFY(!QFileInfo::exists(r.leftoverPath));
        QCOMPARE(rig.content(S(".minecraft/mods/a.jar")), QByteArray("a old"));
    }

    void recoveryLeavesAHealthyInstancesFolderAlone()
    {
        Rig rig;
        QVERIFY(writeFile(rig.instances + S("/.hidden-thing/x.txt"), "x"));
        QVERIFY(writeFile(rig.instances + S("/.zero-restore-notanid/x.txt"), "x"));  // no timestamp: not ours
        const auto before = tree(rig.instances);
        const RecoverResult rec = recoverInterrupted(rig.instances, rig.hooks());
        QVERIFY(rec.actions.isEmpty());
        QVERIFY(rec.problems.isEmpty());
        QCOMPARE(tree(rig.instances), before);
        QVERIFY(rig.trashed.isEmpty());
    }

    // ---------------------------------------------------------------- tasks (the update is gated on the snapshot task)

    void snapshotTaskSucceeds()
    {
        Rig rig;
        PackSnapshotTask task(rig.inst, rig.snaps, rig.meta(), rig.hooks());
        QSignalSpy finished(&task, &Task::finished);
        QSignalSpy progress(&task, &Task::progress);
        task.start();
        QTRY_VERIFY_WITH_TIMEOUT(task.isFinished(), 15000);
        QVERIFY(task.wasSuccessful());
        QVERIFY(QFileInfo(task.zipPath()).isFile());
        QVERIFY(!progress.isEmpty());
        QVERIFY(!WorldBackupJobs::isInstanceLocked(S("inst")));  // the launch lock is released
    }

    void snapshotFailureMeansTheUpdateDoesNotStart()
    {
        // The caller (MainWindow::installModpack) runs the update only when this task succeeded.
        Rig rig;
        rig.free = 1000;
        PackSnapshotTask task(rig.inst, rig.snaps, rig.meta(), rig.hooks());
        task.start();
        QTRY_VERIFY_WITH_TIMEOUT(task.isFinished(), 15000);
        QVERIFY(!task.wasSuccessful());
        QVERIFY(task.zipPath().isEmpty());
        QVERIFY(task.failReason().contains(S("free disk space")));
        QVERIFY(rig.zips().isEmpty());
        QVERIFY(!WorldBackupJobs::isInstanceLocked(S("inst")));
    }

    void snapshotTaskCanBeCancelled()
    {
        Rig rig;
        std::atomic<bool> release{ false };
        Pack::Hooks hooks = rig.hooks();
        // the pool job waits in its free-space check until the test has pressed Cancel, so the order is the same on every run
        hooks.freeBytes = [&release](const QString&) {
            for (int i = 0; i < 5000 && !release; ++i)
                QThread::msleep(2);
            return qint64(-1);
        };
        PackSnapshotTask task(rig.inst, rig.snaps, rig.meta(), hooks);
        QSignalSpy aborted(&task, &Task::aborted);
        task.start();
        QVERIFY(task.abort());
        release = true;
        QTRY_VERIFY_WITH_TIMEOUT(task.isFinished(), 15000);
        QVERIFY(!task.wasSuccessful());
        QCOMPARE(aborted.count(), 1);
        QVERIFY(task.zipPath().isEmpty());
        QVERIFY(QDir(rig.snaps).entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty());  // no zip, no .part, no sidecar
        QVERIFY(!WorldBackupJobs::isInstanceLocked(S("inst")));
    }

    void tasksRefuseWhileTheGameRunsOrAJobHoldsTheInstance()
    {
        Rig rig;
        const CreateResult snap = rig.snapshot();
        rig.doUpdate();
        const auto before = tree(rig.inst);
        {
            PackRollbackTask task(rig.inst, snap.zipPath, rig.snaps, S("PACKID"), rig.hooks(), []() { return S("Close the game first."); });
            task.start();
            QTRY_VERIFY_WITH_TIMEOUT(task.isFinished(), 5000);
            QVERIFY(!task.wasSuccessful());
            QCOMPARE(task.failReason(), S("Close the game first."));
        }
        {
            PackSnapshotTask task(rig.inst, rig.snaps, rig.meta(), rig.hooks(), []() { return S("Close the game first."); });
            task.start();
            QTRY_VERIFY_WITH_TIMEOUT(task.isFinished(), 5000);
            QVERIFY(!task.wasSuccessful());
        }
        WorldBackupJobs::lockInstance(S("inst"));  // a world backup of the instance is running
        {
            PackRollbackTask task(rig.inst, snap.zipPath, rig.snaps, S("PACKID"), rig.hooks());
            task.start();
            QTRY_VERIFY_WITH_TIMEOUT(task.isFinished(), 5000);
            QVERIFY(!task.wasSuccessful());
        }
        WorldBackupJobs::unlockInstance(S("inst"));
        QCOMPARE(tree(rig.inst), before);
        QCOMPARE(rig.zips().size(), 1);
        QVERIFY(rig.trashed.isEmpty());
        QVERIFY(!WorldBackupJobs::isInstanceLocked(S("inst")));
    }

    void rollbackTaskRestoresTheInstance()
    {
        Rig rig;
        const CreateResult snap = rig.snapshot();
        rig.doUpdate();
        PackRollbackTask task(rig.inst, snap.zipPath, rig.snaps, S("PACKID"), rig.hooks());
        task.start();
        QTRY_VERIFY_WITH_TIMEOUT(task.isFinished(), 15000);
        QVERIFY2(task.wasSuccessful(), qPrintable(task.failReason()));
        QCOMPARE(rig.content(S(".minecraft/mods/a.jar")), QByteArray("a old"));
        QCOMPARE(rig.content(S(".minecraft/saves/World2/level.dat")), QByteArray("world two"));
        QVERIFY(!WorldBackupJobs::isInstanceLocked(S("inst")));
    }

    void worldBackupsStillWorkWithTheNewOptionsDefaults()
    {
        // createBackup() without Options is what worlds use: names, kinds and contents are unchanged
        QTemporaryDir d;
        QVERIFY(writeFile(d.filePath(S("w/level.dat")), "x"));
        QVERIFY(writeFile(d.filePath(S("w/session.lock")), "lock"));
        QVERIFY(writeFile(d.filePath(S("w/region/r.0.0.mca")), "r"));
        const BackupResult r = createBackup(d.filePath(S("w")), d.filePath(S("b")), Kind::Manual, { S("i"), S("I"), S("w"), S("W") });
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(!r.cancelled);
        QVERIFY(QFileInfo(r.zipPath).fileName().endsWith(S("-manual.zip")));
        QVERIFY(verifyBackup(r.zipPath).ok);
        QVERIFY(readBackupInfo(r.zipPath).extra.isEmpty());
        QCOMPARE(readBackupInfo(r.zipPath).totalBytes, qint64(2));
    }
};

QTEST_GUILESS_MAIN(PackSnapshotTest)
#include "PackSnapshot_test.moc"
