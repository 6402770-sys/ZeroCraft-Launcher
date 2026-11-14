// SPDX-License-Identifier: GPL-3.0-only
#include "AuthlibInjector.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QVersionNumber>

#include "FileSystem.h"
#include "net/ChecksumValidator.h"
#include "net/NetJob.h"
#include "net/Request.h"

namespace Zero::Auth::AuthlibInjector {

QString elyApiServer()
{
    return QStringLiteral("ely.by");
}

QUrl latestUrl()
{
    return QUrl(QStringLiteral("https://authlib-injector.yushi.moe/artifact/latest.json"));
}

namespace {
bool isHexSha256(const QString& s)
{
    static const QRegularExpression re(QStringLiteral("^[0-9a-fA-F]{64}$"));
    return re.match(s).hasMatch();
}

/// A version goes into a file name, so it must be a plain token (no separators, no "..").
bool isSafeVersion(const QString& v)
{
    static const QRegularExpression re(QStringLiteral("^[0-9A-Za-z][0-9A-Za-z._+-]{0,63}$"));
    return re.match(v).hasMatch() && !v.contains(QStringLiteral(".."));
}

const QString kJarPrefix = QStringLiteral("authlib-injector-");
const QString kJarSuffix = QStringLiteral(".jar");
const QString kChecksumSuffix = QStringLiteral(".sha256");
}  // namespace

std::expected<Artifact, QString> parseLatest(const QByteArray& json)
{
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::unexpected(QStringLiteral("latest.json is not valid JSON"));
    }
    const auto obj = doc.object();

    const auto versionV = obj.value("version");
    if (!versionV.isString() || versionV.toString().isEmpty()) {
        return std::unexpected(QStringLiteral("latest.json has no version"));
    }
    const auto version = versionV.toString();
    if (!isSafeVersion(version)) {
        return std::unexpected(QStringLiteral("latest.json has an unusable version"));
    }

    const auto urlV = obj.value("download_url");
    if (!urlV.isString() || urlV.toString().isEmpty()) {
        return std::unexpected(QStringLiteral("latest.json has no download_url"));
    }
    const QUrl url(urlV.toString(), QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != QStringLiteral("https") || url.host().isEmpty()) {
        return std::unexpected(QStringLiteral("latest.json has a download_url that is not an https address"));
    }

    const auto checksumsV = obj.value("checksums");
    if (!checksumsV.isObject()) {
        return std::unexpected(QStringLiteral("latest.json has no checksums"));
    }
    const auto shaV = checksumsV.toObject().value("sha256");
    if (!shaV.isString() || !isHexSha256(shaV.toString())) {
        return std::unexpected(QStringLiteral("latest.json has no valid sha256 checksum"));
    }

    return Artifact{ version, url, shaV.toString().toLower() };
}

