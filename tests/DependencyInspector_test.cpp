// SPDX-License-Identifier: GPL-3.0-only
// Dependency Inspector (docs/zero/CONTENT-MODS.md): the pure core on real zip jars, its agreement with ModHealth::analyze, the parse
// cache and the scanner's generation token.
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <functional>
#include <vector>

#include "DependencyInspectorFixtures.h"
#include "archive/ArchiveWriter.h"
#include "zero/mods/DependencyInspector.h"
#include "zero/mods/DependencyScanner.h"
#include "zero/mods/ModHealth.h"
#include "zero/mods/ModMetadata.h"

using namespace Zero::Mods;

namespace {

QString S(const char* text)
{
    return QString::fromUtf8(text);
}

QByteArray fabricJson(const QString& id, const QString& name, const QString& version, const QString& extra = QString())
{
    return QStringLiteral(R"RAW({"schemaVersion":1,"id":"%1","name":"%2","version":"%3"%4})RAW")
        .arg(id, name, version.isEmpty() ? QStringLiteral("${version}") : version, extra.isEmpty() ? QString() : QLatin1Char(',') + extra)
        .toUtf8();
}

QByteArray quiltJson(const QString& id, const QString& version, const QString& extra = QString())
{
    return QStringLiteral(R"RAW({"schema_version":1,"quilt_loader":{"group":"g","id":"%1","version":"%2"%3}})RAW")
        .arg(id, version, extra.isEmpty() ? QString() : QLatin1Char(',') + extra)
        .toUtf8();
}

QByteArray readAll(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

InstanceContext fabricCtx(const QString& mc = S("1.21.1"))
{
    InstanceContext ctx;
    ctx.loader = InstanceLoader::Fabric;
    ctx.mcVersion = mc;
    ctx.loaderVersion = S("0.16.5");
    return ctx;
}

/// A folder of real jars.
struct Folder {
    QTemporaryDir dir;
    QString path(const QString& file) const { return dir.filePath(file); }

    bool jar(const QString& file, const QByteArray& metadataFile, const QByteArray& contents, const QList<QPair<QString, QByteArray>>& extra = {})
    {
        MMCZip::ArchiveWriter writer(path(file));
        if (!writer.open()) {
            return false;
        }
        if (!writer.addFile(metadataFile.isEmpty() ? QStringLiteral("fabric.mod.json") : QString::fromUtf8(metadataFile), contents)) {
            return false;
        }
        for (const auto& e : extra) {
            if (!writer.addFile(e.first, e.second)) {
                return false;
            }
        }
        return bool(writer.close());
    }
    bool fabric(const QString& file, const QString& id, const QString& name, const QString& version, const QString& extra = QString(),
                const QList<QPair<QString, QByteArray>>& more = {})
    {
        return jar(file, "fabric.mod.json", fabricJson(id, name, version, extra), more);
    }
    bool broken(const QString& file)
    {
        QFile f(path(file));
        return f.open(QIODevice::WriteOnly) && f.write("not a zip at all") > 0;
    }

    QList<ModMetadata> parse(const QStringList& files) const
    {
        QList<ModMetadata> out;
        for (const QString& f : files) {
            out.append(parseJar(path(f)));
        }
        return out;
    }
    Inspection inspect(const QStringList& enabled, const QStringList& disabled = {}, const InstanceContext& ctx = fabricCtx()) const
    {
        return Zero::Mods::inspect(parse(enabled), parse(disabled), ctx);
    }
};

const ModInspection& of(const Inspection& insp, const Folder& f, const QString& file)
{
    const ModInspection* mod = insp.find(f.path(file));
    if (!mod) {
        qFatal("no inspection of %s", qPrintable(file));
    }
    return *mod;
}

const DepEntry* need(const ModInspection& mod, const QString& id)
{
    for (const DepEntry& d : mod.needs) {
        if (d.id == id) {
            return &d;
        }
    }
    return nullptr;
}

QStringList names(const QList<NeededBy>& list)
{
    QStringList out;
    for (const NeededBy& n : list) {
        out.append(n.enabled ? n.name : n.name + S(" (disabled)"));
    }
    return out;
}

}  // namespace

class DependencyInspectorTest : public QObject {
    Q_OBJECT

   private slots:
    void installedWithName()
    {
        Folder f;
        QVERIFY(f.fabric(S("alpha.jar"), S("alpha"), S("Alpha"), S("1.0.0"), S(R"RAW("depends":{"beta":">=1.0.0"})RAW")));
        QVERIFY(f.fabric(S("beta.jar"), S("beta"), S("Beta Mod"), S("1.2.0")));
        const Inspection insp = f.inspect({ S("alpha.jar"), S("beta.jar") });
        const ModInspection& alpha = of(insp, f, S("alpha.jar"));
        QVERIFY(alpha.readable);
        QVERIFY(alpha.loads);
        QCOMPARE(alpha.name, S("Alpha"));
        QCOMPARE(alpha.needs.size(), 1);
        const DepEntry& d = alpha.needs.first();
        QCOMPARE(d.id, S("beta"));
        QCOMPARE(d.name, S("Beta Mod"));  // the installed mod's name
        QCOMPARE(d.state, DepState::Installed);
        QCOMPARE(d.version, S("1.2.0"));
        QCOMPARE(d.fileName, S("beta.jar"));
        QCOMPARE(d.path, f.path(S("beta.jar")));
        QVERIFY(!d.predicate.isEmpty());  // a plain-language rule
        QVERIFY(!problemsOf(alpha).any());
        QCOMPARE(depStateId(d.state), S("installed"));
    }

