// SPDX-License-Identifier: GPL-3.0-only
// Smart Backup (worlds): backup round trip, verification, zip-slip rejection, restore (happy path, damaged backup, failing rename,
// rollback), retention, listing, free-space refusal, deletion and the job runner (one job per world, running instance, launch
// lock). These are the data-safety invariants of docs/zero/ARCHITECTURE.md ("Smart Backup"): the original world must stay intact
// on every failure path. Nothing here touches the real Recycle Bin (trash hooks remove directly).
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirListing>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRandomGenerator>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <functional>
#include <sstream>

#include <io/stream_writer.h>
#include <tag_compound.h>
#include <tag_primitive.h>
#include <tag_string.h>

#include "GZip.h"
#include "archive/ArchiveReader.h"
#include "archive/ArchiveWriter.h"
#include "zero/backup/WorldBackup.h"
#include "zero/backup/WorldBackupJobs.h"
#include "zero/worlds/WorldScanner.h"

using namespace Zero::Backup;

namespace {

using Tree = QMap<QString, QByteArray>;  // relative path -> bytes

QByteArray levelDat(const QString& name)
{
    nbt::tag_compound data;
    data.insert("LevelName", nbt::tag_string(name.toStdString()));
    data.insert("LastPlayed", nbt::tag_long(1700000000000));
    data.insert("GameType", nbt::tag_int(0));
    nbt::tag_compound root;
    root.insert("Data", std::move(data));
    std::ostringstream stream;
    nbt::io::write_tag("", root, stream);
    QByteArray raw(stream.str().data(), static_cast<qsizetype>(stream.str().size()));
    QByteArray gz;
    GZip::zip(raw, gz);
    return gz;
}

QByteArray randomBytes(int size, quint32 seed)
{
    QRandomGenerator gen(seed);
    QByteArray out(size, Qt::Uninitialized);
    for (int i = 0; i < size; ++i)
        out[i] = char(gen.bounded(256));
    return out;
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

QString sha(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

/// A small world: level.dat, a region file (bigger than one hash chunk), nested data and a session.lock.
Tree worldTree(quint32 seed = 1)
{
    Tree t;
    t["level.dat"] = levelDat("My World");
    t["region/r.0.0.mca"] = randomBytes(1'300'000, seed);
    t["region/r.0.1.mca"] = randomBytes(5000, seed + 1);
    t["data/raids.dat"] = randomBytes(300, seed + 2);
    t["data/nested/deep/map_0.dat"] = randomBytes(77, seed + 3);
    t["playerdata/empty.dat"] = QByteArray();
    return t;
}

QString makeWorld(const QString& saves, const QString& folder, const Tree& tree, bool lock = true)
{
    const QString dir = saves + QLatin1Char('/') + folder;
    for (auto it = tree.cbegin(); it != tree.cend(); ++it)
        if (!writeFile(dir + QLatin1Char('/') + it.key(), it.value()))
            return {};
    if (lock && !writeFile(dir + QStringLiteral("/session.lock"), QByteArray("lock")))
        return {};
    return dir;
}

Tree readTree(const QString& root, bool withLock = false)
{
    Tree t;
    const QString base = QDir(root).absolutePath();
    for (const auto& e : QDirListing(base, QDirListing::IteratorFlag::Recursive | QDirListing::IteratorFlag::FilesOnly |
                                               QDirListing::IteratorFlag::IncludeHidden)) {
        const QString rel = e.filePath().mid(base.size() + 1);
        if (!withLock && rel == QLatin1String("session.lock"))
            continue;
        QFile f(e.filePath());
        if (f.open(QIODevice::ReadOnly))
            t.insert(rel, f.readAll());
    }
    return t;
}

QStringList zeroLeftovers(const QString& saves)
{
    QStringList out;
    for (const QString& name : QDir(saves).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot))
        if (name.startsWith(QLatin1String(".zero-")))
            out << name;
    return out;
}

Hooks removeHooks(int* trashed = nullptr)
{
    Hooks h;
    h.trashPath = [trashed](const QString& path) {
        if (trashed)
            ++*trashed;
        QFileInfo info(path);
        return info.isDir() ? QDir(path).removeRecursively() : QFile::remove(path);
    };
    return h;
}

struct Zip {
    struct Entry {
        QString name;
        QByteArray data;
    };
    QList<Entry> entries;
};

Zip readZip(const QString& path)
{
    Zip z;
    MMCZip::ArchiveReader reader(path);
    const std::function<Result<>(MMCZip::ArchiveReader::File*)> visit = [&](MMCZip::ArchiveReader::File* f) -> Result<> {
        const QString name = f->filename();
        auto data = f->readAll();
        if (!data)
            return std::unexpected{ data.error() };
        z.entries.append({ name, *data });
        return {};
    };
    const auto r = reader.parse(visit);
    if (!r)
        qWarning() << r.error();
    return z;
}

bool writeZip(const QString& path, const Zip& z)
{
    MMCZip::ArchiveWriter w(path);
    if (!w.open())
        return false;
    for (const auto& e : z.entries)
        if (!w.addFile(e.name, e.data))
            return false;
    return bool(w.close());
}

/// A manifest-consistent crafted archive: `entryNames` are stored verbatim (whatever they are), the manifest lists `manifestPaths`.
QString craftZip(const QString& dir, const QString& fileName, const QStringList& entryNames, const QStringList& manifestPaths)
{
    const QByteArray payload("payload");
    QJsonArray files;
    for (const QString& p : manifestPaths)
        files.append(QJsonObject{ { "path", p }, { "size", double(payload.size()) }, { "sha256", sha(payload) } });
    const QJsonObject root{ { "version", 1 },
                            { "kind", "manual" },
                            { "createdMs", double(QDateTime::currentMSecsSinceEpoch()) },
                            { "instanceId", "i" },
                            { "instanceName", "I" },
                            { "worldFolder", "w" },
                            { "worldName", "w" },
                            { "files", files },
                            { "totalBytes", double(payload.size() * manifestPaths.size()) } };
    Zip z;
    z.entries.append({ QStringLiteral("zero-backup.json"), QJsonDocument(root).toJson(QJsonDocument::Compact) });
    for (const QString& n : entryNames)
        z.entries.append({ n, payload });
    const QString path = dir + QLatin1Char('/') + fileName;
    QDir().mkpath(dir);
    return writeZip(path, z) ? path : QString();
}

BackupMeta metaFor(const QString& folder = QStringLiteral("World1"))
{
    return { "inst", "Instance", folder, "My World" };
}

Zero::Worlds::WorldEntry entryFor(const QString& saves, const QString& folder)
{
    Zero::Worlds::WorldEntry e;
    e.instanceId = "inst";
    e.instanceName = "Instance";
    e.folderPath = saves + "/" + folder;
    e.folderName = folder;
    e.name = "My World";
    return e;
}

QString touchBackupName(const QString& dir, const QString& name)
{
    const QString path = dir + QLatin1Char('/') + name;
    writeFile(path, QByteArray("x"));
    return path;
}

}  // namespace

class WorldBackupTest : public QObject {
    Q_OBJECT

