// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Home page: continue-playing hero, favorites, recently played cards, shortcuts. See docs/zero/SHELL.md.
#pragma once

#include <QHash>
#include <QList>
#include <QWidget>
#include <optional>
#include "ui/zero/shell/ShellPage.h"

class MinecraftInstance;
class QGridLayout;
class QLabel;
class QPushButton;
class QStackedLayout;
class QVBoxLayout;
class QTimer;

namespace Zero {

class ArtworkView;
class ElidedLabel;
class IconTile;
class ZeroPlayButton;
class ZeroCard;
class NowPlayingCard;
class LastSessionCard;
class HomeServersSection;

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
    /// "Diagnose" on a crashed / failed session row: open Crash Detective for the instance and analyze now.
    void diagnoseRequested(const QString& instanceId, std::optional<int> exitCode);
    void viewLibraryRequested();
    void newInstanceRequested();
    void importRequested();
    void accountsRequested();
    void instancesFolderRequested();
    /// "Screenshots" of a Now playing card, or the "+N" tile of the Last session card: show that instance's screenshots.
    void screenshotsRequested(const QString& instanceId);
    /// A thumbnail of the Last session card: open this screenshot in the viewer.
    void screenshotRequested(const QString& path);
    void serversRequested();

   public:
    // for tests
    NowPlayingCard* nowPlayingCard(const QString& instanceId) const { return m_nowCards.value(instanceId); }
    int nowPlayingCount() const { return int(m_nowCards.size()); }
    LastSessionCard* lastSessionCard() const { return m_lastCard; }
    HomeServersSection* serversSection() const { return m_servers; }
    /// Re-reads the running sessions and the last session now (normally done by their signals).
    void syncSessionCards();

   protected:
    void showEvent(QShowEvent* event) override;

   private:
    void scheduleRefresh();
    void refresh();
    void refreshHero(MinecraftInstance* hero);
    void rebuildCards(QWidget* section, QGridLayout* grid, QStringList& signature, const QList<MinecraftInstance*>& instances);
    void refreshIcons();
    void rebuildActivity();
    void syncNowPlaying();
    void syncLastSession();

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

    // now playing / last session (above the hero)
    QWidget* m_nowHolder;
    QVBoxLayout* m_nowLayout;
    QHash<QString, NowPlayingCard*> m_nowCards;
    QWidget* m_lastHolder;
    QVBoxLayout* m_lastLayout;
    LastSessionCard* m_lastCard = nullptr;
    QStringList m_lastSignature;

    // favorite servers (after favorites; hides itself when there are none)
    HomeServersSection* m_servers;

    // recent
    QWidget* m_recentSection;
    QGridLayout* m_recentGrid;
    QStringList m_recentSignature;

    // recent activity (session history; hidden when there is nothing to show)
    QWidget* m_activitySection;
    ZeroCard* m_activityCard;
    QVBoxLayout* m_activityRows;
    QStringList m_activitySignature;

    QTimer* m_refreshTimer;
};

}  // namespace Zero
