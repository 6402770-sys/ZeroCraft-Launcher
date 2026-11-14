// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - authlib-injector for Ely.by accounts (docs/zero/ARCHITECTURE.md, "Ely.by accounts").
//
// Minecraft talks to Mojang's session servers. For an Ely.by account the game must talk to Ely.by instead, and that is what the
// authlib-injector java agent (https://github.com/yushijinhun/authlib-injector) does: `-javaagent:<jar>=ely.by`. ZERO downloads the
// jar itself from the project's release API; ElyPrismLauncher's patch components are not used.
//
//  - the PURE part: parse `latest.json`, SHA-256 checks, the `-javaagent` argument, the cache layout and its fallback rules;
//  - `AuthlibInjectorTask`: fetch latest.json, download + verify the jar, keep it in `<data>/zero/authlib-injector/`. The network
//    sits behind `Fetcher`; production uses Prism's Net::Request/NetJob (`makeNetFetcher`), tests use fakes or a plain QNetworkAccessManager.
//
// No widgets and no APPLICATION in this file.
#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>
#include <expected>
#include <functional>
#include <memory>
#include <optional>

#include "tasks/Task.h"

class QNetworkAccessManager;

namespace Zero::Auth::AuthlibInjector {

/// The API location the agent talks to for Ely.by accounts (the `=ely.by` of the -javaagent argument).
QString elyApiServer();

/// What `latest.json` says about the newest release.
struct Artifact {
    QString version;
    QUrl downloadUrl;  ///< https only
    QString sha256;    ///< 64 lower-case hex digits
};

/// https://authlib-injector.yushi.moe/artifact/latest.json
QUrl latestUrl();

/// Reads `latest.json` ({"version", "download_url", "checksums": {"sha256"}}). Rejects a missing or malformed field, a version that
/// is not a plain file-name-safe token, a download URL that is not https, and a checksum that is not 64 hex digits.
std::expected<Artifact, QString> parseLatest(const QByteArray& json);

/// Lower-case hex SHA-256.
QString sha256Hex(const QByteArray& data);
/// True when `data` hashes to `expectedHex` (case-insensitive; an empty or malformed expected value never matches).
bool sha256Matches(const QByteArray& data, const QString& expectedHex);
/// SHA-256 of a file, or the reason it could not be read.
std::expected<QString, QString> fileSha256(const QString& path);
/// True when the file exists and hashes to `expectedHex`.
bool fileMatchesSha256(const QString& path, const QString& expectedHex);

/// `-javaagent:<absolute jar path>=<server>`. The caller passes the path as one process argument (no extra quoting).
QString javaAgentArgument(const QString& absoluteJarPath, const QString& server = elyApiServer());

/// `<dataRoot>/zero/authlib-injector`
QString cacheDirectory(const QString& dataRoot);
/// `authlib-injector-<version>.jar`
QString jarFileName(const QString& version);
/// `<cacheDirectory>/authlib-injector-<version>.jar`
QString jarPath(const QString& directory, const QString& version);

/// The text of the failed launch step.
QString failureMessage(const QString& reason);

/// A jar already in the cache that can be trusted without the network: the newest `authlib-injector-<version>.jar` whose SHA-256
/// equals the one recorded next to it (`<jar>.sha256`, written when ZERO verified the download). None when there is no such jar.
struct CachedJar {
    QString path;
    QString version;
};
std::optional<CachedJar> findCachedJar(const QString& directory);
/// Records the verified hash next to the jar.
bool writeChecksumFile(const QString& jarFile, const QString& sha256);

// ---- the task ----

/// The network seam of the task. Callbacks may run inside the call (fakes) or later (production).
class Fetcher {
   public:
    virtual ~Fetcher() = default;
    /// GET `url` into memory.
    virtual void getBytes(const QUrl& url, std::function<void(bool ok, QByteArray body, QString error)> done) = 0;
    /// GET `url` into the file `path`. A real fetcher also checks `sha256`; the task verifies the file again either way.
    virtual void downloadFile(const QUrl& url, const QString& path, const QString& sha256, std::function<void(bool ok, QString error)> done) = 0;
    virtual void abort() {}
};

/// Prism's network classes (Net::Request in a NetJob on `network`). Needs the running Application.
std::unique_ptr<Fetcher> makeNetFetcher(QNetworkAccessManager* network);

/// Makes sure a verified authlib-injector jar exists in `directory`:
///  1. fetch latest.json; 2. when `authlib-injector-<version>.jar` is there and valid, reuse it; else download it to `.part`, check the
///  SHA-256, rename; 3. when anything of that fails but a verified cached jar exists, use it (warning); else fail with
///  `failureMessage(reason)`.
class AuthlibInjectorTask : public Task {
    Q_OBJECT
   public:
    AuthlibInjectorTask(const QString& directory, std::unique_ptr<Fetcher> fetcher, const QUrl& latest = latestUrl());
    ~AuthlibInjectorTask() override = default;

    /// Valid after the task succeeded.
    QString jarPath() const { return m_jarPath; }
    QString version() const { return m_version; }
    /// True when the jar is an already-cached one used because the network part failed.
    bool usedCacheAfterFailure() const { return m_usedCacheAfterFailure; }
    /// True when the jar was downloaded by this run.
    bool downloaded() const { return m_downloaded; }

    bool canAbort() const override { return true; }
    bool abort() override;

   protected:
    void executeTask() override;

   private:
    void onLatest(bool ok, const QByteArray& body, const QString& error);
    void onDownloaded(bool ok, const QString& error);
    void succeed(const QString& path, const QString& version);
    void fallBackToCache(const QString& reason);

    QString m_directory;
    std::unique_ptr<Fetcher> m_fetcher;
    QUrl m_latestUrl;
    Artifact m_artifact;
    QString m_jarPath;
    QString m_version;
    bool m_usedCacheAfterFailure = false;
    bool m_downloaded = false;
};

}  // namespace Zero::Auth::AuthlibInjector