   private slots:
    // ---------------------------------------------------------------- create / verify

    void backupRoundTrip()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const Tree tree = worldTree();
        const QString world = makeWorld(root.filePath("saves"), "World1", tree);
        const QString dir = root.filePath("backups");

        const BackupResult r = createBackup(world, dir, Kind::Manual, metaFor());
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.zipPath.endsWith("-manual.zip"));
        QVERIFY(QFileInfo(r.zipPath).isFile());
        QVERIFY(!QFileInfo::exists(r.zipPath + ".part"));
        QVERIFY(QDir(dir).entryList({ "*.part" }).isEmpty());
        QVERIFY(verifyBackup(r.zipPath).ok);

        // manifest content
        const Zip z = readZip(r.zipPath);
        QCOMPARE(z.entries.first().name, QString("zero-backup.json"));  // first entry
        const QJsonObject m = QJsonDocument::fromJson(z.entries.first().data).object();
        QCOMPARE(m["version"].toInt(), 1);
        QCOMPARE(m["kind"].toString(), QString("manual"));
        QCOMPARE(m["instanceId"].toString(), QString("inst"));
        QCOMPARE(m["instanceName"].toString(), QString("Instance"));
        QCOMPARE(m["worldFolder"].toString(), QString("World1"));
        QCOMPARE(m["worldName"].toString(), QString("My World"));
        QVERIFY(qAbs(m["createdMs"].toDouble() - double(QDateTime::currentMSecsSinceEpoch())) < 60000);
        qint64 total = 0;
        QMap<QString, QJsonObject> listed;
        for (const auto& v : m["files"].toArray()) {
            listed.insert(v.toObject()["path"].toString(), v.toObject());
            total += qint64(v.toObject()["size"].toDouble());
        }
        QCOMPARE(m["totalBytes"].toDouble(), double(total));
        QCOMPARE(QStringList(listed.keys()), QStringList(tree.keys()));  // session.lock is not listed
        for (auto it = tree.cbegin(); it != tree.cend(); ++it) {
            QCOMPARE(qint64(listed[it.key()]["size"].toDouble()), qint64(it.value().size()));
            QCOMPARE(listed[it.key()]["sha256"].toString(), sha(it.value()));
        }
        // the zip holds exactly manifest + world/<path> entries, no session.lock
        QStringList names;
        for (const auto& e : z.entries)
            names << e.name;
        QVERIFY(!names.contains("world/session.lock"));
        QCOMPARE(names.size(), tree.size() + 1);
        for (const QString& rel : tree.keys())
            QVERIFY2(names.contains("world/" + rel), qPrintable(rel));
        // the world itself is untouched
        QCOMPARE(readTree(world, true).size(), tree.size() + 1);
    }

    void nestedSessionLockIsKept()
    {
        QTemporaryDir root;
        Tree tree = worldTree();
        tree["data/session.lock"] = QByteArray("not the world lock");  // only the root one is excluded
        const QString world = makeWorld(root.filePath("saves"), "W", tree);
        const BackupResult r = createBackup(world, root.filePath("b"), Kind::Manual, metaFor());
        QVERIFY2(r.ok, qPrintable(r.error));
        bool found = false;
        for (const auto& e : readZip(r.zipPath).entries)
            found |= e.name == "world/data/session.lock";
        QVERIFY(found);
    }

    void namesAreUnique()
    {
        QTemporaryDir root;
        const QString world = makeWorld(root.filePath("saves"), "W", worldTree());
        const QString dir = root.filePath("b");
        QSet<QString> paths;
        for (int i = 0; i < 4; ++i) {
            const BackupResult r = createBackup(world, dir, Kind::Manual, metaFor());
            QVERIFY2(r.ok, qPrintable(r.error));
            QVERIFY(!paths.contains(r.zipPath));
            paths.insert(r.zipPath);
        }
        QCOMPARE(QDir(dir).entryList({ "*.zip" }).size(), 4);
    }

    void backupOfMissingWorldFails()
    {
        QTemporaryDir root;
        const BackupResult r = createBackup(root.filePath("nope"), root.filePath("b"), Kind::Manual, metaFor());
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
        QVERIFY(QDir(root.filePath("b")).entryList({ "*.zip*" }).isEmpty());
    }

    void freeSpaceRefusal()
    {
        QTemporaryDir root;
        const Tree tree = worldTree();
        const QString world = makeWorld(root.filePath("saves"), "W", tree);
        const QString dir = root.filePath("b");
        Hooks hooks;
        hooks.freeBytes = [](const QString&) -> qint64 { return 1000; };
        const BackupResult r = createBackup(world, dir, Kind::Manual, metaFor(), hooks);
        QVERIFY(!r.ok);
        QVERIFY2(r.error.contains("Not enough free disk space for a backup (needs about"), qPrintable(r.error));
        QVERIFY(QDir(dir).entryList({ "*.zip*" }).isEmpty());  // no zip, no .part

        // the threshold is world size * 1.1 + 50 MB: enough free space passes
        qint64 world_size = 0;
        for (const QByteArray& b : tree)
            world_size += b.size();
        hooks.freeBytes = [world_size](const QString&) -> qint64 { return qint64(world_size * 1.1) + 50LL * 1024 * 1024 + 1; };
        QVERIFY(createBackup(world, dir, Kind::Manual, metaFor(), hooks).ok);
        hooks.freeBytes = [world_size](const QString&) -> qint64 { return qint64(world_size * 1.1) + 50LL * 1024 * 1024 - 1024; };
        QVERIFY(!createBackup(world, dir, Kind::Manual, metaFor(), hooks).ok);
    }

    void verifyDetectsCorruptedEntry()
    {
        QTemporaryDir root;
        const QString world = makeWorld(root.filePath("saves"), "W", worldTree());
        const BackupResult r = createBackup(world, root.filePath("b"), Kind::Manual, metaFor());
        QVERIFY(r.ok);

        Zip z = readZip(r.zipPath);
        for (auto& e : z.entries)
            if (e.name == "world/region/r.0.0.mca")
                e.data[12345] = char(e.data[12345] ^ 0x01);  // one flipped bit
        const QString bad = root.filePath("bad.zip");
        QVERIFY(writeZip(bad, z));
        const VerifyResult v = verifyBackup(bad);
        QVERIFY(!v.ok);
        QVERIFY2(v.error.contains("r.0.0.mca"), qPrintable(v.error));
        QVERIFY(verifyBackup(r.zipPath).ok);  // the original is fine
    }

    void verifyDetectsMissingAndExtraEntries()
    {
        QTemporaryDir root;
        const QString world = makeWorld(root.filePath("saves"), "W", worldTree());
        const BackupResult r = createBackup(world, root.filePath("b"), Kind::Manual, metaFor());
        QVERIFY(r.ok);

        Zip missing = readZip(r.zipPath);
        for (int i = 0; i < missing.entries.size(); ++i)
            if (missing.entries[i].name == "world/data/raids.dat")
                missing.entries.removeAt(i--);
        QVERIFY(writeZip(root.filePath("missing.zip"), missing));
        const VerifyResult vm = verifyBackup(root.filePath("missing.zip"));
        QVERIFY(!vm.ok);
        QVERIFY2(vm.error.contains("raids.dat"), qPrintable(vm.error));

        Zip extra = readZip(r.zipPath);
        extra.entries.append(Zip::Entry{ "world/unlisted.txt", "surprise" });
        QVERIFY(writeZip(root.filePath("extra.zip"), extra));
        QVERIFY(!verifyBackup(root.filePath("extra.zip")).ok);

        Zip unknown = readZip(r.zipPath);
        unknown.entries.append(Zip::Entry{ "other/thing.txt", "x" });
        QVERIFY(writeZip(root.filePath("unknown.zip"), unknown));
        QVERIFY(!verifyBackup(root.filePath("unknown.zip")).ok);

        Zip dup = readZip(r.zipPath);
        dup.entries.append(dup.entries.last());
        QVERIFY(writeZip(root.filePath("dup.zip"), dup));
        QVERIFY(!verifyBackup(root.filePath("dup.zip")).ok);

        Zip noManifest = readZip(r.zipPath);
        noManifest.entries.removeFirst();
        QVERIFY(writeZip(root.filePath("nomanifest.zip"), noManifest));
        QVERIFY(!verifyBackup(root.filePath("nomanifest.zip")).ok);

        writeFile(root.filePath("garbage.zip"), randomBytes(5000, 9));
        QVERIFY(!verifyBackup(root.filePath("garbage.zip")).ok);
        QVERIFY(!verifyBackup(root.filePath("does-not-exist.zip")).ok);
    }

    // ---------------------------------------------------------------- zip-slip

    void safePathRules_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<bool>("safe");
        QTest::newRow("plain") << "level.dat" << true;
        QTest::newRow("nested") << "region/r.0.0.mca" << true;
        QTest::newRow("dots in name") << "a.b/c..d" << true;
        QTest::newRow("empty") << "" << false;
        QTest::newRow("dotdot") << "../evil" << false;
        QTest::newRow("dotdot middle") << "a/../../evil" << false;
        QTest::newRow("dotdot only") << ".." << false;
        QTest::newRow("dot") << "./a" << false;
        QTest::newRow("absolute") << "/etc/passwd" << false;
        QTest::newRow("drive") << "C:/Windows/x" << false;
        QTest::newRow("drive relative") << "C:evil" << false;
        QTest::newRow("backslash") << "a\\b" << false;
        QTest::newRow("backslash dotdot") << "..\\evil" << false;
        QTest::newRow("double slash") << "a//b" << false;
        QTest::newRow("trailing slash") << "a/b/" << false;
        QTest::newRow("stream") << "a.txt:stream" << false;
        QTest::newRow("device") << "con" << false;
        QTest::newRow("device ext") << "dir/NUL.txt" << false;
        QTest::newRow("trailing dot") << "a." << false;
        QTest::newRow("trailing space") << "a /b" << false;
        QTest::newRow("control") << QString("a\x01") << false;
        QTest::newRow("wildcard") << "a*b" << false;
    }

    void safePathRules()
    {
        QFETCH(QString, path);
        QFETCH(bool, safe);
        QCOMPARE(isSafeRelativePath(path), safe);
    }

    void zipSlipIsRejected_data()
    {
        QTest::addColumn<QString>("entry");
        QTest::addColumn<bool>("inManifest");
        const QStringList bad = { "world/../evil.txt", "world/../../evil.txt", "world//abs.txt", "world/C:/evil.txt", "world/a\\..\\..\\evil.txt",
                                  "world/..\\evil.txt", "../evil.txt", "/evil.txt", "world/nul" };
        for (const QString& name : bad) {
            QTest::newRow(qPrintable(name + " (entry only)")) << name << false;
            if (name.startsWith("world/"))
                QTest::newRow(qPrintable(name + " (in manifest)")) << name << true;
        }
    }

    void zipSlipIsRejected()
    {
        QFETCH(QString, entry);
        QFETCH(bool, inManifest);
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString outside = root.filePath("outside");  // anything that escapes staging would land around here
        const Tree original = worldTree();
        const QString saves = root.filePath("outside/saves");
        const QString world = makeWorld(saves, "W", original);
        const QString dir = root.filePath("b");
        const QString zip = craftZip(dir, "20260101-000000-000-manual.zip", { "world/ok.txt", entry },
                                     inManifest ? QStringList{ "ok.txt", entry.mid(6) } : QStringList{ "ok.txt" });
        QVERIFY(!zip.isEmpty());

        QVERIFY(!verifyBackup(zip).ok);
        const Tree before = readTree(root.path());
        const RestoreResult r = restoreBackup(zip, world, dir, metaFor("W"), removeHooks());
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
        // the world is intact and nothing was created anywhere outside (staging is gone, no evil.txt / abs.txt / ok.txt)
        QCOMPARE(readTree(world), original);
        QVERIFY(zeroLeftovers(saves).isEmpty());
        QCOMPARE(readTree(root.path()).keys(), before.keys());
        QVERIFY(!QFileInfo::exists(root.filePath("evil.txt")));
        QVERIFY(!QFileInfo::exists(outside + "/evil.txt"));
        QVERIFY(!QFileInfo::exists(saves + "/evil.txt"));
        QVERIFY(listBackups(dir).size() >= 1);
    }

    // ---------------------------------------------------------------- restore

    void restoreHappyPath()
    {
        QTemporaryDir root;
        const QString saves = root.filePath("saves");
        const Tree original = worldTree(1);
        const QString world = makeWorld(saves, "W", original);
        const QString dir = root.filePath("b");
        const BackupResult backup = createBackup(world, dir, Kind::Manual, metaFor("W"));
        QVERIFY(backup.ok);

        // the world changes after the backup: edited file, new file, removed file
        Tree changed = worldTree(50);
        changed["new/file.txt"] = QByteArray("only in the new world");
        QVERIFY(QDir(world).removeRecursively());
        makeWorld(saves, "W", changed);
        const Tree beforeRestore = readTree(world);

        int trashed = 0;
        const RestoreResult r = restoreBackup(backup.zipPath, world, dir, metaFor("W"), removeHooks(&trashed));
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(readTree(world), original);  // byte for byte, "new/file.txt" is gone
        QVERIFY(zeroLeftovers(saves).isEmpty());
        QCOMPARE(trashed, 1);

        // the replaced world is in a valid safety backup
        QVERIFY(!r.safetyZip.isEmpty());
        QVERIFY(r.safetyZip.endsWith("-safety.zip"));
        QVERIFY(verifyBackup(r.safetyZip).ok);
        const QList<BackupInfo> list = listBackups(dir);
        QCOMPARE(list.size(), 2);
        int safety = 0;
        for (const auto& b : list)
            safety += b.kind == Kind::Safety;
        QCOMPARE(safety, 1);
        // restoring the safety backup brings the changed world back
        const RestoreResult back = restoreBackup(r.safetyZip, world, dir, metaFor("W"), removeHooks());
        QVERIFY2(back.ok, qPrintable(back.error));
        QCOMPARE(readTree(world), beforeRestore);
    }

    void restoreIntoMissingWorldFolder()
    {
        QTemporaryDir root;
        const QString saves = root.filePath("saves");
        const Tree original = worldTree();
        const QString world = makeWorld(saves, "W", original);
        const QString dir = root.filePath("b");
        const BackupResult backup = createBackup(world, dir, Kind::Manual, metaFor("W"));
        QVERIFY(backup.ok);
        QVERIFY(QDir(world).removeRecursively());

        const RestoreResult r = restoreBackup(backup.zipPath, world, dir, metaFor("W"), removeHooks());
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.safetyZip.isEmpty());  // nothing to save
        QCOMPARE(readTree(world), original);
        QVERIFY(zeroLeftovers(saves).isEmpty());
    }

    void restoreOfDamagedBackupLeavesWorldUntouched()
    {
        QTemporaryDir root;
        const QString saves = root.filePath("saves");
        const Tree original = worldTree(7);
        const QString world = makeWorld(saves, "W", original);
        const QString dir = root.filePath("b");
        const BackupResult backup = createBackup(world, dir, Kind::Manual, metaFor("W"));
        QVERIFY(backup.ok);

        Zip z = readZip(backup.zipPath);
        for (auto& e : z.entries)
            if (e.name == "world/data/raids.dat")
                e.data[10] = char(e.data[10] ^ 0x40);
        const QString damaged = dir + "/20990101-000000-000-manual.zip";
        QVERIFY(writeZip(damaged, z));

        const Tree before = readTree(world, true);
        const RestoreResult r = restoreBackup(damaged, world, dir, metaFor("W"), removeHooks());
        QVERIFY(!r.ok);
        QVERIFY(r.safetyZip.isEmpty());  // verification comes first: no safety backup, nothing touched
        QCOMPARE(readTree(world, true), before);
        QVERIFY(zeroLeftovers(saves).isEmpty());
        QCOMPARE(QDir(dir).entryList({ "*-safety.zip" }).size(), 0);

        // a truncated file and garbage behave the same
        QFile(backup.zipPath).copy(dir + "/trunc.zip");
        {
            QFile f(dir + "/trunc.zip");
            QVERIFY(f.open(QIODevice::ReadWrite));
            f.resize(f.size() / 2);
        }
        QVERIFY(!restoreBackup(dir + "/trunc.zip", world, dir, metaFor("W"), removeHooks()).ok);
        writeFile(dir + "/garbage.zip", randomBytes(3000, 3));
        QVERIFY(!restoreBackup(dir + "/garbage.zip", world, dir, metaFor("W"), removeHooks()).ok);
        QCOMPARE(readTree(world, true), before);
        QVERIFY(zeroLeftovers(saves).isEmpty());
    }

    void restoreRestoresOriginalWhenFinalRenameFails()
    {
        QTemporaryDir root;
        const QString saves = root.filePath("saves");
        const Tree original = worldTree(3);
        const QString world = makeWorld(saves, "W", original);
        const QString dir = root.filePath("b");
        const BackupResult backup = createBackup(world, dir, Kind::Manual, metaFor("W"));
        QVERIFY(backup.ok);
        // make the world differ from the backup so a wrong outcome would be visible
        QVERIFY(writeFile(world + "/data/raids.dat", QByteArray("changed since the backup")));
        const Tree before = readTree(world, true);

        QStringList renames;
        Hooks hooks = removeHooks();
        hooks.rename = [&](const QString& from, const QString& to) {
            renames << QFileInfo(from).fileName() + " -> " + QFileInfo(to).fileName();
            if (QFileInfo(from).fileName().startsWith(".zero-restore-"))  // the staging -> world rename fails
                return false;
            return QDir().rename(from, to);
        };
        const RestoreResult r = restoreBackup(backup.zipPath, world, dir, metaFor("W"), hooks);
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
        QCOMPARE(readTree(world, true), before);  // the original world is back, session.lock included
        QVERIFY(zeroLeftovers(saves).isEmpty());   // staging deleted, replaced folder renamed back
        QCOMPARE(renames.size(), 3);               // world -> replaced, staging -> world (fails), replaced -> world
        QVERIFY(verifyBackup(r.safetyZip).ok);     // the safety backup exists regardless
    }

    void restoreReportsWhenRollbackAlsoFails()
    {
        QTemporaryDir root;
        const QString saves = root.filePath("saves");
        const Tree original = worldTree(4);
        const QString world = makeWorld(saves, "W", original);
        const QString dir = root.filePath("b");
        const BackupResult backup = createBackup(world, dir, Kind::Manual, metaFor("W"));
        QVERIFY(backup.ok);

        Hooks hooks = removeHooks();
        hooks.rename = [&](const QString& from, const QString& to) {
            if (QFileInfo(to).fileName() == "W")  // both attempts to put a folder at the world path fail
                return false;
            return QDir().rename(from, to);
        };
        const RestoreResult r = restoreBackup(backup.zipPath, world, dir, metaFor("W"), hooks);
        QVERIFY(!r.ok);
        // the original is NOT deleted: it sits in .zero-replaced-* and the message says where
        const QStringList left = zeroLeftovers(saves);
        QCOMPARE(left.size(), 1);
        QVERIFY(left.first().startsWith(".zero-replaced-W-"));
        QVERIFY2(r.error.contains(".zero-replaced-W-"), qPrintable(r.error));
        QCOMPARE(readTree(saves + "/" + left.first()), original);
        QVERIFY(verifyBackup(r.safetyZip).ok);
    }

    void restoreFailsWhenWorldCannotBeMovedAside()
    {
        QTemporaryDir root;
        const QString saves = root.filePath("saves");
        const Tree original = worldTree(5);
        const QString world = makeWorld(saves, "W", original);
        const QString dir = root.filePath("b");
        const BackupResult backup = createBackup(world, dir, Kind::Manual, metaFor("W"));
        QVERIFY(backup.ok);
        const Tree before = readTree(world, true);

        Hooks hooks = removeHooks();
        hooks.rename = [](const QString&, const QString&) { return false; };
        const RestoreResult r = restoreBackup(backup.zipPath, world, dir, metaFor("W"), hooks);
        QVERIFY(!r.ok);
        QCOMPARE(readTree(world, true), before);
        QVERIFY(zeroLeftovers(saves).isEmpty());
    }

    void restoreSucceedsWhenTrashFails()
    {
        QTemporaryDir root;
        const QString saves = root.filePath("saves");
        const Tree original = worldTree(6);
        const QString world = makeWorld(saves, "W", original);
        const QString dir = root.filePath("b");
        const BackupResult backup = createBackup(world, dir, Kind::Manual, metaFor("W"));
        QVERIFY(backup.ok);
        QVERIFY(writeFile(world + "/extra.txt", QByteArray("x")));

        Hooks hooks;
        hooks.trashPath = [](const QString&) { return false; };
        const RestoreResult r = restoreBackup(backup.zipPath, world, dir, metaFor("W"), hooks);
        QVERIFY2(r.ok, qPrintable(r.error));  // the old world is in the safety backup; the leftover folder is only logged
        QCOMPARE(readTree(world), original);
        QCOMPARE(zeroLeftovers(saves).size(), 1);
        QVERIFY(zeroLeftovers(saves).first().startsWith(".zero-replaced-W-"));
    }

    void restoreRefusesWithoutFreeSpaceBeforeTouchingAnything()
    {
        QTemporaryDir root;
        const QString saves = root.filePath("saves");
        const QString world = makeWorld(saves, "W", worldTree());
        const QString dir = root.filePath("b");
        const BackupResult backup = createBackup(world, dir, Kind::Manual, metaFor("W"));
        QVERIFY(backup.ok);
        const Tree before = readTree(world, true);
        Hooks hooks = removeHooks();
        hooks.freeBytes = [](const QString&) -> qint64 { return 10; };
        const RestoreResult r = restoreBackup(backup.zipPath, world, dir, metaFor("W"), hooks);
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("Not enough free disk space"));
        QCOMPARE(readTree(world, true), before);
        QVERIFY(zeroLeftovers(saves).isEmpty());
        QCOMPARE(QDir(dir).entryList({ "*-safety.zip" }).size(), 0);
    }

    void scannerIgnoresZeroFolders()
    {
        QTemporaryDir root;
        const QString saves = root.filePath("saves");
        makeWorld(saves, "Real", worldTree());
        makeWorld(saves, ".zero-restore-Real-20260101000000000", worldTree());
        makeWorld(saves, ".zero-replaced-Real-20260101000000000", worldTree());
        const auto worlds = Zero::Worlds::scanWorldsDir("i", "I", saves);
        QCOMPARE(worlds.size(), 1);
        QCOMPARE(worlds.first().folderName, QString("Real"));
    }

    // ---------------------------------------------------------------- list / retention / delete

    void listingNewestFirstIgnoresPartAndFlagsGarbage()
    {
        QTemporaryDir root;
        const QString world = makeWorld(root.filePath("saves"), "W", worldTree());
        const QString dir = root.filePath("b");
        const BackupResult a = createBackup(world, dir, Kind::Manual, metaFor("W"));
        QTest::qWait(5);
        const BackupResult b = createBackup(world, dir, Kind::Safety, metaFor("W"));
        QVERIFY(a.ok && b.ok);
        writeFile(dir + "/20260101-000000-000-manual.zip.part", randomBytes(100, 1));
        writeFile(dir + "/garbage.zip", randomBytes(2000, 2));
        writeFile(dir + "/notes.txt", QByteArray("hi"));

        const QList<BackupInfo> list = listBackups(dir);
        QCOMPARE(list.size(), 3);  // a, b, garbage; no .part, no txt
        int invalid = 0;
        QList<BackupInfo> valid;
        for (const auto& i : list) {
            if (i.valid)
                valid << i;
            else {
                ++invalid;
                QVERIFY(i.zipPath.endsWith("garbage.zip"));
                QCOMPARE(i.fileSize, qint64(2000));
            }
        }
        QCOMPARE(invalid, 1);
        QCOMPARE(valid.size(), 2);
        QVERIFY(valid[0].createdMs >= valid[1].createdMs);
        QCOMPARE(valid[0].zipPath, b.zipPath);
        QCOMPARE(valid[0].kind, Kind::Safety);
        QCOMPARE(valid[1].kind, Kind::Manual);
        QCOMPARE(valid[0].worldName, QString("My World"));
        QVERIFY(valid[0].totalBytes > 1'000'000);
        QVERIFY(valid[0].fileSize > 0);
        QVERIFY(listBackups(root.filePath("missing")).isEmpty());
    }

    void retentionKeepsTenManualAndThreeSafety()
    {
        QTemporaryDir root;
        const QString dir = root.filePath("b");
        QStringList manual, safety;
        for (int i = 0; i < 14; ++i)
            manual << touchBackupName(dir, QString("202601%1-101010-123-manual.zip").arg(i + 1, 2, 10, QChar('0')));
        for (int i = 0; i < 5; ++i)
            safety << touchBackupName(dir, QString("202601%1-111111-456-safety.zip").arg(i + 1, 2, 10, QChar('0')));
        const QString other = touchBackupName(dir, "my-notes.zip");               // not ZERO's: untouched
        const QString other2 = touchBackupName(dir, "20260101-1-manual.zip");     // wrong pattern: untouched
        const QString part = touchBackupName(dir, "20250101-101010-123-manual.zip.part");
        const QString txt = touchBackupName(dir, "20250101-101010-123-manual.txt");

        QCOMPARE(applyRetention(dir), 4 + 2);
        for (int i = 0; i < 14; ++i)
            QCOMPARE(QFileInfo::exists(manual[i]), i >= 4);  // the 10 newest remain
        for (int i = 0; i < 5; ++i)
            QCOMPARE(QFileInfo::exists(safety[i]), i >= 2);  // the 3 newest remain
        for (const QString& p : { other, other2, part, txt })
            QVERIFY2(QFileInfo::exists(p), qPrintable(p));
        QCOMPARE(applyRetention(dir), 0);  // idempotent
    }

    void retentionKeepsFiveAutoIndependently()
    {
        QTemporaryDir root;
        const QString dir = root.filePath("b");
        QStringList autos;
        for (int i = 0; i < 7; ++i)
            autos << touchBackupName(dir, QString("202601%1-101010-123-auto.zip").arg(i + 1, 2, 10, QChar('0')));
        const QString manual = touchBackupName(dir, "20250101-101010-123-manual.zip");  // older than every auto one
        const QString safety = touchBackupName(dir, "20250102-101010-123-safety.zip");

        QCOMPARE(applyRetention(dir), 2);  // only the two oldest auto backups go
        for (int i = 0; i < 7; ++i)
            QCOMPARE(QFileInfo::exists(autos[i]), i >= 2);
        QVERIFY(QFileInfo::exists(manual));
        QVERIFY(QFileInfo::exists(safety));
        QCOMPARE(applyRetention(dir), 0);

        // the protected list is honoured for auto files too; manual / safety keep counting separately
        QCOMPARE(applyRetention(dir, { autos[2] }, 10, 3, 1), 3);
        QVERIFY(QFileInfo::exists(autos[2]));
        QVERIFY(QFileInfo::exists(autos[6]));
        for (int i : { 3, 4, 5 })
            QVERIFY(!QFileInfo::exists(autos[i]));
        QVERIFY(QFileInfo::exists(manual));
        QVERIFY(QFileInfo::exists(safety));

        // many manual backups never touch the auto ones
        for (int i = 0; i < 12; ++i)
            touchBackupName(dir, QString("202602%1-101010-123-manual.zip").arg(i + 1, 2, 10, QChar('0')));
        applyRetention(dir);
        QVERIFY(QFileInfo::exists(autos[2]));
        QVERIFY(QFileInfo::exists(autos[6]));
    }

    void autoBackupKindRoundTrip()
    {
        QTemporaryDir root;
        const QString world = makeWorld(root.filePath("saves"), "W", worldTree());
        const QString dir = root.filePath("b");
        const BackupResult r = createBackup(world, dir, Kind::Auto, metaFor("W"));
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.zipPath.endsWith("-auto.zip"));
        QVERIFY(verifyBackup(r.zipPath).ok);
        const QList<BackupInfo> list = listBackups(dir);
        QCOMPARE(list.size(), 1);
        QVERIFY(list.first().valid);
        QCOMPARE(list.first().kind, Kind::Auto);

        // an auto backup restores like any other (a safety snapshot of the current world comes first)
        const RestoreResult rr = restoreBackup(r.zipPath, world, dir, metaFor("W"), removeHooks());
        QVERIFY2(rr.ok, qPrintable(rr.error));
        QVERIFY(rr.safetyZip.endsWith("-safety.zip"));
        QCOMPARE(readTree(world), worldTree());
    }

    void retentionNeverDeletesProtectedOrNewest()
    {
        QTemporaryDir root;
        const QString dir = root.filePath("b");
        const QString oldest = touchBackupName(dir, "20260101-101010-123-manual.zip");
        const QString mid = touchBackupName(dir, "20260102-101010-123-manual.zip");
        const QString newest = touchBackupName(dir, "20260103-101010-123-manual.zip");
        // keepManual = 0 would delete everything: the protected one survives, nothing else does
        QCOMPARE(applyRetention(dir, { newest }, 0, 0), 2);
        QVERIFY(QFileInfo::exists(newest));
        QVERIFY(!QFileInfo::exists(oldest));
        QVERIFY(!QFileInfo::exists(mid));
        // protecting an old one keeps it while the rest follows the rule
        const QString a = touchBackupName(dir, "20260110-000000-000-safety.zip");
        const QString b = touchBackupName(dir, "20260111-000000-000-safety.zip");
        const QString c = touchBackupName(dir, "20260112-000000-000-safety.zip");
        QCOMPARE(applyRetention(dir, { a }, 10, 1), 1);
        QVERIFY(QFileInfo::exists(a));
        QVERIFY(!QFileInfo::exists(b));
        QVERIFY(QFileInfo::exists(c));
    }

    void retentionOrdersSameMillisecondBySuffix()
    {
        QTemporaryDir root;
        const QString dir = root.filePath("b");
        const QString first = touchBackupName(dir, "20260101-101010-123-manual.zip");
        const QString second = touchBackupName(dir, "20260101-101010-123-2-manual.zip");
        const QString tenth = touchBackupName(dir, "20260101-101010-123-10-manual.zip");
        applyRetention(dir, {}, 1, 3);
        QVERIFY(QFileInfo::exists(tenth));  // -10 is newer than -2 (numeric, not text, order)
        QVERIFY(!QFileInfo::exists(second));
        QVERIFY(!QFileInfo::exists(first));
    }

    void retentionWithRealBackups()
    {
        QTemporaryDir root;
        const QString world = makeWorld(root.filePath("saves"), "W", worldTree());
        const QString dir = root.filePath("b");
        QString last;
        for (int i = 0; i < 12; ++i) {
            const BackupResult r = createBackup(world, dir, Kind::Manual, metaFor("W"));
            QVERIFY2(r.ok, qPrintable(r.error));
            last = r.zipPath;
        }
        QCOMPARE(applyRetention(dir, { last }), 2);
        const QList<BackupInfo> list = listBackups(dir);
        QCOMPARE(list.size(), 10);
        QCOMPARE(list.first().zipPath, last);
        for (const auto& i : list)
            QVERIFY(i.valid);
    }

    void deleteBackupOnlyInsideRoot()
    {
        QTemporaryDir root;
        const QString backups = root.filePath("zero/backups");
        const QString dir = backups + "/inst/W";
        const QString inside = touchBackupName(dir, "20260101-000000-000-manual.zip");
        const QString outside = touchBackupName(root.filePath("elsewhere"), "20260101-000000-000-manual.zip");
        const QString notZip = touchBackupName(dir, "important.txt");
        const QString sneaky = backups + "/../elsewhere/20260101-000000-000-manual.zip";

        QString error;
        QVERIFY(!deleteBackup(outside, backups, &error, removeHooks()));
        QVERIFY(!error.isEmpty());
        QVERIFY(QFileInfo::exists(outside));
        QVERIFY(!deleteBackup(sneaky, backups, &error, removeHooks()));
        QVERIFY(QFileInfo::exists(outside));
        QVERIFY(!deleteBackup(notZip, backups, &error, removeHooks()));
        QVERIFY(QFileInfo::exists(notZip));
        QVERIFY(!deleteBackup(backups, backups, &error, removeHooks()));
        QVERIFY(deleteBackup(inside, backups, &error, removeHooks()));
        QVERIFY(!QFileInfo::exists(inside));
        QVERIFY(!deleteBackup(inside, backups, &error, removeHooks()));  // already gone
    }

    void backupPathHelpers()
    {
        QCOMPARE(backupsRoot("/data"), QString("/data/zero/backups"));
        QCOMPARE(backupDirFor("/data", "inst", "My World"), QString("/data/zero/backups/inst/My World"));
        bool ok = false;
        QCOMPARE(kindFromString("safety", &ok), Kind::Safety);
        QVERIFY(ok);
        QCOMPARE(kindFromString("auto", &ok), Kind::Auto);
        QVERIFY(ok);
        QCOMPARE(kindFromString("manual", &ok), Kind::Manual);
        QVERIFY(ok);
        for (const Kind k : { Kind::Manual, Kind::Safety, Kind::Auto })
            QCOMPARE(kindFromString(kindToString(k), &ok), k);
        QCOMPARE(kindToString(Kind::Auto), QString("auto"));
        kindFromString("bogus", &ok);
        QVERIFY(!ok);
    }

    // ---------------------------------------------------------------- runner

    void runnerRefusesWhileInstanceRuns()
    {
        QTemporaryDir root;
        const QString saves = root.filePath("saves");
        makeWorld(saves, "W", worldTree());
        const auto entry = entryFor(saves, "W");
        bool running = true;
        WorldBackupJobs jobs(root.filePath("zero/backups"), [&](const QString&) { return running; });
        QSignalSpy finished(&jobs, &WorldBackupJobs::jobFinished);
        QSignalSpy started(&jobs, &WorldBackupJobs::jobStarted);

        QVERIFY(!jobs.backup(entry));
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.first().at(1).toBool(), false);
        QVERIFY(!finished.first().at(2).toString().isEmpty());
        QCOMPARE(started.size(), 0);
        QVERIFY(!jobs.isBusy(entry.key()));
        QVERIFY(!WorldBackupJobs::isInstanceLocked("inst"));
        QVERIFY(!QFileInfo::exists(jobs.backupDir(entry)) || QDir(jobs.backupDir(entry)).entryList({ "*.zip*" }).isEmpty());

        QVERIFY(!jobs.restore(entry, jobs.backupDir(entry) + "/x.zip"));
        QVERIFY(!jobs.remove(entry, jobs.backupDir(entry) + "/x.zip"));
        QCOMPARE(finished.size(), 3);
        QCOMPARE(readTree(entry.folderPath, true).size(), worldTree().size() + 1);

        running = false;  // and it works once the game is closed
        QVERIFY(jobs.backup(entry));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 4, 15000);
        QVERIFY(finished.last().at(1).toBool());
    }

    void runnerOneJobPerWorldAndLaunchLock()
    {
        QTemporaryDir root;
        const QString saves = root.filePath("saves");
        makeWorld(saves, "W", worldTree());
        makeWorld(saves, "V", worldTree(9));
        const auto w = entryFor(saves, "W");
        const auto v = entryFor(saves, "V");
        WorldBackupJobs jobs(root.filePath("zero/backups"), {});
        QSignalSpy started(&jobs, &WorldBackupJobs::jobStarted);
        QSignalSpy finished(&jobs, &WorldBackupJobs::jobFinished);
        QSignalSpy changed(&jobs, &WorldBackupJobs::backupsChanged);

        QVERIFY(jobs.backup(w));
        QVERIFY(jobs.isBusy(w.key()));
        QCOMPARE(jobs.operation(w.key()), WorldBackupJobs::Op::Backup);
        QVERIFY(WorldBackupJobs::isInstanceLocked("inst"));  // launching "inst" is blocked while a job runs
        QVERIFY(!jobs.backup(w));                            // second request for the same world: refused
        QCOMPARE(finished.size(), 1);
        QVERIFY(!finished.last().at(1).toBool());
        QVERIFY(jobs.isBusy(w.key()));
        QVERIFY(jobs.backup(v));  // another world is fine
        QCOMPARE(started.size(), 2);

        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 3, 20000);
        QTRY_VERIFY_WITH_TIMEOUT(!jobs.isBusy(v.key()) && !jobs.isBusy(w.key()), 20000);
        QVERIFY(!WorldBackupJobs::isInstanceLocked("inst"));
        QCOMPARE(changed.size(), 2);
        QVERIFY(finished.at(1).at(1).toBool() && finished.at(2).at(1).toBool());
        QCOMPARE(finished.at(1).at(2).toString(), QString("Backup created"));
        QCOMPARE(listBackups(jobs.backupDir(w)).size(), 1);
        QCOMPARE(listBackups(jobs.backupDir(v)).size(), 1);
    }

    void runnerRestoreAndRemove()
    {
        QTemporaryDir root;
        const QString saves = root.filePath("saves");
        const Tree original = worldTree(11);
        makeWorld(saves, "W", original);
        const auto w = entryFor(saves, "W");
        WorldBackupJobs jobs(root.filePath("zero/backups"), {});
        jobs.setHooks(removeHooks());
        QSignalSpy finished(&jobs, &WorldBackupJobs::jobFinished);
        QSignalSpy restored(&jobs, &WorldBackupJobs::worldRestored);
        QSignalSpy listed(&jobs, &WorldBackupJobs::backupsListed);

        QVERIFY(jobs.backup(w));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);
        const QString zip = listBackups(jobs.backupDir(w)).first().zipPath;

        QVERIFY(writeFile(w.folderPath + "/data/raids.dat", QByteArray("changed")));
        QVERIFY(!jobs.restore(w, root.filePath("elsewhere.zip")));  // not this world's backup
        QCOMPARE(finished.size(), 2);
        QVERIFY(jobs.restore(w, zip));
        QCOMPARE(jobs.operation(w.key()), WorldBackupJobs::Op::Restore);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 3, 30000);
        QVERIFY2(finished.last().at(1).toBool(), qPrintable(finished.last().at(2).toString()));
        QCOMPARE(finished.last().at(2).toString(), QString("World restored"));
        QCOMPARE(restored.size(), 1);
        QCOMPARE(readTree(w.folderPath), original);
        QCOMPARE(listBackups(jobs.backupDir(w)).size(), 2);  // the manual one + the safety backup

        // listing is asynchronous and the newest request wins
        jobs.refreshList(w);
        jobs.refreshList(w);
        QTRY_VERIFY_WITH_TIMEOUT(listed.size() >= 1, 10000);
        QCOMPARE(listed.last().at(0).toString(), w.key());
        QCOMPARE(listed.last().at(1).value<QList<BackupInfo>>().size(), 2);

        QVERIFY(jobs.remove(w, zip));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 4, 10000);
        QVERIFY(finished.last().at(1).toBool());
        QVERIFY(!QFileInfo::exists(zip));
        QCOMPARE(listBackups(jobs.backupDir(w)).size(), 1);
    }
};

QTEST_GUILESS_MAIN(WorldBackupTest)

#include "WorldBackup_test.moc"
