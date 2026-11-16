// SPDX-License-Identifier: GPL-3.0-only
#include "JarScanner.h"

#include <archive.h>
#include <archive_entry.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <memory>

namespace Zero {
namespace Security {

namespace {

constexpr qint64 kClassCap = 32LL * 1024 * 1024;       // one .class file
constexpr qint64 kNestedJarCap = 128LL * 1024 * 1024;  // a jar nested in a jar, held in memory
constexpr int kBlockSize = 64 * 1024;

struct Matcher {
    const SignatureList& list;
    bool wantClassBytes = false;
    bool wantNames = false;
    bool wantSha1 = false;
    bool wantSha256 = false;

    explicit Matcher(const SignatureList& l) : list(l)
    {
        wantClassBytes = l.hasKind(Signature::Kind::ClassBytes);
        wantNames = l.hasKind(Signature::Kind::ClassName);
        wantSha1 = l.hasKind(Signature::Kind::Sha1);
        wantSha256 = l.hasKind(Signature::Kind::Sha256);
    }
    bool wantHash() const { return wantSha1 || wantSha256; }
};

void addHit(QList<Hit>& hits, QSet<QString>& seen, const Signature& s, const QString& where)
{
    if (seen.contains(s.id)) {
        return;
    }
    seen.insert(s.id);
    hits.append(Hit{ s.id, s.name, s.infoUrl, where });
}

void matchHash(const Matcher& m, const QByteArray& sha1Hex, const QByteArray& sha256Hex, const QString& where, QList<Hit>& hits,
               QSet<QString>& seen)
{
    for (const Signature& s : m.list.items) {
        if ((s.kind == Signature::Kind::Sha1 && !sha1Hex.isEmpty() && s.pattern == sha1Hex) ||
            (s.kind == Signature::Kind::Sha256 && !sha256Hex.isEmpty() && s.pattern == sha256Hex)) {
            addHit(hits, seen, s, where);
        }
    }
}

using ArchivePtr = std::unique_ptr<archive, int (*)(archive*)>;

/// Walks one open archive. `depth` 0 = the jar, 1 = a jar nested in it (whose own nested jars are not opened).
void walk(archive* a, const Matcher& m, int depth, const QString& prefix, QList<Hit>& hits, QSet<QString>& seen)
{
    archive_entry* entry = nullptr;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if ((archive_entry_filetype(entry) & AE_IFMT) != AE_IFREG) {
            continue;
        }
        QString name = QString::fromUtf8(archive_entry_pathname_utf8(entry));
        if (name.isEmpty()) {
            name = QString::fromLocal8Bit(archive_entry_pathname(entry));
        }
        if (name.startsWith(QLatin1Char('/'))) {
            name.remove(0, 1);
        }
        const bool isClass = name.endsWith(QLatin1String(".class"));
        const bool isJar = depth == 0 && name.endsWith(QLatin1String(".jar"), Qt::CaseInsensitive);

        if (m.wantNames && isClass) {
            const QByteArray path = name.toUtf8();
            for (const Signature& s : m.list.items) {
                if (s.kind == Signature::Kind::ClassName && (path.startsWith(s.pattern) || path.contains("/" + s.pattern))) {
                    addHit(hits, seen, s, prefix + name);
                }
            }
        }

        const bool readClass = isClass && m.wantClassBytes;
        const bool readJar = isJar && (m.wantClassBytes || m.wantNames || m.wantHash());
        if (!readClass && !readJar) {
            archive_read_data_skip(a);
            continue;
        }
        const qint64 cap = readJar ? kNestedJarCap : kClassCap;
        if (archive_entry_size_is_set(entry) && archive_entry_size(entry) > cap) {
            archive_read_data_skip(a);
            continue;
        }
        QByteArray data;
        const void* block = nullptr;
        size_t len = 0;
        la_int64_t offset = 0;
        int status = 0;
        bool tooBig = false;
        while ((status = archive_read_data_block(a, &block, &len, &offset)) == ARCHIVE_OK) {
            if (data.size() + qint64(len) > cap) {
                tooBig = true;
                break;
            }
            data.append(static_cast<const char*>(block), qsizetype(len));
        }
        if (tooBig) {
            archive_read_data_skip(a);
            continue;
        }
        if (status != ARCHIVE_EOF) {
            continue;  // damaged entry: nothing to match
        }
        if (readClass) {
            for (const Signature& s : m.list.items) {
                if (s.kind == Signature::Kind::ClassBytes && data.contains(s.pattern)) {
                    addHit(hits, seen, s, prefix + name);
                }
            }
        } else {
            if (m.wantHash()) {
                const QByteArray sha1 = m.wantSha1 ? QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex() : QByteArray();
                const QByteArray sha256 = m.wantSha256 ? QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex() : QByteArray();
                matchHash(m, sha1, sha256, prefix + name, hits, seen);
            }
            ArchivePtr nested(archive_read_new(), archive_read_free);
            archive_read_support_format_zip(nested.get());
            if (archive_read_open_memory(nested.get(), data.constData(), size_t(data.size())) == ARCHIVE_OK) {
                walk(nested.get(), m, 1, prefix + name + QStringLiteral(" > "), hits, seen);
            }
        }
    }
}

}  // namespace

JarScan scanJar(const QString& path, const SignatureList& list)
{
    JarScan result;
    if (!list.error.isEmpty() || list.items.isEmpty()) {
        return result;
    }
    const Matcher m(list);
    QSet<QString> seen;

    if (m.wantHash()) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            result.readable = false;
            result.error = file.errorString();
            return result;
        }
        QCryptographicHash sha1(QCryptographicHash::Sha1);
        QCryptographicHash sha256(QCryptographicHash::Sha256);
        QByteArray buffer(kBlockSize, Qt::Uninitialized);
        while (true) {
            const qint64 n = file.read(buffer.data(), buffer.size());
            if (n <= 0) {
                break;
            }
            if (m.wantSha1) {
                sha1.addData(QByteArrayView(buffer.constData(), n));
            }
            if (m.wantSha256) {
                sha256.addData(QByteArrayView(buffer.constData(), n));
            }
        }
        matchHash(m, m.wantSha1 ? sha1.result().toHex() : QByteArray(), m.wantSha256 ? sha256.result().toHex() : QByteArray(),
                  QStringLiteral("(the jar file)"), result.hits, seen);
    }

    ArchivePtr a(archive_read_new(), archive_read_free);
    archive_read_support_format_zip(a.get());
    const std::wstring wide = path.toStdWString();
    if (archive_read_open_filename_w(a.get(), wide.c_str(), kBlockSize) != ARCHIVE_OK) {
        result.readable = false;
        result.error = QString::fromUtf8(archive_error_string(a.get()));
        return result;
    }
    walk(a.get(), m, 0, QString(), result.hits, seen);
    archive_read_close(a.get());
    return result;
}

