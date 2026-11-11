// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Server Vault core: the servers saved in every instance's `servers.dat`, grouped by address. No widgets, no
// APPLICATION: the instances come from an injected provider. Strictly READ-ONLY: servers.dat is only ever opened for reading
// (adding, editing and removing servers stays in Prism's per-instance Servers page). See docs/zero/ARCHITECTURE.md ("Server Vault").
#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

namespace Zero {
namespace Servers {

constexpr quint16 kDefaultPort = 25565;

/// One server of one instance's servers.dat.
struct ServerEntry {
    QString name;
    QString address;     ///< the `ip` tag, trimmed (never empty)
    QByteArray iconPng;  ///< decoded `icon` tag (a 64x64 PNG), empty when the server has none
    QString instanceId;  ///< filled in by the scanner, empty after parseServersDat()
    QString instanceName;
};

/// Result of reading one servers.dat.
struct ServersFile {
    QList<ServerEntry> entries;
    QString error;  ///< empty = fine (a missing file is fine); otherwise the file exists but could not be understood
};

/// Reads `path` (read-only). Missing file = empty list, no error. A broken / not-NBT / wrong-shaped file = empty list + error text.
/// Entries without an address and entries Minecraft marks `hidden` (direct-connect leftovers, not in the multiplayer list) are skipped.
ServersFile parseServersDat(const QString& path);
/// Same for bytes already in memory (an empty array is an empty, valid file).
ServersFile parseServersDatBytes(const QByteArray& bytes);

/// A parsed server address.
struct HostPort {
    QString host;  ///< lowercase; IPv6 without brackets
    quint16 port = kDefaultPort;
    bool valid = false;
    QString error;  ///< set when !valid

    bool isIPv6() const { return host.contains(QLatin1Char(':')); }
    /// Normalized text: `host`, `[v6]`, with `:port` only when the port is not 25565.
    QString toString() const;
};

/// "host", "host:port", "[v6]", "[v6]:port" and bare IPv6 literals; surrounding spaces are trimmed, the host is lowercased, the port
/// defaults to 25565. Anything else (empty, spaces inside, bad characters, port 0 / > 65535 / not a number, empty brackets) is invalid.
HostPort parseAddress(const QString& address);

/// An instance a server is saved in.
struct InstanceRef {
    QString id;
    QString name;

    bool operator==(const InstanceRef&) const = default;
};

/// One server address, with every name it is saved under and every instance it is saved in.
struct VaultServer {
    QString address;     ///< normalized (HostPort::toString()), or the trimmed original text when invalid
    bool valid = false;  ///< the address parses
    QStringList names;   ///< distinct non-empty names, sorted
    QList<InstanceRef> instances;  ///< distinct by id, sorted by name
    QByteArray iconPng;  ///< the first non-empty icon (entries in the given order)

    /// First name, or the address when every entry is nameless.
    QString displayName() const { return names.isEmpty() ? address : names.first(); }
    bool inInstance(const QString& instanceId) const;

    bool operator==(const VaultServer&) const = default;
};

/// Groups entries by normalized address (invalid addresses group by their trimmed text), sorted by display name (A-Z), then address.
QList<VaultServer> groupByAddress(const QList<ServerEntry>& entries);

/// Case-insensitive substring search over names, address and instance names. Empty text matches all.
bool matchesSearch(const VaultServer& server, const QString& text);

/// Result of one ping. `Responded` only means the server answered a status request: it does not mean you can join it.
struct PingResult {
    enum class State { Responded, NoResponse, InvalidAddress };
    State state = State::NoResponse;
    int latencyMs = -1;
    int onlinePlayers = -1;  ///< -1 = the server did not say
    int maxPlayers = -1;
    QString versionName;
    QString motd;  ///< plain text (formatting codes stripped), may contain line breaks
};

/// Sort orders of the Servers page (persisted as "name" | "players" | "latency" in ZeroServersSort).
enum class SortMode { Name, Players, Latency };

QString sortModeToString(SortMode mode);
/// Unknown / empty text gives SortMode::Name.
SortMode sortModeFromString(const QString& text);

/// Stable sort. Name = A-Z. Players = most online players first; Latency = fastest first. For both, servers that responded come
/// first, the rest (no result yet, no response, invalid) after them; ties fall back to the name, then the address.
/// `results` is keyed by VaultServer::address.
void sortServers(QList<VaultServer>& servers, SortMode mode, const QHash<QString, PingResult>& results);

/// One instance to read.
struct InstanceSource {
    QString id;
    QString name;
    QString serversDatPath;  ///< `<gameRoot>/servers.dat` (may not exist)
};

/// An instance whose servers.dat exists but could not be read.
struct SourceError {
    QString instanceName;
    QString message;
};

/// Reads every source on the pool and groups the result. Every rescan() starts a new generation: older results are dropped.
class ServerVaultScanner : public QObject {
    Q_OBJECT
   public:
    /// Called on the GUI thread by every rescan().
    using SourceProvider = std::function<QList<InstanceSource>()>;

    explicit ServerVaultScanner(SourceProvider provider, QObject* parent = nullptr);

    void rescan();

    const QList<VaultServer>& servers() const { return m_servers; }
    /// Instances that have at least one server.
    int instanceCount() const { return m_instanceCount; }
    const QList<SourceError>& errors() const { return m_errors; }
    bool isScanning() const { return m_scanning; }
    /// True once a scan delivered its result at least once.
    bool hasScanned() const { return m_hasScanned; }
    quint64 generation() const { return m_generation; }

   signals:
    /// The server list was replaced.
    void listChanged();
    void scanningChanged(bool scanning);

   private:
    void onScanned(quint64 generation, const QList<VaultServer>& servers, int instanceCount, const QList<SourceError>& errors);

    SourceProvider m_provider;
    QList<VaultServer> m_servers;
    QList<SourceError> m_errors;
    int m_instanceCount = 0;
    quint64 m_generation = 0;
    bool m_scanning = false;
    bool m_hasScanned = false;
};

}  // namespace Servers
}  // namespace Zero
