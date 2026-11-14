// SPDX-License-Identifier: GPL-3.0-only
#include "Payload.h"

#include "SafePath.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QtEndian>
#include <algorithm>

namespace ZeroSetup {

namespace {
constexpr char kTrailerMagic[17] = "ZEROSETUPPAYLOAD";  // 16 bytes + NUL
constexpr char kPayloadMagic[5] = "ZSP1";
constexpr quint32 kMaxManifest = 64u << 20;
constexpr quint32 kMaxChunkComp = 2u * kChunkRaw;
constexpr quint64 kMaxFileSize = 1ull << 40;
constexpr int kMaxFiles = 200000;

void setErr(QString* e, const QString& m)
{
    if (e)
        *e = m;
}

QByteArray le32(quint32 v)
{
    QByteArray b(4, 0);
    qToLittleEndian(v, b.data());
    return b;
}
QByteArray le64(quint64 v)
{
    QByteArray b(8, 0);
    qToLittleEndian(v, b.data());
    return b;
}
}  // namespace

QByteArray makeTrailer(quint64 payloadOffset, quint64 payloadSize, const QByteArray& sha256)
{
    QByteArray t(kTrailerMagic, 16);
    t += le64(payloadOffset);
    t += le64(payloadSize);
    t += sha256.left(32).leftJustified(32, 0);
    return t;
}

OpenStatus PayloadReader::open(const QString& exePath, QString* error)
{
    m_exe = exePath;
    m_info = {};
    QFile f(exePath);
    if (!f.open(QIODevice::ReadOnly)) {
        setErr(error, QStringLiteral("cannot read the setup file: ") + f.errorString());
        return OpenStatus::NoPayload;
    }
    const qint64 fileSize = f.size();
    if (fileSize < kTrailerSize + 16) {
        setErr(error, QStringLiteral("no payload"));
        return OpenStatus::NoPayload;
    }
    f.seek(fileSize - kTrailerSize);
    const QByteArray t = f.read(kTrailerSize);
    if (t.size() != kTrailerSize || memcmp(t.constData(), kTrailerMagic, 16) != 0) {
        setErr(error, QStringLiteral("no payload trailer"));
        return OpenStatus::NoPayload;
    }
    const quint64 offset = qFromLittleEndian<quint64>(t.constData() + 16);
    const quint64 size = qFromLittleEndian<quint64>(t.constData() + 24);
    auto corrupt = [&](const QString& m) {
        setErr(error, m);
        return OpenStatus::Corrupt;
    };
    if (offset == 0 || size < 8 || offset > quint64(fileSize) || size > quint64(fileSize) || offset + size + kTrailerSize != quint64(fileSize))
        return corrupt(QStringLiteral("payload bounds do not match the file size"));
    m_info.payloadOffset = offset;
    m_info.payloadSize = size;
    m_info.stubSize = offset;
    m_info.payloadSha256 = t.mid(32, 32);

    f.seek(qint64(offset));
    char head[8] = {};
    if (f.read(head, 8) != 8 || memcmp(head, kPayloadMagic, 4) != 0)
        return corrupt(QStringLiteral("bad payload magic"));
    const quint32 mlen = qFromLittleEndian<quint32>(head + 4);
    if (mlen == 0 || mlen > kMaxManifest || quint64(mlen) + 8 > size)
        return corrupt(QStringLiteral("bad manifest size"));
    const QByteArray mj = f.read(mlen);
    if (quint32(mj.size()) != mlen)
        return corrupt(QStringLiteral("truncated manifest"));
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(mj, &pe);
    if (!doc.isObject())
        return corrupt(QStringLiteral("manifest is not valid JSON"));
    const QJsonObject root = doc.object();
    if (root.value("format").toInt() != 1)
        return corrupt(QStringLiteral("unsupported payload format"));
    m_info.version = root.value("version").toString();
    const QJsonArray files = root.value("files").toArray();
    if (files.size() > kMaxFiles)
        return corrupt(QStringLiteral("too many files"));
    QSet<QString> seen;
    QSet<QString> dirs;
    quint64 total = 0;
    for (const QJsonValue& v : files) {
        const QJsonObject o = v.toObject();
        PayloadEntry e;
        e.path = o.value("path").toString();
        QString why;
        if (!isSafeRelativePath(e.path, &why))
            return corrupt(QStringLiteral("unsafe path in payload (%1): %2").arg(why, e.path.left(80)));
        const double sz = o.value("size").toDouble(-1);
        if (sz < 0 || sz > double(kMaxFileSize))
            return corrupt(QStringLiteral("bad file size for ") + e.path);
        e.size = quint64(sz);
        e.sha256 = QByteArray::fromHex(o.value("sha256").toString().toLatin1());
        if (e.sha256.size() != 32)
            return corrupt(QStringLiteral("bad hash for ") + e.path);
        const QString key = e.path.toLower();
        if (seen.contains(key) || dirs.contains(key))
            return corrupt(QStringLiteral("duplicate or conflicting path ") + e.path);
        seen.insert(key);
        for (int i = key.indexOf('/'); i >= 0; i = key.indexOf('/', i + 1)) {
            const QString d = key.left(i);
            if (seen.contains(d))
                return corrupt(QStringLiteral("file used as a folder: ") + d);
            dirs.insert(d);
        }
        total += e.size;
        m_info.files.push_back(e);
    }
    m_info.totalSize = total;
    m_dataOffset = offset + 8 + mlen;
    return OpenStatus::Ok;
}

bool PayloadReader::verifyHash(const std::atomic<bool>* cancel, QString* error) const
{
    QFile f(m_exe);
    if (!f.open(QIODevice::ReadOnly) || !f.seek(qint64(m_info.payloadOffset))) {
        setErr(error, QStringLiteral("cannot read the setup file"));
        return false;
    }
    QCryptographicHash h(QCryptographicHash::Sha256);
    quint64 left = m_info.payloadSize;
    while (left > 0) {
        if (cancel && cancel->load())
            return false;
        const QByteArray b = f.read(qint64(std::min<quint64>(left, 4u << 20)));
        if (b.isEmpty()) {
            setErr(error, QStringLiteral("the setup file is truncated"));
            return false;
        }
        h.addData(b);
        left -= quint64(b.size());
    }
    if (h.result() != m_info.payloadSha256) {
        setErr(error, QStringLiteral("the setup file is damaged (checksum mismatch)"));
        return false;
    }
    return true;
}

bool PayloadReader::extractAll(const QString& destDir, const std::atomic<bool>* cancel, const ByteProgress& progress, QString* error,
                               bool* cancelled) const
{
    if (cancelled)
        *cancelled = false;
    QFile in(m_exe);
    if (!in.open(QIODevice::ReadOnly) || !in.seek(qint64(m_dataOffset))) {
        setErr(error, QStringLiteral("cannot read the setup file"));
        return false;
    }
    const quint64 end = m_info.payloadOffset + m_info.payloadSize;
    quint64 pos = m_dataOffset;
    quint64 done = 0;
    for (const PayloadEntry& e : m_info.files) {
        if (!isSafeRelativePath(e.path) || !staysInside(destDir, e.path)) {
            setErr(error, QStringLiteral("refusing to write outside the install folder: ") + e.path);
            return false;
        }
        const QString dst = QDir::cleanPath(destDir + '/' + e.path);
        if (!QDir().mkpath(QFileInfo(dst).absolutePath())) {
            setErr(error, QStringLiteral("cannot create the folder for ") + e.path);
            return false;
        }
        QFile out(dst);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            setErr(error, QStringLiteral("cannot write %1 (%2)").arg(e.path, out.errorString()));
            return false;
        }
        QCryptographicHash h(QCryptographicHash::Sha256);
        quint64 got = 0;
        while (got < e.size) {
            if (cancel && cancel->load()) {
                if (cancelled)
                    *cancelled = true;
                return false;
            }
            if (pos + 4 > end) {
                setErr(error, QStringLiteral("payload ends early in ") + e.path);
                return false;
            }
            const QByteArray lenb = in.read(4);
            if (lenb.size() != 4) {
                setErr(error, QStringLiteral("truncated payload"));
                return false;
            }
            const quint32 clen = qFromLittleEndian<quint32>(lenb.constData());
            if (clen < 4 || clen > kMaxChunkComp || pos + 4 + clen > end) {
                setErr(error, QStringLiteral("bad chunk in ") + e.path);
                return false;
            }
            const QByteArray comp = in.read(clen);
            if (quint32(comp.size()) != clen) {
                setErr(error, QStringLiteral("truncated payload"));
                return false;
            }
            pos += 4 + clen;
            const quint32 expect = qFromBigEndian<quint32>(comp.constData());  // qCompress length prefix
            if (expect == 0 || expect > kChunkRaw || got + expect > e.size) {
                setErr(error, QStringLiteral("bad chunk size in ") + e.path);
                return false;
            }
            const QByteArray raw = qUncompress(comp);
            if (quint32(raw.size()) != expect) {
                setErr(error, QStringLiteral("a compressed block is damaged in ") + e.path);
                return false;
            }
            h.addData(raw);
            if (out.write(raw) != raw.size()) {
                setErr(error, QStringLiteral("cannot write %1 (%2)").arg(e.path, out.errorString()));
                return false;
            }
            got += expect;
            done += expect;
            if (progress)
                progress(done);
        }
        out.close();
        if (h.result() != e.sha256) {
            setErr(error, QStringLiteral("checksum mismatch in ") + e.path);
            return false;
        }
    }
    if (pos != end) {
        setErr(error, QStringLiteral("unexpected data after the last file"));
        return false;
    }
    return true;
}

