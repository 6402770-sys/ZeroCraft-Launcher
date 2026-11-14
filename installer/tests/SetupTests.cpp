// SPDX-License-Identifier: GPL-3.0-only
// Unit tests of the ZERO Setup core. Everything runs in a temp folder against MemorySystem: no real registry, shortcut
// or Start menu access, and no install into a real location.
#include "Brand.h"
#include "CmdLine.h"
#include "Footprint.h"
#include "Installer.h"
#include "Jobs.h"
#include "MemorySystem.h"
#include "Payload.h"
#include "SafePath.h"
#include "Uninstaller.h"
#include "Upgrade.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

using namespace ZeroSetup;

namespace {

void writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
    f.write(data);
}

QByteArray readFile(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<missing>");
}

QByteArray randomBytes(int n, quint32 seed)
{
    QRandomGenerator g(seed);
    QByteArray b(n, 0);
    for (int i = 0; i < n; ++i)
        b[i] = char(g.bounded(256));
    return b;
}

// "relative path" -> contents of a small fake launcher folder (includes an empty file, nested folders, a >1 MiB file)
QMap<QString, QByteArray> fakeStage()
{
    QMap<QString, QByteArray> m;
    m["zerolauncher.exe"] = "MZ-fake-exe";
    m["zerolauncher_updater.exe"] = "MZ-updater";
    m["zerolauncher_filelink.exe"] = "MZ-filelink";
    m["qt.conf"] = "[Paths]\n";
    m["Qt6Core.dll"] = randomBytes(2'500'000, 7);  // several 1 MiB chunks
    m["libfoo.dll"] = QByteArray(100'000, 'x');    // compresses well
    m["platforms/qwindows.dll"] = randomBytes(5000, 9);
    m["jars/deep/er/launcher.jar"] = "jar";
    m["multimedia/empty.bin"] = QByteArray();
    return m;
}

void writeStage(const QString& dir, const QMap<QString, QByteArray>& files)
{
    for (auto it = files.begin(); it != files.end(); ++it)
        writeFile(dir + '/' + it.key(), it.value());
}

// Folder snapshot: relative path -> content (to prove "untouched").
QMap<QString, QByteArray> snapshot(const QString& dir)
{
    QMap<QString, QByteArray> m;
    QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        m[QDir(dir).relativeFilePath(it.filePath())] = readFile(it.filePath());
    }
    return m;
}

// A payload built by hand so a test can put hostile paths into the manifest. Chunks are valid.
QByteArray rawPayloadExe(const QByteArray& stub, const QJsonArray& files, const QMap<QString, QByteArray>& content)
{
    QJsonObject root;
    root["format"] = 1;
    root["version"] = "9.9.9";
    root["files"] = files;
    const QByteArray manifest = QJsonDocument(root).toJson(QJsonDocument::Compact);
    QByteArray payload("ZSP1", 4);
    QByteArray len(4, 0);
    qToLittleEndian<quint32>(manifest.size(), len.data());
    payload += len + manifest;
    for (const QJsonValue& v : files) {
        const QByteArray data = content.value(v.toObject().value("path").toString());
        if (data.isEmpty())
            continue;
        const QByteArray comp = qCompress(data, 6);
        QByteArray cl(4, 0);
        qToLittleEndian<quint32>(comp.size(), cl.data());
        payload += cl + comp;
    }
    return stub + payload + makeTrailer(stub.size(), payload.size(), QCryptographicHash::hash(payload, QCryptographicHash::Sha256));
}

QJsonObject entry(const QString& path, const QByteArray& data)
{
    QJsonObject o;
    o["path"] = path;
    o["size"] = double(data.size());
    o["sha256"] = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
    return o;
}

}  // namespace

class SetupTests : public QObject
{
    Q_OBJECT

    QTemporaryDir m_tmp;
    QString m_stub;      // fake "setup stub" exe
    QString m_packed;    // stub + payload of fakeStage()
    QMap<QString, QByteArray> m_stage;

    QString sub(const QString& name) const { return m_tmp.path() + '/' + name; }

   private slots:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        m_stub = sub("stub.exe");
        writeFile(m_stub, randomBytes(30'000, 1));
        m_stage = fakeStage();
        writeStage(sub("stage"), m_stage);
        m_packed = sub("Setup.exe");
        QString err;
        QVERIFY2(packPayload(m_stub, sub("stage"), "0.2.0", m_packed, &err), qPrintable(err));
    }

    // ------------------------------------------------------------------ paths
    void safePaths_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<bool>("safe");
        QTest::newRow("plain") << "zerolauncher.exe" << true;
        QTest::newRow("nested") << "platforms/qwindows.dll" << true;
        QTest::newRow("dotdot") << "../evil.dll" << false;
        QTest::newRow("dotdot mid") << "a/../../b" << false;
        QTest::newRow("absolute") << "/etc/passwd" << false;
        QTest::newRow("drive") << "C:/Windows/x.dll" << false;
        QTest::newRow("drive rel") << "C:x.dll" << false;
        QTest::newRow("stream") << "a.txt:ads" << false;
        QTest::newRow("backslash") << "a\\b.dll" << false;
        QTest::newRow("unc") << "//server/share/x" << false;
        QTest::newRow("empty seg") << "a//b" << false;
        QTest::newRow("dot seg") << "a/./b" << false;
        QTest::newRow("trailing dot") << "a/b." << false;
        QTest::newRow("device") << "con" << false;
        QTest::newRow("device ext") << "nul.txt" << false;
        QTest::newRow("empty") << "" << false;
    }
    void safePaths()
    {
        QFETCH(QString, path);
        QFETCH(bool, safe);
        QCOMPARE(isSafeRelativePath(path), safe);
    }

