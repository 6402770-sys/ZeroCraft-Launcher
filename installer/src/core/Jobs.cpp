// SPDX-License-Identifier: GPL-3.0-only
#include "Jobs.h"

#include "Brand.h"
#include "Footprint.h"
#include "MemorySystem.h"
#include "Uninstaller.h"
#include "Upgrade.h"
#include "WinSystem.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>
#include <memory>

namespace ZeroSetup {

namespace {

std::unique_ptr<ISystem> makeSystem(bool noSystem)
{
    if (noSystem)
        return std::make_unique<MemorySystem>();
    return std::make_unique<WinSystem>();
}

void writePlan(const QString& planFile, const QStringList& lines)
{
    if (planFile.isEmpty())
        return;
    QDir().mkpath(QFileInfo(planFile).absolutePath());
    QFile f(planFile);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QTextStream(&f) << lines.join('\n') << '\n';
    }
}

}  // namespace

QString roamingAppDataDir()
{
    // %APPDATA% (QStandardPaths::AppDataLocation would append the app name)
    const QString env = qEnvironmentVariable("APPDATA");
    return env.isEmpty() ? QString() : QDir::fromNativeSeparators(env);
}

QString localAppDataDir()
{
    const QString env = qEnvironmentVariable("LOCALAPPDATA");
    return QDir::fromNativeSeparators(env);
}

QStringList lockedProgramFiles(const QString& installDir)
{
    QStringList locked;
    for (const QString& f : { Brand::appExe(), Brand::appBase() + QStringLiteral("_updater.exe"), Brand::appBase() + QStringLiteral("_filelink.exe") }) {
        const QString p = QDir(installDir).filePath(f);
        if (isFileLocked(p))
            locked << p;
    }
    return locked;
}

JobResult runInstallJob(const InstallJob& job, const std::atomic<bool>* cancel, const JobProgress& progress)
{
    JobResult r;
    auto report = [&](const QString& s, double f) {
        if (progress)
            progress(s, f);
    };
    const QString target = QDir::cleanPath(job.targetDir);

    if (job.demo) {  // the full UI on a fake slow install: nothing is written anywhere
        const QStringList steps = { QStringLiteral("Copying files"), QStringLiteral("Creating shortcuts"), QStringLiteral("Registering ZERO"),
                                    QStringLiteral("Finishing") };
        for (int i = 0; i <= 100; ++i) {
            if (cancel && cancel->load() && i < 90) {
                r.cancelled = true;
                r.error = QStringLiteral("Cancelled.");
                r.stateNote = QStringLiteral("Nothing was changed.");
                return r;
            }
            if (job.demoFail && i == 55) {
                r.error = QStringLiteral("Could not write platforms/qwindows.dll (the disk is full).");
                r.stateNote = QStringLiteral("Nothing was changed.");
                return r;
            }
            report(i < 90 ? steps[0] : i < 94 ? steps[1] : i < 98 ? steps[2] : steps[3], i / 100.0);
            QThread::msleep(60);
        }
        r.ok = true;
        return r;
    }

    std::unique_ptr<ISystem> sys = makeSystem(job.noSystem);
    MemorySystem* mem = job.noSystem ? static_cast<MemorySystem*>(sys.get()) : nullptr;
    const ExistingInstall existing = detectExisting(*sys, target);

    InstallRequest req;
    req.targetDir = target;
    req.version = job.payload->info().version;
    if (job.writeUninstaller) {
        req.uninstallerSource = job.exePath;
        req.uninstallerBytes = job.payload->info().stubSize;
    }
    req.existing = existing;

    report(QStringLiteral("Copying files"), 0.0);
    const InstallResult ir = installFiles(*job.payload, req, cancel, [&](const QString& step, quint64 done, quint64 total) {
        report(step, total ? 0.9 * double(done) / double(total) : 0.0);
    });
    if (ir.status != InstallStatus::Ok) {
        r.cancelled = ir.status == InstallStatus::Cancelled;
        r.error = ir.error;
        r.stateNote = ir.stateNote;
        return r;
    }

    FootprintOptions fo;
    fo.installDir = target;
    fo.version = req.version;
    fo.desktopShortcut = job.desktopShortcut;
    fo.startMenuShortcut = job.startMenuShortcut;
    fo.writeUninstallEntry = job.writeUninstaller;
    fo.shellAssociation = job.shellAssoc;
    fo.estimatedSizeBytes = job.payload->info().totalSize;
    report(QStringLiteral("Creating shortcuts"), 0.90);
    // (shortcuts and registry are applied together; the two step names are what the user sees, in the order of the NSIS script)
    report(QStringLiteral("Registering ZERO"), 0.94);
    applyFootprint(*sys, fo, &r.warnings);
    report(QStringLiteral("Finishing"), 0.98);
    if (mem) {
        r.plan << QStringLiteral("ZERO Setup plan (--no-system): nothing below was done to this computer.")
               << QStringLiteral("INSTALL FILES %1 -> %2 (%3 files, %4 bytes)")
                      .arg(job.payload->info().version, QDir::toNativeSeparators(target)).arg(job.payload->info().files.size()).arg(job.payload->info().totalSize)
               << QStringLiteral("UPGRADE %1").arg(existing.kind == InstallKind::None ? QStringLiteral("no (fresh install)")
                                                   : existing.kind == InstallKind::V2   ? QStringLiteral("over a ZERO Setup install %1").arg(existing.version)
                                                                                         : QStringLiteral("over an NSIS install %1").arg(existing.version));
        r.plan << mem->log();
        writePlan(job.planFile.isEmpty() ? QDir::tempPath() + QStringLiteral("/zero-setup-plan.txt") : job.planFile, r.plan);
    }
    r.ok = true;
    return r;
}

