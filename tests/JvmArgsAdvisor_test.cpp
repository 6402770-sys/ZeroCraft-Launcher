// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the recommended Java arguments of "Boost performance" (JvmArgsAdvisor): the four states, the 12 GB tier, the per-Java-major
// filtering, no -Xmx / -Xms / AlwaysPreTouch, idempotence (order and spacing do not matter), and apply + restore on real instance settings.
#include <QTemporaryDir>
#include <QTest>

#include "settings/INISettingsObject.h"
#include "zero/perf/JvmArgsAdvisor.h"

using namespace Zero::Perf;

namespace {

QString S(const char* text)
{
    return QString::fromUtf8(text);
}

constexpr int kGb = 1024;

bool has(const JvmArgsAdvice& advice, const QString& flag)
{
    return advice.args().contains(flag);
}

}  // namespace

class JvmArgsAdvisorTest : public QObject {
    Q_OBJECT
   private slots:
    void emptyArgumentsRecommendTheSet()
    {
        for (const QString& current : { QString(), S("   "), S(" \t ") }) {
            const auto advice = adviseJvmArgs(21, 6 * kGb, 8, current);
            QCOMPARE(advice.state, JvmArgsState::NoCustomArgs);
            QVERIFY(advice.offered());
            QCOMPARE(advice.flags.size(), 17);
            QVERIFY(has(advice, S("-XX:+UseG1GC")));
            QVERIFY(has(advice, S("-XX:G1NewSizePercent=30")));
            QVERIFY(has(advice, S("-XX:G1MaxNewSizePercent=40")));
            QVERIFY(has(advice, S("-XX:G1HeapRegionSize=8M")));
            QVERIFY(has(advice, S("-XX:G1ReservePercent=20")));
            QVERIFY(has(advice, S("-XX:InitiatingHeapOccupancyPercent=15")));
        }
    }

    void everyFlagHasAnExplanationAndTheStringIsTheJoinedList()
    {
        const auto advice = adviseJvmArgs(21, 4 * kGb, 8, QString());
        for (const auto& f : advice.flags) {
            QVERIFY(f.flag.startsWith(S("-XX:")));
            QVERIFY2(f.why.size() > 15 && f.why.endsWith(QLatin1Char('.')), qPrintable(f.flag));
            QVERIFY(!f.why.contains(QLatin1Char('\n')));
        }
        QCOMPARE(advice.argsString(), advice.args().join(QLatin1Char(' ')));
        QVERIFY(!advice.argsString().contains(S("  ")));
    }

    void experimentalIsUnlockedBeforeTheFlagsThatNeedIt()
    {
        const auto args = adviseJvmArgs(17, 4 * kGb, 8, QString()).args();
        const int unlock = args.indexOf(S("-XX:+UnlockExperimentalVMOptions"));
        QVERIFY(unlock >= 0);
        for (const QString& experimental : { S("-XX:G1NewSizePercent=30"), S("-XX:G1MaxNewSizePercent=40"), S("-XX:G1MixedGCLiveThresholdPercent=90") }) {
            QVERIFY(args.indexOf(experimental) > unlock);
        }
    }

    void twelveGigabytesOrMoreUsesTheRoomierTier()
    {
        const auto small = adviseJvmArgs(21, 12 * kGb - 1, 8, QString());
        QVERIFY(has(small, S("-XX:G1NewSizePercent=30")));
        const auto big = adviseJvmArgs(21, 12 * kGb, 8, QString());
        QVERIFY(has(big, S("-XX:G1NewSizePercent=40")));
        QVERIFY(has(big, S("-XX:G1MaxNewSizePercent=50")));
        QVERIFY(has(big, S("-XX:G1HeapRegionSize=16M")));
        QVERIFY(has(big, S("-XX:G1ReservePercent=15")));
        QVERIFY(has(big, S("-XX:InitiatingHeapOccupancyPercent=20")));
        QVERIFY(!has(big, S("-XX:G1HeapRegionSize=8M")));
        QCOMPARE(big.flags.size(), small.flags.size());
        QVERIFY(has(adviseJvmArgs(21, 32 * kGb, 8, QString()), S("-XX:G1HeapRegionSize=16M")));
        QVERIFY(has(adviseJvmArgs(21, 0, 8, QString()), S("-XX:G1HeapRegionSize=8M")));  // unknown memory: the small tier
    }

