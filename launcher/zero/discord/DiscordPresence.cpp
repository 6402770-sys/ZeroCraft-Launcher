// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Discord Rich Presence (see DiscordPresence.h).
#include "DiscordPresence.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QUuid>
#include <QtEndian>

namespace Zero {
namespace Discord {

namespace {
enum Opcode : quint32 { Handshake = 0, Frame = 1, Close = 2, Ping = 3, Pong = 4 };
constexpr quint32 kMaxBody = 1u << 20;  // anything bigger is not Discord talking
constexpr int kMaxText = 128;           // Discord's limit for details/state
}  // namespace

QString stateText(const Game& game)
{
    QString text;
    if (!game.mcVersion.isEmpty())
        text = QStringLiteral("Minecraft ") + game.mcVersion;
    if (!game.loader.isEmpty())
        text += (text.isEmpty() ? QString() : QStringLiteral(" · ")) + game.loader;
    return text;
}

QJsonObject buildActivity(const Game& game, bool showInstance)
{
    QJsonObject activity;
    // Discord rejects a whole activity whose details/state text is shorter than 2 or longer than 128 characters
    QString name = game.instanceName.trimmed();
    if (name.size() > kMaxText)
        name = name.left(kMaxText - 1) + QChar(0x2026);
    activity.insert(QStringLiteral("details"), showInstance && name.size() >= 2
                                                   ? name
                                                   : QCoreApplication::translate("DiscordPresence", "Playing Minecraft"));
    QString state = stateText(game);
    if (state.size() > kMaxText)
        state = state.left(kMaxText - 1) + QChar(0x2026);
    if (state.size() >= 2)
        activity.insert(QStringLiteral("state"), state);
    if (game.startEpoch > 0)
        activity.insert(QStringLiteral("timestamps"), QJsonObject{ { QStringLiteral("start"), game.startEpoch } });
    activity.insert(QStringLiteral("assets"), QJsonObject{ { QStringLiteral("large_image"), QStringLiteral("zero") },
                                                          { QStringLiteral("large_text"), QStringLiteral("ZERO Launcher") } });
    return activity;
}

DiscordPresence::DiscordPresence(const Options& options, QObject* parent) : QObject(parent), m_options(options)
{
    m_retry.setInterval(qMax(1, m_options.retryMs));
    m_retry.setSingleShot(false);
    connect(&m_retry, &QTimer::timeout, this, [this]() {
        if (wanted() && !m_socket)
            tryConnect(0);
    });
}

DiscordPresence::~DiscordPresence()
{
    closeSocket();
}

void DiscordPresence::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    sync();
}

void DiscordPresence::setShowInstance(bool show)
{
    if (m_showInstance == show)
        return;
    m_showInstance = show;
    sync();
}

void DiscordPresence::gameStarted(const Game& game)
{
    for (int i = 0; i < m_games.size(); ++i) {
        if (m_games.at(i).id == game.id) {
            m_games.removeAt(i);
            break;
        }
    }
    m_games.append(game);
    sync();
}

void DiscordPresence::gameEnded(const QString& id)
{
    for (int i = 0; i < m_games.size(); ++i) {
        if (m_games.at(i).id == id) {
            m_games.removeAt(i);
            sync();
            return;
        }
    }
}

void DiscordPresence::sync()
{
    if (!wanted()) {
        m_retry.stop();
        closeSocket();
        return;
    }
    if (m_ready) {
        const QJsonObject activity = buildActivity(m_games.last(), m_showInstance);
        sendActivity(&activity);
        return;
    }
    if (!m_retry.isActive())
        m_retry.start();
    if (!m_socket)
        tryConnect(0);
}

void DiscordPresence::tryConnect(int index)
{
    if (m_socket || index >= m_options.pipeCount)
        return;
    m_index = index;
    m_connecting = true;
    m_ready = false;
    m_buffer.clear();
    m_lastSent.clear();
    m_socket = new QLocalSocket(this);
    connect(m_socket, &QLocalSocket::connected, this, &DiscordPresence::onConnected);
    connect(m_socket, &QLocalSocket::readyRead, this, &DiscordPresence::onReadyRead);
    connect(m_socket, &QLocalSocket::disconnected, this, &DiscordPresence::dropSocket);
    connect(m_socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) { dropSocket(); });
    m_socket->connectToServer(m_options.pipeBase + QString::number(index));
}

void DiscordPresence::onConnected()
{
    const QJsonObject hello{ { QStringLiteral("v"), 1 }, { QStringLiteral("client_id"), m_options.clientId } };
    sendFrame(Handshake, QJsonDocument(hello).toJson(QJsonDocument::Compact));
}

