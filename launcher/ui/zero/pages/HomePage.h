// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Home page: continue-playing hero, favorites, recently played cards, shortcuts. See docs/zero/SHELL.md.
#pragma once

#include <QList>
#include <QWidget>
#include "ui/zero/shell/ShellPage.h"

class MinecraftInstance;
class QGridLayout;
class QLabel;
class QPushButton;
class QStackedLayout;
class QTimer;

namespace Zero {

class ArtworkView;
class ElidedLabel;
class IconTile;
class ZeroPlayButton;
class ZeroCard;

class HomePage : public ShellPage {
    Q_OBJECT
   public:
    explicit HomePage(QWidget* parent = nullptr);

    QString title() const override { return tr("Home"); }

   signals:
    void playRequested(const QString& instanceId);
    void stopRequested(const QString& instanceId);
    void editRequested(const QString& instanceId);
    void folderRequested(const QString& instanceId);
    /// A recently played card was clicked: select the instance in the Library and show it.
    void openInLibraryRequested(const QString& instanceId);
    void viewLibraryRequested();
    void newInstanceRequested();
    void importRequested();
    void accountsRequested();
    void instancesFolderRequested();

   protected:
    void showEvent(QShowEvent* event) override;

   private:
    void scheduleRefresh();
    void refresh();
    void refreshHero(MinecraftInstance* hero);
    void rebuildCards(QWidget* section, QGridLayout* grid, QStringList& signature, const QList<MinecraftInstance*>& instances);
    void refreshIcons();

    QStackedLayout* m_stack;  // 0 = empty library, 1 = content

    // hero
    ZeroCard* m_hero;
    IconTile* m_heroIcon;
    ArtworkView* m_heroArt;
    ElidedLabel* m_heroName;
    ElidedLabel* m_heroMeta;
    ZeroPlayButton* m_heroPlay;
    QPushButton* m_heroEdit;
    QPushButton* m_heroFolder;
    QString m_heroId;

    // favorites (above recent, hidden when there are none)
    QWidget* m_favSection;
    QGridLayout* m_favGrid;
    QStringList m_favSignature;

    // recent
    QWidget* m_recentSection;
    QGridLayout* m_recentGrid;
    QStringList m_recentSignature;

    QTimer* m_refreshTimer;
};

}  // namespace Zero
