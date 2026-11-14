// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the video window background of a custom theme: a looping QMediaPlayer feeding a QVideoSink. Owned by
// ZeroShell, which paints the current frame (cover crop + dim) behind the transparent top bar and pages. No QVideoWidget. Silent
// unless the theme turns the sound on: only then a QAudioOutput exists, and it is muted whenever the video is not playing. The
// player only exists while a video theme is active, and only plays while it is worth the CPU: see shouldPlay().
#pragma once

#include <QAudioOutput>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>

class QAudioOutput;
class QMediaPlayer;
class QVideoSink;

namespace Zero {

class ZeroVideoBackground : public QObject {
    Q_OBJECT
   public:
    /// Pure play/pause rule: the video plays on the Home and Appearance pages only, never while the window is minimized or a game
    /// is running, and never with motion off. In every other case it is paused and the last frame stays on screen as a still.
    static bool shouldPlay(const QString& pageId, bool minimized, bool gameRunning, bool motionOff);

    /// Pure sound rule: the video's sound is heard only while the theme asked for it AND the video is playing (the play/pause rule above
    /// decides that: paused on another page, minimized, a game running or motion off = silent).
    static bool audible(bool sound, bool playing) { return sound && playing; }

    /// The source rectangle of an image of `source` pixels that covers `target` (scaled until both sides are filled, the overflow
    /// cropped evenly, centered). Empty for an empty size.
    static QRect coverRect(const QSize& source, const QSize& target);

    explicit ZeroVideoBackground(QObject* parent = nullptr);
    ~ZeroVideoBackground() override;

    /// Uses the video file at `path`; empty stops and releases the player (no decoder is left running). The same path with the same
    /// modification time is a no-op. A new source follows shouldPlay(); when that says pause it still decodes the first frame
    /// and stays on it as the still.
    void setSource(const QString& path);
    QString source() const { return m_path; }

    /// The theme's sound switch and volume (0..1). Off creates no QAudioOutput at all (and destroys an existing one); on creates it
    /// while a player exists. Applies live.
    void setSound(bool on, double volume);
    bool soundOn() const { return m_sound; }
    double volume() const { return m_volume; }
    /// A QAudioOutput exists (only with the sound on and a player).
    bool hasAudioOutput() const { return m_audio != nullptr; }
    QAudioOutput* audioOutput() const { return m_audio; }
    /// The audio output is currently unmuted: audible(sound, playing).
    bool isAudible() const { return m_audio && !m_audio->isMuted(); }

    /// The play/pause inputs; the player follows shouldPlay() of them.
    void setConditions(const QString& pageId, bool minimized, bool gameRunning, bool motionOff);

    /// The latest frame (null until the first one arrives, or when the video could not be played).
    const QImage& frame() const { return m_frame; }
    /// The video could not be loaded or decoded: callers paint the flat background instead.
    bool failed() const { return m_failed; }
    /// The player is running (not paused / stopped).
    bool isPlaying() const { return m_playing; }
    /// A player (decoder) exists: false for no source and for a failed one.
    bool hasPlayer() const { return m_player != nullptr; }
    /// What the rule currently asks for.
    bool wantsPlay() const { return m_wantPlay; }

   signals:
    /// A new frame is ready (or the video failed / was released): repaint.
    void frameChanged();

   private:
    void release();
    void apply();
    void applyAudio();
    void onFrame();
    void fail(const QString& reason);

    QMediaPlayer* m_player = nullptr;
    QVideoSink* m_sink = nullptr;
    QAudioOutput* m_audio = nullptr;
    bool m_sound = false;
    double m_volume = 0.5;
    QString m_path;
    qint64 m_stamp = 0;  // modification time of m_path when it was opened
    QImage m_frame;
    bool m_failed = false;
    bool m_wantPlay = false;
    bool m_playing = false;
    bool m_settleOnFirstFrame = false;  // a paused start: play until the first frame arrives, then pause on it
    int m_nullFrames = 0;  // consecutive decoded frames that could not be turned into an image (reset by every picture)
    bool m_warned = false;
    QString m_page;
    bool m_minimized = false;
    bool m_gameRunning = false;
    bool m_motionOff = false;
};

}  // namespace Zero
