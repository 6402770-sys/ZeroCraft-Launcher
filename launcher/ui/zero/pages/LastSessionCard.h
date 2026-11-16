// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - "Last session" card of Home: what the last play session left behind (duration, screenshots taken during it with a
// Copy action on each, worlds backed up). Dismissed with the close button. See docs/zero/SHELL.md (Home).
#pragma once

#include <QList>
#include <QString>
#include "ui/zero/ZeroCard.h"
#include "zero/session/SessionSummary.h"

class QLabel;
class QToolButton;

namespace Zero {

class ThumbnailCache;

class LastSessionCard : public ZeroCard {
    Q_OBJECT
   public:
    static constexpr int kMaxThumbnails = 6;

    explicit LastSessionCard(const Session::LastSession& session, QWidget* parent = nullptr);

    /// Thumbnails shown (screenshots that still exist, at most kMaxThumbnails).
    int thumbnailCount() const { return m_thumbs; }
    /// The "+N" tile: screenshots beyond the shown ones, 0 when there is no tile.
    int extraCount() const { return m_extra; }
    bool hasBackupLine() const { return m_backupLine != nullptr; }
    QString durationLine() const;
    QToolButton* closeButton() const { return m_close; }

   signals:
    void dismissed();
    void screenshotRequested(const QString& path);
    /// The "+N" tile: show the screenshots of this instance.
    void moreRequested(const QString& instanceId);
    void instanceRequested(const QString& instanceId);

   private:
    void refreshIcons();

    Session::LastSession m_session;
    ThumbnailCache* m_cache;
    QToolButton* m_close;
    QLabel* m_backupLine = nullptr;
    QLabel* m_backupIcon = nullptr;
    int m_thumbs = 0;
    int m_extra = 0;
};

}  // namespace Zero