    void anyVersionHasNoPredicateText()
    {
        Folder f;
        QVERIFY(f.fabric(S("alpha.jar"), S("alpha"), S("Alpha"), S("1.0.0"), S(R"RAW("depends":{"beta":"*"})RAW")));
        QVERIFY(f.fabric(S("beta.jar"), S("beta"), S("Beta"), S("0.0.1")));
        const Inspection insp = f.inspect({ S("alpha.jar"), S("beta.jar") });
        QCOMPARE(of(insp, f, S("alpha.jar")).needs.first().predicate, QString());
        QCOMPARE(of(insp, f, S("alpha.jar")).needs.first().state, DepState::Installed);
    }

    void bundledNestedAndProvides()
    {
        Folder f;
        // lib.jar sits inside host.jar and provides "lib-alias"; alpha needs both lib and lib-alias
        QVERIFY(f.fabric(S("lib.jar"), S("lib"), S("Lib"), S("2.0.0"), S(R"RAW("provides":["lib-alias"])RAW")));
        QVERIFY(f.fabric(S("host.jar"), S("host"), S("Host"), S("1.0.0"), S(R"RAW("jars":[{"file":"META-INF/jars/lib.jar"}])RAW"),
                         { { S("META-INF/jars/lib.jar"), readAll(f.path(S("lib.jar"))) } }));
        QVERIFY(f.fabric(S("alpha.jar"), S("alpha"), S("Alpha"), S("1.0.0"), S(R"RAW("depends":{"lib":">=1.0","lib-alias":"*"})RAW")));
        const Inspection insp = f.inspect({ S("alpha.jar"), S("host.jar") });
        const ModInspection& alpha = of(insp, f, S("alpha.jar"));
        QCOMPARE(alpha.needs.size(), 2);
        for (const QString id : { S("lib"), S("lib-alias") }) {
            const DepEntry* d = need(alpha, id);
            QVERIFY2(d, qPrintable(id));
            QCOMPARE(d->state, DepState::Bundled);
            QCOMPARE(d->fileName, S("host.jar"));  // "Inside host.jar"
            QCOMPARE(d->name, S("Lib"));
        }
        QVERIFY(!problemsOf(alpha).any());
        // a bundled copy is shipped by its carrier, not needed: host.jar is not "needed by" alpha
        QVERIFY(of(insp, f, S("host.jar")).neededBy.isEmpty());
    }

    void topLevelProviderBeatsBundledCopy()
    {
        Folder f;
        QVERIFY(f.fabric(S("lib.jar"), S("lib"), S("Lib"), S("2.0.0")));
        QVERIFY(f.fabric(S("host.jar"), S("host"), S("Host"), S("1.0.0"), S(R"RAW("jars":[{"file":"META-INF/jars/lib.jar"}])RAW"),
                         { { S("META-INF/jars/lib.jar"), readAll(f.path(S("lib.jar"))) } }));
        QVERIFY(f.fabric(S("alpha.jar"), S("alpha"), S("Alpha"), S("1.0.0"), S(R"RAW("depends":{"lib":"*"})RAW")));
        const Inspection insp = f.inspect({ S("alpha.jar"), S("host.jar"), S("lib.jar") });
        QCOMPARE(need(of(insp, f, S("alpha.jar")), S("lib"))->state, DepState::Installed);
        QCOMPARE(need(of(insp, f, S("alpha.jar")), S("lib"))->fileName, S("lib.jar"));
    }

    void disabledProvider()
    {
        Folder f;
        QVERIFY(f.fabric(S("alpha.jar"), S("alpha"), S("Alpha"), S("1.0.0"), S(R"RAW("depends":{"beta":"*"})RAW")));
        QVERIFY(f.fabric(S("beta.jar.disabled"), S("beta"), S("Beta"), S("1.0.0")));
        const Inspection insp = f.inspect({ S("alpha.jar") }, { S("beta.jar.disabled") });
        const DepEntry* d = need(of(insp, f, S("alpha.jar")), S("beta"));
        QVERIFY(d);
        QCOMPARE(d->state, DepState::Disabled);
        QCOMPARE(d->fileName, S("beta.jar.disabled"));
        QCOMPARE(d->path, f.path(S("beta.jar.disabled")));
        QCOMPARE(d->name, S("Beta"));
        const Problems p = problemsOf(of(insp, f, S("alpha.jar")));
        QCOMPARE(p.disabled, QStringList{ S("Beta") });
        QVERIFY(p.missing.isEmpty());
        // the disabled mod is needed by alpha (its own panel says so)
        const ModInspection& beta = of(insp, f, S("beta.jar.disabled"));
        QVERIFY(!beta.enabled);
        QCOMPARE(names(beta.neededBy), QStringList{ S("Alpha") });
    }

    void wrongVersion()
    {
        Folder f;
        QVERIFY(f.fabric(S("alpha.jar"), S("alpha"), S("Alpha"), S("1.0.0"), S(R"RAW("depends":{"beta":">=1.0.0"})RAW")));
        QVERIFY(f.fabric(S("beta.jar"), S("beta"), S("Beta"), S("0.5.1")));
        const Inspection insp = f.inspect({ S("alpha.jar"), S("beta.jar") });
        const DepEntry* d = need(of(insp, f, S("alpha.jar")), S("beta"));
        QVERIFY(d);
        QCOMPARE(d->state, DepState::WrongVersion);
        QCOMPARE(d->version, S("0.5.1"));  // "Has 0.5.1"
        QCOMPARE(problemsOf(of(insp, f, S("alpha.jar"))).wrongVersion, QStringList{ S("Beta") });
        // a wrong version is still "needed by": updating beta is how alpha gets fixed
        QCOMPARE(names(of(insp, f, S("beta.jar")).neededBy), QStringList{ S("Alpha") });
    }

    void missing()
    {
        Folder f;
        QVERIFY(f.fabric(S("alpha.jar"), S("alpha"), S("Alpha"), S("1.0.0"), S(R"RAW("depends":{"indium":"*","beta":"*"})RAW")));
        const Inspection insp = f.inspect({ S("alpha.jar") });
        const ModInspection& alpha = of(insp, f, S("alpha.jar"));
        QCOMPARE(need(alpha, S("indium"))->state, DepState::Missing);
        QCOMPARE(need(alpha, S("indium"))->name, S("indium"));  // the id, nothing is installed
        QVERIFY(need(alpha, S("indium"))->path.isEmpty());
        QCOMPARE(problemsOf(alpha).missing.size(), 2);
        QVERIFY(of(insp, f, S("alpha.jar")).neededBy.isEmpty());
    }

