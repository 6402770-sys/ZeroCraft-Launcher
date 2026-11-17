// SPDX-License-Identifier: GPL-3.0-only
#include "SystemIndicators.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QSettings>
#include <QStandardPaths>

namespace Zero {
namespace Security {

namespace {
constexpr char kInfoUrl[] = "https://github.com/fractureiser-investigation/fractureiser";
}

SystemEnv SystemEnv::current()
{
    SystemEnv env;
    env.localAppData = qEnvironmentVariable("LOCALAPPDATA");
    env.appData = qEnvironmentVariable("APPDATA");
    env.userProfile = qEnvironmentVariable("USERPROFILE");
    if (env.userProfile.isEmpty()) {
        env.userProfile = QDir::homePath();
    }
#ifdef Q_OS_WIN
    env.runKeyValues = []() {
        QStringList values;
        QSettings run(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), QSettings::NativeFormat);
        for (const QString& key : run.childKeys()) {
            values.append(run.value(key).toString());
        }
        return values;
    };
#endif
    return env;
}

QList<SystemTrace> findSystemTraces(const SystemEnv& env)
{
    QList<SystemTrace> found;
    const QString info = QString::fromLatin1(kInfoUrl);

    // docs/users.md: a folder `%localappdata%\Microsoft Edge` (with the space; the real ones are `MicrosoftEdge` and `Microsoft\Edge`).
    if (!env.localAppData.isEmpty()) {
        const QFileInfo folder(QDir(env.localAppData).filePath(QStringLiteral("Microsoft Edge")));
        if (folder.isDir()) {
            found.append({ QStringLiteral("microsoft-edge-folder"), QDir::toNativeSeparators(folder.absoluteFilePath()),
                           QCoreApplication::translate("Zero::Security::SystemIndicators", "Fractureiser's second stage lives in this folder. The real Edge folders have no space in their name."),
                           info });
        }
    }
    // docs/tech.md, Stage 3b persistence: `%AppData%\..\LocalLow\Microsoft\Internet Explorer\DOMStore\microsoft-vm-core`.
    if (!env.userProfile.isEmpty()) {
        const QFileInfo file(QDir(env.userProfile).filePath(QStringLiteral("AppData/LocalLow/Microsoft/Internet Explorer/DOMStore/microsoft-vm-core")));
        if (file.exists()) {
            found.append({ QStringLiteral("microsoft-vm-core"), QDir::toNativeSeparators(file.absoluteFilePath()),
                           QCoreApplication::translate("Zero::Security::SystemIndicators", "A file the third stage of fractureiser leaves to start itself with Windows."), info });
        }
    }
    // docs/tech.md, Stage 1/2: the stage 2 file `libWebGL64.jar` added to the Startup folder when the registry route failed.
    if (!env.appData.isEmpty()) {
        const QFileInfo file(QDir(env.appData).filePath(QStringLiteral("Microsoft/Windows/Start Menu/Programs/Startup/libWebGL64.jar")));
        if (file.exists()) {
            found.append({ QStringLiteral("startup-libwebgl64"), QDir::toNativeSeparators(file.absoluteFilePath()),
                           QCoreApplication::translate("Zero::Security::SystemIndicators", "Fractureiser's second stage is in your Startup folder."), info });
        }
    }
    // docs/tech.md, Stage 1: a value in HKCU\...\CurrentVersion\Run that starts the stage 2 file `libWebGL64.jar`.
    if (env.runKeyValues) {
        for (const QString& value : env.runKeyValues()) {
            if (value.contains(QLatin1String("libWebGL64"), Qt::CaseInsensitive)) {
                found.append({ QStringLiteral("run-key-libwebgl64"),
                               QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                               QCoreApplication::translate("Zero::Security::SystemIndicators", "A startup entry runs fractureiser's second stage (libWebGL64.jar)."), info });
                break;
            }
        }
    }
    return found;
}

}  // namespace Security
}  // namespace Zero
