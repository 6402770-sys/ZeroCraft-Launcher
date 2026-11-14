// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the safety net of mod updates (zero/mods/ModSnapshot): the snapshot a mod update starts with and the roll back.
// Real folders in a temp dir; the Recycle Bin is a folder (an injectable trash function), so every "went to the Recycle Bin" is a file
// that can be looked at. Pure: no widgets, no network.
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include "zero/mods/ModSnapshot.h"

using namespace Zero::Mods::Snapshot;

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

struct Rig {
    QTemporaryDir root;
    QString mods = root.filePath(S("mods"));
    QString snaps = root.filePath(S("data/zero/snapshots/inst"));
    QString bin = root.filePath(S("bin"));
    QStringList trashed;     ///< absolute paths handed to the Recycle Bin, in order
    QSet<QString> failTrash; ///< file names the Recycle Bin refuses
    QSet<QString> failRename;///< file names a rename into place refuses
    qint64 now = 1'700'000'000'000;
    qint64 free = -1;
    int binCounter = 0;

    Rig() { QDir().mkpath(mods + S("/.index")); }

    Hooks hooks()
    {
        Hooks h;
        h.trashPath = [this](const QString& path) {
            const QString name = QFileInfo(path).fileName();
            if (failTrash.contains(name))
                return false;
            trashed.append(path);
            QDir().mkpath(bin);
            return QFile::rename(path, bin + QLatin1Char('/') + QString::number(++binCounter) + QLatin1Char('-') + name);
        };
        h.rename = [this](const QString& from, const QString& to) {
            if (failRename.contains(QFileInfo(to).fileName()))
                return false;
            return QFile::rename(from, to);
        };
        h.freeBytes = [this](const QString&) { return free; };
        h.nowMs = [this]() { return now; };
        return h;
    }

    QString p(const QString& rel) const { return mods + QLatin1Char('/') + rel; }
    bool has(const QString& rel) const { return QFileInfo::exists(p(rel)); }
    QByteArray content(const QString& rel) const { return readAll(p(rel)); }
    bool put(const QString& rel, const QByteArray& data) { return writeFile(p(rel), data); }
    QStringList binNames() const { return QDir(bin).entryList(QDir::Files, QDir::Name); }

    /// Sodium (enabled) + its packwiz entry, Lithium (disabled) + its entry, a bystander Iris with an entry.
    void addTrio()
    {
        QVERIFY(put(S("sodium-0.5.8.jar"), "sodium old"));
        QVERIFY(put(S(".index/sodium.pw.toml"), "name = \"Sodium\"\nold"));
        QVERIFY(put(S("lithium-0.12.0.jar.disabled"), "lithium old"));
        QVERIFY(put(S(".index/lithium.pw.toml"), "name = \"Lithium\"\nold"));
        QVERIFY(put(S("iris-1.7.0.jar"), "iris"));
        QVERIFY(put(S(".index/iris.pw.toml"), "name = \"Iris\""));
    }

    QList<ModFiles> sodiumAndLithium() const
    {
        return { { S("Sodium"), S("0.5.8"), S("0.6.0"), { p(S("sodium-0.5.8.jar")), p(S(".index/sodium.pw.toml")) } },
                 { S("Lithium"), S("0.12.0"), S("0.13.0"), { p(S("lithium-0.12.0.jar.disabled")), p(S(".index/lithium.pw.toml")) } } };
    }

    /// What Prism's update does: the old jar goes, the new one arrives, the entry is rewritten.
    void doUpdate()
    {
        QFile::remove(p(S("sodium-0.5.8.jar")));
        QVERIFY(put(S("sodium-0.6.0.jar"), "sodium new"));
        QFile::remove(p(S(".index/sodium.pw.toml")));
        QVERIFY(put(S(".index/sodium.pw.toml"), "name = \"Sodium\"\nnew"));
        QFile::remove(p(S("lithium-0.12.0.jar.disabled")));
        QVERIFY(put(S("lithium-0.13.0.jar.disabled"), "lithium new"));
        QFile::remove(p(S(".index/lithium.pw.toml")));
        QVERIFY(put(S(".index/lithium.pw.toml"), "name = \"Lithium\"\nnew"));
    }

