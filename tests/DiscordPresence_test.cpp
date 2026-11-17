// SPDX-License-Identifier: GPL-3.0-only
// DiscordPresence: handshake, SET_ACTIVITY content, clear on game end, fallback between games, hidden instance name, reconnect when Discord
// appears later, split frames, PING / PONG. A QLocalServer on a unique pipe name plays Discord.
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTest>
#include <QtEndian>

#include "zero/discord/DiscordPresence.h"

using namespace Zero::Discord;

namespace {

struct InFrame {
    quint32 opcode = 0;
    QJsonObject json;
};

class FakeDiscord : public QObject {
   public:
    QLocalServer server;
    QLocalSocket* conn = nullptr;
    QList<InFrame> frames;
    bool autoReady = true;
    bool splitReady = false;
    int connections = 0;
    bool disconnected = false;

    FakeDiscord()
    {
        connect(&server, &QLocalServer::newConnection, this, [this]() {
            conn = server.nextPendingConnection();
            ++connections;
            disconnected = false;
            m_buf.clear();
            connect(conn, &QLocalSocket::readyRead, this, &FakeDiscord::onRead);
            connect(conn, &QLocalSocket::disconnected, this, [this]() { disconnected = true; });
        });
    }

    bool listen(const QString& name)
    {
        QLocalServer::removeServer(name);
        return server.listen(name);
    }

    static QByteArray frame(quint32 opcode, const QByteArray& json)
    {
        QByteArray out(8, '\0');
        qToLittleEndian<quint32>(opcode, out.data());
        qToLittleEndian<quint32>(quint32(json.size()), out.data() + 4);
        return out + json;
    }

    void send(quint32 opcode, const QByteArray& json, bool split = false)
    {
        const QByteArray bytes = frame(opcode, json);
        if (!split) {
            conn->write(bytes);
            conn->flush();
            return;
        }
        conn->write(bytes.left(8));
        conn->flush();
        QTest::qWait(60);
        conn->write(bytes.mid(8, 5));
        conn->flush();
        QTest::qWait(60);
        conn->write(bytes.mid(13));
        conn->flush();
    }

    void sendReady(bool split = false) { send(1, R"({"cmd":"DISPATCH","evt":"READY","data":{"v":1}})", split); }

    int count(quint32 opcode, const QString& cmd = {}) const
    {
        int n = 0;
        for (const auto& f : frames)
            if (f.opcode == opcode && (cmd.isEmpty() || f.json.value("cmd").toString() == cmd))
                ++n;
        return n;
    }

    int activities() const { return count(1, QStringLiteral("SET_ACTIVITY")); }

    QJsonObject lastActivityArgs() const
    {
        for (int i = frames.size() - 1; i >= 0; --i)
            if (frames.at(i).json.value("cmd").toString() == QLatin1String("SET_ACTIVITY"))
                return frames.at(i).json.value("args").toObject();
        return {};
    }

    QJsonObject lastActivity() const { return lastActivityArgs().value("activity").toObject(); }

   private:
    QByteArray m_buf;
    void onRead()
    {
        m_buf += conn->readAll();
        while (m_buf.size() >= 8) {
            const quint32 op = qFromLittleEndian<quint32>(m_buf.constData());
            const quint32 len = qFromLittleEndian<quint32>(m_buf.constData() + 4);
            if (m_buf.size() < qsizetype(8) + qsizetype(len))
                return;
            InFrame f;
            f.opcode = op;
            f.json = QJsonDocument::fromJson(m_buf.mid(8, len)).object();
            m_buf.remove(0, 8 + int(len));
            frames.append(f);
            if (op == 0 && autoReady)
                sendReady(splitReady);
        }
    }
};

QString uniqueBase()
{
    static int n = 0;
    return QStringLiteral("zero-test-discord-%1-%2-").arg(QCoreApplication::applicationPid()).arg(++n);
}

Options optionsFor(const QString& base)
{
    Options o;
    o.pipeBase = base;
    o.pipeCount = 4;
    o.pid = 4321;
    o.retryMs = 80;
    return o;
}

Game game(const QString& id, const QString& name, const QString& mc = QStringLiteral("1.20.1"), const QString& loader = QStringLiteral("Fabric"),
          qint64 start = 1700000000)
{
    Game g;
    g.id = id;
    g.instanceName = name;
    g.mcVersion = mc;
    g.loader = loader;
    g.startEpoch = start;
    return g;
}

}  // namespace

