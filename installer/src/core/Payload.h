// SPDX-License-Identifier: GPL-3.0-only
// The setup payload: the launcher folder, compressed, appended to the setup exe. See installer/README.md "Payload format".
#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <atomic>
#include <functional>

namespace ZeroSetup {

constexpr int kTrailerSize = 64;
constexpr quint32 kChunkRaw = 1 << 20;  // 1 MiB of file data per compressed chunk

struct PayloadEntry
{
    QString path;       // relative, '/'-separated, validated by isSafeRelativePath
    quint64 size = 0;   // uncompressed size
    QByteArray sha256;  // 32 bytes
};

struct PayloadInfo
{
    QString version;           // "0.1.0"
    QVector<PayloadEntry> files;
    quint64 totalSize = 0;     // sum of file sizes
    quint64 stubSize = 0;      // bytes of the exe before the payload (= the uninstaller image)
    quint64 payloadOffset = 0; // absolute offset of the payload in the exe
    quint64 payloadSize = 0;
    QByteArray payloadSha256;  // from the trailer
};

enum class OpenStatus { Ok, NoPayload, Corrupt };

/// bytesDone counts uncompressed file bytes written so far.
using ByteProgress = std::function<void(quint64 bytesDone)>;

class PayloadReader
{
   public:
    /// Reads the trailer and the manifest only (cheap: the setup window opens instantly). Validates every path.
    OpenStatus open(const QString& exePath, QString* error);
    const PayloadInfo& info() const { return m_info; }
    const QString& exePath() const { return m_exe; }

    /// Hashes the whole payload and compares it with the trailer. Returns false on mismatch or cancel.
    bool verifyHash(const std::atomic<bool>* cancel, QString* error) const;

    /// Decompresses every file below destDir (which must exist), verifying size and SHA-256 per file. Stops with false
    /// (error set, or cancelled=true) on the first problem. Nothing outside destDir is ever written.
    bool extractAll(const QString& destDir, const std::atomic<bool>* cancel, const ByteProgress& progress, QString* error,
                    bool* cancelled = nullptr) const;

   private:
    QString m_exe;
    PayloadInfo m_info;
    quint64 m_dataOffset = 0;  // absolute offset of the first file chunk
};

/// Writes `outExe` = stub exe + payload built from every file below stageDir + trailer.
/// Fails when the stub already carries a payload, or when stageDir has no files.
bool packPayload(const QString& stubExe, const QString& stageDir, const QString& version, const QString& outExe, QString* error,
                 const std::function<void(const QString&)>& log = {});

/// Builds just the trailer bytes (exposed for tests).
QByteArray makeTrailer(quint64 payloadOffset, quint64 payloadSize, const QByteArray& sha256);

}  // namespace ZeroSetup
