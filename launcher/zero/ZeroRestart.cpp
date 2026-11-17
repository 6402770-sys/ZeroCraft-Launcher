// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroRestart.h"

#include <QCoreApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace Zero::Restart {

bool startSuccessor(const QString& dataPath)
{
    QProcess process;
    process.setProgram(QCoreApplication::applicationFilePath());
    QStringList arguments;
    if (!dataPath.isEmpty())
        arguments << QStringLiteral("--dir") << dataPath;
    process.setArguments(arguments);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QString::fromLatin1(kWaitPidVariable), QString::number(QCoreApplication::applicationPid()));
    process.setProcessEnvironment(environment);
    process.setWorkingDirectory(QCoreApplication::applicationDirPath());
    if (!process.startDetached()) {
        qWarning() << "ZERO could not start itself again:" << process.errorString();
        return false;
    }
    return true;
}

void waitForPredecessor(int timeoutMs)
{
    const QByteArray value = qgetenv(kWaitPidVariable);
    if (value.isEmpty())
        return;
    qunsetenv(kWaitPidVariable);  // child processes (the game) must not inherit it
#ifdef Q_OS_WIN
    bool ok = false;
    const DWORD pid = value.toULong(&ok);
    if (!ok || pid == 0)
        return;
    HANDLE handle = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!handle)
        return;  // already gone
    WaitForSingleObject(handle, static_cast<DWORD>(timeoutMs));
    CloseHandle(handle);
#else
    Q_UNUSED(timeoutMs)
#endif
}

}  // namespace Zero::Restart
