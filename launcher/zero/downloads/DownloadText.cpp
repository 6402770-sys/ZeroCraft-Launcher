// SPDX-License-Identifier: GPL-3.0-only
#include "DownloadText.h"

#include <QCoreApplication>
#include <QDateTime>
#include <algorithm>
#include "ui/zero/ZeroFormat.h"

namespace Zero {
namespace Downloads {

namespace {

}  // namespace

QString kindToString(Kind kind)
{
    switch (kind) {
        case Kind::Mod:
            return QStringLiteral("mod");
        case Kind::ResourcePack:
            return QStringLiteral("resourcepack");
        case Kind::ShaderPack:
            return QStringLiteral("shaderpack");
        case Kind::World:
            return QStringLiteral("world");
        case Kind::Modpack:
            return QStringLiteral("modpack");
        case Kind::Mixed:
            return QStringLiteral("mixed");
        case Kind::Update:
            return QStringLiteral("update");
    }
    return QStringLiteral("mixed");
}

Kind kindFromString(const QString& text)
{
    static const Kind all[] = { Kind::Mod, Kind::ResourcePack, Kind::ShaderPack, Kind::World, Kind::Modpack, Kind::Mixed, Kind::Update };
    for (Kind k : all)
        if (kindToString(k) == text)
            return k;
    return Kind::Mixed;
}

QString stateToString(DownloadResult::State state)
{
    switch (state) {
        case DownloadResult::State::Done:
            return QStringLiteral("done");
        case DownloadResult::State::Failed:
            return QStringLiteral("failed");
        case DownloadResult::State::Cancelled:
            return QStringLiteral("cancelled");
    }
    return QStringLiteral("failed");
}

DownloadResult::State stateFromString(const QString& text)
{
    if (text == QLatin1String("done"))
        return DownloadResult::State::Done;
    if (text == QLatin1String("cancelled"))
        return DownloadResult::State::Cancelled;
    return DownloadResult::State::Failed;
}

QString stateText(DownloadResult::State state)
{
    switch (state) {
        case DownloadResult::State::Done:
            return QCoreApplication::translate("Zero::Downloads", "Done");
        case DownloadResult::State::Failed:
            return QCoreApplication::translate("Zero::Downloads", "Failed");
        case DownloadResult::State::Cancelled:
            return QCoreApplication::translate("Zero::Downloads", "Cancelled");
    }
    return QCoreApplication::translate("Zero::Downloads", "Failed");
}

QString sizeText(qint64 bytes)
{
    QString text = Format::bytes(bytes);
    text.replace(QStringLiteral(".0 "), QStringLiteral(" "));
    return text;
}

QString speedText(double bytesPerSecond)
{
    return QCoreApplication::translate("Zero::Downloads", "%1/s").arg(sizeText(qint64(bytesPerSecond)));
}

QString progressText(qint64 done, qint64 total, double speed)
{
    QString text = total > 0 ? QCoreApplication::translate("Zero::Downloads", "%1 of %2").arg(sizeText(done), sizeText(total)) : sizeText(done);
    if (speed >= 1.0)
        text += QStringLiteral(" · ") + speedText(speed);
    return text;
}

QString activeCaption(const ActiveDownload& job)
{
    QStringList parts;
    if (!job.instanceName.isEmpty())
        parts << job.instanceName;
    if (job.bytesKnown())
        parts << progressText(job.bytesDone, job.bytesTotal, job.speed);
    else if (job.fraction >= 0)
        parts << QCoreApplication::translate("Zero::Downloads", "%1%").arg(qRound(job.fraction * 100));
    else
        parts << QCoreApplication::translate("Zero::Downloads", "Preparing...");
    return parts.join(QStringLiteral(" · "));
}

QString historyCaption(const HistoryEntry& entry, qint64 nowMs)
{
    QStringList parts;
    if (!entry.instanceName.isEmpty())
        parts << entry.instanceName;
    if (entry.bytes >= kMinBytes && entry.state == DownloadResult::State::Done)
        parts << sizeText(entry.bytes);
    if (entry.finishedMs() > 0)
        parts << Format::relativeTime(entry.finishedMs(), QDateTime::fromMSecsSinceEpoch(nowMs));
    return parts.join(QStringLiteral(" · "));
}

double overallFraction(const QList<ActiveDownload>& jobs)
{
    double sum = 0;
    int known = 0;
    double weighted = 0;
    double weights = 0;
    bool allBytes = true;
    for (const ActiveDownload& j : jobs) {
        if (j.fraction < 0)
            continue;
        ++known;
        sum += j.fraction;
        if (j.bytesKnown()) {
            weighted += j.fraction * double(j.bytesTotal);
            weights += double(j.bytesTotal);
        } else {
            allBytes = false;
        }
    }
    if (known == 0)
        return -1;
    const double value = (allBytes && weights > 0) ? weighted / weights : sum / known;
    return std::clamp(value, 0.0, 1.0);
}

QString summaryText(const QList<ActiveDownload>& jobs)
{
    if (jobs.isEmpty())
        return {};
    QString text = jobs.size() == 1 ? QCoreApplication::translate("Zero::Downloads", "Downloading 1 item") : QCoreApplication::translate("Zero::Downloads", "Downloading %1 items").arg(jobs.size());
    const double f = overallFraction(jobs);
    if (f >= 0)
        text += QStringLiteral(" · ") + QCoreApplication::translate("Zero::Downloads", "%1%").arg(qRound(f * 100));
    return text;
}

QString startedToastText(const QString& title)
{
    return QCoreApplication::translate("Zero::Downloads", "Downloading %1…").arg(title);
}

QString failedToastText(const QString& title)
{
    return QCoreApplication::translate("Zero::Downloads", "Couldn't download %1").arg(title);
}

QString warningsToastText()
{
    return QCoreApplication::translate("Zero::Downloads", "Downloaded with warnings");
}

QString downloadsActionText()
{
    return QCoreApplication::translate("Zero::Downloads", "Downloads");
}

QString instanceBusyText()
{
    return QCoreApplication::translate("Zero::Downloads", "Files for this instance are still downloading. Try again when they finish.");
}

QString modTitleText(const QString& name, int dependencyCount)
{
    if (dependencyCount <= 0)
        return name;
    const QString deps = dependencyCount == 1 ? QCoreApplication::translate("Zero::Downloads", "1 dependency") : QCoreApplication::translate("Zero::Downloads", "%1 dependencies").arg(dependencyCount);
    return QCoreApplication::translate("Zero::Downloads", "%1 and %2").arg(name, deps);
}

QString updateTitleText(const QStringList& names)
{
    if (names.size() == 1)
        return names.first();
    return QCoreApplication::translate("Zero::Downloads", "Update %1 mods").arg(names.size());
}

QString contentTitleText(int mods, int resourcePacks, int shaders)
{
    QStringList parts;
    if (mods > 0)
        parts << (mods == 1 ? QCoreApplication::translate("Zero::Downloads", "1 mod") : QCoreApplication::translate("Zero::Downloads", "%1 mods").arg(mods));
    if (resourcePacks > 0)
        parts << (resourcePacks == 1 ? QCoreApplication::translate("Zero::Downloads", "1 resource pack") : QCoreApplication::translate("Zero::Downloads", "%1 resource packs").arg(resourcePacks));
    if (shaders > 0)
        parts << (shaders == 1 ? QCoreApplication::translate("Zero::Downloads", "1 shader") : QCoreApplication::translate("Zero::Downloads", "%1 shaders").arg(shaders));
    if (parts.size() <= 1)
        return parts.join(QString());
    return parts.mid(0, parts.size() - 1).join(QStringLiteral(", ")) + QCoreApplication::translate("Zero::Downloads", " and ") + parts.last();
}

}  // namespace Downloads
}  // namespace Zero
