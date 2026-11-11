// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroPageHelp.h"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QGroupBox>
#include <QHash>
#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QWidget>

namespace Zero {
namespace PageHelp {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("PageHelp", text);
}

const QHash<QString, Page>& pages()
{
    static const QHash<QString, Page> table = {
        // ----- launcher settings
        { "launcher-settings",
          { tr("Folders, updates, console and how ZERO behaves"),
            tr("General launcher behaviour: where instances, mods and icons are stored, whether ZERO checks for updates, "
               "how instances are created and when the console shows up. Changes are saved when you leave Settings.") } },
        { "appearance-settings",
          { tr("Theme, icons and colours of the classic windows"),
            tr("Look of the launcher. The ZERO themes and animations live on the Appearance page in the side bar; here you can "
               "also pick the icon set and the cat that sits in the background of the classic windows.") } },
        { "minecraft-settings",
          { tr("Game window, console, performance tweaks"),
            tr("Defaults for every instance: the size of the game window, when the console opens, auto-join, performance "
               "tweaks and workarounds for old versions. Each instance can override these in its own Settings page.") } },
        { "java-settings",
          { tr("Which Java runs the game and how much memory it gets"),
            tr("Minecraft needs Java. ZERO can pick and download the right version for you. Here you also choose how much "
               "memory (RAM) the game may use. Tools > Java Doctor shows which Java each instance will use.") } },
        { "accounts",
          { tr("Your Microsoft accounts"),
            tr("Add the Microsoft account you play Minecraft with, switch between accounts and choose the default one. "
               "ZERO never sees your password: you sign in on Microsoft's own page.") } },
        { "apis",
          { tr("Online services ZERO talks to"),
            tr("Addresses and keys of the services used to download game files and mods (metadata server, CurseForge, "
               "Modrinth, paste service). Leave these alone unless you know you need a different server.") } },
        { "external-tools",
          { tr("Text editor, profilers, world tools"),
            tr("Programs ZERO can open for you: the editor used for config files, Java profilers for finding lag, and an "
               "external world editor.") } },
        { "proxy-settings",
          { tr("Connect through a proxy"),
            tr("Only needed on networks that require a proxy (some schools and offices). Normally this stays on "
               "\"None\" or \"System settings\".") } },
        { "language-settings", { tr("Interface language"), tr("Language of the launcher.") } },

        // ----- instance window
        { "console",
          { tr("Live output of the game"),
            tr("What the game prints while it starts and runs. If something goes wrong this is the first place to look; "
               "Upload shares the log with a link, and Tools > Crash Detective explains common errors.") } },
        { "version",
          { tr("Minecraft version, mod loader and libraries"),
            tr("The building blocks of this instance: the Minecraft version, the mod loader (Fabric, Forge, NeoForge, "
               "Quilt) and extra libraries. Change the version or install a loader here.") } },
        { "mods",
          { tr("Add, update, enable or disable mods"),
            tr("Mods change or add to the game. Download them from Modrinth or CurseForge, drop jar files here, update "
               "them, or turn them off without deleting them. Tools > Mod Health checks for missing pieces.") } },
        { "coremods", { tr("Old-style core mods"), tr("Core mods for very old Forge versions. Most players never need this page.") } },
        { "nilmods", { tr("NilLoader mods"), tr("Mods for NilLoader. Most players never need this page.") } },
        { "resourcepacks",
          { tr("Textures, sounds and models"),
            tr("Resource packs change how the game looks and sounds. Add them here, then turn them on in the game's "
               "Options > Resource Packs.") } },
        { "texturepacks", { tr("Texture packs for old versions"), tr("Texture packs for Minecraft before 1.6.") } },
        { "datapacks",
          { tr("Data packs for every new world"),
            tr("Data packs change game rules, recipes and world generation. Packs here are copied into the worlds you "
               "create in this instance.") } },
        { "shaderpacks",
          { tr("Lighting and shadow effects"),
            tr("Shaders add realistic lighting, shadows and water. They need a shader mod such as Iris or OptiFine, and "
               "a good graphics card.") } },
        { "notes",
          { tr("Your own notes about this instance"),
            tr("A place for anything you want to remember: coordinates, server addresses, to-do lists. Only you see it.") } },
        { "worlds",
          { tr("Singleplayer worlds of this instance"),
            tr("Copy, rename, back up or delete the worlds of this instance. The Worlds page in the side bar shows the "
               "worlds of every instance and makes verified backups.") } },
        { "servers",
          { tr("Multiplayer servers saved in this instance"),
            tr("The servers in the game's Multiplayer list. Add or edit them here; the Servers page in the side bar "
               "shows them for every instance with their status.") } },
        { "screenshots",
          { tr("Pictures you took with F2"),
            tr("Screenshots of this instance. The Screenshots page in the side bar shows the pictures of every "
               "instance in one gallery.") } },
        { "settings",
          { tr("Java, memory and game options for this instance only"),
            tr("Override the global settings for just this instance: its own Java, memory, window size or launch "
               "options. Anything you do not override follows Settings.") } },
        { "logs",
          { tr("Older logs and crash reports"),
            tr("Logs of earlier game sessions and crash reports. Useful when the game closed and the live log is gone.") } },
        { "gameoptions", { tr("The game's options.txt"), tr("The options saved by the game itself (video, sound, controls).") } },
        { "managed_pack",
          { tr("Where this modpack came from"),
            tr("The modpack this instance was installed from, and how to update it.") } },
    };
    return table;
}

// What a group of options (or a single option) does, by object name. Tooltips of the widgets themselves win.
const QHash<QString, QString>& groups()
{
    static const QHash<QString, QString> table = {
        // LauncherPage
        { "userInterfaceBox", tr("How lists of instances look and behave, and what ZERO does when the game starts.") },
        { "updateSettingsBox", tr("Whether ZERO looks for a newer version of itself when it starts.") },
        { "foldersBox",
          tr("Where ZERO keeps instances, shared mods, icons and downloads. Moving a folder here does not move existing "
             "files: move them yourself first.") },
        { "additionalInstDirsGroupBox", tr("Extra folders with instances that ZERO should also list.") },
        { "modsBox", tr("How mods and modpacks are downloaded and checked, for example whether dependencies are added too.") },
        { "instanceBox", tr("Defaults for new instances, such as copying your settings into them.") },
        { "groupBox_2", tr("How many lines the console keeps and what happens when it is full.") },
        { "tasksBox", tr("How many downloads run at the same time. Lower it if downloads fail on a slow connection.") },
        // MinecraftSettingsWidget (global Minecraft page and instance Settings)
        { "windowSizeGroupBox", tr("Size of the game window when it opens, or start it maximized.") },
        { "consoleSettingsBox",
          tr("When the console window opens and closes: always, only when the game crashes, or never.") },
        { "globalDataPacksGroupBox", tr("A folder of data packs that is added to every new world.") },
        { "gameTimeGroupBox", tr("Whether play time is counted and shown for instances.") },
        { "instanceAccountGroupBox", tr("Always start this instance with a specific account instead of the default one.") },
        { "serverJoinGroupBox", tr("Join a server or open a world automatically as soon as the game has started.") },
        { "loaderGroup", tr("Which mod loaders count as compatible when downloading mods for this instance.") },
        { "latestMCVersionGroupBox", tr("Keep using the newest Minecraft version for this instance.") },
        { "legacySettingsGroupBox", tr("Fixes for very old Minecraft versions. Leave off unless an old version misbehaves.") },
        { "nativeWorkaroundsGroupBox",
          tr("Use system libraries for sound and graphics instead of the bundled ones. Only for special setups.") },
        { "perfomanceGroupBox",
          tr("Performance helpers such as Feral GameMode or running on the dedicated graphics card. Turn them on if the "
             "game runs on the wrong GPU or stutters.") },
        // JavaSettingsWidget
        { "javaInstallationGroupBox",
          tr("Which Java program starts the game. Automatic is best: ZERO picks (and downloads) a version that fits.") },
        { "pinnedJavaGroupBox", tr("Java versions ZERO downloaded and keeps for your instances.") },
        { "memoryGroupBox",
          tr("How much memory (RAM) Minecraft may use. Vanilla needs about 2 GB, big modpacks 4 to 8 GB. More is not "
             "always faster, and leave some for Windows.") },
        { "javaArgumentsGroupBox", tr("Extra options for Java. Only change these if a guide tells you to.") },
        // APIPage
        { "groupBox_paste", tr("Where logs are uploaded when you press Upload in the console.") },
        { "groupBox_meta", tr("The server that tells ZERO which Minecraft and loader versions exist.") },
        { "groupBox_assets", tr("The server game sounds and textures are downloaded from.") },
        { "groupBox_fmllibs", tr("The server for libraries of very old Forge versions.") },
        { "groupBox_msa", tr("Keys for Microsoft sign-in and mod sites. Only change them if you have your own.") },
        // ExternalToolsPage
        { "editorsBox", tr("The program used to open config files and notes.") },
        { "profilersBox", tr("Tools that show what slows the game down (JProfiler, VisualVM).") },
        { "worldToolsBox", tr("An external program to edit worlds, such as MCEdit.") },
        // ProxyPage
        { "proxyTypeBox", tr("Whether to use a proxy, and which kind. \"None\" works for almost everyone.") },
        { "proxyAddrBox", tr("Address and port of the proxy server.") },
        { "proxyAuthBox", tr("User name and password for the proxy, if it needs them.") },
    };
    return table;
}

QString titleOf(const QWidget* w)
{
    if (const auto* box = qobject_cast<const QGroupBox*>(w))
        return QString(box->title()).remove(QLatin1Char('&'));
    if (const auto* button = qobject_cast<const QAbstractButton*>(w))
        return QString(button->text()).remove(QLatin1Char('&'));
    return {};
}

}  // namespace

Page forPage(const QString& pageId)
{
    return pages().value(pageId);
}

QString plainText(const QString& text)
{
    if (text.isEmpty())
        return {};
    const QString plain = Qt::mightBeRichText(text) ? QTextDocumentFragment::fromHtml(text).toPlainText() : text;
    return plain.simplified();
}

QString forWidget(const QWidget* widget, QString* title)
{
    for (const QWidget* w = widget; w; w = w->parentWidget()) {
        const QString tip = plainText(w->toolTip());
        if (!tip.isEmpty()) {
            if (title)
                *title = titleOf(w);
            return tip;
        }
        const auto it = groups().constFind(w->objectName());
        if (it != groups().constEnd()) {
            if (title)
                *title = titleOf(w);
            return it.value();
        }
        if (w->isWindow())
            break;
    }
    return {};
}

}  // namespace PageHelp
}  // namespace Zero
