// SPDX-License-Identifier: GPL-3.0-only
#include "WorldBackup.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirListing>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#include <QStorageInfo>
#include <QThread>
#include <QTimeZone>
#include <algorithm>

#include "FileSystem.h"
#include "archive/ArchiveReader.h"
#include "archive/ArchiveWriter.h"

namespace Zero {
namespace Backup {

namespace {

constexpr qint64 kSlackBytes = 50LL * 1024 * 1024;  // free space beyond the data itself
constexpr qint64 kMaxManifestBytes = 64LL * 1024 * 1024;
constexpr qsizetype kChunk = 1024 * 1024;

// QDir::mkpath that survives a concurrent mkpath of a shared parent. On Windows Qt creates missing parents one by one
// and a thread that loses the race to create one (ERROR_ALREADY_EXISTS) gives up, so the first backups of two worlds
// (zero/backups/<instance>/... created by two pool threads) failed with "Could not create the backup folder" at random.
// Each lost race means another thread made that level, so a few retries always get past it.
bool makePath(const QString& path)
{
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (QDir().mkpath(path))
            return true;
    }
    return QFileInfo(path).isDir();
}

// ---------------------------------------------------------------- hooks

}  // namespace

bool renamePath(const QString& from, const QString& to)
{
    // Folder renames on Windows fail while an indexer / antivirus still holds a handle on a file inside: retry a few times.
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (QDir().rename(from, to))
            return true;
        if (QFileInfo::exists(to))  // never retry into an existing destination
            return false;
        QThread::msleep(150);
    }
    return false;
}

