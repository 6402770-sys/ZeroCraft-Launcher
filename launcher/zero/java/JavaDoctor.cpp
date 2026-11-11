// SPDX-License-Identifier: GPL-3.0-only
#include "JavaDoctor.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include "java/JavaVersion.h"

namespace Zero {
namespace Java {

namespace {

constexpr int kMax32BitMemoryMb = 2048;

std::optional<Runtime> findRuntime(const QList<Runtime>& runtimes, const QString& path)
{
    const QString wanted = normalizedPath(path);
    if (wanted.isEmpty())
        return std::nullopt;
    for (const Runtime& r : runtimes)
        if (normalizedPath(r.path) == wanted)
            return r;
    return std::nullopt;
}

bool versionGreater(const Runtime& a, const Runtime& b)
{
    if (a.major != b.major)
        return a.major > b.major;
    return JavaVersion(a.versionString) > JavaVersion(b.versionString);
}

}  // namespace

bool majorFits(const QList<int>& majors, int major)
{
    return majors.isEmpty() || majors.contains(major);
}

QList<int> sortedMajors(const QList<int>& majors)
{
    QList<int> out;
    for (int m : majors)
        if (m > 0 && !out.contains(m))
            out.append(m);
    std::sort(out.begin(), out.end());
    return out;
}

QString normalizedPath(const QString& path)
{
    if (path.trimmed().isEmpty())
        return {};
    QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
#ifdef Q_OS_WIN
    clean = clean.toLower();
#endif
    return clean;
}

std::optional<Runtime> bestRuntimeFor(const QList<int>& majors, const QList<Runtime>& runtimes, bool only64, const QString& skipPath)
{
    if (majors.isEmpty())
        return std::nullopt;
    const QString skip = normalizedPath(skipPath);
    std::optional<Runtime> best;
    for (const Runtime& r : runtimes) {
        if (!majors.contains(r.major) || (only64 && !r.is64bit))
            continue;
        if (!skip.isEmpty() && normalizedPath(r.path) == skip)
            continue;
        if (!best) {
            best = r;
            continue;
        }
        if (r.is64bit != best->is64bit) {
            if (r.is64bit)
                best = r;
            continue;
        }
        if (versionGreater(r, *best))
            best = r;
    }
    return best;
}

namespace {
QJsonObject readJsonObject(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
}  // namespace

Requirements readRequirements(const QString& instanceRoot, const QString& metaDir)
{
    Requirements out;
    const QJsonObject pack = readJsonObject(QDir(instanceRoot).filePath(QStringLiteral("mmc-pack.json")));
    for (const QJsonValue& value : pack.value(QStringLiteral("components")).toArray()) {
        const QJsonObject component = value.toObject();
        const QString uid = component.value(QStringLiteral("uid")).toString();
        const QString version = component.value(QStringLiteral("version")).toString();
        if (uid.isEmpty() || component.value(QStringLiteral("disabled")).toBool())
            continue;
        if (uid == QLatin1String("net.minecraft"))
            out.mcVersion = version;

        QJsonObject file = readJsonObject(QDir(instanceRoot).filePath(QStringLiteral("patches/%1.json").arg(uid)));
        if (file.isEmpty() && !version.isEmpty())
            file = readJsonObject(QDir(metaDir).filePath(QStringLiteral("%1/%2.json").arg(uid, version)));
        // like LaunchProfile::applyCompatibleJavaMajors: every component appends its majors (duplicates are harmless, we skip them)
        for (const QJsonValue& major : file.value(QStringLiteral("compatibleJavaMajors")).toArray()) {
            const int m = major.toInt();
            if (m > 0 && !out.majors.contains(m))
                out.majors.append(m);
        }
        if (const QString name = file.value(QStringLiteral("compatibleJavaName")).toString(); !name.isEmpty())
            out.javaName = name;
    }
    return out;
}

Diagnosis diagnose(const InstanceJava& inst, const QList<Runtime>& runtimes, const GlobalJava& global)
{
    Diagnosis d;
    const QList<int>& majors = inst.compatibleMajors;

    // ---- step 1: AutoInstallJava ----
    const bool autoSkipped = !global.autoSwitch || (inst.overrideLocation && inst.javaPathExists);
    bool fellThroughNoCompatible = false;
    bool picked = false;
    QString pickedPath;
    int pickedMajorGuess = 0;  // used when the picked path is not in `runtimes`

    if (!autoSkipped) {
        // pinned paths win over detected installations and downloads, in the order of the compatible majors
        for (int major : majors) {
            const QString pinned = global.pinned.value(major);
            if (pinned.isEmpty())
                continue;
            if (global.pinnedMissing.contains(major)) {
                if (d.pinnedMissingMajor == 0)
                    d.pinnedMissingMajor = major;
                continue;
            }
            picked = true;
            d.source = Source::AutomaticPinned;
            pickedPath = pinned;
            pickedMajorGuess = major;
            break;
        }
        if (!picked && !global.autoDownload) {
            for (const Runtime& r : runtimes) {
                if (majors.contains(r.major)) {
                    picked = true;
                    d.source = Source::AutomaticDetected;
                    pickedPath = r.path;
                    pickedMajorGuess = r.major;
                    break;
                }
            }
            if (!picked && !majors.isEmpty())
                fellThroughNoCompatible = true;
        } else if (!picked && global.autoDownload && global.downloadSupported && !inst.javaDownloadName.isEmpty() && !majors.isEmpty()) {
            d.source = Source::AutomaticDownload;
            if (!inst.downloadedJavaPath.isEmpty()) {
                picked = true;
                pickedPath = inst.downloadedJavaPath;
                pickedMajorGuess = majors.first();
            } else {
                d.status = Status::Ready;
                d.reason = Reason::WillDownload;
                d.downloadMajor = majors.first();
                return d;
            }
        }
    }

    // ---- the Java that ends up in use ----
    QString path;
    std::optional<Runtime> rt;
    bool pathExists = true;
    if (picked) {
        path = pickedPath;
        rt = findRuntime(runtimes, path);
        if (!rt) {
            Runtime guess;
            guess.path = path;
            guess.major = pickedMajorGuess;
            rt = guess;
        }
    } else {
        d.source = Source::Configured;
        path = inst.javaPathResolved.isEmpty() ? inst.javaPath : inst.javaPathResolved;
        pathExists = !inst.javaPathResolved.isEmpty();
        rt = findRuntime(runtimes, path);
        if (!rt && pathExists) {
            // an installation the detection does not list: all we know is what the last launch stored
            Runtime stored;
            stored.path = path;
            stored.versionString = inst.storedVersion;
            stored.major = inst.storedVersion.isEmpty() ? 0 : JavaVersion(inst.storedVersion).major();
            stored.is64bit = inst.storedArch != QLatin1String("32");
            rt = stored;
        }
    }
    if (rt)
        d.effective = rt;

    // ---- step 2: CheckJava ----
    if (!pathExists) {
        d.effective.reset();
        if (majors.isEmpty() && !autoSkipped) {
            // The requirement is only unknown because the version files are not read yet; at launch they are, and automatic Java may
            // then find a Java: no verdict.
            d.status = Status::Unknown;
            d.reason = Reason::RequirementUnknown;
            return d;
        }
        d.status = Status::WillFail;
        // nothing compatible installed and nothing configured by the user: that is the actual problem
        d.reason = fellThroughNoCompatible && !inst.overrideLocation ? Reason::NoCompatibleInstalledDownloadOff : Reason::ConfiguredPathMissing;
        d.recommendation = bestRuntimeFor(majors, runtimes, false, QString());
        return d;
    }

    // ---- step 3: VerifyJavaInstall ----
    const int major = rt ? rt->major : 0;
    const bool is32 = rt && !rt->is64bit;
    const bool mem32 = is32 && inst.maxMemMb > kMax32BitMemoryMb;
    const QString effectivePath = rt ? rt->path : QString();

    if (majors.isEmpty()) {
        d.status = mem32 ? Status::Warning : Status::Unknown;
        d.reason = mem32 ? Reason::ThirtyTwoBitMemory : Reason::RequirementUnknown;
    } else if (major <= 0) {
        d.status = mem32 ? Status::Warning : Status::Unknown;
        d.reason = mem32 ? Reason::ThirtyTwoBitMemory : Reason::RuntimeUnknown;
    } else if (!majors.contains(major)) {
        if (inst.ignoreCompatibility) {
            d.status = Status::Warning;
            d.reason = Reason::IncompatibleIgnored;
        } else {
            d.status = Status::WillFail;
            d.reason = fellThroughNoCompatible ? Reason::NoCompatibleInstalledDownloadOff : Reason::IncompatibleMajor;
        }
    } else if (mem32) {
        d.status = Status::Warning;
        d.reason = Reason::ThirtyTwoBitMemory;
    } else if (d.pinnedMissingMajor != 0) {
        d.status = Status::Warning;
        d.reason = Reason::PinnedPathMissing;
    } else {
        d.status = Status::Ready;
        d.reason = Reason::Ok;
    }

    if (d.status == Status::Warning || d.status == Status::WillFail) {
        if (d.reason != Reason::PinnedPathMissing)
            d.recommendation = bestRuntimeFor(majors, runtimes, d.reason == Reason::ThirtyTwoBitMemory, effectivePath);
    }
    return d;
}

}  // namespace Java
}  // namespace Zero
