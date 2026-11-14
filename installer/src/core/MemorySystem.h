// SPDX-License-Identifier: GPL-3.0-only
// In-memory ISystem: a fake registry plus a log of everything that was asked of it. Used by the unit tests and by
// `--no-system` (the log becomes the plan file). Nothing here touches the machine.
#pragma once

#include "System.h"

#include <QMap>
#include <QStringList>

namespace ZeroSetup {

class MemorySystem : public ISystem
{
   public:
    explicit MemorySystem(QString programsDir = QStringLiteral("C:/Fake/StartMenu/Programs"),
                          QString desktopDir = QStringLiteral("C:/Fake/Desktop"));

    bool regSetString(const QString& key, const QString& name, const QString& value) override;
    bool regSetDword(const QString& key, const QString& name, quint32 value) override;
    bool regSetNone(const QString& key, const QString& name) override;
    std::optional<QString> regGetString(const QString& key, const QString& name) override;
    bool regKeyExists(const QString& key) override;
    bool regDeleteValue(const QString& key, const QString& name) override;
    bool regDeleteKey(const QString& key) override;
    bool regDeleteKeyIfEmpty(const QString& key) override;
    QString startMenuProgramsDir() override { return m_programs; }
    QString desktopDir() override { return m_desktop; }
    bool createShortcut(const QString& lnkPath, const QString& target, const QString& workDir, const QString& iconPath) override;
    bool removeFile(const QString& path) override;
    bool scheduleDeleteOnReboot(const QString& path) override;
    bool moveToRecycleBin(const QString& path) override;
    void notifyAssocChanged() override;

    /// One line per call that changes something, e.g. "REG SET  HKCU\Software\X [name] = ..." / "LNK CREATE ...".
    const QStringList& log() const { return m_log; }
    QString value(const QString& key, const QString& name) const;  // string/dword text, empty when missing
    bool hasValue(const QString& key, const QString& name) const;
    QStringList trashed() const { return m_trashed; }
    void setTrashFails(bool on) { m_trashFails = on; }  // tests: the Recycle Bin refuses
    QStringList shortcuts() const { return m_shortcuts.keys(); }

   private:
    static QString norm(const QString& key);
    QMap<QString, QMap<QString, QString>> m_keys;  // normalised key -> lower-case name -> "S:text" / "D:123" / "N:"
    QMap<QString, QString> m_shortcuts;
    QStringList m_log;
    QString m_programs, m_desktop;
    QStringList m_trashed;
    bool m_trashFails = false;
};

}  // namespace ZeroSetup
