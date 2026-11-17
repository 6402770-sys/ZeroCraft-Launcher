// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Discord Rich Presence. No widgets, no Application: talks to the Discord desktop client over its local IPC
// (Windows named pipes discord-ipc-0..9, little-endian opcode + length + UTF-8 JSON frames). While no game runs nothing is shown,
// nothing is connected. Everything is asynchronous (QLocalSocket); the pipe base name, the timers and the pid are injectable.
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QLocalSocket>
#include <QObject>
#include <QString>
#include <QTimer>

namespace Zero {
namespace Discord {

/// ZERO's Discord application (public id).
constexpr const char* kClientId = "1572300937387581450";

/// What one running game shows. `loader` is a display name ("Fabric") or empty.
struct Game {
    QString id;
    QString instanceName;
    QString mcVersion;
    QString loader;
    qint64 startEpoch = 0;  ///< seconds since epoch, when the game reached the Running phase
};

/// "Minecraft 1.20.1 · Fabric", "Minecraft 1.20.1", or "" when the version is unknown and there is no loader.
QString stateText(const Game& game);

/// The `activity` object for a game; `showInstance` false puts "Playing Minecraft" in `details`.
QJsonObject buildActivity(const Game& game, bool showInstance);

struct Options {
    QString pipeBase = QStringLiteral("discord-ipc-");  ///< the server name is pipeBase + index
    int pipeCount = 10;
    QString clientId = QString::fromLatin1(kClientId);
    qint64 pid = 0;
    int retryMs = 30000;  ///< while a game runs and Discord is not reachable
};

class DiscordPresence : public QObject {
    Q_OBJECT
   public:
    explicit DiscordPresence(const Options& options, QObject* parent = nullptr);
    ~DiscordPresence() override;

    void setEnabled(bool enabled);
    void setShowInstance(bool show);
    bool enabled() const { return m_enabled; }

    /// A game reached the Running phase (again for the same id: replaces it and makes it the newest).
    void gameStarted(const Game& game);
    /// The game ended (unknown id: nothing happens).
    void gameEnded(const QString& id);

    bool isReady() const { return m_ready; }
    int runningGames() const { return m_games.size(); }

   private:
    bool wanted() const { return m_enabled && !m_games.isEmpty(); }
    void sync();
    void tryConnect(int index);
    void onConnected();
    void onReadyRead();
    void handleFrame(quint32 opcode, const QByteArray& body);
    void sendFrame(quint32 opcode, const QByteArray& json);
    void sendActivity(const QJsonObject* activity);
    void closeSocket();
    void dropSocket();

    Options m_options;
    bool m_enabled = true;
    bool m_showInstance = true;
    QList<Game> m_games;  // oldest first; the last one is shown

    QLocalSocket* m_socket = nullptr;
    int m_index = 0;
    bool m_connecting = false;
    bool m_ready = false;
    bool m_loggedConnected = false;
    QByteArray m_buffer;
    QByteArray m_lastSent;  // the activity JSON last sent while ready ("null" for a clear); empty = nothing sent
    QTimer m_retry;
};

}  // namespace Discord
}  // namespace Zero
