// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the "Backups" block of the Worlds details panel: overline + count, "Back up now" and the list of backups of the
// shown world (Restore / Delete per row). It only asks WorldBackupJobs to do things; the confirmations live here.
#pragma once

#include <QList>
#include <QString>
#include <QWidget>
#include "zero/backup/WorldBackup.h"
#include "zero/worlds/WorldScanner.h"

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace Zero {

namespace Backup {
class WorldBackupJobs;
}

class WorldBackupsSection : public QWidget {
    Q_OBJECT
   public:
    static constexpr int kRowHeight = 40;
    static constexpr int kMaxVisibleRows = 5;

    explicit WorldBackupsSection(QWidget* parent = nullptr);

    void setJobs(Backup::WorldBackupJobs* jobs);
    /// Shows the backups of `entry`; the list is (re)requested only when the world changed. Cheap otherwise.
    void setWorld(const Worlds::WorldEntry& entry);
    void clear();
    /// The instance of the shown world runs: backup, restore and delete are disabled.
    void setInstanceRunning(bool running);

    // for tests / evidence
    QPushButton* backupButton() const { return m_backup; }
    int rowCount() const { return m_rows.size(); }
    QLabel* countLabel() const { return m_count; }

   signals:
    /// A listing arrived: `folderExists` tells whether the backups folder of this world exists (for "Open backups folder").
    void folderStateChanged(bool folderExists);

   private:
    struct Row {
        Backup::BackupInfo info;
        QWidget* widget = nullptr;
        QWidget* restore = nullptr;
        QWidget* remove = nullptr;
    };

    void onListed(const QString& key, const QList<Backup::BackupInfo>& backups);
    void rebuildRows();
    void refreshState();
    void refreshIcons();
    void askRestore(const Backup::BackupInfo& info);
    void askDelete(const Backup::BackupInfo& info);
    void requestList();

    Backup::WorldBackupJobs* m_jobs = nullptr;
    Worlds::WorldEntry m_entry;
    QString m_listKey;  // key the shown list belongs to
    bool m_running = false;
    bool m_listed = false;
    QList<Backup::BackupInfo> m_backups;
    QList<Row> m_rows;

    QLabel* m_count;
    QPushButton* m_backup;
    QScrollArea* m_scroll;
    QWidget* m_listHost;
    QVBoxLayout* m_listLayout;
};

}  // namespace Zero
