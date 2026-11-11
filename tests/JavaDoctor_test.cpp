// SPDX-License-Identifier: GPL-3.0-only
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include "zero/java/JavaDoctor.h"

using namespace Zero::Java;

Q_DECLARE_METATYPE(Zero::Java::Status)
Q_DECLARE_METATYPE(Zero::Java::Reason)
Q_DECLARE_METATYPE(Zero::Java::Source)

namespace {

Runtime rt(const QString& path, const QString& version, int major, bool is64 = true, bool managed = false)
{
    Runtime r;
    r.path = path;
    r.versionString = version;
    r.major = major;
    r.is64bit = is64;
    r.managed = managed;
    return r;
}

const QString kJ21 = QStringLiteral("C:/java/jdk-21.0.2/bin/javaw.exe");
const QString kJ17 = QStringLiteral("C:/java/jdk-17.0.8/bin/javaw.exe");
const QString kJ17x86 = QStringLiteral("C:/java/jdk-17.0.9-x86/bin/javaw.exe");
const QString kJ17old = QStringLiteral("C:/java/jdk-17.0.2/bin/javaw.exe");
const QString kJ8 = QStringLiteral("C:/java/jre-8/bin/javaw.exe");
const QString kGone = QStringLiteral("C:/java/gone/bin/javaw.exe");
const QString kUnlisted = QStringLiteral("D:/elsewhere/javaw.exe");
const QString kPin17 = QStringLiteral("C:/pins/j17/javaw.exe");
const QString kPin21 = QStringLiteral("C:/pins/j21/javaw.exe");
const QString kManaged = QStringLiteral("C:/data/java/java-runtime-gamma/bin/javaw.exe");

/// In the order Prism lists them (highest first); automatic Java takes the first compatible one.
QList<Runtime> sampleRuntimes()
{
    return { rt(kJ21, "21.0.2", 21), rt(kJ17, "17.0.8", 17), rt(kJ17x86, "17.0.9", 17, false), rt(kJ17old, "17.0.2", 17),
             rt(kJ8, "1.8.0_382", 8) };
}

QList<int> parseMajors(const QString& text)
{
    QList<int> out;
    for (const QString& p : text.split(QLatin1Char(','), Qt::SkipEmptyParts))
        out.append(p.toInt());
    return out;
}

}  // namespace

class JavaDoctorTest : public QObject {
    Q_OBJECT