    void neverMemoryNorPreTouch()
    {
        for (int major : { 8, 11, 17, 21, 25, 30 }) {
            for (int mb : { 2 * kGb, 16 * kGb }) {
                for (const QString& flag : adviseJvmArgs(major, mb, 8, QString()).args()) {
                    QVERIFY2(!flag.startsWith(S("-Xmx")) && !flag.startsWith(S("-Xms")) && !flag.startsWith(S("-Xss")), qPrintable(flag));
                    QVERIFY2(!flag.contains(S("AlwaysPreTouch")), qPrintable(flag));
                }
            }
        }
    }

    void perJavaVersionFiltering()
    {
        const QString rset = S("-XX:G1RSetUpdatingPauseTimePercent=5");
        for (int major : { 8, 17, 21, 25 }) {
            const auto advice = adviseJvmArgs(major, 4 * kGb, 8, QString());
            QCOMPARE(advice.state, JvmArgsState::NoCustomArgs);
            QCOMPARE(advice.flags.size(), 17);  // verified on 8 (flag history) and on 17 / 21 / 25 (a JVM was started with the set)
            QVERIFY2(has(advice, rset), qPrintable(QString::number(major)));
        }
        // Beyond the newest verified Java the flag with a history of removal is dropped; the rest stays.
        const auto future = adviseJvmArgs(kLastVerifiedMajor + 1, 4 * kGb, 8, QString());
        QCOMPARE(future.state, JvmArgsState::NoCustomArgs);
        QVERIFY(!has(future, rset));
        QCOMPARE(future.flags.size(), 16);
        QVERIFY(has(future, S("-XX:+UseG1GC")));
    }

    void tooOldUnknownOrSingleCoreIsNotSupported()
    {
        for (const auto advice : { adviseJvmArgs(0, 4 * kGb, 8, QString()), adviseJvmArgs(7, 4 * kGb, 8, QString()),
                                   adviseJvmArgs(21, 4 * kGb, 1, QString()), adviseJvmArgs(21, 4 * kGb, 1, S("-Dfoo=bar")) }) {
            QCOMPARE(advice.state, JvmArgsState::NotSupported);
            QVERIFY(!advice.offered());
            QVERIFY(advice.flags.isEmpty());
        }
        QCOMPARE(adviseJvmArgs(21, 4 * kGb, 0, QString()).state, JvmArgsState::NoCustomArgs);  // unknown cores: fine
        QCOMPARE(adviseJvmArgs(21, 4 * kGb, 2, QString()).state, JvmArgsState::NoCustomArgs);
    }

    void ownArgumentsAreKeptUnlessReplaced()
    {
        const auto advice = adviseJvmArgs(21, 4 * kGb, 8, S("  -XX:+UseZGC   -Dfoo=bar \n"));
        QCOMPARE(advice.state, JvmArgsState::HasCustomArgs);
        QVERIFY(advice.offered());
        QCOMPARE(advice.current, S("-XX:+UseZGC -Dfoo=bar"));
        QVERIFY(!advice.flags.isEmpty());
        // Some of the flags only: still the user's own.
        QCOMPARE(adviseJvmArgs(21, 4 * kGb, 8, S("-XX:+UseG1GC -XX:MaxGCPauseMillis=200")).state, JvmArgsState::HasCustomArgs);
    }

    void theRecommendationItselfIsAlreadyRecommended()
    {
        for (int major : { 8, 17, 21, 25, 26 }) {
            for (int mb : { 3 * kGb, 16 * kGb }) {
                const auto first = adviseJvmArgs(major, mb, 8, QString());
                const auto again = adviseJvmArgs(major, mb, 8, first.argsString());
                QCOMPARE(again.state, JvmArgsState::AlreadyRecommended);
                QVERIFY(!again.offered());
            }
        }
    }

    void idempotentWhateverTheOrderSpacingOrCase()
    {
        const auto base = adviseJvmArgs(21, 4 * kGb, 8, QString());
        QStringList shuffled = base.args();
        std::reverse(shuffled.begin(), shuffled.end());
        QCOMPARE(adviseJvmArgs(21, 4 * kGb, 8, shuffled.join(S("   "))).state, JvmArgsState::AlreadyRecommended);
        QCOMPARE(adviseJvmArgs(21, 4 * kGb, 8, shuffled.join(S("\n"))).state, JvmArgsState::AlreadyRecommended);
        QCOMPARE(adviseJvmArgs(21, 4 * kGb, 8, S("  ") + base.argsString() + S("\t ")).state, JvmArgsState::AlreadyRecommended);
        QCOMPARE(adviseJvmArgs(21, 4 * kGb, 8, base.argsString().replace(S("8M"), S("8m"))).state, JvmArgsState::AlreadyRecommended);
        // Extra arguments of the user's do not undo it.
        QCOMPARE(adviseJvmArgs(21, 4 * kGb, 8, base.argsString() + S(" -Dfoo=bar")).state, JvmArgsState::AlreadyRecommended);
    }

