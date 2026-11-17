// SPDX-License-Identifier: GPL-3.0-only
// Backup destination: the effective root, cloud-folder detection with a fake environment (OneDrive, Google Drive, Dropbox info.json),
// the writability probe, the fallback to the default root and the persisted list of known roots. Temp dirs only.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include "zero/backup/BackupDestination.h"

using namespace Zero::Backup;

namespace {

/// A fake machine: environment variables, existing directories, file contents and mounted drives.
struct FakePc {
    QHash<QString, QString> vars;
    QSet<QString> dirs;
    QHash<QString, QByteArray> files;
    QStringList drives;

    Environment env() const
    {
        Environment e;
        e.env = [this](const QString& name) { return vars.value(name); };
        e.dirExists = [this](const QString& dir) { return dirs.contains(QDir::cleanPath(dir)); };
        e.readFile = [this](const QString& file) { return files.value(QDir::cleanPath(file)); };
        e.driveRoots = [this]() { return drives; };
        return e;
    }
};

QStringList paths(const QList<CloudFolder>& list)
{
    QStringList out;
    for (const CloudFolder& f : list)
        out << f.label + QLatin1Char('=') + f.path;
    return out;
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

}  // namespace

class BackupDestinationTest : public QObject {
    Q_OBJECT

   private slots:
    // ---------------------------------------------------------------- effective root

    void emptyChoiceIsTheDefaultRoot()
    {
        QCOMPARE(activeRoot("D:/data/zero/backups", {}), QString("D:/data/zero/backups"));
        QCOMPARE(activeRoot("D:/data/zero/backups", "   "), QString("D:/data/zero/backups"));
        QCOMPARE(rootInFolder({}), QString());
    }

    void chosenFolderGetsAZeroBackupsSubfolder()
    {
        QCOMPARE(rootInFolder("C:/Users/me/OneDrive"), QString("C:/Users/me/OneDrive/ZERO Backups"));
        QCOMPARE(rootInFolder("C:/Users/me/OneDrive/"), QString("C:/Users/me/OneDrive/ZERO Backups"));
        QCOMPARE(activeRoot("D:/def", "E:/Cloud"), QString("E:/Cloud/ZERO Backups"));
        QCOMPARE(worldBackupDirIn("E:/Cloud/ZERO Backups", "inst", "World 1"), QString("E:/Cloud/ZERO Backups/inst/World 1"));
    }

    // ---------------------------------------------------------------- cloud detection

    void nothingDetectedOnABarePc()
    {
        QVERIFY(detectCloudFolders(FakePc().env()).isEmpty());
    }

    void oneDriveFromTheEnvironment()
    {
        FakePc pc;
        pc.vars["OneDrive"] = "C:/Users/me/OneDrive";
        pc.vars["OneDriveConsumer"] = "C:/Users/me/OneDriveOther";
        pc.dirs << "C:/Users/me/OneDrive" << "C:/Users/me/OneDriveOther";
        QCOMPARE(paths(detectCloudFolders(pc.env())), QStringList({ "OneDrive=C:/Users/me/OneDrive" }));
    }

    void oneDriveFallsBackToTheConsumerVariable()
    {
        FakePc pc;
        pc.vars["OneDriveConsumer"] = "C:/Users/me/OneDrive";
        pc.dirs << "C:/Users/me/OneDrive";
        QCOMPARE(paths(detectCloudFolders(pc.env())), QStringList({ "OneDrive=C:/Users/me/OneDrive" }));

        // `OneDrive` points to a folder that is gone: the consumer one is used
        pc.vars["OneDrive"] = "C:/Users/me/Gone";
        QCOMPARE(paths(detectCloudFolders(pc.env())), QStringList({ "OneDrive=C:/Users/me/OneDrive" }));

        // neither exists: nothing
        pc.dirs.clear();
        QVERIFY(detectCloudFolders(pc.env()).isEmpty());
    }

    void googleDriveOnAnyMountedDrive()
    {
        FakePc pc;
        pc.drives = { "C:/", "G:/", "H:/" };
        pc.dirs << "G:/My Drive" << "C:/Windows";
        QCOMPARE(paths(detectCloudFolders(pc.env())), QStringList({ "Google Drive=G:/My Drive" }));
    }

