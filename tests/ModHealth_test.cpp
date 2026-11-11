// SPDX-License-Identifier: GPL-3.0-only
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "ModHealthFixtures.h"
#include "archive/ArchiveWriter.h"
#include "zero/mods/ModHealth.h"
#include "zero/mods/ModHealthScanner.h"
#include "zero/mods/ModMetadata.h"
#include "zero/mods/Versions.h"

using namespace Zero::Mods;

namespace {

// ---------------------------------------------------------------------------------------------------------- helpers

QString triCode(Tri t)
{
    return t == Tri::Yes ? QStringLiteral("Y") : (t == Tri::No ? QStringLiteral("N") : QStringLiteral("U"));
}

Dependency dep(const QString& id, const QString& predicate = QString(), const QString& side = QString())
{
    Dependency d;
    d.id = id;
    d.versionPredicate = predicate;
    d.side = side;
    return d;
}

ModEntry mod(LoaderFormat format, const QString& id, const QString& version = QStringLiteral("1.0.0"))
{
    ModEntry e;
    e.format = format;
    e.id = id;
    e.version = version;
    e.name = id.toUpper();
    return e;
}

ModMetadata jarOf(const QString& file, const QList<ModEntry>& mods)
{
    ModMetadata j;
    j.fileName = file;
    j.path = QStringLiteral("C:/mods/") + file;
    j.mods = mods;
    return j;
}

ModMetadata fabricJar(const QString& file, const QString& id, const QString& version = QStringLiteral("1.0.0"))
{
    return jarOf(file, { mod(LoaderFormat::Fabric, id, version) });
}

ModMetadata brokenJar(const QString& file)
{
    ModMetadata j;
    j.fileName = file;
    j.path = QStringLiteral("C:/mods/") + file;
    j.parseError = QStringLiteral("not valid");
    return j;
}

InstanceContext ctxOf(InstanceLoader loader, const QString& mc = QStringLiteral("1.20.1"), const QString& loaderVersion = QString())
{
    InstanceContext c;
    c.loader = loader;
    c.mcVersion = mc;
    c.loaderVersion = loaderVersion;
    return c;
}

InstanceContext fabricCtx()
{
    return ctxOf(InstanceLoader::Fabric, QStringLiteral("1.20.1"), QStringLiteral("0.15.11"));
}

Report run(const QList<ModMetadata>& jars, const InstanceContext& ctx, const QStringList& disabled = {}, const QList<ModMetadata>& disabledJars = {})
{
    return analyze(jars, ctx, disabled, disabledJars);
}

QList<Finding> ofKind(const Report& r, FindingKind kind)
{
    QList<Finding> out;
    for (const Finding& f : r.findings)
        if (f.kind == kind)
            out.append(f);
    return out;
}

QString confCode(const Report& r, FindingKind kind)
{
    const QList<Finding> list = ofKind(r, kind);
    if (list.isEmpty())
        return QStringLiteral("none");
    return confidenceId(list.first().confidence);
}

bool writeJar(const QString& path, const QList<QPair<QString, QByteArray>>& entries)
{
    MMCZip::ArchiveWriter w(path);
    if (!w.open())
        return false;
    for (const auto& e : entries)
        if (!w.addFile(e.first, e.second))
            return false;
    return bool(w.close());
}

QByteArray readAllBytes(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QByteArray fabricJson(const QString& id, const QString& version, const QString& extra = QString())
{
    return QStringLiteral(R"RAW({"schemaVersion":1,"id":"%1","version":"%2"%3})RAW").arg(id, version, extra.isEmpty() ? QString() : QLatin1Char(',') + extra).toUtf8();
}

}  // namespace

Q_DECLARE_METATYPE(Zero::Mods::InstanceLoader)

class ModHealthTest : public QObject {
    Q_OBJECT

   private slots:
    // ---------------------------------------------------------------------------------------------------- versions
    void compare_data()
    {
        QTest::addColumn<QString>("a");
        QTest::addColumn<QString>("b");
        QTest::addColumn<int>("expected");  // 99 = not comparable
        auto row = [](const char* name, const char* a, const char* b, int e) { QTest::newRow(name) << QString(a) << QString(b) << e; };
        row("numeric", "1.2.3", "1.2.10", -1);
        row("numeric-reverse", "1.10", "1.9", 1);
        row("padding", "1.20", "1.20.0", 0);
        row("prefix-v", "v1.2", "1.2", 0);
        row("build-metadata-ignored", "1.0.0+build5", "1.0.0+other", 0);
        row("prerelease-below-release", "1.0.0-beta.1", "1.0.0", -1);
        row("alpha-beta", "1.0.0-alpha", "1.0.0-beta", -1);
        row("rc-numbers", "1.0.0-rc.1", "1.0.0-rc.2", -1);
        row("rc-numeric-compare", "2.0.0-beta.2", "2.0.0-beta.10", -1);
        row("mojang-pre", "1.21-pre1", "1.21", -1);
        row("mojang-rc-over-pre", "1.21-rc1", "1.21-pre2", 1);
        row("mojang-rc-dot-same", "1.21-rc1", "1.21-rc.1", 0);
        row("numeric-qualifier-both", "1.0.0-1", "1.0.0-2", -1);
        row("snapshot-a", "23w13a", "1.20", 99);
        row("snapshot-b", "1.20", "24w01b", 99);
        row("old-beta", "b1.7.3", "1.0", 99);
        row("placeholder", "${version}", "1.0", 99);
        row("garbage", "garbage", "1.0", 99);
        row("empty", "", "1.0", 99);
        row("maven-or-semver-qualifier", "1.20.1-47.1.0", "1.20.1", 99);
        row("unknown-word-qualifier", "1.0-foo", "1.0", 99);
        row("different-core-decides", "1.20.1-47.1.0", "1.20.2", -1);
    }
    void compare()
    {
        QFETCH(QString, a);
        QFETCH(QString, b);
        QFETCH(int, expected);
        const auto r = compareVersions(a, b);
        if (expected == 99) {
            QVERIFY(!r.has_value());
        } else {
            QVERIFY(r.has_value());
            QCOMPARE(*r, expected);
        }
        const auto rev = compareVersions(b, a);  // antisymmetric
        QCOMPARE(rev.has_value(), r.has_value());
        if (r)
            QCOMPARE(*rev, -*r);
    }

    void fabricPredicate_data()
    {
        QTest::addColumn<QString>("version");
        QTest::addColumn<QString>("predicate");
        QTest::addColumn<QString>("expected");
        auto row = [](const char* name, const char* v, const char* p, const char* e) { QTest::newRow(name) << QString(v) << QString(p) << QString(e); };
        row("star", "1.20.1", "*", "Y");
        row("empty-is-any", "1.20.1", "", "Y");
        row("empty-version-empty-predicate", "", "", "Y");
        row("ge-yes", "1.20.1", ">=1.20", "Y");
        row("ge-equal", "1.20.1", ">=1.20.1", "Y");
        row("ge-no", "1.19.4", ">=1.20", "N");
        row("gt-equal-no", "1.20.1", ">1.20.1", "N");
        row("gt-yes", "1.20.2", ">1.20.1", "Y");
        row("le-yes", "1.20.1", "<=1.20.1", "Y");
        row("lt-no", "1.20.1", "<1.20.1", "N");
        row("lt-yes", "1.19", "<1.20.1", "Y");
        row("eq-op", "1.20.1", "=1.20.1", "Y");
        row("eq-bare", "1.20.1", "1.20.1", "Y");
        row("eq-bare-no", "1.20.2", "1.20.1", "N");
        row("tilde-yes", "1.20.4", "~1.20.1", "Y");
        row("tilde-next-minor", "1.21", "~1.20.1", "N");
        row("tilde-below", "1.20.1", "~1.20.2", "N");
        row("tilde-two-parts", "1.20.6", "~1.20", "Y");
        row("tilde-one-part", "1.9.9", "~1", "Y");
        row("tilde-one-part-no", "2.0.0", "~1", "N");
        row("caret-yes", "1.9.0", "^1.2.3", "Y");
        row("caret-no", "2.0.0", "^1.2.3", "N");
        row("caret-below", "1.2.2", "^1.2.3", "N");
        row("wildcard-x", "1.20.4", "1.20.x", "Y");
        row("wildcard-x-no", "1.21.0", "1.20.x", "N");
        row("wildcard-star", "1.20.4", "1.20.*", "Y");
        row("wildcard-major", "1.5.0", "1.x", "Y");
        row("wildcard-major-no", "2.0.0", "1.x", "N");
        row("and-yes", "1.20.4", ">=1.20 <1.21", "Y");
        row("and-no-upper", "1.21", ">=1.20 <1.21", "N");
        row("and-no-lower", "1.19", ">=1.20 <1.21", "N");
        row("operator-space", "1.20.4", ">= 1.20 < 1.21", "Y");
        row("or-first", "1.19", "1.19 || 1.20", "Y");
        row("or-second", "1.20", "1.19 || 1.20", "Y");
        row("or-none", "1.21", "1.19 || 1.20", "N");
        row("or-with-and", "1.21.3", ">=1.20 <1.21 || >=1.21.2", "Y");
        row("prerelease-below", "1.0.0-beta", ">=1.0.0", "N");
        row("unparseable-predicate", "1.20.1", "garbage", "U");
        row("unparseable-operand", "1.20.1", ">=abc", "U");
        row("operator-only", "1.20.1", ">=", "U");
        row("snapshot-version", "23w13a", ">=1.20", "U");
        row("empty-version", "", ">=1.0", "U");
        row("placeholder-version", "${version}", ">=1.0", "U");
        row("question-mark", "1.0", "?", "U");
        row("no-beats-unknown", "1.19", ">=1.20 <abc", "N");
        row("yes-beats-unknown-in-or", "1.20", "1.20 || abc", "Y");
        row("unknown-in-or", "1.21", "1.20 || abc", "U");
        row("wildcard-with-operator", "1.20.1", ">=1.x", "U");
    }
    void fabricPredicate()
    {
        QFETCH(QString, version);
        QFETCH(QString, predicate);
        QFETCH(QString, expected);
        QCOMPARE(triCode(matchesFabricPredicate(version, predicate)), expected);
    }

