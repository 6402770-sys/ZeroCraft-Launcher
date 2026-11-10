// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroPixelFont.h"

#include <QDataStream>
#include <QFontDatabase>
#include <QMap>
#include <QPoint>
#include <QtDebug>
#include <algorithm>
#include <iterator>
#include <vector>

namespace Zero {

namespace {

// Glyph grid: 5 columns, 9 rows. Rows 0-6 are the cap height (7 px above the baseline), rows 7-8 are descenders.
// One font pixel is kUnit font units (em = 1000 = 10 pixels).
constexpr int kCols = 5;
constexpr int kRows = 9;
constexpr int kCapRows = 7;
constexpr int kUnit = 100;
constexpr int kEm = 1000;
constexpr int kAscent = 8 * kUnit;
constexpr int kDescent = 2 * kUnit;
constexpr int kSpaceAdvance = 4 * kUnit;
constexpr int kNotdefAdvance = 6 * kUnit;

struct GlyphDef {
    char ch;
    const char* rows;  // up to 9 rows separated by '/', '#' = ink; missing rows are empty
};

// Original design: a geometric 5x7 face, lowercase with a 5 px x-height and 2 px descenders.
const GlyphDef kGlyphs[] = {
    { ' ', "" },
    { '!', "#/#/#/#/#/./#" },
    { '"', "#.#/#.#" },
    { '#', ".#.#./.#.#./#####/.#.#./#####/.#.#./.#.#." },
    { '$', "..#../.####/#.#../.###./..#.#/####./..#.." },
    { '%', "##..#/##..#/...#./..#../.#.../#..##/#..##" },
    { '&', ".##../#..#./#.#../.#.../#.#.#/#..#./.##.#" },
    { '\'', "#/#/#" },
    { '(', "..#/.#./#../#../#../.#./..#" },
    { ')', "#../.#./..#/..#/..#/.#./#.." },
    { '*', "./#.#.#/.###./#####/.###./#.#.#" },
    { '+', "./..#../..#../#####/..#../..#.." },
    { ',', "././././././.#/.#/#." },
    { '-', "./././####" },
    { '.', "././././././#" },
    { '/', "....#/....#/...#./..#../.#.../#..../#...." },
    { '0', ".###./#...#/#..##/#.#.#/##..#/#...#/.###." },
    { '1', "..#../.##../..#../..#../..#../..#../.###." },
    { '2', ".###./#...#/....#/...#./..#../.#.../#####" },
    { '3', "####./....#/....#/.###./....#/....#/####." },
    { '4', "...#./..##./.#.#./#..#./#####/...#./...#." },
    { '5', "#####/#..../####./....#/....#/#...#/.###." },
    { '6', ".###./#..../#..../####./#...#/#...#/.###." },
    { '7', "#####/....#/...#./..#../.#.../.#.../.#..." },
    { '8', ".###./#...#/#...#/.###./#...#/#...#/.###." },
    { '9', ".###./#...#/#...#/.####/....#/....#/.###." },
    { ':', "./././#/././#" },
    { ';', "./././.#/../../.#/.#/#." },
    { '<', "...#/..#./.#../#.../.#../..#./...#" },
    { '=', "././#####/...../#####" },
    { '>', "#.../.#../..#./...#/..#./.#../#..." },
    { '?', ".###./#...#/....#/...#./..#../...../..#.." },
    { '@', ".###./#...#/#.###/#.#.#/#.###/#..../.####" },
    { 'A', ".###./#...#/#...#/#####/#...#/#...#/#...#" },
    { 'B', "####./#...#/#...#/####./#...#/#...#/####." },
    { 'C', ".###./#...#/#..../#..../#..../#...#/.###." },
    { 'D', "####./#...#/#...#/#...#/#...#/#...#/####." },
    { 'E', "#####/#..../#..../####./#..../#..../#####" },
    { 'F', "#####/#..../#..../####./#..../#..../#...." },
    { 'G', ".###./#...#/#..../#.###/#...#/#...#/.###." },
    { 'H', "#...#/#...#/#...#/#####/#...#/#...#/#...#" },
    { 'I', "###/.#./.#./.#./.#./.#./###" },
    { 'J', ".####/...#./...#./...#./...#./#..#./.##.." },
    { 'K', "#...#/#..#./#.#../##.../#.#../#..#./#...#" },
    { 'L', "#..../#..../#..../#..../#..../#..../#####" },
    { 'M', "#...#/##.##/#.#.#/#.#.#/#...#/#...#/#...#" },
    { 'N', "#...#/##..#/##..#/#.#.#/#..##/#..##/#...#" },
    { 'O', ".###./#...#/#...#/#...#/#...#/#...#/.###." },
    { 'P', "####./#...#/#...#/####./#..../#..../#...." },
    { 'Q', ".###./#...#/#...#/#...#/#.#.#/#..#./.##.#" },
    { 'R', "####./#...#/#...#/####./#.#../#..#./#...#" },
    { 'S', ".####/#..../#..../.###./....#/....#/####." },
    { 'T', "#####/..#../..#../..#../..#../..#../..#.." },
    { 'U', "#...#/#...#/#...#/#...#/#...#/#...#/.###." },
    { 'V', "#...#/#...#/#...#/#...#/#...#/.#.#./..#.." },
    { 'W', "#...#/#...#/#...#/#.#.#/#.#.#/##.##/#...#" },
    { 'X', "#...#/#...#/.#.#./..#../.#.#./#...#/#...#" },
    { 'Y', "#...#/#...#/.#.#./..#../..#../..#../..#.." },
    { 'Z', "#####/....#/...#./..#../.#.../#..../#####" },
    { '[', "###/#../#../#../#../#../###" },
    { '\\', "#..../#..../.#.../..#../...#./....#/....#" },
    { ']', "###/..#/..#/..#/..#/..#/###" },
    { '^', "..#../.#.#./#...#" },
    { '_', "././././././././#####" },
    { '`', "#./.#" },
    { 'a', "././.###./....#/.####/#...#/.####" },
    { 'b', "#..../#..../####./#...#/#...#/#...#/####." },
    { 'c', "././.###./#...#/#..../#...#/.###." },
    { 'd', "....#/....#/.####/#...#/#...#/#...#/.####" },
    { 'e', "././.###./#...#/#####/#..../.###." },
    { 'f', "..##/.#../###./.#../.#../.#../.#.." },
    { 'g', "././.####/#...#/#...#/#...#/.####/....#/.###." },
    { 'h', "#..../#..../####./#...#/#...#/#...#/#...#" },
    { 'i', ".#./.../##./.#./.#./.#./.##" },
    { 'j', "..#/.../.##/..#/..#/..#/..#/..#/##." },
    { 'k', "#..../#..../#..#./#.#../##.../#.#../#..#." },
    { 'l', "##./.#./.#./.#./.#./.#./.##" },
    { 'm', "././##.#./#.#.#/#.#.#/#.#.#/#.#.#" },
    { 'n', "././####./#...#/#...#/#...#/#...#" },
    { 'o', "././.###./#...#/#...#/#...#/.###." },
    { 'p', "././####./#...#/#...#/#...#/####./#..../#...." },
    { 'q', "././.####/#...#/#...#/#...#/.####/....#/....#" },
    { 'r', "././#.##./##..#/#..../#..../#...." },
    { 's', "././.####/#..../.###./....#/####." },
    { 't', "./.#../###./.#../.#../.#../..##" },
    { 'u', "././#...#/#...#/#...#/#...#/.####" },
    { 'v', "././#...#/#...#/#...#/.#.#./..#.." },
    { 'w', "././#...#/#...#/#.#.#/#.#.#/.#.#." },
    { 'x', "././#...#/.#.#./..#../.#.#./#...#" },
    { 'y', "././#...#/#...#/#...#/#...#/.####/....#/.###." },
    { 'z', "././#####/...#./..#../.#.../#####" },
    { '{', "..##/.#../.#../#.../.#../.#../..##" },
    { '|', "#/#/#/#/#/#/#/#/#" },
    { '}', "##../..#./..#./...#/..#./..#./##.." },
    { '~', "./././.##.#/#.##." },
};

struct Bitmap {
    bool px[kRows][kCols] = {};
};

Bitmap parse(const char* rows)
{
    Bitmap bm;
    int r = 0, c = 0;
    for (const char* p = rows; *p && r < kRows; ++p) {
        if (*p == '/') {
            ++r;
            c = 0;
        } else {
            if (*p == '#' && c < kCols)
                bm.px[r][c] = true;
            ++c;
        }
    }
    return bm;
}

struct Rect {
    int x0, y0, x1, y1;  // font pixels, y up, baseline = 0
};

/// Greedy rectangle cover: horizontal runs, extended downwards while the next rows hold the same run.
std::vector<Rect> toRects(const Bitmap& bm)
{
    bool used[kRows][kCols] = {};
    std::vector<Rect> out;
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kCols;) {
            if (!bm.px[r][c] || used[r][c]) {
                ++c;
                continue;
            }
            int c1 = c;
            while (c1 + 1 < kCols && bm.px[r][c1 + 1] && !used[r][c1 + 1])
                ++c1;
            int r1 = r;
            while (r1 + 1 < kRows) {
                bool same = true;
                for (int x = c; x <= c1; ++x)
                    same = same && bm.px[r1 + 1][x] && !used[r1 + 1][x];
                if (!same)
                    break;
                ++r1;
            }
            for (int y = r; y <= r1; ++y)
                for (int x = c; x <= c1; ++x)
                    used[y][x] = true;
            // row r spans y = (7 - r - 1) .. (7 - r) above the baseline
            out.push_back({ c, kCapRows - 1 - r1, c1 + 1, kCapRows - r });
            c = c1 + 1;
        }
    }
    return out;
}

