// SPDX-License-Identifier: GPL-3.0-only
// ZERO Setup: installer and uninstaller in one exe. With a payload appended (zero-setup-pack) it installs; without one
// (the copy kept in the install folder as uninstall.exe) it uninstalls. See installer/README.md.
#include "CmdLine.h"
#include "Footprint.h"
#include "Installer.h"
#include "Jobs.h"
#include "MemorySystem.h"
#include "Motion.h"
#include "Payload.h"
#include "SetupWindow.h"
#include "Uninstaller.h"
#include "Upgrade.h"
#include "WinSystem.h"
#include "ZeroPixelFont.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QScreen>
#include <QTemporaryDir>
#include <QThread>
#include <memory>

#ifdef ZERO_SETUP_STATIC
#include <QtPlugin>
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

using namespace ZeroSetup;

namespace {

bool isInside(const QString& path, const QString& dir)
{
    const QString p = QDir::cleanPath(path), d = QDir::cleanPath(dir);
    return p.compare(d, Qt::CaseInsensitive) == 0 || p.startsWith(d + '/', Qt::CaseInsensitive);
}

// Removes the temp copy of the uninstaller after it exited (it cannot delete itself while it runs).
void scheduleTempCleanup(const QString& dir)
{
    if (dir.isEmpty() || !dir.contains(QStringLiteral("zero-uninstall-")))
        return;
    // cmd waits ~3 s (this process has exited by then), then removes the folder; no console window, started from %TEMP%
    std::wstring cmdline = QStringLiteral("cmd.exe /c ping -n 4 127.0.0.1 >nul & rmdir /s /q \"%1\"").arg(QDir::toNativeSeparators(dir)).toStdWString();
    const std::wstring cwd = QDir::toNativeSeparators(QDir::tempPath()).toStdWString();
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, cwd.c_str(), &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

// The uninstaller runs from a copy in %TEMP% so it can delete every file of the install folder, itself included.
bool relaunchFromTemp(const CmdLine& cmd, const QString& exePath, const QString& installDir)
{
    auto* tmp = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/zero-uninstall-XXXXXX"));
    if (!tmp->isValid())
        return false;
    tmp->setAutoRemove(false);
    const QString copy = tmp->path() + QStringLiteral("/uninstall.exe");
    if (!QFile::copy(exePath, copy))
        return false;
    QStringList args = { QStringLiteral("--uninstall"), QStringLiteral("--no-relaunch"), QStringLiteral("--target"), installDir,
                         QStringLiteral("--uninstall-from-temp"), tmp->path() };
    if (cmd.silent)
        args << QStringLiteral("/S");
    if (cmd.deleteUserData)
        args << QStringLiteral("--delete-data");
    if (cmd.noSystem)
        args << QStringLiteral("--no-system");
    if (!cmd.planFile.isEmpty())
        args << QStringLiteral("--plan") << cmd.planFile;
    return QProcess::startDetached(copy, args);
}

}  // namespace

int main(int argc, char** argv)
{
    // --scale must be known before the QApplication exists (screenshots at 125 % / 150 % ...)
    for (int i = 1; i + 1 < argc; ++i)
        if (QByteArray(argv[i]) == "--scale")
            qputenv("QT_SCALE_FACTOR", argv[i + 1]);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("ZERO Launcher Setup"));
    app.setStyle(QStringLiteral("Fusion"));
    Zero::registerPixelFont();

    CmdLine cmd = parseCommandLine(app.arguments().mid(1));
    if (!cmd.errors.isEmpty()) {
        // a wrong switch must not start an install by accident
        return 2;
    }
    // Motion is ON by default, whatever the Windows "Show animations" setting says (same policy as the launcher).
    // ZERO_SETUP_REDUCE_MOTION=1 is the only switch that turns it off (used by tests).
    Motion::setEnabled(qEnvironmentVariable("ZERO_SETUP_REDUCE_MOTION") != QStringLiteral("1"));

    const QString exePath = QDir::fromNativeSeparators(QCoreApplication::applicationFilePath());
    const QString exeDir = QFileInfo(exePath).absolutePath();
    const bool screenshot = !cmd.screenshotScreen.isEmpty();

    SetupConfig cfg;
    cfg.cmd = cmd;
    cfg.exePath = exePath;

    // read-only probe of the machine (never the real registry for screenshots and --no-system runs)
    std::unique_ptr<ISystem> probe;
    if (screenshot || cmd.noSystem)
        probe = std::make_unique<MemorySystem>();
    else
        probe = std::make_unique<WinSystem>();
    const QString local = localAppDataDir().isEmpty() ? QDir::tempPath() : localAppDataDir();