    /// A finished (finalized) snapshot of the trio after doUpdate().
    Info snapshotAfterUpdate()
    {
        addTrio();
        const CreateResult created = createSnapshot(snaps, S("inst"), mods, sodiumAndLithium(), hooks());
        [&]() { QVERIFY2(created.ok, qPrintable(created.error)); }();
        doUpdate();
        const FinalizeResult done = finalizeSnapshot(created.dir, mods, created.before, true);
        [&]() { QVERIFY2(done.ok, qPrintable(done.error)); }();
        return readSnapshot(created.dir);
    }
};

}  // namespace

class ModSnapshotTest : public QObject {
    Q_OBJECT
   private slots:
    // ---- create -----------------------------------------------------------------------------------------------------------------

    void createCopiesTheRightFilesWithTheirHashes()
    {
        Rig rig;
        rig.addTrio();
        const CreateResult created = createSnapshot(rig.snaps, S("inst"), rig.mods, rig.sodiumAndLithium(), rig.hooks());
        QVERIFY2(created.ok, qPrintable(created.error));
        QVERIFY(created.dir.endsWith(QDir(rig.snaps).entryList(QDir::Dirs | QDir::NoDotAndDotDot).first()));
        QCOMPARE(created.before.size(), 6);

        const Info info = readSnapshot(created.dir);
        QVERIFY2(info.valid, qPrintable(info.error));
        QCOMPARE(info.instanceId, S("inst"));
        QCOMPARE(info.kind, S("mods-update"));
        QCOMPARE(info.outcome, QString());  // not finished yet
        QCOMPARE(info.files.size(), 4);     // the bystander Iris is not copied
        QSet<QString> paths;
        for (const FileRef& f : info.files) {
            paths.insert(f.path);
            QCOMPARE(qint64(readAll(created.dir + S("/files/") + f.path).size()), f.size);
            QCOMPARE(f.sha256.size(), 64);
        }
        QVERIFY(paths.contains(S("sodium-0.5.8.jar")));
        QVERIFY(paths.contains(S("lithium-0.12.0.jar.disabled")));  // a disabled mod
        QVERIFY(paths.contains(S(".index/sodium.pw.toml")));
        QVERIFY(paths.contains(S(".index/lithium.pw.toml")));
        QVERIFY(!paths.contains(S("iris-1.7.0.jar")));
        QCOMPARE(info.mods.size(), 2);
        QCOMPARE(info.mods.first().name, S("Sodium"));
        QCOMPARE(info.mods.first().oldVersion, S("0.5.8"));
        QCOMPARE(info.mods.first().newVersion, S("0.6.0"));
        QCOMPARE(info.mods.first().files.size(), 2);
        // the mods folder is untouched, and no `.part` file is left
        QCOMPARE(rig.content(S("sodium-0.5.8.jar")), QByteArray("sodium old"));
        QVERIFY(QDir(created.dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot).contains(S("files")));
        QCOMPARE(QDir(rig.snaps).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 1);
        QDirIterator it(created.dir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            QVERIFY(!it.next().endsWith(S(".part")));
    }

    void aModWithoutAnEntryStillGetsItsJar()
    {
        Rig rig;
        QVERIFY(rig.put(S("plain.jar"), "plain"));
        const CreateResult created = createSnapshot(
            rig.snaps, S("inst"), rig.mods, { { S("Plain"), S("1"), S("2"), { rig.p(S("plain.jar")), rig.p(S(".index/plain.pw.toml")) } } }, rig.hooks());
        QVERIFY2(created.ok, qPrintable(created.error));
        QCOMPARE(readSnapshot(created.dir).files.size(), 1);
    }

    void createRefusesWhatItCannotProtect()
    {
        Rig rig;
        rig.addTrio();
        // a file outside the mods folder
        QVERIFY(rig.put(S("../outside.jar"), "x"));
        CreateResult r = createSnapshot(rig.snaps, S("inst"), rig.mods, { { S("X"), {}, {}, { rig.p(S("../outside.jar")) } } }, rig.hooks());
        QVERIFY(!r.ok);
        // a deeper path
        QVERIFY(rig.put(S("sub/deep.jar"), "x"));
        r = createSnapshot(rig.snaps, S("inst"), rig.mods, { { S("X"), {}, {}, { rig.p(S("sub/deep.jar")) } } }, rig.hooks());
        QVERIFY(!r.ok);
        // a mod whose file is not there
        r = createSnapshot(rig.snaps, S("inst"), rig.mods, { { S("Ghost"), {}, {}, { rig.p(S("ghost.jar")) } } }, rig.hooks());
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(S("Ghost")));
        // nothing at all
        QVERIFY(!createSnapshot(rig.snaps, S("inst"), rig.mods, {}, rig.hooks()).ok);
        QVERIFY(!QDir(rig.snaps).exists() || QDir(rig.snaps).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
    }

    void createFailsWhenTheDiskIsFull()
    {
        Rig rig;
        rig.addTrio();
        rig.free = 100;  // less than the slack
        const CreateResult r = createSnapshot(rig.snaps, S("inst"), rig.mods, rig.sodiumAndLithium(), rig.hooks());
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
        QVERIFY(QDir(rig.snaps).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
        QCOMPARE(rig.content(S("sodium-0.5.8.jar")), QByteArray("sodium old"));
    }

    void createFailsWhenTheSnapshotFolderCannotBeMade()
    {
        Rig rig;
        rig.addTrio();
        QVERIFY(rig.put(S("../blocker"), "a file where the folder should go"));
        const CreateResult r = createSnapshot(rig.root.filePath(S("blocker/inst")), S("inst"), rig.mods, rig.sodiumAndLithium(), rig.hooks());
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
    }

    void twoSnapshotsInTheSameSecondGetDifferentFolders()
    {
        Rig rig;
        rig.addTrio();
        const CreateResult a = createSnapshot(rig.snaps, S("inst"), rig.mods, rig.sodiumAndLithium(), rig.hooks());
        const CreateResult b = createSnapshot(rig.snaps, S("inst"), rig.mods, rig.sodiumAndLithium(), rig.hooks());
        QVERIFY(a.ok && b.ok);
        QVERIFY(a.dir != b.dir);
        QVERIFY(b.dir.endsWith(S("-2")));
        QCOMPARE(listSnapshots(rig.snaps).first().dir, QDir::cleanPath(b.dir));  // the -2 one is the newer
    }

    // ---- finalize ---------------------------------------------------------------------------------------------------------------

    void finalizeRecordsWhatWasAdded()
    {
        Rig rig;
        rig.addTrio();
        const CreateResult created = createSnapshot(rig.snaps, S("inst"), rig.mods, rig.sodiumAndLithium(), rig.hooks());
        rig.doUpdate();
        const FinalizeResult done = finalizeSnapshot(created.dir, rig.mods, created.before, true);
        QVERIFY2(done.ok, qPrintable(done.error));
        QVERIFY(!done.pruned);
        QCOMPARE(done.changedMods, 2);
        const Info info = readSnapshot(created.dir);
        QCOMPARE(info.outcome, S("ok"));
        QCOMPARE(QSet<QString>(info.added.begin(), info.added.end()), QSet<QString>({ S("sodium-0.6.0.jar"), S("lithium-0.13.0.jar.disabled") }));
        QCOMPARE(info.changedCount(), 2);
        QVERIFY(info.mods.first().changed);
        // the text of the strip
        QVERIFY(noticeText(info, info.createdMs + 2 * 3600 * 1000).contains(S("Updated 2 mods")));
        QVERIFY(noticeText(info, info.createdMs + 2 * 3600 * 1000).contains(S("2 h ago")));
    }

    void aPartialUpdateIsRecordedAsFailedAndCountsOnlyTheChangedMods()
    {
        Rig rig;
        rig.addTrio();
        const CreateResult created = createSnapshot(rig.snaps, S("inst"), rig.mods, rig.sodiumAndLithium(), rig.hooks());
        // only Sodium got updated
        QFile::remove(rig.p(S("sodium-0.5.8.jar")));
        QVERIFY(rig.put(S("sodium-0.6.0.jar"), "sodium new"));
        const FinalizeResult done = finalizeSnapshot(created.dir, rig.mods, created.before, false);
        QVERIFY(done.ok);
        QCOMPARE(done.changedMods, 1);
        QCOMPARE(done.changedNames, QStringList({ S("Sodium") }));
        const Info info = readSnapshot(created.dir);
        QCOMPARE(info.outcome, S("failed"));
        QVERIFY(!info.mods.at(1).changed);
    }

    void anUpdateThatChangedNothingPrunesTheSnapshot()
    {
        Rig rig;
        rig.addTrio();
        const CreateResult created = createSnapshot(rig.snaps, S("inst"), rig.mods, rig.sodiumAndLithium(), rig.hooks());
        QVERIFY(QFileInfo(created.dir).isDir());
        const FinalizeResult done = finalizeSnapshot(created.dir, rig.mods, created.before, false);
        QVERIFY(done.ok);
        QVERIFY(done.pruned);
        QVERIFY(!QFileInfo::exists(created.dir));
        QVERIFY(newestActive(rig.snaps).dir.isEmpty());
        QCOMPARE(rig.content(S("sodium-0.5.8.jar")), QByteArray("sodium old"));  // the user's files are never part of the pruning
    }

    void aFileReplacedUnderTheSameNameIsAChange()
    {
        Rig rig;
        rig.addTrio();
        const CreateResult created = createSnapshot(rig.snaps, S("inst"), rig.mods, rig.sodiumAndLithium(), rig.hooks());
        QVERIFY(rig.put(S(".index/sodium.pw.toml"), "name = \"Sodium\"\nrewritten"));  // same name, same listing
        const FinalizeResult done = finalizeSnapshot(created.dir, rig.mods, created.before, true);
        QVERIFY(done.ok);
        QVERIFY(!done.pruned);
        QCOMPARE(done.changedMods, 1);
        QVERIFY(readSnapshot(created.dir).added.isEmpty());
    }

    // ---- roll back --------------------------------------------------------------------------------------------------------------

    void rollBackBringsTheOldFilesBackAndTrashesTheNewOnes()
    {
        Rig rig;
        const Info info = rig.snapshotAfterUpdate();
        QVERIFY(rig.has(S("sodium-0.6.0.jar")));
        const RollbackResult r = rollback(info, rig.mods, rig.hooks());
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.restored, 4);
        QCOMPARE(r.trashed, 4);  // the 2 new jars and the 2 rewritten entries
        QCOMPARE(rig.content(S("sodium-0.5.8.jar")), QByteArray("sodium old"));
        QCOMPARE(rig.content(S("lithium-0.12.0.jar.disabled")), QByteArray("lithium old"));  // still disabled
        QCOMPARE(rig.content(S(".index/sodium.pw.toml")), QByteArray("name = \"Sodium\"\nold"));
        QCOMPARE(rig.content(S(".index/lithium.pw.toml")), QByteArray("name = \"Lithium\"\nold"));
        QVERIFY(!rig.has(S("sodium-0.6.0.jar")));
        QVERIFY(!rig.has(S("lithium-0.13.0.jar.disabled")));
        QCOMPARE(rig.content(S("iris-1.7.0.jar")), QByteArray("iris"));  // the bystander is untouched
        // the new files are in the Recycle Bin, not deleted
        QVERIFY(rig.binNames().join(QLatin1Char(' ')).contains(S("sodium-0.6.0.jar")));
        QVERIFY(rig.binNames().join(QLatin1Char(' ')).contains(S("lithium-0.13.0.jar.disabled")));
        // no staging folder left, the snapshot is marked
        QVERIFY(QDir(rig.mods).entryList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).contains(S(".zero-rollback-1")) == false);
        QVERIFY(readSnapshot(info.dir).rolledBack);
        QVERIFY(newestActive(rig.snaps).dir.isEmpty());
        // a second roll back is refused
        QVERIFY(!rollback(readSnapshot(info.dir), rig.mods, rig.hooks()).ok);
    }

