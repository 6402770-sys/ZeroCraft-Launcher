// SPDX-License-Identifier: GPL-3.0-only
// Interface sounds: event classification of the app-wide filter (through an injectable sink, no real audio), throttle, the
// off switch, theme -> sound set, custom theme overrides (copied into the theme folder, exported and imported), and the generated
// WAV resources (all present, 44.1 kHz / 16-bit / mono, length and level limits).
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QListView>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>
#include <memory>

#include "ui/zero/ZeroNavRail.h"
#include "ui/zero/ZeroPlayButton.h"
#include "ui/zero/ZeroSound.h"
#include "ui/zero/ZeroThemeState.h"
#include "zero/theme/CustomTheme.h"
#include "zero/theme/ThemeArchive.h"
#include "zero/theme/ThemeEditorModel.h"
#include "zero/theme/ThemeFiles.h"

using namespace Zero;
using Sound::Event;

class ZeroSoundTest : public QObject {
    Q_OBJECT

    QList<Event> m_events;
    qint64 m_clock = 1000000;

    void tick(qint64 ms = 100) { m_clock += ms; }  // every interaction is further apart than the throttle unless a test says otherwise

    /// Writes a minimal valid mono 16-bit WAV (20 ms of silence) to `path`.
    static void writeWav(const QString& path, int samples = 882)
    {
        QByteArray data(samples * 2, '\0');
        QByteArray h;
        const auto le32 = [&](quint32 v) { h.append(char(v & 0xFF)).append(char((v >> 8) & 0xFF)).append(char((v >> 16) & 0xFF)).append(char((v >> 24) & 0xFF)); };
        const auto le16 = [&](quint16 v) { h.append(char(v & 0xFF)).append(char((v >> 8) & 0xFF)); };
        h.append("RIFF");
        le32(36 + data.size());
        h.append("WAVEfmt ");
        le32(16);
        le16(1);
        le16(1);
        le32(44100);
        le32(88200);
        le16(2);
        le16(16);
        h.append("data");
        le32(data.size());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(h + data);
    }

   private slots:
    void init()
    {
        m_events.clear();
        m_clock = 1000000;
        Sound::install(qApp);  // idempotent; outside the Application it only installs the filter
        Sound::setSink([this](Event e) { m_events << e; });
        Sound::setClock([this]() { return m_clock; });
        Sound::resetThrottle();
        Sound::setConfig({ true, 35 });
    }

    void cleanup()
    {
        Sound::setSink({});
        Sound::setClock({});
        Sound::setConfig({ true, Sound::kDefaultVolume });
    }

    // ---------------------------------------------------------------- classification

    void pushButtonIsClick()
    {
        QPushButton b("Go");
        b.show();
        QVERIFY(QTest::qWaitForWindowExposed(&b));
        QTest::mouseClick(&b, Qt::LeftButton);
        QCOMPARE(m_events, QList<Event>({ Event::Click }));
    }

    void releaseOutsideTheButtonIsNothing()
    {
        QPushButton b("Go");
        b.resize(60, 30);
        b.show();
        QVERIFY(QTest::qWaitForWindowExposed(&b));
        QTest::mousePress(&b, Qt::LeftButton);
        QTest::mouseRelease(&b, Qt::LeftButton, {}, QPoint(200, 200));  // dragged off, released elsewhere
        QVERIFY(m_events.isEmpty());
    }

    void switchPlaysOnThenOffByTheNewState()
    {
        QCheckBox c("Option");
        c.show();
        QVERIFY(QTest::qWaitForWindowExposed(&c));
        QTest::mouseClick(&c, Qt::LeftButton);
        QVERIFY(c.isChecked());
        tick();
        QTest::mouseClick(&c, Qt::LeftButton);
        QVERIFY(!c.isChecked());
        QCOMPARE(m_events, QList<Event>({ Event::ToggleOn, Event::ToggleOff }));
    }

    void programmaticChangeIsSilent()
    {
        QCheckBox c("Option");
        c.setChecked(true);
        c.setChecked(false);
        c.click();  // QAbstractButton::click() is programmatic: no mouse event
        QVERIFY(m_events.isEmpty());
    }