class Out {
   public:
    Out() : m_s(&m_d, QIODevice::WriteOnly) {}
    Out& u8(quint8 v)
    {
        m_s << v;
        return *this;
    }
    Out& u16(quint16 v)
    {
        m_s << v;
        return *this;
    }
    Out& i16(qint16 v)
    {
        m_s << v;
        return *this;
    }
    Out& u32(quint32 v)
    {
        m_s << v;
        return *this;
    }
    Out& i64(qint64 v)
    {
        m_s << v;
        return *this;
    }
    Out& bytes(const QByteArray& b)
    {
        m_s.writeRawData(b.constData(), int(b.size()));
        return *this;
    }
    Out& zeros(int n) { return bytes(QByteArray(n, '\0')); }
    const QByteArray& data() const { return m_d; }
    int size() const { return int(m_d.size()); }

   private:
    QByteArray m_d;
    QDataStream m_s;
};

quint32 checksum(const QByteArray& t)
{
    QByteArray padded = t;
    while (padded.size() % 4)
        padded.append('\0');
    quint32 sum = 0;
    for (qsizetype i = 0; i < padded.size(); i += 4)
        sum += (quint32(quint8(padded[i])) << 24) | (quint32(quint8(padded[i + 1])) << 16) | (quint32(quint8(padded[i + 2])) << 8) |
               quint32(quint8(padded[i + 3]));
    return sum;
}

