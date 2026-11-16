// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the favorite servers shown on Home: which ones, in which order, and the text of their status pill. No widgets.
#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include "zero/servers/ServerVault.h"

namespace Zero {
namespace Servers {

constexpr int kHomeServersMax = 6;

/// The favorite servers of `all` (already sorted by name by the scanner), at most `max`. `total` (optional) receives how many favorites
/// exist, so the section can offer "See all" when some are left out. Empty when there are no favorites: the section is then hidden.
QList<VaultServer> favoriteServersForHome(const QList<VaultServer>& all, const QSet<QString>& favoriteKeys, int max = kHomeServersMax,
                                          int* total = nullptr);

enum class StatusTone { Checking, Online, Offline };

struct StatusPill {
    StatusTone tone = StatusTone::Checking;
    QString text;
};

/// "Checking…" (no result yet), "Online · 12/100 players" ("Online" when the server did not say), "Offline" (no response or invalid
/// address). `result` is null while no ping answered yet.
StatusPill statusPill(const PingResult* result);

/// The instance that "Join" launches: the first instance saved with the server that is not in `busy`; nullopt when all are busy.
std::optional<InstanceRef> joinInstance(const VaultServer& server, const QSet<QString>& busy);

}  // namespace Servers
}  // namespace Zero