    PayloadReader payload;
    QString payloadError;
    const OpenStatus status = payload.open(exePath, &payloadError);

    if (screenshot) {
        Motion::freezeAt(60000);
        cfg.version = QStringLiteral("0.2.0");
        cfg.targetDir = QStringLiteral("C:/Users/player/AppData/Local/Programs/ZEROLauncher");
    } else if (cmd.demo) {
        cfg.version = QStringLiteral("0.2.0");
        cfg.targetDir = cmd.target.isEmpty() ? QDir::tempPath() + QStringLiteral("/zero-setup-demo/ZEROLauncher") : cmd.target;
        cfg.uninstallMode = cmd.uninstall;
    } else {
        bool wantUninstall = cmd.uninstall;
        if (status == OpenStatus::NoPayload && !wantUninstall && detectExisting(*probe, exeDir).kind != InstallKind::None)
            wantUninstall = true;  // double-clicking uninstall.exe
        if (wantUninstall) {
            cfg.uninstallMode = true;
            QString dir = cmd.target;
            if (dir.isEmpty())
                dir = detectExisting(*probe, exeDir).kind != InstallKind::None ? exeDir : proposedInstallDir(*probe, local);
            dir = QDir::cleanPath(QDir::fromNativeSeparators(dir));
            cfg.targetDir = dir;
            cfg.existing = detectExisting(*probe, dir);
            if (cfg.existing.kind == InstallKind::None) {
                cfg.startupError = QStringLiteral("ZERO Launcher is not installed in this folder.");
                cfg.startupErrorState = QStringLiteral("Nothing was removed.");
            } else if (!cmd.inPlace && isInside(exePath, dir)) {
                if (relaunchFromTemp(cmd, exePath, dir))
                    return 0;
                // could not copy to %TEMP%: fall through and run in place (locked files are removed at the next restart)
            }
        } else if (status != OpenStatus::Ok) {
            cfg.startupError = QStringLiteral("This setup file is damaged or incomplete. Download it again.");
            cfg.startupErrorState = QStringLiteral("Nothing was changed.");
            cfg.targetDir = proposedInstallDir(*probe, local);
        } else {
            cfg.payload = &payload;
            cfg.version = payload.info().version;
            cfg.targetDir = cmd.target.isEmpty() ? proposedInstallDir(*probe, local) : QDir::cleanPath(QDir::fromNativeSeparators(cmd.target));
            cfg.existing = detectExisting(*probe, cfg.targetDir);
        }
    }

    // ---------------------------------------------------------------- silent (no window)
    if (cmd.silent && !screenshot && !cmd.demo && cfg.startupError.isEmpty()) {
        const QString dir = cfg.targetDir;
        for (int i = 0; i < 60 && !lockedProgramFiles(dir).isEmpty(); ++i)  // the user may still be closing ZERO
            QThread::sleep(1);
        if (!lockedProgramFiles(dir).isEmpty())
            return 2;
        JobResult r;
        if (cfg.uninstallMode) {
            UninstallJob uj;
            uj.installDir = dir;
            uj.deleteUserData = cmd.deleteUserData;
            uj.noSystem = cmd.noSystem;
            uj.planFile = cmd.planFile;
            r = runUninstallJob(uj, nullptr, {});
            scheduleTempCleanup(cmd.stubDir);
        } else {
            InstallJob ij;
            ij.payload = &payload;
            ij.exePath = exePath;
            ij.targetDir = dir;
            ij.desktopShortcut = ij.startMenuShortcut = !cmd.noShortcuts;
            ij.shellAssoc = cmd.shellAssoc;
            ij.writeUninstaller = !cmd.noUninstaller;
            ij.noSystem = cmd.noSystem;
            ij.planFile = cmd.planFile;
            r = runInstallJob(ij, nullptr, {});
        }
        return r.ok ? 0 : 1;
    }
    if (cmd.silent && cfg.startupError.isEmpty() == false)
        return 1;

    // ---------------------------------------------------------------- window
    SetupWindow window(cfg);
    if (screenshot) {
        if (!window.presentForScreenshot(cmd.screenshotScreen))
            return 2;
        const QImage img = window.renderImage();
        return img.save(cmd.screenshotPath) ? 0 : 1;
    }
    const QRect avail = window.screen() ? window.screen()->availableGeometry() : QRect(0, 0, 1280, 720);
    window.move(avail.center() - QPoint(window.width() / 2, window.height() / 2));
    window.show();
    window.raise();
    window.activateWindow();
    const int rc = app.exec();
    if (cfg.uninstallMode)
        scheduleTempCleanup(cmd.stubDir);
    return rc != 0 ? rc : window.exitCode();
}