class DiscordPresenceTest : public QObject {
    Q_OBJECT
   private slots:
    void stateTextCases()
    {
        QCOMPARE(stateText(game("a", "A")), QString::fromUtf8("Minecraft 1.20.1 · Fabric"));
        QCOMPARE(stateText(game("a", "A", "1.21", "")), QStringLiteral("Minecraft 1.21"));
        QCOMPARE(stateText(game("a", "A", "", "Forge")), QStringLiteral("Forge"));
        QCOMPARE(stateText(game("a", "A", "", "")), QString());
    }

    void handshakeAndActivity()
    {
        const QString base = uniqueBase();
        FakeDiscord fake;
        QVERIFY(fake.listen(base + "0"));
        DiscordPresence presence(optionsFor(base));
        QTest::qWait(50);
        QCOMPARE(fake.connections, 0);  // nothing while no game runs

        presence.gameStarted(game("a", "My Pack"));
        QTRY_COMPARE(fake.activities(), 1);
        QCOMPARE(fake.frames.first().opcode, 0u);
        QCOMPARE(fake.frames.first().json.value("v").toInt(), 1);
        QCOMPARE(fake.frames.first().json.value("client_id").toString(), QStringLiteral("1572300937387581450"));
        QVERIFY(presence.isReady());

        const QJsonObject args = fake.lastActivityArgs();
        QCOMPARE(args.value("pid").toInt(), 4321);
        const QJsonObject act = args.value("activity").toObject();
        QCOMPARE(act.value("details").toString(), QStringLiteral("My Pack"));
        QCOMPARE(act.value("state").toString(), QString::fromUtf8("Minecraft 1.20.1 · Fabric"));
        QCOMPARE(act.value("timestamps").toObject().value("start").toDouble(), 1700000000.0);
        QCOMPARE(act.value("assets").toObject().value("large_image").toString(), QStringLiteral("zero"));
        QCOMPARE(act.value("assets").toObject().value("large_text").toString(), QStringLiteral("ZERO Launcher"));
        QVERIFY(!fake.frames.last().json.value("nonce").toString().isEmpty());
    }

    void clearOnGameEndClosesPipe()
    {
        const QString base = uniqueBase();
        FakeDiscord fake;
        QVERIFY(fake.listen(base + "0"));
        DiscordPresence presence(optionsFor(base));
        presence.gameStarted(game("a", "A"));
        QTRY_COMPARE(fake.activities(), 1);

        presence.gameEnded("a");
        QTRY_COMPARE(fake.activities(), 2);
        QVERIFY(fake.lastActivityArgs().contains("activity"));
        QVERIFY(fake.lastActivityArgs().value("activity").isNull());
        QTRY_COMPARE(fake.count(2), 1);
        QTRY_VERIFY(fake.disconnected);
        QVERIFY(!presence.isReady());
        QTest::qWait(250);  // the retry timer is off: no reconnection
        QCOMPARE(fake.connections, 1);
    }

    void fallbackBetweenGames()
    {
        const QString base = uniqueBase();
        FakeDiscord fake;
        QVERIFY(fake.listen(base + "0"));
        DiscordPresence presence(optionsFor(base));
        presence.gameStarted(game("a", "First", "1.19.2", "Forge", 1700000000));
        QTRY_COMPARE(fake.activities(), 1);
        presence.gameStarted(game("b", "Second", "1.20.4", "Quilt", 1700000500));
        QTRY_COMPARE(fake.activities(), 2);
        QCOMPARE(fake.lastActivity().value("details").toString(), QStringLiteral("Second"));

        presence.gameEnded("b");
        QTRY_COMPARE(fake.activities(), 3);
        QCOMPARE(fake.lastActivity().value("details").toString(), QStringLiteral("First"));
        QCOMPARE(fake.lastActivity().value("timestamps").toObject().value("start").toDouble(), 1700000000.0);
        QCOMPARE(presence.runningGames(), 1);

        presence.gameEnded("a");
        QTRY_COMPARE(fake.activities(), 4);
        QVERIFY(fake.lastActivityArgs().value("activity").isNull());
        QCOMPARE(fake.connections, 1);
    }

    void textStaysWithinDiscordLimits()
    {
        // details/state must be 2..128 characters, or Discord rejects the whole activity
        QCOMPARE(buildActivity(game("a", "X"), true).value("details").toString(), QStringLiteral("Playing Minecraft"));
        const QString longName(200, QLatin1Char('n'));
        const QString details = buildActivity(game("a", longName), true).value("details").toString();
        QCOMPARE(details.size(), 128);
        QVERIFY(details.endsWith(QChar(0x2026)));
        QCOMPARE(buildActivity(game("a", "Pack", "1.21", ""), true).value("details").toString(), QStringLiteral("Pack"));
    }