QByteArray utf16be(const QString& s)
{
    QByteArray b;
    for (QChar c : s) {
        b.append(char(c.unicode() >> 8));
        b.append(char(c.unicode() & 0xFF));
    }
    return b;
}

struct GlyphOut {
    QByteArray glyf;
    int advance = 0;
    int lsb = 0;
    int xMin = 0, yMin = 0, xMax = 0, yMax = 0;
    int points = 0, contours = 0;
};

GlyphOut makeGlyph(const Bitmap& bm, int fixedAdvance)
{
    GlyphOut g;
    const auto rects = toRects(bm);
    int maxCol = -1;
    for (int r = 0; r < kRows; ++r)
        for (int c = 0; c < kCols; ++c)
            if (bm.px[r][c])
                maxCol = std::max(maxCol, c);
    g.advance = fixedAdvance > 0 ? fixedAdvance : (maxCol + 2) * kUnit;  // ink + 1 px spacing
    if (rects.empty())
        return g;

    int x0 = 100000, y0 = 100000, x1 = -100000, y1 = -100000;
    for (const auto& r : rects) {
        x0 = std::min(x0, r.x0 * kUnit);
        y0 = std::min(y0, r.y0 * kUnit);
        x1 = std::max(x1, r.x1 * kUnit);
        y1 = std::max(y1, r.y1 * kUnit);
    }
    g.xMin = x0;
    g.yMin = y0;
    g.xMax = x1;
    g.yMax = y1;
    g.lsb = x0;
    g.contours = int(rects.size());
    g.points = g.contours * 4;

    Out o;
    o.i16(qint16(g.contours)).i16(qint16(x0)).i16(qint16(y0)).i16(qint16(x1)).i16(qint16(y1));
    for (int i = 0; i < g.contours; ++i)
        o.u16(quint16(i * 4 + 3));
    o.u16(0);  // no instructions
    for (int i = 0; i < g.points; ++i)
        o.u8(1);  // on-curve point, 16 bit coordinate deltas
    // Clockwise outer contours (y up): bottom-left, top-left, top-right, bottom-right.
    std::vector<QPoint> pts;
    for (const auto& r : rects) {
        pts.push_back({ r.x0 * kUnit, r.y0 * kUnit });
        pts.push_back({ r.x0 * kUnit, r.y1 * kUnit });
        pts.push_back({ r.x1 * kUnit, r.y1 * kUnit });
        pts.push_back({ r.x1 * kUnit, r.y0 * kUnit });
    }
    int prev = 0;
    for (const auto& p : pts) {
        o.i16(qint16(p.x() - prev));
        prev = p.x();
    }
    prev = 0;
    for (const auto& p : pts) {
        o.i16(qint16(p.y() - prev));
        prev = p.y();
    }
    g.glyf = o.data();
    while (g.glyf.size() % 4)
        g.glyf.append('\0');
    return g;
}

