// SPDX-License-Identifier: GPL-3.0-only
// InstanceProfile: tag normalization, JSON round trip, favorite / description, artwork (scaling, atomic replace, clear), mod count.
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

#include "NullInstance.h"
#include "settings/INISettingsObject.h"
#include "settings/SettingsObject.h"
#include "zero/profiles/InstanceProfile.h"

using namespace Zero::Profile;

namespace {

/// A bare instance in `<root>/<id>` (instance.cfg with a [General] header), with the global settings BaseInstance overrides.
struct Fixture {
    explicit Fixture(const QString& root, const QString& id = QStringLiteral("inst"))
    {
        global = std::make_unique<INISettingsObject>(root + QStringLiteral("/global.cfg"));
        for (const char* name : { "ShowGameTime", "RecordGameTime", "ShowConsole", "AutoCloseConsole", "ShowConsoleOnError", "LogPrePostOutput",
                                  "ConsoleOverflowStop" })
            global->registerSetting(name, false);
        for (const char* name : { "PreLoadCommand", "PreLaunchCommand", "WrapperCommand", "PostExitCommand" })
            global->registerSetting(name, QString());
        global->registerSetting("ConsoleMaxLines", 100000);

        const QString dir = root + QLatin1Char('/') + id;
        QDir().mkpath(dir);
        QFile cfg(dir + QStringLiteral("/instance.cfg"));
        if (!cfg.exists() && cfg.open(QIODevice::WriteOnly))
            cfg.write("[General]\nname=Test\n");
        cfg.close();
        instance = std::make_unique<NullInstance>(global.get(), std::make_unique<INISettingsObject>(dir + QStringLiteral("/instance.cfg")), dir);
    }
    std::unique_ptr<SettingsObject> global;
    std::unique_ptr<NullInstance> instance;
};

QString makeImage(const QString& path, int w, int h, QColor color = Qt::red)
{
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(color);
    image.save(path, "PNG");
    return path;
}

QByteArray readAll(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

}  // namespace

class InstanceProfileTest : public QObject {
    Q_OBJECT
   private slots:
    void normalizeTrimsCollapsesAndDropsEmpties()
    {
        const QStringList in = { QStringLiteral("  pvp  "), QStringLiteral("big   base"), QString(), QStringLiteral("   "), QStringLiteral("#tech"),
                                 QStringLiteral("\tre\tdstone ") };
        QCOMPARE(normalizeTags(in), (QStringList{ "pvp", "big base", "tech", "re dstone" }));
    }

    void normalizeDedupesCaseInsensitivelyKeepingFirst()
    {
        QCOMPARE(normalizeTags({ "PvP", "pvp", "PVP", "Tech", "tech" }), (QStringList{ "PvP", "Tech" }));
        QCOMPARE(normalizeTags({ "a b", "A  B", " a b " }), (QStringList{ "a b" }));
    }

    void normalizeLimitsLengthAndCount()
    {
        const QString longTag(40, QLatin1Char('x'));
        QCOMPARE(normalizeTags({ longTag }).first().size(), kMaxTagLength);
        QStringList many;
        for (int i = 0; i < 30; ++i)
            many << QStringLiteral("tag%1").arg(i);
        const QStringList out = normalizeTags(many);
        QCOMPARE(out.size(), kMaxTags);
        QCOMPARE(out.first(), QStringLiteral("tag0"));
        QCOMPARE(out.last(), QStringLiteral("tag11"));
    }

    void jsonRoundTrip()
    {
        const QStringList tags = { "pvp", "big base", "ünïcode \"quoted\"" };
        const QString json = serializeTags(tags);
        QVERIFY(json.startsWith(QLatin1Char('[')));
        QCOMPARE(parseTags(json), tags);
        QCOMPARE(parseTags(QString()), QStringList());
        QCOMPARE(parseTags(QStringLiteral("not json")), QStringList());
        QCOMPARE(parseTags(QStringLiteral("{\"a\":1}")), QStringList());
        QCOMPARE(parseTags(QStringLiteral("[\"a\", 5, null, \" A \", \"b\"]")), (QStringList{ "a", "b" }));
    }

