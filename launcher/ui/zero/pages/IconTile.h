// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - an instance icon on a rounded elevated tile (Home hero, Library details panel).
#pragma once

#include <QAbstractButton>
#include <QIcon>

namespace Zero {

class IconTile : public QAbstractButton {
    Q_OBJECT
   public:
    /// `tileSize` is the width/height of the rounded tile, `iconSize` the size of the icon painted centered on it.
    IconTile(int tileSize, int iconSize, QWidget* parent = nullptr);

    void setInstanceIcon(const QIcon& icon);
    /// A clickable tile gets a pointing-hand cursor and emits clicked(); otherwise it is purely decorative.
    void setClickable(bool clickable);

    QSize sizeHint() const override { return { m_tile, m_tile }; }

   protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

   private:
    int m_tile;
    int m_icon;
    QIcon m_iconImage;
    bool m_clickable = false;
};

}  // namespace Zero