   private slots:
    void diagnose_data()
    {
        QTest::addColumn<bool>("autoSwitch");
        QTest::addColumn<bool>("autoDownload");
        QTest::addColumn<QString>("pinned");  // "<major>=<path>[!]" separated by ';' ('!' = does not exist)
        QTest::addColumn<QString>("majors");
        QTest::addColumn<bool>("overrideLoc");
        QTest::addColumn<QString>("javaPath");  // path ("" = none); existence from the next column
        QTest::addColumn<bool>("pathExists");
        QTest::addColumn<QString>("storedVersion");
        QTest::addColumn<bool>("ignore");
        QTest::addColumn<int>("memMb");
        QTest::addColumn<QString>("downloadName");
        QTest::addColumn<QString>("downloaded");
        QTest::addColumn<Status>("status");
        QTest::addColumn<Reason>("reason");
        QTest::addColumn<Source>("source");
        QTest::addColumn<QString>("effectivePath");  // "" = none
        QTest::addColumn<QString>("recommended");    // "" = none

        const QString none;
        // clang-format off
        QTest::newRow("automatic picks the first detected compatible runtime")
            << true << false << none << "17" << false << kJ8 << true << none << false << 2048 << none << none
            << Status::Ready << Reason::Ok << Source::AutomaticDetected << kJ17 << none;
        QTest::newRow("detected: first in list order wins")
            << true << false << none << "17,21" << false << kJ8 << true << none << false << 2048 << none << none
            << Status::Ready << Reason::Ok << Source::AutomaticDetected << kJ21 << none;
        QTest::newRow("pinned beats detected")
            << true << false << QString(QStringLiteral("17=") + kPin17) << "17,21" << false << kJ8 << true << none << false << 2048 << none << none
            << Status::Ready << Reason::Ok << Source::AutomaticPinned << kPin17 << none;
        QTest::newRow("pinned follows the order of the compatible majors")
            << true << false << QString(QStringLiteral("17=") + kPin17 + QStringLiteral(";21=") + kPin21) << "21,17" << false << kJ8 << true << none << false << 2048 << none << none
            << Status::Ready << Reason::Ok << Source::AutomaticPinned << kPin21 << none;
        QTest::newRow("pinned beats a download")
            << true << true << QString(QStringLiteral("17=") + kPin17) << "17" << false << kJ8 << true << none << false << 2048 << "java-runtime-gamma" << none
            << Status::Ready << Reason::Ok << Source::AutomaticPinned << kPin17 << none;
        QTest::newRow("pinned of an unneeded major is ignored")
            << true << false << QString(QStringLiteral("8=") + kPin17) << "17" << false << kJ8 << true << none << false << 2048 << none << none
            << Status::Ready << Reason::Ok << Source::AutomaticDetected << kJ17 << none;
        QTest::newRow("missing pin falls back to detected with a warning")
            << true << false << QString(QStringLiteral("17=") + kPin17 + QStringLiteral("!")) << "17" << false << kJ8 << true << none << false << 2048 << none << none
            << Status::Warning << Reason::PinnedPathMissing << Source::AutomaticDetected << kJ17 << none;
        QTest::newRow("override with an existing path skips automatic Java")
            << true << false << QString(QStringLiteral("17=") + kPin17) << "17" << true << kJ8 << true << none << false << 2048 << none << none
            << Status::WillFail << Reason::IncompatibleMajor << Source::Configured << kJ8 << kJ17;
        QTest::newRow("override with a missing path falls back to automatic Java")
            << true << false << none << "17" << true << kGone << false << none << false << 2048 << none << none
            << Status::Ready << Reason::Ok << Source::AutomaticDetected << kJ17 << none;
        QTest::newRow("override with a missing path, automatic Java off")
            << false << false << none << "17,21" << true << kGone << false << none << false << 2048 << none << none
            << Status::WillFail << Reason::ConfiguredPathMissing << Source::Configured << none << kJ21;
        QTest::newRow("global java does not resolve, automatic Java off")
            << false << false << none << "17" << false << none << false << none << false << 2048 << none << none
            << Status::WillFail << Reason::ConfiguredPathMissing << Source::Configured << none << kJ17;
        QTest::newRow("nothing compatible installed, no java configured")
            << true << false << none << "25" << false << none << false << none << false << 2048 << none << none
            << Status::WillFail << Reason::NoCompatibleInstalledDownloadOff << Source::Configured << none << none;
        QTest::newRow("nothing compatible installed, the user's java path is gone")
            << true << false << none << "25" << true << kGone << false << none << false << 2048 << none << none
            << Status::WillFail << Reason::ConfiguredPathMissing << Source::Configured << none << none;
        QTest::newRow("unknown requirement and no java configured: no verdict while automatic Java may still find one")
            << true << false << none << none << false << none << false << none << false << 2048 << none << none
            << Status::Unknown << Reason::RequirementUnknown << Source::Configured << none << none;
        QTest::newRow("unknown requirement, no java configured, automatic Java off")
            << false << false << none << none << false << none << false << none << false << 2048 << none << none
            << Status::WillFail << Reason::ConfiguredPathMissing << Source::Configured << none << none;
        QTest::newRow("compatibility check off turns a mismatch into a warning")
            << false << false << none << "17" << true << kJ8 << true << none << true << 2048 << none << none
            << Status::Warning << Reason::IncompatibleIgnored << Source::Configured << kJ8 << kJ17;
        QTest::newRow("mismatch without automatic Java fails")
            << false << false << none << "17" << true << kJ8 << true << none << false << 2048 << none << none
            << Status::WillFail << Reason::IncompatibleMajor << Source::Configured << kJ8 << kJ17;
        QTest::newRow("configured and compatible")
            << false << false << none << "17,21" << true << kJ21 << true << none << false << 2048 << none << none
            << Status::Ready << Reason::Ok << Source::Configured << kJ21 << none;
        QTest::newRow("32-bit java with 4096 MB warns and recommends a 64-bit one")
            << false << false << none << "17" << true << kJ17x86 << true << none << false << 4096 << none << none
            << Status::Warning << Reason::ThirtyTwoBitMemory << Source::Configured << kJ17x86 << kJ17;
        QTest::newRow("32-bit java with 2048 MB is fine")
            << false << false << none << "17" << true << kJ17x86 << true << none << false << 2048 << none << none
            << Status::Ready << Reason::Ok << Source::Configured << kJ17x86 << none;
        QTest::newRow("32-bit java with unknown requirement and lots of memory still warns")
            << false << false << none << none << true << kJ17x86 << true << none << false << 4096 << none << none
            << Status::Warning << Reason::ThirtyTwoBitMemory << Source::Configured << kJ17x86 << none;
        QTest::newRow("empty majors mean unknown requirement")
            << true << false << none << none << false << kJ8 << true << none << false << 2048 << none << none
            << Status::Unknown << Reason::RequirementUnknown << Source::Configured << kJ8 << none;
        QTest::newRow("empty majors never trigger automatic picking or downloads")
            << true << true << none << none << false << kJ8 << true << none << false << 2048 << "java-runtime-gamma" << none
            << Status::Unknown << Reason::RequirementUnknown << Source::Configured << kJ8 << none;
        QTest::newRow("nothing compatible installed and downloads off")
            << true << false << none << "25" << false << kJ8 << true << none << false << 2048 << none << none
            << Status::WillFail << Reason::NoCompatibleInstalledDownloadOff << Source::Configured << kJ8 << none;
        QTest::newRow("nothing compatible installed, downloads off, check off")
            << true << false << none << "25" << false << kJ8 << true << none << true << 2048 << none << none
            << Status::Warning << Reason::IncompatibleIgnored << Source::Configured << kJ8 << none;
        QTest::newRow("downloads on and the runtime is not there yet")
            << true << true << none << "17" << false << kJ8 << true << none << false << 2048 << "java-runtime-gamma" << none
            << Status::Ready << Reason::WillDownload << Source::AutomaticDownload << none << none;
        QTest::newRow("downloads on and the runtime is already downloaded")
            << true << true << none << "17" << false << kJ8 << true << none << false << 2048 << "java-runtime-gamma" << kManaged
            << Status::Ready << Reason::Ok << Source::AutomaticDownload << kManaged << none;
        QTest::newRow("downloads on but the version has no runtime name: configured java decides")
            << true << true << none << "17" << false << kJ8 << true << none << false << 2048 << none << none
            << Status::WillFail << Reason::IncompatibleMajor << Source::Configured << kJ8 << kJ17;
        QTest::newRow("automatic Java off: downloads are never planned")
            << false << true << none << "17" << false << kJ8 << true << none << false << 2048 << "java-runtime-gamma" << none
            << Status::WillFail << Reason::IncompatibleMajor << Source::Configured << kJ8 << kJ17;
        QTest::newRow("a java the detection does not list uses the stored version")
            << false << false << none << "17" << true << kUnlisted << true << "17.0.1" << false << 2048 << none << none
            << Status::Ready << Reason::Ok << Source::Configured << kUnlisted << none;
        QTest::newRow("a java the detection does not list, stored version incompatible")
            << false << false << none << "17" << true << kUnlisted << true << "1.8.0_100" << false << 2048 << none << none
            << Status::WillFail << Reason::IncompatibleMajor << Source::Configured << kUnlisted << kJ17;
        QTest::newRow("a java the detection does not list and never checked")
            << false << false << none << "17" << true << kUnlisted << true << none << false << 2048 << none << none
            << Status::Unknown << Reason::RuntimeUnknown << Source::Configured << kUnlisted << none;
        // clang-format on
    }

