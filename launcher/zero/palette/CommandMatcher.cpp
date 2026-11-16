// SPDX-License-Identifier: GPL-3.0-only
#include "CommandMatcher.h"

#include <algorithm>
#include <vector>

namespace Zero {
namespace Palette {

namespace {

constexpr double kNoScore = -1e9;
constexpr double kBase = 16.0;
constexpr double kWordStart = 24.0;
constexpr double kFirstChar = 8.0;
constexpr double kConsecutive = 18.0;
constexpr double kExact = 300.0;
constexpr double kPrefix = 150.0;

bool isWordStart(const QString& text, int j)
{
    if (j == 0)
        return true;
    const QChar prev = text.at(j - 1);
    const QChar cur = text.at(j);
    return !prev.isLetterOrNumber() || (prev.isLower() && cur.isUpper());
}

QString foldWithoutSpaces(const QString& s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s)
        if (!c.isSpace())
            out.append(c.toLower());
    return out;
}

}  // namespace

TextMatch matchText(const QString& query, const QString& text)
{
    TextMatch result;
    const QString q = foldWithoutSpaces(query);
    const int m = q.size();
    const int n = text.size();
    if (m == 0) {
        result.matched = true;
        return result;
    }
    if (m > n)
        return result;

    QString lower;
    lower.reserve(n);
    for (const QChar c : text)
        lower.append(c.toLower());

    // Quick reject: is `q` a subsequence at all?
    {
        int qi = 0;
        for (int j = 0; j < n && qi < m; ++j)
            if (lower.at(j) == q.at(qi))
                ++qi;
        if (qi < m)
            return result;
    }

    // best[i][j]: best score with query char i matched at text index j (kNoScore if impossible).
    std::vector<std::vector<double>> best(m, std::vector<double>(n, kNoScore));
    std::vector<std::vector<int>> from(m, std::vector<int>(n, -1));
    for (int j = 0; j < n; ++j) {
        if (lower.at(j) != q.at(0))
            continue;
        double s = kBase;
        if (isWordStart(text, j))
            s += kWordStart;
        if (j == 0)
            s += kFirstChar;
        s -= 0.5 * std::min(j, 30);
        best[0][j] = s;
    }
    for (int i = 1; i < m; ++i) {
        for (int j = i; j < n; ++j) {
            if (lower.at(j) != q.at(i))
                continue;
            double charScore = kBase;
            if (isWordStart(text, j))
                charScore += kWordStart;
            for (int k = i - 1; k < j; ++k) {
                if (best[i - 1][k] <= kNoScore)
                    continue;
                const int gap = j - k - 1;
                const double link = gap == 0 ? kConsecutive : -(4.0 + std::min(gap, 10));
                const double s = best[i - 1][k] + charScore + link;
                if (s > best[i][j]) {
                    best[i][j] = s;
                    from[i][j] = k;
                }
            }
        }
    }
    int endJ = -1;
    double top = kNoScore;
    for (int j = 0; j < n; ++j) {
        if (best[m - 1][j] > top) {
            top = best[m - 1][j];
            endJ = j;
        }
    }
    if (endJ < 0)
        return result;

    QList<int> positions;
    for (int i = m - 1, j = endJ; i >= 0; --i) {
        positions.prepend(j);
        j = from[i][j];
    }

    // Prefix / exact bonuses compare the whitespace-normalised query with the text.
    const QString fullQuery = query.simplified().toLower();
    const QString fullText = lower.simplified();
    if (!fullQuery.isEmpty() && fullText == fullQuery) {
        top += kExact;
    } else if (!fullQuery.isEmpty() && lower.startsWith(fullQuery)) {
        top += kPrefix;
        positions.clear();
        for (int i = 0; i < fullQuery.size(); ++i)
            if (!fullQuery.at(i).isSpace())
                positions.append(i);
    }
    top -= std::min(0.2 * (n - m), 10.0);  // shorter titles are a closer fit
    result.matched = true;
    result.score = top;
    result.positions = positions;
    return result;
}

double recencyBoost(qint64 lastUsedMs, qint64 nowMs)
{
    if (lastUsedMs <= 0)
        return 0.0;
    const double ageDays = std::max<double>(0.0, double(nowMs - lastUsedMs) / 86400000.0);
    return 20.0 / (1.0 + ageDays / 3.0);
}

double kindWeight(Kind kind)
{
    switch (kind) {
        case Kind::Instance:
            return 4.0;
        case Kind::Navigation:
            return 3.0;
        case Kind::Action:
            return 2.0;
        case Kind::Setting:
        case Kind::Appearance:
            return 1.0;
        case Kind::Server:
            return 1.5;
    }
    return 0.0;
}

QList<Ranked> rank(const QString& query, const QList<Candidate>& candidates, qint64 nowMs)
{
    QList<Ranked> out;
    const bool emptyQuery = foldWithoutSpaces(query).isEmpty();
    for (int i = 0; i < candidates.size(); ++i) {
        const Candidate& c = candidates.at(i);
        Ranked r;
        r.index = i;
        double matchScore = 0.0;
        if (!emptyQuery) {
            const TextMatch title = matchText(query, c.title);
            double keywordScore = kNoScore;
            for (const QString& kw : c.keywords) {
                const TextMatch k = matchText(query, kw);
                if (k.matched)
                    keywordScore = std::max(keywordScore, k.score * kKeywordFactor);
            }
            if (!title.matched && keywordScore <= kNoScore)
                continue;
            if (title.matched && title.score >= keywordScore) {
                matchScore = title.score;
                r.positions = title.positions;
            } else {
                matchScore = keywordScore;
                r.viaKeyword = true;
            }
        }
        r.score = matchScore + (c.kind == Kind::Instance ? recencyBoost(c.lastUsedMs, nowMs) : 0.0) +
                  (c.kind == Kind::Server && c.favorite ? kFavoriteBoost : 0.0) + kindWeight(c.kind);
        out.append(r);
    }
    std::stable_sort(out.begin(), out.end(), [&candidates](const Ranked& a, const Ranked& b) {
        if (std::abs(a.score - b.score) > 1e-9)
            return a.score > b.score;
        const Candidate& ca = candidates.at(a.index);
        const Candidate& cb = candidates.at(b.index);
        if (ca.kind != cb.kind)
            return int(ca.kind) < int(cb.kind);
        const int byTitle = QString::compare(ca.title, cb.title, Qt::CaseInsensitive);
        if (byTitle != 0)
            return byTitle < 0;
        return a.index < b.index;
    });
    return out;
}

}  // namespace Palette
}  // namespace Zero
