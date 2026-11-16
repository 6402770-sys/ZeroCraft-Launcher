// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Server Vault entries of the command palette: one command per (server, instance) pair, built from the
// vault's grouped servers. No pinging and no widgets; "Enter" calls the same quick join the Servers page uses.
#pragma once

#include <QList>
#include <QSet>
#include <functional>

#include "PaletteTypes.h"
#include "zero/servers/ServerVault.h"

namespace Zero {
namespace Palette {

/// "Play on server": launches `instanceId` with a quick join to `address` (ServersPage::playOn).
using JoinFunction = std::function<void(const QString& address, const QString& instanceId)>;

/// Id of the command of one server in one instance: "server:<address>|<instanceId>".
QString serverCommandId(const QString& address, const QString& instanceId);

/// One Kind::Server command per (server, instance). Title = server name, subtitle = "address · instance", the server's own
/// icon when servers.dat has one, else the `compass` glyph. `favoriteAddresses` are normalized addresses (FavoriteStore keys).
QList<PaletteCommand> serverCommands(const QList<Servers::VaultServer>& servers, const QSet<QString>& favoriteAddresses, const JoinFunction& join);

}  // namespace Palette
}  // namespace Zero
