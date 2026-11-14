// SPDX-License-Identifier: GPL-3.0-only
#include "Footprint.h"

#include "Brand.h"

#include <QDir>

namespace ZeroSetup {

namespace {

const QString kClasses = QStringLiteral("Software\\Classes\\");

QString native(const QString& p)
{
    QString n = QDir::toNativeSeparators(p);
    while (n.size() > 3 && n.endsWith('\\'))
        n.chop(1);
    return n;
}

QString exePath(const QString& dir)
{
    return native(dir) + '\\' + Brand::appExe();
}

QString lnkName()
{
    return Brand::displayName() + QStringLiteral(".lnk");
}

void note(QStringList* w, bool ok, const QString& what)
{
    if (!ok && w)
        w->append(what);
}

QString versionName4(const QString& v)
{
    return v + QStringLiteral(".0");
}

void setupUrlHandler(ISystem& sys, const QString& scheme, const QString& installDir, QStringList* w)
{
    const QString key = kClasses + scheme;
    note(w, sys.regSetString(key, QStringLiteral("URL Protocol"), QString()), "URL handler " + scheme);
    note(w, sys.regSetString(key + QStringLiteral("\\shell\\open\\command"), QString(), urlHandlerCommand(installDir)), "URL handler " + scheme);
}

void removeUrlHandlerIfOurs(ISystem& sys, const QString& scheme, const QString& installDir)
{
    const auto cur = sys.regGetString(kClasses + scheme + QStringLiteral("\\shell\\open\\command"), QString());
    if (cur && cur->compare(urlHandlerCommand(installDir), Qt::CaseInsensitive) == 0)
        sys.regDeleteKey(kClasses + scheme);
}

// ---- ports of the NSIS macros APP_SETUP_Def / APP_ASSOCIATE_Def / APP_TEARDOWN_Def / APP_UNASSOCIATE ----

void appSetupDef(ISystem& sys, const QString& description, const QString& icon, const QString& appId, const QString& appName,
                 const QString& appExe, const QString& cmdText, const QString& command)
{
    sys.regSetString(kClasses + appId, QString(), description);
    sys.regSetString(kClasses + appId + "\\DefaultIcon", QString(), icon);
    sys.regSetString(kClasses + appId + "\\shell", QString(), QStringLiteral("open"));
    sys.regSetString(kClasses + appId + "\\shell\\open", QString(), cmdText);
    sys.regSetString(kClasses + appId + "\\shell\\open\\command", QString(), command);
    sys.regSetString(kClasses + "Applications\\" + appExe + "\\shell\\open\\command", QString(), command);
    sys.regSetString(kClasses + "Applications\\" + appExe, QStringLiteral("FriendlyAppName"), appName);
}

void appAssociate(ISystem& sys, const QString& ext, const QString& appId, const QString& appExe, bool overwrite, bool registerDefault)
{
    if (overwrite) {
        const QString prev = sys.regGetString(kClasses + ext, QString()).value_or(QString());  // back up the previous file class
        sys.regSetString(kClasses + ext, appId + QStringLiteral("_backup"), prev);
        sys.regSetString(kClasses + ext, QString(), appId);
    }
    sys.regSetNone(kClasses + ext + "\\OpenWithList", appExe);
    sys.regSetNone(kClasses + ext + "\\OpenWithProgids", appId);
    if (registerDefault)
        sys.regSetString(kClasses + "Applications\\" + appExe + "\\Capabilities\\FileAssociations", ext, appId);
}

void appUnassociate(ISystem& sys, const QString& ext, const QString& appId, const QString& appExe)
{
    QString r0;
    const QString r1 = sys.regGetString(kClasses + ext, QString()).value_or(QString());
    if (r1 == appId) {
        r0 = sys.regGetString(kClasses + ext, appId + QStringLiteral("_backup")).value_or(QString());
        sys.regSetString(kClasses + ext, QString(), r0);  // restore the backed up class
    } else {
        r0 = r1;
    }
    sys.regDeleteValue(kClasses + ext, appId + QStringLiteral("_backup"));  // our own backup value (NSIS left it behind)
    const bool noErrors = sys.regDeleteKeyIfEmpty(kClasses + appId);
    // NSIS only cleared the default value when it still equalled the app id; an emptied one (nothing was backed up) goes too
    if (noErrors && (r0 == appId || r0.isEmpty())) {
        sys.regDeleteValue(kClasses + ext, QString());
        sys.regDeleteKeyIfEmpty(kClasses + ext);
    }
    sys.regDeleteValue(kClasses + ext, appId + QStringLiteral("_backup"));  // our own backup value (NSIS left it behind)
    sys.regDeleteValue(kClasses + ext + "\\OpenWithList", appExe);
    sys.regDeleteKeyIfEmpty(kClasses + ext + "\\OpenWithList");
    sys.regDeleteValue(kClasses + ext + "\\OpenWithProgids", appId);
    sys.regDeleteKeyIfEmpty(kClasses + ext + "\\OpenWithProgids");
    sys.regDeleteKeyIfEmpty(kClasses + ext);

    // junk left behind by the Windows shell
    sys.regDeleteValue(QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\ApplicationAssociationToasts"), appId + "_" + ext);
    sys.regDeleteValue(QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\ApplicationAssociationToasts"), "Applications\\" + appExe + "_" + ext);
    const QString fe = QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\") + ext;
    sys.regDeleteValue(fe + "\\OpenWithProgids", appId);
    sys.regDeleteKeyIfEmpty(fe + "\\OpenWithProgids");
    sys.regDeleteKeyIfEmpty(fe + "\\OpenWithList");
    sys.regDeleteKeyIfEmpty(fe);
}

}  // namespace

QString defaultInstallDir(const QString& localAppData)
{
    return QDir::cleanPath(localAppData + QStringLiteral("/Programs/") + Brand::commonName());
}

QString urlHandlerCommand(const QString& installDir)
{
    return QStringLiteral("\"%1\" \"%2\"").arg(exePath(installDir), QStringLiteral("%1"));
}

QString regAppKey()
{
    return QStringLiteral("Software\\") + Brand::commonName();
}

QString regUninstallKey()
{
    return QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\") + Brand::commonName();
}

void setupShellAssociation(ISystem& sys, const QString& installDir)
{
    const QString exe = Brand::appExe();
    const QString command = QStringLiteral("\"%1\" -I \"%2\"").arg(exePath(installDir), QStringLiteral("%1"));
    appSetupDef(sys, Brand::displayName(), exePath(installDir) + QStringLiteral(",0"), Brand::appId(), Brand::displayName(), exe,
                Brand::displayName(), command);
    // "Default Programs" registration (APP_SETUP_DEFAULT-less, APP_ASSOCIATE_DEFAULT for .mrpack)
    appAssociate(sys, QStringLiteral(".mrpack"), Brand::appId(), exe, true, true);
    appAssociate(sys, QStringLiteral(".zip"), Brand::appId(), exe, false, false);
    sys.notifyAssocChanged();
}

void teardownShellAssociation(ISystem& sys, const QString& installDir)
{
    const QString exe = Brand::appExe();
    const QString appId = Brand::appId();
    // APP_TEARDOWN_DEFAULT
    const bool shellEmpty = sys.regDeleteKeyIfEmpty(kClasses + appId + "\\shell");
    if (shellEmpty)
        sys.regDeleteKey(kClasses + appId + "\\DefaultIcon");
    sys.regDeleteKey(kClasses + "Applications\\" + exe);
    sys.regDeleteKey(kClasses + appId);
    const QString dir = native(installDir);
    sys.regDeleteValue(QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Search\\JumplistData"), dir + '\\' + exe);
    sys.regDeleteValue(QStringLiteral("Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\Shell\\MuiCache"), dir + '\\' + exe + ".FriendlyAppName");
    sys.regDeleteValue(QStringLiteral("Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\Shell\\MuiCache"), dir + '\\' + exe + ".ApplicationCompany");
    sys.regDeleteValue(QStringLiteral("Software\\Microsoft\\Windows\\ShellNoRoam\\MUICache"), dir + '\\' + exe);
    sys.regDeleteValue(QStringLiteral("Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Compatibility Assistant\\Store"), dir + '\\' + exe);
    sys.regDeleteValue(QStringLiteral("Software\\RegisteredApplications"), Brand::displayName());
    sys.regDeleteKey(kClasses + "Applications\\" + exe + "\\Capabilities");
    sys.regDeleteKeyIfEmpty(kClasses + "Applications\\" + exe);
    // un-associate
    appUnassociate(sys, QStringLiteral(".zip"), appId, exe);
    appUnassociate(sys, QStringLiteral(".mrpack"), appId, exe);
    sys.notifyAssocChanged();
}

bool applyFootprint(ISystem& sys, const FootprintOptions& o, QStringList* warnings)
{
    const int before = warnings ? int(warnings->size()) : 0;
    const QString dir = native(o.installDir);

    // Write the installation path into the registry
    note(warnings, sys.regSetString(regAppKey(), QStringLiteral("InstallDir"), dir), "install location");

    // URL handlers: curseforge://, modrinth:// (shared with other launchers: taken over, like the NSIS installer did) and zerolauncher://
    setupUrlHandler(sys, QStringLiteral("curseforge"), o.installDir, warnings);
    setupUrlHandler(sys, QStringLiteral("modrinth"), o.installDir, warnings);
    setupUrlHandler(sys, Brand::appBase(), o.installDir, warnings);

    // Uninstall entry for Windows (https://learn.microsoft.com/en-us/windows/win32/msi/uninstall-registry-key)
    if (o.writeUninstallEntry) {
        const QString k = regUninstallKey();
        const QString un = dir + '\\' + Brand::uninstallerName();
        const QString v = o.version;
        const QStringList parts = v.split('.');
        auto s = [&](const QString& name, const QString& value) { note(warnings, sys.regSetString(k, name, value), "uninstall entry " + name); };
        s("DisplayName", Brand::displayName());
        s("DisplayIcon", exePath(o.installDir));
        s("UninstallString", QStringLiteral("\"%1\" --uninstall").arg(un));
        s("QuietUninstallString", QStringLiteral("\"%1\" --uninstall /S").arg(un));
        s("InstallLocation", dir);
        s("Publisher", Brand::publisher());
        s("Version", versionName4(v));
        s("DisplayVersion", v);
        s("VersionMajor", parts.value(0, QStringLiteral("0")));
        s("VersionMinor", parts.value(1, QStringLiteral("0")));
        note(warnings, sys.regSetDword(k, QStringLiteral("EstimatedSize"), quint32((o.estimatedSizeBytes + 1023) / 1024)), "uninstall entry size");
        note(warnings, sys.regSetDword(k, QStringLiteral("NoModify"), 1), "uninstall entry");
        note(warnings, sys.regSetDword(k, QStringLiteral("NoRepair"), 1), "uninstall entry");
    }

    if (o.startMenuShortcut)
        note(warnings, sys.createShortcut(sys.startMenuProgramsDir() + '/' + lnkName(), exePath(o.installDir), dir, exePath(o.installDir)), "Start menu shortcut");
    if (o.desktopShortcut)
        note(warnings, sys.createShortcut(sys.desktopDir() + '/' + lnkName(), exePath(o.installDir), dir, exePath(o.installDir)), "desktop shortcut");

    if (o.shellAssociation)
        setupShellAssociation(sys, o.installDir);

    sys.notifyAssocChanged();
    return !warnings || int(warnings->size()) == before;
}

bool removeFootprint(ISystem& sys, const QString& installDir, QStringList* warnings)
{
    const int before = warnings ? int(warnings->size()) : 0;
    note(warnings, sys.regDeleteKey(regUninstallKey()), "uninstall entry");
    note(warnings, sys.regDeleteKey(regAppKey()), "install location key");

    note(warnings, sys.removeFile(sys.startMenuProgramsDir() + '/' + lnkName()), "Start menu shortcut");
    note(warnings, sys.removeFile(sys.desktopDir() + '/' + lnkName()), "desktop shortcut");

    // curseforge:// and modrinth:// are shared with other launchers: only remove them while they still point here
    removeUrlHandlerIfOurs(sys, QStringLiteral("curseforge"), installDir);
    removeUrlHandlerIfOurs(sys, QStringLiteral("modrinth"), installDir);
    sys.regDeleteKey(kClasses + Brand::appBase());

    teardownShellAssociation(sys, installDir);
    return !warnings || int(warnings->size()) == before;
}

}  // namespace ZeroSetup
