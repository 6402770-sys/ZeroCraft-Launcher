// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroVideoBackground.h"

#include <QAudioOutput>
#include <QDebug>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoSink>
#include <QtMath>

namespace Zero {

namespace {

/// QVideoFrame::toImage() has no conversion for 10-bit planar YUV (Format_YUV420P10: what software decoding of H.264 High 10, HEVC
/// Main 10, VP9 profile 2 and AV1 10-bit produces), so those videos decoded fine but never showed a picture. Converts such a frame
/// ourselves (10 -> 8 bit, BT.601/709/2020, limited or full range). Frames wider than 2560 px are decimated 2x: this is a window background.
QImage yuv420p10ToImage(QVideoFrame frame)
{
    const QVideoFrameFormat format = frame.surfaceFormat();
    if (!frame.map(QVideoFrame::ReadOnly))
        return {};
    const int w = frame.width(), h = frame.height();
    const int step = w > 2560 ? 2 : 1;
    QImage image(w / step, h / step, QImage::Format_RGB32);
    if (image.isNull() || frame.planeCount() < 3 || !frame.bits(0) || !frame.bits(1) || !frame.bits(2)) {
        frame.unmap();
        return {};
    }

    double kr = 0.2126, kb = 0.0722;  // BT.709 (also the guess for HD content with no color space)
    bool full = format.colorRange() == QVideoFrameFormat::ColorRange_Full;
    switch (format.colorSpace()) {
        case QVideoFrameFormat::ColorSpace_BT601:
            kr = 0.299, kb = 0.114;
            break;
        case QVideoFrameFormat::ColorSpace_BT2020:
            kr = 0.2627, kb = 0.0593;
            break;
        case QVideoFrameFormat::ColorSpace_Undefined:
            if (h < 720)
                kr = 0.299, kb = 0.114;
            break;
        default:
            break;
    }
    const double kg = 1.0 - kr - kb;
    const double yScale = full ? 1.0 : 255.0 / 219.0, cScale = full ? 1.0 : 255.0 / 224.0;
    constexpr int Q = 16;
    const int yMul = int(yScale * (1 << Q)), rV = int(2 * (1 - kr) * cScale * (1 << Q)), bU = int(2 * (1 - kb) * cScale * (1 << Q)),
              gU = int(2 * (1 - kb) * kb / kg * cScale * (1 << Q)), gV = int(2 * (1 - kr) * kr / kg * cScale * (1 << Q));
    const int yOff = full ? 0 : 16;
    const auto clamp8 = [](int v) { return uchar(v < 0 ? 0 : (v > 255 ? 255 : v)); };

    const int strideY = frame.bytesPerLine(0), strideU = frame.bytesPerLine(1), strideV = frame.bytesPerLine(2);
    for (int y = 0; y < image.height(); ++y) {
        const uchar* rowY = frame.bits(0) + qsizetype(y) * step * strideY;
        const uchar* rowU = frame.bits(1) + qsizetype(y) * step / 2 * strideU;
        const uchar* rowV = frame.bits(2) + qsizetype(y) * step / 2 * strideV;
        QRgb* out = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const int sx = x * step;
            const int yy = ((int(rowY[sx * 2]) | (int(rowY[sx * 2 + 1]) << 8)) >> 2) - yOff;  // 10-bit little endian -> 8 bit
            const int u = ((int(rowU[(sx / 2) * 2]) | (int(rowU[(sx / 2) * 2 + 1]) << 8)) >> 2) - 128;
            const int v = ((int(rowV[(sx / 2) * 2]) | (int(rowV[(sx / 2) * 2 + 1]) << 8)) >> 2) - 128;
            const int base = yy * yMul + (1 << (Q - 1));
            out[x] = qRgb(clamp8((base + rV * v) >> Q), clamp8((base - gU * u - gV * v) >> Q), clamp8((base + bU * u) >> Q));
        }
    }
    frame.unmap();
    return image;
}

constexpr int kMaxNullFrames = 15;  // about half a second of frames that cannot be shown: the video is given up instead of staying blank

}  // namespace

bool ZeroVideoBackground::shouldPlay(const QString& pageId, bool minimized, bool gameRunning, bool motionOff)
{
    const bool onVideoPage = pageId == QLatin1String("home") || pageId == QLatin1String("appearance");
    return onVideoPage && !minimized && !gameRunning && !motionOff;
}

QRect ZeroVideoBackground::coverRect(const QSize& source, const QSize& target)
{
    if (source.isEmpty() || target.isEmpty())
        return {};
    const qreal scale = qMax(qreal(target.width()) / source.width(), qreal(target.height()) / source.height());
    const QSize crop(qMin(source.width(), qCeil(target.width() / scale)), qMin(source.height(), qCeil(target.height() / scale)));
    return QRect(QPoint((source.width() - crop.width()) / 2, (source.height() - crop.height()) / 2), crop);
}

ZeroVideoBackground::ZeroVideoBackground(QObject* parent) : QObject(parent) {}

ZeroVideoBackground::~ZeroVideoBackground()
{
    if (m_player) {
        m_player->disconnect(this);
        m_player->stop();
        m_player->setSource(QUrl());
    }
}

