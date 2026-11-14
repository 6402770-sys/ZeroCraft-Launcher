// SPDX-License-Identifier: GPL-3.0-only
// The video window background: the pure play/pause rule and cover crop (no media needed), and - when the Qt ffmpeg backend and an
// ffmpeg executable are available - a real clip: first-frame still when paused, frames while playing, release, failure fallback,
// and ZeroShell painting the frame behind the pages; the optional sound (no audio output unless the theme asks for it, muted whenever the
// video is paused).
#include <QApplication>
#include <QFile>
#include <QImage>
#include <QMediaPlayer>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroTokens.h"
#include "ui/zero/shell/ShellPage.h"
#include "ui/zero/shell/ZeroShell.h"
#include "ui/zero/shell/ZeroVideoBackground.h"

using namespace Zero;

namespace {

class StubPage : public ShellPage {
   public:
    explicit StubPage(const QString& title) : m_title(title) {}
    QString title() const override { return m_title; }

   private:
    QString m_title;
};

/// Finds ffmpeg next to the MSYS2 Qt (the dev machine) or on PATH.
QString findFfmpeg()
{
    QString exe = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (exe.isEmpty())
        exe = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"), { QStringLiteral("C:/msys64/ucrt64/bin") });
    return exe;
}

}  // namespace

class ZeroVideoBackgroundTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_clip;
    QString m_skipReason;

    bool needClip()
    {
        if (!m_skipReason.isEmpty())
            QTest::qSkip(qPrintable(m_skipReason), __FILE__, __LINE__);
        return m_skipReason.isEmpty();
    }

   private slots:
    void initTestCase()
    {
        ThemeSpec spec = ThemeSpec::obsidian();
        spec.reduceMotion = false;
        Theme::setCurrent(spec);

        QMediaPlayer probe;
        if (!probe.isAvailable()) {
            m_skipReason = QStringLiteral("no Qt Multimedia backend available (the ffmpeg media plugin is missing)");
            return;
        }
        const QString ffmpeg = findFfmpeg();
        if (ffmpeg.isEmpty()) {
            m_skipReason = QStringLiteral("no ffmpeg executable to generate a test clip");
            return;
        }
        m_clip = m_dir.filePath(QStringLiteral("clip.mp4"));
        QProcess p;
        p.start(ffmpeg, { QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                          QStringLiteral("testsrc2=size=128x72:rate=20"), QStringLiteral("-t"), QStringLiteral("3"), QStringLiteral("-pix_fmt"),
                          QStringLiteral("yuv420p"), m_clip });
        if (!p.waitForFinished(30000) || p.exitCode() != 0 || !QFile::exists(m_clip))
            m_skipReason = QStringLiteral("ffmpeg could not generate the test clip");
    }

    // ---- pure logic -------------------------------------------------------------------------------------------------

    void shouldPlayTruthTable()
    {
        const QStringList pages = { QStringLiteral("home"), QStringLiteral("appearance"), QStringLiteral("library"), QStringLiteral("tools"),
                                    QStringLiteral("settings"), QStringLiteral("discover"), QString() };
        int plays = 0;
        for (const QString& page : pages) {
            for (int bits = 0; bits < 8; ++bits) {
                const bool minimized = bits & 1, game = bits & 2, motionOff = bits & 4;
                const bool expected = (page == QLatin1String("home") || page == QLatin1String("appearance")) && !minimized && !game && !motionOff;
                QCOMPARE(ZeroVideoBackground::shouldPlay(page, minimized, game, motionOff), expected);
                plays += expected;
            }
        }
        QCOMPARE(plays, 2);  // exactly home and appearance with everything else clear
    }

    void coverRectCropsCentered()
    {
        // wide source, tall target: crop the sides evenly
        QCOMPARE(ZeroVideoBackground::coverRect(QSize(200, 100), QSize(100, 100)), QRect(50, 0, 100, 100));
        // tall source, wide target: crop top and bottom
        QCOMPARE(ZeroVideoBackground::coverRect(QSize(100, 200), QSize(100, 100)), QRect(0, 50, 100, 100));
        // same aspect: the whole picture
        QCOMPARE(ZeroVideoBackground::coverRect(QSize(1280, 720), QSize(640, 360)), QRect(0, 0, 1280, 720));
        // a target larger than the source never reaches outside it
        const QRect big = ZeroVideoBackground::coverRect(QSize(100, 50), QSize(1000, 1000));
        QVERIFY(QRect(0, 0, 100, 50).contains(big));
        QVERIFY(ZeroVideoBackground::coverRect(QSize(), QSize(10, 10)).isEmpty());
        QVERIFY(ZeroVideoBackground::coverRect(QSize(10, 10), QSize()).isEmpty());
    }

    // ---- the player ---------------------------------------------------------------------------------------------------

    void missingFileFailsSilentlyAndNeverHoldsAPlayer()
    {
        ZeroVideoBackground v;
        QSignalSpy frames(&v, &ZeroVideoBackground::frameChanged);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("ZeroVideoBackground: cannot play")));
        v.setSource(m_dir.filePath(QStringLiteral("not-there.mp4")));
        QVERIFY(v.failed());
        QVERIFY(!v.hasPlayer());
        QVERIFY(v.frame().isNull());
        QVERIFY(frames.count() >= 1);  // the shell repaints with the flat background
        v.setSource(m_dir.filePath(QStringLiteral("not-there.mp4")));  // same path again: not retried, not warned again
        v.setSource(QString());
        QVERIFY(!v.failed());
    }

    void startsPausedOnTheFirstFrameWhenThePageIsNotAVideoPage()
    {
        if (!needClip())
            return;
        ZeroVideoBackground v;
        v.setConditions(QStringLiteral("library"), false, false, false);
        v.setSource(m_clip);
        QVERIFY(v.hasPlayer());
        QVERIFY(!v.wantsPlay());
        QTRY_VERIFY_WITH_TIMEOUT(!v.frame().isNull(), 15000);
        QCOMPARE(v.frame().size(), QSize(128, 72));
        QVERIFY(!v.isPlaying());
        // paused on the still: no more frames arrive
        QTest::qWait(300);
        QSignalSpy frames(&v, &ZeroVideoBackground::frameChanged);
        QTest::qWait(700);
        QCOMPARE(frames.count(), 0);
    }

    void playsAndPausesWithTheConditions()
    {
        if (!needClip())
            return;
        ZeroVideoBackground v;
        v.setConditions(QStringLiteral("home"), false, false, false);
        v.setSource(m_clip);
        QSignalSpy frames(&v, &ZeroVideoBackground::frameChanged);
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= 5, 15000);  // plays
        QVERIFY(v.isPlaying());

        for (int which = 0; which < 3; ++which) {
            if (which == 0)
                v.setConditions(QStringLiteral("library"), false, false, false);  // another page
            else if (which == 1)
                v.setConditions(QStringLiteral("home"), true, false, false);  // minimized
            else
                v.setConditions(QStringLiteral("home"), false, true, false);  // a game is running
            QVERIFY(!v.isPlaying());
            QTest::qWait(300);  // let a frame in flight land
            const int settled = int(frames.count());
            QTest::qWait(600);
            QCOMPARE(frames.count(), settled);  // paused: the last frame stays
            QVERIFY(!v.frame().isNull());

            v.setConditions(QStringLiteral("home"), false, false, false);  // back: plays again
            const int before = int(frames.count());
            QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= before + 3, 10000);
            QVERIFY(v.isPlaying());
        }

        v.setConditions(QStringLiteral("home"), false, false, true);  // motion off
        QVERIFY(!v.isPlaying());
    }

    // ---- sound ----------------------------------------------------------------------------------------------------------

    void soundIsAudibleOnlyWhileTheVideoPlays()
    {
        // The sound follows the play rule: it is exactly "sound on AND shouldPlay(...)" for every input.
        const QStringList pages = { QStringLiteral("home"), QStringLiteral("appearance"), QStringLiteral("library"), QStringLiteral("tools"), QString() };
        int audible = 0;
        for (const QString& page : pages) {
            for (int bits = 0; bits < 8; ++bits) {
                const bool minimized = bits & 1, game = bits & 2, motionOff = bits & 4;
                const bool plays = ZeroVideoBackground::shouldPlay(page, minimized, game, motionOff);
                QCOMPARE(ZeroVideoBackground::audible(true, plays), plays);
                QVERIFY(!ZeroVideoBackground::audible(false, plays));  // sound off: never
                audible += ZeroVideoBackground::audible(true, plays);
            }
        }
        QCOMPARE(audible, 2);
        QVERIFY(!ZeroVideoBackground::audible(true, false));
    }

    void soundOffCreatesNoAudioOutput()
    {
        if (!needClip())
            return;
        ZeroVideoBackground v;
        QVERIFY(!v.soundOn());
        v.setConditions(QStringLiteral("home"), false, false, false);
        v.setSource(m_clip);
        QVERIFY(v.hasPlayer());
        QVERIFY(!v.hasAudioOutput());  // the default: muted by construction
        QVERIFY(v.audioOutput() == nullptr);
        v.setSound(false, 0.9);
        QVERIFY(!v.hasAudioOutput());
        QVERIFY(!v.isAudible());
    }

    void soundOnCreatesTheOutputAndItFollowsThePlayRule()
    {
        if (!needClip())
            return;
        ZeroVideoBackground v;
        v.setSound(true, 0.4);  // before the source: the player gets its output right away
        v.setConditions(QStringLiteral("library"), false, false, false);
        v.setSource(m_clip);
        QVERIFY(v.hasPlayer());
        QVERIFY(v.hasAudioOutput());
        QCOMPARE(v.audioOutput()->volume(), 0.4f);
        QVERIFY(!v.isAudible());  // another page: the video is paused, so the sound is off

        v.setConditions(QStringLiteral("home"), false, false, false);
        QVERIFY(v.isAudible());
        QVERIFY(!v.audioOutput()->isMuted());
        v.setConditions(QStringLiteral("home"), true, false, false);  // minimized
        QVERIFY(!v.isAudible());
        v.setConditions(QStringLiteral("appearance"), false, false, false);
        QVERIFY(v.isAudible());
        v.setConditions(QStringLiteral("appearance"), false, true, false);  // a game is running
        QVERIFY(!v.isAudible());
        v.setConditions(QStringLiteral("home"), false, false, true);  // motion off
        QVERIFY(!v.isAudible());
        v.setConditions(QStringLiteral("home"), false, false, false);
        QVERIFY(v.isAudible());
    }

    void soundAndVolumeApplyLive()
    {
        if (!needClip())
            return;
        ZeroVideoBackground v;
        v.setConditions(QStringLiteral("home"), false, false, false);
        v.setSource(m_clip);  // starts silent
        QVERIFY(!v.hasAudioOutput());

        v.setSound(true, 0.25);  // switched on while the video plays
        QVERIFY(v.hasAudioOutput());
        QVERIFY(v.isAudible());
        QCOMPARE(v.audioOutput()->volume(), 0.25f);
        v.setSound(true, 0.9);  // the slider moves
        QCOMPARE(v.audioOutput()->volume(), 0.9f);
        QVERIFY(v.hasAudioOutput());
        v.setSound(true, 7.0);  // clamped
        QCOMPARE(v.audioOutput()->volume(), 1.0f);

        v.setSound(false, 0.5);  // switched off again: the output goes away entirely
        QVERIFY(!v.hasAudioOutput());
        QVERIFY(!v.isAudible());

        // a new source keeps the switch; releasing the player releases the output
        v.setSound(true, 0.5);
        QVERIFY(v.hasAudioOutput());
        v.setSource(QString());
        QVERIFY(!v.hasPlayer());
        QVERIFY(!v.hasAudioOutput());
        v.setSource(m_clip);
        QVERIFY(v.hasAudioOutput());  // sound is still on: the new player gets its output
    }

    void shellHandsTheThemesSoundToTheVideo()
    {
        if (!needClip())
            return;
        ThemeSpec spec = ThemeSpec::obsidian();
        spec.reduceMotion = false;
        spec.backgroundVideo = m_clip;
        spec.backgroundDim = 0.3;
        spec.videoSound = true;
        spec.videoVolume = 0.35;
        Theme::setCurrent(spec);
        auto shell = std::make_unique<ZeroShell>();
        shell->addPage(QStringLiteral("home"), QStringLiteral("house"), QStringLiteral("Home"), new StubPage(QStringLiteral("Home")));
        shell->finishRail();
        shell->resize(600, 400);
        shell->show();
        QVERIFY(QTest::qWaitForWindowExposed(shell.get()));
        ZeroVideoBackground* video = shell->videoBackground();
        QVERIFY(video);
        QVERIFY(video->soundOn());
        QVERIFY(video->hasAudioOutput());
        QCOMPARE(video->audioOutput()->volume(), 0.35f);

        spec.videoVolume = 0.8;  // the editor's slider
        Theme::setCurrent(spec);
        QCOMPARE(video->audioOutput()->volume(), 0.8f);
        spec.videoSound = false;
        Theme::setCurrent(spec);
        QVERIFY(!video->hasAudioOutput());
        spec.backgroundVideo.clear();
        Theme::setCurrent(spec);
    }

    void releasesThePlayerWhenTheSourceGoes()
    {
        if (!needClip())
            return;
        ZeroVideoBackground v;
        v.setConditions(QStringLiteral("home"), false, false, false);
        v.setSource(m_clip);
        QTRY_VERIFY_WITH_TIMEOUT(!v.frame().isNull(), 15000);
        v.setSource(QString());
        QVERIFY(!v.hasPlayer());
        QVERIFY(v.frame().isNull());
        QVERIFY(!v.isPlaying());
        QVERIFY(v.source().isEmpty());
    }

    void garbageFileFallsBackToFlat()
    {
        if (!needClip())
            return;
        const QString junk = m_dir.filePath(QStringLiteral("junk.mp4"));
        QFile f(junk);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(512, 'x'));
        f.close();
        ZeroVideoBackground v;
        v.setConditions(QStringLiteral("home"), false, false, false);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("ZeroVideoBackground: cannot play")));
        v.setSource(junk);
        QTRY_VERIFY_WITH_TIMEOUT(v.failed(), 15000);
        QVERIFY(!v.hasPlayer());
        QVERIFY(v.frame().isNull());
    }

    void releasedFileCanBeReplacedAndRemoved()
    {
        // The editor swaps / removes background.<ext> right after the shell stopped using it (Windows refuses to rename or delete an open file).
        if (!needClip())
            return;
        const QString a = m_dir.filePath(QStringLiteral("swap-a.mp4"));
        const QString b = m_dir.filePath(QStringLiteral("swap-b.mp4"));
        QVERIFY(QFile::copy(m_clip, a));
        QVERIFY(QFile::copy(m_clip, b));
        ZeroVideoBackground v;
        v.setConditions(QStringLiteral("home"), false, false, false);
        v.setSource(a);
        QTRY_VERIFY_WITH_TIMEOUT(!v.frame().isNull(), 15000);
        v.setSource(b);  // retarget (what the live preview does)
        QVERIFY(QFile::rename(a, a + QStringLiteral(".bak")));
        QVERIFY(QFile::remove(a + QStringLiteral(".bak")));
        QTRY_VERIFY_WITH_TIMEOUT(!v.frame().isNull(), 15000);
        v.setSource(QString());  // theme without a video
        QVERIFY(QFile::remove(b));
    }

    // ---- ZeroShell ----------------------------------------------------------------------------------------------------

    void shellPaintsTheFrameAndFollowsPagesAndTheme()
    {
        if (!needClip())
            return;
        ThemeSpec spec = ThemeSpec::obsidian();
        spec.backgroundVideo = m_clip;
        spec.backgroundDim = 0.0;
        Theme::setCurrent(spec);

        auto shell = std::make_unique<ZeroShell>();
        shell->addPage(QStringLiteral("home"), QStringLiteral("house"), QStringLiteral("Home"), new StubPage(QStringLiteral("Home")));
        shell->addPage(QStringLiteral("library"), QStringLiteral("library-big"), QStringLiteral("Library"), new StubPage(QStringLiteral("Library")));
        shell->finishRail();
        shell->resize(900, 500);
        shell->show();
        QVERIFY(QTest::qWaitForWindowExposed(shell.get()));

        ZeroVideoBackground* v = shell->videoBackground();
        QVERIFY(v);
        QCOMPARE(v->source(), m_clip);
        QSignalSpy frames(v, &ZeroVideoBackground::frameChanged);
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= 3, 15000);  // Home: playing
        QVERIFY(v->isPlaying());

        // the shell's own background is the video frame, not the flat window color: the colorful test pattern has many colors
        const QImage shot = shell->grab(QRect(200, 100, 600, 380)).toImage();  // the page area (stub pages draw nothing)
        QSet<QRgb> colors;
        for (int y = 0; y < shot.height(); y += 8)
            for (int x = 0; x < shot.width(); x += 8)
                colors.insert(shot.pixel(x, y));
        QVERIFY2(colors.size() > 4, "the shell did not paint the video frame");

        shell->setCurrentPage(QStringLiteral("library"));
        QVERIFY(!v->isPlaying());
        QVERIFY(!v->frame().isNull());  // the still stays

        shell->setCurrentPage(QStringLiteral("home"));
        QVERIFY(v->isPlaying());
        shell->setGameRunning(true);
        QVERIFY(!v->isPlaying());
        shell->setGameRunning(false);
        QVERIFY(v->isPlaying());

        // motion off pauses (Theme::changed), motion on resumes
        Theme::setReduceMotion(true);
        QVERIFY(!v->isPlaying());
        Theme::setReduceMotion(false);
        QVERIFY(v->isPlaying());

        // a theme without a video releases the player
        Theme::setCurrent(ThemeSpec::obsidian());
        QVERIFY(!v->hasPlayer());
        QVERIFY(v->frame().isNull());
    }
};

QTEST_MAIN(ZeroVideoBackgroundTest)
#include "ZeroVideoBackground_test.moc"
