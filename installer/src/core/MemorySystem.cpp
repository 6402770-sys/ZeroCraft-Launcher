// SPDX-License-Identifier: GPL-3.0-only
#include "MemorySystem.h"

namespace ZeroSetup {

MemorySystem::MemorySystem(QString programsDir, QString desktopDir) : m_programs(std::move(programsDir)), m_desktop(std::move(desktopDir)) {}

QString MemorySystem::norm(const QString& key)
{
    QString k = key;
    k.replace('/', '\\');
    while (k.endsWith('\\'))
        k.chop(1);
    return k.toLower();
}

bool MemorySystem::regSetString(const QString& key, const QString& name, const QString& value)
{
    m_keys[norm(key)][name.toLower()] = "S:" + value;
    m_log << QStringLiteral("REG SET  HKCU\\%1 [%2] = \"%3\"").arg(key, name.isEmpty() ? QStringLiteral("(default)") : name, value);
    return true;
}
bool MemorySystem::regSetDword(const QString& key, const QString& name, quint32 value)
{
    m_keys[norm(key)][name.toLower()] = "D:" + QString::number(value);
    m_log << QStringLiteral("REG SET  HKCU\\%1 [%2] = dword %3").arg(key, name).arg(value);
    return true;
}
bool MemorySystem::regSetNone(const QString& key, const QString& name)
{
    m_keys[norm(key)][name.toLower()] = "N:";
    m_log << QStringLiteral("REG SET  HKCU\\%1 [%2] = (none)").arg(key, name);
    return true;
}
std::optional<QString> MemorySystem::regGetString(const QString& key, const QString& name)
{
    const auto k = m_keys.constFind(norm(key));
    if (k == m_keys.constEnd())
        return std::nullopt;
    const auto v = k->constFind(name.toLower());
    if (v == k->constEnd() || !v->startsWith("S:"))
        return std::nullopt;
    return v->mid(2);
}
bool MemorySystem::regKeyExists(const QString& key)
{
    const QString n = norm(key);
    for (auto it = m_keys.constBegin(); it != m_keys.constEnd(); ++it)
        if (it.key() == n || it.key().startsWith(n + '\\'))
            return true;
    return false;
}
bool MemorySystem::regDeleteValue(const QString& key, const QString& name)
{
    const auto k = m_keys.find(norm(key));
    if (k == m_keys.end() || !k->contains(name.toLower()))
        return true;
    k->remove(name.toLower());
    m_log << QStringLiteral("REG DEL  HKCU\\%1 [%2]").arg(key, name);
    return true;
}
bool MemorySystem::regDeleteKey(const QString& key)
{
    const QString n = norm(key);
    bool any = false;
    for (auto it = m_keys.begin(); it != m_keys.end();) {
        if (it.key() == n || it.key().startsWith(n + '\\')) {
            it = m_keys.erase(it);
            any = true;
        } else
            ++it;
    }
    if (any)
        m_log << QStringLiteral("REG DELKEY HKCU\\%1").arg(key);
    return true;
}
bool MemorySystem::regDeleteKeyIfEmpty(const QString& key)
{
    const QString n = norm(key);
    for (auto it = m_keys.constBegin(); it != m_keys.constEnd(); ++it) {
        if (it.key().startsWith(n + '\\'))
            return false;
        if (it.key() == n && !it->isEmpty())
            return false;
    }
    if (m_keys.remove(n))
        m_log << QStringLiteral("REG DELKEY-IF-EMPTY HKCU\\%1").arg(key);
    return true;
}
bool MemorySystem::createShortcut(const QString& lnkPath, const QString& target, const QString& workDir, const QString& iconPath)
{
    m_shortcuts[lnkPath] = target;
    m_log << QStringLiteral("LNK CREATE %1 -> %2 (start in %3, icon %4)").arg(lnkPath, target, workDir, iconPath);
    return true;
}
bool MemorySystem::removeFile(const QString& path)
{
    m_shortcuts.remove(path);
    m_log << QStringLiteral("FILE DELETE %1").arg(path);
    return true;
}
bool MemorySystem::scheduleDeleteOnReboot(const QString& path)
{
    m_log << QStringLiteral("FILE DELETE-ON-REBOOT %1").arg(path);
    return true;
}
bool MemorySystem::moveToRecycleBin(const QString& path)
{
    if (m_trashFails) {
        m_log << QStringLiteral("TRASH FAILED %1").arg(path);
        return false;
    }
    m_trashed << path;
    m_log << QStringLiteral("TRASH %1 (Recycle Bin)").arg(path);
    return true;  // the fake never touches the disk
}
void MemorySystem::notifyAssocChanged()
{
    m_log << QStringLiteral("SHELL SHChangeNotify(SHCNE_ASSOCCHANGED)");
}
QString MemorySystem::value(const QString& key, const QString& name) const
{
    return m_keys.value(norm(key)).value(name.toLower()).mid(2);
}
bool MemorySystem::hasValue(const QString& key, const QString& name) const
{
    return m_keys.value(norm(key)).contains(name.toLower());
}

}  // namespace ZeroSetup