    void unknownVersions()
    {
        Folder f;
        // beta has no readable version (placeholder): the rule cannot be judged
        QVERIFY(f.fabric(S("alpha.jar"), S("alpha"), S("Alpha"), S("1.0.0"), S(R"RAW("depends":{"beta":">=1.0.0"})RAW")));
        QVERIFY(f.fabric(S("beta.jar"), S("beta"), S("Beta"), QString()));
        const Inspection insp = f.inspect({ S("alpha.jar"), S("beta.jar") });
        const DepEntry* d = need(of(insp, f, S("alpha.jar")), S("beta"));
        QVERIFY(d);
        QCOMPARE(d->state, DepState::Unknown);
        QCOMPARE(d->fileName, S("beta.jar"));
        QVERIFY(!problemsOf(of(insp, f, S("alpha.jar"))).any());  // "Can't tell" is never a pill
    }

    void anUnreadableJarMakesMissingUnknown()
    {
        Folder f;
        QVERIFY(f.fabric(S("alpha.jar"), S("alpha"), S("Alpha"), S("1.0.0"), S(R"RAW("depends":{"beta":"*"})RAW")));
        QVERIFY(f.broken(S("mystery.jar")));  // might provide beta: ModHealth says Possible, not Confirmed
        const Inspection insp = f.inspect({ S("alpha.jar"), S("mystery.jar") });
        QCOMPARE(need(of(insp, f, S("alpha.jar")), S("beta"))->state, DepState::Unknown);
        const ModInspection& mystery = of(insp, f, S("mystery.jar"));
        QVERIFY(!mystery.readable);
        QVERIFY(mystery.needs.isEmpty());
        QVERIFY(!problemsOf(mystery).any());
    }

    void gameEntries()
    {
        Folder f;
        QVERIFY(f.fabric(S("alpha.jar"), S("alpha"), S("Alpha"), S("1.0.0"),
                         S(R"RAW("depends":{"minecraft":">=1.20","fabricloader":">=0.15.0","java":">=21"})RAW")));
        QVERIFY(f.fabric(S("old.jar"), S("old"), S("Old"), S("1.0.0"), S(R"RAW("depends":{"minecraft":"1.20.1","fabricloader":">=0.17"})RAW")));
        const Inspection insp = f.inspect({ S("alpha.jar"), S("old.jar") });
        const ModInspection& alpha = of(insp, f, S("alpha.jar"));
        QVERIFY(alpha.needs.isEmpty());  // environment ids are not "needs"
        QCOMPARE(alpha.game.size(), 3);
        QCOMPARE(alpha.game.at(0).kind, GameKind::Minecraft);
        QCOMPARE(alpha.game.at(0).name, S("Minecraft"));
        QCOMPARE(alpha.game.at(0).state, GameState::Ok);
        QCOMPARE(alpha.game.at(0).version, S("1.21.1"));  // what the instance runs
        QCOMPARE(alpha.game.at(1).kind, GameKind::Loader);
        QCOMPARE(alpha.game.at(1).name, S("Fabric Loader"));
        QCOMPARE(alpha.game.at(1).state, GameState::Ok);
        QCOMPARE(alpha.game.at(1).version, S("0.16.5"));
        QCOMPARE(alpha.game.at(2).kind, GameKind::Java);
        QCOMPARE(alpha.game.at(2).state, GameState::Unknown);  // Java is not checked
        QVERIFY(!problemsOf(alpha).any());

        const ModInspection& old = of(insp, f, S("old.jar"));
        QCOMPARE(old.game.at(0).state, GameState::Mismatch);  // 1.20.1 only, the instance runs 1.21.1
        QCOMPARE(old.game.at(1).state, GameState::Mismatch);  // the loader is older than 0.17
        QVERIFY(!problemsOf(old).any());                      // game mismatches are not dependency pills (Prism shows "Not for 1.21.1")
    }

    void gameUnknownWithoutAContext()
    {
        Folder f;
        QVERIFY(f.fabric(S("alpha.jar"), S("alpha"), S("Alpha"), S("1.0.0"), S(R"RAW("depends":{"minecraft":">=1.20","fabricloader":">=0.15.0"})RAW")));
        const Inspection insp = f.inspect({ S("alpha.jar") }, {}, InstanceContext{});
        for (const GameEntry& g : of(insp, f, S("alpha.jar")).game) {
            QCOMPARE(g.state, GameState::Unknown);  // never a claim without a version
        }
        // a snapshot version cannot be compared
        const Inspection snap = f.inspect({ S("alpha.jar") }, {}, fabricCtx(S("23w13a")));
        QCOMPARE(of(snap, f, S("alpha.jar")).game.at(0).state, GameState::Unknown);
    }

    void optionalRecommends()
    {
        Folder f;
        QVERIFY(f.fabric(S("alpha.jar"), S("alpha"), S("Alpha"), S("1.0.0"), S(R"RAW("recommends":{"present":"*","absent":"*"})RAW")));
        QVERIFY(f.fabric(S("present.jar"), S("present"), S("Present"), S("1.0.0")));
        const Inspection insp = f.inspect({ S("alpha.jar"), S("present.jar") });
        const ModInspection& alpha = of(insp, f, S("alpha.jar"));
        QCOMPARE(alpha.optional.size(), 2);
        QCOMPARE(alpha.optional.at(0).id, S("absent"));  // by name
        QCOMPARE(alpha.optional.at(0).state, DepState::NotInstalled);
        QCOMPARE(alpha.optional.at(1).name, S("Present"));
        QCOMPARE(alpha.optional.at(1).id, S("present"));
        QCOMPARE(alpha.optional.at(1).state, DepState::Installed);
        QVERIFY(alpha.needs.isEmpty());
        QVERIFY(!problemsOf(alpha).any());
        // a disabled recommended mod counts as not installed
        const Inspection off = f.inspect({ S("alpha.jar") }, { S("present.jar") });
        QCOMPARE(of(off, f, S("alpha.jar")).optional.at(1).state, DepState::NotInstalled);
    }