JarScan scanJarBytes(const QByteArray& bytes, const SignatureList& list)
{
    JarScan result;
    if (!list.error.isEmpty() || list.items.isEmpty()) {
        return result;
    }
    const Matcher m(list);
    QSet<QString> seen;
    if (m.wantHash()) {
        matchHash(m, m.wantSha1 ? QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex() : QByteArray(),
                  m.wantSha256 ? QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() : QByteArray(),
                  QStringLiteral("(the jar file)"), result.hits, seen);
    }
    ArchivePtr a(archive_read_new(), archive_read_free);
    archive_read_support_format_zip(a.get());
    if (archive_read_open_memory(a.get(), bytes.constData(), size_t(bytes.size())) != ARCHIVE_OK) {
        result.readable = false;
        result.error = QString::fromUtf8(archive_error_string(a.get()));
        return result;
    }
    walk(a.get(), m, 0, QString(), result.hits, seen);
    archive_read_close(a.get());
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------- cache

QString ScanCache::pathFor(const QString& instanceRoot)
{
    return QDir(instanceRoot).filePath(QStringLiteral(".zero/scan-cache.json"));
}

void ScanCache::load(const QString& path, const QString& listKey)
{
    m_path = path;
    m_listKey = listKey;
    m_jars.clear();
    m_modsFingerprint.clear();
    m_modsClean = false;
    m_dirty = false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return;
    }
    const QJsonObject root = doc.object();
    if (root.value(QLatin1String("listKey")).toString() != listKey) {
        m_dirty = true;  // rewritten with the new key on the next save
        return;
    }
    const QJsonObject jars = root.value(QLatin1String("jars")).toObject();
    for (auto it = jars.begin(); it != jars.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        CachedVerdict v;
        v.size = qint64(o.value(QLatin1String("size")).toDouble(-1));
        v.mtimeMs = qint64(o.value(QLatin1String("mtime")).toDouble());
        v.readable = o.value(QLatin1String("readable")).toBool(true);
        for (const QJsonValue& h : o.value(QLatin1String("hits")).toArray()) {
            const QJsonObject ho = h.toObject();
            v.hits.append({ ho.value(QLatin1String("id")).toString(), ho.value(QLatin1String("where")).toString() });
        }
        if (v.size >= 0) {
            m_jars.insert(it.key(), v);
        }
    }
    const QJsonObject mods = root.value(QLatin1String("modsCheck")).toObject();
    m_modsFingerprint = mods.value(QLatin1String("fingerprint")).toString();
    m_modsClean = mods.value(QLatin1String("clean")).toBool();
}

