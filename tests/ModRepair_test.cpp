// SPDX-License-Identifier: GPL-3.0-only
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include "archive/ArchiveWriter.h"
#include "zero/mods/ModHealth.h"
#include "zero/mods/ModHealthScanner.h"
#include "zero/mods/ModRepair.h"

using namespace Zero::Mods;

namespace {

ModMetadata jarOf(const QString& file, LoaderFormat format, const QString& id, const QString& version)
{
    ModMetadata j;
    j.fileName = file;
    j.path = QStringLiteral("C:/mods/") + file;
    ModEntry e;
    e.format = format;
    e.id = id;
    e.version = version;
    j.mods = { e };
    return j;
}

Finding finding(FindingKind kind, Confidence conf, const QStringList& ids, const QStringList& files)
{
    Finding f;
    f.kind = kind;
    f.confidence = conf;
    f.modIds = ids;
    f.fileNames = files;
    f.title = QStringLiteral("Something is wrong with %1").arg(files.join(QLatin1Char(',')));
    return f;
}

QStringList names(const QList<RepairAction>& actions)
{
    QStringList out;
    for (const RepairAction& a : actions)
        out.append(a.fileName);
    return out;
}

bool touch(const QString& path, const QByteArray& content = "x")
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(content);
    return true;
}

bool writeJar(const QString& path, const QByteArray& fabricModJson)
{
    MMCZip::ArchiveWriter w(path);
    if (!w.open())
        return false;
    if (!w.addFile(QStringLiteral("fabric.mod.json"), fabricModJson))
        return false;
    return bool(w.close());
}

QByteArray fabricJson(const QString& id, const QString& version, const QString& extra = QString())
{
    return QStringLiteral(R"RAW({"schemaVersion":1,"id":"%1","version":"%2"%3})RAW")
        .arg(id, version, extra.isEmpty() ? QString() : QLatin1Char(',') + extra)
        .toUtf8();
}

}  // namespace