    void conflictsInstalledOrNot()
    {
        Folder f;
        QVERIFY(f.fabric(S("alpha.jar"), S("alpha"), S("Alpha"), S("1.0.0"),
                         S(R"RAW("breaks":{"optifabric":"*"},"conflicts":{"other":"<2.0.0","later":">=3.0.0"})RAW")));
        QVERIFY(f.fabric(S("optifabric.jar"), S("optifabric"), S("OptiFabric"), S("1.14.0")));
        QVERIFY(f.fabric(S("other.jar"), S("other"), S("Other"), S("2.5.0")));  // outside "<2.0.0": no conflict
        const Inspection insp = f.inspect({ S("alpha.jar"), S("optifabric.jar"), S("other.jar") });
        const ModInspection& alpha = of(insp, f, S("alpha.jar"));
        // only the installed conflict is a row; the two that are not (one installed outside the declared range) are a caption
        QCOMPARE(alpha.conflicts.size(), 1);
        QCOMPARE(alpha.conflicts.at(0).id, S("optifabric"));
        QCOMPARE(alpha.conflicts.at(0).state, DepState::Installed);
        QCOMPARE(alpha.conflicts.at(0).name, S("OptiFabric"));
        QCOMPARE(alpha.otherConflicts, (QStringList{ S("later"), S("other") }));
        QCOMPARE(problemsOf(alpha).conflicts, QStringList{ S("OptiFabric") });

        // both panels name the pair: optifabric declares nothing but alpha broke it
        const ModInspection& opti = of(insp, f, S("optifabric.jar"));
        QCOMPARE(opti.conflicts.size(), 1);
        QCOMPARE(opti.conflicts.first().state, DepState::Installed);
        QCOMPARE(opti.conflicts.first().name, S("Alpha"));
        QCOMPARE(problemsOf(opti).conflicts, QStringList{ S("Alpha") });
        QVERIFY(!of(insp, f, S("other.jar")).conflicts.size());

        // only an enabled mod conflicts
        const Inspection off = f.inspect({ S("alpha.jar") }, { S("optifabric.jar") });
        QVERIFY(of(off, f, S("alpha.jar")).conflicts.isEmpty());
        QVERIFY(of(off, f, S("alpha.jar")).otherConflicts.contains(S("optifabric")));
        QVERIFY(!problemsOf(of(off, f, S("alpha.jar"))).any());
    }

    void conflictInsideTheSameJarIsIgnored()
    {
        Folder f;
        QVERIFY(f.fabric(S("inner.jar"), S("inner"), S("Inner"), S("1.0.0")));
        QVERIFY(f.fabric(S("outer.jar"), S("outer"), S("Outer"), S("1.0.0"),
                         S(R"RAW("breaks":{"inner":"*"},"jars":[{"file":"META-INF/jars/inner.jar"}])RAW"),
                         { { S("META-INF/jars/inner.jar"), readAll(f.path(S("inner.jar"))) } }));
        const Inspection insp = f.inspect({ S("outer.jar") });
        QVERIFY(of(insp, f, S("outer.jar")).conflicts.isEmpty());
        QCOMPARE(of(insp, f, S("outer.jar")).otherConflicts, QStringList{ S("inner") });
    }

    void neededBy()
    {
        Folder f;
        QVERIFY(f.fabric(S("sodium.jar"), S("sodium"), S("Sodium"), S("0.6.0")));
        QVERIFY(f.fabric(S("iris.jar"), S("iris"), S("Iris Shaders"), S("1.8.0"), S(R"RAW("depends":{"sodium":">=0.6.0"})RAW")));
        QVERIFY(f.fabric(S("indium.jar"), S("indium"), S("Indium"), S("1.0.0"), S(R"RAW("depends":{"sodium":"*"})RAW")));
        QVERIFY(f.fabric(S("off.jar.disabled"), S("off"), S("Off Mod"), S("1.0.0"), S(R"RAW("depends":{"sodium":"*"})RAW")));
        QVERIFY(f.fabric(S("lonely.jar"), S("lonely"), S("Lonely"), S("1.0.0")));
        const Inspection insp = f.inspect({ S("sodium.jar"), S("iris.jar"), S("indium.jar"), S("lonely.jar") }, { S("off.jar.disabled") });
        QCOMPARE(names(of(insp, f, S("sodium.jar")).neededBy), (QStringList{ S("Indium"), S("Iris Shaders"), S("Off Mod (disabled)") }));
        QVERIFY(of(insp, f, S("lonely.jar")).neededBy.isEmpty());
        QCOMPARE(of(insp, f, S("sodium.jar")).neededBy.first().path, f.path(S("indium.jar")));
        // a disabled mod's needs are listed, but they are never problems of an enabled row
        QCOMPARE(of(insp, f, S("off.jar.disabled")).needs.first().state, DepState::Installed);
    }

    void aModNeverNeedsItself()
    {
        Folder f;
        // two entries in one file would be a Forge thing; with Fabric the nested jar needs its host
        QVERIFY(f.fabric(S("inner.jar"), S("inner"), S("Inner"), S("1.0.0"), S(R"RAW("depends":{"host":"*"})RAW")));
        QVERIFY(f.fabric(S("host.jar"), S("host"), S("Host"), S("1.0.0"), S(R"RAW("jars":[{"file":"META-INF/jars/inner.jar"}])RAW"),
                         { { S("META-INF/jars/inner.jar"), readAll(f.path(S("inner.jar"))) } }));
        const Inspection insp = f.inspect({ S("host.jar") });
        QVERIFY(of(insp, f, S("host.jar")).needs.isEmpty());
        QVERIFY(of(insp, f, S("host.jar")).neededBy.isEmpty());
    }

