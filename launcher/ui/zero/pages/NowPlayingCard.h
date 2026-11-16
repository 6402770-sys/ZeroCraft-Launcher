// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - "Now playing" card of Home: one per running instance (artwork or icon, name, live elapsed time, game memory use,
// Screenshots / Folder / Stop). The clock and the memory reader are injectable. See docs/zero/SHELL.md (Home).
#pragma once

#include <QString>
#include <functional>
#include <optional>
#include "ElidedLabel.h"
#include "ui/zero/ZeroCard.h"

class QIcon;
class QPushButton;
class QTimer;

namespace Zero {

class ArtworkView;
class IconTile;

class NowPlayingCard : public ZeroCard {
    Q_OBJECT
   public:
    using NowFn = std::function<qint64()>;
    using MemoryFn = std::function<std::optional<qint64>(qint64 pid)>;

    /// `startedMs` is the session start (ms since epoch), `pid` the game process (0 = unknown, no memory line).
    NowPlayingCard(const QString& instanceId, const QString& name, qint64 startedMs, qint64 pid, NowFn now, MemoryFn memory,
                   QWidget* parent = nullptr);

    QString instanceId() const { return m_id; }
    /// "12:04" / "1:20:33".
    QString elapsedText() const;
    /// "2.1 GB", empty when the memory cannot be read.
    QString memoryText() const;
    /// The line under the name: elapsed time, then the memory use when known.
    QString metaText() const { return m_meta->fullText(); }
    bool timerActive() const;

    /// The game process (0 = unknown); the memory line follows.
    void setPid(qint64 pid);
    void setArtwork(const QString& path, const QIcon& icon);
    /// Re-reads the clock and the memory now (the 1 s timer calls this while the card is visible).
    void tick();

   signals:
    void screenshotsRequested(const QString& instanceId);
    void folderRequested(const QString& instanceId);
    void stopRequested(const QString& instanceId);

   protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

   private:
    void refreshIcons();

    QString m_id;
    qint64 m_startedMs;
    qint64 m_pid;
    NowFn m_now;
    MemoryFn m_memory;
    IconTile* m_icon;
    ArtworkView* m_art;
    ElidedLabel* m_name;
    ElidedLabel* m_meta;
    QPushButton* m_screenshots;
    QPushButton* m_folder;
    QPushButton* m_stop;
    QTimer* m_timer;
};

}  // namespace Zero
