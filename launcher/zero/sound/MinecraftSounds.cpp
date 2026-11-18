// SPDX-License-Identifier: GPL-3.0-only
#include "MinecraftSounds.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QtEndian>
#include <algorithm>
#include <cmath>

namespace Zero {
namespace Sound {
namespace Minecraft {

EventSpec specFor(Event event)
{
    const QString click = QStringLiteral("minecraft/sounds/random/click.ogg");
    const QString wood = QStringLiteral("minecraft/sounds/random/wood_click.ogg");
    switch (event) {
        case Event::Click:
            return { { click }, 1.0 };
        case Event::ToggleOn:
            return { { wood }, 1.0 };
        case Event::ToggleOff:
            return { { wood }, 0.8 };
        case Event::Navigate:
            return { { QStringLiteral("minecraft/sounds/item/book/open_flip1.ogg"), click }, 1.0 };
        case Event::Success:
            return { { QStringLiteral("minecraft/sounds/random/orb.ogg") }, 1.0 };
        case Event::Error:
            return { { QStringLiteral("minecraft/sounds/mob/villager/no1.ogg"), QStringLiteral("minecraft/sounds/note/bass.ogg") }, 1.0 };
        case Event::Launch:
            return { { QStringLiteral("minecraft/sounds/random/levelup.ogg") }, 1.0 };
    }
    return {};
}

namespace {

struct IndexFile {
    QString name;
    QString path;
    int number = -1;
    QDateTime modified;
    bool loaded = false;
    QJsonObject objects;
};

QList<IndexFile> listIndexes(const QString& assetsRoot)
{
    QList<IndexFile> out;
    const QFileInfoList files = QDir(assetsRoot + QStringLiteral("/indexes")).entryInfoList({ QStringLiteral("*.json") }, QDir::Files);
    for (const QFileInfo& fi : files) {
        IndexFile f;
        f.name = fi.completeBaseName();
        f.path = fi.absoluteFilePath();
        bool ok = false;
        const int n = f.name.toInt(&ok);
        f.number = ok ? n : -1;
        f.modified = fi.lastModified();
        out.append(f);
    }
    std::sort(out.begin(), out.end(), [](const IndexFile& a, const IndexFile& b) {
        if (a.number != b.number)
            return a.number > b.number;
        return a.modified > b.modified;
    });
    return out;
}

const QJsonObject& objectsOf(IndexFile& f)
{
    if (!f.loaded) {
        f.loaded = true;
        QFile file(f.path);
        if (file.open(QIODevice::ReadOnly))
            f.objects = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("objects")).toObject();
    }
    return f.objects;
}

std::optional<Asset> resolveIn(const QString& assetsRoot, QList<IndexFile>& indexes, const QStringList& keys)
{
    for (const QString& key : keys) {
        for (IndexFile& idx : indexes) {
            const QJsonObject entry = objectsOf(idx).value(key).toObject();
            const QString hash = entry.value(QStringLiteral("hash")).toString();
            const qint64 size = qint64(entry.value(QStringLiteral("size")).toDouble(-1));
            if (hash.size() < 2 || size <= 0)
                continue;
            const QString path = assetsRoot + QStringLiteral("/objects/") + hash.left(2) + QLatin1Char('/') + hash;
            const QFileInfo fi(path);
            if (!fi.isFile() || fi.size() != size)
                continue;
            return Asset{ key, path, hash, size, idx.name };
        }
    }
    return std::nullopt;
}

}  // namespace

std::optional<Asset> resolve(const QString& assetsRoot, const QStringList& keys)
{
    QList<IndexFile> indexes = listIndexes(assetsRoot);
    return resolveIn(assetsRoot, indexes, keys);
}

std::optional<QList<Asset>> resolveAll(const QString& assetsRoot)
{
    QList<IndexFile> indexes = listIndexes(assetsRoot);
    QList<Asset> out;
    for (int i = 0; i < kEventCount; ++i) {
        auto a = resolveIn(assetsRoot, indexes, specFor(Event(i)).keys);
        if (!a)
            return std::nullopt;
        out.append(*a);
    }
    return out;
}

bool available(const QString& assetsRoot)
{
    return resolveAll(assetsRoot).has_value();
}

QJsonObject manifestFor(const QList<Asset>& assets)
{
    QJsonObject events;
    for (int i = 0; i < assets.size() && i < kEventCount; ++i) {
        QJsonObject e;
        e.insert(QStringLiteral("key"), assets[i].key);
        e.insert(QStringLiteral("hash"), assets[i].hash);
        e.insert(QStringLiteral("size"), double(assets[i].size));
        e.insert(QStringLiteral("rateScale"), specFor(Event(i)).rateScale);
        events.insert(eventId(Event(i)), e);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("events"), events);
    return root;
}

Pcm conditioned(const Pcm& in, double maxSeconds, double peak)
{
    Pcm out = in;
    if (out.sampleRate <= 0 || out.channels <= 0)
        return out;
    const qsizetype maxFrames = qsizetype(std::floor(maxSeconds * out.sampleRate));
    const qsizetype maxSamples = maxFrames * out.channels;
    if (out.samples.size() > maxSamples) {
        out.samples.resize(maxSamples);
        const qsizetype fade = std::min<qsizetype>(maxFrames, qsizetype(0.02 * out.sampleRate));
        for (qsizetype f = 0; f < fade; ++f) {
            const double g = double(fade - f) / double(fade);
            const qsizetype frame = maxFrames - fade + f;
            for (int c = 0; c < out.channels; ++c) {
                qint16& s = out.samples[frame * out.channels + c];
                s = qint16(std::lround(s * g));
            }
        }
    }
    int top = 0;
    for (qint16 s : std::as_const(out.samples))
        top = std::max(top, std::abs(int(s)));
    if (top > 0) {
        const double k = peak * 32767.0 / top;
        for (qint16& s : out.samples)
            s = qint16(std::clamp<long>(std::lround(s * k), -32767, 32767));
    }
    return out;
}

QByteArray toWav(const Pcm& pcm, double rateScale)
{
    const quint32 rate = quint32(std::max<long>(1, std::lround(pcm.sampleRate * rateScale)));
    const quint16 channels = quint16(pcm.channels);
    const quint32 dataBytes = quint32(pcm.samples.size()) * 2;
    QByteArray b;
    b.reserve(44 + int(dataBytes));
    const auto u32 = [&b](quint32 v) {
        char t[4];
        qToLittleEndian(v, t);
        b.append(t, 4);
    };
    const auto u16 = [&b](quint16 v) {
        char t[2];
        qToLittleEndian(v, t);
        b.append(t, 2);
    };
    b.append("RIFF", 4);
    u32(36 + dataBytes);
    b.append("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(channels);
    u32(rate);
    u32(rate * channels * 2);
    u16(quint16(channels * 2));
    u16(16);
    b.append("data", 4);
    u32(dataBytes);
    for (qint16 s : pcm.samples) {
        char t[2];
        qToLittleEndian(s, t);
        b.append(t, 2);
    }
    return b;
}

}  // namespace Minecraft
}  // namespace Sound
}  // namespace Zero
