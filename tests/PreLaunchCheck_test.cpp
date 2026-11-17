// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the pure part of "Before you play": the signature list, the jar scanner and its cache, the system traces, the
// checks (runChecks) and the dialog's logic (Session). Every jar here is SYNTHETIC, built in a temporary folder; no real malware is used.
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <limits>

#include "archive/ArchiveWriter.h"
#include "zero/prelaunch/PreLaunchCheck.h"
#include "zero/security/JarScanner.h"
#include "zero/security/MalwareSignatures.h"
#include "zero/security/SystemIndicators.h"

using namespace Zero;
using namespace Zero::Security;
using namespace Zero::PreLaunch;

namespace {

using Entries = QList<QPair<QString, QByteArray>>;

QString S(const char* text)
{
    return QString::fromUtf8(text);
}

bool writeJar(const QString& path, const Entries& entries)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    MMCZip::ArchiveWriter w(path);
    if (!w.open()) {
        return false;
    }
    for (const auto& e : entries) {
        if (!w.addFile(e.first, e.second)) {
            return false;
        }
    }
    return bool(w.close());
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

QByteArray readFile(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

const QDateTime kBaseTime = QDateTime::fromSecsSinceEpoch(1700000000);

bool setMtime(const QString& path, const QDateTime& when = kBaseTime)
{
    QFile f(path);
    return f.open(QIODevice::ReadWrite) && f.setFileTime(when, QFileDevice::FileModificationTime);
}

QByteArray sha1Of(const QString& path)
{
    return QCryptographicHash::hash(readFile(path), QCryptographicHash::Sha1).toHex();
}

QByteArray sha256Of(const QString& path)
{
    return QCryptographicHash::hash(readFile(path), QCryptographicHash::Sha256).toHex();
}

QJsonObject sig(const QString& id, const QString& kind, const QString& pattern)
{
    return QJsonObject{ { S("id"), id }, { S("name"), S("Test ") + id }, { S("info"), S("https://example.invalid/") + id },
                        { S("kind"), kind }, { S("pattern"), pattern }, { S("source"), S("test") } };
}

QByteArray listJson(const QJsonArray& signatures, int version = 1)
{
    return QJsonDocument(QJsonObject{ { S("version"), version }, { S("signatures"), signatures } }).toJson(QJsonDocument::Compact);
}

SignatureList listOf(const QJsonArray& signatures, int version = 1)
{
    return parseSignatures(listJson(signatures, version));
}

const QString kMarker = S("ZEROTEST_MARKER_9f3a");

/// The list most tests use: a class-bytes marker and the path of a class (the kind used for the "dev.neko" client).
SignatureList markerList()
{
    return listOf({ sig(S("t-bytes"), S("class-bytes"), kMarker), sig(S("t-name"), S("class-name"), S("dev.neko.nekoclient.")) });
}

QByteArray badClass()
{
    return QByteArray("\xCA\xFE\xBA\xBE padding ") + kMarker.toUtf8() + QByteArray(" more padding");
}

QByteArray cleanClass()
{
    return QByteArray("\xCA\xFE\xBA\xBE nothing to see here");
}

bool writeCleanJar(const QString& path, const QString& id = QString(), const QString& version = S("1.0.0"))
{
    Entries e{ { S("a/Clean.class"), cleanClass() } };
    if (!id.isEmpty()) {
        e.append({ S("fabric.mod.json"), QStringLiteral(R"({"schemaVersion":1,"id":"%1","version":"%2"})").arg(id, version).toUtf8() });
    }
    return writeJar(path, e) && setMtime(path);
}

bool writeBadJar(const QString& path)
{
    return writeJar(path, { { S("a/Bad.class"), badClass() } }) && setMtime(path);
}

/// All files below a folder with their size and content hash: to prove nothing was written.
QStringList snapshot(const QString& root)
{
    QStringList out;
    QDirIterator it(root, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info(it.next());
        out.append(info.filePath() + S("|") + QString::number(info.size()) + S("|") + QString::fromLatin1(sha1Of(info.filePath())));
    }
    out.sort();
    return out;
}

SystemEnv emptySystemEnv()
{
    return SystemEnv();  // no folders, no Run key: nothing to find
}

struct Layout {
    QString root;  // instance root
    QString game;  // <root>/minecraft
    QString mods;  // <game>/mods
};

Layout makeLayout(const QTemporaryDir& tmp, const QString& name = S("inst"))
{
    Layout l;
    l.root = QDir(tmp.path()).filePath(name);
    l.game = l.root + S("/minecraft");
    l.mods = l.game + S("/mods");
    QDir().mkpath(l.mods);
    return l;
}

Env makeEnv(const Layout& l, const SignatureList* list, bool fabric = false)
{
    Env env;
    env.instanceName = S("Test");
    env.instanceRoot = l.root;
    env.gameRoot = l.game;
    env.signatures = list;
    env.system = emptySystemEnv();
    env.context = [fabric]() {
        Mods::InstanceContext c;
        if (fabric) {
            c.loader = Mods::InstanceLoader::Fabric;
            c.mcVersion = S("1.20.1");
            c.loaderVersion = S("0.15.11");
        }
        return c;
    };
    return env;
}

const Item* findItem(const QList<Item>& items, const QString& id)
{
    for (const Item& item : items) {
        if (item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

QList<Fix::Kind> fixKinds(const Item& item)
{
    QList<Fix::Kind> kinds;
    for (const Fix& f : item.fixes) {
        kinds.append(f.kind);
    }
    return kinds;
}

}  // namespace

class PreLaunchCheckTest : public QObject {
    Q_OBJECT

   private slots:
    // ------------------------------------------------------------------------------------------------------------ the list

    void parseValidList()
    {
        const QByteArray hash1(40, 'a');
        const QByteArray hash256(64, 'B');  // upper case is accepted and lower-cased
        const SignatureList list = listOf({ sig(S("b"), S("class-bytes"), S("hello")), sig(S("n"), S("class-name"), S("dev.example.mal.")),
                                            sig(S("h1"), S("sha1"), QString::fromLatin1(hash1)),
                                            sig(S("h2"), S("sha256"), QString::fromLatin1(hash256)) },
                                          7);
        QVERIFY2(list.error.isEmpty(), qPrintable(list.error));
        QCOMPARE(list.version, 7);
        QVERIFY(list.versionKey.startsWith(S("7-")));
        QCOMPARE(list.items.size(), 4);
        QCOMPARE(list.items.at(0).kind, Signature::Kind::ClassBytes);
        QCOMPARE(list.items.at(0).pattern, QByteArray("hello"));
        QCOMPARE(list.items.at(1).kind, Signature::Kind::ClassName);
        QCOMPARE(list.items.at(1).pattern, QByteArray("dev/example/mal/"));  // dots read as slashes
        QCOMPARE(list.items.at(2).pattern, hash1);
        QCOMPARE(list.items.at(3).pattern, hash256.toLower());
        QVERIFY(list.hasKind(Signature::Kind::Sha256));
        QVERIFY(!listOf({ sig(S("b"), S("class-bytes"), S("x")) }).hasKind(Signature::Kind::Sha1));
        QVERIFY(list.find(S("n")) != nullptr);
        QVERIFY(list.find(S("nope")) == nullptr);
        QCOMPARE(list.find(S("n"))->infoUrl, S("https://example.invalid/n"));
    }

    void parseDropsBadEntries()
    {
        const SignatureList list = listOf({ sig(S("ok"), S("class-bytes"), S("fine")),
                                            sig(S("unknown"), S("regex"), S("x")),                    // unknown kind
                                            sig(S("short"), S("sha1"), S("abc123")),                  // malformed hash
                                            sig(S("nothex"), S("sha256"), QString(64, QLatin1Char('z'))),  // not hex
                                            sig(S("empty"), S("class-bytes"), QString()),             // empty pattern
                                            sig(QString(), S("class-bytes"), S("noid")),              // no id
                                            sig(S("badhex"), S("class-bytes"), S("hex:")) });         // "hex:" with nothing
        QVERIFY(list.error.isEmpty());
        QCOMPARE(list.items.size(), 1);
        QCOMPARE(list.items.at(0).id, S("ok"));
    }

    void parseHexPattern()
    {
        const SignatureList list = listOf({ sig(S("hx"), S("class-bytes"), S("hex:cafebabe00ff")) });
        QCOMPARE(list.items.size(), 1);
        QCOMPARE(list.items.at(0).pattern, QByteArray::fromHex("cafebabe00ff"));
        // and it matches those raw bytes inside a class
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString jar = tmp.filePath(S("hex.jar"));
        QVERIFY(writeJar(jar, { { S("x/Y.class"), QByteArray("aa") + QByteArray::fromHex("cafebabe00ff") + QByteArray("bb") } }));
        QCOMPARE(scanJar(jar, list).hits.size(), 1);
    }

    void parseMalformedJson()
    {
        QVERIFY(!parseSignatures("{ not json").error.isEmpty());
        QVERIFY(!parseSignatures("[1,2]").error.isEmpty());
        QVERIFY(parseSignatures("{}").items.isEmpty());
    }

    void versionKeyFollowsTheBytes()
    {
        const QByteArray a = listJson({ sig(S("x"), S("class-bytes"), S("one")) });
        const QByteArray b = listJson({ sig(S("x"), S("class-bytes"), S("two")) });
        const QByteArray spaced = a + QByteArray("\n");
        QCOMPARE(parseSignatures(a).versionKey, parseSignatures(a).versionKey);
        QVERIFY(parseSignatures(a).versionKey != parseSignatures(b).versionKey);
        QVERIFY(parseSignatures(a).versionKey != parseSignatures(spaced).versionKey);  // even when nobody bumped `version`
    }

    void builtinListLoads()
    {
        const SignatureList& list = builtinSignatures();
        QVERIFY2(list.error.isEmpty(), qPrintable(list.error));
        QCOMPARE(list.items.size(), 27);
        QVERIFY(!list.versionKey.isEmpty());
        QSet<QString> ids;
        for (const Signature& s : list.items) {
            QVERIFY(!s.id.isEmpty());
            QVERIFY(!s.name.isEmpty());
            QVERIFY(!s.pattern.isEmpty());
            QVERIFY(s.infoUrl.startsWith(S("https://")));
            QVERIFY(!s.source.isEmpty());
            ids.insert(s.id);
        }
        QCOMPARE(ids.size(), 27);  // unique ids
        QVERIFY(&builtinSignatures() == &list);
    }

    // ------------------------------------------------------------------------------------------------------------ scanJar

    void scanClassBytes()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString bad = tmp.filePath(S("bad.jar"));
        QVERIFY(writeBadJar(bad));
        const JarScan scan = scanJar(bad, markerList());
        QVERIFY(scan.readable);
        QCOMPARE(scan.hits.size(), 1);
        QCOMPARE(scan.hits.at(0).signatureId, S("t-bytes"));
        QCOMPARE(scan.hits.at(0).where, S("a/Bad.class"));
        QCOMPARE(scan.hits.at(0).name, S("Test t-bytes"));
        QCOMPARE(scan.hits.at(0).infoUrl, S("https://example.invalid/t-bytes"));

        // the same bytes in a file that is not a .class are not a hit
        const QString notClass = tmp.filePath(S("data.jar"));
        QVERIFY(writeJar(notClass, { { S("assets/readme.txt"), badClass() } }));
        QVERIFY(scanJar(notClass, markerList()).hits.isEmpty());
    }

    void scanClassName()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString a = tmp.filePath(S("a.jar"));
        QVERIFY(writeJar(a, { { S("dev/neko/nekoclient/Main.class"), cleanClass() } }));
        JarScan scan = scanJar(a, markerList());
        QCOMPARE(scan.hits.size(), 1);
        QCOMPARE(scan.hits.at(0).signatureId, S("t-name"));
        QCOMPARE(scan.hits.at(0).where, S("dev/neko/nekoclient/Main.class"));

        const QString shaded = tmp.filePath(S("shaded.jar"));  // relocated below another package
        QVERIFY(writeJar(shaded, { { S("com/x/dev/neko/nekoclient/Main.class"), cleanClass() } }));
        QCOMPARE(scanJar(shaded, markerList()).hits.size(), 1);

        const QString near = tmp.filePath(S("near.jar"));  // a look-alike prefix is not the path
        QVERIFY(writeJar(near, { { S("dev/neko/nekoclientx/Main.class"), cleanClass() }, { S("dev/neko/other/Main.class"), cleanClass() } }));
        QVERIFY(scanJar(near, markerList()).hits.isEmpty());
    }

    void scanFileHashes()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString jar = tmp.filePath(S("hashed.jar"));
        QVERIFY(writeCleanJar(jar));
        const SignatureList bySha1 = listOf({ sig(S("h1"), S("sha1"), QString::fromLatin1(sha1Of(jar))) });
        JarScan scan = scanJar(jar, bySha1);
        QCOMPARE(scan.hits.size(), 1);
        QCOMPARE(scan.hits.at(0).where, S("(the jar file)"));
        const SignatureList bySha256 = listOf({ sig(S("h2"), S("sha256"), QString::fromLatin1(sha256Of(jar))) });
        QCOMPARE(scanJar(jar, bySha256).hits.size(), 1);
        const SignatureList other = listOf({ sig(S("h3"), S("sha1"), QString(40, QLatin1Char('0'))), sig(S("h4"), S("sha256"), QString(64, QLatin1Char('0'))) });
        QVERIFY(scanJar(jar, other).hits.isEmpty());

        // the bytes in memory give the same answer
        QCOMPARE(scanJarBytes(readFile(jar), bySha1).hits.size(), 1);
    }

    void scanNestedOneLevel()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString inner = tmp.filePath(S("inner.jar"));
        QVERIFY(writeBadJar(inner));
        const QString outer = tmp.filePath(S("outer.jar"));
        QVERIFY(writeJar(outer, { { S("fabric.mod.json"), QByteArray("{}") }, { S("META-INF/jars/inner.jar"), readFile(inner) } }));
        const JarScan scan = scanJar(outer, markerList());
        QVERIFY(scan.readable);
        QCOMPARE(scan.hits.size(), 1);
        QVERIFY2(scan.hits.at(0).where.contains(S("inner.jar")), qPrintable(scan.hits.at(0).where));
        QVERIFY(scan.hits.at(0).where.endsWith(S("a/Bad.class")));

        // the hash of the nested jar is matched too
        const SignatureList byHash = listOf({ sig(S("h"), S("sha1"), QString::fromLatin1(sha1Of(inner))) });
        const JarScan hashed = scanJar(outer, byHash);
        QCOMPARE(hashed.hits.size(), 1);
        QVERIFY(hashed.hits.at(0).where.contains(S("inner.jar")));
    }

