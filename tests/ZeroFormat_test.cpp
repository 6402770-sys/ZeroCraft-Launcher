// SPDX-License-Identifier: GPL-3.0-only
// Relative time and playtime strings used by the ZERO Home and Library pages.
#include <QTest>

#include "ui/zero/ZeroFormat.h"

class ZeroFormatTest : public QObject {
    Q_OBJECT

    // 2026-03-10 14:30 local time
    const QDateTime m_now{ QDate(2026, 3, 10), QTime(14, 30, 0) };

    qint64 ago(qint64 secs) const { return m_now.addSecs(-secs).toMSecsSinceEpoch(); }
    qint64 daysAgo(int days) const { return m_now.addDays(-days).toMSecsSinceEpoch(); }

   private slots:
    void relativeTime_data()
    {
        QTest::addColumn<qint64>("ms");
        QTest::addColumn<QString>("expected");

        QTest::newRow("never zero") << qint64(0) << "Never";
        QTest::newRow("never negative") << qint64(-5) << "Never";
        QTest::newRow("just now") << ago(0) << "Just now";
        QTest::newRow("59 seconds") << ago(59) << "Just now";
        QTest::newRow("future") << ago(-3600) << "Just now";
        QTest::newRow("1 minute") << ago(60) << "1 minute ago";
        QTest::newRow("5 minutes") << ago(5 * 60 + 20) << "5 minutes ago";
        QTest::newRow("59 minutes") << ago(59 * 60) << "59 minutes ago";
        QTest::newRow("1 hour") << ago(3600) << "1 hour ago";
        QTest::newRow("2 hours") << ago(2 * 3600 + 100) << "2 hours ago";
        QTest::newRow("14 hours same day") << ago(14 * 3600) << "14 hours ago";
        // 15 h ago is 23:30 on the previous calendar day
        QTest::newRow("yesterday late evening") << ago(15 * 3600) << "Yesterday";
        QTest::newRow("yesterday") << ago(24 * 3600) << "Yesterday";
        QTest::newRow("2 days") << daysAgo(2) << "2 days ago";
        QTest::newRow("6 days") << daysAgo(6) << "6 days ago";
        QTest::newRow("1 week") << daysAgo(7) << "1 week ago";
        QTest::newRow("3 weeks") << daysAgo(21) << "3 weeks ago";
        QTest::newRow("1 month") << daysAgo(30) << "1 month ago";
        QTest::newRow("6 months") << daysAgo(190) << "6 months ago";
        QTest::newRow("1 year") << daysAgo(365) << "1 year ago";
        QTest::newRow("3 years") << daysAgo(1200) << "3 years ago";
    }

    void relativeTime()
    {
        QFETCH(qint64, ms);
        QFETCH(QString, expected);
        QCOMPARE(Zero::Format::relativeTime(ms, m_now), expected);
    }

    void playtime_data()
    {
        QTest::addColumn<qint64>("seconds");
        QTest::addColumn<QString>("expected");

        QTest::newRow("zero") << qint64(0) << "0 min";
        QTest::newRow("negative") << qint64(-10) << "0 min";
        QTest::newRow("seconds") << qint64(45) << "< 1 min";
        QTest::newRow("one minute") << qint64(60) << "1 min";
        QTest::newRow("12 minutes") << qint64(12 * 60 + 59) << "12 min";
        QTest::newRow("59 minutes") << qint64(59 * 60) << "59 min";
        QTest::newRow("exact hour") << qint64(3600) << "1 h";
        QTest::newRow("hours minutes") << qint64(34 * 3600 + 12 * 60) << "34 h 12 min";
        QTest::newRow("many hours") << qint64(1234 * 3600 + 5 * 60) << "1234 h 5 min";
    }

    void playtime()
    {
        QFETCH(qint64, seconds);
        QFETCH(QString, expected);
        QCOMPARE(Zero::Format::playtime(seconds), expected);
    }

    void bytes_data()
    {
        QTest::addColumn<qint64>("size");
        QTest::addColumn<QString>("expected");

        QTest::newRow("negative") << qint64(-5) << "0 B";
        QTest::newRow("zero") << qint64(0) << "0 B";
        QTest::newRow("bytes") << qint64(512) << "512 B";
        QTest::newRow("one kb") << qint64(1024) << "1 KB";
        QTest::newRow("kb") << qint64(812 * 1024) << "812 KB";
        QTest::newRow("kb rounds up to mb") << qint64(1024 * 1024 - 100) << "1.0 MB";
        QTest::newRow("mb") << qint64(1024 * 1024 + 1024 * 1024 / 5) << "1.2 MB";
        QTest::newRow("ten mb") << qint64(12) * 1024 * 1024 + 300 * 1024 << "12.3 MB";
        QTest::newRow("gb") << qint64(1.2 * 1024 * 1024 * 1024) << "1.2 GB";
        QTest::newRow("tb") << qint64(3) * 1024 * 1024 * 1024 * 1024 << "3.0 TB";
    }

    void bytes()
    {
        QFETCH(qint64, size);
        QFETCH(QString, expected);
        QCOMPARE(Zero::Format::bytes(size), expected);
    }
};

QTEST_GUILESS_MAIN(ZeroFormatTest)

#include "ZeroFormat_test.moc"
