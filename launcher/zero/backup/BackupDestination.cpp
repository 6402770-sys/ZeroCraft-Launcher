// SPDX-License-Identifier: GPL-3.0-only
#include "BackupDestination.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStorageInfo>
#include <QUuid>

namespace Zero {
namespace Backup {

namespace {

QString clean(const QString& path)
{
    return path.trimmed().isEmpty() ? QString() : QDir::cleanPath(path.trimmed());
}

bool samePath(const QString& a, const QString& b)
{
    return a.compare(b, Qt::CaseInsensitive) == 0;
}

Environment filled(Environment e)
{
    const Environment real = systemEnvironment();
    if (!e.env)
        e.env = real.env;
    if (!e.dirExists)
        e.dirExists = real.dirExists;
    if (!e.readFile)
        e.readFile = real.readFile;
    if (!e.driveRoots)
        e.driveRoots = real.driveRoots;
    return e;
}

void addFolder(QList<CloudFolder>& out, const QString& label, const QString& rawPath, const Environment& e)
{
    const QString path = clean(rawPath);
    if (path.isEmpty() || !e.dirExists(path))
        return;
    for (const CloudFolder& known : out) {
        if (samePath(known.path, path))
            return;
    }
    out.append({ label, path });
}

QJsonObject jsonObject(const QByteArray& bytes)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &error);
    return (error.error == QJsonParseError::NoError && doc.isObject()) ? doc.object() : QJsonObject();
}

}  // namespace

Environment systemEnvironment()
{
    Environment e;
    e.env = [](const QString& name) { return qEnvironmentVariable(name.toUtf8().constData()); };
    e.dirExists = [](const QString& dir) { return QFileInfo(dir).isDir(); };
    e.readFile = [](const QString& path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.read(1024 * 1024) : QByteArray();
    };
    e.driveRoots = []() {
        QStringList roots;
        for (const QStorageInfo& volume : QStorageInfo::mountedVolumes()) {
            if (volume.isValid() && volume.isReady())
                roots << volume.rootPath();
        }
        return roots;
    };
    return e;
}

QList<CloudFolder> detectCloudFolders(const Environment& environment)
{
    const Environment e = filled(environment);
    QList<CloudFolder> out;

    // OneDrive
    {
        QString path = e.env(QStringLiteral("OneDrive"));
        if (path.isEmpty() || !e.dirExists(clean(path)))
            path = e.env(QStringLiteral("OneDriveConsumer"));
        addFolder(out, QStringLiteral("OneDrive"), path, e);
    }

    // Google Drive for desktop: a virtual drive whose "My Drive" folder holds the files; older clients used the profile folder
    {
        for (const QString& root : e.driveRoots())
            addFolder(out, QStringLiteral("Google Drive"), root + QStringLiteral("/My Drive"), e);
        const QString profile = e.env(QStringLiteral("USERPROFILE"));
        if (!profile.isEmpty()) {
            addFolder(out, QStringLiteral("Google Drive"), profile + QStringLiteral("/Google Drive/My Drive"), e);
            addFolder(out, QStringLiteral("Google Drive"), profile + QStringLiteral("/My Drive"), e);
        }
    }

    // Dropbox: info.json of the desktop app
    {
        const QStringList bases{ e.env(QStringLiteral("LOCALAPPDATA")), e.env(QStringLiteral("APPDATA")) };
        for (const QString& base : bases) {
            if (base.isEmpty())
                continue;
            const QJsonObject info = jsonObject(e.readFile(clean(base) + QStringLiteral("/Dropbox/info.json")));
            if (info.isEmpty())
                continue;
            const QString personal = info.value(QStringLiteral("personal")).toObject().value(QStringLiteral("path")).toString();
            const QString business = info.value(QStringLiteral("business")).toObject().value(QStringLiteral("path")).toString();
            addFolder(out, QStringLiteral("Dropbox"), personal, e);
            addFolder(out, QStringLiteral("Dropbox (business)"), business, e);
            if (!personal.isEmpty() || !business.isEmpty())
                break;
        }
    }
    return out;
}

