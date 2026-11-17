// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroTray.h"

#include <QAction>
#include <QGuiApplication>
#include <QMenu>
#include <QSystemTrayIcon>

namespace Zero {

ZeroTray::ZeroTray(TraySource source, QObject* parent) : QObject(parent), m_source(std::move(source))
{
    m_menu = new QMenu;  // a tray menu has no parent widget; owned here
    m_icon = new QSystemTrayIcon(this);
    m_icon->setContextMenu(m_menu);
    connect(m_menu, &QMenu::aboutToShow, this, &ZeroTray::rebuildMenu);
    // a click or double-click on the icon opens the launcher (the context menu is the right click)
    connect(m_icon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            emit openRequested();
    });
    rebuildMenu();
    refresh();
}

ZeroTray::~ZeroTray()
{
    if (m_icon)
        m_icon->hide();
    delete m_menu;
}

bool ZeroTray::available()
{
    return QSystemTrayIcon::isSystemTrayAvailable();
}

void ZeroTray::show()
{
    m_wantVisible = true;
    refresh();
}

bool ZeroTray::isVisible() const
{
    return m_icon->isVisible();
}

QString ZeroTray::tooltip() const
{
    return Session::trayTooltip(m_source.playingNames ? m_source.playingNames() : QStringList());
}

void ZeroTray::refresh()
{
    const QIcon icon = m_source.icon ? m_source.icon() : QGuiApplication::windowIcon();  // follows the theme like the window icon
    m_icon->setIcon(icon);
    m_icon->setToolTip(tooltip());
    // an icon that arrived after show() (the theme is applied later) makes the tray entry appear then
    if (m_wantVisible && available() && !icon.isNull() && !m_icon->isVisible())
        m_icon->show();
}

void ZeroTray::rebuildMenu()
{
    m_menu->clear();
    const auto entries = Session::buildTrayMenu(m_source.favoriteIds ? m_source.favoriteIds() : QStringList(),
                                                m_source.nameOf, m_source.running ? m_source.running() : QSet<QString>(), m_source.canPlay);
    for (const auto& e : entries) {
        using Kind = Session::TrayEntry::Kind;
        if (e.kind == Kind::Separator) {
            m_menu->addSeparator();
            continue;
        }
        QAction* action = m_menu->addAction(e.text);
        action->setEnabled(e.enabled);
        if (e.kind == Kind::Play) {
            const QString id = e.instanceId;
            connect(action, &QAction::triggered, this, [this, id]() { emit playRequested(id); });
        } else if (e.kind == Kind::Hint) {
            action->setEnabled(false);
        } else if (e.kind == Kind::Open) {
            connect(action, &QAction::triggered, this, &ZeroTray::openRequested);
        } else {
            connect(action, &QAction::triggered, this, &ZeroTray::quitRequested);
        }
    }
    refresh();
}

}  // namespace Zero