    void hostilePayloadsAreRejected_data()
    {
        QTest::addColumn<QString>("path");
        for (const char* p : { "../evil.txt", "/abs.txt", "C:/x.txt", "a/../../b.txt", "a\\b.txt", "con", "a:b" })
            QTest::newRow(p) << QString::fromLatin1(p);
    }
    void hostilePayloadsAreRejected()
    {
        QFETCH(QString, path);
        const QByteArray data = "evil";
        QJsonArray files;
        files.append(entry(path, data));
        const QString exe = sub("hostile.exe");
        writeFile(exe, rawPayloadExe(randomBytes(1000, 2), files, { { path, data } }));
        PayloadReader r;
        QString err;
        QCOMPARE(r.open(exe, &err), OpenStatus::Corrupt);
        QVERIFY(!err.isEmpty());
    }
    void duplicateAndConflictingPathsAreRejected()
    {
        QJsonArray dup;
        dup.append(entry("a.txt", "1"));
        dup.append(entry("A.TXT", "2"));  // same file on Windows
        writeFile(sub("dup.exe"), rawPayloadExe("stub-bytes-0123456789", dup, {}));
        PayloadReader r;
        QCOMPARE(r.open(sub("dup.exe"), nullptr), OpenStatus::Corrupt);
        QJsonArray conflict;
        conflict.append(entry("a", "1"));
        conflict.append(entry("a/b.txt", "2"));  // "a" is a file and a folder
        writeFile(sub("conflict.exe"), rawPayloadExe("stub-bytes-0123456789", conflict, {}));
        QCOMPARE(r.open(sub("conflict.exe"), nullptr), OpenStatus::Corrupt);
    }

