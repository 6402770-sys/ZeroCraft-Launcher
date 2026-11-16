// SPDX-License-Identifier: GPL-3.0-only
#include "ServerCommands.h"

#include <QCoreApplication>
#include <QIcon>
#include <QPixmap>

#include "ui/zero/pages/ServerRows.h"

namespace Zero {
namespace Palette {

QString serverCommandId(const QString& address, const QString& instanceId)
{
    return QStringLiteral("server:%1|%2").arg(address, instanceId);
}

QList<PaletteCommand> serverCommands(const QList<Servers::VaultServer>& servers, const QSet<QString>& favoriteAddresses, const JoinFunction& join)
{
    QList<PaletteCommand> out;
    for (const Servers::VaultServer& server : servers) {
        QIcon icon;
        if (!server.iconPng.isEmpty()) {
            const QPixmap pm = ServerIcon::pixmap(server.iconPng, 20, 2.0);
            if (!pm.isNull())
                icon = QIcon(pm);
        }
        const bool favorite = favoriteAddresses.contains(server.address);
        for (const Servers::InstanceRef& ref : server.instances) {
            PaletteCommand c;
            c.id = serverCommandId(server.address, ref.id);
            c.kind = Kind::Server;
            c.title = server.displayName();
            c.subtitle = QStringLiteral("%1 · %2").arg(server.address, ref.name);
            c.keywords = { server.address, QCoreApplication::translate("Zero::Palette", "server") };
            if (icon.isNull())
                c.iconName = QStringLiteral("compass");
            else
                c.icon = icon;
            c.favorite = favorite;
            c.hint = QCoreApplication::translate("Zero::Palette", "Join ↵");
            c.primaryLabel = QCoreApplication::translate("Zero::Palette", "Play on server");
            const QString address = server.address, instanceId = ref.id;
            c.run = [join, address, instanceId]() {
                if (join)
                    join(address, instanceId);
            };
            out.append(c);
        }
    }
    return out;
}

}  // namespace Palette
}  // namespace Zero