    void bundledJarsOnlyAddTheirProblems()
    {
        Folder f;
        QVERIFY(f.fabric(S("lib.jar"), S("lib"), S("Lib Base"), S("1.0.0"),
                         S(R"RAW("depends":{"fabricloader":">=0.15.0","sodium":"*","missinglib":"*","java":">=17","minecraft":"1.20.1"},"recommends":{"nice":"*"})RAW")));
        QVERIFY(f.fabric(S("host.jar"), S("host"), S("Host"), S("1.0.0"),
                         S(R"RAW("depends":{"fabricloader":">=0.12.3","sodium":"*","minecraft":">=1.19.4-"},"jars":[{"file":"META-INF/jars/lib.jar"}])RAW"),
                         { { S("META-INF/jars/lib.jar"), readAll(f.path(S("lib.jar"))) } }));
        QVERIFY(f.fabric(S("sodium.jar"), S("sodium"), S("Sodium"), S("0.6.0")));
        const QList<ModMetadata> en = f.parse({ S("host.jar"), S("sodium.jar") });
        const Inspection insp = inspect(en, {}, fabricCtx());
        const ModInspection& host = of(insp, f, S("host.jar"));
        // Needs: its own Sodium (fine) and the bundled library's problem, labelled; not the library's fine entries
        QCOMPARE(host.needs.size(), 2);
        QVERIFY(need(host, S("sodium")));
        QVERIFY(need(host, S("sodium"))->forBundled.isEmpty());
        QCOMPARE(need(host, S("missinglib"))->state, DepState::Missing);
        QCOMPARE(need(host, S("missinglib"))->forBundled, S("Lib Base"));
        // Game: one row per id with the top-level predicate; the bundled jar's Java / Minecraft rules are not listed
        QCOMPARE(host.game.size(), 2);
        QCOMPARE(host.game.at(0).id, S("minecraft"));
        QCOMPARE(host.game.at(0).state, GameState::Ok);  // ">=1.19.4-" holds on 1.21.1
        QCOMPARE(host.game.at(0).predicate, S("1.19.4 or newer"));
        QCOMPARE(host.game.at(1).id, S("fabricloader"));
        QCOMPARE(host.game.at(1).predicate, S("0.12.3 or newer"));
        QVERIFY(host.optional.isEmpty());  // nothing the bundled jar recommends
        // the row pills still count the bundled problem, as Mod Health does
        QCOMPARE(problemsOf(host).missing, QStringList{ S("missinglib") });
        bool found = false;
        for (const Finding& finding : analyze(en, fabricCtx(), {}, {}).findings) {
            found = found || (finding.kind == FindingKind::MissingDependency && finding.confidence == Confidence::Confirmed &&
                              finding.relatedId == S("missinglib") && finding.fileNames.contains(S("host.jar")));
        }
        QVERIFY(found);
    }

    void aBundledProblemWinsOverAFineTopLevelRowOfTheSameId()
    {
        Folder f;
        QVERIFY(f.fabric(S("lib.jar"), S("lib"), S("Lib"), S("1.0.0"), S(R"RAW("depends":{"sodium":">=9.0"})RAW")));
        QVERIFY(f.fabric(S("host.jar"), S("host"), S("Host"), S("1.0.0"), S(R"RAW("depends":{"sodium":"*"},"jars":[{"file":"META-INF/jars/lib.jar"}])RAW"),
                         { { S("META-INF/jars/lib.jar"), readAll(f.path(S("lib.jar"))) } }));
        QVERIFY(f.fabric(S("sodium.jar"), S("sodium"), S("Sodium"), S("0.6.0")));
        const Inspection insp = f.inspect({ S("host.jar"), S("sodium.jar") });
        const ModInspection& host = of(insp, f, S("host.jar"));
        QCOMPARE(host.needs.size(), 1);  // one row per id ...
        QCOMPARE(host.needs.first().state, DepState::WrongVersion);  // ... and the problem is what stays
        QCOMPARE(host.needs.first().forBundled, S("Lib"));
        QCOMPARE(problemsOf(host).wrongVersion, QStringList{ S("Sodium") });
    }

    void aLowestPreReleaseBoundIsEvaluatedAndShownPlain()
    {
        Folder f;
        QVERIFY(f.fabric(S("alpha.jar"), S("alpha"), S("Alpha"), S("1.0.0"), S(R"RAW("depends":{"minecraft":">=1.19.4-","beta":">=1.0.0-"})RAW")));
        QVERIFY(f.fabric(S("beta.jar"), S("beta"), S("Beta"), S("1.2.0")));
        const InstanceContext ctx = fabricCtx(S("1.21.1"));
        const QList<ModMetadata> en = f.parse({ S("alpha.jar"), S("beta.jar") });
        const Inspection insp = inspect(en, {}, ctx);
        const ModInspection& alpha = of(insp, f, S("alpha.jar"));
        QCOMPARE(alpha.game.first().state, GameState::Ok);
        QCOMPARE(alpha.game.first().predicate, S("1.19.4 or newer"));
        QCOMPARE(need(alpha, S("beta"))->state, DepState::Installed);
        QCOMPARE(need(alpha, S("beta"))->predicate, S("1.0.0 or newer"));
        QVERIFY(analyze(en, ctx, {}, {}).isClean());  // Mod Health agrees
        // an older game is a mismatch, not "can't tell"
        const Inspection old = inspect(en, {}, fabricCtx(S("1.19.2")));
        QCOMPARE(of(old, f, S("alpha.jar")).game.first().state, GameState::Mismatch);
    }

