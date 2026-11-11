// SPDX-License-Identifier: GPL-3.0-only
#include "ServerRows.h"

#include <QAbstractItemView>
#include <QCache>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include "ui/zero/PillTag.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {

namespace {
constexpr int kPadding = Tokens::spaceMd;  // inside the row, left and right
constexpr int kGap = Tokens::spaceMd;      // between icon, text and the status column

QString tr(const char* text)
{
    return QCoreApplication::translate("Zero::ServerRows", text);
}

QCache<QString, QPixmap>& iconCache()
{
    static QCache<QString, QPixmap> instance(128);
    return instance;
}

/// The address is monospace in every theme: no role (a role would put the theme font back on every theme change).
QFont monoCaptionFont()
{
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPixelSize(Tokens::caption.px);
    return mono;
}

QString instancesText(const Servers::VaultServer& server)
{
    QStringList names;
    for (const auto& ref : server.instances)
        names << ref.name;
    return tr("In %1").arg(names.join(QStringLiteral(", ")));
}
}  // namespace

QString serverStatusDisclaimer()
{
    return tr("A response only means the server answered a status request. It doesn't guarantee you can join.");
}

QString playersText(const Servers::PingResult& r)
{
    if (r.onlinePlayers < 0)
        return {};
    if (r.maxPlayers >= 0)
        return tr("%1 / %2 players").arg(r.onlinePlayers).arg(r.maxPlayers);
    return r.onlinePlayers == 1 ? tr("1 player") : tr("%1 players").arg(r.onlinePlayers);
}

QString latencyText(const Servers::PingResult& r)
{
    return r.latencyMs >= 0 ? tr("%1 ms").arg(r.latencyMs) : QString();
}

QString summaryText(const Servers::PingResult& r)
{
    QStringList parts;
    if (const QString players = playersText(r); !players.isEmpty())
        parts << players;
    if (const QString latency = latencyText(r); !latency.isEmpty())
        parts << latency;
    return parts.join(QStringLiteral(" · "));
}

namespace ServerIcon {

QPixmap pixmap(const QByteArray& png, int size, qreal dpr)
{
    if (png.isEmpty() || size <= 0)
        return {};
    const QString key = QStringLiteral("%1|%2|%3").arg(QString::fromLatin1(QCryptographicHash::hash(png, QCryptographicHash::Md5).toHex())).arg(size).arg(dpr);
    if (QPixmap* hit = iconCache().object(key))
        return *hit;
    QPixmap source;
    QPixmap scaled;  // stays null (and cached as such) when the bytes are not an image
    if (source.loadFromData(png)) {
        const int device = qRound(size * dpr);
        scaled = source.scaled(device, device, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        scaled.setDevicePixelRatio(dpr);
    }
    iconCache().insert(key, new QPixmap(scaled));
    return scaled;
}

void clearCache()
{
    iconCache().clear();
}

void paintTile(QPainter* painter, const QRectF& rect, const QByteArray& png, int glyphSize, qreal dpr)
{
    const auto& t = Theme::current();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QPainterPath shape = Paint::panelPath(rect, t.radiusMd);
    painter->setPen(Qt::NoPen);
    painter->setBrush(t.elevated);
    painter->drawPath(shape);

    const QPixmap icon = pixmap(png, qRound(rect.width()), dpr);
    if (!icon.isNull()) {
        painter->setClipPath(shape);
        painter->drawPixmap(rect.topLeft(), icon);
    } else {
        const QRectF glyph(rect.center().x() - glyphSize / 2.0, rect.center().y() - glyphSize / 2.0, glyphSize, glyphSize);
        Icons::icon(QStringLiteral("compass"), t.textTertiary).paint(painter, glyph.toRect());
    }
    painter->restore();
}

}  // namespace ServerIcon

ServerIconTile::ServerIconTile(int tileSize, int glyphSize, QWidget* parent) : QWidget(parent), m_tile(tileSize), m_glyph(glyphSize)
{
    setFixedSize(tileSize, tileSize);
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void ServerIconTile::setIcon(const QByteArray& png)
{
    if (m_png == png)
        return;
    m_png = png;
    update();
}

void ServerIconTile::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    ServerIcon::paintTile(&p, QRectF(0, 0, m_tile, m_tile), m_png, m_glyph, devicePixelRatioF());
}

QVariant ServerRowModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Servers::VaultServer& s = m_rows.at(index.row());
    switch (role) {
        case Qt::DisplayRole:
        case Qt::AccessibleTextRole:
            return s.displayName();
        case Qt::ToolTipRole: {
            QString tip = s.names.size() > 1 ? QStringLiteral("%1\n%2").arg(s.names.join(QStringLiteral(" / ")), s.address) : s.address;
            if (m_pinger) {
                const auto it = m_pinger->results().constFind(s.address);
                if (it != m_pinger->results().constEnd() && it->state == Servers::PingResult::State::Responded)
                    tip += QStringLiteral("\n\n") + serverStatusDisclaimer();
            }
            return tip;
        }
        case AddressRole:
            return s.address;
        default:
            return {};
    }
}

void ServerRowModel::setServers(const QList<Servers::VaultServer>& servers)
{
    beginResetModel();
    m_rows = servers;
    endResetModel();
}

int ServerRowModel::rowForAddress(const QString& address) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).address == address)
            return i;
    }
    return -1;
}

void ServerRowModel::statusChanged(const QString& address)
{
    const int row = rowForAddress(address);
    if (row >= 0)
        emit dataChanged(index(row), index(row));
}

void ServerRowModel::allStatusChanged()
{
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(static_cast<int>(m_rows.size()) - 1));
}