    void diagnose()
    {
        QFETCH(bool, autoSwitch);
        QFETCH(bool, autoDownload);
        QFETCH(QString, pinned);
        QFETCH(QString, majors);
        QFETCH(bool, overrideLoc);
        QFETCH(QString, javaPath);
        QFETCH(bool, pathExists);
        QFETCH(QString, storedVersion);
        QFETCH(bool, ignore);
        QFETCH(int, memMb);
        QFETCH(QString, downloadName);
        QFETCH(QString, downloaded);
        QFETCH(Status, status);
        QFETCH(Reason, reason);
        QFETCH(Source, source);
        QFETCH(QString, effectivePath);
        QFETCH(QString, recommended);

        GlobalJava g;
        g.autoSwitch = autoSwitch;
        g.autoDownload = autoDownload;
        for (const QString& entry : pinned.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            const int eq = entry.indexOf(QLatin1Char('='));
            const int major = entry.left(eq).toInt();
            QString path = entry.mid(eq + 1);
            const bool missing = path.endsWith(QLatin1Char('!'));
            if (missing)
                path.chop(1);
            g.pinned.insert(major, path);
            if (missing)
                g.pinnedMissing.insert(major);
        }

        InstanceJava inst;
        inst.id = QStringLiteral("test");
        inst.name = QStringLiteral("Test");
        inst.mcVersion = QStringLiteral("1.20.1");
        inst.compatibleMajors = parseMajors(majors);
        inst.javaDownloadName = downloadName;
        inst.downloadedJavaPath = downloaded;
        inst.overrideLocation = overrideLoc;
        inst.javaPath = javaPath;
        inst.javaPathExists = pathExists;
        inst.javaPathResolved = pathExists ? javaPath : QString();
        inst.storedVersion = storedVersion;
        inst.ignoreCompatibility = ignore;
        inst.maxMemMb = memMb;

        const Diagnosis d = Zero::Java::diagnose(inst, sampleRuntimes(), g);
        QCOMPARE(int(d.status), int(status));
        QCOMPARE(int(d.reason), int(reason));
        QCOMPARE(int(d.source), int(source));
        if (effectivePath.isEmpty()) {
            QVERIFY(!d.effective.has_value());
        } else {
            QVERIFY(d.effective.has_value());
            QCOMPARE(d.effective->path, effectivePath);
        }
        if (recommended.isEmpty()) {
            QVERIFY(!d.recommendation.has_value());
        } else {
            QVERIFY(d.recommendation.has_value());
            QCOMPARE(d.recommendation->path, recommended);
        }
        if (reason == Reason::WillDownload)
            QCOMPARE(d.downloadMajor, inst.compatibleMajors.first());
        if (reason == Reason::PinnedPathMissing)
            QCOMPARE(d.pinnedMissingMajor, 17);
    }

