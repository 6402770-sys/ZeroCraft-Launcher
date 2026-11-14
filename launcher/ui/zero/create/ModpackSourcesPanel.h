// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the Modpack body of Create Instance: source tiles (Modrinth, CurseForge when the build supports it,
// Technic), "Browse all modpacks" and "More sources" (ATLauncher, FTB, FTB App import, Legacy FTB). Every entry opens the
// matching page of the classic New Instance dialog for now (`pageRequested`); "Browse all modpacks" asks for the modpack
// browser (`browseAllRequested`). Provider logos come from the icon theme the classic pages use; a Lucide glyph stands in
// when the theme has none.
#pragma once

#include <QList>
#include <QPair>
#include <QWidget>

class QMenu;
class QPushButton;

namespace Zero {
namespace Create {

class ModpackSourcesPanel : public QWidget {
    Q_OBJECT
   public:
    /// `curseForge` adds the CurseForge tile (only when the build has a CurseForge API key).
    explicit ModpackSourcesPanel(bool curseForge, QWidget* parent = nullptr);

    /// NewInstanceDialog page ids of the tiles, in order ("modrinth", "flame", "technic").
    QStringList tileIds() const;
    /// Page ids of the "More sources" menu, in order ("atl", "ftb", "import_ftb", "legacy_ftb").
    QStringList moreIds() const { return m_moreIds; }
    QWidget* tile(const QString& pageId) const;
    QPushButton* browseAllButton() const { return m_browseAll; }
    QPushButton* moreButton() const { return m_more; }
    QMenu* moreMenu() const { return m_menu; }

   signals:
    void pageRequested(const QString& pageId);
    void browseAllRequested();

   private:
    void refreshIcons();

    QList<QPair<QString, QWidget*>> m_tiles;
    QStringList m_moreIds;
    QPushButton* m_browseAll;
    QPushButton* m_more;
    QMenu* m_menu;
};

}  // namespace Create
}  // namespace Zero