bool packPayload(const QString& stubExe, const QString& stageDir, const QString& version, const QString& outExe, QString* error,
                 const std::function<void(const QString&)>& log)
{
    {
        PayloadReader probe;
        QString ignore;
        if (probe.open(stubExe, &ignore) != OpenStatus::NoPayload) {
            setErr(error, QStringLiteral("the stub exe already carries a payload"));
            return false;
        }
    }
    const QDir stage(stageDir);
    if (!stage.exists()) {
        setErr(error, QStringLiteral("stage folder not found: ") + stageDir);
        return false;
    }
    QStringList rels;
    QDirIterator it(stage.absolutePath(), QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        rels << stage.relativeFilePath(it.filePath());
    }
    if (rels.isEmpty()) {
        setErr(error, QStringLiteral("stage folder has no files"));
        return false;
    }
    std::sort(rels.begin(), rels.end());

    QJsonArray files;
    quint64 total = 0;
    for (const QString& rel : std::as_const(rels)) {
        QString why;
        if (!isSafeRelativePath(rel, &why)) {
            setErr(error, QStringLiteral("cannot pack %1: %2").arg(rel, why));
            return false;
        }
        QFile f(stage.absoluteFilePath(rel));
        if (!f.open(QIODevice::ReadOnly)) {
            setErr(error, QStringLiteral("cannot read ") + rel);
            return false;
        }
        QCryptographicHash h(QCryptographicHash::Sha256);
        if (!h.addData(&f)) {
            setErr(error, QStringLiteral("cannot hash ") + rel);
            return false;
        }
        QJsonObject o;
        o["path"] = rel;
        o["size"] = double(f.size());
        o["sha256"] = QString::fromLatin1(h.result().toHex());
        files.append(o);
        total += quint64(f.size());
    }
    QJsonObject root;
    root["format"] = 1;
    root["version"] = version;
    root["files"] = files;
    const QByteArray manifest = QJsonDocument(root).toJson(QJsonDocument::Compact);

    const QString tmpOut = outExe + QStringLiteral(".tmp");
    QFile out(tmpOut);
    QFile stub(stubExe);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || !stub.open(QIODevice::ReadOnly)) {
        setErr(error, QStringLiteral("cannot open the output or the stub exe"));
        return false;
    }
    while (!stub.atEnd())
        out.write(stub.read(4 << 20));
    const quint64 payloadOffset = quint64(out.pos());
    QCryptographicHash ph(QCryptographicHash::Sha256);
    auto put = [&](const QByteArray& b) {
        ph.addData(b);
        return out.write(b) == b.size();
    };
    bool ok = put(QByteArray(kPayloadMagic, 4)) && put(le32(quint32(manifest.size()))) && put(manifest);
    quint64 doneBytes = 0;
    for (const QString& rel : std::as_const(rels)) {
        if (!ok)
            break;
        QFile f(stage.absoluteFilePath(rel));
        if (!f.open(QIODevice::ReadOnly)) {
            ok = false;
            break;
        }
        while (!f.atEnd() && ok) {
            const QByteArray raw = f.read(kChunkRaw);
            if (raw.isEmpty())
                break;
            const QByteArray comp = qCompress(raw, 6);
            ok = put(le32(quint32(comp.size()))) && put(comp);
            doneBytes += quint64(raw.size());
        }
        if (log)
            log(QStringLiteral("packed %1 / %2 MB").arg(doneBytes >> 20).arg(total >> 20));
    }
    const quint64 payloadSize = quint64(out.pos()) - payloadOffset;
    ok = ok && out.write(makeTrailer(payloadOffset, payloadSize, ph.result())) == kTrailerSize;
    out.close();
    if (!ok) {
        QFile::remove(tmpOut);
        setErr(error, QStringLiteral("writing the output failed: ") + out.errorString());
        return false;
    }
    QFile::remove(outExe);
    if (!QFile::rename(tmpOut, outExe)) {
        setErr(error, QStringLiteral("cannot move the output into place"));
        return false;
    }
    return true;
}

}  // namespace ZeroSetup