namespace {

bool defaultRename(const QString& from, const QString& to)
{
    return renamePath(from, to);
}

qint64 defaultFreeBytes(const QString& path)
{
    const QStorageInfo info(path);
    return info.isValid() && info.isReady() ? info.bytesAvailable() : -1;
}

bool defaultTrash(const QString& path)
{
    return FS::trash(path) || FS::deletePath(path);
}

Hooks resolve(const Hooks& in)
{
    Hooks h = in;
    if (!h.rename)
        h.rename = defaultRename;
    if (!h.freeBytes)
        h.freeBytes = defaultFreeBytes;
    if (!h.trashPath)
        h.trashPath = defaultTrash;
    return h;
}

QString sizeText(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

// ---------------------------------------------------------------- manifest

struct ManifestFile {
    QString path;
    qint64 size = 0;
    QString sha256;
};

struct Manifest {
    Kind kind = Kind::Manual;
    qint64 createdMs = 0;
    BackupMeta meta;
    QJsonObject extra;
    QList<ManifestFile> files;
    qint64 totalBytes = 0;
};

QByteArray manifestToJson(const Manifest& m)
{
    QJsonArray files;
    for (const ManifestFile& f : m.files)
        files.append(QJsonObject{ { "path", f.path }, { "size", double(f.size) }, { "sha256", f.sha256 } });
    const QJsonObject root{ { "version", kManifestVersion },
                            { "kind", kindToString(m.kind) },
                            { "createdMs", double(m.createdMs) },
                            { "instanceId", m.meta.instanceId },
                            { "instanceName", m.meta.instanceName },
                            { "worldFolder", m.meta.worldFolder },
                            { "worldName", m.meta.worldName },
                            { "files", files },
                            { "totalBytes", double(m.totalBytes) } };
    QJsonObject withExtra = root;
    if (!m.extra.isEmpty())
        withExtra.insert(QStringLiteral("extra"), m.extra);
    return QJsonDocument(withExtra).toJson(QJsonDocument::Compact);
}

bool isHex64(const QString& s)
{
    if (s.size() != 64)
        return false;
    for (QChar c : s) {
        const char16_t u = c.unicode();
        if (!((u >= '0' && u <= '9') || (u >= 'a' && u <= 'f') || (u >= 'A' && u <= 'F')))
            return false;
    }
    return true;
}

bool parseManifest(const QByteArray& json, Manifest* out, QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        *error = QCoreApplication::translate("Zero::Backup", "The backup manifest is unreadable.");
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value("version").toInt(-1) != kManifestVersion) {
        *error = QCoreApplication::translate("Zero::Backup", "The backup was made by an unsupported version.");
        return false;
    }
    bool kindOk = false;
    Manifest m;
    m.kind = kindFromString(root.value("kind").toString(), &kindOk);
    if (!kindOk || !root.value("files").isArray()) {
        *error = QCoreApplication::translate("Zero::Backup", "The backup manifest is incomplete.");
        return false;
    }
    m.createdMs = qint64(root.value("createdMs").toDouble());
    m.meta.instanceId = root.value("instanceId").toString();
    m.meta.instanceName = root.value("instanceName").toString();
    m.meta.worldFolder = root.value("worldFolder").toString();
    m.meta.worldName = root.value("worldName").toString();
    m.totalBytes = qint64(root.value("totalBytes").toDouble(-1));
    m.extra = root.value("extra").toObject();

    QSet<QString> seen;
    qint64 sum = 0;
    for (const QJsonValue& v : root.value("files").toArray()) {
        const QJsonObject o = v.toObject();
        ManifestFile f;
        f.path = o.value("path").toString();
        const double size = o.value("size").toDouble(-1);
        f.sha256 = o.value("sha256").toString().toLower();
        if (!isSafeRelativePath(f.path)) {
            *error = QCoreApplication::translate("Zero::Backup", "The backup contains an unsafe file path and was rejected: %1").arg(f.path);
            return false;
        }
        if (size < 0 || !isHex64(f.sha256)) {
            *error = QCoreApplication::translate("Zero::Backup", "The backup manifest is incomplete.");
            return false;
        }
        f.size = qint64(size);
        // case-insensitive: two paths that differ by case would overwrite each other on Windows
        if (seen.contains(f.path.toLower())) {
            *error = QCoreApplication::translate("Zero::Backup", "The backup lists the same file twice: %1").arg(f.path);
            return false;
        }
        seen.insert(f.path.toLower());
        sum += f.size;
        m.files.append(f);
    }
    if (m.totalBytes != sum) {
        *error = QCoreApplication::translate("Zero::Backup", "The backup manifest does not add up.");
        return false;
    }
    *out = m;
    return true;
}

bool readManifest(const QString& zipPath, Manifest* out, QString* error)
{
    MMCZip::ArchiveReader reader(zipPath);
    const auto data = reader.readFile(QString::fromLatin1(kManifestName));
    if (!data) {
        *error = data.error() == QLatin1String("File not found") ? QCoreApplication::translate("Zero::Backup", "This is not a ZERO backup (no manifest).")
                                                                 : QCoreApplication::translate("Zero::Backup", "The backup cannot be read: %1").arg(data.error());
        return false;
    }
    return parseManifest(*data, out, error);
}

// ---------------------------------------------------------------- hashing / traversal

struct FileItem {
    QString rel;
    QString abs;
    qint64 size = 0;
    QString sha256;
};

bool hashFile(const QString& path, QString* sha, qint64* size, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QCoreApplication::translate("Zero::Backup", "Could not read %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray buffer(kChunk, Qt::Uninitialized);
    qint64 total = 0;
    for (;;) {
        const qint64 n = file.read(buffer.data(), buffer.size());
        if (n < 0) {
            *error = QCoreApplication::translate("Zero::Backup", "Could not read %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
            return false;
        }
        if (n == 0)
            break;
        hash.addData(QByteArrayView(buffer.constData(), n));
        total += n;
    }
    *sha = QString::fromLatin1(hash.result().toHex());
    *size = total;
    return true;
}

/// Regular files below `root`, links (symbolic or junction) neither followed nor listed, `session.lock` of the world root excluded.
/// Same traversal rules as Worlds::folderSize. `exclude` (optional) leaves out files and whole folders by their path relative to `root`.
QList<FileItem> collectFiles(const QString& root, const std::function<bool(const QString&, bool)>& exclude = {})
{
    QList<FileItem> out;
    const QString base = QDir(root).absolutePath();
    QStringList pending{ base };
    while (!pending.isEmpty()) {
        const QString dir = pending.takeLast();
        for (const auto& entry : QDirListing(dir, QDirListing::IteratorFlag::IncludeHidden)) {
            if (entry.isSymLink() || entry.fileInfo().isJunction())
                continue;
            if (entry.isDir()) {
                if (exclude && exclude(entry.filePath().mid(base.size() + 1), true))
                    continue;
                pending.append(entry.filePath());
            } else if (entry.isFile()) {
                FileItem item;
                item.abs = entry.filePath();
                item.rel = item.abs.mid(base.size() + 1);
                if (item.rel == QLatin1String("session.lock"))
                    continue;
                if (exclude && exclude(item.rel, false))
                    continue;
                item.size = entry.size();
                out.append(item);
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const FileItem& a, const FileItem& b) { return a.rel < b.rel; });
    return out;
}

// ---------------------------------------------------------------- names

struct NameParts {
    QString stamp;  // yyyyMMddHHmmsszzz (UTC), fixed width: compares like the time
    int seq = 1;
    Kind kind = Kind::Manual;
};

bool parseBackupName(const QString& fileName, NameParts* out)
{
    static const QRegularExpression re(QStringLiteral("^(\\d{8})-(\\d{6})-(\\d{3})(?:-(\\d+))?-(manual|safety|auto)\\.zip$"));
    const QRegularExpressionMatch m = re.match(fileName);
    if (!m.hasMatch())
        return false;
    out->stamp = m.captured(1) + m.captured(2) + m.captured(3);
    out->seq = m.captured(4).isEmpty() ? 1 : m.captured(4).toInt();
    bool kindOk = false;
    out->kind = kindFromString(m.captured(5), &kindOk);
    return true;
}

bool newerName(const NameParts& a, const NameParts& b)
{
    return a.stamp != b.stamp ? a.stamp > b.stamp : a.seq > b.seq;
}

QString uniqueZipPath(const QString& backupDir, Kind kind)
{
    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    for (int n = 1;; ++n) {
        const QString suffix = n == 1 ? QString() : QStringLiteral("-%1").arg(n);
        const QString path = QDir(backupDir).filePath(stamp + suffix + QLatin1Char('-') + kindToString(kind) + QStringLiteral(".zip"));
        if (!QFileInfo::exists(path) && !QFileInfo::exists(path + QStringLiteral(".part")))
            return path;
    }
}

bool samePath(const QString& a, const QString& b)
{
    return QDir::cleanPath(QFileInfo(a).absoluteFilePath()).compare(QDir::cleanPath(QFileInfo(b).absoluteFilePath()),
#ifdef Q_OS_WIN
                                                                    Qt::CaseInsensitive
#else
                                                                    Qt::CaseSensitive
#endif
                                                                    ) == 0;
}

// ---------------------------------------------------------------- archive reading

/// Streams every entry of `zipPath`, checks its path and records size + hash. On success `manifest` holds the parsed manifest and
/// `seen` maps each world file (as stored) to its measured size + sha256.
struct Seen {
    qint64 size = 0;
    QString sha256;
};

bool scanArchive(const QString& zipPath, Manifest* manifest, QHash<QString, Seen>* seen, QString* error)
{
    QByteArray manifestBytes;
    bool haveManifest = false;
    QSet<QString> lowered;

    MMCZip::ArchiveReader reader(zipPath);
    const std::function<Result<>(MMCZip::ArchiveReader::File*)> visit = [&](MMCZip::ArchiveReader::File* f) -> Result<> {
        const QString name = f->filename();
        if (!f->isFile())
            return std::unexpected{ QCoreApplication::translate("Zero::Backup", "The backup contains an unexpected entry: %1").arg(name) };
        if (name == QLatin1String(kManifestName)) {
            if (haveManifest)
                return std::unexpected{ QCoreApplication::translate("Zero::Backup", "The backup contains two manifests.") };
            haveManifest = true;
            TRY(f->readBlocks([&](const char* data, qint64 len) {
                manifestBytes.append(data, len);
                return manifestBytes.size() <= kMaxManifestBytes;
            }));
            return {};
        }
        if (!name.startsWith(QLatin1String(kWorldPrefix)))
            return std::unexpected{ QCoreApplication::translate("Zero::Backup", "The backup contains an unexpected entry: %1").arg(name) };
        const QString rel = name.mid(int(sizeof(kWorldPrefix)) - 1);
        if (!isSafeRelativePath(rel))
            return std::unexpected{ QCoreApplication::translate("Zero::Backup", "The backup contains an unsafe file path and was rejected: %1").arg(name) };
        if (lowered.contains(rel.toLower()))
            return std::unexpected{ QCoreApplication::translate("Zero::Backup", "The backup contains the same file twice: %1").arg(rel) };
        lowered.insert(rel.toLower());

        QCryptographicHash hash(QCryptographicHash::Sha256);
        Seen s;
        TRY(f->readBlocks([&](const char* data, qint64 len) {
            hash.addData(QByteArrayView(data, len));
            s.size += len;
            return true;
        }));
        s.sha256 = QString::fromLatin1(hash.result().toHex());
        seen->insert(rel, s);
        return {};
    };
    const auto parsed = reader.parse(visit);
    if (!parsed) {
        *error = parsed.error();
        return false;
    }
    if (!haveManifest) {
        *error = QCoreApplication::translate("Zero::Backup", "This is not a ZERO backup (no manifest).");
        return false;
    }
    return parseManifest(manifestBytes, manifest, error);
}

}  // namespace

// ---------------------------------------------------------------- public: names, paths

QString kindToString(Kind kind)
{
    switch (kind) {
        case Kind::Safety:
            return QStringLiteral("safety");
        case Kind::Auto:
            return QStringLiteral("auto");
        case Kind::Manual:
            break;
    }
    return QStringLiteral("manual");
}

Kind kindFromString(const QString& text, bool* ok)
{
    if (ok)
        *ok = text == QLatin1String("manual") || text == QLatin1String("safety") || text == QLatin1String("auto");
    if (text == QLatin1String("safety"))
        return Kind::Safety;
    return text == QLatin1String("auto") ? Kind::Auto : Kind::Manual;
}

QString backupsRoot(const QString& dataRoot)
{
    return QDir(dataRoot).filePath(QStringLiteral("zero/backups"));
}

QString backupDirFor(const QString& dataRoot, const QString& instanceId, const QString& worldFolder)
{
    return QDir(backupsRoot(dataRoot)).filePath(instanceId + QLatin1Char('/') + worldFolder);
}

bool isSafeRelativePath(const QString& path)
{
    if (path.isEmpty() || path.startsWith(QLatin1Char('/')) || path.endsWith(QLatin1Char('/')))
        return false;
    static const QSet<QString> reserved = { "con",  "prn",  "aux",  "nul",  "com1", "com2", "com3", "com4", "com5", "com6", "com7",
                                            "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9" };
    for (const QString& segment : path.split(QLatin1Char('/'))) {
        if (segment.isEmpty() || segment == QLatin1String(".") || segment == QLatin1String(".."))
            return false;
        if (segment.endsWith(QLatin1Char('.')) || segment.endsWith(QLatin1Char(' ')))
            return false;
        if (reserved.contains(segment.section(QLatin1Char('.'), 0, 0).trimmed().toLower()))
            return false;
        for (QChar c : segment) {
            if (c.unicode() < 0x20 || c == QLatin1Char('\\') || c == QLatin1Char(':') || c == QLatin1Char('<') || c == QLatin1Char('>') ||
                c == QLatin1Char('"') || c == QLatin1Char('|') || c == QLatin1Char('?') || c == QLatin1Char('*'))
                return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------- createBackup

BackupResult createBackup(const QString& worldFolderPath, const QString& backupDir, Kind kind, const BackupMeta& meta, const Hooks& hooksIn,
                          const Options& options)
{
    BackupResult result;
    const Hooks hooks = resolve(hooksIn);
    auto fail = [&](const QString& message) {
        result.ok = false;
        result.error = message;
        result.zipPath.clear();
        return result;
    };
    auto stopped = [&]() { return options.cancelled && options.cancelled(); };
    auto cancel = [&]() {
        result.cancelled = true;
        return fail(QCoreApplication::translate("Zero::Backup", "Cancelled."));
    };

    if (!QFileInfo(worldFolderPath).isDir())
        return fail(QCoreApplication::translate("Zero::Backup", "The world folder does not exist."));
    if (!makePath(backupDir))
        return fail(QCoreApplication::translate("Zero::Backup", "Could not create the backup folder."));

    QList<FileItem> files = collectFiles(worldFolderPath, options.exclude);
    if (stopped())
        return cancel();
    qint64 total = 0;
    for (const FileItem& f : std::as_const(files)) {
        if (!isSafeRelativePath(f.rel))
            return fail(options.unsafeName ? options.unsafeName(f.rel) : QCoreApplication::translate("Zero::Backup", "This world contains a file name that cannot be backed up safely: %1").arg(f.rel));
        total += f.size;
    }

    const qint64 needed = qint64(double(total) * 1.1) + kSlackBytes;
    const qint64 available = hooks.freeBytes(backupDir);
    if (available >= 0 && available < needed)
        return fail(QCoreApplication::translate("Zero::Backup", "Not enough free disk space for a backup (needs about %1).").arg(sizeText(needed)));

    // hash first (streamed), so the manifest can be the first entry of the archive
    Manifest manifest;
    manifest.kind = kind;
    manifest.createdMs = QDateTime::currentMSecsSinceEpoch();
    manifest.meta = meta;
    manifest.extra = options.extra;
    QString error;
    const qint64 passTotal = std::max<qint64>(total, 1) * 3;  // hashing, writing, verifying
    qint64 done = 0;
    auto report = [&]() {
        if (options.progress)
            options.progress(std::min(done, passTotal), passTotal);
    };
    for (FileItem& f : files) {
        if (stopped())
            return cancel();
        if (!hashFile(f.abs, &f.sha256, &f.size, &error))
            return fail(error);
        manifest.files.append({ f.rel, f.size, f.sha256 });
        manifest.totalBytes += f.size;
        done += f.size;
        report();
    }

    const QString finalPath = options.fileName.isEmpty() ? uniqueZipPath(backupDir, kind) : QDir(backupDir).filePath(options.fileName);
    if (!options.fileName.isEmpty() && (QFileInfo::exists(finalPath) || QFileInfo::exists(finalPath + QStringLiteral(".part"))))
        return fail(QCoreApplication::translate("Zero::Backup", "The backup file already exists."));
    const QString partPath = finalPath + QStringLiteral(".part");
    auto discardPart = [&]() { QFile::remove(partPath); };

    {
        MMCZip::ArchiveWriter writer(partPath);
        if (auto r = writer.open(); !r) {
            discardPart();
            return fail(QCoreApplication::translate("Zero::Backup", "Could not write the backup: %1").arg(r.error()));
        }
        if (auto r = writer.addFile(QString::fromLatin1(kManifestName), manifestToJson(manifest)); !r) {
            writer.close();
            discardPart();
            return fail(QCoreApplication::translate("Zero::Backup", "Could not write the backup: %1").arg(r.error()));
        }
        for (const FileItem& f : std::as_const(files)) {
            if (stopped()) {
                writer.close();
                discardPart();
                return cancel();
            }
            if (auto r = writer.addFile(f.abs, QString::fromLatin1(kWorldPrefix) + f.rel); !r) {
                writer.close();
                discardPart();
                return fail(QCoreApplication::translate("Zero::Backup", "Could not write the backup: %1").arg(r.error()));
            }
            done += f.size;
            report();
        }
        if (auto r = writer.close(); !r) {
            discardPart();
            return fail(QCoreApplication::translate("Zero::Backup", "Could not write the backup: %1").arg(r.error()));
        }
    }

    // verify before the backup may look valid: every entry against the manifest
    if (stopped()) {
        discardPart();
        return cancel();
    }
    const VerifyResult verified = verifyBackup(partPath);
    if (!verified.ok) {
        discardPart();
        return fail(QCoreApplication::translate("Zero::Backup", "The backup could not be verified and was discarded: %1").arg(verified.error));
    }
    if (!QFile::rename(partPath, finalPath)) {
        discardPart();
        return fail(QCoreApplication::translate("Zero::Backup", "Could not finish the backup file."));
    }
    done = passTotal;
    report();
    result.ok = true;
    result.zipPath = finalPath;
    return result;
}

ManifestInfo readBackupInfo(const QString& zipPath)
{
    ManifestInfo info;
    Manifest manifest;
    if (!readManifest(zipPath, &manifest, &info.error))
        return info;
    info.ok = true;
    info.totalBytes = manifest.totalBytes;
    info.createdMs = manifest.createdMs;
    info.meta = manifest.meta;
    info.extra = manifest.extra;
    return info;
}

// ---------------------------------------------------------------- listBackups / verifyBackup

QList<BackupInfo> listBackups(const QString& backupDir)
{
    QList<BackupInfo> out;
    const QFileInfoList zips = QDir(backupDir).entryInfoList({ QStringLiteral("*.zip") }, QDir::Files, QDir::NoSort);
    for (const QFileInfo& zip : zips) {
        BackupInfo info;
        info.zipPath = zip.absoluteFilePath();
        info.fileSize = zip.size();
        NameParts name;
        const bool named = parseBackupName(zip.fileName(), &name);
        info.kind = named ? name.kind : Kind::Manual;
        info.createdMs = zip.lastModified().toMSecsSinceEpoch();
        Manifest manifest;
        QString error;
        if (readManifest(info.zipPath, &manifest, &error)) {
            info.valid = true;
            info.kind = manifest.kind;
            info.createdMs = manifest.createdMs;
            info.worldName = manifest.meta.worldName;
            info.totalBytes = manifest.totalBytes;
        }
        out.append(info);
    }
    std::sort(out.begin(), out.end(), [](const BackupInfo& a, const BackupInfo& b) {
        return a.createdMs != b.createdMs ? a.createdMs > b.createdMs : a.zipPath > b.zipPath;
    });
    return out;
}

VerifyResult verifyBackup(const QString& zipPath)
{
    VerifyResult result;
    Manifest manifest;
    QHash<QString, Seen> seen;
    QString error;
    if (!scanArchive(zipPath, &manifest, &seen, &error)) {
        result.error = error;
        return result;
    }
    for (const ManifestFile& f : std::as_const(manifest.files)) {
        const auto it = seen.constFind(f.path);
        if (it == seen.constEnd()) {
            result.error = QCoreApplication::translate("Zero::Backup", "The backup is missing a file: %1").arg(f.path);
            return result;
        }
        if (it->size != f.size || it->sha256 != f.sha256) {
            result.error = QCoreApplication::translate("Zero::Backup", "The backup is damaged (checksum mismatch): %1").arg(f.path);
            return result;
        }
    }
    if (seen.size() != manifest.files.size()) {
        result.error = QCoreApplication::translate("Zero::Backup", "The backup contains files that its manifest does not list.");
        return result;
    }
    result.ok = true;
    return result;
}

// ---------------------------------------------------------------- restoreBackup

namespace {

/// Writes every world entry below `staging` (paths validated again, only files listed in the manifest) and then re-reads the staged
/// files from disk and compares size + sha256 with the manifest.
bool extractAndVerify(const QString& zipPath, const Manifest& manifest, const QString& staging, QString* error)
{
    QHash<QString, const ManifestFile*> wanted;
    for (const ManifestFile& f : manifest.files)
        wanted.insert(f.path, &f);
    const QString stagingClean = QDir::cleanPath(QDir(staging).absolutePath());
    QSet<QString> written;

    MMCZip::ArchiveReader reader(zipPath);
    const std::function<Result<>(MMCZip::ArchiveReader::File*)> visit = [&](MMCZip::ArchiveReader::File* f) -> Result<> {
        const QString name = f->filename();
        if (name == QLatin1String(kManifestName))
            return f->skip();
        if (!f->isFile() || !name.startsWith(QLatin1String(kWorldPrefix)))
            return std::unexpected{ QCoreApplication::translate("Zero::Backup", "The backup contains an unexpected entry: %1").arg(name) };
        const QString rel = name.mid(int(sizeof(kWorldPrefix)) - 1);
        if (!isSafeRelativePath(rel) || !wanted.contains(rel) || written.contains(rel))
            return std::unexpected{ QCoreApplication::translate("Zero::Backup", "The backup contains an unsafe or unexpected file path and was rejected: %1").arg(name) };
        const QString target = QDir::cleanPath(stagingClean + QLatin1Char('/') + rel);
        if (!target.startsWith(stagingClean + QLatin1Char('/')))  // belt and braces after isSafeRelativePath
            return std::unexpected{ QCoreApplication::translate("Zero::Backup", "The backup contains an unsafe file path and was rejected: %1").arg(name) };
        if (!makePath(QFileInfo(target).absolutePath()))
            return std::unexpected{ QCoreApplication::translate("Zero::Backup", "Could not create a folder for %1").arg(rel) };
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly))
            return std::unexpected{ QCoreApplication::translate("Zero::Backup", "Could not write %1: %2").arg(rel, out.errorString()) };
        bool writeOk = true;
        TRY(f->readBlocks([&](const char* data, qint64 len) {
            writeOk = out.write(data, len) == len;
            return writeOk;
        }));
        if (!out.flush() || !writeOk) {
            return std::unexpected{ QCoreApplication::translate("Zero::Backup", "Could not write %1: %2").arg(rel, out.errorString()) };
        }
        out.close();
        written.insert(rel);
        return {};
    };
    const auto extracted = reader.parse(visit);
    if (!extracted) {
        *error = extracted.error() == QLatin1String("Aborted") ? QCoreApplication::translate("Zero::Backup", "Could not write the restored files (disk full?).") : extracted.error();
        return false;
    }

    for (const ManifestFile& f : std::as_const(manifest.files)) {
        const QString path = stagingClean + QLatin1Char('/') + f.path;
        QString sha;
        qint64 size = 0;
        if (!QFileInfo(path).isFile() || !hashFile(path, &sha, &size, error)) {
            if (error->isEmpty())
                *error = QCoreApplication::translate("Zero::Backup", "The restored files are incomplete: %1").arg(f.path);
            return false;
        }
        if (size != f.size || sha != f.sha256) {
            *error = QCoreApplication::translate("Zero::Backup", "The restored files do not match the backup: %1").arg(f.path);
            return false;
        }
    }
    return true;
}

}  // namespace

VerifyResult extractBackup(const QString& zipPath, const QString& stagingFolder)
{
    VerifyResult result;
    const VerifyResult verified = verifyBackup(zipPath);
    if (!verified.ok) {
        result.error = QCoreApplication::translate("Zero::Backup", "This backup is damaged and was not restored: %1").arg(verified.error);
        return result;
    }
    Manifest manifest;
    QString error;
    if (!readManifest(zipPath, &manifest, &error)) {
        result.error = error;
        return result;
    }
    if (!makePath(stagingFolder)) {
        result.error = QCoreApplication::translate("Zero::Backup", "Could not create a temporary folder next to the world.");
        return result;
    }
    if (!extractAndVerify(zipPath, manifest, stagingFolder, &error)) {
        result.error = error;
        return result;
    }
    result.ok = true;
    return result;
}

RestoreResult restoreBackup(const QString& zipPath, const QString& worldFolderPath, const QString& backupDir, const BackupMeta& meta,
                            const Hooks& hooksIn)
{
    RestoreResult result;
    const Hooks hooks = resolve(hooksIn);
    auto fail = [&](const QString& message) {
        result.ok = false;
        result.error = message;
        return result;
    };

    // a. verify the backup before anything else happens
    const VerifyResult verified = verifyBackup(zipPath);
    if (!verified.ok)
        return fail(QCoreApplication::translate("Zero::Backup", "This backup is damaged and was not restored: %1").arg(verified.error));
    Manifest manifest;
    QString error;
    if (!readManifest(zipPath, &manifest, &error))
        return fail(error);

    const QFileInfo world(worldFolderPath);
    const QString saves = world.absolutePath();
    const QString folder = world.fileName();
    if (folder.isEmpty() || folder.startsWith(QStringLiteral(".zero-")))
        return fail(QCoreApplication::translate("Zero::Backup", "Invalid world folder."));
    if (!makePath(saves))
        return fail(QCoreApplication::translate("Zero::Backup", "Could not open the saves folder."));

    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddHHmmsszzz"));
    const QString staging = QDir(saves).filePath(QStringLiteral(".zero-restore-%1-%2").arg(folder, stamp));
    const QString replaced = QDir(saves).filePath(QStringLiteral(".zero-replaced-%1-%2").arg(folder, stamp));
    if (QFileInfo::exists(staging) || QFileInfo::exists(replaced))
        return fail(QCoreApplication::translate("Zero::Backup", "A restore is already in progress for this world."));

    const qint64 needed = qint64(double(manifest.totalBytes) * 1.1) + kSlackBytes;
    const qint64 available = hooks.freeBytes(saves);
    if (available >= 0 && available < needed)
        return fail(QCoreApplication::translate("Zero::Backup", "Not enough free disk space to restore (needs about %1).").arg(sizeText(needed)));

    // b. the current world is saved first, verified like any other backup
    const bool hadWorld = world.exists();
    if (hadWorld) {
        const BackupResult safety = createBackup(worldFolderPath, backupDir, Kind::Safety, meta, hooks);
        if (!safety.ok)
            return fail(QCoreApplication::translate("Zero::Backup", "The current world could not be saved first, so nothing was changed: %1").arg(safety.error));
        result.safetyZip = safety.zipPath;
    }

    // c. extract into a sibling staging folder and verify what landed on disk
    auto dropStaging = [&]() { QDir(staging).removeRecursively(); };
    if (!makePath(staging))
        return fail(QCoreApplication::translate("Zero::Backup", "Could not create a temporary folder next to the world."));
    if (!extractAndVerify(zipPath, manifest, staging, &error)) {
        dropStaging();
        return fail(error);
    }

    // d. swap with renames; the old world stays on disk until the new one is in place
    if (hadWorld && !hooks.rename(worldFolderPath, replaced)) {
        dropStaging();
        return fail(QCoreApplication::translate("Zero::Backup", "The current world could not be replaced. Is it open in another program?"));
    }
    if (!hooks.rename(staging, worldFolderPath)) {
        QString message = QCoreApplication::translate("Zero::Backup", "The restored world could not be moved into place.");
        if (hadWorld && !hooks.rename(replaced, worldFolderPath)) {
            message += QLatin1Char(' ') + QCoreApplication::translate("Zero::Backup", "Your original world is in %1").arg(QDir::toNativeSeparators(replaced));
            qWarning() << "ZERO restore: rollback failed, original world is in" << replaced;
        }
        dropStaging();
        return fail(message);
    }

    // e. the old world is also in the safety backup: a failure here must not fail the restore. It gets a readable name
    //    first, so the Recycle Bin shows which world it was (and "Restore" there brings it back as a separate world).
    if (hadWorld) {
        const QString readable = QDir(saves).filePath(
            QCoreApplication::translate("Zero::Backup", "%1 (replaced %2)").arg(folder, QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH.mm.ss"))));
        const bool renamed = !QFileInfo::exists(readable) && hooks.rename(replaced, readable);
        if (!hooks.trashPath(renamed ? readable : replaced)) {
            // Keep the leftover hidden (".zero-*", like an interrupted restore) instead of showing up as an extra world.
            const QString left = renamed && !hooks.rename(readable, replaced) ? readable : replaced;
            qWarning() << "ZERO restore: could not remove the replaced world folder" << left << "(left in place, safe to delete)";
        }
    }

    result.ok = true;
    return result;
}

// ---------------------------------------------------------------- retention / delete

int applyRetention(const QString& backupDir, const QStringList& protectedZips, int keepManual, int keepSafety, int keepAuto)
{
    struct Candidate {
        QString path;
        NameParts name;
    };
    QList<Candidate> manual, safety, autos;
    const QFileInfoList files = QDir(backupDir).entryInfoList({ QStringLiteral("*.zip") }, QDir::Files, QDir::NoSort);
    for (const QFileInfo& fi : files) {
        Candidate c;
        if (!parseBackupName(fi.fileName(), &c.name) || fi.isSymLink())
            continue;  // not ZERO's: never touched
        c.path = fi.absoluteFilePath();
        (c.name.kind == Kind::Safety ? safety : c.name.kind == Kind::Auto ? autos : manual).append(c);
    }

    int removed = 0;
    auto prune = [&](QList<Candidate>& list, int keep) {
        std::sort(list.begin(), list.end(), [](const Candidate& a, const Candidate& b) { return newerName(a.name, b.name); });
        for (int i = std::max(0, keep); i < list.size(); ++i) {
            const bool isProtected = std::any_of(protectedZips.begin(), protectedZips.end(),
                                                 [&](const QString& p) { return !p.isEmpty() && samePath(p, list.at(i).path); });
            if (isProtected)
                continue;
            if (QFile::remove(list.at(i).path))
                ++removed;
            else
                qWarning() << "ZERO backup retention: could not remove" << list.at(i).path;
        }
    };
    prune(manual, keepManual);
    prune(safety, keepSafety);
    prune(autos, keepAuto);
    return removed;
}

bool deleteBackup(const QString& zipPath, const QString& backupsRootDir, QString* error, const Hooks& hooksIn)
{
    const Hooks hooks = resolve(hooksIn);
    auto fail = [&](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    const QString root = QDir::cleanPath(QFileInfo(backupsRootDir).absoluteFilePath());
    const QFileInfo info(zipPath);
    const QString path = QDir::cleanPath(info.absoluteFilePath());
    if (!path.startsWith(root + QLatin1Char('/'), Qt::CaseInsensitive) || info.suffix().compare(QLatin1String("zip"), Qt::CaseInsensitive) != 0)
        return fail(QCoreApplication::translate("Zero::Backup", "This file is not a ZERO backup."));
    if (!info.isFile() || info.isSymLink())
        return fail(QCoreApplication::translate("Zero::Backup", "The backup no longer exists."));
    if (!hooks.trashPath(path))
        return fail(QCoreApplication::translate("Zero::Backup", "The backup could not be deleted."));
    return true;
}

}  // namespace Backup
}  // namespace Zero