    void exclusiveChoiceIsClickOnlyWhenItChanges()
    {
        QWidget host;
        auto* lay = new QVBoxLayout(&host);
        auto* a = new QRadioButton("A");
        auto* b = new QRadioButton("B");
        lay->addWidget(a);
        lay->addWidget(b);
        a->setChecked(true);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        QTest::mouseClick(a, Qt::LeftButton);  // already chosen
        QVERIFY(m_events.isEmpty());
        QTest::mouseClick(b, Qt::LeftButton);
        QCOMPARE(m_events, QList<Event>({ Event::Click }));
    }

    void optedOutButtonIsSilent()
    {
        QPushButton b("Quiet");
        b.setProperty(Sound::kOptOutProperty, true);
        b.show();
        QVERIFY(QTest::qWaitForWindowExposed(&b));
        QTest::mouseClick(&b, Qt::LeftButton);
        QVERIFY(m_events.isEmpty());
    }

    void disabledButtonIsSilent()
    {
        QPushButton b("Off");
        b.setEnabled(false);
        b.show();
        QVERIFY(QTest::qWaitForWindowExposed(&b));
        QTest::mouseClick(&b, Qt::LeftButton);
        QVERIFY(m_events.isEmpty());
    }

    void navRailItemIsNavigate()
    {
        ZeroNavRail rail;
        rail.addItem(QStringLiteral("home"), QStringLiteral("house"), QStringLiteral("Home"));
        rail.addItem(QStringLiteral("library"), QStringLiteral("layers"), QStringLiteral("Library"));
        rail.resize(220, 300);
        rail.show();
        QVERIFY(QTest::qWaitForWindowExposed(&rail));
        QWidget* item = rail.itemWidget(QStringLiteral("library"));
        QVERIFY(item);
        QTest::mouseClick(item, Qt::LeftButton);
        QCOMPARE(m_events, QList<Event>({ Event::Navigate }));
    }

    void pageListSelectionIsNavigate()
    {
        QStandardItemModel model;
        model.appendRow(new QStandardItem("Mods"));
        model.appendRow(new QStandardItem("Worlds"));
        QListView plain;
        plain.setModel(&model);
        plain.show();
        QVERIFY(QTest::qWaitForWindowExposed(&plain));
        QTest::mouseClick(plain.viewport(), Qt::LeftButton, {}, plain.visualRect(model.index(1, 0)).center());
        QVERIFY(m_events.isEmpty());  // an ordinary list is not a navigation list

        QListView nav;
        nav.setModel(&model);
        nav.setProperty(Sound::kEventProperty, "navigate");
        nav.show();
        QVERIFY(QTest::qWaitForWindowExposed(&nav));
        tick();
        QTest::mouseClick(nav.viewport(), Qt::LeftButton, {}, nav.visualRect(model.index(1, 0)).center());
        QCOMPARE(m_events, QList<Event>({ Event::Navigate }));
        tick();
        QTest::mouseClick(nav.viewport(), Qt::LeftButton, {}, QPoint(2, nav.viewport()->height() - 2));  // empty space
        QCOMPARE(m_events.size(), 1);
    }

    void playButtonIsLaunch()
    {
        ZeroPlayButton play;
        play.resize(200, 48);
        play.show();
        QVERIFY(QTest::qWaitForWindowExposed(&play));
        QTest::mouseClick(&play, Qt::LeftButton);
        QCOMPARE(m_events, QList<Event>({ Event::Launch }));
    }

    void spaceAndEnterActivateWithSound()
    {
        QPushButton b("Go");
        b.show();
        QVERIFY(QTest::qWaitForWindowExposed(&b));
        b.setFocus();
        QTest::keyClick(&b, Qt::Key_Space);
        QCOMPARE(m_events, QList<Event>({ Event::Click }));
        tick();
        b.setAutoDefault(true);
        QTest::keyClick(&b, Qt::Key_Return);
        QCOMPARE(m_events, QList<Event>({ Event::Click, Event::Click }));
    }

    // ---------------------------------------------------------------- throttle / switch

    void sameEventIsThrottled()
    {
        Sound::play(Event::Click);
        tick(10);
        Sound::play(Event::Click);  // 10 ms later: dropped
        Sound::play(Event::Error);  // another event is not throttled by it
        tick(Sound::kThrottleMs);
        Sound::play(Event::Click);
        QCOMPARE(m_events, QList<Event>({ Event::Click, Event::Error, Event::Click }));
    }