    // ------------------------------------------------------------------ payload
    void roundTrip()
    {
        PayloadReader r;
        QString err;
        QCOMPARE(r.open(m_packed, &err), OpenStatus::Ok);
        QCOMPARE(r.info().version, QString("0.2.0"));
        QCOMPARE(int(r.info().files.size()), int(m_stage.size()));
        QCOMPARE(r.info().stubSize, quint64(30'000));
        QVERIFY2(r.verifyHash(nullptr, &err), qPrintable(err));

        QVERIFY(QDir().mkpath(sub("rt-out")));
        quint64 last = 0;
        QVERIFY2(r.extractAll(sub("rt-out"), nullptr, [&](quint64 d) { QVERIFY(d >= last); last = d; }, &err), qPrintable(err));
        QCOMPARE(last, r.info().totalSize);
        QCOMPARE(snapshot(sub("rt-out")), m_stage);
        // the stub bytes in front of the payload are untouched
        QCOMPARE(readFile(m_packed).left(30'000), readFile(m_stub));
    }
    void packTwiceIsRefused()
    {
        QString err;
        QVERIFY(!packPayload(m_packed, sub("stage"), "0.2.0", sub("again.exe"), &err));
        QVERIFY(!err.isEmpty());
    }
    void corruptByteIsRejected()
    {
        const QByteArray good = readFile(m_packed);
        for (qsizetype pos : { qsizetype(30'000 + 2000), good.size() / 2, good.size() - 80 }) {
            QByteArray bad = good;
            bad[pos] = char(bad[pos] ^ 0x5A);
            const QString exe = sub("corrupt.exe");
            writeFile(exe, bad);
            PayloadReader r;
            QString err;
            const OpenStatus st = r.open(exe, &err);  // the manifest may be the damaged part
            if (st == OpenStatus::Ok)
                QVERIFY2(!r.verifyHash(nullptr, &err), "corrupt payload passed the checksum");
            else
                QVERIFY(!err.isEmpty());
        }
    }
    void truncatedFileIsRejected()
    {
        const QByteArray good = readFile(m_packed);
        for (int cut : { 1, 10, 63, 64, 65, 5000 }) {
            const QString exe = sub("trunc.exe");
            writeFile(exe, good.left(good.size() - cut));
            PayloadReader r;
            QVERIFY2(r.open(exe, nullptr) != OpenStatus::Ok, qPrintable(QString::number(cut)));
        }
        // trailer that points outside the file
        QByteArray bad = good;
        qToLittleEndian<quint64>(good.size() * 2, bad.data() + bad.size() - 48);
        writeFile(sub("badtrailer.exe"), bad);
        PayloadReader r;
        QCOMPARE(r.open(sub("badtrailer.exe"), nullptr), OpenStatus::Corrupt);
    }
    void stubWithoutPayload()
    {
        PayloadReader r;
        QCOMPARE(r.open(m_stub, nullptr), OpenStatus::NoPayload);
    }

    // ------------------------------------------------------------------ install: happy path, upgrade, failure, cancel
    void freshInstall()
    {
        PayloadReader r;
        QCOMPARE(r.open(m_packed, nullptr), OpenStatus::Ok);
        InstallRequest req;
        req.targetDir = sub("fresh/ZEROLauncher");
        req.version = "0.2.0";
        req.uninstallerSource = m_packed;
        req.uninstallerBytes = r.info().stubSize;
        const InstallResult res = installFiles(r, req, nullptr, {});
        QVERIFY2(res.status == InstallStatus::Ok, qPrintable(res.error));
        QMap<QString, QByteArray> expect = m_stage;
        expect[Brand::uninstallerName()] = readFile(m_stub);  // the uninstaller is the stub without the payload
        auto got = snapshot(req.targetDir);
        QVERIFY(got.contains(Brand::installManifestName()));
        got.remove(Brand::installManifestName());
        QCOMPARE(got, expect);
        QVERIFY(!QFileInfo::exists(stagingDirFor(req.targetDir)));
        QVERIFY(!QFileInfo::exists(req.targetDir + ".zero-setup-old"));
        // manifest lists exactly what was installed
        QString ver;
        QStringList files;
        QVERIFY(readInstallManifest(req.targetDir, &ver, &files));
        QCOMPARE(ver, QString("0.2.0"));
        QCOMPARE(files.size(), int(m_stage.size()) + 2);
    }
    void failedExtractionLeavesTargetUntouched()
    {
        // a payload whose second file is damaged (the whole-payload hash is skipped to reach the per-file check)
        QJsonArray files;
        const QByteArray good1 = "first file", good2 = randomBytes(4000, 3);
        files.append(entry("a.txt", good1));
        QJsonObject bad = entry("b.bin", good2);
        bad["sha256"] = QString(64, 'a');  // wrong checksum
        files.append(bad);
        const QString exe = sub("badfile.exe");
        writeFile(exe, rawPayloadExe(randomBytes(500, 5), files, { { "a.txt", good1 }, { "b.bin", good2 } }));

        const QString target = sub("untouched/ZEROLauncher");
        writeFile(target + "/zerolauncher.exe", "OLD-EXE");
        writeFile(target + "/a.txt", "old a");
        writeFile(target + "/user-notes.txt", "mine");
        const auto before = snapshot(target);

        PayloadReader r;
        QCOMPARE(r.open(exe, nullptr), OpenStatus::Ok);
        InstallRequest req;
        req.targetDir = target;
        req.verifyPayloadHash = false;
        req.existing.kind = InstallKind::Nsis;
        const InstallResult res = installFiles(r, req, nullptr, {});
        QCOMPARE(res.status, InstallStatus::Failed);
        QVERIFY(res.error.contains("checksum"));
        QCOMPARE(res.stateNote, QString("Your previous version is still installed."));
        QCOMPARE(snapshot(target), before);
        QVERIFY(!QFileInfo::exists(stagingDirFor(target)));
        QVERIFY(!QFileInfo::exists(target + ".zero-setup-old"));
    }
    void damagedInstallerFailsBeforeWriting()
    {
        QByteArray bad = readFile(m_packed);
        bad[30'000 + 3000] = char(bad[30'000 + 3000] ^ 0x77);
        writeFile(sub("damaged.exe"), bad);
        PayloadReader r;
        if (r.open(sub("damaged.exe"), nullptr) != OpenStatus::Ok)
            return;  // damage hit the manifest: already rejected
        InstallRequest req;
        req.targetDir = sub("damaged-target/Z");
        const InstallResult res = installFiles(r, req, nullptr, {});
        QCOMPARE(res.status, InstallStatus::Failed);
        QCOMPARE(res.stateNote, QString("Nothing was changed."));
        QVERIFY(!QFileInfo::exists(req.targetDir));
        QVERIFY(!QFileInfo::exists(stagingDirFor(req.targetDir)));
    }
    void cancelRollsBackCleanly()
    {
        const QString target = sub("cancel/ZEROLauncher");
        writeFile(target + "/zerolauncher.exe", "OLD-EXE");
        const auto before = snapshot(target);
        PayloadReader r;
        QCOMPARE(r.open(m_packed, nullptr), OpenStatus::Ok);
        std::atomic<bool> cancel(false);
        InstallRequest req;
        req.targetDir = target;
        req.existing.kind = InstallKind::V2;
        const InstallResult res = installFiles(r, req, &cancel, [&](const QString&, quint64 done, quint64) {
            if (done > 1'000'000)
                cancel = true;  // user presses Cancel in the middle of the copy
        });
        QCOMPARE(res.status, InstallStatus::Cancelled);
        QCOMPARE(snapshot(target), before);
        QVERIFY(!QFileInfo::exists(stagingDirFor(target)));
    }
    void cancelOnFreshInstallLeavesNothing()
    {
        const QString target = sub("cancel-fresh/ZEROLauncher");
        PayloadReader r;
        QCOMPARE(r.open(m_packed, nullptr), OpenStatus::Ok);
        std::atomic<bool> cancel(true);
        InstallRequest req;
        req.targetDir = target;
        QCOMPARE(installFiles(r, req, &cancel, {}).status, InstallStatus::Cancelled);
        QVERIFY(!QFileInfo::exists(target));
        QVERIFY(!QFileInfo::exists(stagingDirFor(target)));
    }
    void commitFailureRollsBack()
    {
        // The target has a plain FILE called "platforms" where the payload needs a folder: the swap fails half way
        // (after some files were already moved in) and everything must be put back.
        const QString target = sub("commitfail/ZEROLauncher");
        writeFile(target + "/zerolauncher.exe", "OLD-EXE");
        writeFile(target + "/Qt6Core.dll", "OLD-QT");
        writeFile(target + "/platforms", "i am a file");
        writeFile(target + "/zz-keep.txt", "keep");
        const auto before = snapshot(target);
        PayloadReader r;
        QCOMPARE(r.open(m_packed, nullptr), OpenStatus::Ok);
        InstallRequest req;
        req.targetDir = target;
        req.existing.kind = InstallKind::Nsis;
        const InstallResult res = installFiles(r, req, nullptr, {});
        QCOMPARE(res.status, InstallStatus::Failed);
        QCOMPARE(snapshot(target), before);
        QVERIFY(!QFileInfo::exists(stagingDirFor(target)));
        QVERIFY(!QFileInfo::exists(target + ".zero-setup-old"));
    }

    // ------------------------------------------------------------------ upgrade detection (fake NSIS install + fake registry)
   public:
    void makeNsisInstall(const QString& dir)
    {
        writeFile(dir + "/zerolauncher.exe", "OLD-EXE");
        writeFile(dir + "/zerolauncher_updater.exe", "OLD-UPD");
        writeFile(dir + "/qt.conf", "old");
        writeFile(dir + "/uninstall.exe", "NSIS-UNINSTALL");
        writeFile(dir + "/Qt6Core.dll", "OLD-QT");
        writeFile(dir + "/libgone.dll", "removed in the new version");
        writeFile(dir + "/platforms/qwindows.dll", "old");
        writeFile(dir + "/jars/old-only.jar", "old jar");
        writeFile(dir + "/my-notes.txt", "not ours");  // user file: must survive
    }

   private slots:
    void detectNothing()
    {
        MemorySystem sys;
        QVERIFY(detectExisting(sys, sub("no-such-dir")).kind == InstallKind::None);
        QVERIFY(QDir().mkpath(sub("empty-dir")));
        QVERIFY(detectExisting(sys, sub("empty-dir")).kind == InstallKind::None);
        QCOMPARE(proposedInstallDir(sys, "C:/Users/x/AppData/Local"), QString("C:/Users/x/AppData/Local/Programs/ZEROLauncher"));
    }
    void detectNsisInstall()
    {
        const QString dir = sub("nsis-detect");
        makeNsisInstall(dir);
        MemorySystem sys;
        sys.regSetString(regAppKey(), "InstallDir", QDir::toNativeSeparators(dir));
        sys.regSetString(regUninstallKey(), "DisplayVersion", "0.1.0");
        sys.regSetString(regUninstallKey(), "InstallLocation", QDir::toNativeSeparators(dir));
        QCOMPARE(proposedInstallDir(sys, "C:/x"), dir);
        const ExistingInstall e = detectExisting(sys, dir);
        QVERIFY(e.kind == InstallKind::Nsis);
        QCOMPARE(e.version, QString("0.1.0"));
        QVERIFY(e.ownedFiles.contains("zerolauncher.exe"));
        QVERIFY(e.ownedFiles.contains("libgone.dll"));
        QVERIFY(e.ownedFiles.contains("platforms/qwindows.dll"));
        QVERIFY(e.ownedFiles.contains("jars/old-only.jar"));
        QVERIFY(!e.ownedFiles.contains("my-notes.txt"));
    }
    void detectIgnoresVersionOfAnotherFolder()
    {
        const QString dir = sub("nsis-other");
        makeNsisInstall(dir);
        MemorySystem sys;
        sys.regSetString(regUninstallKey(), "DisplayVersion", "7.7.7");
        sys.regSetString(regUninstallKey(), "InstallLocation", "D:\\Elsewhere\\ZERO");
        QVERIFY(detectExisting(sys, dir).version.isEmpty());
    }
    void upgradeOverNsisInstall()
    {
        const QString dir = sub("nsis-upgrade");
        makeNsisInstall(dir);
        MemorySystem sys;
        sys.regSetString(regUninstallKey(), "DisplayVersion", "0.1.0");
        const ExistingInstall e = detectExisting(sys, dir);
        PayloadReader r;
        QCOMPARE(r.open(m_packed, nullptr), OpenStatus::Ok);
        InstallRequest req;
        req.targetDir = dir;
        req.version = "0.2.0";
        req.uninstallerSource = m_packed;
        req.uninstallerBytes = r.info().stubSize;
        req.existing = e;
        const InstallResult res = installFiles(r, req, nullptr, {});
        QVERIFY2(res.status == InstallStatus::Ok, qPrintable(res.error));
        QCOMPARE(readFile(dir + "/zerolauncher.exe"), m_stage["zerolauncher.exe"]);
        QCOMPARE(readFile(dir + "/Qt6Core.dll"), m_stage["Qt6Core.dll"]);
        QCOMPARE(readFile(dir + "/uninstall.exe"), readFile(m_stub));       // NSIS uninstaller replaced by ours
        QVERIFY(!QFileInfo::exists(dir + "/libgone.dll"));                  // old file the new version dropped
        QVERIFY(!QFileInfo::exists(dir + "/jars/old-only.jar"));
        QCOMPARE(readFile(dir + "/my-notes.txt"), QByteArray("not ours"));  // user file kept
        QVERIFY(!QFileInfo::exists(dir + ".zero-setup-old"));
        // and it is now a V2 install with the new version
        const ExistingInstall again = detectExisting(sys, dir);
        QVERIFY(again.kind == InstallKind::V2);
        QCOMPARE(again.version, QString("0.2.0"));
        QVERIFY(!again.ownedFiles.contains("my-notes.txt"));
    }
    void upgradeOverV2Install()
    {
        const QString dir = sub("v2-upgrade");
        PayloadReader r;
        QCOMPARE(r.open(m_packed, nullptr), OpenStatus::Ok);
        InstallRequest req;
        req.targetDir = dir;
        req.version = "0.2.0";
        req.uninstallerSource = m_packed;
        req.uninstallerBytes = r.info().stubSize;
        QVERIFY(installFiles(r, req, nullptr, {}).status == InstallStatus::Ok);
        writeFile(dir + "/libstale.dll", "x");  // pretend the v0.2.0 install had owned this
        // rewrite the manifest so libstale.dll is "owned"
        QString v;
        QStringList files;
        QVERIFY(readInstallManifest(dir, &v, &files));
        files << "libstale.dll";
        QJsonArray arr;
        for (const QString& f : files)
            arr.append(f);
        QJsonObject o;
        o["version"] = "0.2.0";
        o["files"] = arr;
        writeFile(dir + "/" + Brand::installManifestName(), QJsonDocument(o).toJson());
        writeFile(dir + "/foreign.txt", "not ours");

        MemorySystem sys;
        const ExistingInstall e = detectExisting(sys, dir);
        QVERIFY(e.kind == InstallKind::V2);
        QCOMPARE(e.version, QString("0.2.0"));
        req.existing = e;
        req.version = "0.3.0";
        QVERIFY(installFiles(r, req, nullptr, {}).status == InstallStatus::Ok);
        QVERIFY(!QFileInfo::exists(dir + "/libstale.dll"));
        QVERIFY(QFileInfo::exists(dir + "/foreign.txt"));
        QString v2;
        QVERIFY(readInstallManifest(dir, &v2, nullptr));
        QCOMPARE(v2, QString("0.3.0"));
    }

    // ------------------------------------------------------------------ footprint (the NSIS table)
    void footprintMatchesNsis()
    {
        MemorySystem sys("C:/Users/x/Start/Programs", "C:/Users/x/Desktop");
        FootprintOptions o;
        o.installDir = "C:/Users/x/AppData/Local/Programs/ZEROLauncher";
        o.version = "0.1.0";
        o.estimatedSizeBytes = 250ull << 20;
        QStringList warn;
        QVERIFY(applyFootprint(sys, o, &warn));
        QVERIFY(warn.isEmpty());
        const QString dir = "C:\\Users\\x\\AppData\\Local\\Programs\\ZEROLauncher";
        QCOMPARE(sys.value("Software\\ZEROLauncher", "InstallDir"), dir);
        for (const char* scheme : { "curseforge", "modrinth", "zerolauncher" }) {
            const QString k = QString("Software\\Classes\\") + scheme;
            QVERIFY(sys.hasValue(k, "URL Protocol"));
            QCOMPARE(sys.value(k + "\\shell\\open\\command", ""), "\"" + dir + "\\zerolauncher.exe\" \"%1\"");
        }
        const QString u = "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ZEROLauncher";
        QCOMPARE(sys.value(u, "DisplayName"), QString("ZERO Launcher"));
        QCOMPARE(sys.value(u, "DisplayIcon"), dir + "\\zerolauncher.exe");
        QCOMPARE(sys.value(u, "InstallLocation"), dir);
        QCOMPARE(sys.value(u, "Publisher"), QString("ZERO Launcher Contributors"));
        QCOMPARE(sys.value(u, "Version"), QString("0.1.0.0"));
        QCOMPARE(sys.value(u, "DisplayVersion"), QString("0.1.0"));
        QCOMPARE(sys.value(u, "VersionMajor"), QString("0"));
        QCOMPARE(sys.value(u, "VersionMinor"), QString("1"));
        QCOMPARE(sys.value(u, "EstimatedSize"), QString::number(250 * 1024));
        QCOMPARE(sys.value(u, "NoModify"), QString("1"));
        QCOMPARE(sys.value(u, "NoRepair"), QString("1"));
        QVERIFY(sys.value(u, "UninstallString").contains("uninstall.exe"));
        QVERIFY(sys.value(u, "QuietUninstallString").endsWith("/S"));
        QStringList lnks = sys.shortcuts();
        QVERIFY(lnks.contains("C:/Users/x/Start/Programs/ZERO Launcher.lnk"));
        QVERIFY(lnks.contains("C:/Users/x/Desktop/ZERO Launcher.lnk"));
        // Open-With association is an opt-in component (as in NSIS): nothing written by default
        QVERIFY(!sys.regKeyExists("Software\\Classes\\ZEROLauncher.App"));
        QVERIFY(!sys.regKeyExists("Software\\Classes\\.mrpack"));
    }
    void footprintOptions()
    {
        MemorySystem sys;
        FootprintOptions o;
        o.installDir = "C:/Z";
        o.version = "1.2.3";
        o.desktopShortcut = false;
        o.startMenuShortcut = false;
        o.writeUninstallEntry = false;  // /NoUninstaller
        o.shellAssociation = true;
        applyFootprint(sys, o, nullptr);
        QVERIFY(sys.shortcuts().isEmpty());
        QVERIFY(!sys.regKeyExists("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ZEROLauncher"));
        QCOMPARE(sys.value("Software\\Classes\\.mrpack", ""), QString("ZEROLauncher.App"));
        QVERIFY(sys.hasValue("Software\\Classes\\.zip\\OpenWithProgids", "ZEROLauncher.App"));
        QVERIFY(!sys.hasValue("Software\\Classes\\.zip", ""));  // .zip is never taken over, only listed in Open With
        // and uninstall reverts all of it
        removeFootprint(sys, "C:/Z", nullptr);
        QVERIFY(!sys.regKeyExists("Software\\Classes\\ZEROLauncher.App"));
        QVERIFY(!sys.regKeyExists("Software\\Classes\\.mrpack"));
        QVERIFY(!sys.regKeyExists("Software\\Classes\\.zip"));
        QVERIFY(!sys.regKeyExists("Software\\Classes\\Applications\\zerolauncher.exe"));
    }
    void shellAssocRestoresPreviousClass()
    {
        MemorySystem sys;
        sys.regSetString("Software\\Classes\\.mrpack", "", "SomeOtherApp.mrpack");
        setupShellAssociation(sys, "C:/Z");
        QCOMPARE(sys.value("Software\\Classes\\.mrpack", ""), QString("ZEROLauncher.App"));
        teardownShellAssociation(sys, "C:/Z");
        QCOMPARE(sys.value("Software\\Classes\\.mrpack", ""), QString("SomeOtherApp.mrpack"));
    }
    void urlHandlersAreRemovedOnlyWhenTheyStillPointAtZero()
    {
        const QString ours = "C:/Users/x/AppData/Local/Programs/ZEROLauncher";
        const QString cmd = QString("\"%1\" \"%2\"").arg(QDir::toNativeSeparators(ours) + "\\zerolauncher.exe", "%1");
        MemorySystem sys;
        // curseforge still points at ZERO, modrinth was taken over by another launcher since
        sys.regSetString("Software\\Classes\\curseforge", "URL Protocol", "");
        sys.regSetString("Software\\Classes\\curseforge\\shell\\open\\command", "", cmd);
        sys.regSetString("Software\\Classes\\modrinth", "URL Protocol", "");
        sys.regSetString("Software\\Classes\\modrinth\\shell\\open\\command", "", "\"C:\\Prism\\prismlauncher.exe\" \"%1\"");
        sys.regSetString("Software\\Classes\\zerolauncher\\shell\\open\\command", "", cmd);
        removeFootprint(sys, ours, nullptr);
        QVERIFY(!sys.regKeyExists("Software\\Classes\\curseforge"));
        QVERIFY(sys.regKeyExists("Software\\Classes\\modrinth"));
        QCOMPARE(sys.value("Software\\Classes\\modrinth\\shell\\open\\command", ""), QString("\"C:\\Prism\\prismlauncher.exe\" \"%1\""));
        QVERIFY(!sys.regKeyExists("Software\\Classes\\zerolauncher"));
        // a handler that points at a different ZERO install is not ours either
        MemorySystem sys2;
        const QString other = QString("\"%1\" \"%2\"").arg("D:\\Other\\zerolauncher.exe", "%1");
        sys2.regSetString("Software\\Classes\\curseforge\\shell\\open\\command", "", other);
        removeFootprint(sys2, ours, nullptr);
        QVERIFY(sys2.regKeyExists("Software\\Classes\\curseforge"));
    }

    // ------------------------------------------------------------------ uninstall
    void uninstallKeepsDataByDefault()
    {
        const QString dir = sub("un1/Programs/ZEROLauncher");
        const QString data = sub("un1/Roaming/ZEROLauncher");
        PayloadReader r;
        QCOMPARE(r.open(m_packed, nullptr), OpenStatus::Ok);
        InstallRequest req;
        req.targetDir = dir;
        req.uninstallerSource = m_packed;
        req.uninstallerBytes = r.info().stubSize;
        QVERIFY(installFiles(r, req, nullptr, {}).status == InstallStatus::Ok);
        writeFile(data + "/instances/Survival/world/level.dat", "WORLD");
        writeFile(data + "/zerolauncher.cfg", "settings");
        writeFile(dir + "/foreign.txt", "not ours");

        MemorySystem sys;
        UninstallRequest ur;
        ur.installDir = dir;
        ur.userDataDir = data;  // set, but not asked for
        const UninstallResult res = uninstallFiles(sys, ur);
        QVERIFY(res.ok);
        QVERIFY(!res.removedUserData);
        QVERIFY(sys.trashed().isEmpty());  // the Recycle Bin is not even asked
        QCOMPARE(readFile(data + "/instances/Survival/world/level.dat"), QByteArray("WORLD"));
        QCOMPARE(readFile(data + "/zerolauncher.cfg"), QByteArray("settings"));
        QVERIFY(!QFileInfo::exists(dir + "/zerolauncher.exe"));
        QVERIFY(!QFileInfo::exists(dir + "/platforms"));
        QVERIFY(!QFileInfo::exists(dir + "/" + Brand::installManifestName()));
        QCOMPARE(readFile(dir + "/foreign.txt"), QByteArray("not ours"));  // not ours, so the folder stays
        QVERIFY(QFileInfo::exists(dir));
    }
    void uninstallRemovesEmptyInstallFolder()
    {
        const QString dir = sub("un2/ZEROLauncher");
        PayloadReader r;
        QCOMPARE(r.open(m_packed, nullptr), OpenStatus::Ok);
        InstallRequest req;
        req.targetDir = dir;
        QVERIFY(installFiles(r, req, nullptr, {}).status == InstallStatus::Ok);
        MemorySystem sys;
        UninstallRequest ur;
        ur.installDir = dir;
        QVERIFY(uninstallFiles(sys, ur).ok);
        QVERIFY(!QFileInfo::exists(dir));
    }
    void uninstallMovesDataToRecycleBinOnlyWhenAskedAndSafe()
    {
        const QString dir = sub("un3/Programs/ZEROLauncher");
        writeFile(dir + "/zerolauncher.exe", "x");
        const QString data = sub("un3/Roaming/ZEROLauncher");
        writeFile(data + "/worlds/a.dat", "w");
        MemorySystem sys;
        UninstallRequest ur;
        ur.installDir = dir;
        ur.userDataDir = data;
        ur.deleteUserData = true;
        const UninstallResult res = uninstallFiles(sys, ur);
        QVERIFY(res.ok);
        QVERIFY(res.removedUserData);
        QCOMPARE(sys.trashed(), QStringList{ data });  // went through the Recycle Bin seam, no removeRecursively
        QVERIFY(!QFileInfo::exists(dir + "/zerolauncher.exe"));

        // never the Prism folder, never an arbitrary folder, never around the install folder
        const QString prism = sub("un3/Roaming/PrismLauncher");
        writeFile(prism + "/prismlauncher.cfg", "owner's real data");
        QVERIFY(!isSafeUserDataDir(prism, dir));
        QVERIFY(!isSafeUserDataDir(sub("un3/Roaming/Documents"), dir));
        QVERIFY(!isSafeUserDataDir("C:/", dir));
        QVERIFY(!isSafeUserDataDir(sub("un3"), dir));
        QVERIFY(!isSafeUserDataDir(sub("un3/Roaming/ZEROLauncher"), sub("un3/Roaming/ZEROLauncher/inner")));
        QVERIFY(isSafeUserDataDir(sub("un3/Roaming/ZEROLauncher"), dir));
        MemorySystem sys2;
        ur.userDataDir = prism;
        writeFile(dir + "/zerolauncher.exe", "x");
        const UninstallResult res2 = uninstallFiles(sys2, ur);
        QVERIFY(!res2.ok);
        QVERIFY(!res2.removedUserData);
        QVERIFY(sys2.trashed().isEmpty());
        QCOMPARE(readFile(prism + "/prismlauncher.cfg"), QByteArray("owner's real data"));
        QVERIFY(QFileInfo::exists(dir + "/zerolauncher.exe"));  // nothing else was removed either
    }
    void uninstallStopsWhenTheRecycleBinRefuses()
    {
        const QString dir = sub("un4/Programs/ZEROLauncher");
        PayloadReader r;
        QCOMPARE(r.open(m_packed, nullptr), OpenStatus::Ok);
        InstallRequest req;
        req.targetDir = dir;
        QVERIFY(installFiles(r, req, nullptr, {}).status == InstallStatus::Ok);
        const QString data = sub("un4/Roaming/ZEROLauncher");
        writeFile(data + "/saves/world/level.dat", "WORLD");
        const auto before = snapshot(dir);
        MemorySystem sys;
        sys.setTrashFails(true);
        UninstallRequest ur;
        ur.installDir = dir;
        ur.userDataDir = data;
        ur.deleteUserData = true;
        const UninstallResult res = uninstallFiles(sys, ur);
        QVERIFY(!res.ok);
        QVERIFY(res.userDataFailed);
        QVERIFY(!res.removedUserData);
        QCOMPARE(res.error, QString("Your worlds and settings were not removed; they are still in %1.").arg(QDir::toNativeSeparators(data)));
        QCOMPARE(readFile(data + "/saves/world/level.dat"), QByteArray("WORLD"));  // no permanent-delete fallback
        QCOMPARE(snapshot(dir), before);                                         // and the program files are untouched
        QVERIFY(!sys.log().join(QChar(10)).contains("REG DELKEY"));                   // registry untouched too
    }
    void uninstallNsisLayout()
    {
        const QString dir = sub("un-nsis");
        makeNsisInstall(dir);
        MemorySystem sys;
        UninstallRequest ur;
        ur.installDir = dir;
        QVERIFY(uninstallFiles(sys, ur).ok);
        QVERIFY(!QFileInfo::exists(dir + "/zerolauncher.exe"));
        QVERIFY(!QFileInfo::exists(dir + "/Qt6Core.dll"));
        QVERIFY(!QFileInfo::exists(dir + "/platforms"));
        QVERIFY(!QFileInfo::exists(dir + "/uninstall.exe"));
        QCOMPARE(readFile(dir + "/my-notes.txt"), QByteArray("not ours"));
    }
    void lockedFileDetection()
    {
        QVERIFY(!isFileLocked(sub("does-not-exist.exe")));
        writeFile(sub("free.exe"), "x");
        QVERIFY(!isFileLocked(sub("free.exe")));
    }

    // ------------------------------------------------------------------ jobs (what --no-system and silent runs use)
    void installJobNoSystemWritesFilesAndPlanOnly()
    {
        PayloadReader r;
        QCOMPARE(r.open(m_packed, nullptr), OpenStatus::Ok);
        InstallJob job;
        job.payload = &r;
        job.exePath = m_packed;
        job.targetDir = sub("job1/ZEROLauncher");
        job.noSystem = true;
        job.planFile = sub("job1/plan.txt");
        double last = -1;
        const JobResult res = runInstallJob(job, nullptr, [&](const QString&, double f) { QVERIFY(f >= last - 1e-9); last = f; });
        QVERIFY2(res.ok, qPrintable(res.error));
        QVERIFY(last >= 0.98);
        QVERIFY(QFileInfo::exists(job.targetDir + "/zerolauncher.exe"));
        QVERIFY(QFileInfo::exists(job.targetDir + "/uninstall.exe"));
        const QString plan = QString::fromUtf8(readFile(job.planFile));
        QVERIFY(plan.contains("curseforge"));
        QVERIFY(plan.contains("LNK CREATE"));
        QVERIFY(plan.contains("INSTALL FILES 0.2.0"));
        QVERIFY(!plan.contains("<missing>"));
    }
    void installJobHonoursSwitches()
    {
        PayloadReader r;
        QCOMPARE(r.open(m_packed, nullptr), OpenStatus::Ok);
        InstallJob job;
        job.payload = &r;
        job.exePath = m_packed;
        job.targetDir = sub("job2/ZEROLauncher");
        job.noSystem = true;
        job.planFile = sub("job2/plan.txt");
        job.desktopShortcut = false;       // /NoShortcuts
        job.startMenuShortcut = false;
        job.writeUninstaller = false;      // /NoUninstaller
        QVERIFY(runInstallJob(job, nullptr, {}).ok);
        const QString plan = QString::fromUtf8(readFile(job.planFile));
        QVERIFY(!plan.contains("LNK CREATE"));
        QVERIFY(!plan.contains("CurrentVersion\\Uninstall"));
        QVERIFY(!QFileInfo::exists(job.targetDir + "/uninstall.exe"));
    }
    void uninstallJobNoSystemOnlyPlansTheRecycleBinMove()
    {
        const QString dir = sub("job3/ZEROLauncher");
        writeFile(dir + "/zerolauncher.exe", "x");
        const QString data = sub("job3/Roaming/ZEROLauncher");
        writeFile(data + "/keep.dat", "worlds");
        UninstallJob job;
        job.installDir = dir;
        job.deleteUserData = true;  // --no-system: the fake system records the move, the disk is not touched
        job.userDataDir = data;
        job.noSystem = true;
        job.planFile = sub("job3/plan.txt");
        QVERIFY(runUninstallJob(job, nullptr, {}).ok);
        QCOMPARE(readFile(data + "/keep.dat"), QByteArray("worlds"));
        QVERIFY(QString::fromUtf8(readFile(job.planFile)).contains("MOVED TO THE RECYCLE BIN"));
        QVERIFY(!QFileInfo::exists(dir + "/zerolauncher.exe"));
    }

    // ------------------------------------------------------------------ command line
    void cmdLine()
    {
        auto c = parseCommandLine({ "/S", "/NoShortcuts", "/D=C:\\Program Files\\ZERO", "x" });
        QVERIFY(c.silent && c.noShortcuts);
        QCOMPARE(c.target, QString("C:\\Program Files\\ZERO x"));
        c = parseCommandLine({ "--uninstall", "/S", "_?=C:\\Apps\\ZERO" });
        QVERIFY(c.uninstall && c.silent && c.inPlace);
        QCOMPARE(c.target, QString("C:\\Apps\\ZERO"));
        c = parseCommandLine({ "--target", "T", "--no-system", "--screenshot", "welcome", "a.png", "--scale", "1.5" });
        QCOMPARE(c.target, QString("T"));
        QVERIFY(c.noSystem);
        QCOMPARE(c.screenshotScreen, QString("welcome"));
        QCOMPARE(c.screenshotPath, QString("a.png"));
        QCOMPARE(c.scale, 1.5);
        QVERIFY(c.errors.isEmpty());
        QVERIFY(!parseCommandLine({ "--bogus" }).errors.isEmpty());
        QVERIFY(!parseCommandLine({ "--target" }).errors.isEmpty());
    }
};

QTEST_GUILESS_MAIN(SetupTests)
#include "SetupTests.moc"