    void quiltUnlessWaivesTheRequirement()
    {
        Folder f;
        QVERIFY(f.jar(S("q.jar"), "quilt.mod.json",
                      quiltJson(S("qmod"), S("1.0.0"),
                                S(R"RAW("depends":[{"id":"waived","versions":"*","unless":"fixer"},{"id":"needed","versions":"*"}],"breaks":[{"id":"badmod","versions":"*","unless":"fixer"}])RAW"))));
        QVERIFY(f.jar(S("bad.jar"), "quilt.mod.json", quiltJson(S("badmod"), S("1.0.0"))));
        QVERIFY(f.jar(S("fixer.jar"), "quilt.mod.json", quiltJson(S("fixer"), S("1.0.0"))));
        InstanceContext ctx;
        ctx.loader = InstanceLoader::Quilt;
        ctx.mcVersion = S("1.21.1");
        ctx.loaderVersion = S("0.27.0");

        const Inspection without = f.inspect({ S("q.jar"), S("bad.jar") }, {}, ctx);
        const ModInspection& q = of(without, f, S("q.jar"));
        QCOMPARE(q.needs.size(), 2);  // "unless" does not hold: both apply
        QCOMPARE(need(q, S("waived"))->state, DepState::Missing);
        QCOMPARE(q.conflicts.first().state, DepState::Installed);

        const Inspection with = f.inspect({ S("q.jar"), S("bad.jar"), S("fixer.jar") }, {}, ctx);
        const ModInspection& qf = of(with, f, S("q.jar"));
        QCOMPARE(qf.needs.size(), 1);  // the fixer waives "waived"
        QCOMPARE(qf.needs.first().id, S("needed"));
        QVERIFY(qf.conflicts.isEmpty());  // and the break
    }

    void forgeSideAndOptional()
    {
        Folder f;
        const QByteArray& toml = kFmodToml;
        QVERIFY(f.jar(S("fmod.jar"), "META-INF/mods.toml", toml));
        const QByteArray& lib = kFlibToml;
        QVERIFY(f.jar(S("flib.jar"), "META-INF/mods.toml", lib));
        InstanceContext ctx;
        ctx.loader = InstanceLoader::Forge;
        ctx.mcVersion = S("1.20.1");
        ctx.loaderVersion = S("47.2.0");
        const Inspection insp = f.inspect({ S("fmod.jar"), S("flib.jar") }, {}, ctx);
        const ModInspection& fmod = of(insp, f, S("fmod.jar"));
        QVERIFY(!need(fmod, S("serveronly")));  // server-only: the launcher starts clients
        QCOMPARE(fmod.needs.size(), 1);
        QCOMPARE(need(fmod, S("flib"))->state, DepState::WrongVersion);  // [2,) vs 1.0.0
        QCOMPARE(need(fmod, S("flib"))->version, S("1.0.0"));
        QCOMPARE(fmod.game.size(), 2);
        QCOMPARE(fmod.game.at(0).id, S("minecraft"));  // Minecraft first, then the loader
        QCOMPARE(fmod.game.at(0).state, GameState::Ok);
        QCOMPARE(fmod.game.at(1).id, S("forge"));
        QCOMPARE(fmod.game.at(1).state, GameState::Ok);
        QCOMPARE(fmod.optional.size(), 1);
        QCOMPARE(fmod.optional.first().id, S("optionalmod"));
        QCOMPARE(fmod.optional.first().state, DepState::NotInstalled);
    }

    void aJarForAnotherLoaderIsFlaggedNotLoading()
    {
        Folder f;
        const QByteArray& toml = kForeignToml;
        QVERIFY(f.jar(S("fmod.jar"), "META-INF/mods.toml", toml));
        QVERIFY(f.fabric(S("alpha.jar"), S("alpha"), S("Alpha"), S("1.0.0"), S(R"RAW("depends":{"fmod":"*"})RAW")));
        const Inspection insp = f.inspect({ S("fmod.jar"), S("alpha.jar") });  // a Fabric instance
        const ModInspection& fmod = of(insp, f, S("fmod.jar"));
        QVERIFY(fmod.readable);
        QVERIFY(!fmod.loads);
        QVERIFY(!problemsOf(fmod).any());          // nothing it declares matters: it will not load
        QCOMPARE(need(fmod, S("flib"))->state, DepState::Missing);  // shown for information
        QCOMPARE(need(of(insp, f, S("alpha.jar")), S("fmod"))->state, DepState::Missing);  // it provides nothing here
        QVERIFY(fmod.neededBy.isEmpty());
    }

