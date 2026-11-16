// SPDX-License-Identifier: GPL-3.0-only
#include "ProcessMemory.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

namespace Zero {
namespace Session {

std::optional<qint64> processWorkingSetBytes(qint64 pid)
{
#ifdef Q_OS_WIN
    if (pid <= 0)
        return std::nullopt;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h)
        return std::nullopt;
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    const BOOL ok = GetProcessMemoryInfo(h, &counters, sizeof(counters));
    CloseHandle(h);
    if (!ok)
        return std::nullopt;
    return static_cast<qint64>(counters.WorkingSetSize);
#else
    Q_UNUSED(pid);
    return std::nullopt;
#endif
}

}  // namespace Session
}  // namespace Zero
