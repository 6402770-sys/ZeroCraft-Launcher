// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - recommended Java arguments (see JvmArgsAdvisor.h).
#include "zero/perf/JvmArgsAdvisor.h"

#include <QObject>
#include <QRegularExpression>
#include <QSet>

#include "settings/SettingsObject.h"

namespace Zero::Perf {

namespace {

struct Tier {
    int newSizePercent;
    int maxNewSizePercent;
    int regionSizeMb;
    int reservePercent;
    int ihop;
};
constexpr Tier kSmall{ 30, 40, 8, 20, 15 };
constexpr Tier kBig{ 40, 50, 16, 15, 20 };

struct Candidate {
    QString flag;
    QString why;
    int minMajor = 8;
    int maxMajor = 0;  ///< 0 = no known end
};

QList<Candidate> candidates(const Tier& t)
{
    return {
        { QStringLiteral("-XX:+UseG1GC"), QObject::tr("Uses the G1 garbage collector, which keeps memory clean-ups short."), 8, 0 },
        { QStringLiteral("-XX:+ParallelRefProcEnabled"), QObject::tr("Cleans up references on several threads, so pauses are shorter."), 8, 0 },
        { QStringLiteral("-XX:MaxGCPauseMillis=200"), QObject::tr("Aims for memory clean-ups shorter than a fifth of a second."), 8, 0 },
        { QStringLiteral("-XX:+UnlockExperimentalVMOptions"), QObject::tr("Allows the tuning options below that Java still labels experimental."), 8, 0 },
        { QStringLiteral("-XX:+DisableExplicitGC"), QObject::tr("Ignores mods that force a full memory clean-up, a common cause of stutter."), 8, 0 },
        { QStringLiteral("-XX:G1NewSizePercent=%1").arg(t.newSizePercent),
          QObject::tr("Keeps at least %1% of memory for new objects, which Minecraft creates all the time.").arg(t.newSizePercent), 8, 0 },
        { QStringLiteral("-XX:G1MaxNewSizePercent=%1").arg(t.maxNewSizePercent),
          QObject::tr("Lets new objects use at most %1% of memory.").arg(t.maxNewSizePercent), 8, 0 },
        { QStringLiteral("-XX:G1HeapRegionSize=%1M").arg(t.regionSizeMb),
          QObject::tr("Splits memory into %1 MB blocks that suit the big chunks of data Minecraft keeps.").arg(t.regionSizeMb), 8, 0 },
        { QStringLiteral("-XX:G1ReservePercent=%1").arg(t.reservePercent),
          QObject::tr("Keeps %1% of memory free as a safety margin for sudden spikes.").arg(t.reservePercent), 8, 0 },
        { QStringLiteral("-XX:G1HeapWastePercent=5"), QObject::tr("Cleans up sooner once 5% of memory is wasted."), 8, 0 },
        { QStringLiteral("-XX:G1MixedGCCountTarget=4"), QObject::tr("Spreads old-memory clean-ups over 4 short steps."), 8, 0 },
        { QStringLiteral("-XX:InitiatingHeapOccupancyPercent=%1").arg(t.ihop),
          QObject::tr("Starts the background clean-up early, at %1% full, so memory never runs out suddenly.").arg(t.ihop), 8, 0 },
        { QStringLiteral("-XX:G1MixedGCLiveThresholdPercent=90"), QObject::tr("Skips cleaning blocks that are still almost entirely in use."), 8, 0 },
        // Present on 8 through 25 (started on 17, 21 and 25); not verified beyond, and the JDK keeps reworking G1's refinement settings.
        { QStringLiteral("-XX:G1RSetUpdatingPauseTimePercent=5"), QObject::tr("Limits book-keeping work inside a pause to 5% of its time."), 8,
          kLastVerifiedMajor },
        { QStringLiteral("-XX:SurvivorRatio=32"), QObject::tr("Gives short-lived objects more room before they are moved."), 8, 0 },
        { QStringLiteral("-XX:+PerfDisableSharedMem"), QObject::tr("Stops Java writing a statistics file on every run, which avoids disk hitches."), 8, 0 },
        { QStringLiteral("-XX:MaxTenuringThreshold=1"), QObject::tr("Moves objects out of the young area after one clean-up so it empties fast."), 8, 0 },
    };
}

QList<JvmFlag> flagsFor(const Tier& tier, int major)
{
    QList<JvmFlag> flags;
    for (const Candidate& c : candidates(tier)) {
        if (major < c.minMajor || (c.maxMajor > 0 && major > c.maxMajor)) {
            continue;
        }
        flags.append({ c.flag, c.why });
    }
    return flags;
}

QSet<QString> normalized(const QStringList& tokens)
{
    QSet<QString> set;
    for (const QString& t : tokens) {
        set.insert(t.toLower());
    }
    return set;
}

bool containsAll(const QSet<QString>& have, const QList<JvmFlag>& flags)
{
    for (const JvmFlag& f : flags) {
        if (!have.contains(f.flag.toLower())) {
            return false;
        }
    }
    return true;
}

}  // namespace

QStringList splitJvmArgs(const QString& args)
{
    return args.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
}

QStringList JvmArgsAdvice::args() const
{
    QStringList list;
    for (const JvmFlag& f : flags) {
        list << f.flag;
    }
    return list;
}

QString JvmArgsAdvice::argsString() const
{
    return args().join(QLatin1Char(' '));
}

JvmArgsAdvice adviseJvmArgs(int javaMajor, int maxMemoryMb, int cpuCores, const QString& currentArgs)
{
    JvmArgsAdvice advice;
    advice.current = currentArgs.simplified();
    if (javaMajor < 8 || cpuCores == 1) {
        return advice;  // NotSupported
    }
    const Tier& tier = maxMemoryMb >= kBigHeapMb ? kBig : kSmall;
    advice.flags = flagsFor(tier, javaMajor);

    const QStringList tokens = splitJvmArgs(currentArgs);
    if (tokens.isEmpty()) {
        advice.state = JvmArgsState::NoCustomArgs;
        return advice;
    }
    // Tuned already: either tier counts (the heap may change in the same dialog), in any order and spacing, extras allowed.
    const QSet<QString> have = normalized(tokens);
    if (containsAll(have, flagsFor(kSmall, javaMajor)) || containsAll(have, flagsFor(kBig, javaMajor))) {
        advice.state = JvmArgsState::AlreadyRecommended;
        return advice;
    }
    advice.state = JvmArgsState::HasCustomArgs;
    return advice;
}

JvmArgsPrevious currentJvmArgs(SettingsObject* settings)
{
    JvmArgsPrevious previous;
    previous.overridden = settings->get(QStringLiteral("OverrideJavaArgs")).toBool();
    if (previous.overridden) {
        previous.args = settings->get(QStringLiteral("JvmArgs")).toString();
    }
    return previous;
}

JvmArgsPrevious applyJvmArgs(SettingsObject* settings, const QString& args)
{
    const JvmArgsPrevious previous = currentJvmArgs(settings);
    settings->set(QStringLiteral("OverrideJavaArgs"), true);
    settings->set(QStringLiteral("JvmArgs"), args);
    return previous;
}

void restoreJvmArgs(SettingsObject* settings, const JvmArgsPrevious& previous)
{
    if (previous.overridden) {
        settings->set(QStringLiteral("OverrideJavaArgs"), true);
        settings->set(QStringLiteral("JvmArgs"), previous.args);
    } else {
        settings->set(QStringLiteral("OverrideJavaArgs"), false);
        settings->reset(QStringLiteral("JvmArgs"));  // the instance's own value goes, the launcher's applies again (like the Java page)
    }
}

QString javaArgsTunedText()
{
    return QObject::tr("Java arguments tuned");
}

}  // namespace Zero::Perf
