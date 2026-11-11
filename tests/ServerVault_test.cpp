// SPDX-License-Identifier: GPL-3.0-only
// Server Vault: servers.dat parsing (real NBT written with the bundled nbt library), address parsing, grouping across instances,
// sorting / search, the async scanner, the pure ping helpers (motd flattening, formatting codes) and the pinger itself against a
// fake status server on 127.0.0.1 (nothing outside the machine is contacted).
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <sstream>

#include <io/stream_writer.h>
#include <tag_compound.h>
#include <tag_list.h>
#include <tag_primitive.h>
#include <tag_string.h>

#include "zero/servers/ServerPinger.h"
#include "zero/servers/ServerVault.h"

using namespace Zero::Servers;

namespace {

struct Spec {
    QString name;
    QString ip;
    QByteArray icon;  // raw PNG bytes (base64 encoded into the tag), empty = no tag
    bool hidden = false;
    bool skipIp = false;
};

QByteArray serversDat(const QList<Spec>& specs)
{
    nbt::tag_list list(nbt::tag_type::Compound);
    for (const Spec& s : specs) {
        nbt::tag_compound c;
        c.insert("name", nbt::tag_string(s.name.toUtf8().toStdString()));
        if (!s.skipIp)
            c.insert("ip", nbt::tag_string(s.ip.toUtf8().toStdString()));
        if (!s.icon.isEmpty())
            c.insert("icon", nbt::tag_string(s.icon.toBase64().toStdString()));
        if (s.hidden)
            c.insert("hidden", nbt::tag_byte(1));
        list.push_back(std::move(c));
    }
    nbt::tag_compound root;
    root.insert("servers", std::move(list));
    std::ostringstream stream;
    nbt::io::write_tag("", root, stream);
    return QByteArray(stream.str().data(), static_cast<qsizetype>(stream.str().size()));
}

void writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
    f.write(data);
}

ServerEntry entry(const QString& name, const QString& address, const QString& instanceId, const QString& instanceName,
                  const QByteArray& icon = {})
{
    ServerEntry e;
    e.name = name;
    e.address = address;
    e.instanceId = instanceId;
    e.instanceName = instanceName;
    e.iconPng = icon;
    return e;
}

QByteArray varInt(int value)
{
    QByteArray out;
    quint32 v = static_cast<quint32>(value);
    do {
        quint8 byte = v & 0x7F;
        v >>= 7;
        if (v)
            byte |= 0x80;
        out.append(static_cast<char>(byte));
    } while (v);
    return out;
}

/// A fake Minecraft status server: answers the handshake + status request with `json` after `delayMs` (-1 = never answers).
class FakeStatusServer : public QObject {
    Q_OBJECT
   public:
    explicit FakeStatusServer(const QByteArray& json, int delayMs = 0) : m_json(json), m_delay(delayMs)
    {
        connect(&m_server, &QTcpServer::newConnection, this, &FakeStatusServer::onConnection);
    }
    bool listen() { return m_server.listen(QHostAddress::AnyIPv4, 0); }
    quint16 port() const { return m_server.serverPort(); }
    int connections() const { return m_connections; }

   private:
    void onConnection()
    {
        while (QTcpSocket* socket = m_server.nextPendingConnection()) {
            ++m_connections;
            ++m_open;
            auto* buffer = new QByteArray;
            connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer]() {
                buffer->append(socket->readAll());
                if (!buffer->endsWith(QByteArray("\x01\x00", 2)) || m_delay < 0)  // the status request is the last packet of the client
                    return;
                QByteArray body = varInt(0) + varInt(static_cast<int>(m_json.size())) + m_json;
                const QByteArray packet = varInt(static_cast<int>(body.size())) + body;
                QTimer::singleShot(m_delay, socket, [socket, packet]() {
                    socket->write(packet);
                    socket->flush();
                });
            });
            connect(socket, &QTcpSocket::disconnected, this, [this, socket, buffer]() {
                --m_open;
                delete buffer;
                socket->deleteLater();
            });
        }
    }

    QTcpServer m_server;
    QByteArray m_json;
    int m_delay;
    int m_connections = 0;
    int m_open = 0;
};