    void tagCountsMergeCaseAndSort()
    {
        const auto counts = tagCounts({ { "pvp", "Tech" }, { "PVP" }, { "tech", "tech", "Alpha" }, {} });
        QCOMPARE(counts.size(), 3);
        QCOMPARE(counts[0].tag, QStringLiteral("Alpha"));
        QCOMPARE(counts[1].tag, QStringLiteral("pvp"));
        QCOMPARE(counts[1].count, 2);
        QCOMPARE(counts[2].tag, QStringLiteral("Tech"));
        QCOMPARE(counts[2].count, 2);  // the third list repeats "tech": counted once
    }

    void settingsPersistAndNotify()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        {
            Fixture f(dir.path());
            BaseInstance* inst = f.instance.get();
            QSignalSpy props(inst, &BaseInstance::propertiesChanged);
            QSignalSpy notifier(Notifier::instance(), &Notifier::profileChanged);

            QVERIFY(!isFavorite(inst));
            setFavorite(inst, true);
            QVERIFY(isFavorite(inst));
            setFavorite(inst, true);  // unchanged: no signal
            QCOMPARE(props.size(), 1);
            QCOMPARE(notifier.size(), 1);
            QCOMPARE(notifier.first().first().toString(), QStringLiteral("inst"));

            setTags(inst, { "  PvP ", "pvp", "tech" });
            QCOMPARE(tags(inst), (QStringList{ "PvP", "tech" }));
            QCOMPARE(props.size(), 2);

            setDescription(inst, QStringLiteral("My notes"));
            QCOMPARE(description(inst), QStringLiteral("My notes"));
            QCOMPARE(inst->notes(), QStringLiteral("My notes"));  // the description is Prism's `notes`
            setDescription(inst, QStringLiteral("My notes"));
            QCOMPARE(props.size(), 3);
        }
        // a new instance object reading the same instance.cfg sees everything
        Fixture again(dir.path());
        QVERIFY(isFavorite(again.instance.get()));
        QCOMPARE(tags(again.instance.get()), (QStringList{ "PvP", "tech" }));
        QCOMPARE(description(again.instance.get()), QStringLiteral("My notes"));
    }

    void handWrittenConfigValues()
    {
        // The INI format (QSettings) needs a value with commas quoted: this is how instance.cfg stores the JSON array.
        for (const QString& line : { QStringLiteral("ZeroTags=\"[\\\"pvp\\\", \\\"tech\\\"]\"") }) {
            QTemporaryDir dir;
            QVERIFY(dir.isValid());
            QDir().mkpath(dir.path() + "/inst");
            QFile cfg(dir.path() + "/inst/instance.cfg");
            QVERIFY(cfg.open(QIODevice::WriteOnly));
            cfg.write(QByteArray("[General]\nname=Test\nZeroFavorite=true\n") + line.toUtf8() + "\n");
            cfg.close();
            Fixture f(dir.path());
            QVERIFY2(isFavorite(f.instance.get()), qPrintable(line));
            QCOMPARE(tags(f.instance.get()), (QStringList{ "pvp", "tech" }));
        }
    }

    void artworkIsScaledDownAndStoredInsideTheInstance()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Fixture f(dir.path());
        BaseInstance* inst = f.instance.get();
        QVERIFY(artworkPath(inst).isEmpty());

        const QString big = makeImage(dir.path() + "/big.png", 3200, 1000);
        QCOMPARE(setArtwork(inst, big), QString());
        const QString path = artworkPath(inst);
        QCOMPARE(path, QDir::cleanPath(dir.path() + "/inst/.zero/artwork.png"));
        const QImage stored(path);
        QCOMPARE(stored.width(), kMaxArtworkSide);
        QCOMPARE(stored.height(), 500);  // aspect ratio kept