void DiscordPresence::onReadyRead()
{
    if (!m_socket)
        return;
    m_buffer += m_socket->readAll();
    while (m_socket && m_buffer.size() >= 8) {
        const quint32 opcode = qFromLittleEndian<quint32>(m_buffer.constData());
        const quint32 length = qFromLittleEndian<quint32>(m_buffer.constData() + 4);
        if (length > kMaxBody) {
            dropSocket();
            return;
        }
        if (m_buffer.size() < qsizetype(8) + qsizetype(length))
            return;  // the rest of the frame is still on its way
        const QByteArray body = m_buffer.mid(8, length);
        m_buffer.remove(0, 8 + int(length));
        handleFrame(opcode, body);
    }
}

void DiscordPresence::handleFrame(quint32 opcode, const QByteArray& body)
{
    switch (opcode) {
        case Ping:
            sendFrame(Pong, body);
            break;
        case Close:
            dropSocket();
            break;
        case Frame: {
            const QJsonObject message = QJsonDocument::fromJson(body).object();
            if (message.value(QStringLiteral("cmd")).toString() == QLatin1String("DISPATCH") &&
                message.value(QStringLiteral("evt")).toString() == QLatin1String("READY") && !m_ready) {
                m_ready = true;
                m_connecting = false;
                m_loggedConnected = true;
                qInfo() << "Discord Rich Presence: connected";
                if (wanted()) {
                    const QJsonObject activity = buildActivity(m_games.last(), m_showInstance);
                    sendActivity(&activity);
                }
            }
            break;
        }
        default:
            break;
    }
}

void DiscordPresence::sendFrame(quint32 opcode, const QByteArray& json)
{
    if (!m_socket || m_socket->state() != QLocalSocket::ConnectedState)
        return;
    QByteArray frame(8, '\0');
    qToLittleEndian<quint32>(opcode, frame.data());
    qToLittleEndian<quint32>(quint32(json.size()), frame.data() + 4);
    frame += json;
    m_socket->write(frame);
}

void DiscordPresence::sendActivity(const QJsonObject* activity)
{
    if (!m_ready)
        return;
    const QByteArray key = activity ? QJsonDocument(*activity).toJson(QJsonDocument::Compact) : QByteArray("null");
    if (key == m_lastSent)
        return;
    m_lastSent = key;
    QJsonObject args{ { QStringLiteral("pid"), double(m_options.pid) } };
    args.insert(QStringLiteral("activity"), activity ? QJsonValue(*activity) : QJsonValue(QJsonValue::Null));
    const QJsonObject message{ { QStringLiteral("cmd"), QStringLiteral("SET_ACTIVITY") },
                               { QStringLiteral("args"), args },
                               { QStringLiteral("nonce"), QUuid::createUuid().toString(QUuid::WithoutBraces) } };
    sendFrame(Frame, QJsonDocument(message).toJson(QJsonDocument::Compact));
}

void DiscordPresence::closeSocket()
{
    if (!m_socket)
        return;
    QLocalSocket* socket = m_socket;
    const bool wasReady = m_ready;
    if (wasReady) {
        sendActivity(nullptr);
        sendFrame(Close, QJsonDocument(QJsonObject{ { QStringLiteral("v"), 1 }, { QStringLiteral("client_id"), m_options.clientId } })
                             .toJson(QJsonDocument::Compact));
        socket->flush();
    }
    socket->disconnect(this);
    m_socket = nullptr;
    m_ready = false;
    m_connecting = false;
    m_lastSent.clear();
    m_buffer.clear();
    if (wasReady && socket->state() != QLocalSocket::UnconnectedState) {
        // delete once the pending bytes are out
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        socket->disconnectFromServer();
    } else {
        socket->abort();
        socket->deleteLater();
    }
    if (wasReady && m_loggedConnected) {
        m_loggedConnected = false;
        qInfo() << "Discord Rich Presence: disconnected";
    }
}

void DiscordPresence::dropSocket()
{
    // Discord closed or refused the pipe: forget it; the next pipe index or the retry timer takes it from here
    QLocalSocket* socket = m_socket;
    if (!socket)
        return;
    const int failedIndex = m_index;
    const bool wasReady = m_ready;
    const bool wasConnecting = m_connecting;
    socket->disconnect(this);
    m_socket = nullptr;
    m_ready = false;
    m_connecting = false;
    m_lastSent.clear();
    m_buffer.clear();
    socket->abort();
    socket->deleteLater();
    if (wasReady && m_loggedConnected) {
        m_loggedConnected = false;
        qInfo() << "Discord Rich Presence: disconnected";
    }
    if (!wanted())
        return;
    if (!wasReady && wasConnecting && failedIndex + 1 < m_options.pipeCount)
        tryConnect(failedIndex + 1);
    else if (!m_retry.isActive())
        m_retry.start();
}

}  // namespace Discord
}  // namespace Zero