JobResult runUninstallJob(const UninstallJob& job, const std::atomic<bool>* cancel, const JobProgress& progress)
{
    Q_UNUSED(cancel);
    JobResult r;
    auto report = [&](const QString& s, double f) {
        if (progress)
            progress(s, f);
    };
    if (job.demo) {
        for (int i = 0; i <= 100; i += 2) {
            report(i < 85 ? QStringLiteral("Removing files") : i < 95 ? QStringLiteral("Removing shortcuts") : QStringLiteral("Finishing"), i / 100.0);
            QThread::msleep(40);
        }
        r.ok = true;
        return r;
    }
    std::unique_ptr<ISystem> sys = makeSystem(job.noSystem);
    MemorySystem* mem = job.noSystem ? static_cast<MemorySystem*>(sys.get()) : nullptr;
    report(QStringLiteral("Removing shortcuts"), 0.02);
    UninstallRequest ur;
    ur.installDir = job.installDir;
    ur.deleteUserData = job.deleteUserData;  // --no-system uses the fake system: the Recycle Bin move is only planned
    ur.userDataDir = job.userDataDir.isEmpty() ? userDataDirFor(roamingAppDataDir()) : job.userDataDir;
    ur.onProgress = [&](double f) { report(QStringLiteral("Removing files"), f); };
    const UninstallResult ures = uninstallFiles(*sys, ur);
    report(QStringLiteral("Finishing"), 0.98);
    r.ok = ures.ok;
    r.error = ures.error;
    r.warnings = ures.warnings;
    r.stillThere = ures.stillThere;
    if (!ures.ok && r.error.isEmpty())
        r.error = QStringLiteral("Some files could not be removed.");
    if (!ures.ok)
        r.stateNote = ures.userDataFailed ? QStringLiteral("Nothing was removed.") : QStringLiteral("ZERO Launcher is partly removed.");
    if (mem) {
        r.plan << QStringLiteral("ZERO Setup uninstall plan (--no-system): the registry / shortcut lines below were only simulated.")
               << QStringLiteral("REMOVE FILES in %1 (files this install owns); data folder %2")
                      .arg(QDir::toNativeSeparators(job.installDir),
                           job.deleteUserData ? QStringLiteral("MOVED TO THE RECYCLE BIN (%1)").arg(QDir::toNativeSeparators(ur.userDataDir)) : QStringLiteral("kept"));
        r.plan << mem->log();
        writePlan(job.planFile.isEmpty() ? QDir::tempPath() + QStringLiteral("/zero-setup-plan.txt") : job.planFile, r.plan);
    }
    return r;
}

}  // namespace ZeroSetup
