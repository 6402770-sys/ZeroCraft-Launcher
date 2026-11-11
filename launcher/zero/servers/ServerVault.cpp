// SPDX-License-Identifier: GPL-3.0-only
#include "ServerVault.h"

#include <QCoreApplication>
#include <QFile>
#include <QHostAddress>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <algorithm>
#include <sstream>

#include <io/stream_reader.h>
#include <tag_compound.h>
#include <tag_list.h>
#include <tag_primitive.h>
#include <tag_string.h>

namespace Zero {
namespace Servers {

namespace {
QString tr(const char* text)
{
    return QCoreApplication::translate("Zero::Servers", text);
}

QString stringTag(const nbt::tag_compound& compound, const char* key)
{
    if (!compound.has_key(key, nbt::tag_type::String))
        return {};
    return QString::fromUtf8(compound.at(key).as<nbt::tag_string>().get().c_str());
}
}  // namespace

ServersFile parseServersDatBytes(const QByteArray& bytes)
{
    ServersFile out;
    if (bytes.isEmpty())
        return out;
    try {
        std::istringstream stream(std::string(bytes.constData(), static_cast<size_t>(bytes.size())));
        auto root = nbt::io::read_compound(stream);
        if (!root.second) {
            out.error = tr("The file is not a servers list.");
            return out;
        }
        if (!root.second->has_key("servers", nbt::tag_type::List)) {
            out.error = tr("The file has no server list.");
            return out;
        }
        const auto& list = root.second->at("servers").as<nbt::tag_list>();
        if (list.size() > 0 && list.el_type() != nbt::tag_type::Compound) {
            out.error = tr("The server list has an unexpected format.");
            return out;
        }
        for (size_t i = 0; i < list.size(); ++i) {
            const auto& server = list[i].as<nbt::tag_compound>();
            const QString address = stringTag(server, "ip").trimmed();
            if (address.isEmpty())
                continue;
            if (server.has_key("hidden", nbt::tag_type::Byte) && server.at("hidden").as<nbt::tag_byte>().get() != 0)
                continue;
            ServerEntry e;
            e.name = stringTag(server, "name").trimmed();
            e.address = address;
            e.iconPng = QByteArray::fromBase64(stringTag(server, "icon").toLatin1());
            out.entries.append(e);
        }
    } catch (...) {  // nbt::io::input_error, std::bad_cast, std::out_of_range, bad_alloc on a corrupt length
        out.entries.clear();
        out.error = tr("The file is damaged and could not be read.");
    }
    return out;
}

ServersFile parseServersDat(const QString& path)
{
    QFile file(path);
    if (!file.exists())
        return {};
    if (!file.open(QIODevice::ReadOnly)) {
        ServersFile out;
        out.error = tr("The file could not be opened.");
        return out;
    }
    return parseServersDatBytes(file.readAll());
}

QString HostPort::toString() const
{
    QString out = isIPv6() ? QStringLiteral("[%1]").arg(host) : host;
    if (port != kDefaultPort)
        out += QStringLiteral(":%1").arg(port);
    return out;
}

HostPort parseAddress(const QString& address)
{
    HostPort result;
    auto fail = [&result](const QString& error) {
        result.valid = false;
        result.error = error;
        return result;
    };

    const QString text = address.trimmed();
    if (text.isEmpty())
        return fail(tr("The address is empty."));

    QString host;
    QString portText;
    bool hasPort = false;
    auto ipv6 = [&](const QString& literal) {
        QHostAddress parsed;
        if (literal.isEmpty() || !parsed.setAddress(literal) || parsed.protocol() != QAbstractSocket::IPv6Protocol)
            return false;
        host = parsed.toString().toLower();
        return true;
    };

    if (text.startsWith(QLatin1Char('['))) {
        const int close = text.indexOf(QLatin1Char(']'));
        if (close < 0)
            return fail(tr("The closing bracket is missing."));
        if (!ipv6(text.mid(1, close - 1).trimmed()))
            return fail(tr("The part in brackets is not an IPv6 address."));
        const QString rest = text.mid(close + 1);
        if (!rest.isEmpty()) {
            if (!rest.startsWith(QLatin1Char(':')))
                return fail(tr("Unexpected text after the closing bracket."));
            portText = rest.mid(1);
            hasPort = true;
        }
    } else {
        const int colons = static_cast<int>(text.count(QLatin1Char(':')));
        if (colons >= 2) {
            if (!ipv6(text))  // a bare IPv6 literal has no port
                return fail(tr("Too many colons."));
        } else if (colons == 1) {
            const int colon = text.indexOf(QLatin1Char(':'));
            host = text.left(colon);
            portText = text.mid(colon + 1);
            hasPort = true;
        } else {
            host = text;
        }
        if (!host.contains(QLatin1Char(':'))) {
            host = host.toLower();
            if (host.isEmpty())
                return fail(tr("The host name is empty."));
            if (host.size() > 253)
                return fail(tr("The host name is too long."));
            for (const QString& label : host.split(QLatin1Char('.'))) {
                if (label.isEmpty() || label.size() > 63)
                    return fail(tr("The host name is not valid."));
                for (const QChar c : label) {
                    if (!(c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_')))
                        return fail(tr("The host name has characters that are not allowed."));
                }
            }
        }
    }

    quint16 port = kDefaultPort;
    if (hasPort) {
        bool ok = false;
        const int value = portText.toInt(&ok);
        for (const QChar c : portText) {
            if (!c.isDigit())
                ok = false;
        }
        if (!ok || value < 1 || value > 65535)
            return fail(tr("The port is not a number between 1 and 65535."));
        port = static_cast<quint16>(value);
    }

    result.host = host;
    result.port = port;
    result.valid = true;
    result.error.clear();
    return result;
}

bool VaultServer::inInstance(const QString& instanceId) const
{
    return std::any_of(instances.cbegin(), instances.cend(), [&](const InstanceRef& r) { return r.id == instanceId; });
}

QList<VaultServer> groupByAddress(const QList<ServerEntry>& entries)
{
    QList<VaultServer> out;
    QHash<QString, int> index;
    for (const ServerEntry& e : entries) {
        const HostPort hp = parseAddress(e.address);
        const QString key = hp.valid ? hp.toString() : e.address.trimmed();
        auto it = index.find(key);
        if (it == index.end()) {
            VaultServer s;
            s.address = key;
            s.valid = hp.valid;
            it = index.insert(key, static_cast<int>(out.size()));
            out.append(s);
        }
        VaultServer& s = out[it.value()];
        const QString name = e.name.trimmed();
        if (!name.isEmpty() && !s.names.contains(name))
            s.names.append(name);
        if (!e.instanceId.isEmpty() && !s.inInstance(e.instanceId))
            s.instances.append({ e.instanceId, e.instanceName });
        if (s.iconPng.isEmpty() && !e.iconPng.isEmpty())
            s.iconPng = e.iconPng;
    }
    for (VaultServer& s : out) {
        std::sort(s.names.begin(), s.names.end(), [](const QString& a, const QString& b) {
            const int c = a.localeAwareCompare(b);
            return c != 0 ? c < 0 : a < b;
        });
        std::sort(s.instances.begin(), s.instances.end(), [](const InstanceRef& a, const InstanceRef& b) {
            const int c = a.name.localeAwareCompare(b.name);
            return c != 0 ? c < 0 : a.id < b.id;
        });
    }
    std::sort(out.begin(), out.end(), [](const VaultServer& a, const VaultServer& b) {
        const int c = a.displayName().localeAwareCompare(b.displayName());
        return c != 0 ? c < 0 : a.address < b.address;
    });
    return out;
}

bool matchesSearch(const VaultServer& server, const QString& text)
{
    const QString needle = text.trimmed();
    if (needle.isEmpty())
        return true;
    if (server.address.contains(needle, Qt::CaseInsensitive))
        return true;
    for (const QString& name : server.names) {
        if (name.contains(needle, Qt::CaseInsensitive))
            return true;
    }
    for (const InstanceRef& ref : server.instances) {
        if (ref.name.contains(needle, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

QString sortModeToString(SortMode mode)
{
    switch (mode) {
        case SortMode::Players:
            return QStringLiteral("players");
        case SortMode::Latency:
            return QStringLiteral("latency");
        case SortMode::Name:
            break;
    }
    return QStringLiteral("name");
}

SortMode sortModeFromString(const QString& text)
{
    if (text == QLatin1String("players"))
        return SortMode::Players;
    if (text == QLatin1String("latency"))
        return SortMode::Latency;
    return SortMode::Name;
}

void sortServers(QList<VaultServer>& servers, SortMode mode, const QHash<QString, PingResult>& results)
{
    auto responded = [&](const VaultServer& s) {
        const auto it = results.constFind(s.address);
        return it != results.constEnd() && it->state == PingResult::State::Responded;
    };
    auto byName = [](const VaultServer& a, const VaultServer& b) {
        const int c = a.displayName().localeAwareCompare(b.displayName());
        return c != 0 ? c < 0 : a.address < b.address;
    };
    std::stable_sort(servers.begin(), servers.end(), [&](const VaultServer& a, const VaultServer& b) {
        if (mode != SortMode::Name) {
            const bool ra = responded(a), rb = responded(b);
            if (ra != rb)
                return ra;
            if (ra) {
                const PingResult& pa = results[a.address];
                const PingResult& pb = results[b.address];
                if (mode == SortMode::Players && pa.onlinePlayers != pb.onlinePlayers)
                    return pa.onlinePlayers > pb.onlinePlayers;
                if (mode == SortMode::Latency && pa.latencyMs != pb.latencyMs)
                    return pa.latencyMs < pb.latencyMs;
            }
        }
        return byName(a, b);
    });
}

ServerVaultScanner::ServerVaultScanner(SourceProvider provider, QObject* parent) : QObject(parent), m_provider(std::move(provider)) {}

void ServerVaultScanner::rescan()
{
    const quint64 generation = ++m_generation;
    const QList<InstanceSource> sources = m_provider ? m_provider() : QList<InstanceSource>();
    if (!m_scanning) {
        m_scanning = true;
        emit scanningChanged(true);
    }

    // The pool thread never touches `this`: it posts to the application object (which outlives every scanner) and the pointer is
    // checked there, on the GUI thread.
    QPointer<ServerVaultScanner> self(this);
    QThreadPool::globalInstance()->start([self, sources, generation]() {
        QList<ServerEntry> all;
        QList<SourceError> errors;
        QHash<QString, bool> instancesWithServers;
        for (const InstanceSource& source : sources) {
            const ServersFile file = parseServersDat(source.serversDatPath);
            if (!file.error.isEmpty())
                errors.append({ source.name, file.error });
            for (ServerEntry e : file.entries) {
                e.instanceId = source.id;
                e.instanceName = source.name;
                all.append(e);
                instancesWithServers.insert(source.id, true);
            }
        }
        const QList<VaultServer> grouped = groupByAddress(all);
        const int instanceCount = static_cast<int>(instancesWithServers.size());
        auto* app = QCoreApplication::instance();
        if (!app)
            return;
        QMetaObject::invokeMethod(
            app,
            [self, generation, grouped, instanceCount, errors]() {
                if (self)
                    self->onScanned(generation, grouped, instanceCount, errors);
            },
            Qt::QueuedConnection);
    });
}

void ServerVaultScanner::onScanned(quint64 generation, const QList<VaultServer>& servers, int instanceCount, const QList<SourceError>& errors)
{
    if (generation != m_generation)
        return;  // a newer rescan() superseded this one
    m_servers = servers;
    m_instanceCount = instanceCount;
    m_errors = errors;
    m_hasScanned = true;
    emit listChanged();
    if (m_scanning) {
        m_scanning = false;
        emit scanningChanged(false);
    }
}

}  // namespace Servers
}  // namespace Zero
