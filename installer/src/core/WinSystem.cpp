// SPDX-License-Identifier: GPL-3.0-only
#include "WinSystem.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <shlguid.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace ZeroSetup {

namespace {

std::wstring w(const QString& s)
{
    return s.toStdWString();
}

// Opens (creating when asked) a key below HKCU. Caller closes.
HKEY openKey(const QString& key, REGSAM access, bool create)
{
    HKEY h = nullptr;
    LONG r;
    if (create)
        r = RegCreateKeyExW(HKEY_CURRENT_USER, w(key).c_str(), 0, nullptr, 0, access, nullptr, &h, nullptr);
    else
        r = RegOpenKeyExW(HKEY_CURRENT_USER, w(key).c_str(), 0, access, &h);
    return r == ERROR_SUCCESS ? h : nullptr;
}

QString knownFolder(const KNOWNFOLDERID& id, const QString& fallbackEnv)
{
    PWSTR p = nullptr;
    QString out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p)
        out = QString::fromWCharArray(p);
    if (p)
        CoTaskMemFree(p);
    if (out.isEmpty())
        out = qEnvironmentVariable(fallbackEnv.toUtf8().constData());
    return QDir::fromNativeSeparators(out);
}

}  // namespace

WinSystem::WinSystem()
{
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    m_comInit = SUCCEEDED(hr);
}

WinSystem::~WinSystem()
{
    if (m_comInit)
        CoUninitialize();
}

bool WinSystem::regSetString(const QString& key, const QString& name, const QString& value)
{
    HKEY h = openKey(key, KEY_SET_VALUE, true);
    if (!h)
        return false;
    const std::wstring v = w(value);
    const LONG r = RegSetValueExW(h, w(name).c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()), DWORD((v.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(h);
    return r == ERROR_SUCCESS;
}

bool WinSystem::regSetDword(const QString& key, const QString& name, quint32 value)
{
    HKEY h = openKey(key, KEY_SET_VALUE, true);
    if (!h)
        return false;
    const DWORD v = value;
    const LONG r = RegSetValueExW(h, w(name).c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
    RegCloseKey(h);
    return r == ERROR_SUCCESS;
}

bool WinSystem::regSetNone(const QString& key, const QString& name)
{
    HKEY h = openKey(key, KEY_SET_VALUE, true);
    if (!h)
        return false;
    const LONG r = RegSetValueExW(h, w(name).c_str(), 0, REG_NONE, nullptr, 0);
    RegCloseKey(h);
    return r == ERROR_SUCCESS;
}

std::optional<QString> WinSystem::regGetString(const QString& key, const QString& name)
{
    HKEY h = openKey(key, KEY_QUERY_VALUE, false);
    if (!h)
        return std::nullopt;
    DWORD type = 0, size = 0;
    const std::wstring n = w(name);
    std::optional<QString> out;
    if (RegQueryValueExW(h, n.c_str(), nullptr, &type, nullptr, &size) == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        std::wstring buf(size / sizeof(wchar_t) + 1, L'\0');
        DWORD sz2 = size;
        if (RegQueryValueExW(h, n.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(buf.data()), &sz2) == ERROR_SUCCESS) {
            buf.resize(wcsnlen(buf.c_str(), buf.size()));
            out = QString::fromStdWString(buf);
        }
    }
    RegCloseKey(h);
    return out;
}

bool WinSystem::regKeyExists(const QString& key)
{
    HKEY h = openKey(key, KEY_READ, false);
    if (h)
        RegCloseKey(h);
    return h != nullptr;
}

bool WinSystem::regDeleteValue(const QString& key, const QString& name)
{
    HKEY h = openKey(key, KEY_SET_VALUE, false);
    if (!h)
        return true;  // nothing there
    const LONG r = RegDeleteValueW(h, w(name).c_str());
    RegCloseKey(h);
    return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
}

bool WinSystem::regDeleteKey(const QString& key)
{
    const LONG r = RegDeleteTreeW(HKEY_CURRENT_USER, w(key).c_str());
    return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
}

bool WinSystem::regDeleteKeyIfEmpty(const QString& key)
{
    HKEY h = openKey(key, KEY_QUERY_VALUE, false);
    if (!h)
        return true;
    DWORD subKeys = 0, values = 0;
    const LONG q = RegQueryInfoKeyW(h, nullptr, nullptr, nullptr, &subKeys, nullptr, nullptr, &values, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(h);
    if (q != ERROR_SUCCESS || subKeys != 0 || values != 0)
        return false;
    return RegDeleteKeyW(HKEY_CURRENT_USER, w(key).c_str()) == ERROR_SUCCESS;
}

QString WinSystem::startMenuProgramsDir()
{
    return knownFolder(FOLDERID_Programs, QStringLiteral("APPDATA"));
}

QString WinSystem::desktopDir()
{
    return knownFolder(FOLDERID_Desktop, QStringLiteral("USERPROFILE"));
}

bool WinSystem::createShortcut(const QString& lnkPath, const QString& target, const QString& workDir, const QString& iconPath)
{
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&link))) || !link)
        return false;
    const QString nTarget = QDir::toNativeSeparators(target);
    link->SetPath(w(nTarget).c_str());
    link->SetWorkingDirectory(w(QDir::toNativeSeparators(workDir)).c_str());
    link->SetIconLocation(w(QDir::toNativeSeparators(iconPath)).c_str(), 0);
    IPersistFile* pf = nullptr;
    bool ok = false;
    if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&pf))) && pf) {
        QDir().mkpath(QFileInfo(lnkPath).absolutePath());
        ok = SUCCEEDED(pf->Save(w(QDir::toNativeSeparators(lnkPath)).c_str(), TRUE));
        pf->Release();
    }
    link->Release();
    return ok;
}

bool WinSystem::removeFile(const QString& path)
{
    return !QFile::exists(path) || QFile::remove(path);
}

bool WinSystem::scheduleDeleteOnReboot(const QString& path)
{
    return MoveFileExW(w(QDir::toNativeSeparators(path)).c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT) != 0;
}

bool WinSystem::moveToRecycleBin(const QString& path)
{
    return QFile::moveToTrash(path);  // never falls back to a permanent delete
}

void WinSystem::notifyAssocChanged()
{
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_FLUSH, nullptr, nullptr);
}

}  // namespace ZeroSetup
