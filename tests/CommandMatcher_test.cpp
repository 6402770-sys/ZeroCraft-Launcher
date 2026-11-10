// SPDX-License-Identifier: GPL-3.0-only
// CommandMatcher: the pure ranking logic of the command palette (fuzzy score, aliases, recency, ordering, highlights).
#include <QTest>

#include "zero/palette/CommandMatcher.h"

using namespace Zero::Palette;

namespace {
Candidate cand(const QString& title, Kind kind = Kind::Action, const QStringList& keywords = {}, qint64 lastUsed = 0)
{
    Candidate c;
    c.title = title;
    c.kind = kind;
    c.keywords = keywords;
    c.lastUsedMs = lastUsed;
    return c;
}
constexpr qint64 kNow = 1'800'000'000'000;
constexpr qint64 kHour = 3'600'000;
constexpr qint64 kDay = 24 * kHour;
}  // namespace

class CommandMatcherTest : public QObject {
    Q_OBJECT
   private slots:
    void prefixBeatsSubsequence()
    {
        QVERIFY(matchText("fab", "Fabulous Survival").score > matchText("fab", "Farm A Base").score);
        const auto ranked = rank("fab", { cand("Farm A Base", Kind::Instance), cand("Fabulous Survival", Kind::Instance) }, kNow);
        QCOMPARE(ranked.size(), 2);
        QCOMPARE(ranked.first().index, 1);
    }

    void exactBeatsPrefix()
    {
        QVERIFY(matchText("java", "Java").score > matchText("java", "Java Runtime").score);
    }

    void wordStartsBeatMidWord()
    {
        // "ms": the m and s of "Mod Setup" both start words; "Camsa" matches in the middle of a word.
        QVERIFY(matchText("ms", "Mod Setup").score > matchText("ms", "Camsa").score);
        const auto ranked = rank("ms", { cand("Camsa"), cand("Mod Setup") }, kNow);
        QCOMPARE(ranked.first().index, 1);
    }

    void consecutiveRunsBeatScatteredMatches()
    {
        QVERIFY(matchText("abc", "xx abc").score > matchText("abc", "xx a_x_b_x_c").score);
    }

    void gapPenalty()
    {
        QVERIFY(matchText("ab", "xab").score > matchText("ab", "xaxb").score);
        QVERIFY(matchText("ab", "xaxb").score > matchText("ab", "xaxxxxxxb").score);
    }

    void caseInsensitiveAndSpacesIgnored()
    {
        QVERIFY(matchText("FAB", "fabulous").matched);
        QVERIFY(matchText("set java", "Settings: Java").matched);
        QCOMPARE(matchText("set java", "Settings: Java").positions.size(), 7);
    }

    void aliasesMatchSlightlyBelowTitles()
    {
        const Candidate java = cand("Settings: Java", Kind::Setting, { "memory", "ram", "jvm" });
        const auto viaAlias = rank("ram", { java }, kNow);
        QCOMPARE(viaAlias.size(), 1);
        QVERIFY(viaAlias.first().viaKeyword);
        QVERIFY(viaAlias.first().positions.isEmpty());

        // An equal-quality title match outranks the keyword match.
        const auto ranked = rank("memory", { cand("Settings: Java", Kind::Setting, { "memory" }), cand("Memory", Kind::Setting) }, kNow);
        QCOMPARE(ranked.size(), 2);
        QCOMPARE(ranked.first().index, 1);
        QVERIFY(!ranked.first().viaKeyword);
        QVERIFY(ranked.at(1).viaKeyword);
        const double titleScore = matchText("memory", "Memory").score;
        const double keywordScore = matchText("memory", "memory").score * kKeywordFactor;
        QVERIFY(keywordScore < titleScore);
    }

