// SPDX-License-Identifier: GPL-3.0-only
// The "Minecraft" interface sound pack: event -> asset keys, resolution against a FAKE assets tree (indexes + objects with made-up
// content; no game file is in the repository), conditioning (trim / peak), the WAV writer and pitch header, the manifest, and - only
// when the machine has the game's assets - a real decode + conversion into a temp folder (skipped otherwise, never copied anywhere).
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
#include <cmath>

#include "ui/zero/ZeroSound.h"
#include "ui/zero/ZeroTokens.h"
#include "zero/sound/MinecraftSoundCache.h"
#include "zero/sound/MinecraftSounds.h"

using namespace Zero::Sound;
using namespace Zero::Sound::Minecraft;

class MinecraftSoundsTest : public QObject {
    Q_OBJECT

    static void put(const QString& path, const QByteArray& data)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data);
    }

    /// Writes <root>/indexes/<name>.json listing `keys` (key -> hash, size) and, for those in `present`, the object file of that size.
    static void makeIndex(const QString& root, const QString& name, const QList<QPair<QString, QString>>& keysAndHashes, int size,
                          const QStringList& presentHashes, int actualSize = -1)
    {
        QJsonObject objects;
        for (const auto& kh : keysAndHashes)
            objects.insert(kh.first, QJsonObject{ { "hash", kh.second }, { "size", size } });
        put(root + "/indexes/" + name + ".json", QJsonDocument(QJsonObject{ { "objects", objects } }).toJson());
        for (const QString& h : presentHashes)
            put(root + "/objects/" + h.left(2) + "/" + h, QByteArray(actualSize >= 0 ? actualSize : size, 'x'));
    }

    /// Every key some event needs, hash = a stable fake per key.
    static QList<QPair<QString, QString>> allKeys(const QString& salt, bool withPreferred = true)
    {
        QList<QPair<QString, QString>> out;
        QStringList keys;
        for (int i = 0; i < kEventCount; ++i)
            for (const QString& k : specFor(Event(i)).keys)
                if (!keys.contains(k) && (withPreferred || k.contains("click.ogg") || k.contains("orb") || k.contains("wood") || k.contains("levelup") || k.contains("bass")))
                    keys << k;
        for (const QString& k : keys)
            out.append({ k, QString::number(qHash(k + salt), 16).rightJustified(40, '0') });
        return out;
    }

    static QStringList hashesOf(const QList<QPair<QString, QString>>& l)
    {
        QStringList out;
        for (const auto& kh : l)
            out << kh.second;
        return out;
    }

    static Pcm tone(int rate, int channels, double seconds, int amplitude)
    {
        Pcm p;
        p.sampleRate = rate;
        p.channels = channels;
        const int frames = int(seconds * rate);
        for (int i = 0; i < frames; ++i)
            for (int c = 0; c < channels; ++c)
                p.samples.append(qint16(amplitude * std::sin(i * 0.05 * (c + 1))));
        return p;
    }

    static quint32 u32(const QByteArray& b, int at) { return qFromLittleEndian<quint32>(b.constData() + at); }
    static quint16 u16(const QByteArray& b, int at) { return qFromLittleEndian<quint16>(b.constData() + at); }

   private slots:
    void specsFollowTheDesign()
    {
        QCOMPARE(specFor(Event::Click).keys, QStringList{ "minecraft/sounds/random/click.ogg" });
        QCOMPARE(specFor(Event::ToggleOn).keys, QStringList{ "minecraft/sounds/random/wood_click.ogg" });
        QCOMPARE(specFor(Event::ToggleOff).keys, specFor(Event::ToggleOn).keys);
        QCOMPARE(specFor(Event::ToggleOff).rateScale, 0.8);
        QCOMPARE(specFor(Event::ToggleOn).rateScale, 1.0);
        QCOMPARE(specFor(Event::Navigate).keys, (QStringList{ "minecraft/sounds/item/book/open_flip1.ogg", "minecraft/sounds/random/click.ogg" }));
        QCOMPARE(specFor(Event::Success).keys, QStringList{ "minecraft/sounds/random/orb.ogg" });
        QCOMPARE(specFor(Event::Error).keys, (QStringList{ "minecraft/sounds/mob/villager/no1.ogg", "minecraft/sounds/note/bass.ogg" }));
        QCOMPARE(specFor(Event::Launch).keys, QStringList{ "minecraft/sounds/random/levelup.ogg" });
    }

    void resolvesAgainstFakeAssets()
    {
        QTemporaryDir dir;
        const QString root = dir.path();
        QVERIFY(!available(root));  // nothing there
        const auto keys = allKeys("a");
        makeIndex(root, "17", keys, 10, hashesOf(keys));
        QVERIFY(available(root));
        const auto all = resolveAll(root);
        QVERIFY(all);
        QCOMPARE(all->size(), kEventCount);
        QCOMPARE((*all)[int(Event::Click)].key, QString("minecraft/sounds/random/click.ogg"));
        QCOMPARE((*all)[int(Event::Click)].indexName, QString("17"));
        QVERIFY(QFileInfo((*all)[int(Event::Launch)].objectPath).isFile());
        QCOMPARE((*all)[int(Event::ToggleOff)].hash, (*all)[int(Event::ToggleOn)].hash);
    }

    void newestIndexWinsButMustHaveTheFile()
    {
        QTemporaryDir dir;
        const QString root = dir.path();
        const auto oldKeys = allKeys("old");
        const auto newKeys = allKeys("new");
        makeIndex(root, "5", oldKeys, 10, hashesOf(oldKeys));
        makeIndex(root, "17", newKeys, 10, hashesOf(newKeys));
        makeIndex(root, "9", allKeys("mid"), 10, {});  // lists everything, has no objects
        auto asset = resolve(root, specFor(Event::Click).keys);
        QVERIFY(asset);
        QCOMPARE(asset->indexName, QString("17"));  // numeric order, not text order ("5" > "17" as text)

        // The newest index loses its object for click: the next newest index with the file answers.
        QFile::remove(asset->objectPath);
        asset = resolve(root, specFor(Event::Click).keys);
        QVERIFY(asset);
        QCOMPARE(asset->indexName, QString("5"));
    }

    void wrongSizeIsRejected()
    {
        QTemporaryDir dir;
        const auto keys = allKeys("s");
        makeIndex(dir.path(), "3", keys, 10, hashesOf(keys), 7);  // files are 7 bytes, the index says 10
        QVERIFY(!resolve(dir.path(), specFor(Event::Click).keys));
        QVERIFY(!available(dir.path()));
    }

    void fallbackKeysAndAvailability()
    {
        QTemporaryDir dir;
        const auto keys = allKeys("f", false);  // no book flip, no villager: the fallbacks have to answer
        makeIndex(dir.path(), "1", keys, 10, hashesOf(keys));
        const auto all = resolveAll(dir.path());
        QVERIFY(all);
        QCOMPARE((*all)[int(Event::Navigate)].key, QString("minecraft/sounds/random/click.ogg"));
        QCOMPARE((*all)[int(Event::Error)].key, QString("minecraft/sounds/note/bass.ogg"));

        // Without the fallback of error nothing resolves for it: the pack is not available.
        QTemporaryDir dir2;
        auto fewer = keys;
        for (int i = fewer.size() - 1; i >= 0; --i)
            if (fewer[i].first.contains("bass"))
                fewer.removeAt(i);
        makeIndex(dir2.path(), "1", fewer, 10, hashesOf(fewer));
        QVERIFY(!available(dir2.path()));
    }

    void nonNumericIndexesFallBackToTime()
    {
        QTemporaryDir dir;
        const auto a = allKeys("x");
        const auto b = allKeys("y");
        makeIndex(dir.path(), "legacy", a, 10, hashesOf(a));
        makeIndex(dir.path(), "pre-1.6", b, 10, hashesOf(b));
        QFile::setPermissions(dir.path() + "/indexes/legacy.json", QFile::ReadOwner | QFile::WriteOwner);
        // Numeric (none here) ties: the newest file wins. Make "pre-1.6" clearly newer.
        QFile f(dir.path() + "/indexes/pre-1.6.json");
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(QDateTime::currentDateTime().addDays(1), QFileDevice::FileModificationTime));
        f.close();
        const auto asset = resolve(dir.path(), specFor(Event::Click).keys);
        QVERIFY(asset);
        QCOMPARE(asset->indexName, QString("pre-1.6"));
    }

    void conditioningTrimsAndNormalises()
    {
        const Pcm in = tone(44100, 1, 4.0, 3000);
        const Pcm out = conditioned(in);
        QCOMPARE(out.sampleRate, 44100);
        QCOMPARE(out.channels, 1);
        QVERIFY(out.seconds() <= kMaxSeconds + 1e-6);
        QVERIFY(out.seconds() > kMaxSeconds - 0.01);
        int peak = 0;
        for (qint16 s : out.samples)
            peak = std::max(peak, std::abs(int(s)));
        QVERIFY2(peak > 15000 && peak <= 16384, qPrintable(QString::number(peak)));  // about -6 dBFS
        QVERIFY(std::abs(int(out.samples.last())) < 100);                            // faded out where it was cut

        const Pcm stereo = conditioned(tone(22050, 2, 0.5, 100));
        QCOMPARE(stereo.channels, 2);
        QCOMPARE(stereo.samples.size(), 22050 / 2 * 2);  // short sounds are not touched in length
        const Pcm silent = conditioned(Pcm{ 44100, 1, QVector<qint16>(100, 0) });
        QCOMPARE(silent.samples, QVector<qint16>(100, 0));
        QCOMPARE(conditioned(Pcm{}).samples.size(), 0);
    }

    void wavHeaderAndPitch()
    {
        const Pcm p = tone(44100, 1, 0.1, 8000);
        const QByteArray plain = toWav(p);
        QCOMPARE(plain.left(4), QByteArray("RIFF"));
        QCOMPARE(plain.mid(8, 8), QByteArray("WAVEfmt "));
        QCOMPARE(int(u32(plain, 4)), plain.size() - 8);
        QCOMPARE(int(u16(plain, 20)), 1);  // PCM
        QCOMPARE(int(u16(plain, 22)), 1);
        QCOMPARE(int(u32(plain, 24)), 44100);
        QCOMPARE(int(u32(plain, 28)), 88200);
        QCOMPARE(int(u16(plain, 34)), 16);
        QCOMPARE(int(u32(plain, 40)), p.samples.size() * 2);
        QCOMPARE(plain.size(), 44 + p.samples.size() * 2);

        const QByteArray low = toWav(p, 0.8);  // same samples, header rate x 0.8: lower and slower
        QCOMPARE(int(u32(low, 24)), 35280);
        QCOMPARE(int(u32(low, 28)), 70560);
        QCOMPARE(low.mid(44), plain.mid(44));

        Pcm stereo = tone(44100, 2, 0.1, 8000);
        const QByteArray st = toWav(stereo);
        QCOMPARE(int(u16(st, 22)), 2);
        QCOMPARE(int(u32(st, 28)), 44100 * 4);
        QCOMPARE(int(u16(st, 32)), 4);
    }

    void manifestAndCacheCurrency()
    {
        QTemporaryDir assetsDir;
        const auto keys = allKeys("m");
        makeIndex(assetsDir.path(), "1", keys, 10, hashesOf(keys));
        const auto assets = *resolveAll(assetsDir.path());
        QTemporaryDir cache;
        QVERIFY(!Cache::isCurrent(cache.path(), assets));
        for (int i = 0; i < kEventCount; ++i)
            QVERIFY(Cache::writeEvent(cache.path(), Event(i), tone(44100, 1, 0.05, 5000)));
        QVERIFY(!Cache::isCurrent(cache.path(), assets));  // no manifest yet
        QVERIFY(Cache::writeManifest(cache.path(), assets));
        QVERIFY(Cache::isCurrent(cache.path(), assets));

        const QByteArray off = [&]() {
            QFile f(cache.path() + "/toggle-off.wav");
            f.open(QIODevice::ReadOnly);
            return f.readAll();
        }();
        QCOMPARE(int(u32(off, 24)), 35280);  // toggle-off carries the lower pitch

        auto changed = assets;
        changed[0].hash = "feedface";
        QVERIFY(!Cache::isCurrent(cache.path(), changed));  // a different source file: rebuild
        QVERIFY(QFile::remove(cache.path() + "/launch.wav"));
        QVERIFY(!Cache::isCurrent(cache.path(), assets));  // a missing file: rebuild
    }

    void cacheWithoutAssetsIsUnavailable()
    {
        QTemporaryDir assets, out;
        Cache cache(assets.path(), out.path() + "/m");
        QSignalSpy spy(&cache, &Cache::changed);
        cache.prepare();
        QCOMPARE(cache.state(), Cache::State::Unavailable);
        QVERIFY(!cache.available());
        QVERIFY(spy.count() >= 1);
        QVERIFY(!QFileInfo::exists(out.path() + "/m"));
    }

    void packSelectionNeedsReadyFiles()
    {
        // sourceFor: the converted file only when the pack is on, the set is retro and the file exists.
        QTemporaryDir dir;
        for (int i = 0; i < kEventCount; ++i)
            QVERIFY(Cache::writeEvent(dir.path(), Event(i), tone(44100, 1, 0.05, 5000)));
        Zero::ThemeSpec mc;
        mc.id = "zero-minecraft";
        mc.baseId = "minecraft";
        Zero::ThemeSpec other;
        other.id = "obsidian";
        Config c;
        c.minecraftPack = true;
        setConfig(c);
        setMinecraftDir({});
        QVERIFY(sourceFor(mc, Event::Click).startsWith(":/zero/sounds/retro/"));  // not ready: retro, never silent
        setMinecraftDir(dir.path());
        QCOMPARE(sourceFor(mc, Event::Click), dir.path() + "/click.wav");
        QVERIFY(sourceFor(other, Event::Click).startsWith(":/zero/sounds/soft/"));  // not a retro set: untouched
        c.minecraftPack = false;
        setConfig(c);
        QVERIFY(sourceFor(mc, Event::Click).startsWith(":/zero/sounds/retro/"));
        setMinecraftDir({});
    }

    void realAssetsDecodeAndConvert()
    {
        const QString root = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation).isEmpty()
                                 ? QString()
                                 : qEnvironmentVariable("APPDATA") + "/ZEROLauncher/assets";
        if (root.isEmpty() || !QFileInfo::exists(root + "/indexes") || !available(root))
            QSKIP("no local Minecraft assets on this machine");

        QTemporaryDir out;
        Cache cache(root, out.path() + "/minecraft");
        QSignalSpy spy(&cache, &Cache::changed);
        cache.prepare();
        QElapsedTimer t;
        t.start();
        while (cache.state() == Cache::State::Preparing && t.elapsed() < 60000)
            QTest::qWait(50);
        QCOMPARE(cache.state(), Cache::State::Ready);
        const auto assets = *resolveAll(root);
        QVERIFY(Cache::isCurrent(cache.dir(), assets));
        for (int i = 0; i < kEventCount; ++i) {
            QFile f(cache.dir() + "/" + eventId(Event(i)) + ".wav");
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QByteArray w = f.readAll();
            QVERIFY(w.size() > 44);
            const double seconds = double(u32(w, 40)) / u16(w, 22) / 2 / u32(w, 24);
            QVERIFY2(seconds > 0.02 && seconds <= kMaxSeconds + 0.01, qPrintable(eventId(Event(i)) + " " + QString::number(seconds)));
            int peak = 0;
            for (int s = 44; s + 1 < w.size(); s += 2)
                peak = std::max(peak, std::abs(int(qint16(u16(w, s)))));
            QVERIFY2(peak > 15000 && peak <= 16384, qPrintable(eventId(Event(i)) + " peak " + QString::number(peak)));
            qInfo().noquote() << eventId(Event(i)) << QString::number(seconds, 'f', 3) + " s" << w.size() << "bytes" << u32(w, 24) << "Hz" << u16(w, 22) << "ch";
        }
        // A second run finds everything current and does not decode again.
        Cache again(root, cache.dir());
        again.prepare();
        QCOMPARE(again.state(), Cache::State::Ready);
    }
};

QTEST_MAIN(MinecraftSoundsTest)

#include "MinecraftSounds_test.moc"