    void offMeansNothing()
    {
        Sound::setConfig({ false, 35 });
        Sound::play(Event::Click);
        QPushButton b("Go");
        b.show();
        QVERIFY(QTest::qWaitForWindowExposed(&b));
        QTest::mouseClick(&b, Qt::LeftButton);
        QVERIFY(m_events.isEmpty());
        Sound::setConfig({ true, 0 });  // volume 0 is silent too
        tick();
        Sound::play(Event::Click);
        QVERIFY(m_events.isEmpty());
        QCOMPARE(Sound::config().volume, 0);
        Sound::setConfig({ true, 250 });
        QCOMPARE(Sound::config().volume, 100);  // clamped
    }

    void previewIgnoresTheSwitch()
    {
        Sound::setConfig({ false, 35 });
        Sound::preview(Event::Success);
        QCOMPARE(m_events, QList<Event>({ Event::Success }));
    }

    void eventIdsMatchThemeIds()
    {
        const QStringList ids = Custom::soundEventIds();
        QCOMPARE(ids.size(), Sound::kEventCount);
        for (int i = 0; i < ids.size(); ++i) {
            QCOMPARE(Sound::eventId(Event(i)), ids[i]);
            QVERIFY(Sound::eventFromId(ids[i]).has_value());
        }
        QVERIFY(!Sound::eventFromId(QStringLiteral("hover")).has_value());
    }

    // ---------------------------------------------------------------- theme -> set

    void themeMapsToSet()
    {
        QCOMPARE(Sound::setForSpec(ThemeSpec::obsidian()), QStringLiteral("soft"));
        QCOMPARE(Sound::setForSpec(Custom::baseSpec(Custom::Base::Minecraft)), QStringLiteral("retro"));
        QCOMPARE(Sound::setForSpec(Custom::baseSpec(Custom::Base::Obsidian)), QStringLiteral("soft"));

        ThemeSpec spec = Custom::baseSpec(Custom::Base::Minecraft);
        spec.soundSet = QStringLiteral("soft");
        QCOMPARE(Sound::setForSpec(spec), QStringLiteral("soft"));
        spec.soundSet = QStringLiteral("none");
        QCOMPARE(Sound::setForSpec(spec), QStringLiteral("none"));
        QVERIFY(Sound::sourceFor(spec, Event::Click).isEmpty());
        spec.soundSet = QStringLiteral("bogus");  // unknown: back to the base theme's style
        QCOMPARE(Sound::setForSpec(spec), QStringLiteral("retro"));

        QCOMPARE(Sound::sourceFor(ThemeSpec::obsidian(), Event::ToggleOn), QStringLiteral(":/zero/sounds/soft/toggle-on.wav"));
        QCOMPARE(Sound::sourceFor(Custom::baseSpec(Custom::Base::Minecraft), Event::Launch), QStringLiteral(":/zero/sounds/retro/launch.wav"));
    }

    // ---------------------------------------------------------------- custom themes

    void validatesPickedSounds()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString good = dir.filePath("good.wav");
        writeWav(good);
        QVERIFY(Custom::validateSoundSource(good).has_value());

        const QString mp3 = dir.filePath("song.mp3");
        QFile(mp3).open(QIODevice::WriteOnly);
        QVERIFY(!Custom::validateSoundSource(mp3).has_value());

        const QString fake = dir.filePath("fake.wav");
        QFile f(fake);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not a riff file at all");
        f.close();
        QVERIFY(!Custom::validateSoundSource(fake).has_value());

