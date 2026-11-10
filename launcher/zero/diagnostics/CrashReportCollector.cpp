// SPDX-License-Identifier: GPL-3.0-only
#include "CrashReportCollector.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include "BaseInstance.h"
#include "minecraft/MinecraftInstance.h"

namespace Zero::Diagnostics {

namespace {

/// Reads the tail (fromEnd) or head of a file, capped at `maxBytes`. Nothing is ever written.
bool readCapped(const QString& path, qint64 maxBytes, bool fromEnd, QString& out, bool& truncated)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const qint64 size = file.size();
    truncated = size > maxBytes;
    if (fromEnd && truncated)
        file.seek(size - maxBytes);
    QByteArray data = file.read(maxBytes);
    if (fromEnd && truncated) {
        // Drop the (probably partial) first line.
        const int nl = data.indexOf('\n');
        if (nl >= 0)
            data.remove(0, nl + 1);
    }
    out = QString::fromUtf8(data);
    return true;
}

/// Newest file matching `filters` in `dir`. `fresh` is set when it was modified at/after `sinceMs`.
bool newestFile(const QString& dir, const QStringList& filters, qint64 sinceMs, QFileInfo& result, bool& fresh)
{
    const QFileInfoList files = QDir(dir).entryInfoList(filters, QDir::Files | QDir::Readable, QDir::Time);
    if (files.isEmpty())
        return false;
    result = files.first();  // sorted newest first
    fresh = sinceMs <= 0 || result.lastModified().toMSecsSinceEpoch() >= sinceMs;
    return true;
}

void addFile(QList<LogSource>& out, const QFileInfo& info, qint64 maxBytes, bool fromEnd, bool older)
{
    LogSource src;
    src.name = info.fileName();
    src.path = info.absoluteFilePath();
    src.modified = info.lastModified();
    src.older = older;
    if (readCapped(src.path, maxBytes, fromEnd, src.text, src.truncated))
        out.append(src);
}

}  // namespace

QList<LogSource> CrashReportCollector::collect(MinecraftInstance* instance)
{
    if (!instance)
        return {};
    return collectFromGameRoot(instance->gameRoot(), instance->lastLaunch());
}

QList<LogSource> CrashReportCollector::collectFromGameRoot(const QString& gameRoot, qint64 lastLaunchMs)
{
    QList<LogSource> out;

    const QFileInfo latest(QDir(gameRoot).filePath(QStringLiteral("logs/latest.log")));
    if (latest.isFile()) {
        const bool older = lastLaunchMs > 0 && latest.lastModified().toMSecsSinceEpoch() < lastLaunchMs;
        addFile(out, latest, kMaxLatestLogBytes, true, older);
    }

    QFileInfo info;
    bool fresh = false;
    if (newestFile(QDir(gameRoot).filePath(QStringLiteral("crash-reports")), { QStringLiteral("*.txt") }, lastLaunchMs, info, fresh))
        addFile(out, info, kMaxReportBytes, false, !fresh);
    if (newestFile(gameRoot, { QStringLiteral("hs_err_pid*.log") }, lastLaunchMs, info, fresh))
        addFile(out, info, kMaxReportBytes, false, !fresh);

    return out;
}

}  // namespace Zero::Diagnostics
