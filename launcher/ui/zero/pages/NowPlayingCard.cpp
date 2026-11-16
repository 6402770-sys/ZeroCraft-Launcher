// SPDX-License-Identifier: GPL-3.0-only
#include "NowPlayingCard.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include "ArtworkCache.h"
#include "ElidedLabel.h"
#include "IconTile.h"
#include "ui/zero/ZeroFormat.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "zero/session/PlayTracker.h"

namespace Zero {

NowPlayingCard::NowPlayingCard(const QString& instanceId, const QString& name, qint64 startedMs, qint64 pid, NowFn now, MemoryFn memory,
                               QWidget* parent)
    : ZeroCard(parent), m_id(instanceId), m_startedMs(startedMs), m_pid(pid), m_now(std::move(now)), m_memory(std::move(memory))
{
    setHoverEnabled(false);
    setContentsMargins(0, 0, 0, 0);  // the layout below sets the padding
    setMinimumHeight(96);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg);
    row->setSpacing(Tokens::spaceLg);

    m_icon = new IconTile(64, 48, this);
    row->addWidget(m_icon, 0, Qt::AlignVCenter);
    m_art = new ArtworkView(this);
    m_art->setFixedSize(128, 72);
    m_art->hide();
    row->addWidget(m_art, 0, Qt::AlignVCenter);

    auto* textColumn = new QWidget(this);
    auto* texts = new QVBoxLayout(textColumn);
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(Tokens::spaceXs);
    auto* overline = new QLabel(tr("Now playing"), this);
    Style::applyRole(overline, Role::Overline);
    Style::setProperty(overline, "zeroTone", "success");
    texts->addWidget(overline);
    m_name = new ElidedLabel(this);
    Style::applyRole(m_name, Role::Heading);
    m_name->setFullText(name);
    texts->addWidget(m_name);
    m_meta = new ElidedLabel(this);
    Style::applyRole(m_meta, Role::Caption);
    texts->addWidget(m_meta);
    row->addWidget(textColumn, 1, Qt::AlignVCenter);

    m_screenshots = new QPushButton(tr("Screenshots"), this);
    Style::setProperty(m_screenshots, "zeroVariant", "secondary");
    m_screenshots->setIconSize(QSize(16, 16));
    connect(m_screenshots, &QPushButton::clicked, this, [this]() { emit screenshotsRequested(m_id); });
    m_folder = new QPushButton(tr("Folder"), this);
    Style::setProperty(m_folder, "zeroVariant", "ghost");
    m_folder->setIconSize(QSize(16, 16));
    connect(m_folder, &QPushButton::clicked, this, [this]() { emit folderRequested(m_id); });
    m_stop = new QPushButton(tr("Stop"), this);
    Style::setProperty(m_stop, "zeroVariant", "danger");
    m_stop->setToolTip(tr("Force the game to close right away. Progress since the last autosave is lost."));
    connect(m_stop, &QPushButton::clicked, this, [this]() { emit stopRequested(m_id); });
    row->addWidget(m_screenshots, 0, Qt::AlignVCenter);
    row->addWidget(m_folder, 0, Qt::AlignVCenter);
    row->addWidget(m_stop, 0, Qt::AlignVCenter);

    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &NowPlayingCard::tick);

    connect(&Theme::instance(), &Theme::changed, this, &NowPlayingCard::refreshIcons);
    refreshIcons();
    tick();
}

QString NowPlayingCard::elapsedText() const
{
    const qint64 now = m_now ? m_now() : m_startedMs;
    return Session::formatClock((now - m_startedMs) / 1000);
}

QString NowPlayingCard::memoryText() const
{
    if (!m_memory || m_pid <= 0)
        return {};
    const auto bytes = m_memory(m_pid);
    return bytes ? Format::bytes(*bytes) : QString();
}

bool NowPlayingCard::timerActive() const
{
    return m_timer->isActive();
}

void NowPlayingCard::setPid(qint64 pid)
{
    if (pid == m_pid)
        return;
    m_pid = pid;
    tick();
}

void NowPlayingCard::setArtwork(const QString& path, const QIcon& icon)
{
    m_icon->setInstanceIcon(icon);
    m_art->setArtwork(path);
    m_art->setVisible(!path.isEmpty());
    m_icon->setVisible(path.isEmpty());
}

void NowPlayingCard::tick()
{
    QStringList parts{ elapsedText() };
    const QString memory = memoryText();
    if (!memory.isEmpty())
        parts << tr("%1 of memory").arg(memory);
    m_meta->setFullText(parts.join(QStringLiteral(" · ")));
}

void NowPlayingCard::refreshIcons()
{
    const auto& t = Theme::current();
    m_screenshots->setIcon(Icons::icon(QStringLiteral("image"), t.text));
    m_folder->setIcon(Icons::icon(QStringLiteral("folder-open"), t.textSecondary));
}

void NowPlayingCard::showEvent(QShowEvent* event)
{
    ZeroCard::showEvent(event);
    tick();
    m_timer->start();  // the 1 s timer runs only while the card is visible
}

void NowPlayingCard::hideEvent(QHideEvent* event)
{
    ZeroCard::hideEvent(event);
    m_timer->stop();
}

}  // namespace Zero
