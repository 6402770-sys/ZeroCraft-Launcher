// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - what the launcher does while a game runs ("While playing" setting) and the model of the tray menu. No widgets: the
// windows are reached through injected functions, so the policy can be tested with fakes.
#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>

namespace Zero {
namespace Session {

/// ZeroWhilePlaying setting values.
enum class WhilePlayingMode { Tray, Open, Close };

QString whilePlayingToString(WhilePlayingMode mode);
/// Unknown / empty text gives Tray (the default).
WhilePlayingMode whilePlayingFromString(const QString& text);

/// The window side of the policy.
struct WindowHooks {
    std::function<WhilePlayingMode()> mode;
    std::function<bool()> mainWindowVisible;
    std::function<void()> hideMainWindow;
    std::function<void()> showMainWindow;
    /// Hides (`false`) or shows (`true`) the console windows of an instance.
    std::function<void(const QString& instanceId, bool visible)> setInstanceWindowsVisible;
};

/// "Hide to the tray": the main window goes away when the first game starts and comes back when the last one ends (or when one crashes),
/// unless the user brought it back already. "Keep it open" does nothing; "Close the launcher" is Prism's CloseAfterLaunch behaviour and
/// is not handled here.
class WhilePlaying : public QObject {
    Q_OBJECT
   public:
    explicit WhilePlaying(WindowHooks hooks, QObject* parent = nullptr);

    /// The game of `instanceId` is up.
    void onGameStarted(const QString& instanceId);
    /// The launch of `instanceId` ended (`crashed`: the game ended abnormally without the user stopping it).
    void onGameEnded(const QString& instanceId, bool crashed);

    bool isHidden() const { return m_hiddenMain; }
    int runningCount() const { return int(m_running.size()); }

   private:
    WindowHooks m_hooks;
    QSet<QString> m_running;
    QSet<QString> m_windowsHidden;
    bool m_hiddenMain = false;
};

/// One line of the tray menu.
struct TrayEntry {
    enum class Kind { Play, Hint, Separator, Open, Quit };
    Kind kind = Kind::Separator;
    QString instanceId;  ///< Play only
    QString text;
    bool enabled = true;
};

constexpr int kTrayMaxInstances = 5;

/// "Play <name>" for up to kTrayMaxInstances favorites (in the order given, duplicates and ids without a name skipped), a separator,
/// "Open ZERO Launcher" and "Quit". With no favorite a disabled hint entry takes the place of the Play entries. A running instance's
/// entry is disabled; `canPlay` (optional) disables the instances that cannot be launched.
QList<TrayEntry> buildTrayMenu(const QStringList& favoriteIds, const std::function<QString(const QString&)>& nameOf,
                               const QSet<QString>& running, const std::function<bool(const QString&)>& canPlay = {});

/// "ZERO Launcher", or "ZERO Launcher - Playing <name>" (several: "Playing <a> and N more").
QString trayTooltip(const QStringList& playingNames);

}  // namespace Session
}  // namespace Zero
