// SPDX-License-Identifier: GPL-3.0-only
#include "SafePath.h"

#include <QDir>
#include <QStringList>

namespace ZeroSetup {

static bool isDeviceName(const QString& segment)
{
    const QString stem = segment.section('.', 0, 0).trimmed().toUpper();
    static const QStringList reserved = { "CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$" };
    if (reserved.contains(stem))
        return true;
    if ((stem.startsWith("COM") || stem.startsWith("LPT")) && stem.size() == 4 && stem[3] >= '0' && stem[3] <= '9')
        return true;
    return false;
}

bool isSafeRelativePath(const QString& rel, QString* why)
{
    auto fail = [&](const char* msg) {
        if (why)
            *why = QString::fromLatin1(msg);
        return false;
    };
    if (rel.isEmpty())
        return fail("empty path");
    if (rel.size() > 400)
        return fail("path too long");
    for (const QChar c : rel) {
        if (c.unicode() < 0x20)
            return fail("control character in path");
        if (c == QLatin1Char('\\'))
            return fail("backslash in path");
        if (c == ':')
            return fail("drive letter or stream in path");
        if (c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            return fail("invalid character in path");
    }
    if (rel.startsWith('/'))
        return fail("absolute path");
    const QStringList parts = rel.split('/');
    for (const QString& p : parts) {
        if (p.isEmpty())
            return fail("empty path segment");
        if (p == "." || p == "..")
            return fail("relative segment in path");
        if (p.endsWith('.') || p.endsWith(' '))
            return fail("segment ends with a dot or space");
        if (isDeviceName(p))
            return fail("reserved device name");
    }
    return true;
}

bool staysInside(const QString& root, const QString& rel)
{
    const QString r = QDir::cleanPath(root);
    const QString full = QDir::cleanPath(r + '/' + rel);
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;  // Windows
    return full.size() > r.size() + 1 && full.startsWith(r + '/', cs);
}

}  // namespace ZeroSetup