    void eitherTierCountsAsTunedWhateverTheHeap()
    {
        const QString big = adviseJvmArgs(21, 16 * kGb, 8, QString()).argsString();
        QCOMPARE(adviseJvmArgs(21, 4 * kGb, 8, big).state, JvmArgsState::AlreadyRecommended);
        const QString small = adviseJvmArgs(21, 4 * kGb, 8, QString()).argsString();
        QCOMPARE(adviseJvmArgs(21, 16 * kGb, 8, small).state, JvmArgsState::AlreadyRecommended);
    }

    void aSetFromAnotherJavaStillCountsForTheNewerOne()
    {
        // The Java 21 set (with the RSet flag) satisfies Java 26's set (without it); the Java 26 set is missing a flag Java 21 wants.
        const QString twentyOne = adviseJvmArgs(21, 4 * kGb, 8, QString()).argsString();
        QCOMPARE(adviseJvmArgs(26, 4 * kGb, 8, twentyOne).state, JvmArgsState::AlreadyRecommended);
        const QString twentySix = adviseJvmArgs(26, 4 * kGb, 8, QString()).argsString();
        QCOMPARE(adviseJvmArgs(21, 4 * kGb, 8, twentySix).state, JvmArgsState::HasCustomArgs);
    }

    void splitsAtAnyWhitespace()
    {
        QCOMPARE(splitJvmArgs(S("  a  b\tc\nd ")), (QStringList{ S("a"), S("b"), S("c"), S("d") }));
        QVERIFY(splitJvmArgs(QString()).isEmpty());
    }

    void summaryText()
    {
        QCOMPARE(javaArgsTunedText(), S("Java arguments tuned"));
    }

    // ---- apply / restore on an instance's settings ----

    void applyWritesTheOverrideAndRestoreBringsBackTheExactState_data()
    {
        QTest::addColumn<bool>("overridden");
        QTest::addColumn<QString>("own");
        QTest::addColumn<QString>("global");
        QTest::newRow("no override, global args") << false << QString() << S("-Dglobal=1");
        QTest::newRow("no override, hidden own value") << false << S("-Dhidden=1") << S("-Dglobal=1");
        QTest::newRow("override with own args") << true << S("-Xshare:auto  -Dmine=1") << S("-Dglobal=1");
        QTest::newRow("override with empty args") << true << QString() << S("-Dglobal=1");
    }
    void applyWritesTheOverrideAndRestoreBringsBackTheExactState()
    {
        QFETCH(bool, overridden);
        QFETCH(QString, own);
        QFETCH(QString, global);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        INISettingsObject globalSettings(dir.filePath(S("global.cfg")));
        auto globalArgs = globalSettings.registerSetting(S("JvmArgs"), S(""));
        globalSettings.set(S("JvmArgs"), global);

        QFile cfg(dir.filePath(S("instance.cfg")));
        QVERIFY(cfg.open(QIODevice::WriteOnly));
        cfg.write("[General]\n");
        cfg.close();
        INISettingsObject instance(dir.filePath(S("instance.cfg")));
        auto gate = instance.registerSetting(S("OverrideJavaArgs"), false);
        instance.registerOverride(globalSettings.getSetting(S("JvmArgs")), gate);
        instance.set(S("OverrideJavaArgs"), overridden);
        if (!own.isEmpty() || overridden) {
            instance.set(S("JvmArgs"), own);
        }
        const QString effectiveBefore = instance.get(S("JvmArgs")).toString();

        const QString recommended = adviseJvmArgs(21, 4 * kGb, 8, QString()).argsString();
        const JvmArgsPrevious previous = applyJvmArgs(&instance, recommended);

        QCOMPARE(previous.overridden, overridden);
        QCOMPARE(previous.args, overridden ? own : QString());
        QVERIFY(instance.get(S("OverrideJavaArgs")).toBool());
        QCOMPARE(instance.get(S("JvmArgs")).toString(), recommended);
        QCOMPARE(globalSettings.get(S("JvmArgs")).toString(), global);  // the global one is never touched

        restoreJvmArgs(&instance, previous);

        QCOMPARE(instance.get(S("OverrideJavaArgs")).toBool(), overridden);
        QCOMPARE(instance.get(S("JvmArgs")).toString(), effectiveBefore);
        QCOMPARE(globalSettings.get(S("JvmArgs")).toString(), global);
    }
};

QTEST_GUILESS_MAIN(JvmArgsAdvisorTest)

#include "JvmArgsAdvisor_test.moc"
