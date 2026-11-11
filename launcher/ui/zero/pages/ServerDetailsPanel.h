// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the Servers details panel: icon, name, address (copy), status, MOTD, version, players, latency, the instances the
// server is saved in, and the actions "Play on server" and "Show instance". The panel never touches servers.dat.
#pragma once

#include <QSet>
#include <QString>
#include "ui/zero/ZeroCard.h"
#include "zero/servers/ServerVault.h"

class QLabel;
class QPushButton;
class QStackedLayout;
class QToolButton;
class QVBoxLayout;

namespace Zero {

class ElidedLabel;
class PillTag;
class ServerIconTile;

class ServerDetailsPanel : public ZeroCard {
    Q_OBJECT
   public:
    static constexpr int kWidth = 300;

    explicit ServerDetailsPanel(QWidget* parent = nullptr);

    /// Shows `server` with its ping outcome (`result` null = not pinged yet; `pending` = a ping is on its way).
    void setServer(const Servers::VaultServer& server, const Servers::PingResult* result, bool pending);
    /// Back to the "Select a server" state.
    void clear();
    /// Instances that are launching or running: they cannot be launched again.
    void setBusyInstances(const QSet<QString>& ids);

    // for tests / evidence
    bool hasServer() const { return !m_server.address.isEmpty(); }
    const Servers::VaultServer& server() const { return m_server; }
    QPushButton* playButton() const { return m_play; }
    QPushButton* showInstanceButton() const { return m_showInstance; }
    QToolButton* copyButton() const { return m_copy; }

   signals:
    /// "Play on server": launch `instanceId` with a quick join to `address`.
    void playRequested(const QString& address, const QString& instanceId);
    void showInstanceRequested(const QString& instanceId);

   private:
    void refreshIcons();
    void refreshActions();
    void copyAddress();
    /// One instance: its id. Several: a menu to pick one; `play` tells which action the pick triggers.
    void chooseInstance(bool play);

    QStackedLayout* m_stack;
    ServerIconTile* m_icon;
    ElidedLabel* m_name;
    ElidedLabel* m_address;
    QToolButton* m_copy;
    PillTag* m_status;
    QLabel* m_disclaimer;
    QLabel* m_players;
    QLabel* m_latency;
    ElidedLabel* m_version;
    QWidget* m_motdBlock;
    QLabel* m_motd;
    QVBoxLayout* m_instances;
    QWidget* m_instancesBlock;
    QPushButton* m_play;
    QPushButton* m_showInstance;
    Servers::VaultServer m_server;
    QSet<QString> m_busy;
};

}  // namespace Zero
