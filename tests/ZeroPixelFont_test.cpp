// SPDX-License-Identifier: GPL-3.0-only
// Validates the generated "ZERO Pixel" TrueType font: file integrity, registration with Qt, glyph coverage,
// metrics and crisp rendering. Also renders a sample to build/zero-v2-pixelfont.png for visual review.
#include <QApplication>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QTest>

#include "ui/zero/ZeroPixelFont.h"

namespace {

QFont pixelFont(int pixelSize)
{
    QFont f(Zero::pixelFamily());
    f.setPixelSize(pixelSize);
    f.setHintingPreference(QFont::PreferNoHinting);
    f.setKerning(false);
    f.setStyleStrategy(QFont::NoAntialias);
    return f;
}

QImage render(const QString& text, int pixelSize, const QSize& size)
{
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setFont(pixelFont(pixelSize));
    p.setPen(Qt::white);
    p.drawText(QPoint(2, 2 + QFontMetrics(p.font()).ascent()), text);
    return img;
}

int inkPixels(const QImage& img)
{
    int n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(img.pixel(x, y)) > 0)
                ++n;
    return n;
}

}  // namespace

class ZeroPixelFontTest : public QObject {
    Q_OBJECT

   private slots:
    void fileIsAValidSfnt()
    {
        const QByteArray d = Zero::pixelFontData();
        QVERIFY(d.size() > 1000);
        auto u32 = [&](int off) {
            return (quint32(quint8(d[off])) << 24) | (quint32(quint8(d[off + 1])) << 16) | (quint32(quint8(d[off + 2])) << 8) |
                   quint32(quint8(d[off + 3]));
        };
        QCOMPARE(u32(0), 0x00010000u);
        QCOMPARE(d.size() % 4, 0);
        // The whole file sums to the magic number once head.checkSumAdjustment is in place.
        quint32 sum = 0;
        for (int i = 0; i < d.size(); i += 4)
            sum += u32(i);
        QCOMPARE(sum, 0xB1B0AFBAu);
        // Table directory is sorted by tag and every table lies inside the file.
        const int n = (quint8(d[4]) << 8) | quint8(d[5]);
        QCOMPARE(n, 10);
        QByteArray prev;
        for (int i = 0; i < n; ++i) {
            const int e = 12 + 16 * i;
            const QByteArray tag = d.mid(e, 4);
            QVERIFY2(prev < tag, qPrintable(QString::fromLatin1(tag)));
            prev = tag;
            QVERIFY(qint64(u32(e + 8)) + u32(e + 12) <= d.size());
        }
    }

    void registersAndIsIdempotent()
    {
        QVERIFY(Zero::registerPixelFont());
        QVERIFY(Zero::registerPixelFont());
        QCOMPARE(Zero::pixelFamily(), QString("ZERO Pixel"));
        QVERIFY(QFontDatabase::families().contains(Zero::pixelFamily()));
    }

    void everyPrintableAsciiCharIsInTheFont()
    {
        QVERIFY(Zero::registerPixelFont());
        const QFontMetrics fm(pixelFont(10));
        for (int c = 32; c <= 126; ++c) {
            QVERIFY2(fm.inFont(QChar(c)), qPrintable(QString("missing U+%1 '%2'").arg(c, 4, 16, QChar('0')).arg(QChar(c))));
            if (c == 32)
                continue;
            // The glyph is more than a box: it draws something, and it is not the .notdef frame.
            QVERIFY2(inkPixels(render(QString(QChar(c)), 10, QSize(16, 16))) > 0, qPrintable(QString("blank '%1'").arg(QChar(c))));
        }
    }

    void metricsFollowTheGrid()
    {
        QVERIFY(Zero::registerPixelFont());
        const QFontMetrics fm10(pixelFont(10));
        QCOMPARE(fm10.horizontalAdvance(QLatin1Char('A')), 6);  // 5 px glyph + 1 px spacing
        QCOMPARE(fm10.horizontalAdvance(QLatin1Char('I')), 4);
        QCOMPARE(fm10.horizontalAdvance(QStringLiteral("ZERO")), 24);
        QCOMPARE(fm10.height(), 10);  // 8 px ascent + 2 px descent
        const QFontMetrics fm20(pixelFont(20));
        QCOMPARE(fm20.horizontalAdvance(QLatin1Char('A')), 12);
        QCOMPARE(fm20.horizontalAdvance(QStringLiteral("ZERO")), 48);
        QCOMPARE(QFontMetrics(pixelFont(30)).horizontalAdvance(QLatin1Char('A')), 18);
    }

    void rendersAtWholePixelScales()
    {
        QVERIFY(Zero::registerPixelFont());
        const QImage zero = render(QStringLiteral("ZERO"), 10, QSize(40, 16));
        QVERIFY(inkPixels(zero) > 20);

        // At 20 px every glyph pixel is a 2x2 block: no half-covered pixels.
        for (int size : { 10, 20, 30 }) {
            const QImage img = render(QStringLiteral("Zero Launcher 0123 ABC abc"), size, QSize(size * 20, size * 3));
            QVERIFY(inkPixels(img) > 0);
            int soft = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x) {
                    const int a = qAlpha(img.pixel(x, y));
                    if (a != 0 && a != 255)
                        ++soft;
                }
            QVERIFY2(soft == 0, qPrintable(QString("%1 px: %2 anti-aliased edge pixels").arg(size).arg(soft)));
        }
    }

    void rendersSample()
    {
        QVERIFY(Zero::registerPixelFont());
        QImage img(560, 190, QImage::Format_ARGB32_Premultiplied);
        img.fill(QColor(0x16, 0x17, 0x1A));
        QPainter p(&img);
        p.setPen(QColor(0xEF, 0xEB, 0xE0));
        int y = 10;
        for (int size : { 10, 20, 30 }) {
            p.setFont(pixelFont(size));
            y += QFontMetrics(p.font()).ascent();
            p.drawText(QPoint(12, y), QStringLiteral("ZERO Launcher 0123 ABC abc"));
            y += QFontMetrics(p.font()).descent() + 10;
        }
        p.setFont(pixelFont(10));
        p.drawText(QPoint(12, y + 8), QStringLiteral("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~ gjpqy"));
        p.end();
        QVERIFY(img.save(QDir(QCoreApplication::applicationDirPath()).filePath("zero-v2-pixelfont.png")));
    }
};

QTEST_MAIN(ZeroPixelFontTest)

#include "ZeroPixelFont_test.moc"
