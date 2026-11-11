// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Server Vault pinger: asks a saved server for its status (Minecraft Server List Ping) through Prism's McResolver /
// McClient. A response only means the server answered a status request; it does not mean you can join it. Pings go only to
// addresses the user saved in their own servers.dat, nothing else is contacted, and only the address and the outcome are logged.
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QSet>
#include <QStringList>
#include "ServerVault.h"

class QTimer;

namespace Zero {
namespace Servers {

/// Removes Minecraft formatting codes: every section sign (U+00A7) together with the character after it (colors, styles and the
/// `§x§r§r§g§g§b§b` RGB form all fall out of that rule). A trailing lone section sign is dropped too.
QString stripFormatting(const QString& text);

/// Flattens the `description` of a status response into plain text: a string, a chat object (`text`, then `extra[]` recursively) or
/// an array of those. Formatting codes are stripped, each line is trimmed, the text is capped at 512 characters and control
/// characters other than line breaks are removed. Anything else gives an empty string.
QString flattenMotd(const QJsonValue& description);

/// Builds a Responded result from the status JSON (`players.online/max`, `version.name`, `description`).
PingResult resultFromStatus(const QJsonObject& status, int latencyMs);

/// Pings servers, at most kMaxInFlight at a time, kTimeoutMs each. Results are cached for the lifetime of the object: ping() of an
/// address that is cached, queued or in flight does nothing; refresh() forgets everything and asks again. GUI thread only.
class ServerPinger : public QObject {
    Q_OBJECT
   public:
    static constexpr int kMaxInFlight = 4;
    static constexpr int kTimeoutMs = 5000;

    explicit ServerPinger(QObject* parent = nullptr);
    ~ServerPinger() override;

    /// Queues `address` (a VaultServer::address) unless it already has a result or is on its way.
    void ping(const QString& address);
    /// Same for several addresses; `settled()` is emitted at most once, after the last of them.
    void pingAll(const QStringList& addresses);
    /// Forgets all results, drops the queue and pings `addresses` again. Pings that are in flight finish quietly (their results are
    /// not used) and keep counting against kMaxInFlight until they do.
    void refresh(const QStringList& addresses);

    bool hasResult(const QString& address) const { return m_results.contains(address); }
    /// True while `address` is queued or being pinged for the current round.
    bool isPending(const QString& address) const { return m_pending.contains(address); }
    const QHash<QString, PingResult>& results() const { return m_results; }
    int pendingCount() const { return static_cast<int>(m_pending.size()); }
    int inFlight() const { return static_cast<int>(m_jobs.size()); }

   signals:
    void result(const QString& address, const Zero::Servers::PingResult& result);
    /// The queue ran empty and nothing is pending for the current round.
    void settled();

   private:
    struct Job;
    void enqueue(const QString& address);
    void pump();
    void start(const QString& address);
    void finish(quint64 jobId, const PingResult& result);
    void deliver(const QString& address, const PingResult& result);

    QHash<quint64, Job*> m_jobs;
    QStringList m_queue;
    QSet<QString> m_pending;  // queued + in flight of the current round
    QHash<QString, PingResult> m_results;
    quint64 m_nextJob = 1;
    quint64 m_round = 1;
    bool m_pumping = false;
};

}  // namespace Servers
}  // namespace Zero

Q_DECLARE_METATYPE(Zero::Servers::PingResult)