    void agreesWithModHealth()
    {
        Folder f;
        // Fabric: missing (confirmed), wrong version (confirmed), fine, disabled provider, wrong loader, optional, conflict
        QVERIFY(f.fabric(S("a.jar"), S("a"), S("A Mod"), S("1.0.0"), S(R"RAW("depends":{"gone":"*","low":">=2.0.0","ok":"*","off":"*"})RAW")));
        QVERIFY(f.fabric(S("b.jar"), S("b"), S("B Mod"), S("1.0.0"), S(R"RAW("depends":{"gone":">=1.0","a":"*","minecraft":"1.20.1"},"breaks":{"ok":"*"})RAW")));
        QVERIFY(f.fabric(S("low.jar"), S("low"), S("Low"), S("1.0.0")));
        QVERIFY(f.fabric(S("ok.jar"), S("ok"), S("Ok"), S("1.0.0")));
        QVERIFY(f.fabric(S("off.jar.disabled"), S("off"), S("Off"), S("1.0.0")));
        const QStringList enabled = { S("a.jar"), S("b.jar"), S("low.jar"), S("ok.jar") };
        const QStringList disabled = { S("off.jar.disabled") };
        const InstanceContext ctx = fabricCtx();

        const QList<ModMetadata> en = f.parse(enabled);
        const QList<ModMetadata> dis = f.parse(disabled);
        const Report report = analyze(en, ctx, disabled, dis);
        const Inspection insp = inspect(en, dis, ctx);

        int missing = 0;
        int versions = 0;
        for (const Finding& finding : report.findings) {
            if (finding.confidence != Confidence::Confirmed) {
                continue;
            }
            if (finding.kind == FindingKind::MissingDependency) {
                ++missing;
                QVERIFY(!finding.fileNames.isEmpty());
                for (const QString& file : finding.fileNames) {
                    const DepEntry* d = need(of(insp, f, file), finding.relatedId);
                    QVERIFY2(d, qPrintable(file + finding.relatedId));
                    // a provider that is only disabled is the one refinement: ModHealth says "enable it", the panel says Disabled
                    QCOMPARE(d->state, finding.relatedId == S("off") ? DepState::Disabled : DepState::Missing);
                }
            } else if (finding.kind == FindingKind::DependencyVersion) {
                ++versions;
                const QString declarer = finding.fileNames.first() == S("low.jar") ? finding.fileNames.last() : finding.fileNames.first();
                const DepEntry* d = need(of(insp, f, declarer), finding.relatedId);
                QVERIFY(d);
                QCOMPARE(d->state, DepState::WrongVersion);
            }
        }
        QCOMPARE(missing, 2);   // "gone" (needed by a and b: one finding) and "off" (only a disabled file provides it)
        QCOMPARE(versions, 1);  // "low"
        // ... and the other way round: everything the inspector calls Missing / WrongVersion for a loading mod is a Confirmed finding
        for (const ModInspection& mod : insp.mods) {
            if (!mod.enabled) {
                continue;
            }
            for (const DepEntry& d : mod.needs) {
                if (d.state != DepState::Missing && d.state != DepState::WrongVersion) {
                    continue;
                }
                bool found = false;
                for (const Finding& finding : report.findings) {
                    const bool kindOk = d.state == DepState::Missing ? finding.kind == FindingKind::MissingDependency
                                                                       : finding.kind == FindingKind::DependencyVersion;
                    found = found || (kindOk && finding.confidence == Confidence::Confirmed && finding.relatedId == d.id &&
                                      finding.fileNames.contains(mod.fileName));
                }
                QVERIFY2(found, qPrintable(mod.fileName + S(" ") + d.id));
            }
        }
        // the disabled provider is Disabled here and "Enable" in ModHealth's suggestion
        QCOMPARE(need(of(insp, f, S("a.jar")), S("off"))->state, DepState::Disabled);
        // the declared break is a Confirmed ModHealth finding and a conflict here
        bool breaks = false;
        for (const Finding& finding : report.findings) {
            breaks = breaks || (finding.kind == FindingKind::DeclaredIncompatibility && finding.confidence == Confidence::Confirmed);
        }
        QVERIFY(breaks);
        QCOMPARE(problemsOf(of(insp, f, S("b.jar"))).conflicts, QStringList{ S("Ok") });
        // and the game mismatch of b (made for 1.20.1) is a Confirmed MinecraftVersion finding
        QCOMPARE(of(insp, f, S("b.jar")).game.first().state, GameState::Mismatch);
        bool minecraft = false;
        for (const Finding& finding : report.findings) {
            minecraft = minecraft || (finding.kind == FindingKind::MinecraftVersion && finding.fileNames.contains(S("b.jar")));
        }
        QVERIFY(minecraft);
    }

    void agreesWithModHealthOnPossibleFindings()
    {
        Folder f;
        // a Possible finding (an unreadable jar may provide it) is never Missing here
        QVERIFY(f.fabric(S("a.jar"), S("a"), S("A"), S("1.0.0"), S(R"RAW("depends":{"gone":"*"})RAW")));
        QVERIFY(f.broken(S("mystery.jar")));
        const QList<ModMetadata> en = f.parse({ S("a.jar"), S("mystery.jar") });
        const Report report = analyze(en, fabricCtx(), {}, {});
        bool possible = false;
        for (const Finding& finding : report.findings) {
            possible = possible || (finding.kind == FindingKind::MissingDependency && finding.confidence == Confidence::Possible);
        }
        QVERIFY(possible);
        const Inspection insp = inspect(en, {}, fabricCtx());
        QVERIFY(need(of(insp, f, S("a.jar")), S("gone"))->state != DepState::Missing);
    }

    void problemsKeepTheOrderOfTheLists()
    {
        ModInspection mod;
        mod.needs = { { S("a"), S("A"), QString(), DepState::Missing, {}, {}, {} },
                      { S("b"), S("B"), QString(), DepState::Disabled, {}, {}, {} },
                      { S("c"), S("C"), QString(), DepState::Missing, {}, {}, {} },
                      { S("d"), S("D"), QString(), DepState::Unknown, {}, {}, {} },
                      { S("e"), S("E"), QString(), DepState::WrongVersion, {}, {}, {} } };
        mod.conflicts = { { S("x"), S("X"), QString(), DepState::Installed, {}, {}, {} }, { S("y"), S("Y"), QString(), DepState::NotInstalled, {}, {}, {} } };
        const Problems p = problemsOf(mod);
        QCOMPARE(p.missing, (QStringList{ S("A"), S("C") }));
        QCOMPARE(p.disabled, QStringList{ S("B") });
        QCOMPARE(p.wrongVersion, QStringList{ S("E") });
        QCOMPARE(p.conflicts, QStringList{ S("X") });
        mod.readable = false;
        QVERIFY(!problemsOf(mod).any());
        mod.readable = true;
        mod.loads = false;
        QVERIFY(!problemsOf(mod).any());
    }

    // ---- the parse cache and the scan -------------------------------------------------------------------------------------------