QByteArray build()
{
    // glyph 0 = .notdef (a hollow box), glyphs 1..95 = ASCII 32..126
    std::vector<GlyphOut> glyphs;
    glyphs.push_back(makeGlyph(parse("#####/#...#/#...#/#...#/#...#/#...#/#####"), kNotdefAdvance));
    for (int code = 32; code <= 126; ++code) {
        const char* rows = "";
        for (const auto& d : kGlyphs)
            if (d.ch == char(code))
                rows = d.rows;
        glyphs.push_back(makeGlyph(parse(rows), code == 32 ? kSpaceAdvance : 0));
    }
    const int numGlyphs = int(glyphs.size());

    int gxMin = 100000, gyMin = 100000, gxMax = -100000, gyMax = -100000, maxAdv = 0, minLsb = 100000, minRsb = 100000, extent = 0,
        maxPts = 0, maxContours = 0;
    qint64 advSum = 0;
    for (const auto& g : glyphs) {
        maxAdv = std::max(maxAdv, g.advance);
        advSum += g.advance;
        maxPts = std::max(maxPts, g.points);
        maxContours = std::max(maxContours, g.contours);
        if (g.contours == 0)
            continue;
        gxMin = std::min(gxMin, g.xMin);
        gyMin = std::min(gyMin, g.yMin);
        gxMax = std::max(gxMax, g.xMax);
        gyMax = std::max(gyMax, g.yMax);
        minLsb = std::min(minLsb, g.lsb);
        minRsb = std::min(minRsb, g.advance - g.xMax);
        extent = std::max(extent, g.xMax);
    }

    // glyf + loca (long format) + hmtx
    Out glyf, loca, hmtx;
    for (const auto& g : glyphs) {
        loca.u32(quint32(glyf.size()));
        glyf.bytes(g.glyf);
        hmtx.u16(quint16(g.advance)).i16(qint16(g.lsb));
    }
    loca.u32(quint32(glyf.size()));

    Out head;
    head.u32(0x00010000).u32(0x00010000).u32(0).u32(0x5F0F3CF5).u16(0x0009).u16(kEm);
    head.i64(3786825600LL).i64(3786825600LL);  // created / modified (seconds since 1904), fixed for reproducibility
    head.i16(qint16(gxMin)).i16(qint16(gyMin)).i16(qint16(gxMax)).i16(qint16(gyMax));
    head.u16(0).u16(10).i16(2).i16(1).i16(0);

    Out hhea;
    hhea.u32(0x00010000).i16(kAscent).i16(-kDescent).i16(0).u16(quint16(maxAdv)).i16(qint16(minLsb)).i16(qint16(minRsb)).i16(qint16(extent));
    hhea.i16(1).i16(0).i16(0).zeros(8).i16(0).u16(quint16(numGlyphs));

    Out maxp;
    maxp.u32(0x00010000).u16(quint16(numGlyphs)).u16(quint16(maxPts)).u16(quint16(maxContours)).u16(0).u16(0).u16(1).u16(0).u16(0).u16(0).u16(0);
    maxp.u16(0).u16(0).u16(0).u16(0);  // stack elements, instruction size, component elements, component depth

    Out os2;
    os2.u16(4).i16(qint16(advSum / numGlyphs)).u16(400).u16(5).u16(0);
    os2.i16(600).i16(700).i16(0).i16(140).i16(600).i16(700).i16(0).i16(480);  // sub/superscript
    os2.i16(100).i16(300).i16(0);                                            // strikeout size/pos, family class
    os2.zeros(10);                                                           // panose
    os2.u32(1).u32(0).u32(0).u32(0);                                         // unicode ranges: Basic Latin
    os2.bytes(QByteArray("NONE"));
    os2.u16(0x00C0).u16(32).u16(126);                                        // REGULAR | USE_TYPO_METRICS
    os2.i16(kAscent).i16(-kDescent).i16(0).u16(kAscent).u16(kDescent);
    os2.u32(1).u32(0);                                                       // code pages: Latin 1
    os2.i16(5 * kUnit).i16(7 * kUnit).u16(32).u16(32).u16(1);

    // cmap: Unicode (0,3) and Windows (3,1) both pointing at one format 4 subtable: 32..126 -> glyph 1..95
    Out cmap;
    cmap.u16(0).u16(2);
    cmap.u16(0).u16(3).u32(20);
    cmap.u16(3).u16(1).u32(20);
    cmap.u16(4).u16(32).u16(0).u16(4).u16(4).u16(1).u16(0);
    cmap.u16(126).u16(0xFFFF).u16(0);
    cmap.u16(32).u16(0xFFFF);
    cmap.u16(quint16(1 - 32)).u16(1);
    cmap.u16(0).u16(0);

    struct NameRec {
        quint16 id;
        QString text;
    };
    const NameRec names[] = {
        { 0, QStringLiteral("ZERO Launcher, GPL-3.0-only") }, { 1, pixelFamily() },
        { 2, QStringLiteral("Regular") },                      { 3, QStringLiteral("ZERO Pixel 1.000") },
        { 4, pixelFamily() },                                  { 5, QStringLiteral("Version 1.000") },
        { 6, QStringLiteral("ZEROPixel-Regular") },
    };
    Out nameStrings, nameRecs;
    for (const auto& n : names) {
        const QByteArray s = utf16be(n.text);
        nameRecs.u16(3).u16(1).u16(0x0409).u16(n.id).u16(quint16(s.size())).u16(quint16(nameStrings.size()));
        nameStrings.bytes(s);
    }
    Out name;
    name.u16(0).u16(quint16(std::size(names))).u16(quint16(6 + 12 * std::size(names)));
    name.bytes(nameRecs.data()).bytes(nameStrings.data());

    Out post;
    post.u32(0x00030000).u32(0).i16(-100).i16(100).u32(0).u32(0).u32(0).u32(0).u32(0);

    QMap<QByteArray, QByteArray> tables;  // sorted by tag, as the table directory requires
    tables.insert("OS/2", os2.data());
    tables.insert("cmap", cmap.data());
    tables.insert("glyf", glyf.data());
    tables.insert("head", head.data());
    tables.insert("hhea", hhea.data());
    tables.insert("hmtx", hmtx.data());
    tables.insert("loca", loca.data());
    tables.insert("maxp", maxp.data());
    tables.insert("name", name.data());
    tables.insert("post", post.data());

    const int n = int(tables.size());
    Out dir;
    dir.u32(0x00010000).u16(quint16(n)).u16(128).u16(3).u16(quint16(n * 16 - 128));
    int offset = 12 + 16 * n;
    QByteArray body;
    int headOffset = 0;
    for (auto it = tables.cbegin(); it != tables.cend(); ++it) {
        dir.bytes(it.key()).u32(checksum(it.value())).u32(quint32(offset)).u32(quint32(it.value().size()));
        if (it.key() == "head")
            headOffset = offset;
        QByteArray padded = it.value();
        while (padded.size() % 4)
            padded.append('\0');
        body.append(padded);
        offset += int(padded.size());
    }
    QByteArray file = dir.data() + body;
    const quint32 adjust = 0xB1B0AFBAu - checksum(file);
    for (int i = 0; i < 4; ++i)
        file[headOffset + 8 + i] = char((adjust >> (24 - 8 * i)) & 0xFF);
    return file;
}

}  // namespace

QString pixelFamily()
{
    return QStringLiteral("ZERO Pixel");
}

QByteArray pixelFontData()
{
    static const QByteArray data = build();
    return data;
}

bool registerPixelFont()
{
    static int tried = 0;  // 0 = not yet, 1 = ok, -1 = failed
    if (tried != 0)
        return tried > 0;
    const int id = QFontDatabase::addApplicationFontFromData(pixelFontData());
    tried = (id >= 0 && QFontDatabase::applicationFontFamilies(id).contains(pixelFamily())) ? 1 : -1;
    if (tried < 0)
        qWarning() << "ZeroPixelFont: Qt rejected the generated font; falling back to the UI font";
    return tried > 0;
}

}  // namespace Zero
