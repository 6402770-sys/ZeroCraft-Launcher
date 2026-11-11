// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the Servers list: a flat model of VaultServer rows, the delegate that paints them (one QListView, no per-row widgets)
// and the server icon helpers shared with the details panel.
#pragma once

#include <QAbstractListModel>
#include <QPixmap>
#include <QStyledItemDelegate>
#include <QWidget>
#include "zero/servers/ServerPinger.h"
#include "zero/servers/ServerVault.h"

class QPainter;

namespace Zero {

namespace ServerIcon {
/// The saved PNG scaled to size x size device-independent pixels with nearest-neighbour (the 64x64 server icons stay crisp); null when
/// `png` is empty or not an image. Cached.
QPixmap pixmap(const QByteArray& png, int size, qreal dpr);
/// Rounded `elevated` tile with the icon (or the placeholder `compass` glyph) inside.
void paintTile(QPainter* painter, const QRectF& rect, const QByteArray& png, int glyphSize, qreal dpr);
void clearCache();
}  // namespace ServerIcon

class ServerIconTile : public QWidget {
    Q_OBJECT
   public:
    ServerIconTile(int tileSize, int glyphSize, QWidget* parent = nullptr);
    void setIcon(const QByteArray& png);

   protected:
    void paintEvent(QPaintEvent*) override;

   private:
    int m_tile;
    int m_glyph;
    QByteArray m_png;
};

/// The text every status shows: a response is not a promise that you can join.
QString serverStatusDisclaimer();

class ServerRowModel : public QAbstractListModel {
    Q_OBJECT
   public:
    enum Role { AddressRole = Qt::UserRole + 1 };

    explicit ServerRowModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(m_rows.size()); }
    QVariant data(const QModelIndex& index, int role) const override;

    /// Replaces every row (model reset).
    void setServers(const QList<Servers::VaultServer>& servers);
    const Servers::VaultServer& server(int row) const { return m_rows.at(row); }
    const QList<Servers::VaultServer>& servers() const { return m_rows; }
    int rowForAddress(const QString& address) const;

    /// The pinger the rows read their status from (not owned).
    void setPinger(const Servers::ServerPinger* pinger) { m_pinger = pinger; }
    const Servers::ServerPinger* pinger() const { return m_pinger; }
    /// A ping result arrived for `address` (or its pending state changed): repaints that row.
    void statusChanged(const QString& address);
    /// Repaints every row (a refresh started).
    void allStatusChanged();

   private:
    QList<Servers::VaultServer> m_rows;
    const Servers::ServerPinger* m_pinger = nullptr;
};

class ServerRowDelegate : public QStyledItemDelegate {
    Q_OBJECT
   public:
    static constexpr int kRowHeight = 72;
    static constexpr int kRowGap = 2;
    static constexpr int kIconSize = 48;

    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

/// "123 / 500 players", "123 players", or empty when the server did not report players.
QString playersText(const Servers::PingResult& result);
/// "45 ms", or empty when there is no latency.
QString latencyText(const Servers::PingResult& result);
/// "123 / 500 players · 45 ms" (the parts that exist).
QString summaryText(const Servers::PingResult& result);

}  // namespace Zero