    void recencyBreaksTies()
    {
        // Same title quality; the instance played more recently wins, whatever the list order is.
        const auto ranked = rank("wor", { cand("World A", Kind::Instance, {}, kNow - 20 * kDay), cand("World B", Kind::Instance, {}, kNow - kHour),
                                          cand("World C", Kind::Instance, {}, 0) },
                                 kNow);
        QCOMPARE(ranked.size(), 3);
        QCOMPARE(ranked.at(0).index, 1);
        QCOMPARE(ranked.at(1).index, 0);
        QCOMPARE(ranked.at(2).index, 2);
        QVERIFY(recencyBoost(kNow - kHour, kNow) > recencyBoost(kNow - 20 * kDay, kNow));
        QCOMPARE(recencyBoost(0, kNow), 0.0);
    }

    void deterministicTieBreak()
    {
        // No recency: the same kind sorts by title, a different kind by kind order, and the result never depends on input order.
        const QList<Candidate> a = { cand("Beta", Kind::Action), cand("Alpha", Kind::Action) };
        const QList<Candidate> b = { cand("Alpha", Kind::Action), cand("Beta", Kind::Action) };
        const auto ra = rank("", a, kNow);
        const auto rb = rank("", b, kNow);
        QCOMPARE(a.at(ra.at(0).index).title, QStringLiteral("Alpha"));
        QCOMPARE(b.at(rb.at(0).index).title, QStringLiteral("Alpha"));
        // Equal scores across kinds: the kind order decides (Navigation before Setting here: weights differ but never invert a clear win).
        const auto mixed = rank("go", { cand("Go to X", Kind::Navigation), cand("Go to X", Kind::Navigation) }, kNow);
        QCOMPARE(mixed.at(0).index, 0);
        QCOMPARE(mixed.at(1).index, 1);
    }

    void highlightPositions()
    {
        QCOMPARE(matchText("fab", "Fabulous Survival").positions, (QList<int>{ 0, 1, 2 }));
        // Word starts are preferred over earlier mid-word characters: "sj" -> the S of "Settings" and the J of "Java".
        QCOMPARE(matchText("sj", "Settings: Java").positions, (QList<int>{ 0, 10 }));
        const auto ranked = rank("sj", { cand("Settings: Java", Kind::Setting) }, kNow);
        QCOMPARE(ranked.first().positions, (QList<int>{ 0, 10 }));
    }

    void emptyQueryMatchesEverythingInKindOrder()
    {
        const QList<Candidate> list = { cand("Theme: A", Kind::Appearance), cand("Go to Home", Kind::Navigation),
                                        cand("Old", Kind::Instance, {}, kNow - 30 * kDay), cand("Fresh", Kind::Instance, {}, kNow - kHour),
                                        cand("New instance", Kind::Action) };
        const auto ranked = rank("", list, kNow);
        QCOMPARE(ranked.size(), list.size());
        QCOMPARE(list.at(ranked.at(0).index).title, QStringLiteral("Fresh"));
        QCOMPARE(list.at(ranked.at(1).index).title, QStringLiteral("Old"));
        QCOMPARE(list.at(ranked.at(2).index).title, QStringLiteral("Go to Home"));
        QCOMPARE(list.at(ranked.at(3).index).title, QStringLiteral("New instance"));
        QCOMPARE(list.at(ranked.at(4).index).title, QStringLiteral("Theme: A"));
        for (const auto& r : ranked)
            QVERIFY(r.positions.isEmpty());
        QVERIFY(matchText("", "anything").matched);
        QVERIFY(matchText("   ", "anything").positions.isEmpty());
    }

    void noMatch()
    {
        QVERIFY(!matchText("zzz", "Fabulous Survival").matched);
        QVERIFY(!matchText("abcd", "abc").matched);
        QVERIFY(rank("zzz", { cand("Fabulous Survival"), cand("Settings: Java", Kind::Setting, { "ram" }) }, kNow).isEmpty());
        QVERIFY(!matchText("ba", "ab").matched);  // order matters
    }
};

QTEST_APPLESS_MAIN(CommandMatcherTest)
#include "CommandMatcher_test.moc"
