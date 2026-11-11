// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the Worlds details panel: icon, name, folder, stats (instance, mode, version, seed, size, last played), the
// Backups block (WorldBackupsSection) and the actions (Open folder, Open backups folder, Show instance in Library). The panel
// itself never changes the world; backups go through Zero::Backup::WorldBackupJobs.
#pragma once

#include <QString>
#include <QVector>
#include "ui/zero/ZeroCard.h"
#include "zero/worlds/WorldScanner.h"

class QLabel;
class QPushButton;
class QStackedLayout;
class QToolButton;

namespace Zero {

class ElidedLabel;
class WorldIconTile;
class WorldBackupsSection;

namespace Backup {
class WorldBackupJobs;
}

class WorldDetailsPanel : public ZeroCard {
    Q_OBJECT
   public:
    static constexpr int kWidth = 300;

    explicit WorldDetailsPanel(QWidget* parent = nullptr);

    /// Shows `entry`. Cheap: call again when its size arrives.
    void setWorld(const Worlds::WorldEntry& entry);
    /// Back to the "Select a world" state.
    void clear();
    /// The backup service the Backups block talks to (not owned).
    void setBackupJobs(Backup::WorldBackupJobs* jobs);
    /// The instance of the shown world runs: backups are disabled.
    void setInstanceRunning(bool running);

    // for tests / evidence
    bool hasWorld() const { return !m_entry.folderPath.isEmpty(); }
    const Worlds::WorldEntry& world() const { return m_entry; }
    QPushButton* openFolderButton() const { return m_openFolder; }
    QPushButton* showInstanceButton() const { return m_showInstance; }
    QToolButton* copySeedButton() const { return m_copySeed; }
    WorldBackupsSection* backupsSection() const { return m_backups; }
    QPushButton* openBackupsButton() const { return m_openBackups; }

   signals:
    void showInstanceRequested(const QString& instanceId);

   private:
    void refreshIcons();
    void copySeed();

    QStackedLayout* m_stack;
    WorldIconTile* m_icon;
    ElidedLabel* m_name;
    ElidedLabel* m_folder;
    ElidedLabel* m_instance;
    ElidedLabel* m_mode;
    ElidedLabel* m_version;
    ElidedLabel* m_seed;
    QToolButton* m_copySeed;
    QLabel* m_size;
    QLabel* m_lastPlayed;
    WorldBackupsSection* m_backups;
    QPushButton* m_openFolder;
    QPushButton* m_openBackups;
    QPushButton* m_showInstance;
    Worlds::WorldEntry m_entry;
    Backup::WorldBackupJobs* m_jobs = nullptr;
};

}  // namespace Zero
