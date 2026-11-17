// SPDX-License-Identifier: GPL-3.0-only
#include "SandboxPlan.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace Zero {
namespace Sandbox {

QString partKey(Part part)
{
    switch (part) {
        case Part::Mods:
            return QStringLiteral("mods");
        case Part::Config:
            return QStringLiteral("config");
        case Part::ResourcePacks:
            return QStringLiteral("resourcepacks");
        case Part::ShaderPacks:
            return QStringLiteral("shaderpacks");
        case Part::GameVersion:
            return QStringLiteral("gameversion");
        case Part::Worlds:
            break;
    }
    return QStringLiteral("worlds");
}

QList<Part> allParts()
{
    return { Part::Mods, Part::Config, Part::ResourcePacks, Part::ShaderPacks, Part::GameVersion, Part::Worlds };
}

QString Layout::savesDir() const
{
    return QDir(gameRoot).filePath(QStringLiteral("saves"));
}

QList<Target> targetsOf(Part part, const Layout& original, const Layout& copy)
{
    auto inGame = [&](const char* name) {
        return Target{ QDir(original.gameRoot).filePath(QLatin1String(name)), QDir(copy.gameRoot).filePath(QLatin1String(name)) };
    };
    auto inInstance = [&](const char* name) {
        return Target{ QDir(original.instanceRoot).filePath(QLatin1String(name)), QDir(copy.instanceRoot).filePath(QLatin1String(name)) };
    };
    switch (part) {
        case Part::Mods:
            return { inGame("mods") };
        case Part::Config:
            return { inGame("config") };
        case Part::ResourcePacks:
            return { inGame("resourcepacks") };
        case Part::ShaderPacks:
            return { inGame("shaderpacks") };
        case Part::GameVersion:
            return { inInstance("mmc-pack.json"), inInstance("patches") };
        case Part::Worlds:
            break;
    }
    return {};
}

namespace {

bool ignored(const QString& name)
{
    return name.startsWith(QLatin1String(".zero-")) || name == QLatin1String("session.lock");
}

bool stop(const CompareOptions& options)
{
    return options.cancelled && options.cancelled();
}

/// A path that exists as itself (a broken link counts).
bool present(const QFileInfo& info)
{
    return info.exists() || info.isSymLink();
}

bool isLink(const QFileInfo& info)
{
    return info.isSymLink() || info.isJunction();
}

bool sameBytes(const QString& a, const QString& b)
{
    QFile fa(a);
    QFile fb(b);
    if (!fa.open(QIODevice::ReadOnly) || !fb.open(QIODevice::ReadOnly))
        return false;
    constexpr qint64 kBlock = 256 * 1024;
    while (true) {
        const QByteArray x = fa.read(kBlock);
        const QByteArray y = fb.read(kBlock);
        if (x != y)
            return false;
        if (x.isEmpty())
            return true;
    }
}

bool filesDiffer(const QFileInfo& a, const QFileInfo& b)
{
    if (a.size() != b.size())
        return true;
    const qint64 delta = qAbs(a.lastModified().toMSecsSinceEpoch() - b.lastModified().toMSecsSinceEpoch());
    if (delta <= kSameTimeMs)
        return false;
    if (a.size() > kMaxReadBytes)
        return true;
    return !sameBytes(a.absoluteFilePath(), b.absoluteFilePath());
}

bool entriesDiffer(const QFileInfo& a, const QFileInfo& b, const CompareOptions& options);

QStringList entryNames(const QString& dir)
{
    QStringList out;
    if (!QFileInfo(dir).isDir())
        return out;
    for (const QString& n : QDir(dir).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!ignored(n))
            out << n;
    }
    return out;
}

/// Two folders (either may be missing = empty) by their entries.
bool foldersDiffer(const QString& a, const QString& b, const CompareOptions& options)
{
    const QStringList na = entryNames(a);
    const QStringList nb = entryNames(b);
    if (na.size() != nb.size())
        return true;
    for (int i = 0; i < na.size(); ++i) {
        if (na.at(i).compare(nb.at(i), Qt::CaseInsensitive) != 0)  // Windows ignores case: the same file under another spelling is the same
            return true;
    }
    for (const QString& n : na) {
        if (stop(options))
            return true;
        if (entriesDiffer(QFileInfo(QDir(a).filePath(n)), QFileInfo(QDir(b).filePath(n)), options))
            return true;
    }
    return false;
}

bool entriesDiffer(const QFileInfo& a, const QFileInfo& b, const CompareOptions& options)
{
    const bool pa = present(a);
    const bool pb = present(b);
    if (!pa && !pb)
        return false;
    if (!pa || !pb) {
        const QFileInfo& other = pa ? a : b;
        if (isLink(other) || !other.isDir())
            return true;  // a file or a link against nothing
        return foldersDiffer(a.absoluteFilePath(), b.absoluteFilePath(), options);  // a folder against nothing: equal only when it is empty
    }
    if (isLink(a) || isLink(b))
        return !(isLink(a) && isLink(b) && a.symLinkTarget() == b.symLinkTarget());
    if (a.isDir() != b.isDir())
        return true;
    if (a.isDir())
        return foldersDiffer(a.absoluteFilePath(), b.absoluteFilePath(), options);
    return filesDiffer(a, b);
}

}  // namespace

bool treesDiffer(const QString& a, const QString& b, const CompareOptions& options)
{
    return entriesDiffer(QFileInfo(a), QFileInfo(b), options);
}

QStringList worldFolders(const QString& savesDir)
{
    QStringList out;
    for (const QString& n : QDir(savesDir).entryList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name)) {
        if (n.startsWith(QLatin1String(".zero-")))
            continue;
        if (QFileInfo(QDir(QDir(savesDir).filePath(n)).filePath(QStringLiteral("level.dat"))).isFile())
            out << n;
    }
    return out;
}

QStringList changedWorlds(const Layout& original, const Layout& copy, const CompareOptions& options)
{
    QStringList out;
    for (const QString& name : worldFolders(copy.savesDir())) {
        if (stop(options))
            return {};
        const QString orig = QDir(original.savesDir()).filePath(name);
        if (!QFileInfo(orig).isDir() || treesDiffer(orig, QDir(copy.savesDir()).filePath(name), options))
            out << name;
    }
    if (stop(options))
        return {};
    return out;
}

QList<PartChange> comparePlan(const Layout& original, const Layout& copy, const CompareOptions& options)
{
    QList<PartChange> out;
    for (const Part part : allParts()) {
        PartChange change;
        change.part = part;
        if (part == Part::Worlds) {
            change.worlds = changedWorlds(original, copy, options);
            change.differs = !change.worlds.isEmpty();
        } else {
            for (const Target& t : targetsOf(part, original, copy)) {
                if (treesDiffer(t.original, t.copy, options)) {
                    change.differs = true;
                    break;
                }
            }
        }
        if (stop(options))
            return {};
        out.append(change);
    }
    return out;
}

QList<Part> differingParts(const QList<PartChange>& plan)
{
    QList<Part> out;
    for (const PartChange& c : plan) {
        if (c.differs)
            out << c.part;
    }
    return out;
}

}  // namespace Sandbox
}  // namespace Zero