    void instanceNameHiddenWhenOptionOff()
    {
        const QString base = uniqueBase();
        FakeDiscord fake;
        QVERIFY(fake.listen(base + "0"));
        DiscordPresence presence(optionsFor(base));
        presence.setShowInstance(false);
        presence.gameStarted(game("a", "Secret Pack"));
        QTRY_COMPARE(fake.activities(), 1);
        QCOMPARE(fake.lastActivity().value("details").toString(), QStringLiteral("Playing Minecraft"));

        presence.setShowInstance(true);  // live change
        QTRY_COMPARE(fake.activities(), 2);
        QCOMPARE(fake.lastActivity().value("details").toString(), QStringLiteral("Secret Pack"));
    }

    void disableClearsAndEnableShows()
    {
        const QString base = uniqueBase();
        FakeDiscord fake;
        QVERIFY(fake.listen(base + "0"));
        DiscordPresence presence(optionsFor(base));
        presence.setEnabled(false);
        presence.gameStarted(game("a", "A"));
        QTest::qWait(200);
        QCOMPARE(fake.connections, 0);  // off: never connects

        presence.setEnabled(true);  // a game is running: shows at once
        QTRY_COMPARE(fake.activities(), 1);
        presence.setEnabled(false);
        QTRY_COMPARE(fake.activities(), 2);
        QVERIFY(fake.lastActivityArgs().value("activity").isNull());
        QTRY_VERIFY(fake.disconnected);
    }

    void reconnectsWhenDiscordAppearsLater()
    {
        const QString base = uniqueBase();
        DiscordPresence presence(optionsFor(base));
        presence.gameStarted(game("a", "Late"));
        QTest::qWait(250);  // several failed rounds, nothing listens
        QVERIFY(!presence.isReady());

        FakeDiscord fake;
        QVERIFY(fake.listen(base + "0"));
        QTRY_COMPARE_WITH_TIMEOUT(fake.activities(), 1, 5000);
        QCOMPARE(fake.lastActivity().value("details").toString(), QStringLiteral("Late"));
    }

    void reconnectsAfterDiscordQuits()
    {
        const QString base = uniqueBase();
        DiscordPresence presence(optionsFor(base));
        {
            FakeDiscord fake;
            QVERIFY(fake.listen(base + "0"));
            presence.gameStarted(game("a", "A"));
            QTRY_COMPARE(fake.activities(), 1);
            // stop listening first: with the pipe still offered, the 80 ms retry reconnects at once and a loaded machine can
            // miss the short not-ready window in between
            fake.server.close();
            fake.conn->disconnectFromServer();
            QTRY_VERIFY(!presence.isReady());
        }
        FakeDiscord again;
        QVERIFY(again.listen(base + "0"));
        QTRY_COMPARE_WITH_TIMEOUT(again.activities(), 1, 5000);
    }

    void usesAnotherPipeIndex()
    {
        const QString base = uniqueBase();
        FakeDiscord fake;
        QVERIFY(fake.listen(base + "2"));
        DiscordPresence presence(optionsFor(base));
        presence.gameStarted(game("a", "A"));
        QTRY_COMPARE_WITH_TIMEOUT(fake.activities(), 1, 5000);
    }

    void splitFrames()
    {
        const QString base = uniqueBase();
        FakeDiscord fake;
        fake.splitReady = true;  // header, then 5 bytes, then the rest, with pauses
        QVERIFY(fake.listen(base + "0"));
        DiscordPresence presence(optionsFor(base));
        presence.gameStarted(game("a", "Split"));
        QTRY_COMPARE_WITH_TIMEOUT(fake.activities(), 1, 5000);
        QCOMPARE(fake.lastActivity().value("details").toString(), QStringLiteral("Split"));
    }

    void pingIsAnsweredWithPong()
    {
        const QString base = uniqueBase();
        FakeDiscord fake;
        QVERIFY(fake.listen(base + "0"));
        DiscordPresence presence(optionsFor(base));
        presence.gameStarted(game("a", "A"));
        QTRY_COMPARE(fake.activities(), 1);

        fake.send(3, R"({"nonce":"abc"})");
        QTRY_COMPARE(fake.count(4), 1);
        for (const auto& f : fake.frames)
            if (f.opcode == 4)
                QCOMPARE(f.json.value("nonce").toString(), QStringLiteral("abc"));

        fake.send(3, R"({"nonce":"def"})", true);  // split PING too
        QTRY_COMPARE_WITH_TIMEOUT(fake.count(4), 2, 5000);
    }
};

QTEST_MAIN(DiscordPresenceTest)
#include "DiscordPresence_test.moc"