bool ScanCache::save()
{
    if (!m_dirty || m_path.isEmpty()) {
        return true;
    }
    QJsonObject jars;
    for (auto it = m_jars.begin(); it != m_jars.end(); ++it) {
        QJsonObject o;
        o.insert(QLatin1String("size"), double(it->size));
        o.insert(QLatin1String("mtime"), double(it->mtimeMs));
        if (!it->readable) {
            o.insert(QLatin1String("readable"), false);
        }
        QJsonArray hits;
        for (const auto& h : it->hits) {
            hits.append(QJsonObject{ { QLatin1String("id"), h.first }, { QLatin1String("where"), h.second } });
        }
        if (!hits.isEmpty()) {
            o.insert(QLatin1String("hits"), hits);
        }
        jars.insert(it.key(), o);
    }
    QJsonObject root;
    root.insert(QLatin1String("listKey"), m_listKey);
    root.insert(QLatin1String("jars"), jars);
    if (!m_modsFingerprint.isEmpty()) {
        root.insert(QLatin1String("modsCheck"),
                    QJsonObject{ { QLatin1String("fingerprint"), m_modsFingerprint }, { QLatin1String("clean"), m_modsClean } });
    }
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        return false;
    }
    m_dirty = false;
    return true;
}

bool ScanCache::lookup(const QString& fileName, qint64 size, qint64 mtimeMs, CachedVerdict* out) const
{
    const auto it = m_jars.constFind(fileName);
    if (it == m_jars.constEnd() || it->size != size || it->mtimeMs != mtimeMs) {
        return false;
    }
    if (out) {
        *out = *it;
    }
    return true;
}

void ScanCache::store(const QString& fileName, const CachedVerdict& verdict)
{
    m_jars.insert(fileName, verdict);
    m_dirty = true;
}

void ScanCache::keepOnly(const QStringList& liveFileNames)
{
    const QSet<QString> live(liveFileNames.begin(), liveFileNames.end());
    for (auto it = m_jars.begin(); it != m_jars.end();) {
        if (live.contains(it.key())) {
            ++it;
        } else {
            it = m_jars.erase(it);
            m_dirty = true;
        }
    }
}

bool ScanCache::modsCheckClean(const QString& fingerprint) const
{
    return m_modsClean && !fingerprint.isEmpty() && fingerprint == m_modsFingerprint;
}

void ScanCache::setModsCheck(const QString& fingerprint, bool clean)
{
    const bool nowClean = clean && !fingerprint.isEmpty();
    if (m_modsFingerprint == fingerprint && m_modsClean == nowClean) {
        return;
    }
    m_modsFingerprint = fingerprint;
    m_modsClean = nowClean;
    m_dirty = true;
}

// ---------------------------------------------------------------------------------------------------------------------- folder

FolderScan scanModsFolder(const QString& modsDir, const SignatureList& list, ScanCache& cache, const FolderScanOptions& options)
{
    FolderScan out;
    const QDir dir(modsDir);
    const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    QList<QFileInfo> jars;
    QStringList live;
    for (const QFileInfo& info : files) {
        if (info.isSymLink() || !info.fileName().endsWith(QLatin1String(".jar"), Qt::CaseInsensitive)) {
            continue;
        }
        jars.append(info);
        live.append(info.fileName());
    }
    out.jars = int(jars.size());
    if (!list.error.isEmpty() || list.items.isEmpty()) {
        return out;  // no usable list: nothing to match (the caller says so)
    }

    int done = 0;
    for (const QFileInfo& info : jars) {
        const QString name = info.fileName();
        const qint64 size = info.size();
        const qint64 mtime = info.lastModified().toMSecsSinceEpoch();
        CachedVerdict verdict;
        QList<Hit> hits;
        bool known = cache.lookup(name, size, mtime, &verdict);
        if (known) {
            for (const auto& h : verdict.hits) {  // a cached hit of a signature that is gone from the list is not trusted: scan again
                const Signature* s = list.find(h.first);
                if (!s) {
                    known = false;
                    hits.clear();
                    break;
                }
                hits.append(Hit{ s->id, s->name, s->infoUrl, h.second });
            }
        }
        if (known) {
            ++out.fromCache;
            if (!verdict.readable) {
                out.unreadable.append(name);
            }
        } else if (options.cachedOnly) {
            out.complete = false;
            return out;
        } else {
            const JarScan scan = scanJar(info.absoluteFilePath(), list);
            ++out.scanned;
            hits = scan.hits;
            verdict = CachedVerdict();
            verdict.size = size;
            verdict.mtimeMs = mtime;
            verdict.readable = scan.readable;
            for (const Hit& h : hits) {
                verdict.hits.append({ h.signatureId, h.where });
            }
            cache.store(name, verdict);
            if (!scan.readable) {
                out.unreadable.append(name);
            }
        }
        if (!hits.isEmpty()) {
            out.infected.append(InfectedJar{ name, info.absoluteFilePath(), hits });
        }
        ++done;
        if (options.progress) {
            options.progress(done, int(jars.size()));
        }
    }
    cache.keepOnly(live);
    return out;
}

}  // namespace Security
}  // namespace Zero