    void effectiveRuntimeDetails()
    {
        GlobalJava g;
        g.autoSwitch = true;
        InstanceJava inst;
        inst.compatibleMajors = { 17 };
        inst.maxMemMb = 4096;
        Diagnosis d = Zero::Java::diagnose(inst, sampleRuntimes(), g);
        QVERIFY(d.effective.has_value());
        QCOMPARE(d.effective->major, 17);
        QCOMPARE(d.effective->versionString, QStringLiteral("17.0.8"));
        QVERIFY(d.effective->is64bit);

        // a pinned path that the detection does not list is assumed to be the pinned major
        g.pinned.insert(17, kPin17);
        d = Zero::Java::diagnose(inst, sampleRuntimes(), g);
        QCOMPARE(d.source, Source::AutomaticPinned);
        QCOMPARE(d.effective->major, 17);
    }

    void pinnedPathMatchesADetectedRuntime()
    {
        GlobalJava g;
        g.autoSwitch = true;
        g.pinned.insert(8, kJ8.toUpper());  // same file, other spelling (Windows paths are case-insensitive)
        InstanceJava inst;
        inst.compatibleMajors = { 8 };
        const Diagnosis d = Zero::Java::diagnose(inst, sampleRuntimes(), g);
        QCOMPARE(d.source, Source::AutomaticPinned);
#ifdef Q_OS_WIN
        QCOMPARE(d.effective->versionString, QStringLiteral("1.8.0_382"));
#endif
    }