class ModRepairTest : public QObject {
    Q_OBJECT
   private slots:
    void singleFileRulesByKind_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<int>("confidence");
        QTest::addColumn<int>("expected");
        const auto C = [](Confidence c) { return int(c); };
        const auto K = [](FindingKind k) { return int(k); };
        QTest::newRow("wrong-loader confirmed") << K(FindingKind::WrongLoader) << C(Confidence::Confirmed) << 1;
        QTest::newRow("wrong-loader likely") << K(FindingKind::WrongLoader) << C(Confidence::Likely) << 0;
        QTest::newRow("wrong-loader possible") << K(FindingKind::WrongLoader) << C(Confidence::Possible) << 0;
        QTest::newRow("minecraft confirmed") << K(FindingKind::MinecraftVersion) << C(Confidence::Confirmed) << 1;
        QTest::newRow("minecraft likely") << K(FindingKind::MinecraftVersion) << C(Confidence::Likely) << 0;
        QTest::newRow("loader-version confirmed") << K(FindingKind::LoaderVersion) << C(Confidence::Confirmed) << 1;
        QTest::newRow("loader-version unknown") << K(FindingKind::LoaderVersion) << C(Confidence::Unknown) << 0;
        QTest::newRow("missing confirmed") << K(FindingKind::MissingDependency) << C(Confidence::Confirmed) << 0;
        QTest::newRow("dependency-version confirmed") << K(FindingKind::DependencyVersion) << C(Confidence::Confirmed) << 0;
        QTest::newRow("incompatible confirmed") << K(FindingKind::DeclaredIncompatibility) << C(Confidence::Confirmed) << 0;
        QTest::newRow("unreadable") << K(FindingKind::UnreadableMetadata) << C(Confidence::Unknown) << 0;
    }

    void singleFileRulesByKind()
    {
        QFETCH(int, kind);
        QFETCH(int, confidence);
        QFETCH(int, expected);
        const QList<ModMetadata> jars = { jarOf("a.jar", LoaderFormat::Forge, "a", "1.0"), jarOf("b.jar", LoaderFormat::Fabric, "b", "1.0") };
        const Finding f = finding(FindingKind(kind), Confidence(confidence), { "a" }, { "a.jar" });
        const QList<RepairAction> actions = actionsFor(f, jars);
        QCOMPARE(actions.size(), expected);
        if (expected == 1) {
            QCOMPARE(actions.first().fileName, QStringLiteral("a.jar"));
            QCOMPARE(actions.first().kind, RepairAction::Kind::DisableJar);
            QCOMPARE(actions.first().reason, f.title);
        }
    }

    void singleFileNeedsOneEnabledTopLevelJar()
    {
        const QList<ModMetadata> jars = { jarOf("a.jar", LoaderFormat::Forge, "a", "1.0"), jarOf("b.jar", LoaderFormat::Forge, "b", "1.0") };
        // two files: ambiguous
        QVERIFY(actionsFor(finding(FindingKind::WrongLoader, Confidence::Confirmed, { "a" }, { "a.jar", "b.jar" }), jars).isEmpty());
        // no file
        QVERIFY(actionsFor(finding(FindingKind::WrongLoader, Confidence::Confirmed, { "a" }, {}), jars).isEmpty());
        // a mod that is only bundled inside another jar is not an enabled top-level jar
        QVERIFY(actionsFor(finding(FindingKind::MinecraftVersion, Confidence::Confirmed, { "nested" }, { "nested-lib.jar" }), jars).isEmpty());
        // not a jar name
        const QList<ModMetadata> odd = { jarOf("a.zip", LoaderFormat::Forge, "a", "1.0") };
        QVERIFY(actionsFor(finding(FindingKind::WrongLoader, Confidence::Confirmed, { "a" }, { "a.zip" }), odd).isEmpty());
    }

    void duplicateKeepsNewest()
    {
        const QList<ModMetadata> jars = { jarOf("m-1.0.jar", LoaderFormat::Fabric, "m", "1.0.0"), jarOf("m-1.2.jar", LoaderFormat::Fabric, "m", "1.2.0"),
                                          jarOf("m-1.1.jar", LoaderFormat::Fabric, "m", "1.1.0") };
        const Finding f = finding(FindingKind::DuplicateMod, Confidence::Confirmed, { "m" }, { "m-1.0.jar", "m-1.1.jar", "m-1.2.jar" });
        const QList<RepairAction> actions = actionsFor(f, jars);
        QCOMPARE(names(actions), (QStringList{ "m-1.0.jar", "m-1.1.jar" }));
        QVERIFY(actions.first().reason.contains(QLatin1String("m-1.2.jar")));
    }

    void duplicateGivesNothingWhenUnsure()
    {
        // tie for the highest version
        QList<ModMetadata> jars = { jarOf("a.jar", LoaderFormat::Fabric, "m", "1.2.0"), jarOf("b.jar", LoaderFormat::Fabric, "m", "1.2.0"),
                                    jarOf("c.jar", LoaderFormat::Fabric, "m", "1.0.0") };
        Finding f = finding(FindingKind::DuplicateMod, Confidence::Confirmed, { "m" }, { "a.jar", "b.jar", "c.jar" });
        QVERIFY(actionsFor(f, jars).isEmpty());
        // all equal
        jars = { jarOf("a.jar", LoaderFormat::Fabric, "m", "1.0.0"), jarOf("b.jar", LoaderFormat::Fabric, "m", "1.0.0") };
        f = finding(FindingKind::DuplicateMod, Confidence::Confirmed, { "m" }, { "a.jar", "b.jar" });
        QVERIFY(actionsFor(f, jars).isEmpty());
        // a version that cannot be compared
        jars = { jarOf("a.jar", LoaderFormat::Fabric, "m", "1.0.0"), jarOf("b.jar", LoaderFormat::Fabric, "m", "23w13a") };
        QVERIFY(actionsFor(f, jars).isEmpty());
        // an unknown (empty) version
        jars = { jarOf("a.jar", LoaderFormat::Fabric, "m", "1.0.0"), jarOf("b.jar", LoaderFormat::Fabric, "m", QString()) };
        QVERIFY(actionsFor(f, jars).isEmpty());
        // Likely (legacy forge) is not Confirmed
        jars = { jarOf("a.jar", LoaderFormat::LegacyForge, "m", "1.0.0"), jarOf("b.jar", LoaderFormat::LegacyForge, "m", "2.0.0") };
        f = finding(FindingKind::DuplicateMod, Confidence::Likely, { "m" }, { "a.jar", "b.jar" });
        QVERIFY(actionsFor(f, jars).isEmpty());
        // one of the files is not an enabled top-level jar
        jars = { jarOf("a.jar", LoaderFormat::Fabric, "m", "1.0.0") };
        f = finding(FindingKind::DuplicateMod, Confidence::Confirmed, { "m" }, { "a.jar", "b.jar" });
        QVERIFY(actionsFor(f, jars).isEmpty());
        // one jar declares two versions of the id: ambiguous
        ModMetadata two = jarOf("a.jar", LoaderFormat::Fabric, "m", "1.0.0");
        ModEntry second = two.mods.first();
        second.format = LoaderFormat::Forge;
        second.version = "3.0.0";
        two.mods.append(second);
        jars = { two, jarOf("b.jar", LoaderFormat::Fabric, "m", "2.0.0") };
        QVERIFY(actionsFor(f, jars).isEmpty());
    }

    void disableEnableRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(touch(dir.filePath("a.jar"), "payload"));

        const RepairResult off = disableJar(dir.path(), QStringLiteral("a.jar"));
        QVERIFY2(off.ok, qPrintable(off.error));
        QCOMPARE(off.newFileName, QStringLiteral("a.jar.disabled"));
        QVERIFY(!QFileInfo::exists(dir.filePath("a.jar")));
        QVERIFY(QFileInfo::exists(dir.filePath("a.jar.disabled")));

        const RepairResult on = enableJar(dir.path(), QStringLiteral("a.jar.disabled"));
        QVERIFY2(on.ok, qPrintable(on.error));
        QCOMPARE(on.newFileName, QStringLiteral("a.jar"));
        QVERIFY(QFileInfo::exists(dir.filePath("a.jar")));
        QVERIFY(!QFileInfo::exists(dir.filePath("a.jar.disabled")));
        QFile f(dir.filePath("a.jar"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("payload"));  // content untouched

        // a plain ".disabled" suffix works too
        QVERIFY(touch(dir.filePath("b.jar.DISABLED")));
        QVERIFY(enableJar(dir.path(), QStringLiteral("b.jar.DISABLED")).ok);
        QVERIFY(QFileInfo::exists(dir.filePath("b.jar")));
    }

    void neverOverwrites()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(touch(dir.filePath("a.jar"), "new"));
        QVERIFY(touch(dir.filePath("a.jar.disabled"), "old"));
        RepairResult r = disableJar(dir.path(), QStringLiteral("a.jar"));
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QLatin1String("a.jar.disabled already exists")));
        QVERIFY(r.newFileName.isEmpty());
        QVERIFY(QFileInfo::exists(dir.filePath("a.jar")));
        QFile disabled(dir.filePath("a.jar.disabled"));
        QVERIFY(disabled.open(QIODevice::ReadOnly));
        QCOMPARE(disabled.readAll(), QByteArray("old"));
        disabled.close();

        r = enableJar(dir.path(), QStringLiteral("a.jar.disabled"));
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QLatin1String("a.jar already exists")));
        QVERIFY(QFileInfo::exists(dir.filePath("a.jar.disabled")));
        QFile jar(dir.filePath("a.jar"));
        QVERIFY(jar.open(QIODevice::ReadOnly));
        QCOMPARE(jar.readAll(), QByteArray("new"));
    }

    void refusals()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir().mkpath(dir.filePath("mods/sub")));
        const QString mods = dir.filePath("mods");
        QVERIFY(touch(dir.filePath("mods/sub/inner.jar")));
        QVERIFY(touch(dir.filePath("outside.jar")));
        QVERIFY(touch(dir.filePath("mods/readme.txt")));
        QVERIFY(touch(dir.filePath("mods/x.jar.disabled")));
        QVERIFY(QDir().mkpath(dir.filePath("mods/folder.jar")));

        const QStringList bad = { "../outside.jar", "..\\outside.jar", "sub/inner.jar", "sub\\inner.jar", "..", ".", "", "C:/x.jar",
                                  "a..b.jar", dir.filePath("outside.jar") };
        for (const QString& name : bad) {
            const RepairResult r = disableJar(mods, name);
            QVERIFY2(!r.ok, qPrintable(name));
            QVERIFY2(!r.error.isEmpty(), qPrintable(name));
        }
        QVERIFY(QFileInfo::exists(dir.filePath("outside.jar")));
        QVERIFY(QFileInfo::exists(dir.filePath("mods/sub/inner.jar")));
        QVERIFY(!disableJar(mods, QStringLiteral("inner.jar")).ok);                 // only in a subfolder
        QVERIFY(!disableJar(mods, QStringLiteral("readme.txt")).ok);                // wrong suffix
        QVERIFY(!disableJar(mods, QStringLiteral("x.jar.disabled")).ok);            // already disabled
        QVERIFY(!disableJar(mods, QStringLiteral("missing.jar")).ok);               // not there
        QVERIFY(!disableJar(mods, QStringLiteral("folder.jar")).ok);                // a directory
        QVERIFY(!disableJar(dir.filePath("nope"), QStringLiteral("a.jar")).ok);     // no such mods folder
        QVERIFY(!enableJar(mods, QStringLiteral("readme.txt")).ok);                 // not disabled
        QVERIFY(!enableJar(mods, QStringLiteral(".disabled")).ok);                  // would give an empty name
        QVERIFY(!enableJar(mods, QStringLiteral("../x.jar.disabled")).ok);
        QVERIFY(QFileInfo::exists(dir.filePath("mods/readme.txt")));
        QVERIFY(QFileInfo::exists(dir.filePath("mods/x.jar.disabled")));
    }

    void refusesLinks()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(touch(dir.filePath("real.jar")));
        const QString link = dir.filePath("linked.jar");
        // QFile::link makes a real symlink on POSIX and a ".lnk" shortcut on Windows; only a genuine link name is checked
        if (!QFile::link(dir.filePath("real.jar"), link) || !QFileInfo(link).isSymLink())
            QSKIP("cannot create a symbolic link without privileges here");
        const RepairResult r = disableJar(dir.path(), QStringLiteral("linked.jar"));
        QVERIFY(!r.ok);
        QVERIFY(QFileInfo(link).isSymLink());
    }

    void batchWithOneFailure()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(touch(dir.filePath("a.jar")));
        QVERIFY(touch(dir.filePath("b.jar")));
        QVERIFY(touch(dir.filePath("b.jar.disabled")));  // b cannot be disabled
        QVERIFY(touch(dir.filePath("c.jar")));
        QList<RepairAction> actions;
        for (const char* n : { "a.jar", "b.jar", "gone.jar", "c.jar" }) {
            RepairAction a;
            a.fileName = QString::fromLatin1(n);
            actions.append(a);
        }
        const RepairBatchResult batch = applyAll(dir.path(), actions);
        QCOMPARE(batch.results.size(), 4);
        QVERIFY(!batch.allOk());
        QVERIFY(batch.firstError.contains(QLatin1String("b.jar.disabled already exists")));
        QCOMPARE(batch.succeeded, (QStringList{ "a.jar.disabled", "c.jar.disabled" }));
        QVERIFY(batch.results[0].ok);
        QVERIFY(!batch.results[1].ok);
        QVERIFY(!batch.results[2].ok);
        QVERIFY(batch.results[3].ok);
        // the renames that worked stay and are reversible
        QVERIFY(QFileInfo::exists(dir.filePath("a.jar.disabled")));
        QVERIFY(QFileInfo::exists(dir.filePath("b.jar")));
        QVERIFY(QFileInfo::exists(dir.filePath("c.jar.disabled")));
        for (const QString& n : batch.succeeded)
            QVERIFY(enableJar(dir.path(), n).ok);
        QVERIFY(QFileInfo::exists(dir.filePath("a.jar")));
        QVERIFY(QFileInfo::exists(dir.filePath("c.jar")));

        const RepairBatchResult allGood = applyAll(dir.path(), { actions[0], actions[3] });
        QVERIFY(allGood.allOk());
        QVERIFY(applyAll(dir.path(), {}).results.isEmpty());
        QVERIFY(!applyAll(dir.path(), {}).allOk());
    }

    void scannerAttachesActions()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QDir mods(dir.filePath("mods"));
        QVERIFY(QDir().mkpath(mods.path()));
        QVERIFY(writeJar(mods.filePath("m-1.0.jar"), fabricJson("m", "1.0.0")));
        QVERIFY(writeJar(mods.filePath("m-1.2.jar"), fabricJson("m", "1.2.0")));
        QVERIFY(writeJar(mods.filePath("old.jar"), fabricJson("old", "1.0.0", R"RAW("depends":{"minecraft":"~1.19"})RAW")));
        QVERIFY(writeJar(mods.filePath("needs.jar"), fabricJson("needs", "1.0.0", R"RAW("depends":{"nothere":"*"})RAW")));

        InstanceContext ctx;
        ctx.loader = InstanceLoader::Fabric;
        ctx.mcVersion = QStringLiteral("1.20.1");
        ctx.loaderVersion = QStringLiteral("0.15.11");
        const Report r = scanModsFolder(mods.path(), ctx);

        QStringList targets;
        int withActions = 0;
        for (const Finding& f : r.findings) {
            if (f.kind == FindingKind::MissingDependency)
                QVERIFY(f.actions.isEmpty());
            if (!f.actions.isEmpty())
                ++withActions;
            targets += names(f.actions);
        }
        targets.sort();
        QCOMPARE(targets, (QStringList{ "m-1.0.jar", "old.jar" }));
        QCOMPARE(withActions, 2);
        QCOMPARE(names(r.allActions()).size(), 2);

        // the whole flow: apply, scan again, the problems are gone, and the files are restorable
        const RepairBatchResult batch = applyAll(mods.path(), r.allActions());
        QVERIFY(batch.allOk());
        const Report after = scanModsFolder(mods.path(), ctx);
        QCOMPARE(ofKindCount(after), 0);
    }

   private:
    static int ofKindCount(const Report& r)
    {
        int n = 0;
        for (const Finding& f : r.findings)
            if (f.kind == FindingKind::DuplicateMod || f.kind == FindingKind::MinecraftVersion)
                ++n;
        return n;
    }

   private slots:
    void allActionsDeduplicates()
    {
        Report r;
        Finding a;
        RepairAction x;
        x.fileName = QStringLiteral("x.jar");
        RepairAction y;
        y.fileName = QStringLiteral("y.jar");
        a.actions = { x };
        Finding b;
        b.actions = { x, y };
        r.findings = { a, b };
        QCOMPARE(names(r.allActions()), (QStringList{ "x.jar", "y.jar" }));
    }
};

QTEST_GUILESS_MAIN(ModRepairTest)

#include "ModRepair_test.moc"
