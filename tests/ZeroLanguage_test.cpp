// SPDX-License-Identifier: GPL-3.0-only
// Languages of the launcher: which one starts, which Prism file goes with it, and that a ZERO translation is found through the same
// context the code looks it up with (a class of a namespace, and a string of a file that used to have its own free tr() helper).
#include <QCoreApplication>
#include <QDateTime>
#include <QTest>
#include <QTranslator>

#include "ui/zero/ZeroFormat.h"
#include "ui/zero/instance/ModsPage.h"
#include "zero/ZeroLanguage.h"

namespace {

// zero_test_es.qm, made with `lrelease -nounfinished` from:
//   Zero::Format             "Never" -> "Nunca", "Just now" -> "Ahora mismo", "Yesterday" -> unfinished "Ayer?" (left out of the file)
//   Zero::ModsUi::ModsPage   "Check for updates" -> "Buscar actualizaciones"
const uchar kTestQm[] = {
    0x3c, 0xb8, 0x64, 0x18, 0xca, 0xef, 0x9c, 0x95, 0xcd, 0x21, 0x1c, 0xbf, 0x60, 0xa1, 0xbd, 0xdd,
    0xa7, 0x00, 0x00, 0x00, 0x02, 0x65, 0x73, 0x42, 0x00, 0x00, 0x00, 0x18, 0x00, 0x54, 0xcc, 0xc2,
    0x00, 0x00, 0x00, 0x3f, 0x01, 0x4c, 0x05, 0x23, 0x00, 0x00, 0x00, 0x6f, 0x0c, 0xa6, 0x72, 0x77,
    0x00, 0x00, 0x00, 0x00, 0x69, 0x00, 0x00, 0x00, 0xd7, 0x03, 0x00, 0x00, 0x00, 0x16, 0x00, 0x41,
    0x00, 0x68, 0x00, 0x6f, 0x00, 0x72, 0x00, 0x61, 0x00, 0x20, 0x00, 0x6d, 0x00, 0x69, 0x00, 0x73,
    0x00, 0x6d, 0x00, 0x6f, 0x08, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x08, 0x4a, 0x75,
    0x73, 0x74, 0x20, 0x6e, 0x6f, 0x77, 0x07, 0x00, 0x00, 0x00, 0x0c, 0x5a, 0x65, 0x72, 0x6f, 0x3a,
    0x3a, 0x46, 0x6f, 0x72, 0x6d, 0x61, 0x74, 0x01, 0x03, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x4e, 0x00,
    0x75, 0x00, 0x6e, 0x00, 0x63, 0x00, 0x61, 0x08, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x05, 0x4e, 0x65, 0x76, 0x65, 0x72, 0x07, 0x00, 0x00, 0x00, 0x0c, 0x5a, 0x65, 0x72, 0x6f, 0x3a,
    0x3a, 0x46, 0x6f, 0x72, 0x6d, 0x61, 0x74, 0x01, 0x03, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x42, 0x00,
    0x75, 0x00, 0x73, 0x00, 0x63, 0x00, 0x61, 0x00, 0x72, 0x00, 0x20, 0x00, 0x61, 0x00, 0x63, 0x00,
    0x74, 0x00, 0x75, 0x00, 0x61, 0x00, 0x6c, 0x00, 0x69, 0x00, 0x7a, 0x00, 0x61, 0x00, 0x63, 0x00,
    0x69, 0x00, 0x6f, 0x00, 0x6e, 0x00, 0x65, 0x00, 0x73, 0x08, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00,
    0x00, 0x00, 0x11, 0x43, 0x68, 0x65, 0x63, 0x6b, 0x20, 0x66, 0x6f, 0x72, 0x20, 0x75, 0x70, 0x64,
    0x61, 0x74, 0x65, 0x73, 0x07, 0x00, 0x00, 0x00, 0x16, 0x5a, 0x65, 0x72, 0x6f, 0x3a, 0x3a, 0x4d,
    0x6f, 0x64, 0x73, 0x55, 0x69, 0x3a, 0x3a, 0x4d, 0x6f, 0x64, 0x73, 0x50, 0x61, 0x67, 0x65, 0x01,
    0x88, 0x00, 0x00, 0x00, 0x02, 0x01, 0x01,
};

QLocale spanish(QLocale::Territory territory)
{
    return QLocale(QLocale::Spanish, territory);
}

QLocale english()
{
    return QLocale(QLocale::English, QLocale::UnitedStates);
}

}  // namespace

