// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - instance profile data (favorite, tags, description, artwork, mod count). No widgets.
// Everything lives in the instance's own files: `instance.cfg` (ZeroFavorite, ZeroTags, ZeroArtwork, ZeroBackgroundVideo, and Prism's `notes`
// as the description), `<instance root>/.zero/artwork.png` and the optional `<instance root>/.zero/background.<ext>` video. See docs/zero/ARCHITECTURE.md.
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <optional>
#include "LibraryFilter.h"

class BaseInstance;
class MinecraftInstance;

namespace Zero {
namespace Profile {

constexpr int kMaxTagLength = 24;
constexpr int kMaxTags = 12;
constexpr int kMaxArtworkSide = 1600;

/// Folder (relative to the instance root) and file name of the artwork.
QString artworkRelativePath();

// ----- tags -----

/// Trims, collapses inner whitespace, drops a leading '#', drops empties, truncates each tag to kMaxTagLength characters,
/// removes case-insensitive duplicates (the first spelling wins) and keeps at most kMaxTags tags, in input order.
QStringList normalizeTags(const QStringList& tags);
/// Normalized tags from the JSON array stored in `ZeroTags` (anything that is not an array of strings gives an empty list).
QStringList parseTags(const QString& json);
/// Compact JSON array of the normalized tags.
QString serializeTags(const QStringList& tags);

struct TagCount {
    QString tag;  ///< first spelling seen
    int count = 0;
};
/// Tags with the number of lists that contain them (case-insensitive), sorted alphabetically (case-insensitive).
QList<TagCount> tagCounts(const QList<QStringList>& tagLists);
QList<TagCount> allTags(const QList<BaseInstance*>& instances);

// ----- per-instance data -----
// Every setter ignores a value equal to the stored one; otherwise it saves the setting, emits BaseInstance::propertiesChanged
// (so the instance list and its proxies refresh) and Notifier::profileChanged.

bool isFavorite(BaseInstance* instance);
void setFavorite(BaseInstance* instance, bool favorite);

QStringList tags(BaseInstance* instance);
void setTags(BaseInstance* instance, const QStringList& tags);

/// The description is Prism's `notes` setting (shown in Prism's instance Notes page too).
QString description(BaseInstance* instance);
void setDescription(BaseInstance* instance, const QString& text);

// ----- artwork -----

/// Absolute path of the artwork, or an empty string when none is set or the file is missing / outside the instance folder.
QString artworkPath(BaseInstance* instance);
/// Loads `sourcePath`, scales it down to at most kMaxArtworkSide px on the long side (never up) and writes it as
/// `<instance root>/.zero/artwork.png` through a temporary file that replaces the old artwork only when completely written.
/// Returns an empty string on success, otherwise a message; on failure the old artwork is untouched.
QString setArtwork(BaseInstance* instance, const QString& sourcePath);
/// Deletes `<instance root>/.zero/artwork.png` (and only that file) and clears the setting.
void clearArtwork(BaseInstance* instance);

// ----- background video -----

/// Folder (relative to the instance root) and base name (without extension) of the background video: `.zero/background.<ext>`.
QString backgroundVideoBaseName();

/// Absolute path of the Home background video, or an empty string when none is set or the file is missing / outside the instance
/// folder.
QString backgroundVideoPath(BaseInstance* instance);
/// Checks `sourcePath` as a background video: one of the theme import types (mp4, webm, mkv, mov), an existing file, at most
/// `maxBytes` (the theme import limit when negative). Returns an empty string when fine, otherwise a message.
QString checkBackgroundVideo(const QString& sourcePath, qint64 maxBytes = -1);
/// The file part of setBackgroundVideo(), free of widgets, settings and the instance object, so it can run on a worker thread:
/// copies `sourcePath` to `<instanceRoot>/.zero/background.<ext>` through a temporary file that replaces the old video only when
/// completely written, then removes an old video with another extension. `storedName` receives the file name (e.g. "background.mp4").
/// Returns an empty string on success, otherwise a message; on failure the old video is untouched.
QString copyBackgroundVideo(const QString& instanceRoot, const QString& sourcePath, QString* storedName, qint64 maxBytes = -1);
/// Records an already copied video (`storedName` from copyBackgroundVideo()): saves the setting and notifies. Main thread.
void commitBackgroundVideo(BaseInstance* instance, const QString& storedName);
/// checkBackgroundVideo() + copyBackgroundVideo() + commitBackgroundVideo() on the calling thread (the GUI uses the pieces so the
/// copy runs in the background). Empty string on success, otherwise a message.
QString setBackgroundVideo(BaseInstance* instance, const QString& sourcePath, qint64 maxBytes = -1);
/// Deletes the instance's background video file (and only that file) and clears the setting.
void clearBackgroundVideo(BaseInstance* instance);

// ----- mods -----

/// Number of enabled mods (`*.jar`, not `*.jar.disabled`) directly in `modsFolder`; nullopt when the folder does not exist.
/// Cached by the folder's modification time.
std::optional<int> countEnabledMods(const QString& modsFolder);
/// countEnabledMods() of the instance's mods folder, only for instances with a mod loader (nullopt otherwise).
std::optional<int> modCount(MinecraftInstance* instance);

// ----- Library -----

/// What the Library filter needs to know about `instance` (`group` = its Prism group, empty for none).
LibraryFilter::Facts libraryFacts(MinecraftInstance* instance, const QString& group);

/// Emits profileChanged(instanceId) after any profile change so views can repaint.
class Notifier : public QObject {
    Q_OBJECT
   public:
    static Notifier* instance();
    void notify(const QString& instanceId, bool artwork = false, bool video = false)
    {
        if (artwork)
            emit artworkChanged(instanceId);
        if (video)
            emit backgroundVideoChanged(instanceId);
        emit profileChanged(instanceId);
    }
   signals:
    void profileChanged(const QString& instanceId);
    /// Emitted (before profileChanged) when the artwork file of the instance was replaced or removed.
    void artworkChanged(const QString& instanceId);
    /// Emitted (before profileChanged) when the background video of the instance was set, replaced or removed.
    void backgroundVideoChanged(const QString& instanceId);

   private:
    Notifier() = default;
};

}  // namespace Profile
}  // namespace Zero