    void aRestoredPathThatIsOccupiedGoesToTheBinFirst()
    {
        Rig rig;
        const Info info = rig.snapshotAfterUpdate();
        // The user put another file where the old Sodium jar goes.
        QVERIFY(rig.put(S("sodium-0.5.8.jar"), "something else"));
        const RollbackResult r = rollback(info, rig.mods, rig.hooks());
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(rig.content(S("sodium-0.5.8.jar")), QByteArray("sodium old"));
        QVERIFY(rig.trashed.contains(rig.p(S("sodium-0.5.8.jar"))));
        bool found = false;
        for (const QString& name : rig.binNames())
            found = found || readAll(rig.bin + QLatin1Char('/') + name) == "something else";
        QVERIFY(found);  // never overwritten in place: the user's file is in the bin
    }

    void aFileAlreadyInPlaceIsLeftAlone()
    {
        Rig rig;
        rig.addTrio();
        const CreateResult created = createSnapshot(rig.snaps, S("inst"), rig.mods, rig.sodiumAndLithium(), rig.hooks());
        // only Sodium is updated; Lithium was never touched
        QFile::remove(rig.p(S("sodium-0.5.8.jar")));
        QVERIFY(rig.put(S("sodium-0.6.0.jar"), "sodium new"));
        QVERIFY(finalizeSnapshot(created.dir, rig.mods, created.before, false).ok);
        const RollbackResult r = rollback(readSnapshot(created.dir), rig.mods, rig.hooks());
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.restored, 1);
        QVERIFY(!rig.trashed.contains(rig.p(S("lithium-0.12.0.jar.disabled"))));
        QVERIFY(!rig.trashed.contains(rig.p(S(".index/lithium.pw.toml"))));
        QCOMPARE(rig.content(S("sodium-0.5.8.jar")), QByteArray("sodium old"));
        QVERIFY(!rig.has(S("sodium-0.6.0.jar")));
    }

    void anAddedFileTheUserAlreadyRemovedIsSkipped()
    {
        Rig rig;
        const Info info = rig.snapshotAfterUpdate();
        QFile::remove(rig.p(S("sodium-0.6.0.jar")));  // the user removed the new jar after the update
        const RollbackResult r = rollback(info, rig.mods, rig.hooks());
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(!rig.trashed.contains(rig.p(S("sodium-0.6.0.jar"))));
        QCOMPARE(rig.content(S("sodium-0.5.8.jar")), QByteArray("sodium old"));
    }

    void aDamagedSnapshotIsRefusedAndNothingIsTouched()
    {
        Rig rig;
        const Info info = rig.snapshotAfterUpdate();
        QVERIFY(writeFile(info.dir + S("/files/lithium-0.12.0.jar.disabled"), "tampered"));
        const RollbackResult r = rollback(info, rig.mods, rig.hooks());
        QVERIFY(!r.ok);
        QVERIFY(!r.touched);
        QVERIFY(r.error.contains(S("lithium-0.12.0.jar.disabled")));
        QVERIFY(rig.trashed.isEmpty());
        QVERIFY(rig.has(S("sodium-0.6.0.jar")));
        QVERIFY(!rig.has(S("sodium-0.5.8.jar")));
        QVERIFY(!readSnapshot(info.dir).rolledBack);
        // a missing copy is the same
        QVERIFY(QFile::remove(info.dir + S("/files/sodium-0.5.8.jar")));
        QVERIFY(!rollback(info, rig.mods, rig.hooks()).ok);
        QVERIFY(rig.trashed.isEmpty());
    }

    void aTrashFailureStopsAndKeepsTheSnapshotAndASecondTryFinishes()
    {
        Rig rig;
        const Info info = rig.snapshotAfterUpdate();
        // jars go first, then entries: make the 2nd file (lithium's new jar sorts before the sodium one) fail
        rig.failTrash.insert(S("sodium-0.6.0.jar"));
        const RollbackResult r = rollback(info, rig.mods, rig.hooks());
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(S("sodium-0.6.0.jar")));
        QVERIFY(r.error.contains(S("Roll back again")));
        QVERIFY(r.touched);  // lithium-0.13.0 had already gone
        QVERIFY(r.error.contains(S("lithium-0.13.0.jar.disabled")));
        QVERIFY(rig.has(S("sodium-0.6.0.jar")));          // the failing file stays where it is
        QVERIFY(!rig.has(S("sodium-0.5.8.jar")));         // and nothing more was restored
        QVERIFY(!QDir(rig.mods).entryList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).contains(S(".zero-rollback-1")));
        QVERIFY(QFileInfo::exists(info.dir + S("/snapshot.json")));
        QVERIFY(!readSnapshot(info.dir).rolledBack);
        QVERIFY(newestActive(rig.snaps).valid);  // still offered

        // the bin works again: the second click finishes the job (the file that is gone is skipped)
        rig.failTrash.clear();
        const RollbackResult again = rollback(readSnapshot(info.dir), rig.mods, rig.hooks());
        QVERIFY2(again.ok, qPrintable(again.error));
        QCOMPARE(rig.content(S("sodium-0.5.8.jar")), QByteArray("sodium old"));
        QCOMPARE(rig.content(S("lithium-0.12.0.jar.disabled")), QByteArray("lithium old"));
        QVERIFY(!rig.has(S("sodium-0.6.0.jar")));
        QVERIFY(readSnapshot(info.dir).rolledBack);
    }

    void aTrashFailureOnTheFirstFileChangesNothing()
    {
        Rig rig;
        const Info info = rig.snapshotAfterUpdate();
        rig.failTrash.insert(S("lithium-0.13.0.jar.disabled"));  // the first one it tries
        const RollbackResult r = rollback(info, rig.mods, rig.hooks());
        QVERIFY(!r.ok);
        QVERIFY(!r.touched);
        QVERIFY(rig.trashed.isEmpty());
        QVERIFY(r.error.contains(S("Nothing in the mods folder was changed")));
        QVERIFY(rig.has(S("lithium-0.13.0.jar.disabled")));
    }

    void aFailedRenameStopsAndReportsTheState()
    {
        Rig rig;
        const Info info = rig.snapshotAfterUpdate();
        rig.failRename.insert(S("sodium-0.5.8.jar"));
        const RollbackResult r = rollback(info, rig.mods, rig.hooks());
        QVERIFY(!r.ok);
        QVERIFY(r.touched);
        QVERIFY(r.error.contains(S("sodium-0.5.8.jar")));
        QVERIFY(!readSnapshot(info.dir).rolledBack);
        QVERIFY(!QDir(rig.mods).entryList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).contains(S(".zero-rollback-1")));
        // second try
        rig.failRename.clear();
        QVERIFY2(rollback(readSnapshot(info.dir), rig.mods, rig.hooks()).ok, "second try");
        QCOMPARE(rig.content(S("sodium-0.5.8.jar")), QByteArray("sodium old"));
    }

    /// An update may write the packwiz entry under another name (and Prism then leaves the old one or removes it): the entry the mod had is
    /// copied, the new one is only recorded as added, and the roll back ends with the old entry and without the new one.
    void aRenamedEntryComesBackUnderItsOldNameAndTheNewOneGoesToTheBin()
    {
        Rig rig;
        rig.addTrio();
        const CreateResult created = createSnapshot(rig.snaps, S("inst"), rig.mods, rig.sodiumAndLithium(), rig.hooks());
        QVERIFY2(created.ok, qPrintable(created.error));
        QFile::remove(rig.p(S("sodium-0.5.8.jar")));
        QVERIFY(rig.put(S("sodium-0.6.0.jar"), "sodium new"));
        QFile::remove(rig.p(S(".index/sodium.pw.toml")));  // the old entry goes ...
        QVERIFY(rig.put(S(".index/sodium-fabric.pw.toml"), "name = \"Sodium\"\nnew"));  // ... the new one has another name
        QVERIFY(finalizeSnapshot(created.dir, rig.mods, created.before, true).ok);
        const Info info = readSnapshot(created.dir);
        QVERIFY(info.valid);
        QVERIFY(info.added.contains(S(".index/sodium-fabric.pw.toml")));
        QVERIFY(info.mods.first().changed);

        const RollbackResult r = rollback(info, rig.mods, rig.hooks());
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(rig.content(S("sodium-0.5.8.jar")), QByteArray("sodium old"));
        QCOMPARE(rig.content(S(".index/sodium.pw.toml")), QByteArray("name = \"Sodium\"\nold"));
        QVERIFY(!rig.has(S(".index/sodium-fabric.pw.toml")));
        QVERIFY(!rig.has(S("sodium-0.6.0.jar")));
        QVERIFY(rig.trashed.contains(rig.p(S(".index/sodium-fabric.pw.toml"))));
    }

    void aFolderWhereAFileShouldGoIsRefused()
    {
        Rig rig;
        const Info info = rig.snapshotAfterUpdate();
        QVERIFY(QDir().mkpath(rig.p(S("sodium-0.5.8.jar"))));
        const RollbackResult r = rollback(info, rig.mods, rig.hooks());
        QVERIFY(!r.ok);
        QVERIFY(!r.touched);
        QVERIFY(rig.trashed.isEmpty());
    }

    void notEnoughSpaceForTheStagingRefusesBeforeTouchingAnything()
    {
        Rig rig;
        const Info info = rig.snapshotAfterUpdate();
        rig.free = 10;
        const RollbackResult r = rollback(info, rig.mods, rig.hooks());
        QVERIFY(!r.ok);
        QVERIFY(!r.touched);
        QVERIFY(rig.trashed.isEmpty());
    }

    void thereIsNoPermanentDeleteInTheDefaultTrash()
    {
        // The default trash hook is FS::trash only; here the hook returns false for everything: nothing may disappear.
        Rig rig;
        const Info info = rig.snapshotAfterUpdate();
        Hooks h = rig.hooks();
        h.trashPath = [](const QString&) { return false; };
        const RollbackResult r = rollback(info, rig.mods, h);
        QVERIFY(!r.ok);
        QVERIFY(rig.has(S("sodium-0.6.0.jar")));
        QVERIFY(rig.has(S("lithium-0.13.0.jar.disabled")));
        QVERIFY(rig.has(S(".index/sodium.pw.toml")));
        QVERIFY(rig.binNames().isEmpty());
    }

    // ---- reading, retention, lookups -------------------------------------------------------------------------------------------

    void aSnapshotWithAnUnsafePathIsNotValid()
    {
        Rig rig;
        const Info info = rig.snapshotAfterUpdate();
        QFile f(info.dir + S("/snapshot.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        QJsonArray added = root.value(S("added")).toArray();
        added.append(S("../../Windows/evil.dll"));
        root[S("added")] = added;
        QVERIFY(writeFile(info.dir + S("/snapshot.json"), QJsonDocument(root).toJson()));
        const Info tampered = readSnapshot(info.dir);
        QVERIFY(!tampered.valid);
        QVERIFY(!rollback(tampered, rig.mods, rig.hooks()).ok);
        QVERIFY(rig.trashed.isEmpty());
    }

    void retentionKeepsTheNewestFive()
    {
        Rig rig;
        rig.addTrio();
        QStringList dirs;
        for (int i = 0; i < 8; ++i) {
            rig.now += 60'000;  // a minute apart
            const CreateResult c = createSnapshot(rig.snaps, S("inst"), rig.mods, rig.sodiumAndLithium(), rig.hooks());
            QVERIFY2(c.ok, qPrintable(c.error));
            dirs.append(c.dir);
        }
        QCOMPARE(listSnapshots(rig.snaps).size(), 5);
        QVERIFY(QFileInfo(dirs.last()).isDir());
        QVERIFY(QFileInfo(dirs.at(3)).isDir());
        QVERIFY(!QFileInfo(dirs.at(2)).exists());
        QVERIFY(!QFileInfo(dirs.first()).exists());
        // the mods folder is never part of it
        QCOMPARE(rig.content(S("sodium-0.5.8.jar")), QByteArray("sodium old"));
    }

    void retentionLeavesForeignFoldersAndOtherInstancesAlone()
    {
        Rig rig;
        rig.addTrio();
        QVERIFY(writeFile(rig.snaps + S("/notes/readme.txt"), "mine"));
        QVERIFY(writeFile(rig.root.filePath(S("data/zero/snapshots/other/20200101-000000/snapshot.json")), "{}"));
        for (int i = 0; i < 7; ++i) {
            rig.now += 60'000;
            QVERIFY(createSnapshot(rig.snaps, S("inst"), rig.mods, rig.sodiumAndLithium(), rig.hooks()).ok);
        }
        QVERIFY(QFileInfo::exists(rig.snaps + S("/notes/readme.txt")));
        QVERIFY(QFileInfo::exists(rig.root.filePath(S("data/zero/snapshots/other/20200101-000000/snapshot.json"))));
        QCOMPARE(listSnapshots(rig.snaps).size(), 5);
    }

    void newestActiveLooksOnlyAtTheNewest()
    {
        Rig rig;
        rig.addTrio();
        // an older finished snapshot
        CreateResult a = createSnapshot(rig.snaps, S("inst"), rig.mods, rig.sodiumAndLithium(), rig.hooks());
        rig.doUpdate();
        QVERIFY(finalizeSnapshot(a.dir, rig.mods, a.before, true).ok);
        QCOMPARE(newestActive(rig.snaps).dir, QDir::cleanPath(a.dir));
        // a newer one that is still running (no outcome): hides the older
        rig.now += 60'000;
        CreateResult b = createSnapshot(rig.snaps, S("inst"), rig.mods, rig.sodiumAndLithium(), rig.hooks());
        QVERIFY(b.ok);
        QVERIFY(newestActive(rig.snaps).dir.isEmpty());
        // finished: it is the one
        QFile::remove(rig.p(S("sodium-0.6.0.jar")));
        QVERIFY(rig.put(S("sodium-0.7.0.jar"), "x"));
        QVERIFY(finalizeSnapshot(b.dir, rig.mods, b.before, true).ok);
        QCOMPARE(newestActive(rig.snaps).dir, QDir::cleanPath(b.dir));
        // dismissed: nothing, and the older one is not offered behind it
        QVERIFY(markDismissed(b.dir));
        QVERIFY(readSnapshot(b.dir).dismissed);
        QVERIFY(newestActive(rig.snaps).dir.isEmpty());
        QVERIFY(QFileInfo(b.dir + S("/snapshot.json")).exists());  // it stays for the retention
    }

    void listFolderListsFilesOfTheFolderAndTheIndex()
    {
        Rig rig;
        rig.addTrio();
        QVERIFY(QDir().mkpath(rig.p(S("config"))));
        QVERIFY(rig.put(S("config/x.txt"), "x"));
        const QStringList list = listFolder(rig.mods);
        QCOMPARE(list.size(), 6);
        QVERIFY(list.contains(S(".index/sodium.pw.toml")));
        QVERIFY(list.contains(S("lithium-0.12.0.jar.disabled")));
        QVERIFY(!list.contains(S("config/x.txt")));
    }

    void ageTextReadsLikeAPerson()
    {
        const qint64 t = 1'700'000'000'000;
        QCOMPARE(ageText(t, t + 5'000), S("just now"));
        QCOMPARE(ageText(t, t + 5 * 60'000), S("5 min ago"));
        QCOMPARE(ageText(t, t + 2 * 3600'000), S("2 h ago"));
        QCOMPARE(ageText(t, t + 3 * 86400'000), S("3 d ago"));
        QVERIFY(!ageText(t, t + 90LL * 86400'000).contains(S("ago")));
        QCOMPARE(ageText(t, t - 1000), S("just now"));  // a clock that went back
    }
};

QTEST_GUILESS_MAIN(ModSnapshotTest)
#include "ModSnapshot_test.moc"