    void recommendationOrdering()
    {
        InstanceJava inst;
        inst.compatibleMajors = { 17 };
        inst.overrideLocation = true;
        inst.javaPath = kJ8;
        inst.javaPathExists = true;
        inst.javaPathResolved = kJ8;
        GlobalJava g;
        g.autoSwitch = true;

        // 64-bit beats a newer 32-bit one, then the highest version wins
        QList<Runtime> list = { rt(kJ17x86, "17.0.9", 17, false), rt(kJ17old, "17.0.2", 17), rt(kJ17, "17.0.8", 17), rt(kJ8, "1.8.0_382", 8) };
        Diagnosis d = Zero::Java::diagnose(inst, list, g);
        QCOMPARE(d.status, Status::WillFail);
        QVERIFY(d.recommendation.has_value());
        QCOMPARE(d.recommendation->path, kJ17);

        // only 32-bit and one older 64-bit: the 64-bit one still wins
        list = { rt(kJ17x86, "17.0.9", 17, false), rt(kJ17old, "17.0.2", 17), rt(kJ8, "1.8.0_382", 8) };
        d = Zero::Java::diagnose(inst, list, g);
        QCOMPARE(d.recommendation->path, kJ17old);

        // only 32-bit candidates: still better than nothing for an incompatible Java
        list = { rt(kJ17x86, "17.0.9", 17, false), rt(kJ8, "1.8.0_382", 8) };
        d = Zero::Java::diagnose(inst, list, g);
        QCOMPARE(d.recommendation->path, kJ17x86);

        // ...but never for the 32-bit memory warning, and never the Java that is already in use
        inst.javaPath = inst.javaPathResolved = kJ17x86;
        inst.maxMemMb = 4096;
        d = Zero::Java::diagnose(inst, list, g);
        QCOMPARE(d.reason, Reason::ThirtyTwoBitMemory);
        QVERIFY(!d.recommendation.has_value());

        // among equal majors a higher version number wins even when it is listed later
        list = { rt(kJ17old, "17.0.2", 17), rt(kJ17, "17.0.8", 17), rt(kJ8, "1.8.0_382", 8) };
        inst.javaPath = inst.javaPathResolved = kJ8;
        inst.maxMemMb = 2048;
        d = Zero::Java::diagnose(inst, list, g);
        QCOMPARE(d.recommendation->path, kJ17);

        // nothing compatible installed: no recommendation
        list = { rt(kJ8, "1.8.0_382", 8) };
        d = Zero::Java::diagnose(inst, list, g);
        QVERIFY(!d.recommendation.has_value());

        // a ready instance never gets one
        inst.javaPath = inst.javaPathResolved = kJ17;
        list = { rt(kJ17, "17.0.8", 17), rt(kJ21, "21.0.2", 21) };
        inst.compatibleMajors = { 17, 21 };
        d = Zero::Java::diagnose(inst, list, g);
        QCOMPARE(d.status, Status::Ready);
        QVERIFY(!d.recommendation.has_value());
    }