    void googleDriveInTheProfileFolder()
    {
        FakePc pc;
        pc.vars["USERPROFILE"] = "C:/Users/me";
        pc.dirs << "C:/Users/me/Google Drive/My Drive";
        QCOMPARE(paths(detectCloudFolders(pc.env())), QStringList({ "Google Drive=C:/Users/me/Google Drive/My Drive" }));

        pc.dirs.clear();
        pc.dirs << "C:/Users/me/My Drive";
        QCOMPARE(paths(detectCloudFolders(pc.env())), QStringList({ "Google Drive=C:/Users/me/My Drive" }));
    }

    void dropboxPersonalAndBusinessFromInfoJson()
    {
        FakePc pc;
        pc.vars["LOCALAPPDATA"] = "C:/Users/me/AppData/Local";
        pc.vars["APPDATA"] = "C:/Users/me/AppData/Roaming";
        pc.files["C:/Users/me/AppData/Local/Dropbox/info.json"] =
            R"json({"personal":{"path":"C:\\Users\\me\\Dropbox","host":1},"business":{"path":"C:\\Users\\me\\Dropbox (Work)"}})json";
        pc.dirs << "C:/Users/me/Dropbox" << "C:/Users/me/Dropbox (Work)";
        QCOMPARE(paths(detectCloudFolders(pc.env())),
                 QStringList({ "Dropbox=C:/Users/me/Dropbox", "Dropbox (business)=C:/Users/me/Dropbox (Work)" }));

        // a folder that does not exist is left out
        pc.dirs.remove("C:/Users/me/Dropbox");
        QCOMPARE(paths(detectCloudFolders(pc.env())), QStringList({ "Dropbox (business)=C:/Users/me/Dropbox (Work)" }));
    }

    void dropboxInfoJsonFromRoamingAppData()
    {
        FakePc pc;
        pc.vars["APPDATA"] = "C:/Users/me/AppData/Roaming";
        pc.files["C:/Users/me/AppData/Roaming/Dropbox/info.json"] = R"({"personal":{"path":"D:\\Dropbox"}})";
        pc.dirs << "D:/Dropbox";
        QCOMPARE(paths(detectCloudFolders(pc.env())), QStringList({ "Dropbox=D:/Dropbox" }));
    }

    void brokenDropboxInfoJsonIsIgnored()
    {
        FakePc pc;
        pc.vars["LOCALAPPDATA"] = "C:/L";
        pc.vars["APPDATA"] = "C:/R";
        pc.files["C:/L/Dropbox/info.json"] = "{ not json";
        pc.files["C:/R/Dropbox/info.json"] = R"({"personal":{"path":"D:\\Dropbox"}})";
        pc.dirs << "D:/Dropbox";
        QCOMPARE(paths(detectCloudFolders(pc.env())), QStringList({ "Dropbox=D:/Dropbox" }));

        pc.files["C:/R/Dropbox/info.json"] = "[]";
        QVERIFY(detectCloudFolders(pc.env()).isEmpty());
    }

    void allThreeAtOnceInAFixedOrderWithoutDuplicates()
    {
        FakePc pc;
        pc.vars["OneDrive"] = "C:/Users/me/OneDrive";
        pc.vars["USERPROFILE"] = "C:/Users/me";
        pc.vars["LOCALAPPDATA"] = "C:/Users/me/AppData/Local";
        pc.drives = { "G:/" };
        pc.files["C:/Users/me/AppData/Local/Dropbox/info.json"] = R"({"personal":{"path":"C:\\Users\\me\\Dropbox"}})";
        pc.dirs << "C:/Users/me/OneDrive" << "G:/My Drive" << "C:/Users/me/Dropbox";
        QCOMPARE(paths(detectCloudFolders(pc.env())),
                 QStringList({ "OneDrive=C:/Users/me/OneDrive", "Google Drive=G:/My Drive", "Dropbox=C:/Users/me/Dropbox" }));

        // the same folder reached twice (profile "My Drive" is also the drive's) is listed once
        pc.vars["USERPROFILE"] = "G:";
        pc.dirs << "G:/My Drive";
        QCOMPARE(detectCloudFolders(pc.env()).size(), 3);
    }

    // ---------------------------------------------------------------- writability and fallback

