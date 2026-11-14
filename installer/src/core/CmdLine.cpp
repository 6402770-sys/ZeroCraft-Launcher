// SPDX-License-Identifier: GPL-3.0-only
#include "CmdLine.h"

namespace ZeroSetup {

CmdLine parseCommandLine(const QStringList& args)
{
    CmdLine c;
    for (int i = 0; i < args.size(); ++i) {
        const QString a = args[i];
        const QString low = a.toLower();
        auto next = [&](QString* out) {
            if (i + 1 >= args.size()) {
                c.errors << a + QStringLiteral(" needs a value");
                return false;
            }
            *out = args[++i];
            return true;
        };
        if (low == "/s" || low == "--silent" || low == "--quiet") {
            c.silent = true;
        } else if (low.startsWith("/d=")) {
            c.target = a.mid(3);
            // NSIS: /D= must be the last argument and its value may contain spaces (it is never quoted)
            for (int j = i + 1; j < args.size(); ++j)
                c.target += ' ' + args[j];
            if (c.target.size() >= 2 && c.target.startsWith('"') && c.target.endsWith('"'))
                c.target = c.target.mid(1, c.target.size() - 2);
            break;
        } else if (low == "/noshortcuts" || low == "--no-shortcuts") {
            c.noShortcuts = true;
        } else if (low == "/nouninstaller" || low == "--no-uninstaller") {
            c.noUninstaller = true;
        } else if (low == "--shell-assoc" || low == "/shellassoc") {
            c.shellAssoc = true;
        } else if (low == "--uninstall" || low == "/uninstall") {
            c.uninstall = true;
        } else if (low.startsWith("_?=")) {
            c.inPlace = true;
            if (c.target.isEmpty())
                c.target = a.mid(3);
        } else if (low == "--no-relaunch") {
            c.inPlace = true;
        } else if (low == "--no-system") {
            c.noSystem = true;
        } else if (low == "--demo") {
            c.demo = true;
        } else if (low == "--demo-fail") {
            c.demo = c.demoFail = true;
        } else if (low == "--delete-data") {
            c.deleteUserData = true;
        } else if (low == "--no-launch") {
            c.launchAfter = false;
        } else if (low == "--target") {
            next(&c.target);
        } else if (low == "--plan") {
            next(&c.planFile);
        } else if (low == "--scale") {
            QString v;
            if (next(&v)) {
                bool ok = false;
                c.scale = v.toDouble(&ok);
                if (!ok || c.scale < 0.5 || c.scale > 4)
                    c.errors << QStringLiteral("bad --scale value");
            }
        } else if (low == "--screenshot") {
            if (next(&c.screenshotScreen))
                next(&c.screenshotPath);
        } else if (low == "--uninstall-from-temp") {
            next(&c.stubDir);
        } else {
            c.errors << QStringLiteral("unknown argument ") + a;
        }
    }
    return c;
}

}  // namespace ZeroSetup