    void mavenRange_data()
    {
        QTest::addColumn<QString>("version");
        QTest::addColumn<QString>("range");
        QTest::addColumn<QString>("expected");
        auto row = [](const char* name, const char* v, const char* r, const char* e) { QTest::newRow(name) << QString(v) << QString(r) << QString(e); };
        row("closed-open-in", "1.20.1", "[1.20,1.21)", "Y");
        row("closed-open-out-upper", "1.21", "[1.20,1.21)", "N");
        row("closed-open-out-lower", "1.19.4", "[1.20,1.21)", "N");
        row("closed-lower-inclusive", "1.20", "[1.20,1.21)", "Y");
        row("exact", "1.20.1", "[1.20.1]", "Y");
        row("exact-no", "1.20.2", "[1.20.1]", "N");
        row("open-upper-yes", "47.2.0", "[47,)", "Y");
        row("open-upper-no", "46.0.5", "[47,)", "N");
        row("open-lower-inclusive-upper", "1.0", "(,1.0]", "Y");
        row("open-lower-exclusive-upper", "1.0", "(,1.0)", "N");
        row("exclusive-lower", "1.0", "(1.0,2.0)", "N");
        row("union-second", "1.5", "[1.0,1.2),[1.4,2.0)", "Y");
        row("union-gap", "1.3", "[1.0,1.2),[1.4,2.0)", "N");
        row("soft-at-least", "1.1", "1.0", "Y");
        row("soft-below", "0.9", "1.0", "N");
        row("star", "1.0", "*", "Y");
        row("empty", "1.0", "", "Y");
        row("placeholder-range", "1.0", "${minecraft_range}", "U");
        row("unbalanced", "1.0", "[1.0", "U");
        row("exclusive-single", "1.0", "(1.0]", "U");
        row("snapshot-version", "23w13a", "[1.20,)", "U");
        row("unparseable-bound", "1.0", "[abc,2.0)", "U");
        row("no-beats-unknown", "1.0", "[2.0,3.0),[abc,]", "U");  // second part is Unknown, first No
        row("yes-beats-unknown", "2.5", "[2.0,3.0),[abc,)", "Y");
        row("whitespace", "1.20.1", " [1.20, 1.21) ", "Y");
    }
    void mavenRange()
    {
        QFETCH(QString, version);
        QFETCH(QString, range);
        QFETCH(QString, expected);
        QCOMPARE(triCode(matchesMavenRange(version, range)), expected);
    }

    void describe()
    {
        QCOMPARE(describePredicate(QStringLiteral(">=1.20"), false), QStringLiteral("1.20 or newer"));
        QCOMPARE(describePredicate(QStringLiteral("<1.21"), false), QStringLiteral("older than 1.21"));
        QCOMPARE(describePredicate(QStringLiteral("[1.20,)"), true), QStringLiteral("1.20 or newer"));
        QCOMPARE(describePredicate(QStringLiteral("[1.20,1.21)"), true), QStringLiteral("1.20 up to, but not including, 1.21"));
        QCOMPARE(describePredicate(QStringLiteral("*"), false), QStringLiteral("any version"));
    }

