// SPDX-License-Identifier: GPL-3.0-only
#include "WhilePlaying.h"

namespace Zero {
namespace Session {

QString whilePlayingToString(WhilePlayingMode mode)
{
    switch (mode) {
        case WhilePlayingMode::Open:
            return QStringLiteral("open");
        case WhilePlayingMode::Close:
            return QStringLiteral("close");
        case WhilePlayingMode::Tray:
            break;
    }
    return QStringLiteral("tray");
}

WhilePlayingMode whilePlayingFromString(const QString& text)
{
    if (text == QLatin1String("open"))
        return WhilePlayingMode::Open;
    if (text == QLatin1String("close"))
        return WhilePlayingMode::Close;
    return WhilePlayingMode::Tray;
}

WhilePlaying::WhilePlaying(WindowHooks hooks, QObject* parent) : QObject(parent), m_hooks(std::move(hooks)) {}

void WhilePlaying::onGameStarted(const QString& instanceId)
{
    const bool first = m_running.isEmpty();
    m_running.insert(instanceId);
    if (!m_hooks.mode || m_hooks.mode() != WhilePlayingMode::Tray)
        return;
    if (m_hooks.setInstanceWindowsVisible) {
        m_windowsHidden.insert(instanceId);
        m_hooks.setInstanceWindowsVisible(instanceId, false);
    }
    // only the first game hides the launcher: a window the user brought back during another game stays
    if (first && m_hooks.hideMainWindow && (!m_hooks.mainWindowVisible || m_hooks.mainWindowVisible())) {
        m_hiddenMain = true;
        m_hooks.hideMainWindow();
    }
}

void WhilePlaying::onGameEnded(const QString& instanceId, bool crashed)
{
    m_running.remove(instanceId);
    if (m_windowsHidden.remove(instanceId) && m_hooks.setInstanceWindowsVisible)
        m_hooks.setInstanceWindowsVisible(instanceId, true);
    if (!m_hiddenMain)
        return;
    if (!m_running.isEmpty() && !crashed)
        return;
    m_hiddenMain = false;
    if (m_hooks.showMainWindow && (!m_hooks.mainWindowVisible || !m_hooks.mainWindowVisible()))
        m_hooks.showMainWindow();
}

QList<TrayEntry> buildTrayMenu(const QStringList& favoriteIds, const QStringList& recentIds,
                               const std::function<QString(const QString&)>& nameOf, const QSet<QString>& running,
                               const std::function<bool(const QString&)>& canPlay)
{
    QList<TrayEntry> out;
    QSet<QString> seen;
    int count = 0;
    for (const QStringList* list : { &favoriteIds, &recentIds }) {
        for (const QString& id : *list) {
            if (count >= kTrayMaxInstances)
                break;
            if (seen.contains(id))
                continue;
            const QString name = nameOf ? nameOf(id) : id;
            if (name.isEmpty())
                continue;
            seen.insert(id);
            TrayEntry e;
            e.kind = TrayEntry::Kind::Play;
            e.instanceId = id;
            e.text = QObject::tr("Play %1").arg(name);
            e.enabled = !running.contains(id) && (!canPlay || canPlay(id));
            out.append(e);
            ++count;
        }
    }
    if (!out.isEmpty())
        out.append({ TrayEntry::Kind::Separator, {}, {}, true });
    out.append({ TrayEntry::Kind::Open, {}, QObject::tr("Open ZERO Launcher"), true });
    out.append({ TrayEntry::Kind::Quit, {}, QObject::tr("Quit"), true });
    return out;
}

QString trayTooltip(const QStringList& playingNames)
{
    if (playingNames.isEmpty())
        return QObject::tr("ZERO Launcher");
    if (playingNames.size() == 1)
        return QObject::tr("ZERO Launcher — Playing %1").arg(playingNames.first());
    return QObject::tr("ZERO Launcher — Playing %1 and %2 more").arg(playingNames.first()).arg(playingNames.size() - 1);
}

}  // namespace Session
}  // namespace Zero