class ZeroLanguageTest : public QObject {
    Q_OBJECT

   private slots:
    void initialCode_data()
    {
        QTest::addColumn<QString>("saved");
        QTest::addColumn<QLocale>("system");
        QTest::addColumn<QString>("expected");

        // nothing saved: follow Windows
        QTest::newRow("empty, Spanish (Argentina)") << QString() << spanish(QLocale::Argentina) << "es";
        QTest::newRow("empty, Spanish (Spain)") << QString() << spanish(QLocale::Spain) << "es";
        QTest::newRow("empty, Spanish (Mexico)") << QString() << spanish(QLocale::Mexico) << "es";
        QTest::newRow("empty, English") << QString() << english() << "en_US";
        QTest::newRow("empty, German") << QString() << QLocale(QLocale::German, QLocale::Germany) << "en_US";
        QTest::newRow("blank, Spanish") << QStringLiteral("  ") << spanish(QLocale::Uruguay) << "es";
        // a saved value wins over Windows
        QTest::newRow("saved English, Spanish system") << "en_US" << spanish(QLocale::Argentina) << "en_US";
        QTest::newRow("saved Spanish, English system") << "es" << english() << "es";
        // what Prism may have saved is read as the nearest of the two languages
        QTest::newRow("Prism es_ES") << "es_ES" << english() << "es";
        QTest::newRow("Prism es_UY") << "es_UY" << english() << "es";
        QTest::newRow("es-MX") << "es-MX" << english() << "es";
        QTest::newRow("Prism de_DE, Spanish system") << "de_DE" << spanish(QLocale::Spain) << "en_US";
        QTest::newRow("Prism en_GB") << "en_GB" << spanish(QLocale::Spain) << "en_US";
        QTest::newRow("a code that only starts with es") << "esperanto" << english() << "en_US";
    }
    void initialCode()
    {
        QFETCH(QString, saved);
        QFETCH(QLocale, system);
        QFETCH(QString, expected);
        QCOMPARE(Zero::Language::initialCode(saved, system), expected);
    }

    void choicesAreEnglishAndSpanishInTheirOwnLanguage()
    {
        const auto choices = Zero::Language::choices();
        QCOMPARE(choices.size(), 2);
        QCOMPARE(choices[0].code, QString("en_US"));
        QCOMPARE(choices[0].name, QString("English"));
        QCOMPARE(choices[1].code, QString("es"));
        QCOMPARE(choices[1].name, QString::fromUtf8("Espa\xC3\xB1ol"));
        // every code the picker saves is a code the start reads back unchanged
        for (const auto& c : choices)
            QCOMPARE(Zero::Language::initialCode(c.code, english()), c.code);
    }

    void localeFollowsTheLanguage()
    {
        QCOMPARE(Zero::Language::localeFor("en_US", spanish(QLocale::Argentina)).language(), QLocale::English);
        QCOMPARE(Zero::Language::localeFor("es", english()).language(), QLocale::Spanish);
        // an already Spanish system keeps its region (the dates and numbers of Argentina stay Argentine)
        QCOMPARE(Zero::Language::localeFor("es", spanish(QLocale::Argentina)).territory(), QLocale::Argentina);
    }

