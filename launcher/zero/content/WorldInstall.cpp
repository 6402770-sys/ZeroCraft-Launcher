// SPDX-License-Identifier: GPL-3.0-only
#include "WorldInstall.h"

#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>
#include <QSet>

#include "zero/mods/Versions.h"

namespace Zero::Content {

namespace {

bool isReleaseNumber(const QString& version)
{
    static const QRegularExpression release(QStringLiteral("^\\d+\\.\\d+(\\.\\d+)?$"));
    return release.match(version).hasMatch();
}

}  // namespace

QString newestReleaseVersion(const QStringList& versions)
{
    QString newest;
    for (const QString& raw : versions) {
        const QString version = raw.trimmed();
        if (!isReleaseNumber(version)) {
            continue;
        }
        if (newest.isEmpty()) {
            newest = version;
            continue;
        }
        const auto order = Mods::compareVersions(version, newest);
        if (order && *order > 0) {
            newest = version;
        }
    }
    return newest;
}

WorldFit worldFit(const QStringList& fileVersions, const QString& targetVersion, const QString& targetName)
{
    WorldFit fit;
    const QString target = targetVersion.trimmed();
    if (!isReleaseNumber(target)) {
        return fit;
    }
    // A file often lists several versions: the target itself = fits; else the newest one at or below the target (the game upgrades it);
    // only when every listed release is newer than the target, the oldest of them is the warning.
    QString olderBest;
    QString newerLeast;
    for (const QString& raw : fileVersions) {
        const QString version = raw.trimmed();
        if (!isReleaseNumber(version)) {
            continue;
        }
        const auto order = Mods::compareVersions(version, target);
        if (!order) {
            continue;
        }
        if (*order == 0) {
            return fit;
        }
        if (*order < 0) {
            const auto vsBest = olderBest.isEmpty() ? std::optional<int>(1) : Mods::compareVersions(version, olderBest);
            if (vsBest && *vsBest > 0) {
                olderBest = version;
            }
        } else {
            const auto vsLeast = newerLeast.isEmpty() ? std::optional<int>(-1) : Mods::compareVersions(version, newerLeast);
            if (vsLeast && *vsLeast < 0) {
                newerLeast = version;
            }
        }
    }
    if (!olderBest.isEmpty()) {
        fit.tone = WorldFit::Tone::Info;
        fit.text = QObject::tr("Made for %1. Minecraft upgrades it the first time you open it.").arg(olderBest);
    } else if (!newerLeast.isEmpty()) {
        fit.tone = WorldFit::Tone::Warning;
        fit.text = QObject::tr("Made for %1, newer than %2 (%3): the game may not open it.").arg(newerLeast, targetName, target);
    }
    return fit;
}

int defaultWorldFileIndex(const QList<QStringList>& fileVersions, const QString& targetVersion)
{
    if (fileVersions.isEmpty()) {
        return -1;
    }
    for (int i = 0; i < fileVersions.size(); ++i) {
        if (worldFit(fileVersions.at(i), targetVersion, QString()).tone != WorldFit::Tone::Warning) {
            return i;
        }
    }
    return 0;
}

QString madeForText(const QStringList& projectVersions)
{
    const QString newest = newestReleaseVersion(projectVersions);
    return newest.isEmpty() ? QString() : QObject::tr("Made for %1").arg(newest);
}

QStringList worldPills(const ContentItem& item)
{
    QStringList out;
    if (item.pack) {
        for (const QString& category : item.pack->categories) {
            if (out.size() >= 2) {
                break;
            }
            if (!category.trimmed().isEmpty() && !out.contains(category)) {
                out.append(category);
            }
        }
    }
    if (const QString made = madeForText(item.gameVersions); !made.isEmpty()) {
        out.append(made);
    }
    return out;
}

bool worldFileDownloadable(const ModPlatform::IndexedVersion& version)
{
    return !version.downloadUrl.trimmed().isEmpty() && !version.fileName.trimmed().isEmpty();
}

QUrl webUrl(const QString& url)
{
    const QUrl parsed(url.trimmed(), QUrl::StrictMode);
    if (!parsed.isValid() || parsed.host().isEmpty()) {
        return {};
    }
    const QString scheme = parsed.scheme().toLower();
    return scheme == QLatin1String("http") || scheme == QLatin1String("https") ? parsed : QUrl();
}

QString worldOnlyOnWebsiteText()
{
    return QObject::tr("This world can only be downloaded on CurseForge.");
}

QString worldOpenWebsiteText()
{
    return QObject::tr("Open on CurseForge");
}

QString worldPrimaryActionText()
{
    return QObject::tr("Add world");
}

QString worldDownloadDir(const QString& instanceRoot)
{
    return QDir(instanceRoot).filePath(QStringLiteral(".zero/world-download"));
}

QString worldDownloadPath(const QString& instanceRoot, const QString& fileId, const QString& fileName)
{
    QString name = QFileInfo(fileName.trimmed()).fileName();  // no folders, whatever slashes the provider sent
    // Characters a Windows path cannot hold, and control characters (built from code points: no hex escape in the pattern).
    QString bad = QStringLiteral("<>:\"/\\|?*");
    for (int c = 0; c < 32; ++c) {
        bad.append(QChar(c));
    }
    for (QChar& ch : name) {
        if (bad.contains(ch)) {
            ch = QLatin1Char('_');
        }
    }
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String("..")) {
        name = QStringLiteral("world.zip");
    }
    QString id = fileId;
    id.replace(QRegularExpression(QStringLiteral("[^0-9A-Za-z_-]")), QStringLiteral("_"));
    return QDir(worldDownloadDir(instanceRoot)).filePath(id.isEmpty() ? name : id + QLatin1Char('-') + name);
}

bool removeWorldDownload(const QString& path, const QString& instanceRoot)
{
    if (path.isEmpty()) {
        return true;
    }
    if (QFile::exists(path)) {
        QFile::remove(path);
    }
    const bool gone = !QFile::exists(path);
    // The folders are ours (.zero/world-download): drop them when nothing else lives there.
    QDir dir(worldDownloadDir(instanceRoot));
    if (dir.exists() && dir.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
        const QString parent = QDir::cleanPath(dir.absoluteFilePath(QStringLiteral("..")));
        dir.removeRecursively();
        QDir zero(parent);
        if (zero.exists() && zero.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
            zero.removeRecursively();
        }
    }
    return gone;
}

QString newWorldFolder(const QStringList& before, const QStringList& after)
{
    const QSet<QString> known(before.cbegin(), before.cend());
    QString found;
    for (const QString& name : after) {
        if (known.contains(name)) {
            continue;
        }
        if (!found.isEmpty()) {
            return {};
        }
        found = name;
    }
    return found;
}

}  // namespace Zero::Content
