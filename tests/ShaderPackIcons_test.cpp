// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the shader pack icons' pure part (launcher/zero/content/ShaderPackIcons): the ids to ask per provider and
// the mapping of the Modrinth / CurseForge answers (saved samples in testdata/ShaderPackIcons) to icon URLs. No network.
#include <QFile>
#include <QTest>

#include "zero/content/ShaderPackIcons.h"

using namespace Zero::ShaderIcons;
using ModPlatform::ResourceProvider;

namespace {
QByteArray sample(const QString& name)
{
    QFile f(QFINDTESTDATA("testdata/ShaderPackIcons/" + name));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
}  // namespace

class ShaderPackIconsTest : public QObject {
    Q_OBJECT
   private slots:
    void keysNameTheProvider()
    {
        QCOMPARE(keyOf(ResourceProvider::MODRINTH, "abc"), QString("modrinth:abc"));
        QCOMPARE(keyOf(ResourceProvider::FLAME, " 42 "), QString("curseforge:42"));
        QVERIFY(keyOf(ResourceProvider::FLAME, "  ").isEmpty());
    }

    void idsAreCollectedPerProviderOnceAndSkipWhatIsKnownOrAsked()
    {
        const QList<Ref> refs = { { ResourceProvider::MODRINTH, "b" }, { ResourceProvider::MODRINTH, "a" },
                                  { ResourceProvider::MODRINTH, "b" }, { ResourceProvider::FLAME, "9" },
                                  { ResourceProvider::FLAME, "10" },   { ResourceProvider::FLAME, "" },
                                  { ResourceProvider::MODRINTH, "known" }, { ResourceProvider::FLAME, "asked" } };
        QHash<QString, QString> known;
        known.insert("modrinth:known", "https://x/y.png");
        QSet<QString> asked;
        asked.insert("curseforge:asked");
        const Ids ids = collectIds(refs, known, asked);
        QCOMPARE(ids.modrinth, QStringList({ "a", "b" }));
        QCOMPARE(ids.curseforge, QStringList({ "10", "9" }));  // plain string order
        QVERIFY(!ids.isEmpty());
        QVERIFY(collectIds({}, {}, {}).isEmpty());
        QVERIFY(collectIds({ { ResourceProvider::MODRINTH, "known" } }, known, {}).isEmpty());
    }

    void modrinthAnswerMapsToIconUrls()
    {
        const QByteArray json = sample("modrinth-projects.json");
        QVERIFY(!json.isEmpty());
        const auto urls = parseModrinth(json);
        const QString icon = "https://cdn.modrinth.com/data/HVnmMxH1/79cb7c8123bbc54945305b2ebad6b8881efdf5f8_96.webp";
        QCOMPARE(urls.value("modrinth:HVnmMxH1"), icon);
        QCOMPARE(urls.value("modrinth:complementary-reimagined"), icon);  // the slug finds it too
        QVERIFY(!urls.contains("modrinth:ZZZZZZZZ"));                      // null icon: left out
        QVERIFY(parseModrinth("not json").isEmpty());
        QVERIFY(parseModrinth("{}").isEmpty());
    }

    void curseForgeAnswerMapsToLogoUrls()
    {
        const QByteArray json = sample("curseforge-mods.json");
        QVERIFY(!json.isEmpty());
        const auto urls = parseCurseForge(json);
        QCOMPARE(urls.value("curseforge:123456"), QString("https://media.forgecdn.net/avatars/thumbnails/1/2/256/256/bsl.png"));
        QCOMPARE(urls.value("curseforge:777"), QString("https://media.forgecdn.net/avatars/9/9/full.png"));
        QVERIFY(!urls.contains("curseforge:888"));
        QCOMPARE(urls.size(), 2);
        QVERIFY(parseCurseForge("[]").isEmpty());
    }
};

QTEST_GUILESS_MAIN(ShaderPackIconsTest)

#include "ShaderPackIcons_test.moc"
