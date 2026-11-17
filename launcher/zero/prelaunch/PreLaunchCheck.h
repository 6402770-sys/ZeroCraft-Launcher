// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - "Before you play" (docs/zero/ARCHITECTURE.md): the quick look at an instance when the user presses Play. This file is the
// pure part (no widgets, no Application): the items, the checks, and the session logic behind the dialog.
//
// Checks (each gives Items with a severity, Block or Warn):
//   Malware  (Block)  every enabled jar of `<game root>/mods` against the public signature list (zero/security), cached per jar;
//   System   (Block)  the documented traces of a fractureiser infection on the PC (read-only paths, zero/security/SystemIndicators);
//   Mods     (Warn)   Mod Intelligence (zero/mods), Confirmed findings only: duplicate mod ids, mods for another loader / Minecraft
//                     version, missing required dependencies. The fix is the existing ModRepair "disable the jar"; where ZERO is not sure
//                     which file is the culprit (or the dependency is missing) the row says "Open Mods";
//   Packs    (Warn)   the resource-pack rollback marker (zero/content/PackRollback), asked once per marker.
// The global setting `ZeroPreLaunchCheck` switches everything off; the per-instance opt-out switches off Mods and Packs only, never the
// malware look. Nothing here deletes a file: a jar goes to the Recycle Bin or gets the `.disabled` suffix.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

#include "zero/mods/ModHealth.h"
#include "zero/security/JarScanner.h"
#include "zero/security/MalwareSignatures.h"
#include "zero/security/SystemIndicators.h"

namespace Zero {
namespace PreLaunch {

enum class Severity { Block, Warn };
enum class Category { Malware, System, Mods, Packs };

struct Fix {
    enum class Kind {
        RecycleFile,    ///< targets[0] = absolute path of the jar: to the Recycle Bin
        ShowFile,       ///< targets[0] = path: shown in Explorer
        OpenUrl,        ///< targets[0] = the page
        DisableJars,    ///< `actions` (ModRepair): rename to `.jar.disabled`
        RecycleJars,    ///< targets = absolute paths of the jars `DisableJars` would touch: each to the Recycle Bin
        OpenMods,       ///< leaves the check: the instance's Mods page opens and the launch is cancelled
        RestorePacks,   ///< put the resource packs back (PackRollback)
        KeepPacksOff,   ///< play without them; the marker is not asked about again
        Recheck         ///< look again at the category of the row
    };

    Kind kind = Kind::OpenUrl;
    QString label;
    QStringList targets;
    QList<Mods::RepairAction> actions;
    bool danger = false;  ///< drawn as the destructive-looking button (Recycle Bin)
};

struct Item {
    QString id;  ///< stable between runs: "malware:<file>", "system:<trace>", "mods:<kind>:<files>:<related>", "packs:rollback"
    Severity severity = Severity::Warn;
    Category category = Category::Mods;
    QString title;
    QString detail;
    QString iconName;  ///< a Lucide name from the bundle ("shield-check", "triangle-alert", "puzzle", "images")
    QList<Fix> fixes;
    // state
    bool fixed = false;
    QString fixedText;  ///< "Fixed", "Left off"
    QString error;      ///< the last fix failed: user-facing English
};

struct PackOffer {
    bool valid = false;
    QStringList suspects;  ///< the pack files that most likely broke the last launch (still in the folder)
};

/// Everything a run needs; tests fill it with temp folders.
struct Env {
    QString instanceName;
    QString instanceRoot;
    QString gameRoot;

    bool enabled = true;               ///< the global setting ZeroPreLaunchCheck
    bool instanceChecksEnabled = true; ///< false: the per-instance opt-out (Mods and Packs are skipped, malware is not)

    const Security::SignatureList* signatures = nullptr;  ///< null = the compiled-in list
    Security::SystemEnv system = Security::SystemEnv::current();
    std::function<Mods::InstanceContext()> context;       ///< null = readInstanceContext(instanceRoot)

    std::function<PackOffer()> packOffer;                 ///< null = no pack check
    std::function<QString()> restorePacks;                ///< "" = done, else why not
    std::function<void()> dismissPacks;

    QString modsDir() const;
    const Security::SignatureList& list() const;
};

struct RunOptions {
    /// Never opens a jar or parses metadata: `complete` says whether the cache could answer everything.
    bool cachedOnly = false;
    std::function<void(int done, int total)> progress;
};

struct CheckResult {
    QList<Item> items;       ///< Block first, then Warn; by category
    bool complete = true;    ///< false (cachedOnly): something is not in the cache, run again without it
    int jarsScanned = 0;
    int jarsFromCache = 0;
    qint64 elapsedMs = 0;
};

/// Runs every category. Writes `<instance root>/.zero/scan-cache.json` (unless cachedOnly changed nothing).
CheckResult runChecks(const Env& env, const RunOptions& options = {});
/// One category (what a fix re-runs).
QList<Item> runCategory(const Env& env, Category category);

/// A fingerprint of the mods folder and the instance components: (name, size, mtime) of every file directly in `modsDir` plus the context.
/// Exposed for the tests.
QString modsFingerprint(const QString& modsDir, const Mods::InstanceContext& context);

// ---------------------------------------------------------------------------------------------------------------------- the dialog's logic

struct Hooks {
    std::function<bool(const QString& path)> recycle;    ///< null = FS::trash
    std::function<void(const QString& path)> showFile;   ///< null = Explorer
    std::function<void(const QString& url)> openUrl;     ///< null = the browser
    std::function<void(bool skip)> setSkipInstance;      ///< stores the per-instance opt-out; null = nothing is stored
};

enum class Decision { Play, Cancel, OpenMods };

class Session {
   public:
    using Rerun = std::function<QList<Item>(Category)>;

    /// `rerun` null = runCategory(env, category).
    Session(Env env, Hooks hooks, QList<Item> items, Rerun rerun = {});

    const QList<Item>& items() const { return m_items; }
    int openCount() const;       ///< not fixed
    int openBlockCount() const;
    bool hasOpenBlock() const { return openBlockCount() > 0; }
    /// "We found 3 things that may stop the game from working." (counts the open ones, "Nothing left to fix." when none)
    QString summary() const;
    /// The footer button: "Play" when nothing is open, else "Play anyway".
    QString playLabel() const;
    /// The primary button is the Play one only when no Block item is open.
    bool playIsPrimary() const { return !hasOpenBlock(); }

    struct FixResult {
        bool ok = false;
        bool leavesCheck = false;  ///< OpenMods: the dialog closes with Decision::OpenMods
        QString error;
    };
    FixResult applyFix(const QString& itemId, int fixIndex);

    void setOptOut(bool skip) { m_optOut = skip; }
    bool optOut() const { return m_optOut; }

    /// "Play" / "Play anyway". With an open Block item `confirmBlock` is asked ("Play anyway" behind a second confirmation); it must say
    /// yes for the result to be Play (Cancel otherwise, nothing changes). Play stores the opt-out and, for an open Packs row, the
    /// choice "play without them".
    Decision play(const std::function<bool()>& confirmBlock);
    /// Cancel / the dialog was closed: nothing is stored.
    Decision cancel() const { return Decision::Cancel; }

    const Env& env() const { return m_env; }

   private:
    void recheck(Category category);
    Item* find(const QString& id);

    Env m_env;
    Hooks m_hooks;
    QList<Item> m_items;
    Rerun m_rerun;
    bool m_optOut = false;
};

}  // namespace PreLaunch
}  // namespace Zero
