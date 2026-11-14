// SPDX-License-Identifier: GPL-3.0-only
// Everything the installer does outside the install folder goes through this interface, so tests (and --no-system
// runs) use an in-memory fake and never touch the real registry, Start menu or desktop.
#pragma once

#include <QString>
#include <optional>

namespace ZeroSetup {

class ISystem
{
   public:
    virtual ~ISystem() = default;

    // --- registry, always HKEY_CURRENT_USER (the NSIS installer ran per-user, so its ShCtx was HKCU).
    // Key paths use backslashes. The default value of a key is the empty name.
    virtual bool regSetString(const QString& key, const QString& name, const QString& value) = 0;
    virtual bool regSetDword(const QString& key, const QString& name, quint32 value) = 0;
    virtual bool regSetNone(const QString& key, const QString& name) = 0;  // REG_NONE value (Open-With lists)
    virtual std::optional<QString> regGetString(const QString& key, const QString& name) = 0;
    virtual bool regKeyExists(const QString& key) = 0;
    virtual bool regDeleteValue(const QString& key, const QString& name) = 0;
    virtual bool regDeleteKey(const QString& key) = 0;         // recursive
    virtual bool regDeleteKeyIfEmpty(const QString& key) = 0;  // only when it has no values and no subkeys

    // --- shell
    virtual QString startMenuProgramsDir() = 0;
    virtual QString desktopDir() = 0;
    virtual bool createShortcut(const QString& lnkPath, const QString& target, const QString& workDir, const QString& iconPath) = 0;
    virtual bool removeFile(const QString& path) = 0;
    virtual bool scheduleDeleteOnReboot(const QString& path) = 0;
    /// Moves a file or folder to the Recycle Bin. There is no permanent-delete fallback: false means it is still where it was.
    virtual bool moveToRecycleBin(const QString& path) = 0;
    virtual void notifyAssocChanged() = 0;
};

}  // namespace ZeroSetup
