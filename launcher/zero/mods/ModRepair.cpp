// SPDX-License-Identifier: GPL-3.0-only
#include "ModRepair.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QThread>

#include "zero/mods/ModHealth.h"
#include "zero/mods/Versions.h"

namespace Zero {
namespace Mods {

namespace {

const ModMetadata* findJar(const QList<ModMetadata>& jars, const QString& fileName)
{
    for (const ModMetadata& jar : jars)
        if (jar.fileName == fileName)
            return &jar;
    return nullptr;
}

/// The one version a jar declares for `modId`; empty when the jar has no such entry or its entries disagree (ambiguous).
QString versionOf(const ModMetadata& jar, const QString& modId)
{
    QString version;
    bool found = false;
    for (const ModEntry& e : jar.mods) {
        if (e.id != modId)
            continue;
        if (found && e.version != version)
            return {};
        version = e.version;
        found = true;
    }
    return version;
}

QString displayNameOf(const ModMetadata& jar, const QString& modId)
{
    for (const ModEntry& e : jar.mods)
        if (e.id == modId)
            return e.displayName();
    return modId;
}

/// A plain file name: no separators, no drive / stream marker, no `.`, `..` component.
bool isPlainFileName(const QString& name)
{
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        return false;
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) || name.contains(QLatin1Char(':')) ||
        name.contains(QStringLiteral("..")) || name.contains(QChar(0)))
        return false;
    return name == name.trimmed();
}

RepairResult refuse(const QString& fileName, const QString& error)
{
    RepairResult r;
    r.fileName = fileName;
    r.error = error;
    return r;
}

/// Validates `fileName` as a regular file directly in `modsDir`.
QString checkSource(const QString& modsDir, const QString& fileName)
{
    if (!isPlainFileName(fileName))
        return QCoreApplication::translate("ModRepair", "\"%1\" is not a file name in the mods folder.").arg(fileName);
    if (modsDir.isEmpty() || !QFileInfo(modsDir).isDir())
        return QCoreApplication::translate("ModRepair", "The mods folder does not exist.");
    const QFileInfo info(QDir(modsDir).filePath(fileName));
    if (info.isSymLink())
        return QCoreApplication::translate("ModRepair", "%1 is a link, ZERO does not rename links.").arg(fileName);
    if (!info.exists())
        return QCoreApplication::translate("ModRepair", "%1 no longer exists in the mods folder.").arg(fileName);
    if (!info.isFile())
        return QCoreApplication::translate("ModRepair", "%1 is not a regular file.").arg(fileName);
    return {};
}

/// Never overwrites: refuses when `to` exists, retries a few times when an antivirus / indexer still holds the file.
RepairResult renameFile(const QString& modsDir, const QString& from, const QString& to)
{
    const QDir dir(modsDir);
    const QString toPath = dir.filePath(to);
    if (QFileInfo::exists(toPath) || QFileInfo(toPath).isSymLink())
        return refuse(from, QCoreApplication::translate("ModRepair", "%1 already exists in the mods folder.").arg(to));
    QFile file(dir.filePath(from));
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (file.rename(toPath)) {
            RepairResult r;
            r.ok = true;
            r.fileName = from;
            r.newFileName = to;
            return r;
        }
        if (QFileInfo::exists(toPath))
            break;
        QThread::msleep(150);
    }
    return refuse(from, QCoreApplication::translate("ModRepair", "Could not rename %1: %2").arg(from, file.errorString()));
}

}  // namespace