    void probeAcceptsAWritableFolderAndLeavesNoProbeFile()
    {
        QTemporaryDir tmp;
        const Probe p = probeFolder(tmp.path());
        QVERIFY2(p.ok, qPrintable(p.error));
        const QString root = tmp.filePath("ZERO Backups");
        QVERIFY(QFileInfo(root).isDir());
        QVERIFY(QDir(root).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
    }

    void probeRejectsMissingAndUnusableFolders()
    {
        QTemporaryDir tmp;
        QVERIFY(!probeFolder({}).ok);
        const Probe missing = probeFolder(tmp.filePath("not-there"));
        QVERIFY(!missing.ok);
        QVERIFY(!missing.error.isEmpty());
        QVERIFY(!QFileInfo::exists(tmp.filePath("not-there")));  // the chosen folder itself is never created

        QVERIFY(writeFile(tmp.filePath("a-file"), "x"));
        QVERIFY(!probeFolder(tmp.filePath("a-file")).ok);  // a file is not a folder

        // `ZERO Backups` exists as a plain file: it cannot be used as the root
        QVERIFY(QDir().mkpath(tmp.filePath("blocked")));
        QVERIFY(writeFile(tmp.filePath("blocked/ZERO Backups"), "x"));
        QVERIFY(!probeFolder(tmp.filePath("blocked")).ok);
    }

    void resolveUsesTheChosenFolderWhenItWorks()
    {
        QTemporaryDir tmp;
        const Resolved r = resolveRoot(tmp.filePath("default"), tmp.path());
        QVERIFY(!r.fellBack);
        QCOMPARE(r.root, QDir::cleanPath(tmp.filePath("ZERO Backups")));
        QVERIFY(r.chosenFolder.isEmpty());
    }

    void resolveWithoutAChoiceIsTheDefault()
    {
        QTemporaryDir tmp;
        const Resolved r = resolveRoot(tmp.filePath("default"), {});
        QVERIFY(!r.fellBack);
        QCOMPARE(r.root, tmp.filePath("default"));
        QVERIFY(!QFileInfo::exists(tmp.filePath("default")));  // resolving creates nothing
    }

    void resolveFallsBackToTheDefaultAndSaysWhy()
    {
        QTemporaryDir tmp;
        const QString gone = tmp.filePath("unplugged");
        const Resolved r = resolveRoot(tmp.filePath("default"), gone);
        QVERIFY(r.fellBack);
        QCOMPARE(r.root, tmp.filePath("default"));
        QCOMPARE(r.chosenFolder, QDir::cleanPath(gone));
        QVERIFY(!r.reason.isEmpty());
        const QString message = fallbackMessage(gone);
        QVERIFY(message.startsWith("Saved in the ZERO folder: "));
        QVERIFY(message.contains(QDir::toNativeSeparators(QDir::cleanPath(gone))));
        QVERIFY(message.endsWith("isn't reachable"));
    }

    // ---------------------------------------------------------------- known roots

    void mergeRootsPutsTheDefaultFirstWithoutDuplicates()
    {
        const QStringList roots = mergeRoots("D:/data/zero/backups", { "E:/Cloud/ZERO Backups", "d:/DATA/zero/backups", "", "E:/cloud/zero backups/" },
                                             "F:/Other/ZERO Backups");
        QCOMPARE(roots, QStringList({ "D:/data/zero/backups", "E:/Cloud/ZERO Backups", "F:/Other/ZERO Backups" }));
        QCOMPARE(mergeRoots("D:/def", {}), QStringList({ "D:/def" }));
    }

    void knownRootsPersistAndDeduplicate()
    {
        QTemporaryDir tmp;
        const QString file = tmp.filePath("zero/backup-roots.json");
        {
            KnownRoots known(file);
            QVERIFY(known.list().isEmpty());
            QVERIFY(known.add("E:/Cloud/ZERO Backups"));
            QVERIFY(!known.add("e:/cloud/zero backups"));  // same folder
            QVERIFY(!known.add(""));
            QVERIFY(known.add("F:/Other"));
            QCOMPARE(known.list(), QStringList({ "E:/Cloud/ZERO Backups", "F:/Other" }));
        }
        KnownRoots again(file);
        QCOMPARE(again.list(), QStringList({ "E:/Cloud/ZERO Backups", "F:/Other" }));
    }

    void unreadableKnownRootsFileCountsAsEmpty()
    {
        QTemporaryDir tmp;
        const QString file = tmp.filePath("backup-roots.json");
        QVERIFY(writeFile(file, "{ broken"));
        KnownRoots known(file);
        QVERIFY(known.list().isEmpty());
        QVERIFY(known.add("E:/X"));
        QCOMPARE(KnownRoots(file).list(), QStringList({ "E:/X" }));
    }
};

QTEST_GUILESS_MAIN(BackupDestinationTest)

#include "BackupDestination_test.moc"
