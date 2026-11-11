// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Java Doctor core (no widgets, no APPLICATION, no Prism instance classes). Predicts, without launching anything, which
// Java an instance will use and whether that launch would work. `diagnose` is PURE and mirrors Prism's launch steps in their order:
//   1. AutoInstallJava (launcher/minecraft/launch/AutoInstallJava.cpp), skipped when automatic Java switching is off or the instance has
//      its own Java path that exists; otherwise: a pinned path of a compatible major, then (downloads off) the first detected runtime of
//      a compatible major, or (downloads on) the managed runtime of the version / a download. Falling through means "use the configured
//      Java".
//   2. CheckJava: the configured Java must exist.
//   3. VerifyJavaInstall: 32-bit Java with more than 2 GiB of memory warns; a Java major the version does not support fails the launch
//      unless the compatibility check is turned off (then it warns); an empty list of majors means "any".
// Nothing here changes any setting: applying a recommendation is the UI's job and always needs the user's confirmation.
#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <optional>

namespace Zero {
namespace Java {

/// A detected Java installation.
struct Runtime {
    QString path;           ///< the java executable
    QString versionString;  ///< "17.0.8"
    int major = 0;          ///< 0 = unknown
    bool is64bit = true;
    bool managed = false;  ///< downloaded by the launcher (inside <dataRoot>/java)
};

/// Launcher-wide Java settings.
struct GlobalJava {
    bool autoSwitch = false;    ///< AutomaticJavaSwitch
    bool autoDownload = false;  ///< AutomaticJavaDownload
    /// PinnedJavaPaths: major -> configured path. Use the resolved executable when it resolves; a path that does not resolve is
    /// listed in `pinnedMissing`. Empty paths are not pinned.
    QHash<int, QString> pinned;
    QSet<int> pinnedMissing;
    bool downloadSupported = true;  ///< false when this OS / architecture cannot download Mojang runtimes
};

/// What one instance asks for and has configured (the effective values, as `settings()->get(...)` returns them).
struct InstanceJava {
    QString id;
    QString name;
    QString mcVersion;
    QList<int> compatibleMajors;  ///< empty = unknown / any
    QString javaDownloadName;     ///< the Mojang runtime name of the version ("java-runtime-gamma"); empty = unknown
    QString downloadedJavaPath;   ///< the managed java executable of `javaDownloadName` when it is already on disk, else empty
    bool overrideLocation = false;
    QString javaPath;          ///< effective JavaPath (instance override or global)
    bool javaPathExists = false;  ///< QFileInfo::exists(javaPath): what AutoInstallJava tests for an instance override
    QString javaPathResolved;  ///< the executable javaPath resolves to (FS::ResolveExecutable), empty when it does not resolve
    QString storedVersion;     ///< JavaVersion as stored by the last launch ("" = never checked)
    QString storedArch;        ///< JavaArchitecture as stored ("32" / "64" / "")
    bool ignoreCompatibility = false;
    int maxMemMb = 0;
};

enum class Status { Ready, Warning, WillFail, Unknown };

enum class Source {
    Configured,         ///< the Java path of the instance / the launcher settings
    AutomaticPinned,    ///< a pinned path of the launcher settings, picked by automatic Java
    AutomaticDetected,  ///< a detected installation, picked by automatic Java
    AutomaticDownload   ///< the managed runtime of the version (already downloaded, or downloaded at launch)
};

enum class Reason {
    Ok,
    IncompatibleMajor,                 ///< the Java in use is a major this version does not support: the launch fails
    IncompatibleIgnored,               ///< the same, but the compatibility check is off: warning only
    ConfiguredPathMissing,             ///< the configured Java does not exist: the launch fails
    NoCompatibleInstalledDownloadOff,  ///< automatic Java found nothing compatible and may not download; the configured Java does not fit
    WillDownload,                      ///< a compatible Java will be downloaded at the next launch
    ThirtyTwoBitMemory,                ///< 32-bit Java with more than 2048 MB of memory
    RequirementUnknown,                ///< the version does not say which Java it needs
    PinnedPathMissing,                 ///< a pinned path of a compatible major does not exist (another Java is used)
    RuntimeUnknown                     ///< the Java in use is not a known installation and has never been checked
};

struct Diagnosis {
    Status status = Status::Unknown;
    Source source = Source::Configured;
    Reason reason = Reason::RequirementUnknown;
    /// The Java the launch will use, when known (path always set; `major` 0 when only the path is known).
    std::optional<Runtime> effective;
    int downloadMajor = 0;      ///< WillDownload: the major that will be downloaded
    int pinnedMissingMajor = 0;  ///< first compatible major whose pinned path does not exist (0 = none)
    /// Only for Warning / WillFail with a compatible installed runtime: prefers 64-bit, then the highest version.
    std::optional<Runtime> recommendation;
};

/// What a version asks for, read from the files on disk.
struct Requirements {
    QString mcVersion;
    QList<int> majors;     ///< compatible Java majors in the order Prism applies them; empty = unknown
    QString javaName;      ///< Mojang runtime name ("java-runtime-gamma"); empty = unknown
};

/// Read-only look at the component list of an instance (`<instanceRoot>/mmc-pack.json`) and the version files behind it: the custom
/// `<instanceRoot>/patches/<uid>.json` when there is one, else the local metadata cache `<metaDir>/<uid>/<version>.json`. Never touches the
/// network and never writes (loading a PackProfile would resolve metadata and rewrite files). Missing / unreadable files give empty fields.
Requirements readRequirements(const QString& instanceRoot, const QString& metaDir);

/// Predicts the launch. Pure: the result depends only on the arguments. `runtimes` is in the order Prism lists them (automatic Java
/// takes the first compatible one).
Diagnosis diagnose(const InstanceJava& instance, const QList<Runtime>& runtimes, const GlobalJava& global);

/// True when `major` fits `majors` the way VerifyJavaInstall tests it (an empty list fits everything).
bool majorFits(const QList<int>& majors, int major);

/// Ascending copy without duplicates and without non-positive values ("needs Java 17 or 21").
QList<int> sortedMajors(const QList<int>& majors);

/// Case-insensitive on Windows, separators and `.` / `..` normalized; empty stays empty.
QString normalizedPath(const QString& path);

/// The best candidate among `runtimes` for `majors` (64-bit first, then the highest version; `only64` drops 32-bit ones; `skipPath`
/// is never returned). Empty `majors` = no candidate.
std::optional<Runtime> bestRuntimeFor(const QList<int>& majors, const QList<Runtime>& runtimes, bool only64, const QString& skipPath);

}  // namespace Java
}  // namespace Zero
