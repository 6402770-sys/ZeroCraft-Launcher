// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - a wide "hero" card for the featured strip of the Discover sections (docs/zero/CONTENT.md, "Modpacks" > Featured
// strip): 16:9 artwork (the pack icon on a tile when it has none), a darker plate at the bottom with name (title role), author,
// two description lines and the Minecraft / loader pills, the provider badge on the artwork and an Install button.
// Reusable: it only needs a ModpackItem. Clicking the card asks to open the item; the button asks to install it.
#pragma once

#include <QPushButton>
#include <QWidget>

#include "zero/content/ModpackSource.h"

namespace Zero {
class RemoteImageCache;
}

namespace Zero::Content {

class ContentHeroCard : public QWidget {
    Q_OBJECT
   public:
    static constexpr int kPlateHeight = 118;

    explicit ContentHeroCard(Zero::RemoteImageCache* images, QWidget* parent = nullptr);

    void setItem(const ModpackItemPtr& item);
    ModpackItemPtr item() const { return m_item; }
    void setInstalled(bool installed);
    bool installed() const { return m_installed; }
    QPushButton* installButton() const { return m_install; }
    void setSelected(bool selected);

    QSize sizeHint() const override { return { 427, 240 }; }

   signals:
    void activated(const Zero::Content::ModpackItemPtr& item);
    void installRequested(const Zero::Content::ModpackItemPtr& item);

   protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

   private:
    void placeButton();

    Zero::RemoteImageCache* m_images;
    ModpackItemPtr m_item;
    QPushButton* m_install;
    bool m_hover = false;
    bool m_installed = false;
    bool m_selected = false;
};

}  // namespace Zero::Content
