// SPDX-License-Identifier: GPL-3.0-only
#include "MinecraftSoundCache.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QPointer>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <memory>

namespace Zero {
namespace Sound {
namespace Minecraft {

namespace {

constexpr int kDecodeTimeoutMs = 15000;

/// Appends the samples of `buf` (any sample format) to `out` as 16-bit.
void appendSamples(const QAudioBuffer& buf, Pcm& out)
{
    const int n = buf.sampleCount();
    switch (buf.format().sampleFormat()) {
        case QAudioFormat::Int16: {
            const qint16* d = buf.constData<qint16>();
            out.samples.append(QVector<qint16>(d, d + n));
            break;
        }
        case QAudioFormat::UInt8: {
            const quint8* d = buf.constData<quint8>();
            for (int i = 0; i < n; ++i)
                out.samples.append(qint16((int(d[i]) - 128) << 8));
            break;
        }
        case QAudioFormat::Int32: {
            const qint32* d = buf.constData<qint32>();
            for (int i = 0; i < n; ++i)
                out.samples.append(qint16(d[i] >> 16));
            break;
        }
        case QAudioFormat::Float: {
            const float* d = buf.constData<float>();
            for (int i = 0; i < n; ++i)
                out.samples.append(qint16(std::lround(std::clamp(d[i], -1.0f, 1.0f) * 32767.0f)));
            break;
        }
        default:
            break;
    }
}

}  // namespace

void decodeFile(QObject* parent, const QString& path, std::function<void(bool, Pcm)> done)
{
    struct State {
        Pcm pcm;
        bool finished = false;
        std::function<void(bool, Pcm)> done;
    };
    auto st = std::make_shared<State>();
    st->done = std::move(done);
    auto* decoder = new QAudioDecoder(parent);
    auto* timeout = new QTimer(decoder);
    timeout->setSingleShot(true);

    const auto complete = [st, decoder](bool ok) {
        if (st->finished)
            return;
        st->finished = true;
        decoder->stop();
        auto cb = std::move(st->done);
        Pcm pcm = std::move(st->pcm);
        decoder->deleteLater();
        // Decided before the call: the moved-into argument may be built before the other one is evaluated.
        const bool good = ok && pcm.sampleRate > 0 && pcm.channels > 0 && !pcm.samples.isEmpty();
        if (!good)
            qWarning() << "Minecraft sounds: no audio decoded" << ok << pcm.sampleRate << pcm.channels << pcm.samples.size();
        if (cb)
            cb(good, std::move(pcm));
    };

    // No output format is forced: an incomplete one (no rate / channels) makes the FFmpeg backend fail; appendSamples converts.
    QObject::connect(decoder, &QAudioDecoder::bufferReady, decoder, [st, decoder]() {
        const QAudioBuffer buf = decoder->read();
        if (!buf.isValid())
            return;
        if (st->pcm.sampleRate == 0) {
            st->pcm.sampleRate = buf.format().sampleRate();
            st->pcm.channels = buf.format().channelCount();
        }
        appendSamples(buf, st->pcm);
    });
    QObject::connect(decoder, &QAudioDecoder::finished, decoder, [complete]() { complete(true); });
    QObject::connect(decoder, QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error), decoder, [complete, decoder]() {
        qWarning() << "Minecraft sounds: decoding failed:" << decoder->errorString();
        complete(false);
    });
    QObject::connect(timeout, &QTimer::timeout, decoder, [complete]() { complete(false); });
    decoder->setSource(QUrl::fromLocalFile(path));
    timeout->start(kDecodeTimeoutMs);
    decoder->start();
}

Cache::Cache(QString assetsRoot, QString cacheDir, QObject* parent) : QObject(parent), m_assetsRoot(std::move(assetsRoot)), m_dir(std::move(cacheDir)) {}

void Cache::refresh()
{
    const bool now = Minecraft::available(m_assetsRoot);
    if (now != m_available) {
        m_available = now;
        emit changed();
    }
}

void Cache::setState(State s)
{
    if (s == m_state)
        return;
    m_state = s;
    emit changed();
}

bool Cache::isCurrent(const QString& dir, const QList<Asset>& assets)
{
    QFile f(dir + QStringLiteral("/manifest.json"));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    if (QJsonDocument::fromJson(f.readAll()).object() != manifestFor(assets))
        return false;
    for (int i = 0; i < kEventCount; ++i)
        if (!QFile::exists(dir + QLatin1Char('/') + eventId(Event(i)) + QStringLiteral(".wav")))
            return false;
    return true;
}

bool Cache::writeEvent(const QString& dir, Event event, const Pcm& decoded)
{
    if (!QDir().mkpath(dir))
        return false;
    const QByteArray wav = toWav(conditioned(decoded), specFor(event).rateScale);
    QSaveFile out(dir + QLatin1Char('/') + eventId(event) + QStringLiteral(".wav"));
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(wav);
    return out.commit();
}

bool Cache::writeManifest(const QString& dir, const QList<Asset>& assets)
{
    if (!QDir().mkpath(dir))
        return false;
    QSaveFile out(dir + QStringLiteral("/manifest.json"));
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(QJsonDocument(manifestFor(assets)).toJson(QJsonDocument::Indented));
    return out.commit();
}

void Cache::prepare()
{
    if (m_state == State::Preparing)
        return;
    const auto assets = resolveAll(m_assetsRoot);
    if (!assets) {
        m_available = false;
        if (m_state == State::Unavailable)
            emit changed();  // availability may have changed back and forth; callers re-read
        setState(State::Unavailable);
        return;
    }
    m_available = true;
    m_assets = *assets;
    if (isCurrent(m_dir, m_assets)) {
        if (m_state == State::Ready)
            return;
        setState(State::Ready);
        return;
    }
    ++m_generation;
    m_decoded.clear();
    m_pendingKeys.clear();
    for (const Asset& a : std::as_const(m_assets))
        if (!m_pendingKeys.contains(a.key))
            m_pendingKeys.append(a.key);
    setState(State::Preparing);
    decodeNext();
}

void Cache::decodeNext()
{
    if (m_pendingKeys.isEmpty()) {
        commit();
        return;
    }
    const QString key = m_pendingKeys.takeFirst();
    QString path;
    for (const Asset& a : std::as_const(m_assets))
        if (a.key == key)
            path = a.objectPath;
    const int gen = m_generation;
    QPointer<Cache> self(this);
    decodeFile(this, path, [self, gen, key](bool ok, Pcm pcm) {
        if (!self || self->m_generation != gen)
            return;
        if (!ok) {
            qWarning() << "Minecraft sounds: could not decode" << key;
            self->fail();
            return;
        }
        self->m_decoded.insert(key, std::move(pcm));
        self->decodeNext();
    });
}

void Cache::commit()
{
    for (int i = 0; i < kEventCount; ++i) {
        const Pcm& pcm = m_decoded.value(m_assets[i].key);
        if (!writeEvent(m_dir, Event(i), pcm)) {
            qWarning() << "Minecraft sounds: could not write" << eventId(Event(i)) << "to" << m_dir;
            fail();
            return;
        }
    }
    if (!writeManifest(m_dir, m_assets)) {
        qWarning() << "Minecraft sounds: could not write the manifest in" << m_dir;  // last: a half-written cache is never taken for a finished one
        fail();
        return;
    }
    m_decoded.clear();
    setState(State::Ready);
}

void Cache::fail()
{
    ++m_generation;
    m_decoded.clear();
    m_pendingKeys.clear();
    setState(State::Unavailable);
}

}  // namespace Minecraft
}  // namespace Sound
}  // namespace Zero
