// SPDX-License-Identifier: GPL-3.0-only
// Crash Detective: every rule against realistic log excerpts, plus negatives that must not raise a Critical finding.
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "zero/diagnostics/CrashDetective.h"
#include "zero/diagnostics/CrashReportCollector.h"

using namespace Zero::Diagnostics;

class CrashDetectiveTest : public QObject {
    Q_OBJECT

    static LogSource load(const QString& file, const QString& name = {})
    {
        const QString path = QFINDTESTDATA("testdata/CrashDetective/" + file);
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            qWarning() << "missing fixture" << file;
            return {};
        }
        LogSource s;
        s.name = name.isEmpty() ? file : name;
        s.path = path;
        s.text = QString::fromUtf8(f.readAll());
        return s;
    }

    static QList<Finding> run(const QString& file, const QString& name = {}, std::optional<int> exit = std::nullopt)
    {
        const LogSource s = load(file, name);
        return CrashDetective::analyze({ s }, exit);
    }

    static const Finding* find(const QList<Finding>& all, const QString& ruleId, const QString& titlePart = {})
    {
        for (const auto& f : all)
            if (f.ruleId == ruleId && (titlePart.isEmpty() || f.title.contains(titlePart)))
                return &f;
        return nullptr;
    }

    static bool hasCritical(const QList<Finding>& all)
    {
        for (const auto& f : all)
            if (f.severity == Severity::Critical)
                return true;
        return false;
    }

   private slots:
    void tableHasAllRules()
    {
        QCOMPARE(CrashDetective::ruleCount(), 13);
        QVERIFY(CrashDetective::ruleIds().contains("wrong-java"));
    }

    void wrongJavaClassVersion()
    {
        const auto r = run("wrong-java-class-version.log", "latest.log");
        const auto* f = find(r, "wrong-java");
        QVERIFY(f);
        QCOMPARE(f->confidence, Confidence::Confirmed);
        QCOMPARE(f->severity, Severity::Critical);
        QVERIFY2(f->title.contains("Java 21"), qPrintable(f->title));
        QVERIFY(f->explanation.contains("Java 17"));
        QVERIFY(f->actions.contains(Action::OpenInstanceSettings));
        QVERIFY(f->actions.contains(Action::OpenJavaDoctor));
        QVERIFY(!f->evidence.isEmpty());
        QCOMPARE(f->evidence.first().line, 3);
        QCOMPARE(f->evidence.first().source, QString("latest.log"));
    }

    void wrongJavaFabric()
    {
        const auto r = run("wrong-java-fabric.log");
        const auto* f = find(r, "wrong-java");
        QVERIFY(f);
        QVERIFY2(f->title.contains("Java 21"), qPrintable(f->title));
        QVERIFY(f->explanation.contains("Java 17"));
    }

    void outOfMemoryHeap()
    {
        const auto r = run("oom-heap.log");
        const auto* f = find(r, "out-of-memory");
        QVERIFY(f);
        QCOMPARE(f->confidence, Confidence::Confirmed);
        QVERIFY(f->title.contains("out of memory"));
        QCOMPARE(f->evidence.size(), 1);
    }

    void outOfMemoryReserve()
    {
        const auto r = run("oom-reserve.log");
        const auto* f = find(r, "out-of-memory", "reserve");
        QVERIFY(f);
        QVERIFY(f->explanation.contains("32-bit"));
    }

    void missingDependencyFabric()
    {
        const auto r = run("fabric-missing-dep.log");
        const auto* sodium = find(r, "missing-dependency", "Sodium");
        QVERIFY(sodium);
        QCOMPARE(sodium->confidence, Confidence::Confirmed);
        QVERIFY(sodium->explanation.contains("Sodium Extra"));
        QVERIFY(sodium->explanation.contains("Iris Shaders"));
        QVERIFY(sodium->explanation.contains("0.5.0 or later"));
        QCOMPARE(sodium->evidence.size(), 2);
        const auto* api = find(r, "missing-dependency", "Fabric API");
        QVERIFY(api);
        QVERIFY(api->explanation.contains("Mod Menu"));
    }

    void missingDependencyForge()
    {
        const auto r = run("forge-missing-dep.log");
        const auto* gecko = find(r, "missing-dependency", "geckolib");
        QVERIFY(gecko);
        QVERIFY(gecko->explanation.contains("alexsmobs"));
        QVERIFY2(gecko->explanation.contains("4.2.1 or newer"), qPrintable(gecko->explanation));
        QVERIFY(find(r, "missing-dependency", "curios"));
    }

    void incompatibleMinecraftVersion()
    {
        const auto r = run("fabric-wrong-minecraft.log");
        const auto* f = find(r, "incompatible-version", "Minecraft");
        QVERIFY(f);
        QCOMPARE(f->confidence, Confidence::Confirmed);
        QVERIFY(f->explanation.contains("1.20.4"));
        QVERIFY(f->explanation.contains("Lithium"));
        QVERIFY(f->explanation.contains("Starlight"));
        QVERIFY(f->actions.contains(Action::OpenModsFolder));
    }

    void duplicateMods()
    {
        const auto r = run("duplicate-forge.log");
        const auto* f = find(r, "duplicate-mods");
        QVERIFY(f);
        QCOMPARE(f->confidence, Confidence::Confirmed);
        QVERIFY(f->explanation.contains("jei"));
        QVERIFY(f->explanation.contains("jei-1.20.1-forge-15.2.0.27.jar"));
    }

    void mixinFailure()
    {
        const auto r = run("mixin-failure.log");
        const auto* f = find(r, "mixin-failure");
        QVERIFY(f);
        QCOMPARE(f->confidence, Confidence::Likely);
        QVERIFY(f->title.contains("bettergrass"));
        // The earlier WARN about modernfix is not an error.
        QVERIFY(!find(r, "mixin-failure", "modernfix"));
    }

    void suspectedMods()
    {
        const auto r = run("crash-suspected-mods.txt", "crash-2024-03-02.txt");
        const auto* f = find(r, "suspected-mods", "Create");
        QVERIFY(f);
        QCOMPARE(f->confidence, Confidence::Likely);
        QCOMPARE(f->severity, Severity::Warning);
    }

    void mixinsInStacktrace()
    {
        const auto r = run("crash-mixins-in-stacktrace.txt");
        const auto* f = find(r, "suspected-mods", "entityculling");
        QVERIFY(f);
        QCOMPARE(f->confidence, Confidence::Possible);
        // "Suspected Mods: NONE" must not produce a suspect.
        QCOMPARE(r.size(), 1);
    }

    void graphicsDriverHsErr()
    {
        const auto r = run("hs_err_nvidia.log", "hs_err_pid11820.log");
        const auto* f = find(r, "graphics-driver");
        QVERIFY(f);
        QCOMPARE(f->confidence, Confidence::Likely);
        QVERIFY(f->title.contains("nvoglv64.dll"));
        QVERIFY(f->suggestions.join(" ").contains("driver"));
        QVERIFY(!find(r, "jvm-native-crash"));
    }

    void graphicsDriverGlfw()
    {
        const auto r = run("glfw-driver.log");
        const auto* f = find(r, "graphics-driver");
        QVERIFY(f);
        QCOMPARE(f->confidence, Confidence::Confirmed);
    }

    void tickingEntity()
    {
        const auto r = run("crash-ticking-entity.txt");
        const auto* f = find(r, "ticking-crash");
        QVERIFY(f);
        QCOMPARE(f->confidence, Confidence::Likely);
        QVERIFY(f->explanation.contains("alexsmobs:crocodile"));
        QVERIFY(f->explanation.contains("120.50"));
        QVERIFY(f->suggestions.join(" ").contains("backup"));
    }

    void configCorrupt()
    {
        const auto r = run("config-corrupt.log");
        const auto* f = find(r, "config-corrupt", "alexsmobs.toml");
        QVERIFY(f);
        QCOMPARE(f->confidence, Confidence::Confirmed);
        QCOMPARE(f->severity, Severity::Critical);
        QCOMPARE(r.size(), 1);
    }

    void wrongLoaderMissingModsToml()
    {
        const auto r = run("wrong-loader.log");
        const auto* f = find(r, "wrong-loader");
        QVERIFY(f);
        QCOMPARE(f->confidence, Confidence::Likely);
        QVERIFY(f->explanation.contains("sodium-fabric-0.5.8+mc1.20.1.jar"));
    }

    void wrongLoaderDependency()
    {
        const auto r = run("wrong-loader-dep.log");
        const auto* f = find(r, "wrong-loader");
        QVERIFY(f);
        QVERIFY(f->explanation.contains("someneomod"));
        // Not reported as an ordinary missing mod.
        QVERIFY(!find(r, "missing-dependency"));
    }

    void nativeCrashWithOverlay()
    {
        const auto r = run("hs_err_other.log", "hs_err_pid4412.log");
        const auto* f = find(r, "jvm-native-crash");
        QVERIFY(f);
        QCOMPARE(f->confidence, Confidence::Likely);
        QVERIFY(f->explanation.contains("gameoverlayrenderer64.dll"));
        QVERIFY(!find(r, "graphics-driver"));
    }

    void exitCodeWhenNothingElseExplains()
    {
        const auto r = run("clean-session.log", "latest.log", -1073741819);
        QCOMPARE(r.size(), 1);
        QCOMPARE(r.first().ruleId, QString("exit-code"));
        QCOMPARE(r.first().confidence, Confidence::Possible);
        QVERIFY(r.first().title.contains("0xC0000005"));
        QVERIFY(r.first().severity != Severity::Critical);
    }

    void exitCodeIgnoredWhenLogExplains()
    {
        const auto r = run("oom-heap.log", "latest.log", -1073741819);
        QVERIFY(!find(r, "exit-code"));
        QVERIFY(find(r, "out-of-memory"));
    }

    void unknownExitCodeSaysNothing()
    {
        QVERIFY(run("clean-session.log", "latest.log", 1).isEmpty());
        QVERIFY(run("clean-session.log", "latest.log", 0).isEmpty());
    }

    void cleanSessionHasNoFindings()
    {
        QVERIFY(run("clean-session.log", "latest.log").isEmpty());
    }

    void harmlessWarningsNeverCritical()
    {
        const auto r = run("harmless-warnings.log", "latest.log");
        QVERIFY2(!hasCritical(r), qPrintable(r.isEmpty() ? QString() : r.first().title));
        QVERIFY2(r.isEmpty(), qPrintable(r.isEmpty() ? QString() : r.first().title));
    }

    void sortedBySeverityThenConfidence()
    {
        // Combine a Possible warning with a Confirmed critical.
        LogSource a = load("crash-mixins-in-stacktrace.txt");
        LogSource b = load("oom-heap.log");
        const auto r = CrashDetective::analyze({ a, b });
        QVERIFY(r.size() >= 2);
        QCOMPARE(r.first().ruleId, QString("out-of-memory"));
        for (int i = 1; i < r.size(); ++i) {
            QVERIFY(r[i - 1].severity <= r[i].severity);
            if (r[i - 1].severity == r[i].severity)
                QVERIFY(r[i - 1].confidence <= r[i].confidence);
        }
    }

    void duplicateEvidenceIsMergedAndCapped()
    {
        // The same message in latest.log and in a crash report yields one finding.
        LogSource a = load("oom-heap.log", "latest.log");
        LogSource b = load("oom-heap.log", "crash-1.txt");
        const auto r = CrashDetective::analyze({ a, b });
        int count = 0;
        for (const auto& f : r)
            count += f.ruleId == "out-of-memory";
        QCOMPARE(count, 1);
        for (const auto& f : r)
            QVERIFY(f.evidence.size() <= 5);
    }

    void olderSourceIsFlagged()
    {
        LogSource a = load("oom-heap.log", "latest.log");
        a.older = true;
        const auto r = CrashDetective::analyze({ a });
        QVERIFY(find(r, "out-of-memory"));
        QVERIFY(find(r, "out-of-memory")->explanation.contains("earlier session"));
    }

    void collectorPicksFreshFiles()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir root(dir.path());
        QVERIFY(root.mkpath("logs"));
        QVERIFY(root.mkpath("crash-reports"));
        auto write = [&](const QString& rel, const QByteArray& data, const QDateTime& when) {
            QFile f(root.filePath(rel));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(data);
            f.setFileTime(when, QFileDevice::FileModificationTime);
            f.close();
        };
        const QDateTime launch = QDateTime::currentDateTime().addSecs(-600);
        write("logs/latest.log", "hello\nworld\n", launch.addSecs(300));
        write("crash-reports/crash-old.txt", "old", launch.addSecs(-3600));
        write("crash-reports/crash-new.txt", "new", launch.addSecs(200));
        write("hs_err_pid1.log", "hs", launch.addSecs(-7200));

        const auto sources = CrashReportCollector::collectFromGameRoot(dir.path(), launch.toMSecsSinceEpoch());
        QCOMPARE(sources.size(), 3);
        QCOMPARE(sources[0].name, QString("latest.log"));
        QVERIFY(!sources[0].older);
        QCOMPARE(sources[1].name, QString("crash-new.txt"));
        QVERIFY(!sources[1].older);
        QCOMPARE(sources[2].name, QString("hs_err_pid1.log"));
        QVERIFY(sources[2].older);  // only an old one exists: returned, but flagged

        // Unknown launch time: nothing is flagged.
        const auto any = CrashReportCollector::collectFromGameRoot(dir.path(), 0);
        for (const auto& s : any)
            QVERIFY(!s.older);
    }

    void collectorCapsLatestLogToItsTail()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir root(dir.path());
        QVERIFY(root.mkpath("logs"));
        QFile f(root.filePath("logs/latest.log"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        const QByteArray line(99, 'x');
        const qint64 total = CrashReportCollector::kMaxLatestLogBytes + 100000;
        for (qint64 written = 0; written < total; written += 100)
            f.write(line + "\n");
        f.write("LAST-LINE\n");
        f.close();

        const auto sources = CrashReportCollector::collectFromGameRoot(dir.path(), 0);
        QCOMPARE(sources.size(), 1);
        QVERIFY(sources[0].truncated);
        QVERIFY(sources[0].text.size() <= CrashReportCollector::kMaxLatestLogBytes);
        QVERIFY(sources[0].text.trimmed().endsWith("LAST-LINE"));
        QVERIFY(sources[0].text.startsWith('x'));  // partial first line was dropped, the rest is whole lines
    }

    void collectorMissingFilesIsEmpty()
    {
        QTemporaryDir dir;
        QVERIFY(CrashReportCollector::collectFromGameRoot(dir.path(), 0).isEmpty());
    }
};

QTEST_GUILESS_MAIN(CrashDetectiveTest)

#include "CrashDetective_test.moc"