QByteArray statusJson()
{
    return QJsonDocument(QJsonObject{ { "version", QJsonObject{ { "name", "\xC2\xA7" "aPaper 1.20.4" }, { "protocol", 765 } } },
                                      { "players", QJsonObject{ { "max", 500 }, { "online", 123 } } },
                                      { "description", QJsonObject{ { "text", "Hello " },
                                                                    { "extra", QJsonArray{ QJsonObject{ { "text", "World" }, { "color", "gold" } } } } } } })
        .toJson(QJsonDocument::Compact);
}

}  // namespace

class ServerVaultTest : public QObject {
    Q_OBJECT

   private slots:
    // ----- servers.dat -----
    void parse_realFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray png("\x89PNG\r\n\x1a\n-fake-icon-", 18);
        const QString path = dir.filePath("servers.dat");
        writeFile(path, serversDat({ { "Home", " Example.COM:25570 ", png }, { "Café", "play.example.org" }, { "No name", "10.0.0.2" } }));

        const ServersFile file = parseServersDat(path);
        QCOMPARE(file.error, QString());
        QCOMPARE(file.entries.size(), 3);
        QCOMPARE(file.entries[0].name, QStringLiteral("Home"));
        QCOMPARE(file.entries[0].address, QStringLiteral("Example.COM:25570"));  // trimmed, not normalized
        QCOMPARE(file.entries[0].iconPng, png);
        QCOMPARE(file.entries[1].name, QStringLiteral("Café"));
        QVERIFY(file.entries[1].iconPng.isEmpty());
        QVERIFY(file.entries[0].instanceId.isEmpty());
    }

    void parse_skipsHiddenAndAddressless()
    {
        Spec hidden{ "Direct", "hidden.example.com", {}, true };
        Spec nameless{ "No ip", {}, {}, false, true };
        Spec blank{ "Blank", "   " };
        const ServersFile file = parseServersDatBytes(serversDat({ { "A", "a.example.com" }, hidden, nameless, blank }));
        QCOMPARE(file.error, QString());
        QCOMPARE(file.entries.size(), 1);
        QCOMPARE(file.entries[0].address, QStringLiteral("a.example.com"));
    }

    void parse_missingEmptyAndEmptyList()
    {
        QTemporaryDir dir;
        const ServersFile missing = parseServersDat(dir.filePath("nope/servers.dat"));
        QVERIFY(missing.entries.isEmpty());
        QCOMPARE(missing.error, QString());

        writeFile(dir.filePath("empty.dat"), {});
        const ServersFile empty = parseServersDat(dir.filePath("empty.dat"));
        QVERIFY(empty.entries.isEmpty());
        QCOMPARE(empty.error, QString());

        const ServersFile none = parseServersDatBytes(serversDat({}));
        QVERIFY(none.entries.isEmpty());
        QCOMPARE(none.error, QString());
    }

    void parse_brokenFiles()
    {
        QTemporaryDir dir;
        const QByteArray good = serversDat({ { "A", "a.example.com" }, { "B", "b.example.com" } });

        struct Case {
            const char* label;
            QByteArray bytes;
        };
        nbt::tag_compound wrongShape;
        wrongShape.insert("other", nbt::tag_int(1));
        std::ostringstream shapeStream;
        nbt::io::write_tag("", wrongShape, shapeStream);
        nbt::tag_list ints(nbt::tag_type::Int);
        ints.push_back(nbt::tag_int(1));
        nbt::tag_compound intRoot;
        intRoot.insert("servers", std::move(ints));
        std::ostringstream intStream;
        nbt::io::write_tag("", intRoot, intStream);

        const QList<Case> cases = {
            { "truncated", good.left(good.size() / 2) },
            { "garbage", QByteArray("this is not nbt at all, not even close") },
            { "gzip-looking", QByteArray("\x1f\x8b\x08\x00garbage", 11) },
            { "no servers tag", QByteArray(shapeStream.str().data(), static_cast<qsizetype>(shapeStream.str().size())) },
            { "servers not compounds", QByteArray(intStream.str().data(), static_cast<qsizetype>(intStream.str().size())) },
        };
        for (const Case& c : cases) {
            const QString path = dir.filePath(QStringLiteral("%1.dat").arg(QString::fromLatin1(c.label)));
            writeFile(path, c.bytes);
            const ServersFile file = parseServersDat(path);
            QVERIFY2(file.entries.isEmpty(), c.label);
            QVERIFY2(!file.error.isEmpty(), c.label);
        }
    }

    void parse_readsOnly()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("servers.dat");
        const QByteArray bytes = serversDat({ { "A", "a.example.com" } });
        writeFile(path, bytes);
        const QDateTime before = QFileInfo(path).lastModified();
        parseServersDat(path);
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), bytes);
        QCOMPARE(QFileInfo(path).lastModified(), before);
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 1);  // no temp / backup file appeared
    }

    // ----- addresses -----
    void address_table_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<QString>("host");
        QTest::addColumn<int>("port");
        QTest::addColumn<QString>("normalized");

        QTest::newRow("plain") << "example.com" << true << "example.com" << 25565 << "example.com";
        QTest::newRow("upper+trim") << "  Play.Example.COM  " << true << "play.example.com" << 25565 << "play.example.com";
        QTest::newRow("port") << "example.com:25570" << true << "example.com" << 25570 << "example.com:25570";
        QTest::newRow("default port explicit") << "example.com:25565" << true << "example.com" << 25565 << "example.com";
        QTest::newRow("ipv4") << "192.168.1.5:1234" << true << "192.168.1.5" << 1234 << "192.168.1.5:1234";
        QTest::newRow("localhost") << "localhost" << true << "localhost" << 25565 << "localhost";
        QTest::newRow("underscore+dash") << "my_server-1.example.com" << true << "my_server-1.example.com" << 25565 << "my_server-1.example.com";
        QTest::newRow("ipv6 brackets") << "[::1]" << true << "::1" << 25565 << "[::1]";
        QTest::newRow("ipv6 brackets port") << "[2001:DB8::1]:25570" << true << "2001:db8::1" << 25570 << "[2001:db8::1]:25570";
        QTest::newRow("ipv6 bare") << "::1" << true << "::1" << 25565 << "[::1]";
        QTest::newRow("max port") << "example.com:65535" << true << "example.com" << 65535 << "example.com:65535";

        QTest::newRow("empty") << "" << false << "" << 0 << "";
        QTest::newRow("spaces only") << "   " << false << "" << 0 << "";
        QTest::newRow("space inside") << "my server.com" << false << "" << 0 << "";
        QTest::newRow("port 0") << "example.com:0" << false << "" << 0 << "";
        QTest::newRow("port too big") << "example.com:65536" << false << "" << 0 << "";
        QTest::newRow("port text") << "example.com:abc" << false << "" << 0 << "";
        QTest::newRow("port empty") << "example.com:" << false << "" << 0 << "";
        QTest::newRow("port signed") << "example.com:+80" << false << "" << 0 << "";
        QTest::newRow("empty host") << ":25565" << false << "" << 0 << "";
        QTest::newRow("empty label") << "a..b" << false << "" << 0 << "";
        QTest::newRow("bad char") << "exa$mple.com" << false << "" << 0 << "";
        QTest::newRow("slash") << "example.com/path" << false << "" << 0 << "";
        QTest::newRow("empty brackets") << "[]" << false << "" << 0 << "";
        QTest::newRow("open bracket") << "[::1" << false << "" << 0 << "";
        QTest::newRow("brackets not ipv6") << "[example.com]:25565" << false << "" << 0 << "";
        QTest::newRow("junk after bracket") << "[::1]x" << false << "" << 0 << "";
        QTest::newRow("too many colons") << "a:b:c" << false << "" << 0 << "";
        QTest::newRow("long host") << QString(300, QLatin1Char('a')) << false << "" << 0 << "";
    }

    void address_table()
    {
        QFETCH(QString, input);
        QFETCH(bool, valid);
        QFETCH(QString, host);
        QFETCH(int, port);
        QFETCH(QString, normalized);
        const HostPort hp = parseAddress(input);
        QCOMPARE(hp.valid, valid);
        if (valid) {
            QCOMPARE(hp.host, host);
            QCOMPARE(int(hp.port), port);
            QCOMPARE(hp.toString(), normalized);
            QVERIFY(hp.error.isEmpty());
            QCOMPARE(parseAddress(hp.toString()).toString(), normalized);  // normalizing twice changes nothing
        } else {
            QVERIFY(!hp.error.isEmpty());
        }
    }

    // ----- grouping -----
    void group_dedupesAcrossInstances()
    {
        const QList<ServerEntry> entries = {
            entry("Survival Hub", "Play.Example.com", "i2", "Survival 1.20"),
            entry("Hub", "play.example.com:25565", "i1", "Creative", "ICON"),
            entry("Hub", "play.example.com", "i1", "Creative"),  // same server saved twice in one instance
            entry("Other", "other.example.com:1234", "i1", "Creative"),
            entry("Weird", "bad host", "i2", "Survival 1.20"),
            entry("Weird 2", " bad host ", "i1", "Creative"),
        };
        const QList<VaultServer> groups = groupByAddress(entries);
        QCOMPARE(groups.size(), 3);

        // sorted by display name: Hub, Other, Weird
        QCOMPARE(groups[0].address, QStringLiteral("play.example.com"));
        QVERIFY(groups[0].valid);
        QCOMPARE(groups[0].names, (QStringList{ "Hub", "Survival Hub" }));
        QCOMPARE(groups[0].displayName(), QStringLiteral("Hub"));
        QCOMPARE(groups[0].instances.size(), 2);
        QCOMPARE(groups[0].instances[0].name, QStringLiteral("Creative"));  // sorted by name
        QCOMPARE(groups[0].instances[1].id, QStringLiteral("i2"));
        QCOMPARE(groups[0].iconPng, QByteArray("ICON"));  // the first non-empty icon
        QVERIFY(groups[0].inInstance("i1"));
        QVERIFY(!groups[0].inInstance("zz"));

        QCOMPARE(groups[1].address, QStringLiteral("other.example.com:1234"));
        QCOMPARE(groups[1].instances.size(), 1);

        QCOMPARE(groups[2].address, QStringLiteral("bad host"));
        QVERIFY(!groups[2].valid);
        QCOMPARE(groups[2].instances.size(), 2);
        QCOMPARE(groups[2].names, (QStringList{ "Weird", "Weird 2" }));
    }

    void group_namelessUsesAddress()
    {
        const QList<VaultServer> groups = groupByAddress({ entry("", "zeta.example.com", "i1", "A"), entry("Alpha", "mid.example.com", "i1", "A") });
        QCOMPARE(groups.size(), 2);
        QCOMPARE(groups[0].displayName(), QStringLiteral("Alpha"));
        QCOMPARE(groups[1].displayName(), QStringLiteral("zeta.example.com"));
        QVERIFY(groupByAddress({}).isEmpty());
    }

    void search()
    {
        const QList<VaultServer> groups = groupByAddress({ entry("Hub", "play.example.com", "i1", "Survival 1.20") });
        const VaultServer& s = groups.first();
        QVERIFY(matchesSearch(s, ""));
        QVERIFY(matchesSearch(s, "  "));
        QVERIFY(matchesSearch(s, "hub"));
        QVERIFY(matchesSearch(s, "PLAY.EX"));
        QVERIFY(matchesSearch(s, "survival"));
        QVERIFY(!matchesSearch(s, "creative"));
    }

    void sort_modes()
    {
        QList<VaultServer> servers = groupByAddress({ entry("Bravo", "b.example.com", "i1", "A"), entry("Alpha", "a.example.com", "i1", "A"),
                                                      entry("Charlie", "c.example.com", "i1", "A"), entry("Delta", "d.example.com", "i1", "A"),
                                                      entry("Echo", "bad host", "i1", "A") });
        auto responded = [](int players, int latency) {
            PingResult r;
            r.state = PingResult::State::Responded;
            r.onlinePlayers = players;
            r.latencyMs = latency;
            return r;
        };
        QHash<QString, PingResult> results;
        results.insert("a.example.com", responded(10, 80));
        results.insert("b.example.com", responded(300, 40));
        results.insert("c.example.com", PingResult());  // NoResponse
        // d: no result yet; "bad host": invalid
        PingResult invalid;
        invalid.state = PingResult::State::InvalidAddress;
        results.insert("bad host", invalid);

        auto order = [&](SortMode mode) {
            sortServers(servers, mode, results);
            QStringList names;
            for (const auto& s : servers)
                names << s.displayName();
            return names;
        };
        QCOMPARE(order(SortMode::Name), (QStringList{ "Alpha", "Bravo", "Charlie", "Delta", "Echo" }));
        QCOMPARE(order(SortMode::Players), (QStringList{ "Bravo", "Alpha", "Charlie", "Delta", "Echo" }));
        QCOMPARE(order(SortMode::Latency), (QStringList{ "Bravo", "Alpha", "Charlie", "Delta", "Echo" }));
        results.insert("a.example.com", responded(10, 20));
        QCOMPARE(order(SortMode::Latency), (QStringList{ "Alpha", "Bravo", "Charlie", "Delta", "Echo" }));

        QCOMPARE(sortModeFromString(sortModeToString(SortMode::Players)), SortMode::Players);
        QCOMPARE(sortModeFromString(sortModeToString(SortMode::Latency)), SortMode::Latency);
        QCOMPARE(sortModeFromString("garbage"), SortMode::Name);
        QCOMPARE(sortModeFromString(""), SortMode::Name);
    }

    // ----- scanner -----
    void scanner_groupsAcrossInstancesAndReportsBrokenFiles()
    {
        QTemporaryDir dir;
        const QString a = dir.filePath("a/servers.dat"), b = dir.filePath("b/servers.dat"), c = dir.filePath("c/servers.dat");
        writeFile(a, serversDat({ { "Shared", "shared.example.com" }, { "Only A", "a.example.com" } }));
        writeFile(b, serversDat({ { "Shared", "SHARED.example.com:25565" } }));
        writeFile(c, QByteArray("not nbt"));
        QByteArray before;
        {
            QFile original(a);
            QVERIFY(original.open(QIODevice::ReadOnly));
            before = original.readAll();
        }

        ServerVaultScanner scanner([&]() {
            return QList<InstanceSource>{ { "a", "Alpha", a }, { "b", "Beta", b }, { "c", "Gamma (broken)", c }, { "d", "Delta (no file)", dir.filePath("d/servers.dat") } };
        });
        QVERIFY(!scanner.hasScanned());
        QSignalSpy changed(&scanner, &ServerVaultScanner::listChanged);
        QSignalSpy scanning(&scanner, &ServerVaultScanner::scanningChanged);
        scanner.rescan();
        QVERIFY(scanner.isScanning());
        QTRY_COMPARE(changed.count(), 1);
        QVERIFY(!scanner.isScanning());
        QVERIFY(scanner.hasScanned());
        QCOMPARE(scanning.count(), 2);

        QCOMPARE(scanner.servers().size(), 2);
        QCOMPARE(scanner.instanceCount(), 2);  // broken and missing files count for nothing
        const VaultServer& shared = scanner.servers().at(1);  // "Only A" < "Shared"
        QCOMPARE(shared.address, QStringLiteral("shared.example.com"));
        QCOMPARE(shared.instances.size(), 2);
        QCOMPARE(scanner.errors().size(), 1);
        QCOMPARE(scanner.errors().first().instanceName, QStringLiteral("Gamma (broken)"));
        QFile f(a);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), before);
    }

    void scanner_dropsSupersededGeneration()
    {
        QTemporaryDir dir;
        const QString a = dir.filePath("servers.dat");
        writeFile(a, serversDat({ { "One", "one.example.com" } }));
        ServerVaultScanner scanner([&]() { return QList<InstanceSource>{ { "a", "Alpha", a } }; });
        QSignalSpy changed(&scanner, &ServerVaultScanner::listChanged);
        scanner.rescan();
        scanner.rescan();
        scanner.rescan();
        QCOMPARE(scanner.generation(), quint64(3));
        QTRY_COMPARE(changed.count(), 1);
        QTest::qWait(150);
        QCOMPARE(changed.count(), 1);  // the first two results were dropped
    }

    // ----- ping helpers -----
    void stripFormatting_cases()
    {
        QCOMPARE(stripFormatting(QString::fromUtf8("\xC2\xA7" "aGreen \xC2\xA7lbold\xC2\xA7r plain")), QStringLiteral("Green bold plain"));
        QCOMPARE(stripFormatting(QString::fromUtf8("\xC2\xA7x\xC2\xA7" "f\xC2\xA7" "f\xC2\xA7" "0\xC2\xA7" "0\xC2\xA7" "0\xC2\xA7" "0hex")), QStringLiteral("hex"));
        QCOMPARE(stripFormatting(QString::fromUtf8("trailing\xC2\xA7")), QStringLiteral("trailing"));
        QCOMPARE(stripFormatting(QString::fromUtf8("\xC2\xA7\xC2\xA7" "ab")), QStringLiteral("ab"));  // the second section sign is the code of the first
        QCOMPARE(stripFormatting("no codes & fine"), QStringLiteral("no codes & fine"));
        QCOMPARE(stripFormatting(""), QString());
    }

    void flattenMotd_cases()
    {
        QCOMPARE(flattenMotd(QJsonValue(QString::fromUtf8("\xC2\xA7" "6Gold \xC2\xA7" "lserver"))), QStringLiteral("Gold server"));
        QCOMPARE(flattenMotd(QJsonObject{ { "text", "A " }, { "extra", QJsonArray{ "B ", QJsonObject{ { "text", "C" } } } } }), QStringLiteral("A B C"));
        QCOMPARE(flattenMotd(QJsonObject{ { "text", "" },
                                         { "extra", QJsonArray{ QJsonObject{ { "text", "one\n" } },
                                                                QJsonObject{ { "text", "two" }, { "extra", QJsonArray{ QJsonObject{ { "text", "!" } } } } } } } }),
                 QStringLiteral("one\ntwo!"));
        QCOMPARE(flattenMotd(QJsonArray{ "x", QJsonObject{ { "text", "y" } } }), QStringLiteral("xy"));
        QCOMPARE(flattenMotd(QJsonValue(QString("  a  \n   b \r\n"))), QStringLiteral("a\nb"));
        QCOMPARE(flattenMotd(QJsonValue(QString("tab\there\x01x"))), QStringLiteral("tab herex"));
        QCOMPARE(flattenMotd(QJsonValue(42)), QString());
        QCOMPARE(flattenMotd(QJsonValue()), QString());
        QCOMPARE(flattenMotd(QJsonObject{ { "color", "red" } }), QString());
        QCOMPARE(flattenMotd(QJsonValue(QString(2000, QLatin1Char('z')))).size(), 512);

        QJsonObject deep{ { "text", "x" } };  // 40 levels deep: the flattening stops instead of recursing forever
        for (int i = 0; i < 40; ++i)
            deep = QJsonObject{ { "text", "a" }, { "extra", QJsonArray{ deep } } };
        QVERIFY(flattenMotd(deep).size() <= 20);
    }

    void resultFromStatus_cases()
    {
        const QJsonObject status = QJsonDocument::fromJson(statusJson()).object();
        const PingResult r = resultFromStatus(status, 45);
        QCOMPARE(r.state, PingResult::State::Responded);
        QCOMPARE(r.latencyMs, 45);
        QCOMPARE(r.onlinePlayers, 123);
        QCOMPARE(r.maxPlayers, 500);
        QCOMPARE(r.versionName, QStringLiteral("Paper 1.20.4"));
        QCOMPARE(r.motd, QStringLiteral("Hello World"));

        const PingResult minimal = resultFromStatus(QJsonObject{ { "players", "not an object" } }, 1);
        QCOMPARE(minimal.state, PingResult::State::Responded);
        QCOMPARE(minimal.onlinePlayers, -1);
        QCOMPARE(minimal.maxPlayers, -1);
        QVERIFY(minimal.versionName.isEmpty());
        QVERIFY(minimal.motd.isEmpty());

        const PingResult odd = resultFromStatus(QJsonObject{ { "players", QJsonObject{ { "online", "12" }, { "max", 20.0 } } },
                                                             { "version", QJsonObject{ { "name", QString(200, QLatin1Char('v')) } } } },
                                                 2);
        QCOMPARE(odd.onlinePlayers, -1);  // strings are not numbers
        QCOMPARE(odd.maxPlayers, 20);
        QCOMPARE(odd.versionName.size(), 64);
    }

    // ----- pinger against a local fake server -----
    void pinger_responded()
    {
        FakeStatusServer server(statusJson(), 60);
        QVERIFY(server.listen());
        const QString address = QStringLiteral("127.0.0.1:%1").arg(server.port());

        ServerPinger pinger;
        QSignalSpy results(&pinger, &ServerPinger::result);
        QSignalSpy settled(&pinger, &ServerPinger::settled);
        pinger.ping(address);
        pinger.ping(address);  // already pending: no second connection
        QVERIFY(pinger.isPending(address));
        QTRY_COMPARE(results.count(), 1);
        QCOMPARE(settled.count(), 1);
        QVERIFY(!pinger.isPending(address));
        QCOMPARE(results.first().at(0).toString(), address);
        const PingResult r = results.first().at(1).value<PingResult>();
        QCOMPARE(r.state, PingResult::State::Responded);
        QCOMPARE(r.onlinePlayers, 123);
        QCOMPARE(r.maxPlayers, 500);
        QCOMPARE(r.versionName, QStringLiteral("Paper 1.20.4"));
        QCOMPARE(r.motd, QStringLiteral("Hello World"));
        QVERIFY(r.latencyMs >= 50);  // the server waited 60 ms
        QVERIFY(r.latencyMs < 3000);

        // cached for the session
        pinger.ping(address);
        QTest::qWait(150);
        QCOMPARE(results.count(), 1);
        QCOMPARE(server.connections(), 1);

        // refresh asks again
        pinger.refresh({ address });
        QVERIFY(!pinger.hasResult(address));
        QTRY_COMPARE(results.count(), 2);
        QCOMPARE(server.connections(), 2);
    }

    void pinger_invalidAndRefused()
    {
        quint16 closedPort = 0;
        {
            QTcpServer probe;
            QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
            closedPort = probe.serverPort();
        }  // nothing listens on it any more
        const QString refused = QStringLiteral("127.0.0.1:%1").arg(closedPort);

        ServerPinger pinger;
        QSignalSpy results(&pinger, &ServerPinger::result);
        QSignalSpy settled(&pinger, &ServerPinger::settled);
        pinger.refresh({ QStringLiteral("not a host"), QStringLiteral("example.com:99999"), refused });
        QTRY_COMPARE_WITH_TIMEOUT(results.count(), 3, 8000);
        QCOMPARE(settled.count(), 1);
        QCOMPARE(pinger.results().value("not a host").state, PingResult::State::InvalidAddress);
        QCOMPARE(pinger.results().value("example.com:99999").state, PingResult::State::InvalidAddress);
        QCOMPARE(pinger.results().value(refused).state, PingResult::State::NoResponse);
        QCOMPARE(pinger.results().value(refused).latencyMs, -1);
        QCOMPARE(pinger.inFlight(), 0);
    }

    void pinger_atMostFourInFlight()
    {
        FakeStatusServer server(statusJson(), 250);
        QVERIFY(server.listen());
        QStringList addresses;
        for (int i = 1; i <= 9; ++i)  // 127.0.0.1 .. 127.0.0.9 all reach the same server
            addresses << QStringLiteral("127.0.0.%1:%2").arg(i).arg(server.port());

        ServerPinger pinger;
        int highest = 0;
        QSignalSpy results(&pinger, &ServerPinger::result);
        QTimer sampler;
        connect(&sampler, &QTimer::timeout, &pinger, [&]() { highest = qMax(highest, pinger.inFlight()); });
        sampler.start(5);
        for (const QString& a : addresses)
            pinger.ping(a);
        highest = qMax(highest, pinger.inFlight());
        QCOMPARE(pinger.pendingCount(), 9);
        QTRY_COMPARE_WITH_TIMEOUT(results.count(), 9, 8000);
        QVERIFY2(highest == ServerPinger::kMaxInFlight, qPrintable(QString::number(highest)));
        for (const QString& a : addresses)
            QCOMPARE(pinger.results().value(a).state, PingResult::State::Responded);
    }

    void pinger_timesOutWhenSilent()
    {
        FakeStatusServer server(statusJson(), -1);  // accepts, never answers
        QVERIFY(server.listen());
        const QString address = QStringLiteral("127.0.0.1:%1").arg(server.port());
        ServerPinger pinger;
        QSignalSpy results(&pinger, &ServerPinger::result);
        QElapsedTimer clock;
        clock.start();
        pinger.ping(address);
        QTRY_COMPARE_WITH_TIMEOUT(results.count(), 1, ServerPinger::kTimeoutMs + 3000);
        QVERIFY2(clock.elapsed() >= ServerPinger::kTimeoutMs - 200, qPrintable(QString::number(clock.elapsed())));
        QCOMPARE(pinger.results().value(address).state, PingResult::State::NoResponse);
        QCOMPARE(pinger.inFlight(), 0);
    }
};

QTEST_GUILESS_MAIN(ServerVaultTest)

#include "ServerVault_test.moc"