QString sha256Hex(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

bool sha256Matches(const QByteArray& data, const QString& expectedHex)
{
    if (!isHexSha256(expectedHex)) {
        return false;
    }
    return sha256Hex(data) == expectedHex.toLower();
}

std::expected<QString, QString> fileSha256(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return std::unexpected(QStringLiteral("Cannot read %1: %2").arg(path, f.errorString()));
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f)) {
        return std::unexpected(QStringLiteral("Cannot read %1: %2").arg(path, f.errorString()));
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool fileMatchesSha256(const QString& path, const QString& expectedHex)
{
    if (!isHexSha256(expectedHex)) {
        return false;
    }
    const auto actual = fileSha256(path);
    return actual && *actual == expectedHex.toLower();
}

QString javaAgentArgument(const QString& absoluteJarPath, const QString& server)
{
    return QStringLiteral("-javaagent:%1=%2").arg(absoluteJarPath, server);
}

QString cacheDirectory(const QString& dataRoot)
{
    return FS::PathCombine(dataRoot, QStringLiteral("zero"), QStringLiteral("authlib-injector"));
}

QString jarFileName(const QString& version)
{
    return kJarPrefix + version + kJarSuffix;
}

QString jarPath(const QString& directory, const QString& version)
{
    return FS::PathCombine(directory, jarFileName(version));
}

QString failureMessage(const QString& reason)
{
    return QObject::tr("Ely.by accounts need authlib-injector, which couldn't be downloaded: %1").arg(reason);
}

bool writeChecksumFile(const QString& jarFile, const QString& sha256)
{
    QFile f(jarFile + kChecksumSuffix);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return f.write(sha256.toLower().toLatin1()) >= 0;
}

std::optional<CachedJar> findCachedJar(const QString& directory)
{
    const QDir dir(directory);
    if (!dir.exists()) {
        return std::nullopt;
    }
    std::optional<CachedJar> best;
    QVersionNumber bestVersion;
    const auto files = dir.entryInfoList({ kJarPrefix + QStringLiteral("*") + kJarSuffix }, QDir::Files);
    for (const auto& info : files) {
        const auto name = info.fileName();
        const auto version = name.mid(kJarPrefix.size(), name.size() - kJarPrefix.size() - kJarSuffix.size());
        if (!isSafeVersion(version)) {
            continue;
        }
        QFile sidecar(info.absoluteFilePath() + kChecksumSuffix);
        if (!sidecar.open(QIODevice::ReadOnly)) {
            continue;
        }
        const auto recorded = QString::fromLatin1(sidecar.readAll()).trimmed();
        if (!fileMatchesSha256(info.absoluteFilePath(), recorded)) {
            continue;
        }
        const auto number = QVersionNumber::fromString(version);
        if (!best || number > bestVersion) {
            best = CachedJar{ info.absoluteFilePath(), version };
            bestVersion = number;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------------------------------------- the task

AuthlibInjectorTask::AuthlibInjectorTask(const QString& directory, std::unique_ptr<Fetcher> fetcher, const QUrl& latest)
    : Task(false), m_directory(directory), m_fetcher(std::move(fetcher)), m_latestUrl(latest)
{}

bool AuthlibInjectorTask::abort()
{
    if (m_fetcher) {
        m_fetcher->abort();
    }
    return Task::abort();
}

void AuthlibInjectorTask::executeTask()
{
    setStatus(tr("Checking authlib-injector..."));
    m_fetcher->getBytes(m_latestUrl, [this](bool ok, QByteArray body, QString error) { onLatest(ok, body, error); });
}

void AuthlibInjectorTask::onLatest(bool ok, const QByteArray& body, const QString& error)
{
    if (!isRunning()) {
        return;
    }
    if (!ok) {
        fallBackToCache(error);
        return;
    }
    const auto artifact = parseLatest(body);
    if (!artifact) {
        fallBackToCache(artifact.error());
        return;
    }
    m_artifact = *artifact;

    const auto jar = Zero::Auth::AuthlibInjector::jarPath(m_directory, m_artifact.version);
    if (fileMatchesSha256(jar, m_artifact.sha256)) {
        writeChecksumFile(jar, m_artifact.sha256);
        succeed(jar, m_artifact.version);
        return;
    }

    setStatus(tr("Downloading authlib-injector %1...").arg(m_artifact.version));
    if (!FS::ensureFilePathExists(jar)) {
        fallBackToCache(tr("Cannot create the folder %1").arg(m_directory));
        return;
    }
    const auto part = jar + QStringLiteral(".part");
    QFile::remove(part);
    m_fetcher->downloadFile(m_artifact.downloadUrl, part, m_artifact.sha256, [this](bool done, QString err) { onDownloaded(done, err); });
}

void AuthlibInjectorTask::onDownloaded(bool ok, const QString& error)
{
    if (!isRunning()) {
        return;
    }
    const auto jar = Zero::Auth::AuthlibInjector::jarPath(m_directory, m_artifact.version);
    const auto part = jar + QStringLiteral(".part");
    if (!ok) {
        QFile::remove(part);
        fallBackToCache(error);
        return;
    }
    if (!fileMatchesSha256(part, m_artifact.sha256)) {
        QFile::remove(part);
        fallBackToCache(tr("the downloaded file does not match the checksum published by authlib-injector"));
        return;
    }
    // the old file (if any) failed the check above, so replace it
    QFile::remove(jar);
    QFile::remove(jar + kChecksumSuffix);
    if (!QFile::rename(part, jar)) {
        QFile::remove(part);
        fallBackToCache(tr("Cannot move the downloaded file to %1").arg(jar));
        return;
    }
    writeChecksumFile(jar, m_artifact.sha256);
    m_downloaded = true;
    succeed(jar, m_artifact.version);
}

void AuthlibInjectorTask::succeed(const QString& path, const QString& version)
{
    m_jarPath = path;
    m_version = version;
    emitSucceeded();
}

void AuthlibInjectorTask::fallBackToCache(const QString& reason)
{
    if (const auto cached = findCachedJar(m_directory)) {
        m_usedCacheAfterFailure = true;
        logWarning(QStringLiteral("authlib-injector: using the cached version %1 (%2)").arg(cached->version, reason));
        succeed(cached->path, cached->version);
        return;
    }
    emitFailed(failureMessage(reason));
}

// ------------------------------------------------------------------------------------------------- production fetcher

namespace {
class NetFetcher final : public Fetcher {
   public:
    explicit NetFetcher(QNetworkAccessManager* network) : m_network(network) {}

    void getBytes(const QUrl& url, std::function<void(bool, QByteArray, QString)> done) override
    {
        auto [request, bytes] = Net::Request::makeByteArray(url);
        request->enableAutoRetry(true);
        auto job = makeJob("AuthlibInjectorLatest", request);
        QObject::connect(job.get(), &Task::finished, job.get(), [request, bytes, done]() {
            if (request->wasSuccessful() && request->error() == QNetworkReply::NoError) {
                done(true, *bytes, QString());
            } else {
                done(false, QByteArray(), request->errorString());
            }
        });
        start(job);
    }

    void downloadFile(const QUrl& url, const QString& path, const QString& sha256, std::function<void(bool, QString)> done) override
    {
        auto request = Net::Request::makeFile(url, path);
        request->addValidator(new Net::ChecksumValidator(QCryptographicHash::Sha256, sha256));
        auto job = makeJob("AuthlibInjectorJar", request);
        QObject::connect(job.get(), &Task::finished, job.get(), [request, done]() {
            if (request->wasSuccessful() && request->error() == QNetworkReply::NoError) {
                done(true, QString());
            } else {
                done(false, request->errorString());
            }
        });
        start(job);
    }

    void abort() override
    {
        if (m_job) {
            m_job->abort();
        }
    }

   private:
    NetJob::Ptr makeJob(const QString& name, const Net::Request::Ptr& request)
    {
        NetJob::Ptr job(new NetJob(name, m_network));
        job->setAskRetry(false);
        job->addNetAction(request);
        return job;
    }
    void start(const NetJob::Ptr& job)
    {
        m_job = job;  // the previous job (whose callback may be the caller) is released with deleteLater
        m_job->start();
    }

    QNetworkAccessManager* m_network;
    NetJob::Ptr m_job;
};
}  // namespace

std::unique_ptr<Fetcher> makeNetFetcher(QNetworkAccessManager* network)
{
    return std::make_unique<NetFetcher>(network);
}

}  // namespace Zero::Auth::AuthlibInjector
