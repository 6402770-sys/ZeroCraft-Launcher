// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tray icon: the launcher keeps a presence in the notification area while it runs, with a menu to play a favorite
// instance, open the launcher or quit. The menu content comes from Zero::Session::buildTrayMenu(). See docs/zero/SHELL.md ("Tray").
#pragma once

#include <QIcon>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <functional>
#include "zero/session/WhilePlaying.h"

class QMenu;
class QSystemTrayIcon;

namespace Zero {

/// What the tray needs to know; everything is read when the menu opens (and when the tooltip is refreshed).
struct TraySource {
    std::function<QStringList()> favoriteIds;
    std::function<QStringList()> recentIds;  ///< most recently played first
    std::function<QString(const QString&)> nameOf;
    std::function<QSet<QString>()> running;
    std::function<QStringList()> playingNames;
    std::function<bool(const QString&)> canPlay;
    std::function<QIcon()> icon;  ///< the themed app icon (default: the application window icon)
};

class ZeroTray : public QObject {
    Q_OBJECT
   public:
    explicit ZeroTray(TraySource source, QObject* parent = nullptr);
    ~ZeroTray() override;

    /// True when the system has a notification area (nothing is created or shown otherwise).
    static bool available();

    /// Shows the icon (only when available()).
    void show();
    bool isVisible() const;

    QMenu* menu() const { return m_menu; }
    QString tooltip() const;
    /// Rebuilds the menu from the source (done on every aboutToShow).
    void rebuildMenu();
    /// Re-reads the tooltip and the icon (theme).
    void refresh();

   signals:
    void openRequested();
    void quitRequested();
    void playRequested(const QString& instanceId);

   private:
    TraySource m_source;
    QSystemTrayIcon* m_icon = nullptr;
    QMenu* m_menu = nullptr;
    bool m_wantVisible = false;
};

}  // namespace Zero
