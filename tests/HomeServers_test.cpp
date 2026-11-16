// SPDX-License-Identifier: GPL-3.0-only
// Favorite servers on Home: which servers are picked (max 6), the status pill text, the Join target, and the visibility of the Home
// section (hidden without favorites, hidden when the favorites are in no servers.dat, shown with rows otherwise).
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <sstream>

#include <io/stream_writer.h>
#include <tag_compound.h>
#include <tag_list.h>
#include <tag_primitive.h>
#include <tag_string.h>

#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/pages/HomeServersSection.h"
#include "zero/favorites/FavoriteStore.h"
#include "zero/servers/HomeServers.h"

using namespace Zero;
using namespace Zero::Servers;

namespace {

QByteArray serversDat(const QList<QPair<QString, QString>>& nameAndIp)
{
    nbt::tag_list list(nbt::tag_type::Compound);
    for (const auto& s : nameAndIp) {
        nbt::tag_compound c;
        c.insert("name", nbt::tag_string(s.first.toUtf8().toStdString()));
        c.insert("ip", nbt::tag_string(s.second.toUtf8().toStdString()));
        list.push_back(std::move(c));
    }
    nbt::tag_compound root;
    root.insert("servers", std::move(list));
    std::ostringstream stream;
    nbt::io::write_tag("", root, stream);
    return QByteArray(stream.str().data(), static_cast<qsizetype>(stream.str().size()));
}

VaultServer server(const QString& address, const QString& name, const QList<InstanceRef>& instances = {})
{
    VaultServer s;
    s.address = address;
    s.valid = true;
    s.names = { name };
    s.instances = instances;
    return s;
}

}  // namespace

class HomeServersTest : public QObject {
    Q_OBJECT

   private slots:
    void initTestCase() { Theme::setCurrent(ThemeSpec::obsidian()); }

    void picksFavoritesInScannerOrderUpToSix()
    {
        QList<VaultServer> all;
        QSet<QString> favorites;
        for (int i = 0; i < 9; ++i) {
            const QString address = QStringLiteral("s%1.example").arg(i);
            all << server(address, QStringLiteral("Server %1").arg(i));
            if (i != 1)
                favorites.insert(address);
        }
        int total = 0;
        const auto picked = favoriteServersForHome(all, favorites, kHomeServersMax, &total);
        QCOMPARE(picked.size(), 6);
        QCOMPARE(total, 8);
        QCOMPARE(picked.first().address, QStringLiteral("s0.example"));
        QCOMPARE(picked.at(1).address, QStringLiteral("s2.example"));  // s1 is not a favorite
        QVERIFY(favoriteServersForHome(all, {}).isEmpty());
        QVERIFY(favoriteServersForHome({}, favorites).isEmpty());
    }

    void pillTexts()
    {
        QCOMPARE(statusPill(nullptr).text, QStringLiteral("Checking…"));
        QCOMPARE(statusPill(nullptr).tone, StatusTone::Checking);
        PingResult online;
        online.state = PingResult::State::Responded;
        online.onlinePlayers = 12;
        online.maxPlayers = 100;
        QCOMPARE(statusPill(&online).text, QStringLiteral("Online · 12/100 players"));
        QCOMPARE(statusPill(&online).tone, StatusTone::Online);
        online.onlinePlayers = -1;
        QCOMPARE(statusPill(&online).text, QStringLiteral("Online"));
        PingResult off;
        off.state = PingResult::State::NoResponse;
        QCOMPARE(statusPill(&off).text, QStringLiteral("Offline"));
        QCOMPARE(statusPill(&off).tone, StatusTone::Offline);
        off.state = PingResult::State::InvalidAddress;
        QCOMPARE(statusPill(&off).text, QStringLiteral("Offline"));
    }

    void joinInstanceSkipsBusyOnes()
    {
        const auto s = server(QStringLiteral("x"), QStringLiteral("X"), { { "a", "Alpha" }, { "b", "Beta" } });
        QCOMPARE(joinInstance(s, {})->id, QStringLiteral("a"));
        QCOMPARE(joinInstance(s, { "a" })->id, QStringLiteral("b"));
        QVERIFY(!joinInstance(s, { "a", "b" }));
        QVERIFY(!joinInstance(server(QStringLiteral("y"), QStringLiteral("Y")), {}));
    }

    void sectionVisibility()
    {
        QTemporaryDir tmp;
        const QString datA = tmp.filePath(QStringLiteral("a/servers.dat"));
        QDir().mkpath(tmp.filePath(QStringLiteral("a")));
        {
            QFile f(datA);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(serversDat({ { "One", "127.0.0.1:9" }, { "Two", "127.0.0.1:10" } }));
        }
        auto provider = [datA]() { return QList<InstanceSource>{ { "a", "Alpha", datA } }; };
        Favorites::FavoriteStore store(tmp.filePath(QStringLiteral("favorites.json")));

        HomeServersSection section(provider, &store);
        // no favorites: hidden, nothing is scanned or pinged
        section.activate();
        QCOMPARE(section.rowCount(), 0);
        QVERIFY(!section.isVisible());
        QVERIFY(!section.scanner()->isScanning());

        // a favorite that no servers.dat has: still hidden
        store.set(Favorites::Kind::Server, QStringLiteral("nowhere.example"), true);
        section.activate();
        QTRY_VERIFY(section.scanner()->hasScanned());
        QCOMPARE(section.rowCount(), 0);
        QVERIFY(!section.isVisible());

        // a favorite that is listed: shown with its row, in the section
        store.set(Favorites::Kind::Server, QStringLiteral("127.0.0.1:9"), true);
        section.activate();
        QTRY_COMPARE(section.rowCount(), 1);
        QVERIFY(section.isVisible());
        QCOMPARE(section.shownAddresses(), QStringList{ QStringLiteral("127.0.0.1:9") });
        QVERIFY(section.seeAllVisible());

        // removing the last favorite hides it again on the next show
        store.set(Favorites::Kind::Server, QStringLiteral("127.0.0.1:9"), false);
        store.set(Favorites::Kind::Server, QStringLiteral("nowhere.example"), false);
        section.activate();
        QCOMPARE(section.rowCount(), 0);
        QVERIFY(!section.isVisible());
    }
};

QTEST_MAIN(HomeServersTest)
#include "HomeServers_test.moc"