        // never scaled up
        const QString small = makeImage(dir.path() + "/small.png", 300, 200, Qt::blue);
        QCOMPARE(setArtwork(inst, small), QString());
        const QImage storedSmall(artworkPath(inst));
        QCOMPARE(storedSmall.size(), QSize(300, 200));
        QCOMPARE(storedSmall.pixelColor(10, 10), QColor(Qt::blue));
        // no temporary files left behind
        QCOMPARE(QDir(dir.path() + "/inst/.zero").entryList(QDir::Files).size(), 1);
    }

    void unreadableSourceKeepsTheOldArtwork()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Fixture f(dir.path());
        BaseInstance* inst = f.instance.get();
        QCOMPARE(setArtwork(inst, makeImage(dir.path() + "/a.png", 400, 300)), QString());
        const QByteArray before = readAll(artworkPath(inst));
        QVERIFY(!before.isEmpty());

        QFile bad(dir.path() + "/bad.png");
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("this is not an image");
        bad.close();
        QVERIFY(!setArtwork(inst, dir.path() + "/bad.png").isEmpty());
        QVERIFY(!setArtwork(inst, dir.path() + "/missing.png").isEmpty());
        QCOMPARE(readAll(artworkPath(inst)), before);
    }

    void failedReplaceLeavesTheOldFileIntact()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Fixture f(dir.path());
        BaseInstance* inst = f.instance.get();
        QCOMPARE(setArtwork(inst, makeImage(dir.path() + "/a.png", 400, 300, Qt::green)), QString());
        const QString path = artworkPath(inst);
        const QByteArray before = readAll(path);

        // Windows refuses to replace a read-only file: the new picture is completely written to the temporary file, then the replace fails.
        QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner));
        const QString source = makeImage(dir.path() + "/b.png", 500, 500, Qt::red);
        const QString error = setArtwork(inst, source);
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        if (error.isEmpty())
            QSKIP("This platform replaces read-only files; the failure path cannot be provoked this way.");
        QCOMPARE(readAll(path), before);
        QCOMPARE(QImage(path).size(), QSize(400, 300));
        QCOMPARE(QDir(QFileInfo(path).absolutePath()).entryList(QDir::Files).size(), 1);  // temporary file removed
    }

    void clearArtworkDeletesOnlyItsFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Fixture f(dir.path());
        BaseInstance* inst = f.instance.get();
        const QString source = makeImage(dir.path() + "/a.png", 400, 300);
        QCOMPARE(setArtwork(inst, source), QString());
        const QString neighbour = dir.path() + "/inst/.zero/keep.txt";
        QFile keep(neighbour);
        QVERIFY(keep.open(QIODevice::WriteOnly));
        keep.write("x");
        keep.close();
        const QString outside = makeImage(dir.path() + "/inst/other.png", 10, 10);

        clearArtwork(inst);
        QVERIFY(artworkPath(inst).isEmpty());
        QVERIFY(!QFile::exists(dir.path() + "/inst/.zero/artwork.png"));
        QVERIFY(QFile::exists(neighbour));
        QVERIFY(QFile::exists(outside));
        QVERIFY(QFile::exists(source));  // the user's source file is never touched
        clearArtwork(inst);              // idempotent
    }

    void artworkPathRejectsEscapesAndMissingFiles()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Fixture f(dir.path());
        BaseInstance* inst = f.instance.get();
        makeImage(dir.path() + "/outside.png", 10, 10);
        inst->settings()->set("ZeroArtwork", QStringLiteral("../outside.png"));
        QVERIFY(artworkPath(inst).isEmpty());  // outside the instance folder
        inst->settings()->set("ZeroArtwork", QStringLiteral(".zero/artwork.png"));
        QVERIFY(artworkPath(inst).isEmpty());  // file does not exist
    }

    void modsAreCountedByFolder()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString mods = dir.path() + "/mods";
        QVERIFY(!countEnabledMods(mods).has_value());  // no folder
        QVERIFY(QDir().mkpath(mods));
        QCOMPARE(countEnabledMods(mods).value(), 0);
        QTest::qSleep(50);

        auto touch = [&](const QString& name) {
            QFile f(mods + '/' + name);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("x");
        };
        touch("a.jar");
        touch("B.JAR");
        touch("c.jar.disabled");
        touch("readme.txt");
        QVERIFY(QDir().mkpath(mods + "/sub"));
        QFile nested(mods + "/sub/d.jar");
        QVERIFY(nested.open(QIODevice::WriteOnly));
        nested.close();
        QCOMPARE(countEnabledMods(mods).value(), 2);  // top level only, disabled ones excluded

        QTest::qSleep(50);  // the count is cached by the folder's modification time
        touch("e.jar");
        QCOMPARE(countEnabledMods(mods).value(), 3);
    }
};

QTEST_GUILESS_MAIN(InstanceProfileTest)
#include "InstanceProfile_test.moc"