QString rootInFolder(const QString& chosenFolder)
{
    const QString folder = clean(chosenFolder);
    return folder.isEmpty() ? QString() : QDir::cleanPath(folder + QLatin1Char('/') + QLatin1String(kBackupsFolderName));
}

QString worldBackupDirIn(const QString& root, const QString& instanceId, const QString& worldFolder)
{
    return QDir(root).filePath(instanceId + QLatin1Char('/') + worldFolder);
}

QString activeRoot(const QString& defaultRoot, const QString& chosenFolder)
{
    const QString root = rootInFolder(chosenFolder);
    return root.isEmpty() ? defaultRoot : root;
}

Probe probeFolder(const QString& chosenFolder)
{
    const QString folder = clean(chosenFolder);
    if (folder.isEmpty())
        return { false, QCoreApplication::translate("Zero::Backup::Destination", "No folder was chosen.") };
    if (!QFileInfo(folder).isDir())
        return { false, QCoreApplication::translate("Zero::Backup::Destination", "The folder does not exist.") };
    const QString root = rootInFolder(folder);
    if (!QDir().mkpath(root))
        return { false, QCoreApplication::translate("Zero::Backup::Destination", "ZERO cannot create its backup folder there.") };
    const QString probe = root + QStringLiteral("/.zero-probe-") + QUuid::createUuid().toString(QUuid::Id128);
    {
        QFile f(probe);
        if (!f.open(QIODevice::WriteOnly) || f.write("zero", 4) != 4) {
            f.close();
            QFile::remove(probe);
            return { false, QCoreApplication::translate("Zero::Backup::Destination", "The folder is not writable.") };
        }
    }
    if (!QFile::remove(probe))
        return { false, QCoreApplication::translate("Zero::Backup::Destination", "The folder is not writable.") };
    return { true, {} };
}

Resolved resolveRoot(const QString& defaultRoot, const QString& chosenFolder)
{
    Resolved r;
    if (clean(chosenFolder).isEmpty()) {
        r.root = defaultRoot;
        return r;
    }
    const Probe probe = probeFolder(chosenFolder);
    if (probe.ok) {
        r.root = rootInFolder(chosenFolder);
        return r;
    }
    r.root = defaultRoot;
    r.fellBack = true;
    r.chosenFolder = clean(chosenFolder);
    r.reason = probe.error;
    return r;
}

QString fallbackMessage(const QString& chosenFolder)
{
    return QCoreApplication::translate("Zero::Backup::Destination", "Saved in the ZERO folder: %1 isn't reachable").arg(QDir::toNativeSeparators(clean(chosenFolder)));
}

QStringList mergeRoots(const QString& defaultRoot, const QStringList& extra, const QString& active)
{
    QStringList out;
    auto add = [&out](const QString& raw) {
        const QString root = clean(raw);
        if (root.isEmpty())
            return;
        for (const QString& known : std::as_const(out)) {
            if (samePath(known, root))
                return;
        }
        out << root;
    };
    add(defaultRoot);
    for (const QString& root : extra)
        add(root);
    add(active);
    return out;
}

KnownRoots::KnownRoots(QString path) : m_path(std::move(path))
{
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = jsonObject(f.readAll());
    for (const QJsonValue& v : root.value(QStringLiteral("roots")).toArray()) {
        const QString r = clean(v.toString());
        if (r.isEmpty())
            continue;
        bool dup = false;
        for (const QString& known : std::as_const(m_roots))
            dup = dup || samePath(known, r);
        if (!dup)
            m_roots << r;
    }
}

bool KnownRoots::add(const QString& rawRoot)
{
    const QString root = clean(rawRoot);
    if (root.isEmpty())
        return false;
    for (const QString& known : std::as_const(m_roots)) {
        if (samePath(known, root))
            return false;
    }
    m_roots << root;
    if (save())
        return true;
    m_roots.removeLast();
    return false;
}

bool KnownRoots::save() const
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("roots"), QJsonArray::fromStringList(m_roots));
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

}  // namespace Backup
}  // namespace Zero