QList<RepairAction> actionsFor(const Finding& finding, const QList<ModMetadata>& enabledJars)
{
    if (finding.confidence != Confidence::Confirmed)
        return {};

    switch (finding.kind) {
        case FindingKind::WrongLoader:
        case FindingKind::MinecraftVersion:
        case FindingKind::LoaderVersion: {
            if (finding.fileNames.size() != 1)
                return {};
            const QString& name = finding.fileNames.first();
            if (!findJar(enabledJars, name) || !name.endsWith(QLatin1String(".jar"), Qt::CaseInsensitive))
                return {};
            RepairAction a;
            a.fileName = name;
            a.reason = finding.title;
            return { a };
        }
        case FindingKind::DuplicateMod: {
            if (finding.modIds.size() != 1 || finding.fileNames.size() < 2)
                return {};
            const QString& id = finding.modIds.first();
            QStringList versions;
            QList<const ModMetadata*> jars;
            for (const QString& name : finding.fileNames) {
                const ModMetadata* jar = findJar(enabledJars, name);
                if (!jar || !name.endsWith(QLatin1String(".jar"), Qt::CaseInsensitive))
                    return {};
                const QString v = versionOf(*jar, id);
                if (v.isEmpty())
                    return {};
                jars.append(jar);
                versions.append(v);
            }
            // the newest must beat every other copy, and every pair that matters must be comparable
            int newest = -1;
            for (int i = 0; i < jars.size(); ++i) {
                bool beatsAll = true;
                for (int j = 0; j < jars.size(); ++j) {
                    if (i == j)
                        continue;
                    const auto c = compareVersions(versions[i], versions[j]);
                    if (!c)
                        return {};
                    if (*c <= 0)
                        beatsAll = false;
                }
                if (beatsAll)
                    newest = i;
            }
            if (newest < 0)
                return {};  // a tie for the highest version
            const QString name = displayNameOf(*jars[newest], id);
            QList<RepairAction> out;
            for (int i = 0; i < jars.size(); ++i) {
                if (i == newest)
                    continue;
                RepairAction a;
                a.fileName = jars[i]->fileName;
                a.reason = QCoreApplication::translate("ModRepair", "Older copy of %1 (keeping %2)").arg(name, jars[newest]->fileName);
                out.append(a);
            }
            return out;
        }
        case FindingKind::MissingDependency:
        case FindingKind::DependencyVersion:
        case FindingKind::DeclaredIncompatibility:
        case FindingKind::UnreadableMetadata:
            break;
    }
    return {};
}

RepairResult disableJar(const QString& modsDir, const QString& fileName)
{
    if (const QString error = checkSource(modsDir, fileName); !error.isEmpty())
        return refuse(fileName, error);
    if (!fileName.endsWith(QLatin1String(".jar"), Qt::CaseInsensitive))
        return refuse(fileName, QCoreApplication::translate("ModRepair", "%1 is not a .jar file.").arg(fileName));
    return renameFile(modsDir, fileName, fileName + QStringLiteral(".disabled"));
}

RepairResult enableJar(const QString& modsDir, const QString& disabledFileName)
{
    if (const QString error = checkSource(modsDir, disabledFileName); !error.isEmpty())
        return refuse(disabledFileName, error);
    static const QString suffix = QStringLiteral(".disabled");
    if (!disabledFileName.endsWith(suffix, Qt::CaseInsensitive) || disabledFileName.size() <= suffix.size())
        return refuse(disabledFileName, QCoreApplication::translate("ModRepair", "%1 is not a disabled mod.").arg(disabledFileName));
    return renameFile(modsDir, disabledFileName, disabledFileName.left(disabledFileName.size() - suffix.size()));
}

RepairBatchResult applyAll(const QString& modsDir, const QList<RepairAction>& actions)
{
    RepairBatchResult batch;
    for (const RepairAction& action : actions) {
        RepairResult r;
        switch (action.kind) {
            case RepairAction::Kind::DisableJar:
                r = disableJar(modsDir, action.fileName);
                break;
        }
        if (r.ok)
            batch.succeeded.append(r.newFileName);
        else if (batch.firstError.isEmpty())
            batch.firstError = r.error;
        batch.results.append(r);
    }
    return batch;
}

}  // namespace Mods
}  // namespace Zero
