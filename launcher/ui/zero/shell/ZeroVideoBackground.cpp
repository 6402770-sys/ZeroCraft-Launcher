// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroVideoBackground.h"

#include <QAudioOutput>
#include <QDebug>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>
#include <QtMath>

namespace Zero {

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
    if (image.isNull())
        return;
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