    void prismKey_data()
    {
        QTest::addColumn<QString>("code");
        QTest::addColumn<QLocale>("system");
        QTest::addColumn<QStringList>("available");
        QTest::addColumn<QString>("expected");

        const QStringList both = { "en_US", "es_ES", "es_UY", "de_DE" };
        QTest::newRow("English needs none") << "en_US" << spanish(QLocale::Spain) << both << QString();
        QTest::newRow("Spain") << "es" << spanish(QLocale::Spain) << both << "es_ES";
        QTest::newRow("Uruguay, exact") << "es" << spanish(QLocale::Uruguay) << both << "es_UY";
        QTest::newRow("Argentina: Latin American") << "es" << spanish(QLocale::Argentina) << both << "es_UY";
        QTest::newRow("English system: Latin American") << "es" << english() << both << "es_UY";
        QTest::newRow("only Spain's offered") << "es" << spanish(QLocale::Argentina) << QStringList{ "en_US", "es_ES" } << "es_ES";
        QTest::newRow("no Spanish offered") << "es" << spanish(QLocale::Spain) << QStringList{ "en_US", "de_DE" } << QString();
        QTest::newRow("nothing known yet") << "es" << spanish(QLocale::Spain) << QStringList{ "en_US" } << QString();
    }
    void prismKey()
    {
        QFETCH(QString, code);
        QFETCH(QLocale, system);
        QFETCH(QStringList, available);
        QFETCH(QString, expected);
        QCOMPARE(Zero::Language::prismKey(code, system, available), expected);
    }

    void englishHasNoTranslator() { QVERIFY(!Zero::Language::loadZeroTranslator("en_US")); }

    void translationIsFoundThroughTheContextOfTheCode()
    {
        QCOMPARE(Zero::Format::relativeTime(0), QString("Never"));
        QCOMPARE(Zero::ModsUi::ModsPage::tr("Check for updates"), QString("Check for updates"));

        QTranslator translator;
        QVERIFY(translator.load(kTestQm, int(sizeof(kTestQm))));
        QVERIFY(QCoreApplication::installTranslator(&translator));

        // a string of a file that had its own free tr() helper (context "Zero::Format", written into the call)
        QCOMPARE(Zero::Format::relativeTime(0), QString("Nunca"));
        QCOMPARE(Zero::Format::relativeTime(QDateTime::currentMSecsSinceEpoch()), QString("Ahora mismo"));
        // an entry that is not finished is not in the file: the English text stays
        QCOMPARE(QCoreApplication::translate("Zero::Format", "Yesterday"), QString("Yesterday"));

        // a class of a namespace: tr() uses the class name with its namespaces, which is the context lupdate writes into the .ts
        QCOMPARE(QString::fromLatin1(Zero::ModsUi::ModsPage::staticMetaObject.className()), QString("Zero::ModsUi::ModsPage"));
        QCOMPARE(Zero::ModsUi::ModsPage::tr("Check for updates"), QString("Buscar actualizaciones"));
        // the same text in another context is not touched
        QCOMPARE(QCoreApplication::translate("Zero::Format", "Check for updates"), QString("Check for updates"));

        QCoreApplication::removeTranslator(&translator);
        QCOMPARE(Zero::Format::relativeTime(0), QString("Never"));
    }

    void theTranslatorInstalledLastWins()
    {
        // Prism's file is installed first and ZERO's last: the last one installed is asked first
        QTranslator prism;
        QTranslator zero;
        QVERIFY(zero.load(kTestQm, int(sizeof(kTestQm))));
        QCoreApplication::installTranslator(&prism);  // empty: finds nothing, the next one is asked
        QCoreApplication::installTranslator(&zero);
        QCOMPARE(Zero::Format::relativeTime(0), QString("Nunca"));
        QCoreApplication::removeTranslator(&zero);
        QCoreApplication::removeTranslator(&prism);
    }
};

QTEST_GUILESS_MAIN(ZeroLanguageTest)
#include "ZeroLanguage_test.moc"
