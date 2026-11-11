// SPDX-License-Identifier: GPL-3.0-only
#include "FavoriteStar.h"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QPainter>
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {
namespace FavoriteStar {

void paint(QPainter* painter, const QRect& hit, bool favorite, bool rowHovered)
{
    if (!favorite && !rowHovered)
        return;
    const auto& t = Theme::current();
    const QRect glyph(hit.center().x() - kGlyph / 2 + 1, hit.center().y() - kGlyph / 2 + 1, kGlyph, kGlyph);
    if (favorite)
        Icons::filledIcon(QStringLiteral("star"), t.highlight).paint(painter, glyph);
    else
        Icons::icon(QStringLiteral("star"), t.textTertiary).paint(painter, glyph);
}

bool handleMouse(QEvent* event, const QRect& hit, const std::function<void()>& toggle)
{
    const auto type = event->type();
    if (type != QEvent::MouseButtonPress && type != QEvent::MouseButtonRelease && type != QEvent::MouseButtonDblClick)
        return false;
    auto* mouse = static_cast<QMouseEvent*>(event);
    if (mouse->button() != Qt::LeftButton || !hit.contains(mouse->position().toPoint()))
        return false;
    if (type == QEvent::MouseButtonRelease && toggle)
        toggle();
    return true;
}

}  // namespace FavoriteStar

FavoriteButton::FavoriteButton(Favorites::Kind kind, QWidget* parent) : QPushButton(parent), m_kind(kind)
{
    Style::setProperty(this, "zeroVariant", "ghost");
    setIconSize(QSize(16, 16));
    setCursor(Qt::PointingHandCursor);
    connect(this, &QPushButton::clicked, this, [this]() {
        if (auto* store = Favorites::FavoriteStore::instance(); store && !m_key.isEmpty())
            store->toggle(m_kind, m_key);
    });
    if (auto* store = Favorites::FavoriteStore::instance()) {
        connect(store, &Favorites::FavoriteStore::changed, this, [this](Favorites::Kind kind, const QString& key, bool) {
            if (kind == m_kind && key == m_key)
                refresh();
        });
    }
    connect(&Theme::instance(), &Theme::changed, this, &FavoriteButton::refresh);
    refresh();
}

void FavoriteButton::setKey(const QString& key)
{
    m_key = key;
    refresh();
}

void FavoriteButton::refresh()
{
    const auto* store = Favorites::FavoriteStore::instance();
    const bool favorite = store && !m_key.isEmpty() && store->contains(m_kind, m_key);
    const auto& t = Theme::current();
    setIcon(favorite ? Icons::filledIcon(QStringLiteral("star"), t.highlight) : Icons::icon(QStringLiteral("star"), t.textSecondary));
    const bool world = m_kind == Favorites::Kind::World;
    setText(favorite ? QCoreApplication::translate("FavoriteButton", "Favorite")
                     : QCoreApplication::translate("FavoriteButton", "Add to favorites"));
    setToolTip(favorite ? (world ? QCoreApplication::translate("FavoriteButton", "Remove this world from your favorites")
                                 : QCoreApplication::translate("FavoriteButton", "Remove this server from your favorites"))
                        : (world ? QCoreApplication::translate("FavoriteButton", "Favorites stay at the top of the list")
                                 : QCoreApplication::translate("FavoriteButton", "Favorites stay at the top of the list")));
    setVisible(store != nullptr);
}

}  // namespace Zero
