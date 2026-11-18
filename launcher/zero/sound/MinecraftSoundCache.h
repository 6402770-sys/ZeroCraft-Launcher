// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - converts the local Minecraft sounds (see MinecraftSounds.h) into the private WAV cache
// <data>/zero/sounds/minecraft/<event>.wav + manifest.json. Decoding is asynchronous (Qt Multimedia's QAudioDecoder, FFmpeg
// backend), so the UI thread never blocks. The cache is rebuilt only when a file or the manifest (source keys + hashes) differs.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>

#include "MinecraftSounds.h"

namespace Zero {
namespace Sound {
namespace Minecraft {

class Cache : public QObject {
    Q_OBJECT
   public:
    enum class State {
        Idle,         ///< prepare() was not asked yet
        Preparing,    ///< decoding / writing
        Ready,        ///< every event has its WAV in dir()
        Unavailable,  ///< the game's sounds are missing, or could not be decoded
    };

    Cache(QString assetsRoot, QString cacheDir, QObject* parent = nullptr);

    State state() const { return m_state; }
    /// Every event resolves in the local assets (checked by refresh() / prepare(), not on every call).
    bool available() const { return m_available; }
    const QString& dir() const { return m_dir; }
    /// Re-checks `available()` only.
    void refresh();
    /// Checks the assets and the cache and converts when needed (idempotent while preparing). Emits changed().
    void prepare();

    /// True when manifest.json matches `assets` and every event's WAV exists in `dir`.
    static bool isCurrent(const QString& dir, const QList<Asset>& assets);
    /// Writes one event's WAV (conditioned + rate header) with QSaveFile.
    static bool writeEvent(const QString& dir, Event event, const Pcm& decoded);
    static bool writeManifest(const QString& dir, const QList<Asset>& assets);

   signals:
    void changed();

   private:
    void setState(State s);
    void decodeNext();
    void fail();
    void commit();

    QString m_assetsRoot;
    QString m_dir;
    State m_state = State::Idle;
    bool m_available = false;
    int m_generation = 0;
    QList<Asset> m_assets;
    QStringList m_pendingKeys;  // unique asset keys still to decode
    QHash<QString, Pcm> m_decoded;
};

/// Decodes one asset file (an extensionless object file is fine) asynchronously; calls `done(ok, pcm)` once, on the thread of `parent`.
/// Public so tests and tools can exercise decoding without the cache.
void decodeFile(QObject* parent, const QString& path, std::function<void(bool, Pcm)> done);

}  // namespace Minecraft
}  // namespace Sound
}  // namespace Zero
