// SPDX-License-Identifier: GPL-3.0-only
// The real ISystem: HKCU registry, Start menu / desktop shortcuts. NEVER used by tests or by --no-system runs.
#pragma once

#include "System.h"

namespace ZeroSetup {

class WinSystem : public ISystem
{
   public:
    WinSystem();
    ~WinSystem() override;

    bool regSetString(const QString& key, const QString& name, const QString& value) override;
    bool regSetDword(const QString& key, const QString& name, quint32 value) override;
    bool regSetNone(const QString& key, const QString& name) override;
    std::optional<QString> regGetString(const QString& key, const QString& name) override;
    bool regKeyExists(const QString& key) override;
    bool regDeleteValue(const QString& key, const QString& name) override;
    bool regDeleteKey(const QString& key) override;
    bool regDeleteKeyIfEmpty(const QString& key) override;
    QString startMenuProgramsDir() override;
    QString desktopDir() override;
    bool createShortcut(const QString& lnkPath, const QString& target, const QString& workDir, const QString& iconPath) override;
    bool removeFile(const QString& path) override;
    bool scheduleDeleteOnReboot(const QString& path) override;
    bool moveToRecycleBin(const QString& path) override;
    void notifyAssocChanged() override;

   private:
    bool m_comInit = false;
};

}  // namespace ZeroSetup