    void parseCacheParsesOnlyChangedFiles()
    {
        int calls = 0;
        ParseCache cache([&calls](const QString& path) {
            ++calls;
            ModMetadata m;
            m.path = path;
            m.fileName = QFileInfo(path).fileName();
            return m;
        });
        const auto first = cache.get(S("C:/m/a.jar"), S("10/100"));
        QCOMPARE(calls, 1);
        QCOMPARE(cache.get(S("C:/m/a.jar"), S("10/100")).get(), first.get());  // same stamp: the same object, no parse
        QCOMPARE(calls, 1);
        cache.get(S("C:/m/a.jar"), S("10/101"));  // another time
        QCOMPARE(calls, 2);
        cache.get(S("C:/m/a.jar"), S("11/101"));  // another size
        QCOMPARE(calls, 3);
        cache.get(S("C:/m/b.jar"), S("5/5"));
        QCOMPARE(calls, 4);
        QCOMPARE(cache.size(), 2);
        cache.get(S("C:/m/c.jar"), QString());  // no stamp: parsed every time, never kept
        cache.get(S("C:/m/c.jar"), QString());
        QCOMPARE(calls, 6);
        QCOMPARE(cache.size(), 2);
        cache.keepOnly({ S("C:/m/b.jar") });
        QCOMPARE(cache.size(), 1);
        QCOMPARE(cache.parseCount(), 6);
    }

    void scanFilesReadsOnlyWhatChanged()
    {
        Folder f;
        QVERIFY(f.fabric(S("alpha.jar"), S("alpha"), S("Alpha"), S("1.0.0"), S(R"RAW("depends":{"beta":"*"})RAW")));
        QVERIFY(f.fabric(S("beta.jar.disabled"), S("beta"), S("Beta"), S("1.0.0")));
        ParseCache cache;
        ScanRequest request;
        request.files = { { f.path(S("alpha.jar")), true }, { f.path(S("beta.jar.disabled")), false } };
        request.context = fabricCtx();
        const Inspection first = scanFiles(request, cache);
        QCOMPARE(cache.parseCount(), 2);
        QCOMPARE(first.mods.size(), 2);
        QCOMPARE(of(first, f, S("alpha.jar")).needs.first().state, DepState::Disabled);
        QVERIFY(!of(first, f, S("alpha.jar")).stamp.isEmpty());
        QCOMPARE(of(first, f, S("alpha.jar")).stamp, fileStampOf(f.path(S("alpha.jar"))));

        scanFiles(request, cache);
        QCOMPARE(cache.parseCount(), 2);  // nothing changed: nothing was read again

        // alpha is rewritten (a new size): only alpha is parsed again, and the answer follows
        QVERIFY(f.fabric(S("alpha.jar"), S("alpha"), S("Alpha"), S("1.0.0"), S(R"RAW("depends":{"beta":"*","gamma":"*"})RAW")));
        const Inspection second = scanFiles(request, cache);
        QCOMPARE(cache.parseCount(), 3);
        QCOMPARE(of(second, f, S("alpha.jar")).needs.size(), 2);
        QCOMPARE(of(second, f, S("alpha.jar")).stamp, fileStampOf(f.path(S("alpha.jar"))));

        // a file that is gone is forgotten
        request.files.removeLast();
        scanFiles(request, cache);
        QCOMPARE(cache.size(), 1);
        QCOMPARE(fileStampOf(f.path(S("nothing.jar"))), QString());
    }

    void aSupersededScanIsNeverDelivered()
    {
        std::vector<ScanDone> pending;
        QList<ScanRequest> requests;
        DependencyScanner scanner([&](const ScanRequest& r, ScanDone done) {
            requests.append(r);
            pending.push_back(std::move(done));
        });
        QSignalSpy spy(&scanner, &DependencyScanner::finished);
        const auto answer = [](const QString& marker) {
            Inspection i;
            ModInspection m;
            m.path = marker;
            i.mods.append(m);
            i.byPath.insert(marker, 0);
            return i;
        };

        scanner.start({});
        scanner.start({});
        QCOMPARE(pending.size(), size_t(2));
        QVERIFY(scanner.isScanning());
        // the newer answer arrives first, the older one after it: only the newer is delivered
        pending[1](answer(S("new")));
        QTRY_COMPARE(spy.count(), 1);
        pending[0](answer(S("old")));
        QTest::qWait(60);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().value<Inspection>().mods.first().path, S("new"));
        QVERIFY(!scanner.isScanning());

        // the older answer arrives first
        spy.clear();
        pending.clear();
        scanner.start({});
        scanner.start({});
        pending[0](answer(S("old")));
        QTest::qWait(60);
        QCOMPARE(spy.count(), 0);
        QVERIFY(scanner.isScanning());
        pending[1](answer(S("new")));
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().value<Inspection>().mods.first().path, S("new"));

        // cancel() drops what is running
        spy.clear();
        pending.clear();
        scanner.start({});
        scanner.cancel();
        pending[0](answer(S("late")));
        QTest::qWait(60);
        QCOMPARE(spy.count(), 0);
        QVERIFY(!scanner.isScanning());
    }

    void theDefaultRunnerScansOnThePool()
    {
        Folder f;
        QVERIFY(f.fabric(S("alpha.jar"), S("alpha"), S("Alpha"), S("1.0.0"), S(R"RAW("depends":{"beta":"*"})RAW")));
        DependencyScanner scanner;
        QSignalSpy spy(&scanner, &DependencyScanner::finished);
        ScanRequest request;
        request.files = { { f.path(S("alpha.jar")), true } };
        request.context = fabricCtx();
        scanner.start(request);
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 10000);
        const Inspection insp = spy.first().first().value<Inspection>();
        QCOMPARE(of(insp, f, S("alpha.jar")).needs.first().state, DepState::Missing);
        QVERIFY(scanner.cache());
        QCOMPARE(scanner.cache()->parseCount(), 1);
        // a second scan of the same file reads nothing again
        spy.clear();
        scanner.start(request);
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 10000);
        QCOMPARE(scanner.cache()->parseCount(), 1);
    }
};

QTEST_MAIN(DependencyInspectorTest)

#include "DependencyInspector_test.moc"