    // ---------------------------------------------------------------------------------------------------- parsers
    void parseFabric()
    {
        const QByteArray json = R"RAW({
            "schemaVersion": 1, "id": "examplemod", "version": "1.2.3", "name": "Example Mod",
            "provides": ["examplealias"],
            "depends": { "fabricloader": ">=0.14", "minecraft": ["1.20", "1.20.1"], "fabric-api": "*" },
            "recommends": { "modmenu": "*" },
            "breaks": { "badmod": "<2.0" },
            "conflicts": { "meh": ">=1" },
            "jars": [ { "file": "META-INF/jars/inner.jar" } ]
        })RAW";
        const ParsedFile p = parseFabricModJson(json);
        QVERIFY(p.error.isEmpty());
        QCOMPARE(p.mods.size(), 1);
        const ModEntry& e = p.mods.first();
        QCOMPARE(e.format, LoaderFormat::Fabric);
        QCOMPARE(e.id, QStringLiteral("examplemod"));
        QCOMPARE(e.version, QStringLiteral("1.2.3"));
        QCOMPARE(e.name, QStringLiteral("Example Mod"));
        QCOMPARE(e.provides.size(), 1);
        QCOMPARE(e.provides.first().id, QStringLiteral("examplealias"));
        QCOMPARE(e.depends.size(), 3);
        QString minecraft;
        for (const Dependency& d : e.depends)
            if (d.id == QLatin1String("minecraft"))
                minecraft = d.versionPredicate;
        QCOMPARE(minecraft, QStringLiteral("1.20 || 1.20.1"));  // array = OR
        QCOMPARE(e.recommends.size(), 1);
        QVERIFY(!e.recommends.first().mandatory);
        QCOMPARE(e.breaks.size(), 1);
        QCOMPARE(e.breaks.first().versionPredicate, QStringLiteral("<2.0"));
        QCOMPARE(e.conflicts.size(), 1);
        QCOMPARE(p.nestedJars, QStringList{ QStringLiteral("META-INF/jars/inner.jar") });
    }

    void parseFabricErrors()
    {
        QVERIFY(!parseFabricModJson("{not json").error.isEmpty());
        QVERIFY(!parseFabricModJson("[1,2]").error.isEmpty());
        QVERIFY(!parseFabricModJson(R"RAW({"version":"1"})RAW").error.isEmpty());  // no id
        QVERIFY(parseFabricModJson(QByteArray("\xEF\xBB\xBF") + R"RAW({"id":"a","version":"1"})RAW").error.isEmpty());  // BOM tolerated
    }

    void parseQuilt()
    {
        const QByteArray json = R"RAW({
            "schema_version": 1,
            "quilt_loader": {
                "group": "g", "id": "quiltmod", "version": "2.0.0",
                "metadata": { "name": "Quilt Mod" },
                "provides": ["alias1", { "id": "alias2", "version": "9.9" }],
                "depends": [
                    "plainmod",
                    { "id": "objmod", "versions": ">=1.0 <2.0" },
                    { "id": "anymod", "versions": ["1.0", "2.0"] },
                    { "id": "optmod", "versions": "*", "optional": true },
                    { "id": "unlessmod", "versions": "*", "unless": "other" },
                    { "id": "complex", "versions": { "all": [">=1.0", "<2.0"] } },
                    { "id": "weird", "versions": { "not": "1.0" } }
                ],
                "breaks": [ { "id": "badmod", "versions": "<3", "unless": { "id": "fixer" } } ],
                "jars": ["META-INF/jars/q.jar"]
            }
        })RAW";
        const ParsedFile p = parseQuiltModJson(json);
        QVERIFY(p.error.isEmpty());
        QCOMPARE(p.mods.size(), 1);
        const ModEntry& e = p.mods.first();
        QCOMPARE(e.format, LoaderFormat::Quilt);
        QCOMPARE(e.id, QStringLiteral("quiltmod"));
        QCOMPARE(e.name, QStringLiteral("Quilt Mod"));
        QCOMPARE(e.provides.size(), 2);
        QCOMPARE(e.provides[1].version, QStringLiteral("9.9"));
        QHash<QString, Dependency> byId;
        for (const Dependency& d : e.depends)
            byId.insert(d.id, d);
        QCOMPARE(byId.size(), 6);  // optmod is a recommendation
        QCOMPARE(byId.value("plainmod").versionPredicate, QString());
        QCOMPARE(byId.value("objmod").versionPredicate, QStringLiteral(">=1.0 <2.0"));
        QCOMPARE(byId.value("anymod").versionPredicate, QStringLiteral("1.0 || 2.0"));
        QCOMPARE(byId.value("unlessmod").unless, QStringLiteral("other"));
        QCOMPARE(byId.value("complex").versionPredicate, QStringLiteral(">=1.0 <2.0"));
        QCOMPARE(byId.value("weird").versionPredicate, QStringLiteral("?"));
        QCOMPARE(e.recommends.size(), 1);
        QCOMPARE(e.recommends.first().id, QStringLiteral("optmod"));
        QCOMPARE(e.breaks.size(), 1);
        QCOMPARE(e.breaks.first().unless, QStringLiteral("fixer"));
        QCOMPARE(p.nestedJars, QStringList{ QStringLiteral("META-INF/jars/q.jar") });
        QVERIFY(!parseQuiltModJson(R"RAW({"quilt_loader":{}})RAW").error.isEmpty());
    }

    void parseForgeToml()
    {
        const QByteArray& toml = kForgeToml;
        const ParsedFile p = parseModsToml(toml, LoaderFormat::Forge);
        QVERIFY2(p.error.isEmpty(), qPrintable(p.error));
        QCOMPARE(p.mods.size(), 1);
        const ModEntry& e = p.mods.first();
        QCOMPARE(e.format, LoaderFormat::Forge);
        QCOMPARE(e.id, QStringLiteral("forgemod"));
        QCOMPARE(e.version, QStringLiteral("3.1.4"));
        QCOMPARE(e.name, QStringLiteral("Forge Mod"));
        QCOMPARE(e.depends.size(), 3);  // forge, minecraft, serveronly
        QCOMPARE(e.depends[0].versionPredicate, QStringLiteral("[47.1.0,)"));
        QCOMPARE(e.depends[2].side, QStringLiteral("SERVER"));
        QCOMPARE(e.recommends.size(), 2);  // optionalmod, and noflag (Forge without `mandatory`: assume nothing)
        QCOMPARE(e.recommends[0].id, QStringLiteral("optionalmod"));
        QVERIFY(!e.recommends[0].mandatory);
    }

    void parseNeoForgeToml()
    {
        const QByteArray& toml = kNeoForgeToml;
        const ParsedFile p = parseModsToml(toml, LoaderFormat::NeoForge);
        QVERIFY2(p.error.isEmpty(), qPrintable(p.error));
        const ModEntry& e = p.mods.first();
        QCOMPARE(e.format, LoaderFormat::NeoForge);
        QCOMPARE(e.depends.size(), 2);  // neoforge + untyped (NeoForge default type is required)
        QCOMPARE(e.recommends.size(), 1);
        QCOMPARE(e.breaks.size(), 1);
        QCOMPARE(e.breaks.first().id, QStringLiteral("bad"));
        QCOMPARE(e.conflicts.size(), 1);
        QCOMPARE(e.conflicts.first().id, QStringLiteral("meh"));
    }

    void parseMultiModToml()
    {
        const QByteArray& toml = kMultiModToml;
        const ParsedFile p = parseModsToml(toml, LoaderFormat::Forge);
        QCOMPARE(p.mods.size(), 2);
        QCOMPARE(p.mods[0].id, QStringLiteral("first"));
        QCOMPARE(p.mods[0].depends.size(), 1);
        QCOMPARE(p.mods[0].depends[0].id, QStringLiteral("lib"));
        QCOMPARE(p.mods[1].id, QStringLiteral("second"));
        QCOMPARE(p.mods[1].depends.size(), 1);
        QCOMPARE(p.mods[1].depends[0].id, QStringLiteral("other"));
        QVERIFY(!parseModsToml("this is = not [ toml", LoaderFormat::Forge).error.isEmpty());
        QVERIFY(!parseModsToml("license=\"x\"", LoaderFormat::Forge).error.isEmpty());  // no [[mods]]
    }

    void parseJarVersionPlaceholder()
    {
        const QByteArray toml = "[[mods]]\nmodId=\"m\"\nversion=\"${file.jarVersion}\"\n[[mods]]\nmodId=\"n\"\nversion=\"${other}\"\n";
        const ParsedFile resolved = parseModsToml(toml, LoaderFormat::Forge, QStringLiteral("2.5.0"));
        QCOMPARE(resolved.mods[0].version, QStringLiteral("2.5.0"));
        QCOMPARE(resolved.mods[1].version, QString());  // unresolved placeholder = unknown
        const ParsedFile unresolved = parseModsToml(toml, LoaderFormat::Forge);
        QCOMPARE(unresolved.mods[0].version, QString());

        const QByteArray manifest = "Manifest-Version: 1.0\r\nImplementation-Title: x\r\nImplementation-Version: 2.\r\n 5.0\r\n\r\nName: foo\r\nImplementation-Version: 9\r\n";
        QCOMPARE(parseManifestImplementationVersion(manifest), QStringLiteral("2.5.0"));  // continuation joined, main section only
        QCOMPARE(parseManifestImplementationVersion("Manifest-Version: 1.0\n"), QString());
    }

    void parseLegacyAndJarJar()
    {
        const ParsedFile arr = parseMcmodInfo(R"RAW([{"modid":"oldmod","name":"Old","version":"1.2"},{"name":"no id"}])RAW");
        QCOMPARE(arr.mods.size(), 1);
        QCOMPARE(arr.mods.first().format, LoaderFormat::LegacyForge);
        QCOMPARE(arr.mods.first().id, QStringLiteral("oldmod"));
        const ParsedFile obj = parseMcmodInfo(R"RAW({"modListVersion":2,"modList":[{"modid":"v2","version":"${version}"}]})RAW");
        QCOMPARE(obj.mods.size(), 1);
        QCOMPARE(obj.mods.first().version, QString());
        QVERIFY(!parseMcmodInfo("garbage").error.isEmpty());

        const ParsedFile jj = parseJarJarMetadata(kJarJarJson);
        QCOMPARE(jj.nestedJars, QStringList{ QStringLiteral("META-INF/jarjar/a-1.0.jar") });
    }

    // ---------------------------------------------------------------------------------------------------- analysis: dependencies
    void missingRequired()
    {
        ModEntry a = mod(LoaderFormat::Fabric, "a");
        a.depends.append(dep("fabric-api", ">=0.90"));
        const Report r = run({ jarOf("a.jar", { a }) }, fabricCtx());
        QCOMPARE(r.findings.size(), 1);
        const Finding& f = r.findings.first();
        QCOMPARE(f.kind, FindingKind::MissingDependency);
        QCOMPARE(f.confidence, Confidence::Confirmed);
        QCOMPARE(f.relatedId, QStringLiteral("fabric-api"));
        QCOMPARE(f.fileNames, QStringList{ QStringLiteral("a.jar") });
        QCOMPARE(f.modIds, QStringList{ QStringLiteral("a") });
        QVERIFY(!f.title.isEmpty() && !f.detail.isEmpty() && !f.suggestion.isEmpty());
        QCOMPARE(r.confirmed, 1);
        QCOMPARE(r.modsScanned, 1);
    }

    void missingRequiredNegatives()
    {
        ModEntry a = mod(LoaderFormat::Fabric, "a");
        a.depends.append(dep("fabric-api", ">=0.90"));
        const ModMetadata ja = jarOf("a.jar", { a });

        // provided by id
        QVERIFY(run({ ja, fabricJar("fabric-api.jar", "fabric-api", "0.92.0") }, fabricCtx()).isClean());

        // provided by `provides` of another mod
        ModEntry p = mod(LoaderFormat::Fabric, "bundle", "0.95.0");
        p.provides.append({ QStringLiteral("fabric-api"), QString() });
        QVERIFY(run({ ja, jarOf("bundle.jar", { p }) }, fabricCtx()).isClean());

        // provided by a nested jar
        ModMetadata outer = fabricJar("outer.jar", "outer");
        outer.nested.push_back(fabricJar("inner.jar", "fabric-api", "0.93.0"));
        QVERIFY(run({ ja, outer }, fabricCtx()).isClean());

        // optional (recommends) missing
        ModEntry opt = mod(LoaderFormat::Fabric, "opt");
        opt.recommends.append(dep("modmenu"));
        QVERIFY(run({ jarOf("opt.jar", { opt }) }, fabricCtx()).isClean());

        // environment ids
        ModEntry env = mod(LoaderFormat::Fabric, "env");
        env.depends = { dep("minecraft", "1.20.x"), dep("fabricloader", ">=0.14"), dep("java", ">=17") };
        QVERIFY(run({ jarOf("env.jar", { env }) }, fabricCtx()).isClean());

        // Forge language ids and the loader ids
        ModEntry fm = mod(LoaderFormat::Forge, "fm");
        fm.depends = { dep("forge", "[45,)"), dep("fml"), dep("javafml"), dep("minecraft", "[1.20,1.21)") };
        QVERIFY(run({ jarOf("fm.jar", { fm }) }, ctxOf(InstanceLoader::Forge, "1.20.1", "47.2.0")).isClean());

        // Quilt provides the Fabric loader id
        ModEntry fq = mod(LoaderFormat::Fabric, "fq");
        fq.depends = { dep("fabricloader", ">=0.14") };
        QVERIFY(run({ jarOf("fq.jar", { fq }) }, ctxOf(InstanceLoader::Quilt, "1.20.1", "0.26.0")).isClean());

        // server-only dependency does not apply to a client
        ModEntry s = mod(LoaderFormat::Forge, "s");
        s.depends = { dep("serverlib", "*", "SERVER") };
        QVERIFY(run({ jarOf("s.jar", { s }) }, ctxOf(InstanceLoader::Forge, "1.20.1", "47.2.0")).isClean());

        // Quilt `unless`
        ModEntry q = mod(LoaderFormat::Quilt, "q");
        Dependency u = dep("needed");
        u.unless = QStringLiteral("alt");
        q.depends = { u };
        QVERIFY(run({ jarOf("q.jar", { q }), jarOf("alt.jar", { mod(LoaderFormat::Quilt, "alt") }) }, ctxOf(InstanceLoader::Quilt)).isClean());
        QCOMPARE(run({ jarOf("q.jar", { q }) }, ctxOf(InstanceLoader::Quilt)).findings.size(), 1);

        // the legacy `fabric` id is only there when a jar provides it
        ModEntry legacy = mod(LoaderFormat::Fabric, "legacy");
        legacy.depends = { dep("fabric") };
        QCOMPARE(run({ jarOf("legacy.jar", { legacy }) }, fabricCtx()).confirmed, 1);
        ModEntry api = mod(LoaderFormat::Fabric, "fabric-api", "0.92.0");
        api.provides.append({ QStringLiteral("fabric"), QString() });
        QVERIFY(run({ jarOf("legacy.jar", { legacy }), jarOf("api.jar", { api }) }, fabricCtx()).isClean());
    }

    void brokenJarLowersMissing()
    {
        ModEntry a = mod(LoaderFormat::Fabric, "a");
        a.depends.append(dep("libx"));
        const Report r = run({ jarOf("a.jar", { a }), brokenJar("mystery.jar") }, fabricCtx());
        QCOMPARE(r.confirmed, 0);
        QCOMPARE(r.possible, 1);
        QCOMPARE(r.unknown, 1);
        QCOMPARE(confCode(r, FindingKind::MissingDependency), QStringLiteral("possible"));
        QCOMPARE(confCode(r, FindingKind::UnreadableMetadata), QStringLiteral("unknown"));
        // a jar with no recognizable metadata (no parse error either) does the same
        const Report r2 = run({ jarOf("a.jar", { a }), jarOf("plain.jar", {}) }, fabricCtx());
        QCOMPARE(confCode(r2, FindingKind::MissingDependency), QStringLiteral("possible"));
        QCOMPARE(confCode(r2, FindingKind::UnreadableMetadata), QStringLiteral("unknown"));
        QVERIFY(ofKind(r2, FindingKind::UnreadableMetadata).first().detail.contains(QLatin1String("may still work")));
        // a nested jar that could not be read does too
        ModMetadata outer = jarOf("o.jar", { a });
        outer.nested.push_back(brokenJar("inner.jar"));
        QCOMPARE(confCode(run({ outer }, fabricCtx()), FindingKind::MissingDependency), QStringLiteral("possible"));
    }

    void missingGroupedAndDisabled()
    {
        ModEntry a = mod(LoaderFormat::Fabric, "a");
        a.depends.append(dep("fabric-api"));
        ModEntry b = mod(LoaderFormat::Fabric, "b");
        b.depends.append(dep("fabric-api"));
        const Report grouped = run({ jarOf("a.jar", { a }), jarOf("b.jar", { b }) }, fabricCtx());
        QCOMPARE(grouped.findings.size(), 1);  // one finding per missing id
        QCOMPARE(grouped.findings.first().fileNames, (QStringList{ QStringLiteral("a.jar"), QStringLiteral("b.jar") }));

        // present only as a disabled file: still missing, but the suggestion says to enable it
        const Report byName = run({ jarOf("a.jar", { a }) }, fabricCtx(), { QStringLiteral("fabric-api-0.92.0+1.20.1.jar.disabled") });
        QCOMPARE(byName.findings.size(), 1);
        QCOMPARE(byName.confirmed, 1);
        QVERIFY(byName.findings.first().suggestion.contains(QLatin1String("fabric-api-0.92.0+1.20.1.jar.disabled")));
        QVERIFY(byName.findings.first().suggestion.contains(QLatin1String("may")));  // a name match is only a guess

        ModMetadata dis = fabricJar("renamed.jar.disabled", "fabric-api", "0.92.0");
        const Report exact = run({ jarOf("a.jar", { a }) }, fabricCtx(), { QStringLiteral("renamed.jar.disabled") }, { dis });
        QCOMPARE(exact.findings.size(), 1);
        QVERIFY(exact.findings.first().suggestion.startsWith(QLatin1String("Enable renamed.jar.disabled")));

        // an unrelated disabled file changes nothing
        const Report other = run({ jarOf("a.jar", { a }) }, fabricCtx(), { QStringLiteral("sodium-0.5.jar.disabled") });
        QVERIFY(other.findings.first().suggestion.startsWith(QLatin1String("Install")));
    }

    void dependencyVersion()
    {
        ModEntry a = mod(LoaderFormat::Fabric, "a");
        a.depends.append(dep("b", ">=2.0"));
        auto withB = [&](const QString& v) { return run({ jarOf("a.jar", { a }), fabricJar("b.jar", "b", v) }, fabricCtx()); };
        const Report no = withB("1.0.0");
        QCOMPARE(no.findings.size(), 1);
        QCOMPARE(no.findings.first().kind, FindingKind::DependencyVersion);
        QCOMPARE(no.findings.first().confidence, Confidence::Confirmed);
        QCOMPARE(no.findings.first().fileNames, (QStringList{ QStringLiteral("a.jar"), QStringLiteral("b.jar") }));
        QVERIFY(withB("2.1.0").isClean());
        const Report unknown = withB(QString());  // version not readable: cannot say
        QCOMPARE(unknown.findings.size(), 1);
        QCOMPARE(unknown.findings.first().confidence, Confidence::Possible);
        QCOMPARE(withB("23w13a").possible, 1);  // not comparable
        // a requirement we cannot parse is not a claim either
        ModEntry weird = mod(LoaderFormat::Fabric, "w");
        weird.depends.append(dep("b", "?"));
        QCOMPARE(run({ jarOf("w.jar", { weird }), fabricJar("b.jar", "b", "1.0") }, fabricCtx()).confirmed, 0);
        // two providers, one fits
        QVERIFY(run({ jarOf("a.jar", { a }), fabricJar("b1.jar", "b", "1.0.0"), fabricJar("b2.jar", "b", "3.0.0") }, fabricCtx()).findings.size() <= 1);
        // Forge Maven range
        ModEntry fa = mod(LoaderFormat::Forge, "fa");
        fa.depends.append(dep("fb", "[2,3)"));
        const Report fr = run({ jarOf("fa.jar", { fa }), jarOf("fb.jar", { mod(LoaderFormat::Forge, "fb", "3.5") }) },
                              ctxOf(InstanceLoader::Forge, "1.20.1", "47.2.0"));
        QCOMPARE(confCode(fr, FindingKind::DependencyVersion), QStringLiteral("confirmed"));
    }

    void minecraftVersion()
    {
        ModEntry a = mod(LoaderFormat::Fabric, "a");
        a.depends.append(dep("minecraft", "~1.19"));
        const Report no = run({ jarOf("a.jar", { a }) }, fabricCtx());
        QCOMPARE(no.findings.size(), 1);
        QCOMPARE(no.findings.first().kind, FindingKind::MinecraftVersion);
        QCOMPARE(no.findings.first().confidence, Confidence::Confirmed);
        QVERIFY(no.findings.first().detail.contains(QLatin1String("1.20.1")));
        QVERIFY(run({ jarOf("a.jar", { a }) }, ctxOf(InstanceLoader::Fabric, "1.19.2", "0.15.11")).isClean());
        QVERIFY(run({ jarOf("a.jar", { a }) }, ctxOf(InstanceLoader::Fabric, "23w13a", "0.15.11")).isClean());  // snapshot: Unknown
        QVERIFY(run({ jarOf("a.jar", { a }) }, ctxOf(InstanceLoader::Fabric, QString(), "0.15.11")).isClean());  // unknown instance version
        ModEntry f = mod(LoaderFormat::Forge, "f");
        f.depends.append(dep("minecraft", "[1.19,1.20)"));
        QCOMPARE(run({ jarOf("f.jar", { f }) }, ctxOf(InstanceLoader::Forge, "1.20.1", "47.2.0")).confirmed, 1);
        QVERIFY(run({ jarOf("f.jar", { f }) }, ctxOf(InstanceLoader::Forge, "1.19.2", "43.2.0")).isClean());
    }

    void loaderVersion()
    {
        ModEntry a = mod(LoaderFormat::Fabric, "a");
        a.depends.append(dep("fabricloader", ">=0.15.0"));
        const Report no = run({ jarOf("a.jar", { a }) }, ctxOf(InstanceLoader::Fabric, "1.20.1", "0.14.0"));
        QCOMPARE(no.findings.size(), 1);
        QCOMPARE(no.findings.first().kind, FindingKind::LoaderVersion);
        QCOMPARE(no.findings.first().confidence, Confidence::Confirmed);
        QVERIFY(run({ jarOf("a.jar", { a }) }, ctxOf(InstanceLoader::Fabric, "1.20.1", "0.15.11")).isClean());
        QVERIFY(run({ jarOf("a.jar", { a }) }, ctxOf(InstanceLoader::Fabric, "1.20.1", QString())).isClean());
        QVERIFY(run({ jarOf("a.jar", { a }) }, ctxOf(InstanceLoader::Quilt, "1.20.1", "0.26.0")).isClean());  // QL: version unknown

        ModEntry f = mod(LoaderFormat::Forge, "f");
        f.depends.append(dep("forge", "[47,)"));
        QCOMPARE(confCode(run({ jarOf("f.jar", { f }) }, ctxOf(InstanceLoader::Forge, "1.20.1", "45.0.0")), FindingKind::LoaderVersion),
                 QStringLiteral("confirmed"));
        QVERIFY(run({ jarOf("f.jar", { f }) }, ctxOf(InstanceLoader::Forge, "1.20.1", "47.1.3")).isClean());

        ModEntry n = mod(LoaderFormat::NeoForge, "n");
        n.depends.append(dep("neoforge", "[21.1,)"));
        QCOMPARE(confCode(run({ jarOf("n.jar", { n }) }, ctxOf(InstanceLoader::NeoForge, "1.21.1", "20.4.1")), FindingKind::LoaderVersion),
                 QStringLiteral("confirmed"));
        QVERIFY(run({ jarOf("n.jar", { n }) }, ctxOf(InstanceLoader::NeoForge, "1.21.1", "21.1.5")).isClean());
    }

    // ---------------------------------------------------------------------------------------------------- analysis: loaders
    void wrongLoader_data()
    {
        QTest::addColumn<InstanceLoader>("loader");
        QTest::addColumn<QString>("mc");
        QTest::addColumn<QString>("formats");  // comma separated formats the jar carries
        QTest::addColumn<bool>("connector");
        QTest::addColumn<QString>("expected");  // none / confirmed / likely / possible
        using L = InstanceLoader;
        auto row = [](const char* name, L l, const char* mc, const char* formats, bool connector, const char* e) {
            QTest::newRow(name) << l << QString(mc) << QString(formats) << connector << QString(e);
        };
        row("fabric-jar-in-forge", L::Forge, "1.20.1", "fabric", false, "confirmed");
        row("fabric-jar-in-forge-connector", L::Forge, "1.20.1", "fabric", true, "possible");
        row("fabric-jar-in-neoforge", L::NeoForge, "1.21.1", "fabric", false, "confirmed");
        row("fabric-jar-in-neoforge-connector", L::NeoForge, "1.21.1", "fabric", true, "possible");
        row("quilt-only-in-forge-connector", L::Forge, "1.20.1", "quilt", true, "confirmed");
        row("fabric-jar-in-quilt", L::Quilt, "1.20.1", "fabric", false, "none");
        row("quilt-jar-in-quilt", L::Quilt, "1.20.1", "quilt", false, "none");
        row("quilt-only-in-fabric", L::Fabric, "1.20.1", "quilt", false, "confirmed");
        row("forge-jar-in-fabric", L::Fabric, "1.20.1", "forge", false, "confirmed");
        row("forge-jar-in-quilt", L::Quilt, "1.20.1", "forge", false, "confirmed");
        row("neoforge-jar-in-fabric", L::Fabric, "1.21.1", "neoforge", false, "confirmed");
        row("legacy-jar-in-fabric", L::Fabric, "1.20.1", "legacy", false, "likely");
        row("forge-jar-in-neoforge-1.20.1", L::NeoForge, "1.20.1", "forge", false, "possible");
        row("forge-jar-in-neoforge-1.21.1", L::NeoForge, "1.21.1", "forge", false, "confirmed");
        row("forge-jar-in-neoforge-unknown-mc", L::NeoForge, "", "forge", false, "possible");
        row("neoforge-jar-in-forge", L::Forge, "1.20.1", "neoforge", false, "confirmed");
        row("fabric-jar-in-fabric", L::Fabric, "1.20.1", "fabric", false, "none");
        row("forge-jar-in-forge", L::Forge, "1.20.1", "forge", false, "none");
        row("legacy-jar-in-forge", L::Forge, "1.12.2", "legacy", false, "none");
        row("multi-format-fabric-and-forge-in-fabric", L::Fabric, "1.20.1", "fabric,forge", false, "none");
        row("multi-format-fabric-and-forge-in-forge", L::Forge, "1.20.1", "fabric,forge", false, "none");
        row("multi-format-fabric-and-neoforge-in-neoforge", L::NeoForge, "1.21.1", "fabric,neoforge", false, "none");
        row("fabric-and-quilt-in-forge", L::Forge, "1.20.1", "fabric,quilt", true, "possible");  // the Fabric half may load through Connector
        row("quilt-and-neoforge-in-forge", L::Forge, "1.20.1", "quilt,neoforge", true, "confirmed");
    }
    void wrongLoader()
    {
        QFETCH(InstanceLoader, loader);
        QFETCH(QString, mc);
        QFETCH(QString, formats);
        QFETCH(bool, connector);
        QFETCH(QString, expected);
        auto formatOf = [](const QString& s) {
            if (s == QLatin1String("fabric"))
                return LoaderFormat::Fabric;
            if (s == QLatin1String("quilt"))
                return LoaderFormat::Quilt;
            if (s == QLatin1String("forge"))
                return LoaderFormat::Forge;
            if (s == QLatin1String("neoforge"))
                return LoaderFormat::NeoForge;
            return LoaderFormat::LegacyForge;
        };
        QList<ModEntry> entries;
        for (const QString& f : formats.split(QLatin1Char(',')))
            entries.append(mod(formatOf(f), "themod"));
        QList<ModMetadata> jars = { jarOf("themod.jar", entries) };
        if (connector)
            jars.append(jarOf("connector.jar", { mod(loader == InstanceLoader::NeoForge ? LoaderFormat::NeoForge : LoaderFormat::Forge, "connector") }));
        const Report r = run(jars, ctxOf(loader, mc, QStringLiteral("1.0.0")));
        QCOMPARE(confCode(r, FindingKind::WrongLoader), expected);
        if (expected == QLatin1String("none"))
            QVERIFY(r.isClean());
        else
            QCOMPARE(ofKind(r, FindingKind::WrongLoader).first().fileNames, QStringList{ QStringLiteral("themod.jar") });
    }

    void wrongLoaderWordingMatchesConfidence()
    {
        const Report possible = run({ jarOf("m.jar", { mod(LoaderFormat::Fabric, "m") }), jarOf("c.jar", { mod(LoaderFormat::Forge, "connector") }) },
                                    ctxOf(InstanceLoader::Forge));
        const Finding p = ofKind(possible, FindingKind::WrongLoader).first();
        QVERIFY(p.detail.contains(QLatin1String("may")));
        QVERIFY(!p.detail.contains(QLatin1String("will not")));
        const Report confirmed = run({ jarOf("m.jar", { mod(LoaderFormat::Fabric, "m") }) }, ctxOf(InstanceLoader::Forge));
        QVERIFY(ofKind(confirmed, FindingKind::WrongLoader).first().detail.contains(QLatin1String("will not load")));
        const Report likely = run({ jarOf("m.jar", { mod(LoaderFormat::LegacyForge, "m") }) }, ctxOf(InstanceLoader::Fabric));
        QVERIFY(ofKind(likely, FindingKind::WrongLoader).first().detail.contains(QLatin1String("probably")));
    }

    void wrongLoaderProviderNotMissing()
    {
        // a jar that may still load (Connector) supplies the dependency: not reported as missing
        ModEntry user = mod(LoaderFormat::Forge, "user");
        user.depends = { dep("fabric-api") };
        const Report r = run({ jarOf("user.jar", { user }), jarOf("fapi.jar", { mod(LoaderFormat::Fabric, "fabric-api") }),
                               jarOf("connector.jar", { mod(LoaderFormat::Forge, "connector") }) },
                             ctxOf(InstanceLoader::Forge, "1.20.1", "47.2.0"));
        QCOMPARE(ofKind(r, FindingKind::MissingDependency).size(), 0);
        QCOMPARE(confCode(r, FindingKind::WrongLoader), QStringLiteral("possible"));
        // a jar that certainly does not load supplies nothing
        const Report r2 = run({ jarOf("user.jar", { user }), jarOf("fapi.jar", { mod(LoaderFormat::Fabric, "fabric-api") }) },
                              ctxOf(InstanceLoader::Forge, "1.20.1", "47.2.0"));
        QCOMPARE(confCode(r2, FindingKind::MissingDependency), QStringLiteral("confirmed"));
    }

    void formatWithBrokenMetadataIsNotWrongLoader()
    {
        // valid fabric.mod.json, but the mods.toml of the same jar did not parse: it may well be a Forge mod
        ModMetadata j = jarOf("both.jar", { mod(LoaderFormat::Fabric, "both") });
        j.unparsedFormats = { LoaderFormat::Forge };
        j.parseError = QStringLiteral("bad toml");
        const Report r = run({ j }, ctxOf(InstanceLoader::Forge, "1.20.1", "47.2.0"));
        QCOMPARE(confCode(r, FindingKind::WrongLoader), QStringLiteral("none"));
        QCOMPARE(confCode(r, FindingKind::UnreadableMetadata), QStringLiteral("unknown"));
        // in a Fabric instance the same jar is fine
        QVERIFY(run({ j }, fabricCtx()).isClean());
    }

    // ---------------------------------------------------------------------------------------------------- analysis: declared incompatibilities
    void declaredBreaksAndConflicts()
    {
        ModEntry a = mod(LoaderFormat::Fabric, "a");
        a.breaks = { dep("b", "<2.0") };
        auto with = [&](const QString& v) { return run({ jarOf("a.jar", { a }), fabricJar("b.jar", "b", v) }, fabricCtx()); };
        const Report hit = with("1.5.0");
        QCOMPARE(hit.findings.size(), 1);
        QCOMPARE(hit.findings.first().kind, FindingKind::DeclaredIncompatibility);
        QCOMPARE(hit.findings.first().confidence, Confidence::Confirmed);
        QCOMPARE(hit.findings.first().fileNames, (QStringList{ QStringLiteral("a.jar"), QStringLiteral("b.jar") }));
        QVERIFY(with("2.0.0").isClean());          // outside the declared range
        QVERIFY(with(QString()).isClean());        // unknown version: no claim
        QVERIFY(with("23w13a").isClean());
        QVERIFY(run({ jarOf("a.jar", { a }) }, fabricCtx()).isClean());  // not installed

        ModEntry c = mod(LoaderFormat::Fabric, "c");
        c.conflicts = { dep("b", "*") };
        const Report conflict = run({ jarOf("c.jar", { c }), fabricJar("b.jar", "b") }, fabricCtx());
        QCOMPARE(conflict.findings.size(), 1);
        QCOMPARE(conflict.findings.first().confidence, Confidence::Likely);
        QVERIFY(conflict.findings.first().detail.contains(QLatin1String("probably")));
    }

    void declaredMutualIsOneFinding()
    {
        ModEntry a = mod(LoaderFormat::Fabric, "a");
        a.breaks = { dep("b") };
        ModEntry b = mod(LoaderFormat::Fabric, "b");
        b.breaks = { dep("a") };
        const Report r = run({ jarOf("a.jar", { a }), jarOf("b.jar", { b }) }, fabricCtx());
        QCOMPARE(r.findings.size(), 1);
    }

    void declaredBreaksMinecraftAndUnless()
    {
        ModEntry a = mod(LoaderFormat::Quilt, "a");
        Dependency brk = dep("minecraft", ">=1.20");
        a.breaks = { brk };
        QCOMPARE(run({ jarOf("a.jar", { a }) }, ctxOf(InstanceLoader::Quilt, "1.20.1")).confirmed, 1);
        QVERIFY(run({ jarOf("a.jar", { a }) }, ctxOf(InstanceLoader::Quilt, "1.19.4")).isClean());
        QVERIFY(run({ jarOf("a.jar", { a }) }, ctxOf(InstanceLoader::Quilt, "23w13a")).isClean());

        ModEntry u = mod(LoaderFormat::Quilt, "u");
        Dependency b2 = dep("other");
        b2.unless = QStringLiteral("fixer");
        u.breaks = { b2 };
        const ModMetadata other = jarOf("other.jar", { mod(LoaderFormat::Quilt, "other") });
        QCOMPARE(run({ jarOf("u.jar", { u }), other }, ctxOf(InstanceLoader::Quilt)).confirmed, 1);
        QVERIFY(run({ jarOf("u.jar", { u }), other, jarOf("fixer.jar", { mod(LoaderFormat::Quilt, "fixer") }) }, ctxOf(InstanceLoader::Quilt)).isClean());
    }

    void declaredInsideSameJarIgnored()
    {
        ModEntry a = mod(LoaderFormat::Fabric, "a");
        a.breaks = { dep("inner") };
        ModMetadata outer = jarOf("a.jar", { a });
        outer.nested.push_back(fabricJar("inner.jar", "inner"));
        QVERIFY(run({ outer }, fabricCtx()).isClean());
    }

    void neoForgeIncompatible()
    {
        const InstanceContext ctx = ctxOf(InstanceLoader::NeoForge, "1.21.1", "21.1.5");
        ModEntry n = mod(LoaderFormat::NeoForge, "n");
        n.breaks = { dep("bad", "[1,2)") };
        n.conflicts = { dep("meh", "*") };
        const Report r = run({ jarOf("n.jar", { n }), jarOf("bad.jar", { mod(LoaderFormat::NeoForge, "bad", "1.5") }),
                               jarOf("meh.jar", { mod(LoaderFormat::NeoForge, "meh", "1.0") }) },
                             ctx);
        QCOMPARE(r.findings.size(), 2);
        QCOMPARE(r.confirmed, 1);  // incompatible
        QCOMPARE(r.likely, 1);     // discouraged
        // out-of-range version of the incompatible mod: nothing
        QVERIFY(run({ jarOf("n.jar", { n }), jarOf("bad.jar", { mod(LoaderFormat::NeoForge, "bad", "2.5") }) }, ctx).isClean());
    }

    void forgeOptionalRange()
    {
        const InstanceContext ctx = ctxOf(InstanceLoader::Forge, "1.20.1", "47.2.0");
        ModEntry f = mod(LoaderFormat::Forge, "f");
        Dependency opt = dep("opt", "[2,3)");
        opt.mandatory = false;
        f.recommends = { opt };
        auto with = [&](const QString& v) { return run({ jarOf("f.jar", { f }), jarOf("opt.jar", { mod(LoaderFormat::Forge, "opt", v) }) }, ctx); };
        const Report no = with("3.1");
        QCOMPARE(no.findings.size(), 1);
        QCOMPARE(no.findings.first().kind, FindingKind::DeclaredIncompatibility);
        QCOMPARE(no.findings.first().confidence, Confidence::Likely);
        QVERIFY(with("2.5").isClean());
        QVERIFY(with(QString()).isClean());
        QVERIFY(run({ jarOf("f.jar", { f }) }, ctx).isClean());  // optional and absent
        // a Fabric `recommends` is never enforced
        ModEntry fa = mod(LoaderFormat::Fabric, "fa");
        fa.recommends = { dep("opt", "<1") };
        QVERIFY(run({ jarOf("fa.jar", { fa }), fabricJar("opt.jar", "opt", "5.0") }, fabricCtx()).isClean());
    }

    // ---------------------------------------------------------------------------------------------------- analysis: duplicates, unreadable
    void duplicates()
    {
        const Report r = run({ fabricJar("a-1.0.jar", "samemod", "1.0.0"), fabricJar("a-2.0.jar", "samemod", "2.0.0"), fabricJar("other.jar", "other") },
                             fabricCtx());
        QCOMPARE(r.findings.size(), 1);
        const Finding& f = r.findings.first();
        QCOMPARE(f.kind, FindingKind::DuplicateMod);
        QCOMPARE(f.confidence, Confidence::Confirmed);
        QCOMPARE(f.fileNames, (QStringList{ QStringLiteral("a-1.0.jar"), QStringLiteral("a-2.0.jar") }));
        QVERIFY(f.suggestion.contains(QLatin1String("a-2.0.jar")));  // newest

        // a copy bundled in another jar is deduplicated by the loader
        ModMetadata outer = fabricJar("outer.jar", "outer");
        outer.nested.push_back(fabricJar("inner.jar", "samemod", "1.0.0"));
        QVERIFY(run({ outer, fabricJar("a.jar", "samemod", "2.0.0") }, fabricCtx()).isClean());
        // two bundled copies in different jars neither
        ModMetadata outer2 = fabricJar("outer2.jar", "outer2");
        outer2.nested.push_back(fabricJar("inner.jar", "samemod", "1.5.0"));
        QVERIFY(run({ outer, outer2 }, fabricCtx()).isClean());
        // disabled copies do not count
        QVERIFY(run({ fabricJar("a.jar", "samemod") }, fabricCtx(), { QStringLiteral("old-a.jar.disabled") }, { fabricJar("old-a.jar.disabled", "samemod") }).isClean());
        // unparseable versions: still a duplicate, no claim about which is newer
        const Report unknownVersions = run({ fabricJar("x1.jar", "dup", QString()), fabricJar("x2.jar", "dup", "1.0") }, fabricCtx());
        QCOMPARE(unknownVersions.confirmed, 1);
        QVERIFY(unknownVersions.findings.first().suggestion.startsWith(QLatin1String("Keep only one")));
        // legacy (mcmod.info based) ids are less reliable
        const Report legacy = run({ jarOf("l1.jar", { mod(LoaderFormat::LegacyForge, "old") }), jarOf("l2.jar", { mod(LoaderFormat::LegacyForge, "old") }) },
                                  ctxOf(InstanceLoader::Forge, "1.12.2", "14.23.5.2860"));
        QCOMPARE(confCode(legacy, FindingKind::DuplicateMod), QStringLiteral("likely"));
    }

    void unreadable()
    {
        const Report r = run({ brokenJar("b.jar"), fabricJar("ok.jar", "ok") }, fabricCtx());
        QCOMPARE(r.findings.size(), 1);
        QCOMPARE(r.findings.first().kind, FindingKind::UnreadableMetadata);
        QCOMPARE(r.findings.first().confidence, Confidence::Unknown);
        QCOMPARE(r.unknown, 1);
        QCOMPARE(r.modsScanned, 2);
        QVERIFY(run({ fabricJar("ok.jar", "ok") }, fabricCtx()).isClean());
    }

    void noLoaderInstance()
    {
        // without a known loader nothing is judged by format; dependencies still count
        ModEntry a = mod(LoaderFormat::Forge, "a");
        a.depends = { dep("lib") };
        const Report r = run({ jarOf("a.jar", { a }), fabricJar("f.jar", "f") }, ctxOf(InstanceLoader::None, "1.20.1"));
        QCOMPARE(r.findings.size(), 1);
        QCOMPARE(r.findings.first().kind, FindingKind::MissingDependency);
    }

    // ---------------------------------------------------------------------------------------------------- analysis: ordering, determinism
    void sortingAndCounts()
    {
        ModEntry zed = mod(LoaderFormat::Fabric, "zed");
        zed.depends = { dep("missing-one") };  // Possible (an unreadable jar is present)
        ModEntry alpha = mod(LoaderFormat::Fabric, "alpha");
        alpha.depends = { dep("missing-two") };  // Possible, sorts before zed by name
        ModEntry cc = mod(LoaderFormat::Fabric, "cc");
        cc.conflicts = { dep("dd") };  // Likely
        ModEntry opt = mod(LoaderFormat::Fabric, "opt");
        opt.depends = { dep("dd", ">=5") };  // Confirmed version mismatch (dd is 1.0.0)
        const QList<ModMetadata> jars = { jarOf("zed.jar", { zed }), jarOf("alpha.jar", { alpha }), jarOf("cc.jar", { cc }), fabricJar("dd.jar", "dd"),
                                          brokenJar("zz.jar"), jarOf("opt.jar", { opt }) };
        const Report r = run(jars, fabricCtx());
        QCOMPARE(r.findings.size(), 5);
        QCOMPARE(r.confirmed, 1);
        QCOMPARE(r.likely, 1);
        QCOMPARE(r.possible, 2);
        QCOMPARE(r.unknown, 1);
        QCOMPARE(r.count(Confidence::Possible), 2);
        QCOMPARE(r.modsScanned, 6);
        QList<int> order;
        for (const Finding& f : r.findings)
            order.append(int(f.confidence));
        QVERIFY(std::is_sorted(order.begin(), order.end()));
        QCOMPARE(r.findings[0].kind, FindingKind::DependencyVersion);
        QCOMPARE(r.findings[1].kind, FindingKind::DeclaredIncompatibility);
        QCOMPARE(r.findings[2].args.value(0), QStringLiteral("ALPHA"));
        QCOMPARE(r.findings[3].args.value(0), QStringLiteral("ZED"));
        QCOMPARE(r.findings.last().confidence, Confidence::Unknown);

        // input order does not matter
        QList<ModMetadata> reversed;
        for (int i = int(jars.size()) - 1; i >= 0; --i)
            reversed.append(jars[i]);
        const Report r2 = run(reversed, fabricCtx());
        QCOMPARE(r2.findings.size(), r.findings.size());
        for (int i = 0; i < r.findings.size(); ++i) {
            QCOMPARE(r2.findings[i].title, r.findings[i].title);
            QCOMPARE(r2.findings[i].detail, r.findings[i].detail);
            QCOMPARE(r2.findings[i].fileNames, r.findings[i].fileNames);
        }
    }

    void nestedModDependenciesAreChecked()
    {
        ModEntry inner = mod(LoaderFormat::Fabric, "inner");
        inner.depends = { dep("nothere") };
        ModMetadata outer = fabricJar("outer.jar", "outer");
        ModMetadata in = jarOf("inner.jar", { inner });
        outer.nested.push_back(in);
        const Report r = run({ outer }, fabricCtx());
        QCOMPARE(r.findings.size(), 1);
        QCOMPARE(r.findings.first().fileNames, QStringList{ QStringLiteral("outer.jar") });  // attributed to the file the user sees
    }

    // ---------------------------------------------------------------------------------------------------- instance context
    void instanceContext()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(readInstanceContext(dir.path()).loader == InstanceLoader::None);  // no file
        auto write = [&](const QByteArray& json) {
            QFile f(dir.filePath(QStringLiteral("mmc-pack.json")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(json);
        };
        write(R"RAW({"formatVersion":1,"components":[{"uid":"net.minecraft","version":"1.20.1"},{"uid":"net.fabricmc.fabric-loader","version":"0.15.11"}]})RAW");
        InstanceContext c = readInstanceContext(dir.path());
        QCOMPARE(c.mcVersion, QStringLiteral("1.20.1"));
        QCOMPARE(c.loader, InstanceLoader::Fabric);
        QCOMPARE(c.loaderVersion, QStringLiteral("0.15.11"));
        write(R"RAW({"components":[{"uid":"net.minecraft","version":"1.20.1"},{"uid":"org.quiltmc.quilt-loader","version":"0.26.0"}]})RAW");
        QCOMPARE(readInstanceContext(dir.path()).loader, InstanceLoader::Quilt);
        write(R"RAW({"components":[{"uid":"net.minecraft","version":"1.20.1"},{"uid":"net.minecraftforge","version":"47.2.0"}]})RAW");
        c = readInstanceContext(dir.path());
        QCOMPARE(c.loader, InstanceLoader::Forge);
        QCOMPARE(c.loaderVersion, QStringLiteral("47.2.0"));
        write(R"RAW({"components":[{"uid":"net.minecraft","version":"1.21.1"},{"uid":"net.neoforged","version":"21.1.5"}]})RAW");
        QCOMPARE(readInstanceContext(dir.path()).loader, InstanceLoader::NeoForge);
        write(R"RAW({"components":[{"uid":"net.minecraft","version":"1.20.1"},{"uid":"net.neoforged","version":"21.1.5","disabled":true}]})RAW");
        QCOMPARE(readInstanceContext(dir.path()).loader, InstanceLoader::None);  // disabled component ignored
        write(R"RAW({"components":[{"uid":"net.minecraft","version":"1.20.1"}]})RAW");
        c = readInstanceContext(dir.path());
        QCOMPARE(c.loader, InstanceLoader::None);
        QCOMPARE(c.mcVersion, QStringLiteral("1.20.1"));
        write("not json");
        QCOMPARE(readInstanceContext(dir.path()).loader, InstanceLoader::None);
    }

    // ---------------------------------------------------------------------------------------------------- real jars
    void realJarRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // innermost jar first, then wrap it: depth 0 (top) .. 4
        QString previous;
        for (int level = 4; level >= 0; --level) {
            const QString path = dir.filePath(QStringLiteral("lvl%1.jar").arg(level));
            QList<QPair<QString, QByteArray>> entries;
            entries.append({ QStringLiteral("fabric.mod.json"),
                             fabricJson(QStringLiteral("lvl%1").arg(level), QStringLiteral("1.%1.0").arg(level),
                                        previous.isEmpty() ? QString() : QStringLiteral(R"RAW("jars":[{"file":"META-INF/jars/next.jar"}])RAW")) });
            if (!previous.isEmpty())
                entries.append({ QStringLiteral("META-INF/jars/next.jar"), readAllBytes(previous) });
            QVERIFY(writeJar(path, entries));
            previous = path;
        }
        const ModMetadata deep = parseJar(dir.filePath(QStringLiteral("lvl0.jar")));
        QVERIFY(deep.parseError.isEmpty());
        QCOMPARE(deep.fileName, QStringLiteral("lvl0.jar"));
        QCOMPARE(deep.primaryId(), QStringLiteral("lvl0"));
        const ModMetadata* node = &deep;
        int depth = 0;
        while (!node->nested.empty()) {
            node = &node->nested.front();
            ++depth;
            QCOMPARE(node->primaryId(), QStringLiteral("lvl%1").arg(depth));
            QCOMPARE(node->fileName, QStringLiteral("next.jar"));
        }
        QCOMPARE(depth, kMaxNestingDepth);  // lvl4 is deeper than the limit and is not read
        // the same from memory
        const ModMetadata fromBytes = parseJarBytes(readAllBytes(dir.filePath(QStringLiteral("lvl0.jar"))), QStringLiteral("mem.jar"));
        QCOMPARE(fromBytes.fileName, QStringLiteral("mem.jar"));
        QCOMPARE(fromBytes.nested.size(), size_t(1));
        QCOMPARE(fromBytes.nested.front().nested.front().primaryId(), QStringLiteral("lvl2"));
    }

    void realJarFabricNestedAndAnalysis()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString inner = dir.filePath(QStringLiteral("inner.jar"));
        QVERIFY(writeJar(inner, { { QStringLiteral("fabric.mod.json"), fabricJson("inner", "1.2.0", R"RAW("provides":["inner-alias"])RAW") } }));
        const QString outer = dir.filePath(QStringLiteral("outer.jar"));
        QVERIFY(writeJar(outer, { { QStringLiteral("fabric.mod.json"),
                                    fabricJson("outer", "1.0.0", R"RAW("depends":{"inner-alias":">=1.0","fabricloader":">=0.14"},"jars":[{"file":"META-INF/jars/inner.jar"}])RAW") },
                                  { QStringLiteral("META-INF/jars/inner.jar"), readAllBytes(inner) } }));
        const ModMetadata md = parseJar(outer);
        QVERIFY(md.parseError.isEmpty());
        QCOMPARE(md.mods.size(), 1);
        QCOMPARE(md.mods.first().depends.size(), 2);
        QCOMPARE(md.nested.size(), size_t(1));
        QCOMPARE(md.nested.front().primaryId(), QStringLiteral("inner"));
        QCOMPARE(md.nested.front().mods.first().provides.first().id, QStringLiteral("inner-alias"));
        QCOMPARE(md.loaderFormat(), LoaderFormat::Fabric);
        // the nested jar satisfies the dependency
        QVERIFY(run({ md }, fabricCtx()).isClean());
        // a Fabric-declared nested jar that is not in the archive is reported (opaque)
        const QString lost = dir.filePath(QStringLiteral("lost.jar"));
        QVERIFY(writeJar(lost, { { QStringLiteral("fabric.mod.json"), fabricJson("lost", "1.0.0", R"RAW("jars":[{"file":"META-INF/jars/gone.jar"}],"depends":{"x":"*"})RAW") } }));
        const ModMetadata lostMd = parseJar(lost);
        QCOMPARE(lostMd.nested.size(), size_t(1));
        QVERIFY(!lostMd.nested.front().parseError.isEmpty());
        QCOMPARE(confCode(run({ lostMd }, fabricCtx()), FindingKind::MissingDependency), QStringLiteral("possible"));
    }

    void realJarNeoForgeLegacyToml()
    {
        // NeoForge 1.20.2 - 1.20.4 mods ship META-INF/mods.toml; a mandatory "neoforge" dependency makes them NeoForge mods
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString neo = dir.filePath(QStringLiteral("neo.jar"));
        QVERIFY(writeJar(neo, { { QStringLiteral("META-INF/mods.toml"),
                                  "modLoader=\"javafml\"\nloaderVersion=\"[2,)\"\nlicense=\"MIT\"\n[[mods]]\nmodId=\"neomod\"\nversion=\"1.0.0\"\n"
                                  "[[dependencies.neomod]]\nmodId=\"neoforge\"\ntype=\"required\"\nversionRange=\"[20.4,)\"\n" } }));
        const QString forge = dir.filePath(QStringLiteral("forge.jar"));
        QVERIFY(writeJar(forge, { { QStringLiteral("META-INF/mods.toml"),
                                    "[[mods]]\nmodId=\"forgeonly\"\nversion=\"1.0.0\"\n"
                                    "[[dependencies.forgeonly]]\nmodId=\"forge\"\nmandatory=true\nversionRange=\"[49,)\"\n" } }));
        const ModMetadata n = parseJar(neo);
        QCOMPARE(n.mods.size(), 1);
        QCOMPARE(n.mods.first().format, LoaderFormat::NeoForge);
        const ModMetadata f = parseJar(forge);
        QCOMPARE(f.mods.size(), 1);
        QCOMPARE(f.mods.first().format, LoaderFormat::Forge);
    }

    void realJarForgeManifestAndJarJar()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString lib = dir.filePath(QStringLiteral("lib.jar"));
        QVERIFY(writeJar(lib, { { QStringLiteral("META-INF/mods.toml"), "[[mods]]\nmodId=\"libmod\"\nversion=\"1.0.0\"\n" } }));
        const QString plain = dir.filePath(QStringLiteral("plain.jar"));
        QVERIFY(writeJar(plain, { { QStringLiteral("some/Class.class"), "x" } }));
        const QString forge = dir.filePath(QStringLiteral("forge.jar"));
        const QByteArray toml =
            "modLoader=\"javafml\"\nloaderVersion=\"[47,)\"\nlicense=\"MIT\"\n[[mods]]\nmodId=\"forgemod\"\nversion=\"${file.jarVersion}\"\n"
            "[[dependencies.forgemod]]\nmodId=\"libmod\"\nmandatory=true\nversionRange=\"[1,)\"\n";
        const QByteArray jarjar =
            R"RAW({"jars":[{"path":"META-INF/jarjar/lib.jar"},{"path":"META-INF/jarjar/plain.jar"}]})RAW";
        QVERIFY(writeJar(forge, { { QStringLiteral("META-INF/MANIFEST.MF"), "Manifest-Version: 1.0\r\nImplementation-Version: 2.5.0\r\n\r\n" },
                                  { QStringLiteral("META-INF/mods.toml"), toml },
                                  { QStringLiteral("META-INF/jarjar/metadata.json"), jarjar },
                                  { QStringLiteral("META-INF/jarjar/lib.jar"), readAllBytes(lib) },
                                  { QStringLiteral("META-INF/jarjar/plain.jar"), readAllBytes(plain) } }));
        const ModMetadata md = parseJar(forge);
        QVERIFY2(md.parseError.isEmpty(), qPrintable(md.parseError));
        QCOMPARE(md.loaderFormat(), LoaderFormat::Forge);
        QCOMPARE(md.mods.first().version, QStringLiteral("2.5.0"));  // from the manifest
        QCOMPARE(md.nested.size(), size_t(2));
        QCOMPARE(md.nested[0].primaryId(), QStringLiteral("libmod"));
        QVERIFY(md.nested[1].mods.isEmpty());       // a library without mod metadata
        QVERIFY(md.nested[1].parseError.isEmpty());  // is not an error
        // the bundled mod satisfies the dependency and the plain library does not make the analysis uncertain
        QVERIFY(run({ md }, ctxOf(InstanceLoader::Forge, "1.20.1", "47.2.0")).isClean());

        // not an archive at all / an archive without metadata
        const QString junk = dir.filePath(QStringLiteral("junk.jar"));
        QFile f(junk);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("this is not a zip file");
        f.close();
        const ModMetadata bad = parseJar(junk);
        QVERIFY(bad.mods.isEmpty());
        QVERIFY(!bad.parseError.isEmpty());
        QVERIFY(parseJar(dir.filePath(QStringLiteral("missing.jar"))).mods.isEmpty());
        const ModMetadata noMeta = parseJar(plain);
        QVERIFY(noMeta.mods.isEmpty());
        // broken fabric.mod.json
        const QString broken = dir.filePath(QStringLiteral("broken.jar"));
        QVERIFY(writeJar(broken, { { QStringLiteral("fabric.mod.json"), "{ nope" } }));
        const ModMetadata brokenMd = parseJar(broken);
        QVERIFY(brokenMd.mods.isEmpty());
        QCOMPARE(brokenMd.unparsedFormats, QList<LoaderFormat>{ LoaderFormat::Fabric });
        QVERIFY(!brokenMd.parseError.isEmpty());
    }

    void folderScan()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QDir mods(dir.filePath(QStringLiteral("mods")));
        QVERIFY(QDir().mkpath(mods.path()));
        QVERIFY(QDir().mkpath(mods.filePath(QStringLiteral("subfolder"))));
        QVERIFY(writeJar(mods.filePath(QStringLiteral("a.jar")), { { QStringLiteral("fabric.mod.json"), fabricJson("a", "1.0.0", R"RAW("depends":{"libx":"*"})RAW") } }));
        QVERIFY(writeJar(mods.filePath(QStringLiteral("libx.jar.disabled")), { { QStringLiteral("fabric.mod.json"), fabricJson("libx", "1.0.0") } }));
        QVERIFY(writeJar(mods.filePath(QStringLiteral("subfolder/ignored.jar")), { { QStringLiteral("fabric.mod.json"), fabricJson("ignored", "1.0.0") } }));
        QFile notJar(mods.filePath(QStringLiteral("readme.txt")));
        QVERIFY(notJar.open(QIODevice::WriteOnly));
        notJar.write("hi");
        notJar.close();
        QFile junk(mods.filePath(QStringLiteral("junk.jar")));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write("not a zip");
        junk.close();

        const Report r = scanModsFolder(mods.path(), fabricCtx());
        QCOMPARE(r.modsScanned, 2);  // a.jar + junk.jar; the subfolder, readme.txt and the disabled file are not enabled mods
        QCOMPARE(ofKind(r, FindingKind::UnreadableMetadata).size(), 1);
        const QList<Finding> missing = ofKind(r, FindingKind::MissingDependency);
        QCOMPARE(missing.size(), 1);
        QCOMPARE(missing.first().confidence, Confidence::Possible);  // junk.jar might provide it
        QVERIFY(missing.first().suggestion.startsWith(QLatin1String("Enable libx.jar.disabled")));
        // nothing was written into the mods folder
        QCOMPARE(mods.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).size(), 5);
    }

    void asyncScanner()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.filePath(QStringLiteral("inst"));
        const QString mods = root + QStringLiteral("/minecraft/mods");
        QVERIFY(QDir().mkpath(mods));
        QFile pack(root + QStringLiteral("/mmc-pack.json"));
        QVERIFY(pack.open(QIODevice::WriteOnly));
        pack.write(R"RAW({"formatVersion":1,"components":[{"uid":"net.minecraft","version":"1.20.1"},{"uid":"net.fabricmc.fabric-loader","version":"0.15.11"}]})RAW");
        pack.close();
        QVERIFY(writeJar(mods + QStringLiteral("/a.jar"), { { QStringLiteral("fabric.mod.json"), fabricJson("a", "1.0.0", R"RAW("depends":{"minecraft":"~1.19"})RAW") } }));

        ModHealthScanner scanner;
        QSignalSpy ready(&scanner, &ModHealthScanner::reportReady);
        scanner.analyze(QStringLiteral("old"), mods, root);
        scanner.analyze(QStringLiteral("new"), mods, root);  // supersedes the first
        QVERIFY(scanner.isScanning());
        QCOMPARE(scanner.generation(), quint64(2));
        QVERIFY(ready.wait(15000));
        QTest::qWait(300);  // the superseded job must not report
        QCOMPARE(ready.size(), 1);
        QCOMPARE(ready.first().at(0).toString(), QStringLiteral("new"));
        const Report report = ready.first().at(1).value<Report>();
        QCOMPARE(report.modsScanned, 1);
        QCOMPARE(report.confirmed, 1);
        QCOMPARE(report.findings.first().kind, FindingKind::MinecraftVersion);
        QVERIFY(!scanner.isScanning());
    }

    void textsAreUserFacing()
    {
        // every finding kind carries ready-to-show text and a stable id
        ModEntry a = mod(LoaderFormat::Fabric, "a");
        a.depends = { dep("gone"), dep("minecraft", "~1.19"), dep("fabricloader", ">=9") };
        a.breaks = { dep("b") };
        const Report r = run({ jarOf("a.jar", { a }), fabricJar("b.jar", "b"), fabricJar("b2.jar", "b"), jarOf("f.jar", { mod(LoaderFormat::Forge, "f") }),
                               brokenJar("x.jar") },
                             ctxOf(InstanceLoader::Fabric, "1.20.1", "0.15.11"));
        QSet<QString> kinds;
        for (const Finding& f : r.findings) {
            QVERIFY(!f.title.isEmpty());
            QVERIFY(!f.detail.isEmpty());
            QVERIFY(!f.suggestion.isEmpty());
            QVERIFY(!f.title.contains(QLatin1Char('%')));
            QVERIFY(!f.detail.contains(QLatin1Char('%')));
            kinds.insert(findingKindId(f.kind));
        }
        QVERIFY(kinds.contains(QStringLiteral("missing-dependency")));
        QVERIFY(kinds.contains(QStringLiteral("minecraft-version")));
        QVERIFY(kinds.contains(QStringLiteral("loader-version")));
        QVERIFY(kinds.contains(QStringLiteral("wrong-loader")));
        QVERIFY(kinds.contains(QStringLiteral("duplicate-mod")));
        QVERIFY(kinds.contains(QStringLiteral("unreadable-metadata")));
        QCOMPARE(confidenceId(Confidence::Likely), QStringLiteral("likely"));
        QCOMPARE(instanceLoaderId(InstanceLoader::NeoForge), QStringLiteral("neoforge"));
        QCOMPARE(loaderFormatId(LoaderFormat::LegacyForge), QStringLiteral("legacy-forge"));
    }
};

QTEST_GUILESS_MAIN(ModHealthTest)

#include "ModHealth_test.moc"
