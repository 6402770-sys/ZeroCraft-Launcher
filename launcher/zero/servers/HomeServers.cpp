// SPDX-License-Identifier: GPL-3.0-only
#include "HomeServers.h"

#include <QObject>

namespace Zero {
namespace Servers {

QList<VaultServer> favoriteServersForHome(const QList<VaultServer>& all, const QSet<QString>& favoriteKeys, int max, int* total)
{
    QList<VaultServer> out;
    int count = 0;
    for (const auto& s : all) {
        if (!favoriteKeys.contains(s.address))
            continue;
        ++count;
        if (out.size() < max)
            out.append(s);
    }
    if (total)
        *total = count;
    return out;
}

StatusPill statusPill(const PingResult* result)
{
    if (!result)
        return { StatusTone::Checking, QObject::tr("Checking…") };
    if (result->state != PingResult::State::Responded)
        return { StatusTone::Offline, QObject::tr("Offline") };
    if (result->onlinePlayers >= 0 && result->maxPlayers >= 0)
        return { StatusTone::Online, QObject::tr("Online · %1/%2 players").arg(result->onlinePlayers).arg(result->maxPlayers) };
    return { StatusTone::Online, QObject::tr("Online") };
}

std::optional<InstanceRef> joinInstance(const VaultServer& server, const QSet<QString>& busy)
{
    for (const auto& ref : server.instances) {
        if (!busy.contains(ref.id))
            return ref;
    }
    return std::nullopt;
}

}  // namespace Servers
}  // namespace Zero