    void scanNestedTwoLevelsIsNotOpened()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString deep = tmp.filePath(S("deep.jar"));
        QVERIFY(writeBadJar(deep));
        const QString mid = tmp.filePath(S("mid.jar"));
        QVERIFY(writeJar(mid, { { S("x/Ok.class"), cleanClass() }, { S("META-INF/jars/deep.jar"), readFile(deep) } }));
        const QString outer = tmp.filePath(S("outer.jar"));
        QVERIFY(writeJar(outer, { { S("META-INF/jars/mid.jar"), readFile(mid) } }));
        QVERIFY(scanJar(outer, markerList()).hits.isEmpty());  // deep.jar is two levels down

        // the same signature one level down is found (control: the walk does reach mid.jar)
        const QString midBad = tmp.filePath(S("midbad.jar"));
        QVERIFY(writeJar(midBad, { { S("x/Bad.class"), badClass() } }));
        const QString outer2 = tmp.filePath(S("outer2.jar"));
        QVERIFY(writeJar(outer2, { { S("META-INF/jars/midbad.jar"), readFile(midBad) } }));
        QCOMPARE(scanJar(outer2, markerList()).hits.size(), 1);
        // and the hash of a jar two levels down is not matched either
        const SignatureList byHash = listOf({ sig(S("h"), S("sha1"), QString::fromLatin1(sha1Of(deep))) });
        QVERIFY(scanJar(outer, byHash).hits.isEmpty());
    }

    void scanCleanJar()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString jar = tmp.filePath(S("clean.jar"));
        QVERIFY(writeCleanJar(jar, S("cleanmod")));
        const JarScan scan = scanJar(jar, markerList());
        QVERIFY(scan.readable);
        QVERIFY(scan.hits.isEmpty());
        QVERIFY(scan.error.isEmpty());
    }

    void scanNotAZipAndMissingFile()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString junk = tmp.filePath(S("junk.jar"));
        QVERIFY(writeFile(junk, "this is definitely not a zip file at all"));
        JarScan scan = scanJar(junk, markerList());
        QVERIFY(!scan.readable);
        QVERIFY(scan.hits.isEmpty());
        QVERIFY(!scan.error.isEmpty());

        // with a hash list too (the file is read for the hash first)
        scan = scanJar(junk, listOf({ sig(S("h"), S("sha1"), QString::fromLatin1(sha1Of(junk))) }));
        QVERIFY(!scan.readable);

        scan = scanJar(tmp.filePath(S("missing.jar")), markerList());
        QVERIFY(!scan.readable);
        QVERIFY(scan.hits.isEmpty());

        // a zip cut in half: no crash, no hit
        const QString whole = tmp.filePath(S("whole.jar"));
        QVERIFY(writeBadJar(whole));
        const QByteArray bytes = readFile(whole);
        const QString cut = tmp.filePath(S("cut.jar"));
        QVERIFY(writeFile(cut, bytes.left(bytes.size() / 2)));
        scanJar(cut, markerList());  // must simply return

        QVERIFY(!scanJarBytes("garbage", markerList()).readable);
        QVERIFY(scanJar(whole, SignatureList()).hits.isEmpty());  // no list = nothing to match
    }

    // ------------------------------------------------------------------------------------------------------------ the cache

    void folderFirstThenCachedOnly()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const Layout l = makeLayout(tmp);
        QVERIFY(writeCleanJar(l.mods + S("/a.jar")));
        QVERIFY(writeCleanJar(l.mods + S("/b.jar")));
        QVERIFY(writeBadJar(l.mods + S("/c.jar")));
        const SignatureList list = markerList();
        const QString cachePath = ScanCache::pathFor(l.root);
        QVERIFY(cachePath.endsWith(S(".zero/scan-cache.json")));

        ScanCache cache;
        cache.load(cachePath, list.versionKey);
        FolderScan first = scanModsFolder(l.mods, list, cache);
        QCOMPARE(first.jars, 3);
        QCOMPARE(first.scanned, 3);
        QCOMPARE(first.fromCache, 0);
        QVERIFY(first.complete);
        QCOMPARE(first.infected.size(), 1);
        QCOMPARE(first.infected.at(0).fileName, S("c.jar"));
        QVERIFY(QFileInfo(first.infected.at(0).path).isAbsolute());
        QVERIFY(cache.save());
        QVERIFY(QFileInfo::exists(cachePath));

        ScanCache again;
        again.load(cachePath, list.versionKey);
        QCOMPARE(again.size(), 3);
        int progress = 0;
        FolderScanOptions options;
        options.cachedOnly = true;
        options.progress = [&](int done, int total) {
            QCOMPARE(total, 3);
            progress = done;
        };
        const FolderScan second = scanModsFolder(l.mods, list, again, options);
        QVERIFY(second.complete);
        QCOMPARE(second.scanned, 0);
        QCOMPARE(second.fromCache, second.jars);
        QCOMPARE(second.infected.size(), 1);  // the verdict is cached too
        QCOMPARE(second.infected.at(0).hits.at(0).where, S("a/Bad.class"));
        QCOMPARE(second.infected.at(0).hits.at(0).signatureId, S("t-bytes"));
        QCOMPARE(progress, 3);
    }

    void cachedOnlyOpensNothing()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const Layout l = makeLayout(tmp);
        const QString jar = l.mods + S("/a.jar");
        QVERIFY(writeCleanJar(jar));
        const SignatureList list = markerList();
        ScanCache cache;
        cache.load(ScanCache::pathFor(l.root), list.versionKey);
        scanModsFolder(l.mods, list, cache);
        // swap the content for an infected one of the same size and the same mtime: only a real read could notice
        const qint64 size = QFileInfo(jar).size();
        QByteArray bad = badClass();
        QVERIFY(writeFile(jar, QByteArray(size, 'x')));
        QVERIFY(setMtime(jar));
        FolderScanOptions options;
        options.cachedOnly = true;
        const FolderScan scan = scanModsFolder(l.mods, list, cache, options);
        QVERIFY(scan.complete);
        QCOMPARE(scan.fromCache, 1);
        QCOMPARE(scan.scanned, 0);
        QVERIFY(scan.infected.isEmpty());
        QVERIFY(scan.unreadable.isEmpty());  // the cached verdict (readable) was used, the junk was never opened
    }

    void changedMtimeOrSizeForcesARescan()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const Layout l = makeLayout(tmp);
        const QString a = l.mods + S("/a.jar");
        const QString b = l.mods + S("/b.jar");
        QVERIFY(writeCleanJar(a));
        QVERIFY(writeCleanJar(b));
        const SignatureList list = markerList();
        ScanCache cache;
        cache.load(ScanCache::pathFor(l.root), list.versionKey);
        scanModsFolder(l.mods, list, cache);

        FolderScanOptions cachedOnly;
        cachedOnly.cachedOnly = true;
        QVERIFY(scanModsFolder(l.mods, list, cache, cachedOnly).complete);

        QVERIFY(setMtime(a, kBaseTime.addSecs(60)));  // mtime only
        QVERIFY(!scanModsFolder(l.mods, list, cache, cachedOnly).complete);
        FolderScan scan = scanModsFolder(l.mods, list, cache);
        QCOMPARE(scan.scanned, 1);
        QCOMPARE(scan.fromCache, 1);
        QVERIFY(scanModsFolder(l.mods, list, cache, cachedOnly).complete);

        QVERIFY(writeBadJar(b));  // size (and content) changed, same mtime
        QVERIFY(!scanModsFolder(l.mods, list, cache, cachedOnly).complete);
        scan = scanModsFolder(l.mods, list, cache);
        QCOMPARE(scan.scanned, 1);
        QCOMPARE(scan.infected.size(), 1);
        QCOMPARE(scan.infected.at(0).fileName, S("b.jar"));
    }

    void otherListVersionDropsTheCache()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const Layout l = makeLayout(tmp);
        QVERIFY(writeCleanJar(l.mods + S("/a.jar")));
        const SignatureList list = markerList();
        ScanCache cache;
        cache.load(ScanCache::pathFor(l.root), list.versionKey);
        scanModsFolder(l.mods, list, cache);
        QVERIFY(cache.save());

        ScanCache same;
        same.load(ScanCache::pathFor(l.root), list.versionKey);
        QCOMPARE(same.size(), 1);
        ScanCache other;
        other.load(ScanCache::pathFor(l.root), S("2-deadbeef"));
        QCOMPARE(other.size(), 0);
        FolderScanOptions cachedOnly;
        cachedOnly.cachedOnly = true;
        QVERIFY(!scanModsFolder(l.mods, list, other, cachedOnly).complete);
        // a list that lost a signature does not trust a cached hit of it, even under the same key
        QVERIFY(writeBadJar(l.mods + S("/bad.jar")));
        ScanCache c2;
        c2.load(ScanCache::pathFor(l.root), list.versionKey);
        QCOMPARE(scanModsFolder(l.mods, list, c2).infected.size(), 1);
        const SignatureList fewer = listOf({ sig(S("t-name"), S("class-name"), S("dev.neko.nekoclient.")) });
        FolderScan reduced = scanModsFolder(l.mods, fewer, c2, cachedOnly);
        QVERIFY(!reduced.complete);
        reduced = scanModsFolder(l.mods, fewer, c2);
        QVERIFY(reduced.infected.isEmpty());
        QCOMPARE(reduced.scanned, 1);
    }

    void disabledFilesAndSubfoldersAreIgnored()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const Layout l = makeLayout(tmp);
        QVERIFY(writeCleanJar(l.mods + S("/a.jar")));
        QVERIFY(writeBadJar(l.mods + S("/off.jar.disabled")));
        QVERIFY(writeBadJar(l.mods + S("/1.20.1/inside.jar")));
        QVERIFY(writeFile(l.mods + S("/readme.txt"), "hi"));
        QVERIFY(writeBadJar(l.mods + S("/other.zip")));
        const SignatureList list = markerList();
        ScanCache cache;
        cache.load(ScanCache::pathFor(l.root), list.versionKey);
        const FolderScan scan = scanModsFolder(l.mods, list, cache);
        QCOMPARE(scan.jars, 1);
        QCOMPARE(scan.scanned, 1);
        QVERIFY(scan.infected.isEmpty());
        QCOMPARE(cache.size(), 1);
        // a missing folder is simply empty
        QCOMPARE(scanModsFolder(l.mods + S("/nope"), list, cache).jars, 0);
    }

    void deletedJarsArePruned()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const Layout l = makeLayout(tmp);
        QVERIFY(writeCleanJar(l.mods + S("/a.jar")));
        QVERIFY(writeCleanJar(l.mods + S("/b.jar")));
        const SignatureList list = markerList();
        ScanCache cache;
        cache.load(ScanCache::pathFor(l.root), list.versionKey);
        scanModsFolder(l.mods, list, cache);
        QCOMPARE(cache.size(), 2);
        QVERIFY(QFile::remove(l.mods + S("/b.jar")));
        scanModsFolder(l.mods, list, cache);
        QCOMPARE(cache.size(), 1);
        QVERIFY(cache.save());
        ScanCache reloaded;
        reloaded.load(ScanCache::pathFor(l.root), list.versionKey);
        QCOMPARE(reloaded.size(), 1);
        CachedVerdict v;
        QVERIFY(!reloaded.lookup(S("b.jar"), 1, 1, &v));
    }

    void unreadableCacheFileIsIgnored()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const Layout l = makeLayout(tmp);
        QVERIFY(writeCleanJar(l.mods + S("/a.jar")));
        const SignatureList list = markerList();
        const QString path = ScanCache::pathFor(l.root);
        QVERIFY(writeFile(path, "{ this is not json"));
        ScanCache cache;
        cache.load(path, list.versionKey);
        QCOMPARE(cache.size(), 0);
        const FolderScan scan = scanModsFolder(l.mods, list, cache);
        QCOMPARE(scan.scanned, 1);
        QVERIFY(cache.save());  // it is rewritten
        QVERIFY(QJsonDocument::fromJson(readFile(path)).isObject());
        // wrong shapes inside valid JSON do not crash either
        QVERIFY(writeFile(path, R"({"listKey":"x","jars":{"a.jar":"oops"},"modsCheck":5})"));
        ScanCache shape;
        shape.load(path, S("x"));
        QCOMPARE(shape.size(), 0);
        QVERIFY(writeFile(path, "[]"));
        shape.load(path, S("x"));
        QCOMPARE(shape.size(), 0);
    }

    void unreadableJarVerdictIsCached()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const Layout l = makeLayout(tmp);
        QVERIFY(writeFile(l.mods + S("/junk.jar"), "not a zip"));
        QVERIFY(setMtime(l.mods + S("/junk.jar")));
        const SignatureList list = markerList();
        ScanCache cache;
        cache.load(ScanCache::pathFor(l.root), list.versionKey);
        FolderScan scan = scanModsFolder(l.mods, list, cache);
        QCOMPARE(scan.unreadable, QStringList{ S("junk.jar") });
        QVERIFY(scan.infected.isEmpty());  // unreadable is never a finding by itself
        QVERIFY(cache.save());
        ScanCache again;
        again.load(ScanCache::pathFor(l.root), list.versionKey);
        FolderScanOptions cachedOnly;
        cachedOnly.cachedOnly = true;
        scan = scanModsFolder(l.mods, list, again, cachedOnly);
        QVERIFY(scan.complete);
        QCOMPARE(scan.unreadable, QStringList{ S("junk.jar") });
    }

    void modsCheckVerdictInTheCache()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path = tmp.filePath(S("c/scan-cache.json"));
        ScanCache cache;
        cache.load(path, S("k"));
        QVERIFY(!cache.modsCheckClean(S("fp")));
        cache.setModsCheck(S("fp"), true);
        QVERIFY(cache.modsCheckClean(S("fp")));
        QVERIFY(!cache.modsCheckClean(S("other")));
        QVERIFY(!cache.modsCheckClean(QString()));
        QVERIFY(cache.save());
        ScanCache again;
        again.load(path, S("k"));
        QVERIFY(again.modsCheckClean(S("fp")));
        again.setModsCheck(S("fp"), false);
        QVERIFY(!again.modsCheckClean(S("fp")));
        ScanCache otherKey;
        otherKey.load(path, S("k2"));
        QVERIFY(!otherKey.modsCheckClean(S("fp")));
    }

    // ------------------------------------------------------------------------------------------------------------ system traces

    void systemTracesEachOne()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        SystemEnv env;
        env.localAppData = tmp.filePath(S("Local"));
        env.appData = tmp.filePath(S("Roaming"));
        env.userProfile = tmp.filePath(S("User"));
        QDir().mkpath(env.localAppData);
        QDir().mkpath(env.appData);
        QDir().mkpath(env.userProfile);
        QVERIFY(findSystemTraces(env).isEmpty());  // clean

        // the real Edge folders (no space) are not a trace
        QDir().mkpath(env.localAppData + S("/MicrosoftEdge"));
        QDir().mkpath(env.localAppData + S("/Microsoft/Edge"));
        QVERIFY(findSystemTraces(env).isEmpty());

        QDir().mkpath(env.localAppData + S("/Microsoft Edge"));
        QList<SystemTrace> found = findSystemTraces(env);
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.at(0).id, S("microsoft-edge-folder"));
        QVERIFY(!found.at(0).description.isEmpty());
        QVERIFY(found.at(0).infoUrl.startsWith(S("https://")));
        QDir(env.localAppData + S("/Microsoft Edge")).removeRecursively();
        QVERIFY(findSystemTraces(env).isEmpty());

        QVERIFY(writeFile(env.userProfile + S("/AppData/LocalLow/Microsoft/Internet Explorer/DOMStore/microsoft-vm-core"), "x"));
        found = findSystemTraces(env);
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.at(0).id, S("microsoft-vm-core"));
        QVERIFY(QFile::remove(env.userProfile + S("/AppData/LocalLow/Microsoft/Internet Explorer/DOMStore/microsoft-vm-core")));
        QVERIFY(findSystemTraces(env).isEmpty());

        QVERIFY(writeFile(env.appData + S("/Microsoft/Windows/Start Menu/Programs/Startup/libWebGL64.jar"), "x"));
        found = findSystemTraces(env);
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.at(0).id, S("startup-libwebgl64"));
        QVERIFY(QFile::remove(env.appData + S("/Microsoft/Windows/Start Menu/Programs/Startup/libWebGL64.jar")));

        env.runKeyValues = []() { return QStringList{ S("\"C:/Program Files/App/app.exe\" --x"), S("C:/Users/u/AppData/Roaming/libWebGL64.jar") }; };
        found = findSystemTraces(env);
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.at(0).id, S("run-key-libwebgl64"));
        env.runKeyValues = []() { return QStringList{ S("\"C:/Program Files/App/app.exe\" --x") }; };
        QVERIFY(findSystemTraces(env).isEmpty());
        env.runKeyValues = {};
        QVERIFY(findSystemTraces(env).isEmpty());

        // looking never writes: with every trace present, the folder is the same after
        QDir().mkpath(env.localAppData + S("/Microsoft Edge"));
        QVERIFY(writeFile(env.userProfile + S("/AppData/LocalLow/Microsoft/Internet Explorer/DOMStore/microsoft-vm-core"), "x"));
        QVERIFY(writeFile(env.appData + S("/Microsoft/Windows/Start Menu/Programs/Startup/libWebGL64.jar"), "x"));
        const QStringList before = snapshot(tmp.path());
        QCOMPARE(findSystemTraces(env).size(), 3);
        QCOMPARE(snapshot(tmp.path()), before);
    }

    void systemTracesAllAtOnceInFixedOrder()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        SystemEnv env;
        env.localAppData = tmp.filePath(S("Local"));
        env.appData = tmp.filePath(S("Roaming"));
        env.userProfile = tmp.filePath(S("User"));
        QDir().mkpath(env.localAppData + S("/Microsoft Edge"));
        QVERIFY(writeFile(env.userProfile + S("/AppData/LocalLow/Microsoft/Internet Explorer/DOMStore/microsoft-vm-core"), "x"));
        QVERIFY(writeFile(env.appData + S("/Microsoft/Windows/Start Menu/Programs/Startup/libWebGL64.jar"), "x"));
        env.runKeyValues = []() { return QStringList{ S("x libwebgl64 y") }; };  // case-insensitive
        const QList<SystemTrace> found = findSystemTraces(env);
        QStringList ids;
        for (const SystemTrace& t : found) {
            ids.append(t.id);
        }
        QCOMPARE(ids, (QStringList{ S("microsoft-edge-folder"), S("microsoft-vm-core"), S("startup-libwebgl64"), S("run-key-libwebgl64") }));
        // empty folders in the env: nothing is looked at
        QVERIFY(findSystemTraces(SystemEnv()).isEmpty());
    }

    // ------------------------------------------------------------------------------------------------------------ runChecks

    void malwareIsABlockWithFixes()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const Layout l = makeLayout(tmp);
        QVERIFY(writeCleanJar(l.mods + S("/good.jar")));
        QVERIFY(writeBadJar(l.mods + S("/evil.jar")));
        const SignatureList list = markerList();
        const Env env = makeEnv(l, &list);

        const CheckResult result = runChecks(env);
        QVERIFY(result.complete);
        QCOMPARE(result.items.size(), 1);
        const Item& item = result.items.at(0);
        QCOMPARE(item.id, S("malware:evil.jar"));
        QCOMPARE(item.severity, Severity::Block);
        QCOMPARE(item.category, Category::Malware);
        QVERIFY(!item.title.isEmpty());
        QVERIFY(item.title.contains(S("evil.jar")));
        QVERIFY(item.detail.contains(S("Test t-bytes")));
        QVERIFY(!item.fixed);
        QCOMPARE(fixKinds(item), (QList<Fix::Kind>{ Fix::Kind::RecycleFile, Fix::Kind::ShowFile, Fix::Kind::OpenUrl }));
        QVERIFY(item.fixes.at(0).danger);
        QCOMPARE(QFileInfo(item.fixes.at(0).targets.value(0)).fileName(), S("evil.jar"));
        QCOMPARE(item.fixes.at(2).targets.value(0), S("https://example.invalid/t-bytes"));
        QCOMPARE(result.jarsScanned, 2);
        QCOMPARE(result.jarsFromCache, 0);
        QVERIFY(QFileInfo::exists(ScanCache::pathFor(l.root)));

        const CheckResult second = runChecks(env);  // answered from the cache
        QCOMPARE(second.jarsScanned, 0);
        QCOMPARE(second.jarsFromCache, 2);
        QCOMPARE(second.items.size(), 1);
        QVERIFY(QFileInfo::exists(l.mods + S("/evil.jar")));  // looking never touches a jar
    }

    void globalSwitchOffGivesNothing()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const Layout l = makeLayout(tmp);
        QVERIFY(writeBadJar(l.mods + S("/evil.jar")));
        const SignatureList list = markerList();
        Env env = makeEnv(l, &list);
        env.enabled = false;
        env.packOffer = []() { return PackOffer{ true, { S("x.zip") } }; };
        QVERIFY(runChecks(env).items.isEmpty());
        QVERIFY(runChecks(env).complete);
        QVERIFY(runCategory(env, Category::Malware).isEmpty());
        QVERIFY(!QFileInfo::exists(ScanCache::pathFor(l.root)));  // nothing was even scanned
    }

    void instanceOptOutKeepsMalware()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const Layout l = makeLayout(tmp);
        QVERIFY(writeBadJar(l.mods + S("/evil.jar")));
        QVERIFY(writeCleanJar(l.mods + S("/one.jar"), S("dupe")));
        QVERIFY(writeCleanJar(l.mods + S("/two.jar"), S("dupe")));
        const SignatureList list = markerList();
        Env env = makeEnv(l, &list, true);
        env.packOffer = []() { return PackOffer{ true, { S("x.zip") } }; };

        const CheckResult all = runChecks(env);
        QStringList categories;
        for (const Item& item : all.items) {
            categories.append(QString::number(int(item.category)));
        }
        QVERIFY(findItem(all.items, S("malware:evil.jar")));
        QVERIFY(findItem(all.items, S("packs:rollback")));
        bool anyMods = false;
        for (const Item& item : all.items) {
            anyMods = anyMods || item.category == Category::Mods;
        }
        QVERIFY(anyMods);

        env.instanceChecksEnabled = false;
        const CheckResult optedOut = runChecks(env);
        QCOMPARE(optedOut.items.size(), 1);
        QCOMPARE(optedOut.items.at(0).id, S("malware:evil.jar"));
        QVERIFY(runCategory(env, Category::Mods).isEmpty());
        QVERIFY(runCategory(env, Category::Packs).isEmpty());
        QCOMPARE(runCategory(env, Category::Malware).size(), 1);
    }

    void packOfferMakesAWarnRow()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const Layout l = makeLayout(tmp);
        const SignatureList list = markerList();
        Env env = makeEnv(l, &list);
        QVERIFY(runChecks(env).items.isEmpty());  // no hook: no pack check

        env.packOffer = []() { return PackOffer{}; };  // not valid: nothing to offer
        QVERIFY(runChecks(env).items.isEmpty());

        env.packOffer = []() { return PackOffer{ true, { S("Faithful.zip") } }; };
        const CheckResult result = runChecks(env);
        QCOMPARE(result.items.size(), 1);
        const Item& item = result.items.at(0);
        QCOMPARE(item.id, S("packs:rollback"));
        QCOMPARE(item.severity, Severity::Warn);
        QCOMPARE(item.category, Category::Packs);
        QVERIFY(!item.title.isEmpty());
        QVERIFY2(item.detail.contains(S("Faithful")), qPrintable(item.detail));
        QCOMPARE(fixKinds(item), (QList<Fix::Kind>{ Fix::Kind::RestorePacks, Fix::Kind::KeepPacksOff }));

        env.packOffer = []() { return PackOffer{ true, {} }; };  // no suspects: restore everything
        QCOMPARE(runChecks(env).items.size(), 1);
    }

    void duplicateModsGiveAWarnWithDisableFix()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const Layout l = makeLayout(tmp);
        QVERIFY(writeCleanJar(l.mods + S("/one.jar"), S("dupe")));
        QVERIFY(writeCleanJar(l.mods + S("/two.jar"), S("dupe")));
        QVERIFY(writeCleanJar(l.mods + S("/solo.jar"), S("solo")));
        const SignatureList list = markerList();
        const Env env = makeEnv(l, &list, true);

        const CheckResult result = runChecks(env);
        QVERIFY(result.complete);
        QCOMPARE(result.items.size(), 1);
        const Item& item = result.items.at(0);
        QCOMPARE(item.category, Category::Mods);
        QCOMPARE(item.severity, Severity::Warn);
        QVERIFY(item.id.startsWith(S("mods:duplicate-mod:")));
        QVERIFY(!item.title.isEmpty());
        QVERIFY2(item.fixes.at(0).kind == Fix::Kind::DisableJars || item.fixes.at(0).kind == Fix::Kind::OpenMods, "a fix is offered");
        if (item.fixes.at(0).kind == Fix::Kind::DisableJars) {
            QVERIFY(!item.fixes.at(0).actions.isEmpty());
            QCOMPARE(item.fixes.size(), 2);
            const Fix& recycle = item.fixes.at(1);
            QCOMPARE(recycle.kind, Fix::Kind::RecycleJars);
            QVERIFY(recycle.danger);
            QVERIFY(!item.fixes.at(0).danger);
            QCOMPARE(recycle.targets.size(), item.fixes.at(0).actions.size());
            QCOMPARE(recycle.targets.at(0), QDir(l.mods).filePath(item.fixes.at(0).actions.at(0).fileName));
            QVERIFY(recycle.label.contains(S("Recycle Bin")));
        } else {
            QCOMPARE(item.fixes.size(), 1);
        }
        // not asked again while the folder and the instance stay the same only when it was clean: a finding is always shown
        QCOMPARE(runChecks(env).items.size(), 1);
    }

    void cleanModsAreRememberedAndCachedOnlyAnswers()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const Layout l = makeLayout(tmp);
        QVERIFY(writeCleanJar(l.mods + S("/a.jar"), S("alpha")));
        QVERIFY(writeCleanJar(l.mods + S("/b.jar"), S("beta")));
        const SignatureList list = markerList();
        const Env env = makeEnv(l, &list, true);

        RunOptions cachedOnly;
        cachedOnly.cachedOnly = true;
        CheckResult cold = runChecks(env, cachedOnly);
        QVERIFY(!cold.complete);  // nothing cached yet
        QVERIFY(cold.items.isEmpty());
        QVERIFY(!QFileInfo::exists(ScanCache::pathFor(l.root)));  // and the cached-only run wrote nothing

        const CheckResult full = runChecks(env);
        QVERIFY(full.complete);
        QVERIFY(full.items.isEmpty());
        const QByteArray cacheBytes = readFile(ScanCache::pathFor(l.root));

        const CheckResult warm = runChecks(env, cachedOnly);
        QVERIFY(warm.complete);
        QVERIFY(warm.items.isEmpty());
        QCOMPARE(warm.jarsScanned, 0);
        QCOMPARE(warm.jarsFromCache, 2);
        QCOMPARE(readFile(ScanCache::pathFor(l.root)), cacheBytes);

        // a new jar: not complete from the cache any more
        QVERIFY(writeCleanJar(l.mods + S("/c.jar"), S("gamma")));
        QVERIFY(!runChecks(env, cachedOnly).complete);
        // a changed instance (another Minecraft version) re-runs the mods check
        QVERIFY(runChecks(env).complete);
        Env other = env;
        other.context = []() {
            Mods::InstanceContext c;
            c.loader = Mods::InstanceLoader::Fabric;
            c.mcVersion = S("1.21");
            c.loaderVersion = S("0.15.11");
            return c;
        };
        QVERIFY(!runChecks(other, cachedOnly).complete);
    }

    void fingerprintFollowsFilesAndContext()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const Layout l = makeLayout(tmp);
        QVERIFY(writeCleanJar(l.mods + S("/a.jar"), S("alpha")));
        Mods::InstanceContext ctx;
        ctx.loader = Mods::InstanceLoader::Fabric;
        ctx.mcVersion = S("1.20.1");
        ctx.loaderVersion = S("0.15.11");
        const QString base = modsFingerprint(l.mods, ctx);
        QVERIFY(!base.isEmpty());
        QCOMPARE(modsFingerprint(l.mods, ctx), base);
        Mods::InstanceContext other = ctx;
        other.mcVersion = S("1.21");
        QVERIFY(modsFingerprint(l.mods, other) != base);
        other = ctx;
        other.loader = Mods::InstanceLoader::Quilt;
        QVERIFY(modsFingerprint(l.mods, other) != base);
        QVERIFY(setMtime(l.mods + S("/a.jar"), kBaseTime.addSecs(5)));
        const QString touched = modsFingerprint(l.mods, ctx);
        QVERIFY(touched != base);
        QVERIFY(writeFile(l.mods + S("/b.jar.disabled"), "x"));
        QVERIFY(modsFingerprint(l.mods, ctx) != touched);  // a disabled file counts: enabling it changes the mods
        QVERIFY(!modsFingerprint(l.mods + S("/nope"), ctx).isEmpty());
    }

    void orderingBlockBeforeWarn()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const Layout l = makeLayout(tmp);
        QVERIFY(writeBadJar(l.mods + S("/evil.jar")));
        QVERIFY(writeCleanJar(l.mods + S("/one.jar"), S("dupe")));
        QVERIFY(writeCleanJar(l.mods + S("/two.jar"), S("dupe")));
        QTemporaryDir sys;
        QVERIFY(sys.isValid());
        const SignatureList list = markerList();
        Env env = makeEnv(l, &list, true);
        env.system.localAppData = sys.path();
        QDir().mkpath(sys.path() + S("/Microsoft Edge"));
        env.packOffer = []() { return PackOffer{ true, { S("x.zip") } }; };

        const CheckResult result = runChecks(env);
        QList<QPair<int, int>> order;  // (severity, category)
        for (const Item& item : result.items) {
            order.append({ int(item.severity), int(item.category) });
        }
        QCOMPARE(order, (QList<QPair<int, int>>{ { int(Severity::Block), int(Category::Malware) },
                                                  { int(Severity::Block), int(Category::System) },
                                                  { int(Severity::Warn), int(Category::Mods) },
                                                  { int(Severity::Warn), int(Category::Packs) } }));
        QCOMPARE(result.items.at(1).id, S("system:microsoft-edge-folder"));
        QCOMPARE(fixKinds(result.items.at(1)), (QList<Fix::Kind>{ Fix::Kind::ShowFile, Fix::Kind::OpenUrl, Fix::Kind::Recheck }));
    }

    void noModsFolderOrNoLoaderIsQuiet()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        Layout l;
        l.root = tmp.filePath(S("bare"));
        l.game = l.root + S("/minecraft");
        l.mods = l.game + S("/mods");
        QDir().mkpath(l.game);  // no mods folder
        const SignatureList list = markerList();
        QVERIFY(runChecks(makeEnv(l, &list, true)).items.isEmpty());
        QVERIFY(runChecks(makeEnv(l, &list, true)).complete);
        // a broken list is "no list", not an alarm
        const SignatureList broken = parseSignatures("nope");
        QVERIFY(!broken.error.isEmpty());
        const Layout l2 = makeLayout(tmp, S("two"));
        QVERIFY(writeBadJar(l2.mods + S("/evil.jar")));
        QVERIFY(runChecks(makeEnv(l2, &broken)).items.isEmpty());
    }

    // ------------------------------------------------------------------------------------------------------------ Session

    void sessionSummaryAndPlayLabel()
    {
        Env env;
        Item block;
        block.id = S("malware:x.jar");
        block.severity = Severity::Block;
        block.category = Category::Malware;
        Item warn;
        warn.id = S("packs:rollback");
        warn.severity = Severity::Warn;
        warn.category = Category::Packs;
        Item warn2 = warn;
        warn2.id = S("mods:a");
        warn2.category = Category::Mods;

        Session none(env, {}, {});
        QCOMPARE(none.summary(), S("Nothing left to fix."));
        QCOMPARE(none.playLabel(), S("Play"));
        QVERIFY(none.playIsPrimary());
        QVERIFY(!none.hasOpenBlock());

        Session one(env, {}, { warn });
        QCOMPARE(one.summary(), S("We found 1 thing that may stop the game from working."));
        QCOMPARE(one.playLabel(), S("Play anyway"));
        QVERIFY(one.playIsPrimary());  // only warnings: Play stays the primary button

        Session three(env, {}, { block, warn, warn2 });
        QCOMPARE(three.summary(), S("We found 3 things that may stop the game from working."));
        QCOMPARE(three.openCount(), 3);
        QCOMPARE(three.openBlockCount(), 1);
        QVERIFY(three.hasOpenBlock());
        QVERIFY(!three.playIsPrimary());
        QCOMPARE(three.playLabel(), S("Play anyway"));

        Item fixed = block;
        fixed.fixed = true;
        Session allFixed(env, {}, { fixed });
        QCOMPARE(allFixed.openCount(), 0);
        QCOMPARE(allFixed.summary(), S("Nothing left to fix."));
        QCOMPARE(allFixed.playLabel(), S("Play"));
        QVERIFY(allFixed.playIsPrimary());
    }

    void sessionRecycleFixThenRerunIsClean()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const Layout l = makeLayout(tmp);
        QVERIFY(writeCleanJar(l.mods + S("/good.jar")));
        QVERIFY(writeBadJar(l.mods + S("/evil.jar")));
        const SignatureList list = markerList();
        const Env env = makeEnv(l, &list);

        QStringList recycled;
        Hooks hooks;
        hooks.recycle = [&](const QString& path) {
            recycled.append(path);
            return QFile::remove(path);  // stands in for the Recycle Bin
        };
        Session session(env, hooks, runChecks(env).items);
        QCOMPARE(session.items().size(), 1);
        QVERIFY(session.hasOpenBlock());

        const auto result = session.applyFix(S("malware:evil.jar"), 0);
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(!result.leavesCheck);
        QCOMPARE(recycled.size(), 1);
        QCOMPARE(QFileInfo(recycled.at(0)).fileName(), S("evil.jar"));
        QVERIFY(!QFileInfo::exists(l.mods + S("/evil.jar")));
        QVERIFY(QFileInfo::exists(l.mods + S("/good.jar")));
        QVERIFY(session.items().at(0).fixed);
        QCOMPARE(session.items().at(0).fixedText, S("Fixed"));
        QCOMPARE(session.openCount(), 0);
        QCOMPARE(session.summary(), S("Nothing left to fix."));
        QCOMPARE(session.playLabel(), S("Play"));
        QVERIFY(session.playIsPrimary());
        QVERIFY(runChecks(env).items.isEmpty());  // the rerun no longer shows it
    }

    void sessionRecycleDuplicateJar()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const Layout l = makeLayout(tmp);
        QVERIFY(writeCleanJar(l.mods + S("/one.jar"), S("dupe")));
        QVERIFY(writeCleanJar(l.mods + S("/two.jar"), S("dupe"), S("2.0.0")));
        const SignatureList list = markerList();
        const Env env = makeEnv(l, &list, true);
        QStringList recycled;
        Hooks hooks;
        hooks.recycle = [&](const QString& path) {
            recycled.append(path);
            return QFile::remove(path);
        };
        Session session(env, hooks, runChecks(env).items);
        const Item item = session.items().at(0);
        if (item.fixes.at(0).kind != Fix::Kind::DisableJars) {
            QSKIP("Mod Intelligence did not name a culprit for this synthetic pair");
        }
        const auto r = session.applyFix(item.id, 1);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(recycled.size(), 1);
        QCOMPARE(QDir(l.mods).entryList({ S("*.jar") }).size(), 1);
        QCOMPARE(QDir(l.mods).entryList({ S("*.jar.disabled") }).size(), 0);
        QVERIFY(session.items().at(0).fixed);
    }

    void sessionRecycleJarsFailureNamesTheFile()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const Layout l = makeLayout(tmp);
        QVERIFY(writeCleanJar(l.mods + S("/one.jar"), S("dupe")));
        QVERIFY(writeCleanJar(l.mods + S("/two.jar"), S("dupe"), S("2.0.0")));
        const SignatureList list = markerList();
        const Env env = makeEnv(l, &list, true);
        Hooks hooks;
        hooks.recycle = [](const QString&) { return false; };
        Session session(env, hooks, runChecks(env).items);
        const Item item = session.items().at(0);
        if (item.fixes.at(0).kind != Fix::Kind::DisableJars) {
            QSKIP("Mod Intelligence did not name a culprit for this synthetic pair");
        }
        const auto r = session.applyFix(item.id, 1);
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QFileInfo(item.fixes.at(1).targets.at(0)).fileName()));
        QVERIFY(!session.items().at(0).fixed);
        QCOMPARE(QDir(l.mods).entryList({ S("*.jar") }).size(), 2);
    }

    void sessionFailingHookSetsError()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const Layout l = makeLayout(tmp);
        QVERIFY(writeBadJar(l.mods + S("/evil.jar")));
        const SignatureList list = markerList();
        const Env env = makeEnv(l, &list);
        Hooks hooks;
        hooks.recycle = [](const QString&) { return false; };
        Session session(env, hooks, runChecks(env).items);
        const auto result = session.applyFix(S("malware:evil.jar"), 0);
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains(S("evil.jar")));
        QCOMPARE(session.items().at(0).error, result.error);
        QVERIFY(!session.items().at(0).fixed);
        QVERIFY(QFileInfo::exists(l.mods + S("/evil.jar")));
        QCOMPARE(session.openCount(), 1);
    }

    void sessionHooksAndUnknownFix()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const Layout l = makeLayout(tmp);
        QVERIFY(writeBadJar(l.mods + S("/evil.jar")));
        const SignatureList list = markerList();
        const Env env = makeEnv(l, &list);
        QStringList shown;
        QStringList urls;
        Hooks hooks;
        hooks.showFile = [&](const QString& p) { shown.append(p); };
        hooks.openUrl = [&](const QString& u) { urls.append(u); };
        hooks.recycle = [](const QString&) { return false; };
        Session session(env, hooks, runChecks(env).items);
        QVERIFY(session.applyFix(S("malware:evil.jar"), 1).ok);
        QCOMPARE(shown.size(), 1);
        QVERIFY(shown.at(0).endsWith(S("evil.jar")));
        QVERIFY(session.applyFix(S("malware:evil.jar"), 2).ok);
        QCOMPARE(urls, QStringList{ S("https://example.invalid/t-bytes") });
        QVERIFY(!session.items().at(0).fixed);  // neither fixes anything
        QVERIFY(!session.applyFix(S("malware:evil.jar"), 9).ok);
        QVERIFY(!session.applyFix(S("malware:evil.jar"), -1).ok);
        QVERIFY(!session.applyFix(S("nope"), 0).ok);
        QVERIFY(!session.applyFix(S("nope"), 0).error.isEmpty());
        // a file that is already gone: the fix succeeds without calling the Recycle Bin (it is gone, the rerun says so)
        QVERIFY(QFile::remove(l.mods + S("/evil.jar")));
        QVERIFY(session.applyFix(S("malware:evil.jar"), 0).ok);
        QVERIFY(session.items().at(0).fixed);
    }

    void sessionPlayWithOpenBlock()
    {
        Env env;
        Item block;
        block.id = S("malware:x.jar");
        block.severity = Severity::Block;
        block.category = Category::Malware;
        int skipCalls = 0;
        bool skipValue = false;
        Hooks hooks;
        hooks.setSkipInstance = [&](bool skip) {
            ++skipCalls;
            skipValue = skip;
        };

        {  // "no" to the second confirmation: Cancel, nothing is stored
            Session s(env, hooks, { block });
            s.setOptOut(true);
            int asked = 0;
            QCOMPARE(s.play([&]() { ++asked; return false; }), Decision::Cancel);
            QCOMPARE(asked, 1);
            QCOMPARE(skipCalls, 0);
            QCOMPARE(s.play({}), Decision::Cancel);  // no confirm callback is a "no"
            QCOMPARE(skipCalls, 0);
            QCOMPARE(s.cancel(), Decision::Cancel);
        }
        {  // "yes": Play and the opt-out is stored
            Session s(env, hooks, { block });
            s.setOptOut(true);
            QVERIFY(s.optOut());
            QCOMPARE(s.play([]() { return true; }), Decision::Play);
            QCOMPARE(skipCalls, 1);
            QVERIFY(skipValue);
        }
        {  // "yes" without the opt-out: nothing stored
            skipCalls = 0;
            Session s(env, hooks, { block });
            QCOMPARE(s.play([]() { return true; }), Decision::Play);
            QCOMPARE(skipCalls, 0);
        }
        {  // nothing open: the confirmation is not asked
            Item fixed = block;
            fixed.fixed = true;
            Session s(env, hooks, { fixed });
            int asked = 0;
            QCOMPARE(s.play([&]() { ++asked; return false; }), Decision::Play);
            QCOMPARE(asked, 0);
        }
    }

    void sessionPlayDismissesOpenPacks()
    {
        int dismissed = 0;
        Env env;
        env.dismissPacks = [&]() { ++dismissed; };
        Item packs;
        packs.id = S("packs:rollback");
        packs.severity = Severity::Warn;
        packs.category = Category::Packs;
        Hooks hooks;
        {
            Session s(env, hooks, { packs });
            QCOMPARE(s.play({}), Decision::Play);  // only a warning: no confirmation
            QCOMPARE(dismissed, 1);
        }
        {  // cancelled behind a block: not dismissed
            dismissed = 0;
            Item block;
            block.id = S("malware:x.jar");
            block.severity = Severity::Block;
            block.category = Category::Malware;
            Session s(env, hooks, { block, packs });
            QCOMPARE(s.play([]() { return false; }), Decision::Cancel);
            QCOMPARE(dismissed, 0);
            QCOMPARE(s.play([]() { return true; }), Decision::Play);
            QCOMPARE(dismissed, 1);
        }
        {  // the packs row was already handled: not dismissed again
            dismissed = 0;
            Item done = packs;
            done.fixed = true;
            Session s(env, hooks, { done });
            QCOMPARE(s.play({}), Decision::Play);
            QCOMPARE(dismissed, 0);
        }
    }

    void sessionPackFixes()
    {
        int dismissed = 0;
        QString restoreAnswer;
        Env env;
        env.dismissPacks = [&]() { ++dismissed; };
        env.restorePacks = [&]() { return restoreAnswer; };
        Item packs;
        packs.id = S("packs:rollback");
        packs.severity = Severity::Warn;
        packs.category = Category::Packs;
        Fix restore;
        restore.kind = Fix::Kind::RestorePacks;
        Fix keep;
        keep.kind = Fix::Kind::KeepPacksOff;
        packs.fixes = { restore, keep };

        {  // restore fails: error, not fixed
            restoreAnswer = S("The packs are in use.");
            Session s(env, {}, { packs }, [](Category) { return QList<Item>{}; });
            const auto r = s.applyFix(S("packs:rollback"), 0);
            QVERIFY(!r.ok);
            QCOMPARE(r.error, S("The packs are in use."));
            QCOMPARE(s.items().at(0).error, r.error);
            QVERIFY(!s.items().at(0).fixed);
        }
        {  // restore works: the rerun does not offer it again
            restoreAnswer.clear();
            Session s(env, {}, { packs }, [](Category) { return QList<Item>{}; });
            QVERIFY(s.applyFix(S("packs:rollback"), 0).ok);
            QVERIFY(s.items().at(0).fixed);
            QCOMPARE(dismissed, 0);
        }
        {  // keep off: dismissed once, row marked "Left off"
            Session s(env, {}, { packs });
            QVERIFY(s.applyFix(S("packs:rollback"), 1).ok);
            QCOMPARE(dismissed, 1);
            QVERIFY(s.items().at(0).fixed);
            QCOMPARE(s.items().at(0).fixedText, S("Left off"));
            QCOMPARE(s.play({}), Decision::Play);
            QCOMPARE(dismissed, 1);  // not asked again at Play
        }
        {  // no restore hook at all
            Env bare;
            Session s(bare, {}, { packs });
            QVERIFY(!s.applyFix(S("packs:rollback"), 0).ok);
        }
    }

    void sessionOpenModsLeavesTheCheck()
    {
        Env env;
        Item mods;
        mods.id = S("mods:x");
        mods.category = Category::Mods;
        Fix open;
        open.kind = Fix::Kind::OpenMods;
        mods.fixes = { open };
        Session s(env, {}, { mods });
        const auto r = s.applyFix(S("mods:x"), 0);
        QVERIFY(r.ok);
        QVERIFY(r.leavesCheck);
        QVERIFY(!s.items().at(0).fixed);
    }

    void sessionRecheckSystemRow()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const Layout l = makeLayout(tmp);
        QTemporaryDir sys;
        QVERIFY(sys.isValid());
        const SignatureList list = markerList();
        Env env = makeEnv(l, &list);
        env.system.localAppData = sys.path();
        QDir().mkpath(sys.path() + S("/Microsoft Edge"));
        Session s(env, {}, runChecks(env).items);
        QCOMPARE(s.items().size(), 1);
        QVERIFY(s.applyFix(s.items().at(0).id, 2).ok);  // "Check again": still there
        QVERIFY(!s.items().at(0).fixed);
        QVERIFY(QDir(sys.path() + S("/Microsoft Edge")).removeRecursively());
        QVERIFY(s.applyFix(s.items().at(0).id, 2).ok);
        QVERIFY(s.items().at(0).fixed);
    }

    void sessionDisableDuplicateJar()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const Layout l = makeLayout(tmp);
        QVERIFY(writeCleanJar(l.mods + S("/one.jar"), S("dupe")));
        QVERIFY(writeCleanJar(l.mods + S("/two.jar"), S("dupe"), S("2.0.0")));
        const SignatureList list = markerList();
        const Env env = makeEnv(l, &list, true);
        Session session(env, {}, runChecks(env).items);
        QCOMPARE(session.items().size(), 1);
        const Item item = session.items().at(0);
        if (item.fixes.at(0).kind != Fix::Kind::DisableJars) {
            QSKIP("Mod Intelligence did not name a culprit for this synthetic pair");
        }
        const auto r = session.applyFix(item.id, 0);
        QVERIFY2(r.ok, qPrintable(r.error));
        // exactly one jar was renamed, nothing was deleted
        const int disabled = QDir(l.mods).entryList({ S("*.jar.disabled") }).size();
        const int enabled = QDir(l.mods).entryList({ S("*.jar") }).size();
        QCOMPARE(disabled, 1);
        QCOMPARE(enabled, 1);
        QVERIFY(session.items().at(0).fixed);
        QVERIFY(runChecks(env).items.isEmpty());
    }

    // ------------------------------------------------------------------------------------------------------------ performance

    void cachedOnlyOver200JarsIsFast()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const Layout l = makeLayout(tmp);
        for (int i = 0; i < 200; ++i) {
            QVERIFY(writeCleanJar(l.mods + QStringLiteral("/mod-%1.jar").arg(i, 3, 10, QLatin1Char('0')), QStringLiteral("mod%1").arg(i)));
        }
        const SignatureList list = markerList();
        ScanCache cache;
        cache.load(ScanCache::pathFor(l.root), list.versionKey);
        QCOMPARE(scanModsFolder(l.mods, list, cache).scanned, 200);
        QVERIFY(cache.save());

        qint64 best = std::numeric_limits<qint64>::max();
        for (int round = 0; round < 5; ++round) {
            ScanCache warm;
            warm.load(ScanCache::pathFor(l.root), list.versionKey);
            FolderScanOptions options;
            options.cachedOnly = true;
            QElapsedTimer timer;
            timer.start();
            const FolderScan scan = scanModsFolder(l.mods, list, warm, options);
            best = std::min(best, timer.nsecsElapsed());
            QVERIFY(scan.complete);
            QCOMPARE(scan.jars, 200);
            QCOMPARE(scan.fromCache, scan.jars);
            QCOMPARE(scan.scanned, 0);
        }
        qInfo("cachedOnly scanModsFolder over 200 cached jars: best of 5 = %.2f ms", double(best) / 1e6);
        QVERIFY2(best < 50LL * 1000 * 1000, "a cached-only look at 200 jars must stay well under 50 ms");

        // the whole runChecks, mods check included (loader none: no metadata is parsed)
        const Env env = makeEnv(l, &list, false);
        RunOptions options;
        options.cachedOnly = true;
        qint64 bestRun = std::numeric_limits<qint64>::max();
        for (int round = 0; round < 5; ++round) {
            QElapsedTimer timer;
            timer.start();
            const CheckResult result = runChecks(env, options);
            bestRun = std::min(bestRun, timer.nsecsElapsed());
            QVERIFY(result.complete);
            QCOMPARE(result.jarsScanned, 0);
            QCOMPARE(result.jarsFromCache, 200);
        }
        qInfo("cachedOnly runChecks over 200 cached jars: best of 5 = %.2f ms", double(bestRun) / 1e6);
        QVERIFY2(bestRun < 50LL * 1000 * 1000, "runChecks cachedOnly must stay well under 50 ms");
    }
};

QTEST_GUILESS_MAIN(PreLaunchCheckTest)

#include "PreLaunchCheck_test.moc"