    void autoPickedThirtyTwoBitIsReported()
    {
        GlobalJava g;
        g.autoSwitch = true;
        InstanceJava inst;
        inst.compatibleMajors = { 17 };
        inst.maxMemMb = 4096;
        const QList<Runtime> list = { rt(kJ17x86, "17.0.9", 17, false), rt(kJ8, "1.8.0_382", 8) };
        Diagnosis d = Zero::Java::diagnose(inst, list, g);
        QCOMPARE(d.source, Source::AutomaticDetected);
        QCOMPARE(d.status, Status::Warning);
        QCOMPARE(d.reason, Reason::ThirtyTwoBitMemory);
        QVERIFY(!d.recommendation.has_value());
        inst.maxMemMb = 2048;
        d = Zero::Java::diagnose(inst, list, g);
        QCOMPARE(d.status, Status::Ready);
    }

    void readRequirementsFromFiles()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        auto write = [](const QString& path, const QByteArray& data) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(data);
        };
        const QString inst = tmp.filePath("inst");
        const QString meta = tmp.filePath("meta");

        // nothing on disk
        Requirements r = Zero::Java::readRequirements(inst, meta);
        QVERIFY(r.majors.isEmpty());
        QVERIFY(r.mcVersion.isEmpty());

        // pack file without any version file: the Minecraft version is known, the requirement is not
        write(inst + "/mmc-pack.json",
              R"({"components":[{"uid":"net.minecraft","version":"1.20.1","important":true},{"uid":"net.fabricmc.fabric-loader","version":"0.15.0"}],"formatVersion":1})");
        r = Zero::Java::readRequirements(inst, meta);
        QCOMPARE(r.mcVersion, QStringLiteral("1.20.1"));
        QVERIFY(r.majors.isEmpty());

        // metadata cache
        write(meta + "/net.minecraft/1.20.1.json", R"({"compatibleJavaMajors":[17,21],"compatibleJavaName":"java-runtime-gamma"})");
        r = Zero::Java::readRequirements(inst, meta);
        QCOMPARE(r.majors, (QList<int>{ 17, 21 }));
        QCOMPARE(r.javaName, QStringLiteral("java-runtime-gamma"));

        // a second component adds majors without duplicates; the custom patch replaces the cached file of its component
        write(meta + "/net.fabricmc.fabric-loader/0.15.0.json", R"({"compatibleJavaMajors":[21,25]})");
        r = Zero::Java::readRequirements(inst, meta);
        QCOMPARE(r.majors, (QList<int>{ 17, 21, 25 }));
        write(inst + "/patches/net.minecraft.json", R"({"compatibleJavaMajors":[8]})");
        r = Zero::Java::readRequirements(inst, meta);
        QCOMPARE(r.majors, (QList<int>{ 8, 21, 25 }));
        QVERIFY(r.javaName.isEmpty());

        // disabled components and broken files are ignored
        write(inst + "/mmc-pack.json",
              R"({"components":[{"uid":"net.minecraft","version":"1.20.1","important":true,"disabled":true},{"uid":"x","version":"1"}]})");
        write(meta + "/x/1.json", "not json");
        r = Zero::Java::readRequirements(inst, meta);
        QVERIFY(r.majors.isEmpty());
    }

    void helpers()
    {
        QVERIFY(majorFits({}, 8));
        QVERIFY(majorFits({ 17, 21 }, 17));
        QVERIFY(!majorFits({ 17, 21 }, 8));
        QCOMPARE(sortedMajors({ 21, 17, 21, 0, -1 }), (QList<int>{ 17, 21 }));
        QCOMPARE(normalizedPath(QString()), QString());
        QCOMPARE(normalizedPath(QStringLiteral("  ")), QString());
        QCOMPARE(normalizedPath(QStringLiteral("C:\\java\\.\\x\\..\\jdk\\bin")), normalizedPath(QStringLiteral("c:/java/jdk/bin")));
        QVERIFY(!bestRuntimeFor({}, sampleRuntimes(), false, QString()).has_value());
    }
};

QTEST_GUILESS_MAIN(JavaDoctorTest)

#include "JavaDoctor_test.moc"