void ZeroVideoBackground::release()
{
    if (m_player) {
        m_player->disconnect(this);
        m_sink->disconnect(this);
        m_player->stop();
        m_player->setVideoSink(nullptr);
        m_player->setAudioOutput(nullptr);
        if (m_audio) {
            m_audio->deleteLater();
            m_audio = nullptr;
        }
        m_player->setSource(QUrl());  // lets go of the file and the decoder
        m_player->deleteLater();
        m_sink->deleteLater();
        m_player = nullptr;
        m_sink = nullptr;
    }
    m_playing = false;
    m_settleOnFirstFrame = false;
    m_nullFrames = 0;
    m_frame = QImage();
}

void ZeroVideoBackground::setSource(const QString& path)
{
    const qint64 stamp = path.isEmpty() ? 0 : QFileInfo(path).lastModified().toMSecsSinceEpoch();
    if (path == m_path && stamp == m_stamp)
        return;  // already open (or already known to be broken: not retried on every theme change)
    release();
    m_path = path;
    m_stamp = stamp;
    m_failed = false;
    m_warned = false;
    if (path.isEmpty()) {
        emit frameChanged();
        return;
    }
    if (!QFileInfo(path).isFile()) {
        fail(QStringLiteral("the file is missing"));
        return;
    }

    m_player = new QMediaPlayer(this);  // silent by construction: a QAudioOutput only comes with the theme's sound switch (applyAudio)
    m_sink = new QVideoSink(this);
    m_player->setVideoSink(m_sink);
    m_player->setLoops(QMediaPlayer::Infinite);
    connect(m_sink, &QVideoSink::videoFrameChanged, this, [this]() { onFrame(); });
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString& text) { fail(text); });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::InvalidMedia)
            fail(QStringLiteral("the media is not valid"));
    });
    m_player->setSource(QUrl::fromLocalFile(path));
    apply();
    applyAudio();
}

void ZeroVideoBackground::setSound(bool on, double volume)
{
    m_sound = on;
    m_volume = qBound(0.0, volume, 1.0);
    applyAudio();
}

void ZeroVideoBackground::applyAudio()
{
    if (!m_player)
        return;
    if (m_sound && !m_audio) {
        m_audio = new QAudioOutput(this);
        m_player->setAudioOutput(m_audio);
    } else if (!m_sound && m_audio) {
        m_player->setAudioOutput(nullptr);
        delete m_audio;
        m_audio = nullptr;
    }
    if (m_audio) {
        m_audio->setVolume(float(m_volume));
        m_audio->setMuted(!audible(m_sound, m_wantPlay));  // a paused player is silent anyway; this also covers the first-frame settle
    }
}

void ZeroVideoBackground::setConditions(const QString& pageId, bool minimized, bool gameRunning, bool motionOff)
{
    m_page = pageId;
    m_minimized = minimized;
    m_gameRunning = gameRunning;
    m_motionOff = motionOff;
    const bool want = shouldPlay(pageId, minimized, gameRunning, motionOff);
    if (want == m_wantPlay)
        return;
    m_wantPlay = want;
    apply();
}

void ZeroVideoBackground::apply()
{
    if (!m_player)
        return;
    if (m_wantPlay) {
        m_settleOnFirstFrame = false;
        if (m_player->playbackState() != QMediaPlayer::PlayingState)
            m_player->play();
    } else if (m_frame.isNull()) {
        // No picture to keep yet: run until the first frame arrives, then pause on it (onFrame).
        m_settleOnFirstFrame = true;
        if (m_player->playbackState() != QMediaPlayer::PlayingState)
            m_player->play();
    } else if (m_player->playbackState() == QMediaPlayer::PlayingState) {
        m_player->pause();
    }
    applyAudio();
    const bool nowPlaying = m_wantPlay;
    if (nowPlaying != m_playing) {
        m_playing = nowPlaying;
        qDebug().noquote() << "ZeroVideoBackground:" << (m_playing ? "play" : "pause") << "(page" << m_page << ", minimized" << m_minimized
                           << ", game running" << m_gameRunning << ", motion off" << m_motionOff << ")";
    }
}

void ZeroVideoBackground::onFrame()
{
    if (!m_sink || m_failed)
        return;
    const QVideoFrame videoFrame = m_sink->videoFrame();
    if (!videoFrame.isValid())
        return;
    QImage image = videoFrame.toImage();  // the one image allocated per frame
    if (image.isNull() && videoFrame.pixelFormat() == QVideoFrameFormat::Format_YUV420P10)
        image = yuv420p10ToImage(videoFrame);  // Qt cannot convert 10-bit planar YUV itself
    if (image.isNull()) {
        // Decoded but not showable (a pixel format neither Qt nor we convert): say so instead of staying blank without a trace.
        if (++m_nullFrames >= kMaxNullFrames && m_frame.isNull())
            fail(QStringLiteral("the video decodes to a pixel format that cannot be shown (%1)").arg(QVideoFrameFormat::pixelFormatToString(videoFrame.pixelFormat())));
        return;
    }
    m_nullFrames = 0;
    m_frame = std::move(image);
    if (m_settleOnFirstFrame && !m_wantPlay) {
        m_settleOnFirstFrame = false;
        m_player->pause();  // the first frame stays as the still
    }
    emit frameChanged();
}

void ZeroVideoBackground::fail(const QString& reason)
{
    if (m_failed)
        return;
    m_failed = true;
    if (!m_warned) {
        m_warned = true;
        qWarning().noquote() << "ZeroVideoBackground: cannot play" << m_path << "-" << reason << "(showing the flat background)";
    }
    const QString path = m_path;
    const qint64 stamp = m_stamp;
    release();
    m_path = path;  // remembered, so the same broken file is not retried on every theme change
    m_stamp = stamp;
    emit frameChanged();
}

}  // namespace Zero