QSize ServerRowDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const
{
    return { 0, kRowHeight + kRowGap };
}

void ServerRowDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const auto* model = qobject_cast<const ServerRowModel*>(index.model());
    if (!model || index.row() >= model->rowCount())
        return;
    const Servers::VaultServer& s = model->server(index.row());
    const auto& t = Theme::current();
    const qreal dpr = option.widget ? option.widget->devicePixelRatioF() : 1.0;
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;

    const QRect row = QRect(option.rect.left(), option.rect.top() + kRowGap / 2, option.rect.width(), kRowHeight);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // background: hover surfaceHover, selected elevated + 1 px accent border
    if (selected || hovered) {
        const QRectF frame = QRectF(row).adjusted(0.5, 0.5, -0.5, -0.5);
        painter->setPen(selected ? QPen(t.accent, 1) : QPen(Qt::NoPen));
        painter->setBrush(selected ? t.elevated : t.surfaceHover);
        painter->drawPath(Paint::panelPath(frame, t.radiusMd));
    }

    const int centerY = row.center().y();
    const QRect inner = row.adjusted(kPadding, 0, -kPadding, 0);

    // icon tile
    const QRectF tile(inner.left(), centerY - kIconSize / 2.0 + 0.5, kIconSize, kIconSize);
    ServerIcon::paintTile(painter, tile, s.iconPng, 22, dpr);

    // status column (right): pill + "players · latency", or "Pinging…"
    const QFont captionFont = Style::font(Role::Caption);
    const QFontMetrics captionMetrics(captionFont);
    const int lineHeight = captionMetrics.height();
    QString pillText, caption;
    PillTag::Tone tone = PillTag::Tone::Plain;
    bool pinging = false;
    const Servers::ServerPinger* pinger = model->pinger();
    const Servers::PingResult* result = nullptr;
    if (pinger) {
        const auto it = pinger->results().constFind(s.address);
        if (it != pinger->results().constEnd())
            result = &it.value();
    }
    if (result) {
        switch (result->state) {
            case Servers::PingResult::State::Responded:
                pillText = tr("Responded");
                tone = PillTag::Tone::Success;
                caption = summaryText(*result);
                break;
            case Servers::PingResult::State::NoResponse:
                pillText = tr("No response");
                break;
            case Servers::PingResult::State::InvalidAddress:
                pillText = tr("Invalid address");
                tone = PillTag::Tone::Warning;
                break;
        }
    } else if (pinger && pinger->isPending(s.address)) {
        pinging = true;
        caption = tr("Pinging…");
    }

    int rightWidth = 0;
    if (!pillText.isEmpty())
        rightWidth = PillTag::widthForText(pillText);
    if (!caption.isEmpty())
        rightWidth = qMax(rightWidth, captionMetrics.horizontalAdvance(caption));
    rightWidth = qMin(rightWidth, qMax(120, inner.width() / 2));
    const int rightLeft = inner.right() + 1 - rightWidth;
    if (pinging) {
        painter->setFont(captionFont);
        painter->setPen(t.textTertiary);
        painter->drawText(QRect(rightLeft, centerY - lineHeight / 2, rightWidth, lineHeight), Qt::AlignRight | Qt::AlignVCenter, caption);
    } else if (!pillText.isEmpty()) {
        const int pillHeight = Tokens::badgeHeight;
        const int block = pillHeight + (caption.isEmpty() ? 0 : Tokens::spaceXs + lineHeight);
        const int top = centerY - block / 2;
        const int pillWidth = qMin(PillTag::widthForText(pillText), rightWidth);
        PillTag::paintPill(painter, QRectF(inner.right() + 1 - pillWidth, top, pillWidth, pillHeight), pillText, tone);
        if (!caption.isEmpty()) {
            painter->setFont(captionFont);
            painter->setPen(t.textSecondary);
            painter->drawText(QRect(rightLeft, top + pillHeight + Tokens::spaceXs, rightWidth, lineHeight), Qt::AlignRight | Qt::AlignVCenter,
                              captionMetrics.elidedText(caption, Qt::ElideLeft, rightWidth));
        }
    }

    // name, address (monospace), "In <instances>"
    QFont nameFont = Style::font(Role::Label);
    nameFont.setWeight(QFont::DemiBold);
    const QFontMetrics nameMetrics(nameFont);
    const QFont monoFont = monoCaptionFont();
    const QFontMetrics monoMetrics(monoFont);
    const int nameLeft = int(tile.right()) + 1 + kGap;
    const int textWidth = qMax(0, (rightWidth > 0 ? rightLeft - kGap : inner.right() + 1) - nameLeft);
    const int blockHeight = nameMetrics.height() + 2 + monoMetrics.height() + 1 + lineHeight;
    int y = centerY - blockHeight / 2;
    painter->setFont(nameFont);
    painter->setPen(t.text);
    painter->drawText(QRect(nameLeft, y, textWidth, nameMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                      nameMetrics.elidedText(s.displayName(), Qt::ElideRight, textWidth));
    y += nameMetrics.height() + 2;
    painter->setFont(monoFont);
    painter->setPen(t.textSecondary);
    painter->drawText(QRect(nameLeft, y, textWidth, monoMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                      monoMetrics.elidedText(s.address, Qt::ElideRight, textWidth));
    y += monoMetrics.height() + 1;
    painter->setFont(captionFont);
    painter->setPen(t.textTertiary);
    painter->drawText(QRect(nameLeft, y, textWidth, lineHeight), Qt::AlignLeft | Qt::AlignVCenter,
                      captionMetrics.elidedText(instancesText(s), Qt::ElideRight, textWidth));
    painter->restore();
}

}  // namespace Zero
