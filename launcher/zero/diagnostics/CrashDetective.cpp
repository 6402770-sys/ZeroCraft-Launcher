// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Crash Detective rules. Table driven: every rule is a function over a shared, pre-split context.
//
// Confidence policy
//   Confirmed  the log states the cause in so many words (an exception / message that names the problem).
//   Likely     the log points at the cause but the final link is an inference (e.g. a mixin config names a mod).
//   Possible   a weak signal; shown so the user has something to try, never as a diagnosis.
// A rule that matches ordinary warnings or INFO lines is a bug: the negative fixtures guard against that.
#include "CrashDetective.h"

#include <QCoreApplication>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <functional>

namespace Zero::Diagnostics {

namespace {

QString T(const char* text)
{
    return QCoreApplication::translate("Zero::Diagnostics::CrashDetective", text);
}

QString lastPathSegment(const QString& path)
{
    static const QRegularExpression sep(QStringLiteral("[\\\\/]"));
    return path.section(sep, -1);
}

// ---------------------------------------------------------------------------------------------
// Context
// ---------------------------------------------------------------------------------------------

struct DepIssue {
    int src = 0;
    int idx = 0;
    QString mod, modId, depName, depId, range, actual;
    bool missing = true;
};

struct Ctx {
    const QList<LogSource>& sources;
    QList<QStringList> lines;
    std::optional<int> exitCode;
    QList<DepIssue> deps;

    Ctx(const QList<LogSource>& s, std::optional<int> code) : sources(s), exitCode(code)
    {
        for (const auto& source : sources) {
            QString t = source.text;
            t.remove(QChar::CarriageReturn);
            lines.append(t.split(QChar::LineFeed));
        }
    }

    Evidence ev(int s, int i) const
    {
        Evidence e;
        e.source = sources[s].name;
        e.line = i + 1;
        e.text = lines[s][i].trimmed().left(300);
        return e;
    }

    bool isHsErr(int s) const
    {
        return sources[s].name.startsWith(QLatin1String("hs_err")) ||
               sources[s].text.contains(QLatin1String("A fatal error has been detected by the Java Runtime Environment"));
    }

