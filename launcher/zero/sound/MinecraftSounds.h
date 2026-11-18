// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the "Minecraft" interface sound pack, pure part (no widgets, no audio): which of the game's own sound files
// stand in for each interface event, how to find them in the assets the launcher already downloaded for the game
// (<data>/assets/indexes/<id>.json + objects/<hh>/<hash>), and how decoded audio becomes a small WAV (trim, normalise, pitch header).
// Nothing from the game is shipped or copied into the repository: the files are only read from the user's own PC at run time.
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

#include "ui/zero/ZeroSound.h"

namespace Zero {
namespace Sound {
namespace Minecraft {

/// Where an event's sound comes from: candidate asset keys in order of preference (the first one found wins) and the playback
/// rate factor written into the WAV header (toggle-off: the toggle-on sound about 20 % lower, a plain pitch / tempo shift).
struct EventSpec {
    QStringList keys;
    double rateScale = 1.0;
};
EventSpec specFor(Event event);

/// An asset file found in the local game assets.
struct Asset {
    QString key;         ///< "minecraft/sounds/random/click.ogg"
    QString objectPath;  ///< <assets>/objects/<hh>/<hash>
    QString hash;
    qint64 size = 0;
    QString indexName;  ///< "17" (the index file it was found in)
};

/// The first of `keys` that some index lists AND whose object file exists with the indexed size. Indexes are tried newest first
/// (highest numeric name, then newest modification time).
std::optional<Asset> resolve(const QString& assetsRoot, const QStringList& keys);
/// One asset per event (index = int(Event)); empty when any event cannot be resolved.
std::optional<QList<Asset>> resolveAll(const QString& assetsRoot);
/// Every event resolves: the pack can be used.
bool available(const QString& assetsRoot);

/// Identity of a conversion: which asset (and rate factor) every event was made from. Equal manifests = the cache is current.
QJsonObject manifestFor(const QList<Asset>& assets);

/// Decoded audio: 16-bit samples, interleaved.
struct Pcm {
    int sampleRate = 0;
    int channels = 0;
    QVector<qint16> samples;
    double seconds() const { return sampleRate > 0 && channels > 0 ? double(samples.size()) / channels / sampleRate : 0.0; }
};
inline constexpr double kMaxSeconds = 2.5;
inline constexpr double kPeak = 0.5;  ///< about -6 dBFS, like the built-in files

/// Trims to `maxSeconds` (a short fade-out when something was cut) and scales so the peak is `peak` of full scale.
Pcm conditioned(const Pcm& in, double maxSeconds = kMaxSeconds, double peak = kPeak);
/// A complete 16-bit PCM WAV file; the sample rate in the header is round(rate * rateScale).
QByteArray toWav(const Pcm& pcm, double rateScale = 1.0);

}  // namespace Minecraft
}  // namespace Sound
}  // namespace Zero
