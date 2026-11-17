// SPDX-License-Identifier: GPL-3.0-only
#include "Versions.h"

#include <QCoreApplication>
#include <QList>
#include <QRegularExpression>
#include <QStringList>

namespace Zero {
namespace Mods {

namespace {

struct Token {
    bool numeric = false;
    QString text;  // digits without leading zeros, or lowercase letters
};

struct Parsed {
    QStringList core;  // numeric segments, no leading zeros
    QList<Token> pre;
    bool ambiguousPre = false;  // the qualifier is not a well known pre-release tag
};

bool allDigits(const QString& s)
{
    if (s.isEmpty())
        return false;
    for (QChar c : s)
        if (c < QLatin1Char('0') || c > QLatin1Char('9'))
            return false;
    return true;
}

QString stripZeros(const QString& digits)
{
    int i = 0;
    while (i < digits.size() - 1 && digits[i] == QLatin1Char('0'))
        ++i;
    return digits.mid(i);
}

int compareNumbers(const QString& a, const QString& b)  // both normalized digit strings
{
    if (a.size() != b.size())
        return a.size() < b.size() ? -1 : 1;
    const int c = a.compare(b);
    return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

bool isKnownPreTag(const QString& t)
{
    static const QStringList known = { "alpha", "beta", "a", "b", "rc", "cr", "pre", "preview", "snapshot", "dev", "milestone", "m", "nightly" };
    return known.contains(t);
}

std::optional<Parsed> parseVersion(const QString& input)
{
    QString s = input.trimmed();
    if (s.size() > 1 && (s[0] == QLatin1Char('v') || s[0] == QLatin1Char('V')) && s[1].isDigit())
        s.remove(0, 1);
    if (s.isEmpty())
        return std::nullopt;
    static const QRegularExpression snapshot(QStringLiteral("^\\d{2}w\\d{2}[a-z]"));
    if (snapshot.match(s).hasMatch())
        return std::nullopt;
    if (const int plus = s.indexOf(QLatin1Char('+')); plus >= 0)
        s.truncate(plus);
    QString coreText = s;
    QString preText;
    if (const int dash = s.indexOf(QLatin1Char('-')); dash >= 0) {
        coreText = s.left(dash);
        preText = s.mid(dash + 1);
        if (preText.isEmpty())
            return std::nullopt;
    }
    Parsed p;
    const QStringList segments = coreText.split(QLatin1Char('.'));
    if (segments.isEmpty() || segments.size() > 8)
        return std::nullopt;
    for (const QString& seg : segments) {
        if (!allDigits(seg))
            return std::nullopt;
        p.core.append(stripZeros(seg));
    }
    if (!preText.isEmpty()) {
        QString run;
        bool runNumeric = false;
        auto flush = [&]() {
            if (run.isEmpty())
                return;
            Token t;
            t.numeric = runNumeric;
            t.text = runNumeric ? stripZeros(run) : run.toLower();
            p.pre.append(t);
            run.clear();
        };
        for (QChar c : preText) {
            if (c.unicode() < 128 && c.isLetterOrNumber()) {
                const bool digit = c.isDigit();
                if (!run.isEmpty() && digit != runNumeric)
                    flush();
                runNumeric = digit;
                run.append(c);
            } else if (c == QLatin1Char('.') || c == QLatin1Char('-') || c == QLatin1Char('_')) {
                flush();
            } else {
                return std::nullopt;
            }
        }
        flush();
        if (p.pre.isEmpty())
            return std::nullopt;
        // `1.0-1` reads as "newer than 1.0" in Maven and "older" in semver; unknown words behave like that too.
        p.ambiguousPre = p.pre.first().numeric || !isKnownPreTag(p.pre.first().text);
    }
    return p;
}

int sign(int v)
{
    return v < 0 ? -1 : (v > 0 ? 1 : 0);
}

std::optional<int> compareParsed(const Parsed& a, const Parsed& b)
{
    const int n = qMax(a.core.size(), b.core.size());
    for (int i = 0; i < n; ++i) {
        const QString x = i < a.core.size() ? a.core[i] : QStringLiteral("0");
        const QString y = i < b.core.size() ? b.core[i] : QStringLiteral("0");
        if (const int c = compareNumbers(x, y); c != 0)
            return c;
    }
    const bool ap = !a.pre.isEmpty();
    const bool bp = !b.pre.isEmpty();
    if (!ap && !bp)
        return 0;
    if (ap != bp) {
        const Parsed& withPre = ap ? a : b;
        if (withPre.ambiguousPre)
            return std::nullopt;
        return ap ? -1 : 1;
    }
    // both carry a qualifier
    bool equalTokens = a.pre.size() == b.pre.size();
    for (int i = 0; equalTokens && i < a.pre.size(); ++i)
        equalTokens = a.pre[i].numeric == b.pre[i].numeric && a.pre[i].text == b.pre[i].text;
    if (equalTokens)
        return 0;
    if (a.ambiguousPre || b.ambiguousPre) {
        const bool bothNumeric = a.pre.first().numeric && b.pre.first().numeric;
        if (!bothNumeric)
            return std::nullopt;
    }
    const int m = qMin(a.pre.size(), b.pre.size());
    for (int i = 0; i < m; ++i) {
        const Token& x = a.pre[i];
        const Token& y = b.pre[i];
        if (x.numeric != y.numeric)
            return x.numeric ? -1 : 1;  // numeric identifiers sort below alphanumeric ones
        const int c = x.numeric ? compareNumbers(x.text, y.text) : sign(x.text.compare(y.text));
        if (c != 0)
            return c;
    }
    return a.pre.size() < b.pre.size() ? -1 : (a.pre.size() > b.pre.size() ? 1 : 0);
}

std::optional<int> cmp(const Parsed& v, const QString& bound)
{
    const auto b = parseVersion(bound);
    if (!b)
        return std::nullopt;
    return compareParsed(v, *b);
}

Tri fromCmp(const std::optional<int>& c, bool (*pred)(int))
{
    if (!c)
        return Tri::Unknown;
    return pred(*c) ? Tri::Yes : Tri::No;
}

Tri andOf(Tri a, Tri b)
{
    if (a == Tri::No || b == Tri::No)
        return Tri::No;
    if (a == Tri::Unknown || b == Tri::Unknown)
        return Tri::Unknown;
    return Tri::Yes;
}

Tri orOf(Tri a, Tri b)
{
    if (a == Tri::Yes || b == Tri::Yes)
        return Tri::Yes;
    if (a == Tri::Unknown || b == Tri::Unknown)
        return Tri::Unknown;
    return Tri::No;
}

bool isWildcard(const QString& seg)
{
    return seg == QLatin1String("x") || seg == QLatin1String("X") || seg == QLatin1String("*");
}

const QStringList& operators()
{
    // longest first: ">=" must win over ">"
    static const QStringList ops = { ">=", "<=", ">", "<", "=", "~", "^" };
    return ops;
}

void splitOperator(const QString& term, QString& op, QString& rest)
{
    op.clear();
    rest = term;
    for (const QString& candidate : operators()) {
        if (term.startsWith(candidate)) {
            op = candidate;
            rest = term.mid(candidate.size());
            return;
        }
    }
}

// ---- Fabric ----

Tri matchTerm(const Parsed& v, const QString& term)
{
    QString op;
    QString rest;
    splitOperator(term, op, rest);
    rest = rest.trimmed();
    bool lowest = false;
    if (rest.size() > 1 && rest.endsWith(QLatin1Char('-'))) {
        lowest = true;
        rest.chop(1);
    }
    if (rest.isEmpty())
        return Tri::Unknown;
    if (isWildcard(rest))
        return op.isEmpty() || op == QLatin1String("=") ? Tri::Yes : Tri::Unknown;

    const QStringList segs = rest.split(QLatin1Char('.'));
    bool hasWildcard = false;
    for (const QString& s : segs)
        hasWildcard = hasWildcard || isWildcard(s);
    if (hasWildcard) {
        if (!op.isEmpty() && op != QLatin1String("="))
            return Tri::Unknown;
        // wildcards only at the end: 1.x, 1.2.x, 1.2.*
        QStringList prefix;
        for (const QString& s : segs) {
            if (isWildcard(s))
                break;
            if (!allDigits(s))
                return Tri::Unknown;
            prefix.append(stripZeros(s));
        }
        for (int i = prefix.size() + 1; i < segs.size(); ++i)
            if (!isWildcard(segs[i]))
                return Tri::Unknown;
        for (int i = 0; i < prefix.size(); ++i) {
            const QString have = i < v.core.size() ? v.core[i] : QStringLiteral("0");
            if (compareNumbers(have, prefix[i]) != 0)
                return Tri::No;
        }
        return Tri::Yes;
    }

    // A trailing "-" ("1.19.4-") is Fabric's way to say "the lowest pre-release of 1.19.4": the bound itself, but every pre-release of
    // it counts as at-or-above it, and a version below 1.19.4 is below it.
    const auto bound = parseVersion(rest);
    if (!bound)
        return Tri::Unknown;
    const auto c = compareParsed(v, *bound);
    bool preOfBound = false;
    if (lowest && !v.pre.isEmpty()) {
        Parsed bare = v;
        bare.pre.clear();
        bare.ambiguousPre = false;
        const auto core = compareParsed(bare, *bound);
        preOfBound = core && *core == 0;
    }
    // "at or above the bound" and its strict forms, with the pre-release rule applied
    const auto atLeast = [&]() { return lowest && preOfBound ? Tri::Yes : fromCmp(c, [](int x) { return x >= 0; }); };
    if (op.isEmpty() || op == QLatin1String("=")) {
        if (lowest)
            return preOfBound || (c && *c == 0) ? Tri::Unknown : (c ? Tri::No : Tri::Unknown);
        return fromCmp(c, [](int x) { return x == 0; });
    }
    if (op == QLatin1String(">="))
        return atLeast();
    if (op == QLatin1String(">")) {
        if (lowest && preOfBound)
            return Tri::Unknown;  // above the lowest pre-release, or the lowest itself: cannot tell
        return fromCmp(c, [](int x) { return x > 0; });
    }
    if (op == QLatin1String("<=")) {
        if (lowest && preOfBound)
            return Tri::Unknown;
        if (lowest)
            return fromCmp(c, [](int x) { return x < 0; });  // equal to the bound is above its lowest pre-release
        return fromCmp(c, [](int x) { return x <= 0; });
    }
    if (op == QLatin1String("<")) {
        if (lowest && preOfBound)
            return Tri::No;
        return fromCmp(c, [](int x) { return x < 0; });
    }

    // ~ (same minor) and ^ (same major): lower bound inclusive, computed upper bound exclusive
    const QStringList base = bound->core;
    const int bumpIndex = (op == QLatin1String("~") && base.size() >= 2) ? 1 : 0;
    QStringList upper = base.mid(0, bumpIndex + 1);
    bool ok = false;
    const qulonglong n = upper[bumpIndex].toULongLong(&ok);
    if (!ok)
        return Tri::Unknown;
    upper[bumpIndex] = QString::number(n + 1);
    return andOf(atLeast(), fromCmp(cmp(v, upper.join(QLatin1Char('.'))), [](int x) { return x < 0; }));
}

QStringList splitTerms(const QString& alternative)
{
    const QStringList raw = alternative.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    QStringList terms;
    for (int i = 0; i < raw.size(); ++i) {
        QString t = raw[i];
        if (operators().contains(t) && i + 1 < raw.size())
            t += raw[++i];  // ">= 1.0" written with a space
        terms.append(t);
    }
    return terms;
}

Tri matchAnd(const Parsed& v, const QString& alternative)
{
    const QStringList terms = splitTerms(alternative);
    if (terms.isEmpty())
        return Tri::Unknown;
    Tri result = Tri::Yes;
    for (const QString& t : terms)
        result = andOf(result, matchTerm(v, t));
    return result;
}

// ---- Maven ----

Tri checkLower(const Parsed& v, const QString& lo, bool inclusive)
{
    if (lo.isEmpty())
        return Tri::Yes;
    const auto c = cmp(v, lo);
    if (!c)
        return Tri::Unknown;
    return (inclusive ? *c >= 0 : *c > 0) ? Tri::Yes : Tri::No;
}

Tri checkUpper(const Parsed& v, const QString& hi, bool inclusive)
{
    if (hi.isEmpty())
        return Tri::Yes;
    const auto c = cmp(v, hi);
    if (!c)
        return Tri::Unknown;
    return (inclusive ? *c <= 0 : *c < 0) ? Tri::Yes : Tri::No;
}

QString describeTerm(const QString& term)
{
    QString op;
    QString rest;
    splitOperator(term, op, rest);
    if (rest.size() > 1 && rest.endsWith(QLatin1Char('-')))
        rest.chop(1);  // ">=1.19.4-" (lowest pre-release) reads "1.19.4 or newer"
    if (rest.isEmpty())
        return term;
    if (isWildcard(rest))
        return QCoreApplication::translate("ModHealth", "any version");
    if (op == QLatin1String(">="))
        return QCoreApplication::translate("ModHealth", "%1 or newer").arg(rest);
    if (op == QLatin1String(">"))
        return QCoreApplication::translate("ModHealth", "newer than %1").arg(rest);
    if (op == QLatin1String("<="))
        return QCoreApplication::translate("ModHealth", "%1 or older").arg(rest);
    if (op == QLatin1String("<"))
        return QCoreApplication::translate("ModHealth", "older than %1").arg(rest);
    if (op == QLatin1String("~"))
        return QCoreApplication::translate("ModHealth", "%1 (same minor version)").arg(rest);
    if (op == QLatin1String("^"))
        return QCoreApplication::translate("ModHealth", "%1 (same major version)").arg(rest);
    return rest;
}

QString describeMaven(const QString& range)
{
    QStringList parts;
    int i = 0;
    while (i < range.size()) {
        const QChar c = range[i];
        if (c.isSpace() || c == QLatin1Char(',')) {
            ++i;
            continue;
        }
        if (c == QLatin1Char('[') || c == QLatin1Char('(')) {
            int end = i + 1;
            while (end < range.size() && range[end] != QLatin1Char(']') && range[end] != QLatin1Char(')'))
                ++end;
            if (end >= range.size())
                return range;
            const bool loInc = c == QLatin1Char('[');
            const bool hiInc = range[end] == QLatin1Char(']');
            const QString inner = range.mid(i + 1, end - i - 1);
            const int comma = inner.indexOf(QLatin1Char(','));
            if (comma < 0) {
                parts.append(inner.trimmed());
            } else {
                const QString lo = inner.left(comma).trimmed();
                const QString hi = inner.mid(comma + 1).trimmed();
                if (!lo.isEmpty() && hi.isEmpty())
                    parts.append(loInc ? QCoreApplication::translate("ModHealth", "%1 or newer").arg(lo) : QCoreApplication::translate("ModHealth", "newer than %1").arg(lo));
                else if (lo.isEmpty() && !hi.isEmpty())
                    parts.append(hiInc ? QCoreApplication::translate("ModHealth", "%1 or older").arg(hi) : QCoreApplication::translate("ModHealth", "older than %1").arg(hi));
                else if (!lo.isEmpty() && !hi.isEmpty())
                    parts.append(hiInc ? QCoreApplication::translate("ModHealth", "%1 to %2").arg(lo, hi) : QCoreApplication::translate("ModHealth", "%1 up to, but not including, %2").arg(lo, hi));
                else
                    parts.append(QCoreApplication::translate("ModHealth", "any version"));
            }
            i = end + 1;
        } else {
            int end = i;
            while (end < range.size() && range[end] != QLatin1Char(','))
                ++end;
            parts.append(QCoreApplication::translate("ModHealth", "%1 or newer").arg(range.mid(i, end - i).trimmed()));
            i = end;
        }
    }
    return parts.isEmpty() ? range : parts.join(QCoreApplication::translate("ModHealth", " or "));
}

}  // namespace

std::optional<int> compareVersions(const QString& a, const QString& b)
{
    const auto pa = parseVersion(a);
    const auto pb = parseVersion(b);
    if (!pa || !pb)
        return std::nullopt;
    return compareParsed(*pa, *pb);
}

Tri matchesFabricPredicate(const QString& version, const QString& predicate)
{
    const QString pred = predicate.trimmed();
    if (pred.isEmpty() || pred == QLatin1String("*"))
        return Tri::Yes;
    const auto v = parseVersion(version);
    if (!v)
        return Tri::Unknown;
    Tri result = Tri::No;
    for (const QString& alternative : pred.split(QStringLiteral("||")))
        result = orOf(result, matchAnd(*v, alternative.trimmed()));
    return result;
}

Tri matchesMavenRange(const QString& version, const QString& range)
{
    const QString text = range.trimmed();
    if (text.isEmpty() || text == QLatin1String("*"))
        return Tri::Yes;
    const auto v = parseVersion(version);
    if (!v)
        return Tri::Unknown;

    Tri result = Tri::No;
    bool any = false;
    int i = 0;
    while (i < text.size()) {
        const QChar c = text[i];
        if (c.isSpace() || c == QLatin1Char(',')) {
            ++i;
            continue;
        }
        any = true;
        if (c == QLatin1Char('[') || c == QLatin1Char('(')) {
            int end = i + 1;
            while (end < text.size() && text[end] != QLatin1Char(']') && text[end] != QLatin1Char(')'))
                ++end;
            if (end >= text.size())
                return Tri::Unknown;  // unbalanced bracket
            const bool loInc = c == QLatin1Char('[');
            const bool hiInc = text[end] == QLatin1Char(']');
            const QString inner = text.mid(i + 1, end - i - 1);
            Tri part;
            if (const int comma = inner.indexOf(QLatin1Char(',')); comma < 0) {
                if (!loInc || !hiInc || inner.trimmed().isEmpty())
                    part = Tri::Unknown;
                else
                    part = fromCmp(cmp(*v, inner.trimmed()), [](int x) { return x == 0; });
            } else if (inner.indexOf(QLatin1Char(','), comma + 1) >= 0) {
                part = Tri::Unknown;
            } else {
                part = andOf(checkLower(*v, inner.left(comma).trimmed(), loInc), checkUpper(*v, inner.mid(comma + 1).trimmed(), hiInc));
            }
            result = orOf(result, part);
            i = end + 1;
        } else {
            int end = i;
            while (end < text.size() && text[end] != QLatin1Char(','))
                ++end;
            const QString bare = text.mid(i, end - i).trimmed();
            // soft requirement: "at least"
            result = orOf(result, bare == QLatin1String("*") ? Tri::Yes : checkLower(*v, bare, true));
            i = end;
        }
    }
    return any ? result : Tri::Unknown;
}

QString describePredicate(const QString& predicate, bool mavenSyntax)
{
    const QString p = predicate.trimmed();
    if (p.isEmpty() || p == QLatin1String("*"))
        return QCoreApplication::translate("ModHealth", "any version");
    if (mavenSyntax)
        return describeMaven(p);
    QStringList alternatives;
    for (const QString& alt : p.split(QStringLiteral("||"))) {
        QStringList terms;
        for (const QString& t : splitTerms(alt.trimmed()))
            terms.append(describeTerm(t));
        alternatives.append(terms.join(QCoreApplication::translate("ModHealth", " and ")));
    }
    return alternatives.join(QCoreApplication::translate("ModHealth", " or "));
}

}  // namespace Mods
}  // namespace Zero