    void scan(const std::function<void(int, int, const QString&)>& fn) const
    {
        for (int s = 0; s < lines.size(); ++s)
            for (int i = 0; i < lines[s].size(); ++i)
                fn(s, i, lines[s][i]);
    }
};

bool isNoiseLevel(const QString& line)
{
    return line.contains(QLatin1String("/INFO]")) || line.contains(QLatin1String("/DEBUG]")) || line.contains(QLatin1String("/TRACE]")) ||
           line.contains(QLatin1String(" INFO ")) || line.contains(QLatin1String(" DEBUG "));
}

Finding mk(const char* id, Severity sev, Confidence conf, const QString& title, const QString& explanation)
{
    Finding f;
    f.ruleId = QString::fromLatin1(id);
    f.severity = sev;
    f.confidence = conf;
    f.title = title;
    f.explanation = explanation;
    return f;
}

void addEvidence(Finding& f, const Evidence& e)
{
    if (f.evidence.size() >= 5)
        return;
    for (const auto& x : f.evidence)
        if (x.source == e.source && x.line == e.line)
            return;
    f.evidence.append(e);
}

QString joinNames(const QStringList& names, int maxShown = 3)
{
    if (names.isEmpty())
        return {};
    if (names.size() == 1)
        return names.first();
    if (names.size() <= maxShown)
        return names.mid(0, names.size() - 1).join(QStringLiteral(", ")) + T(" and ") + names.last();
    return names.mid(0, maxShown).join(QStringLiteral(", ")) + T(" and %1 more").arg(names.size() - maxShown);
}

QString label(const QString& name, const QString& id)
{
    return name.trimmed().isEmpty() ? id : name.trimmed();
}

/// Forge/Maven style ranges ("[1.2,)") in plain words. Anything unrecognised is returned unchanged.
QString friendlyRange(const QString& raw)
{
    const QString r = raw.trimmed();
    static const QRegularExpression interval(R"(^([\[(])\s*([^,\]\)]*)\s*(?:,\s*([^,\]\)]*)\s*)?([\])])$)");
    const auto m = interval.match(r);
    if (!m.hasMatch())
        return r == QLatin1String("*") ? T("any version") : r;
    const QString lo = m.captured(2), hi = m.captured(3);
    const bool single = !r.contains(QLatin1Char(','));
    if (single && !lo.isEmpty())
        return T("exactly %1").arg(lo);
    if (!lo.isEmpty() && hi.isEmpty())
        return m.captured(1) == QLatin1String("[") ? T("%1 or newer").arg(lo) : T("newer than %1").arg(lo);
    if (lo.isEmpty() && !hi.isEmpty())
        return m.captured(4) == QLatin1String("]") ? T("%1 or older").arg(hi) : T("older than %1").arg(hi);
    if (!lo.isEmpty() && !hi.isEmpty())
        return lo == hi ? T("exactly %1").arg(lo) : T("%1 to %2").arg(lo, hi);
    return r;
}

bool isLoaderId(const QString& id)
{
    static const QSet<QString> ids = { QStringLiteral("fabricloader"), QStringLiteral("quilt_loader"), QStringLiteral("forge"),
                                       QStringLiteral("neoforge"),     QStringLiteral("fml"),          QStringLiteral("fabric-loader") };
    return ids.contains(id.toLower());
}

/// Java version text ("17", "1.8.0_301", "21.0.1", "17 or later") -> major version (8, 17, 21). 0 if none.
int javaMajor(const QString& v)
{
    static const QRegularExpression re(R"((\d+)(?:\.(\d+))?)");
    const auto m = re.match(v);
    if (!m.hasMatch())
        return 0;
    const int a = m.captured(1).toInt();
    if (a == 1 && !m.captured(2).isEmpty())
        return m.captured(2).toInt();
    return a;
}

// ---------------------------------------------------------------------------------------------
// Dependency parsing (Fabric / Quilt / Forge / NeoForge), shared by several rules
// ---------------------------------------------------------------------------------------------

void parseDeps(Ctx& c)
{
    // Fabric / Quilt:  Mod 'Sodium Extra' (sodium-extra) 0.5.0 requires version 0.5.0 or later of mod 'Sodium' (sodium), which is missing!
    static const QRegularExpression fabricMissing(
        R"re(Mod '?(?<mod>[^'(]+?)'? \((?<modId>[^)]+)\)(?: (?<modVer>\S+))? requires (?:any version|version (?<range>.+?)) of (?:mod )?'?(?<dep>[^'(]+?)'? \((?<depId>[^)]+)\),? which is missing)re");
    // ... requires version 1.20.1 of Minecraft (minecraft), but only the wrong version is present: 1.20.4!
    static const QRegularExpression fabricWrong(
        R"re(Mod '?(?<mod>[^'(]+?)'? \((?<modId>[^)]+)\)(?: (?<modVer>\S+))? requires version (?<range>.+?) of (?:mod )?'?(?<dep>[^'(]+?)'? \((?<depId>[^)]+)\),? but only the wrong version is present: (?<actual>[^!\s]+))re");
    // Fabric Loader 0.8 - 0.11:  Could not find required mod: bclib requires {fabric @ [0.30.0+]}
    static const QRegularExpression fabricOld(R"(Could not find required mod: (?<mod>\S+) requires \{(?<dep>\S+) @ \[?(?<range>[^\]}]*)\]?\})");
    // Forge / NeoForge:  Mod ID: 'jei', Requested by: 'mymod', Expected range: '[15.0.0,)', Actual version: '[MISSING]'
    static const QRegularExpression forgeDep(
        R"(Mod ID: '(?<dep>[^']+)', Requested by: '(?<mod>[^']+)', Expected range: '(?<range>[^']*)', Actual version: '(?<actual>[^']*)')");
    // Forge 1.12 - 1.16:  Mod mymod requires jei 15.0.0 or above  /  Currently, jei is not installed
    static const QRegularExpression forgeOld(R"(Mod (?<mod>\S+) requires (?<dep>\S+) (?<range>\S+) or above)");

    for (int s = 0; s < c.lines.size(); ++s) {
        const auto& ls = c.lines[s];
        for (int i = 0; i < ls.size(); ++i) {
            const QString& line = ls[i];
            if (!line.contains(QLatin1String("requires")) && !line.contains(QLatin1String("Mod ID:")) &&
                !line.contains(QLatin1String("Could not find required mod")))
                continue;
            DepIssue d;
            d.src = s;
            d.idx = i;
            if (auto m = fabricMissing.match(line); m.hasMatch()) {
                d.mod = m.captured("mod");
                d.modId = m.captured("modId");
                d.depName = m.captured("dep");
                d.depId = m.captured("depId");
                d.range = m.captured("range");
                d.missing = true;
            } else if (auto m2 = fabricWrong.match(line); m2.hasMatch()) {
                d.mod = m2.captured("mod");
                d.modId = m2.captured("modId");
                d.depName = m2.captured("dep");
                d.depId = m2.captured("depId");
                d.range = m2.captured("range");
                d.actual = m2.captured("actual");
                d.missing = false;
            } else if (auto m3 = fabricOld.match(line); m3.hasMatch()) {
                d.mod = d.modId = m3.captured("mod");
                d.depName = d.depId = m3.captured("dep");
                d.range = m3.captured("range");
                d.missing = true;
            } else if (auto m4 = forgeDep.match(line); m4.hasMatch()) {
                d.mod = d.modId = m4.captured("mod");
                d.depName = d.depId = m4.captured("dep");
                d.range = friendlyRange(m4.captured("range"));
                d.actual = m4.captured("actual");
                d.missing = d.actual.contains(QLatin1String("MISSING"));
                if (d.missing)
                    d.actual.clear();
            } else if (auto m5 = forgeOld.match(line); m5.hasMatch() && i + 1 < ls.size() && ls[i + 1].contains(QLatin1String("Currently,"))) {
                d.mod = d.modId = m5.captured("mod");
                d.depName = d.depId = m5.captured("dep");
                d.range = m5.captured("range") + T(" or newer");
                d.missing = ls[i + 1].contains(QLatin1String("is not installed"));
                if (!d.missing) {
                    static const QRegularExpression cur(R"(is version (\S+))");
                    d.actual = cur.match(ls[i + 1]).captured(1);
                }
            } else {
                continue;
            }
            c.deps.append(d);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------------------------

void ruleJavaVersion(const Ctx& c, QList<Finding>& out)
{
    static const QRegularExpression classVer(
        R"(class file version (\d+)\.\d+\), this version of the Java Runtime only recognizes class file versions up to (\d+)\.\d+)");
    static const QRegularExpression oldClassVer(R"(Unsupported major\.minor version (\d+)\.\d+)");
    static const QRegularExpression generic(
        R"((?:\b(?:requires?|needs?)\s+(?:at least\s+)?Java\s+(?:version\s+)?(\d+)\b|\bJava\s+(\d+)\s+(?:or\s+(?:newer|later|higher|above)|is\s+required)))");

    int needed = 0, current = 0;
    Finding f = mk("wrong-java", Severity::Critical, Confidence::Confirmed, {}, {});
    c.scan([&](int s, int i, const QString& line) {
        if (auto m = classVer.match(line); m.hasMatch()) {
            needed = std::max(needed, m.captured(1).toInt() - 44);
            current = m.captured(2).toInt() - 44;
            addEvidence(f, c.ev(s, i));
        } else if (auto m2 = oldClassVer.match(line); m2.hasMatch() && line.contains(QLatin1String("UnsupportedClassVersionError"))) {
            needed = std::max(needed, m2.captured(1).toInt() - 44);
            addEvidence(f, c.ev(s, i));
        } else if (!isNoiseLevel(line) && (line.contains(QLatin1String("Exception")) || line.contains(QLatin1String("Error")) ||
                                           line.contains(QLatin1String("ERROR")) || line.contains(QLatin1String("FATAL")) ||
                                           line.contains(QLatin1String("Caused by")))) {
            if (auto m3 = generic.match(line); m3.hasMatch()) {
                needed = std::max(needed, std::max(m3.captured(1).toInt(), m3.captured(2).toInt()));
                addEvidence(f, c.ev(s, i));
            }
        }
    });
    for (const auto& d : c.deps) {
        if (d.depId != QLatin1String("java"))
            continue;
        needed = std::max(needed, javaMajor(d.range));
        if (!d.actual.isEmpty())
            current = javaMajor(d.actual);
        addEvidence(f, c.ev(d.src, d.idx));
    }
    if (needed < 5 || f.evidence.isEmpty())
        return;

    f.title = T("This game needs Java %1 or newer").arg(needed);
    f.explanation = current > 0 ? T("The game or one of its mods was built for Java %1, but it is being run with Java %2.").arg(needed).arg(current)
                                : T("The game or one of its mods was built for Java %1, but an older Java is being used to run it.").arg(needed);
    f.suggestions = { T("Open the instance settings, go to the Java section and pick a Java %1 (or newer) installation.").arg(needed),
                      T("If none is listed, use the auto-detect or download option there, or install Java %1 and detect again.").arg(needed) };
    f.actions = { Action::OpenJavaDoctor, Action::OpenInstanceSettings };
    out.append(f);
}

void ruleOutOfMemory(const Ctx& c, QList<Finding>& out)
{
    struct Pattern {
        const char* key;
        QRegularExpression re;
    };
    static const QList<Pattern> patterns = {
        { "heap", QRegularExpression(R"(OutOfMemoryError: Java heap space)") },
        { "gc", QRegularExpression(R"(OutOfMemoryError: GC overhead limit exceeded)") },
        { "meta", QRegularExpression(R"(OutOfMemoryError: (?:Metaspace|Compressed class space))") },
        { "thread", QRegularExpression(R"(OutOfMemoryError: unable to create (?:new )?native thread)") },
        { "reserve",
          QRegularExpression(R"(Could not reserve enough space for (?:\d+KB )?object heap|Invalid maximum heap size|Invalid initial heap size)") },
        { "native",
          QRegularExpression(R"(There is insufficient memory for the Java Runtime Environment to continue|Native memory allocation \(\w+\) failed)") },
    };
    QHash<QString, Finding> found;
    c.scan([&](int s, int i, const QString& line) {
        for (const auto& p : patterns) {
            if (!p.re.match(line).hasMatch())
                continue;
            const QString key = QString::fromLatin1(p.key);
            auto it = found.find(key);
            if (it == found.end()) {
                Finding f = mk("out-of-memory", Severity::Critical, Confidence::Confirmed, {}, {});
                f.actions = { Action::OpenInstanceSettings };
                it = found.insert(key, f);
            }
            addEvidence(it.value(), c.ev(s, i));
        }
    });

    auto report = [&](const char* key, const QString& title, const QString& expl, const QStringList& tips) {
        auto it = found.find(QString::fromLatin1(key));
        if (it == found.end())
            return;
        Finding f = it.value();
        f.title = title;
        f.explanation = expl;
        f.suggestions = tips;
        out.append(f);
    };
    const QString raiseTip = T("In the instance settings, open Java and raise the maximum memory (for example 4096 MiB for a lightly modded game, "
                            "6-8 GiB for a large modpack). Never allocate more than about 75% of your computer's RAM.");
    report("heap", T("The game ran out of memory"),
         T("Java's memory pool was full, so Minecraft could not continue. This is common with large modpacks, shaders and high render distances."),
         { raiseTip, T("Lower the render distance, or remove shaders and heavy mods, if raising the limit is not possible.") });
    report("gc", T("The game ran out of memory"),
         T("Java spent almost all of its time trying to free memory and gave up. The memory limit is too low for what is loaded."), { raiseTip });
    report("meta", T("Java ran out of class metadata space"),
         T("The part of Java that stores loaded classes is full. This usually means a very large number of mods or a mod that generates classes in a loop."),
         { T("Remove recently added mods and see whether the problem goes away."),
           T("Advanced: add -XX:MaxMetaspaceSize=512m (or larger) to the JVM arguments in the instance's Java settings.") });
    report("thread", T("The system refused to create more threads"),
         T("Java asked Windows for a new thread and was denied. The computer is short on memory, or too much memory is reserved for the game."),
         { T("Close other programs, and lower the maximum memory if it is set above 75% of your RAM.") });
    report("reserve", T("Java could not reserve the requested memory"),
         T("Java was started with a memory limit larger than Windows could provide, or a 32-bit Java is in use (it cannot use more than about 1.5 GiB)."),
         { T("Lower the maximum memory in the instance's Java settings to fit within your free RAM."),
           T("Make sure a 64-bit Java is selected in the same settings page; 32-bit Java installs are listed as 32-bit.") });
    report("native", T("The computer ran out of memory"),
         T("Java asked Windows for memory and there was none left to give. Other programs may be using it, or the game limit is set too high."),
         { T("Close other programs and lower the maximum memory in the instance's Java settings."),
           T("Increase the Windows page file size if the problem keeps happening.") });
}

void ruleMissingDependency(const Ctx& c, QList<Finding>& out)
{
    struct Group {
        QString depName, depId;
        QStringList requesters;
        QStringList ranges;
        QList<Evidence> evidence;
    };
    QList<QString> order;
    QHash<QString, Group> groups;
    for (const auto& d : c.deps) {
        if (!d.missing || d.depId == QLatin1String("java") || isLoaderId(d.depId) || d.depId == QLatin1String("minecraft"))
            continue;
        auto& g = groups[d.depId];
        if (g.depId.isEmpty()) {
            g.depId = d.depId;
            g.depName = label(d.depName, d.depId);
            order.append(d.depId);
        }
        const QString requester = label(d.mod, d.modId);
        if (!g.requesters.contains(requester)) {
            g.requesters.append(requester);
            g.ranges.append(d.range);
        }
        g.evidence.append(c.ev(d.src, d.idx));
    }
    for (const auto& key : order) {
        const Group& g = groups[key];
        Finding f = mk("missing-dependency", Severity::Critical, Confidence::Confirmed, T("Missing required mod: %1").arg(g.depName), {});
        QStringList parts;
        for (int i = 0; i < g.requesters.size(); ++i) {
            const QString range = g.ranges[i].isEmpty() ? QString() : friendlyRange(g.ranges[i]);
            parts.append(range.isEmpty() || range == T("any version") ? g.requesters[i] : T("%1 (needs version %2)").arg(g.requesters[i], range));
        }
        f.explanation = T("%1 cannot start without %2, but %2 was not found among the installed mods.").arg(joinNames(parts), g.depName);
        f.suggestions = { T("Install %1 for the same Minecraft version and mod loader as this instance, then launch again.").arg(g.depName),
                          T("Use the instance's mod browser to search for it, or add its .jar file to the mods folder."),
                          T("If you do not need %1, remove the mods that depend on it.").arg(joinNames(g.requesters)) };
        for (const auto& e : g.evidence)
            addEvidence(f, e);
        f.actions = { Action::OpenModsFolder, Action::OpenInstanceSettings };
        out.append(f);
    }
}

void ruleIncompatibleVersion(const Ctx& c, QList<Finding>& out)
{
    struct Group {
        QString depName;
        QStringList lines;
        QList<Evidence> evidence;
        QString actual;
    };
    QList<QString> order;
    QHash<QString, Group> groups;
    for (const auto& d : c.deps) {
        if (d.missing || d.depId == QLatin1String("java"))
            continue;
        auto& g = groups[d.depId];
        if (g.depName.isEmpty()) {
            g.depName = label(d.depName, d.depId);
            order.append(d.depId);
        }
        if (g.actual.isEmpty())
            g.actual = d.actual;
        g.lines.append(T("%1 needs %2 %3").arg(label(d.mod, d.modId), g.depName, friendlyRange(d.range)));
        g.evidence.append(c.ev(d.src, d.idx));
    }
    for (const auto& key : order) {
        const Group& g = groups[key];
        const bool isMc = key == QLatin1String("minecraft");
        const bool isLoader = isLoaderId(key);
        QString title, expl;
        QStringList tips;
        const QString actualSuffix = g.actual.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(g.actual);
        if (isMc) {
            title = T("Some mods are not made for this Minecraft version");
            expl = T("The instance runs Minecraft %1, but %2.").arg(g.actual.isEmpty() ? T("(see log)") : g.actual, g.lines.join(QStringLiteral("; ")));
            tips = { T("Download the version of each listed mod that matches this instance's Minecraft version, or remove the mod."),
                     T("Alternatively change the Minecraft version in the instance's Version page to the one the mods support.") };
        } else if (isLoader) {
            title = T("A mod needs a different %1 version").arg(g.depName);
            expl = T("The installed %1%2 does not satisfy the requirements: %3.").arg(g.depName, actualSuffix, g.lines.join(QStringLiteral("; ")));
            tips = { T("Change the loader version in the instance's Version page to one in the required range."),
                     T("Or use an older/newer build of the mod that matches the installed loader.") };
        } else {
            title = T("Wrong version of %1").arg(g.depName);
            expl = T("%1, but a different version%2 is installed.").arg(g.lines.join(QStringLiteral("; ")), actualSuffix);
            tips = { T("Update or downgrade %1 so it satisfies the requirement above.").arg(g.depName),
                     T("Check that every mod involved was built for the same Minecraft version.") };
        }
        Finding f = mk("incompatible-version", Severity::Critical, Confidence::Confirmed, title, expl);
        f.suggestions = tips;
        for (const auto& e : g.evidence)
            addEvidence(f, e);
        f.actions = { Action::OpenModsFolder, Action::OpenInstanceSettings };
        out.append(f);
    }

    // Mods that declare each other incompatible.
    static const QRegularExpression conflict(
        R"(Mod '?(?<a>[^'(]+?)'? \((?<aid>[^)]+)\)[^\n]*? (?:conflicts with|is incompatible with|breaks)[^\n]*?mod '?(?<b>[^'(]+?)'? \((?<bid>[^)]+)\))");
    c.scan([&](int s, int i, const QString& line) {
        if (!line.contains(QLatin1String("conflicts with")) && !line.contains(QLatin1String("incompatible with")) &&
            !line.contains(QLatin1String("breaks")))
            return;
        const auto m = conflict.match(line);
        if (!m.hasMatch())
            return;
        const QString a = label(m.captured("a"), m.captured("aid")), b = label(m.captured("b"), m.captured("bid"));
        Finding f = mk("incompatible-version", Severity::Critical, Confidence::Confirmed, T("%1 and %2 cannot be used together").arg(a, b),
                       T("%1 declares itself incompatible with %2, so the game refuses to start with both installed.").arg(a, b));
        f.suggestions = { T("Remove either %1 or %2 from the mods folder.").arg(a, b),
                          T("Check whether an updated version of either mod removes the conflict.") };
        addEvidence(f, c.ev(s, i));
        f.actions = { Action::OpenModsFolder };
        out.append(f);
    });
}

void ruleDuplicateMods(const Ctx& c, QList<Finding>& out)
{
    static const QRegularExpression forge(R"(Mod ID: '(?<id>[^']+)' from mod files: (?<files>.+))");
    static const QRegularExpression fabric(R"(Duplicate mod (?:id|ID)[: ]+'?(?<id>[\w\-.]+))");
    static const QRegularExpression header(R"((?i)found duplicate mods|duplicate mods? found|duplicate mod ids?)");

    Finding f = mk("duplicate-mods", Severity::Critical, Confidence::Confirmed, {}, {});
    QStringList ids;
    QStringList fileHints;
    bool headerSeen = false;
    c.scan([&](int s, int i, const QString& line) {
        if (auto m = forge.match(line); m.hasMatch()) {
            ids << m.captured("id");
            fileHints << m.captured("files").trimmed();
            addEvidence(f, c.ev(s, i));
        } else if (auto m2 = fabric.match(line); m2.hasMatch()) {
            ids << m2.captured("id");
            addEvidence(f, c.ev(s, i));
        } else if (header.match(line).hasMatch() && !isNoiseLevel(line)) {
            headerSeen = true;
            addEvidence(f, c.ev(s, i));
        }
    });
    ids.removeDuplicates();
    if (ids.isEmpty() && !headerSeen)
        return;
    f.title = T("The same mod is installed more than once");
    if (ids.isEmpty()) {
        f.explanation = T("The game found two copies of the same mod in the mods folder and refuses to start.");
    } else {
        f.explanation = T("These mods exist in more than one file: %1.").arg(joinNames(ids, 5));
        if (ids.size() == 1 && !fileHints.isEmpty())
            f.explanation += T(" Files: %1.").arg(fileHints.first());
    }
    f.suggestions = { T("Open the mods folder and delete the older copy of each duplicated mod (keep one .jar per mod)."),
                      T("Check subfolders of the mods folder too: Forge and NeoForge load .jar files from there.") };
    f.actions = { Action::OpenModsFolder };
    out.append(f);
}

QString modFromMixinConfig(QString config)
{
    config = lastPathSegment(config);
    static const QRegularExpression ext(QStringLiteral("\\.json5?$"));
    config.remove(ext);
    if (config.startsWith(QLatin1String("mixins.")))
        config = config.mid(7);
    else if (config.endsWith(QLatin1String(".mixins")))
        config.chop(7);
    else if (config.endsWith(QLatin1String(".mixin")))
        config.chop(6);
    return config;
}

void ruleMixin(const Ctx& c, QList<Finding>& out)
{
    static const QRegularExpression marker(
        R"(Mixin apply (?:for mod \S+ )?failed|InvalidInjectionException|MixinApplyError|InjectionError|Mixin transformation of .+ failed|FAILED during APPLY|Critical injection failure|MixinTransformerError)");
    static const QRegularExpression fromMod(R"(from mod (?<mod>[\w\-.]+))");
    static const QRegularExpression applyForMod(R"(Mixin apply for mod (?<mod>[\w\-.]+) failed)");
    static const QRegularExpression config(R"((?<cfg>[\w\-.]+\.json5?)(?::[\w.$]+)?)");
    static const QRegularExpression configBracket(R"(config \[(?<cfg>[\w\-.]+\.json5?)\])");

    QHash<QString, Finding> byMod;
    QStringList order;
    c.scan([&](int s, int i, const QString& line) {
        if (!marker.match(line).hasMatch() || isNoiseLevel(line))
            return;
        // Look at this line and the next two for the mod / config name.
        QString mod;
        for (int k = i; k < std::min<int>(i + 3, c.lines[s].size()) && mod.isEmpty(); ++k) {
            const QString& l = c.lines[s][k];
            if (auto m = applyForMod.match(l); m.hasMatch())
                mod = m.captured("mod");
            else if (auto m2 = fromMod.match(l); m2.hasMatch())
                mod = m2.captured("mod");
            else if (auto m3 = configBracket.match(l); m3.hasMatch())
                mod = modFromMixinConfig(m3.captured("cfg"));
            else if (l.contains(QLatin1String("mixin"), Qt::CaseInsensitive) && l.contains(QLatin1String(".json")))
                if (auto m4 = config.match(l); m4.hasMatch())
                    mod = modFromMixinConfig(m4.captured("cfg"));
        }
        if (mod.isEmpty() || mod == QLatin1String("minecraft"))
            return;
        if (!byMod.contains(mod)) {
            byMod.insert(mod, mk("mixin-failure", Severity::Critical, Confidence::Likely, T("A mixin from \"%1\" failed to apply").arg(mod), {}));
            order.append(mod);
        }
        addEvidence(byMod[mod], c.ev(s, i));
    });
    for (const auto& mod : order) {
        Finding f = byMod[mod];
        f.explanation = T("Mixins are how mods change the game's code. A change made by \"%1\" could not be applied, usually because the mod "
                          "was built for another Minecraft version, or because another mod changes the same code.")
                            .arg(mod);
        f.suggestions = { T("Update \"%1\" to a version made for this exact Minecraft version, or remove it.").arg(mod),
                          T("If it still fails after updating, another mod is probably clashing with it: remove recently added mods one at a time.") };
        f.actions = { Action::OpenModsFolder, Action::OpenCrashReport };
        out.append(f);
    }
}

bool isIndented(const QString& line)
{
    return line.startsWith(QLatin1Char('\t')) || line.startsWith(QLatin1Char(' '));
}

void ruleSuspectedMods(const Ctx& c, QList<Finding>& out)
{
    static const QRegularExpression header(R"(^\s*Suspected Mods?:\s*(?<rest>.*)$)");
    static const QRegularExpression entry(R"(^\s*(?<name>.+?) \((?<id>[\w\-.]+)\)(?:, Version: (?<ver>\S+))?\s*$)");
    static const QRegularExpression mixinsHeader(R"(^\s*Mixins in Stacktrace:\s*$)");
    static const QRegularExpression mixinCfg(R"(^\s*(?<cfg>[\w\-.]+\.json5?):\w)");

    for (int s = 0; s < c.lines.size(); ++s) {
        const auto& ls = c.lines[s];
        for (int i = 0; i < ls.size(); ++i) {
            if (auto m = header.match(ls[i]); m.hasMatch()) {
                QStringList names;
                QList<Evidence> ev;
                ev.append(c.ev(s, i));
                auto consider = [&](const QString& text, int idx) {
                    if (text.contains(QLatin1String("TRANSFORMER/")) || text.trimmed().startsWith(QLatin1String("at ")))
                        return;
                    for (const QString& part : text.split(QStringLiteral("), "), Qt::SkipEmptyParts)) {
                        QString p = part.trimmed();
                        if (!p.endsWith(QLatin1Char(')')) && p.contains(QLatin1Char('(')))
                            p += QLatin1Char(')');
                        // "Create (create), Version: 0.5" splits on "), " so re-attach the version-less form.
                        const auto e = entry.match(p);
                        if (!e.hasMatch())
                            continue;
                        const QString id = e.captured("id");
                        if (isLoaderId(id) || id == QLatin1String("minecraft") || id == QLatin1String("javafml"))
                            continue;
                        names << label(e.captured("name"), id);
                        ev.append(c.ev(s, idx));
                    }
                };
                const QString rest = m.captured("rest").trimmed();
                if (!rest.isEmpty() && rest.compare(QLatin1String("None"), Qt::CaseInsensitive) != 0)
                    consider(rest, i);
                for (int k = i + 1; k < std::min<int>(i + 40, ls.size()); ++k) {
                    if (ls[k].trimmed().isEmpty() || !isIndented(ls[k]))
                        break;
                    consider(ls[k], k);
                }
                names.removeDuplicates();
                if (names.isEmpty())
                    continue;
                const bool single = names.size() == 1;
                Finding f = mk("suspected-mods", Severity::Warning, single ? Confidence::Likely : Confidence::Possible,
                               single ? T("Suspected mod: %1").arg(names.first()) : T("Suspected mods: %1").arg(joinNames(names)), {});
                f.explanation = single ? T("The crash report's own analysis found code from \"%1\" in the error trace. The mod is the most likely place to start, "
                                           "although it may only be where another problem surfaced.")
                                               .arg(names.first())
                                       : T("The crash report found code from several mods in the error trace (%1). One of them is probably involved, "
                                           "but the report cannot tell which.")
                                               .arg(joinNames(names));
                f.suggestions = { T("Update the suspected mod(s) to the latest version for this Minecraft version."),
                                  T("If that does not help, remove them one at a time (or half of them at a time) and launch again.") };
                for (const auto& e : ev)
                    addEvidence(f, e);
                f.actions = { Action::OpenModsFolder, Action::OpenCrashReport };
                out.append(f);
            } else if (mixinsHeader.match(ls[i]).hasMatch()) {
                QStringList mods;
                QList<Evidence> ev;
                ev.append(c.ev(s, i));
                for (int k = i + 1; k < std::min<int>(i + 60, ls.size()); ++k) {
                    if (ls[k].trimmed().isEmpty() || !isIndented(ls[k]))
                        break;
                    if (auto cm = mixinCfg.match(ls[k]); cm.hasMatch()) {
                        const QString mod = modFromMixinConfig(cm.captured("cfg"));
                        if (!mod.isEmpty() && mod != QLatin1String("minecraft") && !mods.contains(mod)) {
                            mods << mod;
                            ev.append(c.ev(s, k));
                        }
                    }
                }
                if (mods.isEmpty())
                    continue;
                Finding f = mk("suspected-mods", Severity::Warning, Confidence::Possible, T("Mixins from %1 appear in the error trace").arg(joinNames(mods)), {});
                f.explanation = T("These mods changed the game code that was running when it crashed (%1). That makes them suspects, not proof: "
                                  "mixins show up in a trace even when the bug is elsewhere.")
                                    .arg(joinNames(mods));
                f.suggestions = { T("Update these mods to the latest version for this Minecraft version."),
                                  T("If the crash continues, temporarily remove them and launch again.") };
                for (const auto& e : ev)
                    addEvidence(f, e);
                f.actions = { Action::OpenModsFolder, Action::OpenCrashReport };
                out.append(f);
            }
        }
    }
}

const QRegularExpression& gpuDllRegex()
{
    static const QRegularExpression re(
        R"((?<dll>\b(?:atio6axx|atioglxx|atig6pxx|amdxx\d*|amdxc\d*|aticfx\d*|nvoglv(?:32|64)|nvwgf2umx|nvd3dum|ig[a-z0-9]*(?:icd|iumd|umd)\d+|igxprd(?:32|64)|opengl32)\.dll))",
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

QStringList driverSuggestions()
{
    return { T("Update the graphics driver from the manufacturer's site (NVIDIA, AMD or Intel), not through Windows Update."),
             T("On a laptop with two graphics chips, make sure Java (javaw.exe) runs on the dedicated GPU in the Windows graphics settings."),
             T("Turn off overlays (Discord, Steam, GeForce Experience, MSI Afterburner) while testing, and remove graphics mods or shaders once to check."),
             T("Remote desktop sessions and virtual machines do not provide OpenGL; run the game on the physical screen.") };
}

void ruleGraphics(const Ctx& c, QList<Finding>& out)
{
    static const QRegularExpression glfw(
        R"(GLFW error 6554[234]|WGL: The driver does not appear to support OpenGL|Pixel format not accelerated|Failed to find a suitable pixel format|Couldn't set pixel format|WGL: Failed to make context current|WGL: Failed to create OpenGL context)",
        QRegularExpression::CaseInsensitiveOption);

    Finding glfwFinding = mk("graphics-driver", Severity::Critical, Confidence::Confirmed, {}, {});
    c.scan([&](int s, int i, const QString& line) {
        if (isNoiseLevel(line) && !line.contains(QLatin1String("GLFW error")))
            return;
        if (glfw.match(line).hasMatch())
            addEvidence(glfwFinding, c.ev(s, i));
    });
    if (!glfwFinding.evidence.isEmpty()) {
        glfwFinding.title = T("The graphics driver cannot run Minecraft");
        glfwFinding.explanation = T("Minecraft could not create its OpenGL window: the graphics driver reported that it does not support, or could not set up, the "
                                    "required OpenGL. This is almost always an outdated or missing driver.");
        glfwFinding.suggestions = driverSuggestions();
        glfwFinding.actions = { Action::OpenLogsFolder };
        out.append(glfwFinding);
    }

    // JVM crash inside a vendor driver DLL.
    for (int s = 0; s < c.lines.size(); ++s) {
        if (!c.isHsErr(s))
            continue;
        const auto& ls = c.lines[s];
        bool accessViolation = false;
        QString problem;
        int problemIdx = -1;
        for (int i = 0; i < ls.size(); ++i) {
            if (ls[i].contains(QLatin1String("EXCEPTION_ACCESS_VIOLATION")) || ls[i].contains(QLatin1String("SIGSEGV")))
                accessViolation = true;
            if (ls[i].contains(QLatin1String("Problematic frame:")) && i + 1 < ls.size()) {
                problem = ls[i + 1];
                problemIdx = i + 1;
                break;
            }
        }
        if (problemIdx >= 0) {
            if (const auto m = gpuDllRegex().match(problem); m.hasMatch()) {
                Finding f = mk("graphics-driver", Severity::Critical, Confidence::Likely, T("The JVM crashed inside the graphics driver (%1)").arg(m.captured("dll")), {});
                f.explanation = T("Java stopped in %1, which belongs to the graphics driver%2. The log does not say why, but a driver bug or an outdated driver is "
                                  "the usual cause.")
                                    .arg(m.captured("dll"), accessViolation ? T(" (memory access violation)") : QString());
                f.suggestions = driverSuggestions();
                addEvidence(f, c.ev(s, problemIdx));
                f.actions = { Action::OpenCrashReport };
                out.append(f);
                continue;
            }
        }
        // Not the problematic frame, but a driver DLL right at the top of the native stack.
        for (int i = 0; i < ls.size(); ++i) {
            if (!ls[i].contains(QLatin1String("Native frames:")))
                continue;
            for (int k = i + 1; k < std::min<int>(i + 9, ls.size()); ++k) {
                if (const auto m = gpuDllRegex().match(ls[k]); m.hasMatch() && accessViolation) {
                    Finding f = mk("graphics-driver", Severity::Warning, Confidence::Possible, T("The JVM crashed near the graphics driver (%1)").arg(m.captured("dll")), {});
                    f.explanation = T("A graphics driver library (%1) is among the first calls on the crashed thread's stack. This may point to a driver problem, "
                                      "though it can also be a bug in the game or a mod that the driver tripped over.")
                                        .arg(m.captured("dll"));
                    f.suggestions = driverSuggestions();
                    addEvidence(f, c.ev(s, k));
                    f.actions = { Action::OpenCrashReport };
                    out.append(f);
                    break;
                }
            }
            break;
        }
    }
}

void ruleTickingCrash(const Ctx& c, QList<Finding>& out)
{
    static const QRegularExpression desc(
        R"(^\s*Description:\s*(?<what>Ticking entity|Ticking block entity|Ticking tile entity|Exception ticking world|Ticking player)\s*$)");
    static const QRegularExpression entityType(R"(Entity Type:\s*(?<type>[\w:.\-/]+))");
    static const QRegularExpression blockEntity(R"(^\s*Name:\s*(?<type>[\w:.\-/]+) // )");
    static const QRegularExpression location(R"((?:Entity's Exact location|Block location|Entity's Block location):\s*(?<where>.+)$)");
    for (int s = 0; s < c.lines.size(); ++s) {
        const auto& ls = c.lines[s];
        for (int i = 0; i < ls.size(); ++i) {
            const auto d = desc.match(ls[i]);
            if (!d.hasMatch())
                continue;
            QString type, where;
            for (int k = i + 1; k < std::min<int>(i + 120, ls.size()); ++k) {
                if (type.isEmpty()) {
                    if (auto m = entityType.match(ls[k]); m.hasMatch()) {
                        type = m.captured("type");
                    } else if (ls[k].contains(QLatin1String("Block Entity Details"))) {
                        for (int j = k + 1; j < std::min<int>(k + 6, ls.size()); ++j) {
                            if (auto bm = blockEntity.match(ls[j]); bm.hasMatch()) {
                                type = bm.captured("type");
                                break;
                            }
                        }
                    }
                }
                if (where.isEmpty())
                    if (auto m = location.match(ls[k]); m.hasMatch())
                        where = m.captured("where").trimmed();
            }
            const QString what = d.captured("what");
            const bool entity = what == QLatin1String("Ticking entity") || what == QLatin1String("Ticking player");
            Finding f = mk("ticking-crash", Severity::Critical, Confidence::Likely,
                           entity ? T("The game crashed while updating an entity") : T("The game crashed while updating the world"), {});
            QString expl = T("The crash report says the game failed while processing%1. This is usually caused by a damaged entity or block in the world, or by a "
                             "mod that added it and has since been removed or changed.")
                               .arg(type.isEmpty() ? QString() : QStringLiteral(" %1").arg(type));
            if (!where.isEmpty())
                expl += T(" Location: %1.").arg(where.left(120));
            f.explanation = expl;
            f.suggestions = { T("Restore a backup of the world from before the first crash, if you have one."),
                              T("If the entity or block belongs to a mod, update that mod (or put back the one that was removed)."),
                              T("Without a backup, a world editor such as NBTExplorer or MCASelector can delete the entity/chunk at the location above.") };
            addEvidence(f, c.ev(s, i));
            f.actions = { Action::OpenGameFolder, Action::OpenCrashReport };
            out.append(f);
            break;
        }
    }
}

void ruleConfigCorrupt(const Ctx& c, QList<Finding>& out)
{
    static const QRegularExpression forgeFail(R"(Failed loading config file (?<file>\S+) of type \w+ for (?:mod )?(?<mod>\S+))");
    static const QRegularExpression parseErr(
        R"(ParsingException|TomlParseError|JsonSyntaxException|MalformedJsonException|JsonParseException|JsonIOException|com\.moandjiezana\.toml|IllegalStateException: Expected [A-Z_]+ but was|Unexpected character .* at line)");
    static const QRegularExpression configFile(R"((?:config|configs)[\\/](?<file>[\w\-. /\\]+?\.(?:toml|json5?|cfg|properties|ya?ml|conf|txt))\b)",
                                               QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression failVerb(
        R"((?i)(?:failed|unable|could not|couldn't|error)\b[^\n]{0,40}\b(?:load|read|pars)\w*\b[^\n]{0,40}\bconfig(?:uration)?\b[^\n]*?(?<file>[\w\-.]+\.(?:toml|json5?|cfg|properties|ya?ml|conf)))");

    QHash<QString, Finding> byFile;
    QStringList order;
    auto touch = [&](const QString& file, Severity sev, const QString& mod) -> Finding& {
        if (!byFile.contains(file)) {
            Finding f = mk("config-corrupt", sev, Confidence::Likely, T("Configuration file \"%1\" is damaged").arg(file), {});
            f.explanation = T("The game could not read the file \"%1\"%2. A config file that was edited by hand or cut short during a crash can look like this.")
                                .arg(file, mod.isEmpty() ? QString() : T(" of the mod \"%1\"").arg(mod));
            f.suggestions = { T("Close the game, open the game folder's config folder, and rename \"%1\" so the mod writes a fresh default one.").arg(file),
                              T("If you edited it by hand, check for a missing quote, comma or bracket.") };
            f.actions = { Action::OpenGameFolder };
            byFile.insert(file, f);
            order.append(file);
        } else if (sev == Severity::Critical) {
            byFile[file].severity = Severity::Critical;
        }
        return byFile[file];
    };

    c.scan([&](int s, int i, const QString& line) {
        if (auto m = forgeFail.match(line); m.hasMatch()) {
            Finding& f = touch(lastPathSegment(m.captured("file")), Severity::Critical, m.captured("mod"));
            f.confidence = Confidence::Confirmed;
            addEvidence(f, c.ev(s, i));
            return;
        }
        if (isNoiseLevel(line))
            return;
        if (auto m = failVerb.match(line); m.hasMatch()) {
            addEvidence(touch(m.captured("file"), Severity::Warning, {}), c.ev(s, i));
            return;
        }
        if (parseErr.match(line).hasMatch()) {
            for (int k = std::max(0, i - 6); k < std::min<int>(i + 7, c.lines[s].size()); ++k) {
                if (auto cm = configFile.match(c.lines[s][k]); cm.hasMatch()) {
                    Finding& f = touch(lastPathSegment(cm.captured("file")), Severity::Warning, {});
                    addEvidence(f, c.ev(s, k));
                    addEvidence(f, c.ev(s, i));
                    break;
                }
            }
        }
    });
    for (const auto& file : order)
        out.append(byFile[file]);
}

void ruleWrongLoader(const Ctx& c, QList<Finding>& out)
{
    static const QRegularExpression forgeNoToml(
        R"(Mod file (?<file>.+?) is missing (?:a )?(?:neoforge\.)?mods\.toml(?: file)?|Missing mods\.toml)");
    static const QRegularExpression named(
        R"((?i)\b(?<a>forge|neoforge|fabric|quilt)\b mod\b[^\n]{0,60}\b(?:on|for|with|in) (?:a |an |the )?(?<b>forge|neoforge|fabric|quilt)\b)");
    static const QRegularExpression explicit_(R"((?i)is a (?<a>forge|neoforge|fabric|quilt) mod|requires the (?<b>forge|neoforge|fabric|quilt) mod ?loader)");

    // 1) A dependency on a loader that is not installed means the mod was built for another loader.
    QHash<QString, Finding> byLoader;
    QStringList order;
    for (const auto& d : c.deps) {
        if (!d.missing || !isLoaderId(d.depId))
            continue;
        const QString loader = label(d.depName, d.depId);
        if (!byLoader.contains(d.depId)) {
            Finding nf = mk("wrong-loader", Severity::Critical, Confidence::Likely, T("A mod was built for a different mod loader (%1)").arg(loader), {});
            nf.suggestions = { T("Remove the listed mod, or download the build made for the loader this instance uses."),
                               T("Mod pages list Fabric, Quilt, Forge and NeoForge versions separately.") };
            nf.actions = { Action::OpenModsFolder, Action::OpenInstanceSettings };
            byLoader.insert(d.depId, nf);
            order.append(d.depId);
        }
        Finding& f = byLoader[d.depId];
        f.explanation += (f.explanation.isEmpty() ? QString() : QStringLiteral(" ")) +
                         T("\"%1\" requires %2, which is not part of this instance's mod loader.").arg(label(d.mod, d.modId), loader);
        addEvidence(f, c.ev(d.src, d.idx));
    }
    for (const auto& id : order)
        out.append(byLoader[id]);

    // 2) Forge / NeoForge cannot read a jar without mods.toml: usually a Fabric mod or a plain library.
    Finding noToml = mk("wrong-loader", Severity::Critical, Confidence::Likely, T("A file in the mods folder is not a Forge/NeoForge mod"), {});
    QStringList files;
    c.scan([&](int s, int i, const QString& line) {
        if (isNoiseLevel(line))
            return;
        if (auto m = forgeNoToml.match(line); m.hasMatch()) {
            if (!m.captured("file").isEmpty())
                files << lastPathSegment(m.captured("file"));
            addEvidence(noToml, c.ev(s, i));
        } else if (named.match(line).hasMatch() || explicit_.match(line).hasMatch()) {
            addEvidence(noToml, c.ev(s, i));
        }
    });
    if (!noToml.evidence.isEmpty()) {
        files.removeDuplicates();
        noToml.explanation = files.isEmpty()
                                 ? T("The mod loader rejected a mod because it was built for a different loader (for example a Fabric mod on Forge).")
                                 : T("The mod loader cannot read %1. A jar without mods.toml is usually a mod built for a different loader (for example Fabric), or not a mod at all.")
                                       .arg(joinNames(files, 4));
        noToml.suggestions = { T("Remove that file from the mods folder, or replace it with the build made for this instance's loader."),
                               T("Check the mod's download page: each loader (Fabric, Quilt, Forge, NeoForge) has its own file.") };
        noToml.actions = { Action::OpenModsFolder, Action::OpenInstanceSettings };
        out.append(noToml);
    }
}

void ruleNativeCrash(const Ctx& c, QList<Finding>& out)
{
    static const QRegularExpression lib(R"(\[(?<lib>[^\]+]+)(?:\+0x[0-9a-fA-F]+)?\])");
    static const QRegularExpression overlay(
        R"((?i)gameoverlayrenderer|discordhook|rtsshooks|nahimic|obs-?[\w]*hook|medal|overwolf|msiafterburner|radeonsoftware)");
    for (int s = 0; s < c.lines.size(); ++s) {
        if (!c.isHsErr(s))
            continue;
        const auto& ls = c.lines[s];
        QString problem;
        int idx = -1;
        for (int i = 0; i < ls.size(); ++i) {
            if (ls[i].contains(QLatin1String("Problematic frame:")) && i + 1 < ls.size()) {
                problem = ls[i + 1];
                idx = i + 1;
                break;
            }
        }
        if (idx < 0 || gpuDllRegex().match(problem).hasMatch())
            continue;
        const QString where = lib.match(problem).captured("lib").trimmed();
        const bool overlayHit = overlay.match(problem).hasMatch() || overlay.match(c.sources[s].text.left(200000)).hasMatch();

        Finding f = mk("jvm-native-crash", Severity::Warning, overlayHit ? Confidence::Likely : Confidence::Possible,
                       T("The Java virtual machine crashed in native code"), {});
        f.explanation = T("Java itself stopped (the file hs_err_pid... is its crash dump)%1. Crashes like this are normally caused by drivers, overlay software, "
                          "a damaged Java installation, or failing RAM rather than by a mod.")
                            .arg(where.isEmpty() ? QString() : T(" inside \"%1\"").arg(where));
        if (overlayHit)
            f.explanation += T(" Overlay or capture software was detected in the dump; that kind of program often injects into Java and crashes it.");
        f.suggestions = { T("Close overlay and capture programs (Discord overlay, Steam overlay, OBS, MSI Afterburner, Overwolf) and try again."),
                          T("Update your graphics driver and try a different Java installation in the instance settings."),
                          T("If it keeps happening at random, test your RAM (Windows Memory Diagnostic).") };
        addEvidence(f, c.ev(s, idx));
        f.actions = { Action::OpenCrashReport, Action::OpenInstanceSettings };
        out.append(f);
    }
}

void ruleExitCode(const Ctx& c, QList<Finding>& out)
{
    if (!c.exitCode || *c.exitCode == 0)
        return;
    const quint32 code = static_cast<quint32>(*c.exitCode);
    struct Entry {
        quint32 code;
        const char* name;
        const char* text;
        bool graphics;
    };
    static const Entry table[] = {
        { 0xC0000005, "access violation (0xC0000005)",
          "The program touched memory it should not. For a game this nearly always comes from a graphics driver, an overlay program, or a native library that crashed.",
          true },
        { 0xC0000409, "stack buffer overrun (0xC0000409)", "Windows stopped the process because a native library detected memory corruption.", true },
        { 0xC0000374, "heap corruption (0xC0000374)",
          "Windows detected that memory used by a native library was damaged. Faulty drivers or overlays are the usual cause.", true },
        { 0xC00000FD, "stack overflow (0xC00000FD)", "A program ran out of stack space, typically from endless recursion in a native library.", false },
        { 0xC000001D, "illegal instruction (0xC000001D)",
          "The CPU was asked to run an instruction it does not support. This may point to a Java build for different hardware, or a failing CPU/RAM.", false },
        { 0xC0000135, "missing DLL (0xC0000135)", "A required Windows library was not found, so the process could not start. Java may be installed incompletely.",
          false },
        { 0xC0000142, "DLL initialization failed (0xC0000142)", "A library failed to start. Security software or a broken Java install can cause this.", false },
        { 0xC000013A, "terminated by Ctrl+C (0xC000013A)", "The process was closed from a console, not by a game error.", false },
        { 0x80000003, "breakpoint (0x80000003)", "The process hit a debugger breakpoint, which means a native library aborted on purpose.", false },
    };
    for (const auto& e : table) {
        if (e.code != code)
            continue;
        Finding f = mk("exit-code", Severity::Warning, Confidence::Possible,
                       T("The game exited with a Windows error: %1").arg(QString::fromLatin1(e.name)), T(e.text));
        f.suggestions = e.graphics ? QStringList{ T("Update the graphics driver and close overlay software (Discord, Steam, OBS, Afterburner)."),
                                                  T("Try a different Java version in the instance settings.") }
                                   : QStringList{ T("Try a different Java version in the instance settings, then reinstall Java if it still fails.") };
        f.actions = { Action::OpenInstanceSettings, Action::OpenLogsFolder };
        out.append(f);
        return;
    }
    // Other codes carry no reliable meaning: report nothing rather than guess.
}

// ---------------------------------------------------------------------------------------------
// Rule table
// ---------------------------------------------------------------------------------------------

struct Rule {
    const char* id;
    void (*run)(const Ctx&, QList<Finding>&);
};

const Rule kRules[] = {
    { "wrong-java", ruleJavaVersion },
    { "out-of-memory", ruleOutOfMemory },
    { "missing-dependency", ruleMissingDependency },
    { "incompatible-version", ruleIncompatibleVersion },
    { "duplicate-mods", ruleDuplicateMods },
    { "mixin-failure", ruleMixin },
    { "suspected-mods", ruleSuspectedMods },
    { "graphics-driver", ruleGraphics },
    { "ticking-crash", ruleTickingCrash },
    { "config-corrupt", ruleConfigCorrupt },
    { "wrong-loader", ruleWrongLoader },
    { "jvm-native-crash", ruleNativeCrash },
    { "exit-code", ruleExitCode },
};

}  // namespace

int CrashDetective::ruleCount()
{
    return int(sizeof(kRules) / sizeof(kRules[0]));
}

QStringList CrashDetective::ruleIds()
{
    QStringList ids;
    for (const auto& r : kRules)
        ids << QString::fromLatin1(r.id);
    return ids;
}

QList<Finding> CrashDetective::analyze(const QList<LogSource>& sources, std::optional<int> exitCode)
{
    Ctx ctx(sources, exitCode);
    parseDeps(ctx);

    QList<Finding> raw;
    for (const auto& rule : kRules) {
        // The exit code is only a fallback: skip it when something log-based already explains the failure.
        if (QLatin1String(rule.id) == QLatin1String("exit-code")) {
            const bool explained = std::any_of(raw.begin(), raw.end(), [](const Finding& f) {
                return f.severity != Severity::Info && f.confidence != Confidence::Possible;
            });
            if (explained)
                continue;
        }
        rule.run(ctx, raw);
    }

    // Deduplicate on (rule, title), merging evidence.
    QList<Finding> result;
    for (const Finding& f : raw) {
        auto it = std::find_if(result.begin(), result.end(), [&](const Finding& x) { return x.ruleId == f.ruleId && x.title == f.title; });
        if (it == result.end()) {
            result.append(f);
            continue;
        }
        for (const auto& e : f.evidence)
            addEvidence(*it, e);
        it->severity = std::min(it->severity, f.severity);
        it->confidence = std::min(it->confidence, f.confidence);
    }

    // A finding supported only by files that predate the last launch may describe an earlier session.
    QSet<QString> olderNames;
    for (const auto& s : sources)
        if (s.older)
            olderNames.insert(s.name);
    for (auto& f : result) {
        if (f.evidence.isEmpty() || olderNames.isEmpty())
            continue;
        const bool allOlder = std::all_of(f.evidence.begin(), f.evidence.end(), [&](const Evidence& e) { return olderNames.contains(e.source); });
        if (allOlder)
            f.explanation += T(" Note: this was found only in a file from before the most recent launch, so it may describe an earlier session.");
    }

    std::stable_sort(result.begin(), result.end(), [](const Finding& a, const Finding& b) {
        if (a.severity != b.severity)
            return a.severity < b.severity;
        return a.confidence < b.confidence;
    });
    return result;
}

}  // namespace Zero::Diagnostics