        QVERIFY(!Custom::validateSoundSource(good, 10).has_value());  // over the size cap
    }

    void customSoundsAreCopiedIntoTheThemeFolderAndResolve()
    {
        QTemporaryDir root;
        QTemporaryDir src;
        QVERIFY(root.isValid() && src.isValid());
        const QString mine = src.filePath("mine.wav");
        writeWav(mine);

        Custom::Definition def = Custom::Definition::fromBase(Custom::Base::Obsidian, "Loud");
        def.soundSet = QStringLiteral("retro");
        Custom::AssetSources assets;
        assets.sounds.insert(QStringLiteral("click"), mine);
        auto saved = Custom::saveTheme(root.path(), {}, def, {}, {}, assets);
        QVERIFY2(saved.has_value(), qPrintable(saved ? QString() : saved.error()));

        QVERIFY(QFile::exists(QDir(saved->folder).filePath("sound-click.wav")));
        QVERIFY(!QFile::exists(QDir(saved->folder).filePath(".zero-snd-click.tmp")));
        auto loaded = Custom::loadDefinition(saved->folder);
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->soundSet, QStringLiteral("retro"));
        QCOMPARE(loaded->sounds.value("click"), QStringLiteral("sound-click.wav"));

        const ThemeSpec spec = Custom::deriveSpec(*loaded, saved->folder, saved->id);
        QCOMPARE(spec.soundSet, QStringLiteral("retro"));
        const QString expected = QDir(saved->folder).absoluteFilePath("sound-click.wav");
        QCOMPARE(spec.soundFiles.value("click"), expected);
        QCOMPARE(Sound::sourceFor(spec, Event::Click), expected);                                  // the theme's own file
        QCOMPARE(Sound::sourceFor(spec, Event::Error), QStringLiteral(":/zero/sounds/retro/error.wav"));  // the style's for the rest

        // The own file keeps playing when the style is "none".
        ThemeSpec silent = spec;
        silent.soundSet = QStringLiteral("none");
        QCOMPARE(Sound::sourceFor(silent, Event::Click), expected);
        QVERIFY(Sound::sourceFor(silent, Event::Error).isEmpty());

        // Resetting (dropping the entry) deletes the file on the next save.
        loaded->sounds.remove("click");
        auto again = Custom::saveTheme(root.path(), saved->folder, *loaded);
        QVERIFY(again.has_value());
        QVERIFY(!QFile::exists(expected));
    }

    void invalidSoundRejectsTheSave()
    {
        QTemporaryDir root;
        QTemporaryDir src;
        const QString bad = src.filePath("bad.wav");
        QFile f(bad);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("nope");
        f.close();
        Custom::AssetSources assets;
        assets.sounds.insert(QStringLiteral("click"), bad);
        auto saved = Custom::saveTheme(root.path(), {}, Custom::Definition::fromBase(Custom::Base::Obsidian, "Bad"), {}, {}, assets);
        QVERIFY(!saved.has_value());
        QVERIFY(QDir(root.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());  // nothing half-done
    }

    void soundsTravelWithAnExportedTheme()
    {
        QTemporaryDir root;
        QTemporaryDir src;
        QTemporaryDir out;
        const QString mine = src.filePath("mine.wav");
        writeWav(mine);
        Custom::AssetSources assets;
        assets.sounds.insert(QStringLiteral("success"), mine);
        Custom::Definition def = Custom::Definition::fromBase(Custom::Base::Minecraft, "Travel");
        def.soundSet = QStringLiteral("soft");
        auto saved = Custom::saveTheme(root.path(), {}, def, {}, {}, assets);
        QVERIFY(saved.has_value());

        const QString zip = out.filePath("travel.zip");
        auto exported = Custom::exportTheme(saved->folder, zip);
        QVERIFY2(exported.has_value(), qPrintable(exported ? QString() : exported.error()));

        QTemporaryDir other;
        auto imported = Custom::importTheme(zip, other.path());
        QVERIFY2(imported.has_value(), qPrintable(imported ? QString() : imported.error()));
        auto def2 = Custom::loadDefinition(imported->folder);
        QVERIFY(def2.has_value());
        QCOMPARE(def2->soundSet, QStringLiteral("soft"));
        QCOMPARE(def2->sounds.value("success"), QStringLiteral("sound-success.wav"));
        QVERIFY(QFile::exists(QDir(imported->folder).filePath("sound-success.wav")));
        QCOMPARE(QFileInfo(QDir(imported->folder).filePath("sound-success.wav")).size(), QFileInfo(mine).size());

        // Another theme can also be duplicated with its sounds.
        auto copy = Custom::duplicateTheme(root.path(), saved->folder);
        QVERIFY(copy.has_value());
        QVERIFY(QFile::exists(QDir(copy->folder).filePath("sound-success.wav")));
    }

    void editorModelTracksSounds()
    {
        QTemporaryDir src;
        const QString mine = src.filePath("mine.wav");
        writeWav(mine);

        Custom::ThemeEditorModel model;
        model.beginNew("Mine");
        QCOMPARE(model.soundStyle(), QStringLiteral("soft"));  // Obsidian base
        model.setBase(Custom::Base::Minecraft);
        QCOMPARE(model.soundStyle(), QStringLiteral("retro"));  // follows the base until chosen
        model.setSoundStyle(QStringLiteral("none"));
        QCOMPARE(model.soundStyle(), QStringLiteral("none"));
        model.setBase(Custom::Base::Obsidian);
        QCOMPARE(model.soundStyle(), QStringLiteral("none"));

        QVERIFY(!model.hasEventSound("click"));
        model.setEventSound("click", mine);
        QVERIFY(model.hasEventSound("click"));
        QCOMPARE(model.eventSoundPath("click"), QFileInfo(mine).absoluteFilePath());
        QCOMPARE(model.eventSoundName("click"), QStringLiteral("mine.wav"));
        QCOMPARE(model.spec().soundFiles.value("click"), QFileInfo(mine).absoluteFilePath());  // the live spec previews the picked file
        QCOMPARE(model.spec().soundSet, QStringLiteral("none"));
        QCOMPARE(model.pendingEventSounds().size(), 1);
        QCOMPARE(model.definition().sounds.value("click"), QStringLiteral("sound-click.wav"));
        model.resetEventSound("click");
        QVERIFY(!model.hasEventSound("click"));
        QVERIFY(model.spec().soundFiles.isEmpty());
        QVERIFY(model.pendingEventSounds().isEmpty());
    }

    // ---------------------------------------------------------------- generated WAVs

    void everyGeneratedWavIsInTheResourcesAndWellFormed()
    {
        for (const QString& set : { QStringLiteral("retro"), QStringLiteral("soft") }) {
            for (const QString& event : Custom::soundEventIds()) {
                const QString path = QStringLiteral(":/zero/sounds/%1/%2.wav").arg(set, event);
                QFile f(path);
                QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(path + " is missing from zero.qrc"));
                const QByteArray all = f.readAll();
                QVERIFY2(all.size() > 44, qPrintable(path));
                QCOMPARE(all.left(4), QByteArray("RIFF"));
                QCOMPARE(all.mid(8, 4), QByteArray("WAVE"));
                const auto u16 = [&](int at) { return quint16(quint8(all[at]) | (quint8(all[at + 1]) << 8)); };
                const auto u32 = [&](int at) { return quint32(u16(at)) | (quint32(u16(at + 2)) << 16); };
                QCOMPARE(all.mid(12, 4), QByteArray("fmt "));
                QCOMPARE(int(u16(20)), 1);       // PCM
                QCOMPARE(int(u16(22)), 1);       // mono
                QCOMPARE(int(u32(24)), 44100);   // sample rate
                QCOMPARE(int(u16(34)), 16);      // bits
                QCOMPARE(all.mid(36, 4), QByteArray("data"));
                const int bytes = int(u32(40));
                QVERIFY(bytes > 0 && 44 + bytes <= all.size());
                const double seconds = bytes / 2.0 / 44100.0;
                const double limit = event == QLatin1String("success") ? 0.5 : 0.3;
                QVERIFY2(seconds > 0.015 && seconds <= limit, qPrintable(QStringLiteral("%1 lasts %2 s").arg(path).arg(seconds)));
                int peak = 0;
                for (int i = 0; i < bytes / 2; ++i)
                    peak = std::max(peak, std::abs(int(qint16(u16(44 + i * 2)))));
                QVERIFY2(peak > 8000 && peak < int(0.56 * 32767), qPrintable(QStringLiteral("%1 peaks at %2").arg(path).arg(peak)));  // about -6 dBFS
                // Fades: the first and last samples are (almost) silent, so nothing clicks.
                QVERIFY2(std::abs(int(qint16(u16(44)))) < 1500, qPrintable(path + " starts with a click"));
                QVERIFY2(std::abs(int(qint16(u16(44 + bytes - 2)))) < 1500, qPrintable(path + " ends with a click"));
            }
        }
    }
};

QTEST_MAIN(ZeroSoundTest)

#include "ZeroSound_test.moc"
