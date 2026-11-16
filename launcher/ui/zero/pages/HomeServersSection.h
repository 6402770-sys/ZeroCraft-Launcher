// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the "Servers" section of Home: the favorite servers (from the Server Vault) as compact rows with a status pill and a
// Join button. Pings when the section is shown, never on a timer. Hidden while there are no favorite servers.
#pragma once

#include <QWidget>
#include <functional>
#include "zero/servers/ServerPinger.h"
#include "zero/servers/ServerVault.h"

class QVBoxLayout;

namespace Zero {

namespace Favorites {
class FavoriteStore;
}

class ZeroCard;

class HomeServersSection : public QWidget {
    Q_OBJECT
   public:
    /// `provider` lists the servers.dat files to read (default: every instance of the application); `store` holds the favorites
    /// (default: the application's store).
    explicit HomeServersSection(Servers::ServerVaultScanner::SourceProvider provider = {}, Favorites::FavoriteStore* store = nullptr,
                                QWidget* parent = nullptr);

    /// The page became visible: reads the servers and pings the favorites (nothing when there are none).
    void activate();
    /// Rows shown (0 = the section is hidden).
    int rowCount() const { return m_rows; }
    /// Rows whose server has a Join target; for tests.
    QStringList shownAddresses() const { return m_addresses; }
    bool seeAllVisible() const;

    Servers::ServerPinger* pinger() const { return m_pinger; }
    Servers::ServerVaultScanner* scanner() const { return m_scanner; }

   signals:
    void seeAllRequested();
    void joinRequested(const QString& address, const QString& instanceId);

   private:
    void rebuild();
    bool isBusy(const QString& instanceId) const;

    Servers::ServerVaultScanner* m_scanner;
    Servers::ServerPinger* m_pinger;
    Favorites::FavoriteStore* m_store;
    ZeroCard* m_card;
    QVBoxLayout* m_rowsLayout;
    QWidget* m_seeAll;
    QStringList m_addresses;
    int m_rows = 0;
};

}  // namespace Zero
