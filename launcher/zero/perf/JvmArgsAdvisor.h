// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - "Boost performance", JAVA ARGUMENTS section (docs/zero/SHELL.md, "Boost performance"), without widgets:
//
//  - `adviseJvmArgs`: the recommended Java arguments for a Java major, the maximum memory the instance will have and the PC's cores, each with a
//    one-line plain-English reason, and what to do with what the instance has now (state),
//  - `applyJvmArgs` / `restoreJvmArgs`: write the per-instance override and put the previous one back exactly (the global `JvmArgs` is never touched).
//
// The set is the well-known tuned G1 one (Aikar's flags), with a second tier from 12 GB of heap. It never contains -Xmx / -Xms (the MEMORY section
// owns the memory) nor AlwaysPreTouch (it slows the start of a client). Every flag has the Java majors it is valid for, so the JVM never prints
// "Unrecognized VM option" / "Ignoring option" warnings and never refuses to start: `kLastVerifiedMajor` is the newest Java the set was run on
// (8 by the flag history of the JDK, 17 / 21 / 25 by actually starting the JVM with the set); newer majors get the set minus the flags with a history
// of removal.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

class SettingsObject;

namespace Zero::Perf {

enum class JvmArgsState {
    AlreadyRecommended,  ///< the instance already has the recommended flags (any order / spacing)
    NoCustomArgs,        ///< empty: recommend them
    HasCustomArgs,       ///< the user has their own arguments: offer to replace them (unticked)
    NotSupported,        ///< Java unknown / too old, or a single-core PC: the section is hidden
};

struct JvmFlag {
    QString flag;  ///< "-XX:+UseG1GC"
    QString why;   ///< one line, plain English
};

struct JvmArgsAdvice {
    JvmArgsState state = JvmArgsState::NotSupported;
    QList<JvmFlag> flags;  ///< empty for NotSupported
    QString current;       ///< the arguments the instance has now, whitespace simplified

    /// The row is shown with something to apply (NoCustomArgs / HasCustomArgs).
    bool offered() const { return state == JvmArgsState::NoCustomArgs || state == JvmArgsState::HasCustomArgs; }
    QStringList args() const;
    /// The recommended arguments joined with single spaces.
    QString argsString() const;
};

/// The newest Java major the set is verified on.
constexpr int kLastVerifiedMajor = 25;
/// From this heap (MB) the roomier tier is used.
constexpr int kBigHeapMb = 12 * 1024;

/// `javaMajor` 0 = unknown (NotSupported); `maxMemoryMb` the heap after the dialog's MEMORY change (0 = unknown: small tier); `cpuCores` 0 = unknown
/// (supported), 1 = NotSupported (a single core is better served by the JVM's own choice).
JvmArgsAdvice adviseJvmArgs(int javaMajor, int maxMemoryMb, int cpuCores, const QString& currentArgs);

/// The arguments of a string split at whitespace.
QStringList splitJvmArgs(const QString& args);

/// What the instance had before: the override flag and, when it was on, the instance's own arguments.
struct JvmArgsPrevious {
    bool overridden = false;
    QString args;
};
/// The state `settings` (an instance's) has now.
JvmArgsPrevious currentJvmArgs(SettingsObject* settings);
/// Writes OverrideJavaArgs = true and JvmArgs = `args` to the instance; returns what it had.
JvmArgsPrevious applyJvmArgs(SettingsObject* settings, const QString& args);
/// Puts `previous` back: override off (and the own value forgotten) or on with the exact previous arguments.
void restoreJvmArgs(SettingsObject* settings, const JvmArgsPrevious& previous);

/// The summary part: "Java arguments tuned".
QString javaArgsTunedText();

}  // namespace Zero::Perf
