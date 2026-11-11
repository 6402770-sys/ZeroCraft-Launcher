// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Tools > Storage: disk usage report and safe cleanup. The measuring and trashing live in zero/storage/StorageScanner.
#pragma once

#include <QPointer>
#include <QWidget>
#include "zero/storage/StorageScanner.h"

class QLabel;
class QPushButton;
class QScrollArea;
class QStackedLayout;
class QVBoxLayout;

namespace Zero {

class StorageView : public QWidget {
    Q_OBJECT
   public:
    explicit StorageView(QWidget* parent = nullptr);

    /// One-line description shown as the page subtitle while the tool is open.
    static QString description();

    /// Top bar widget (the Rescan button); owned by the view.
    QWidget* controlsWidget() const { return m_controls; }
    /// Called whenever the tool is opened: starts a scan unless one is already running.
    void open();
    Storage::StorageScanner* scanner() const { return m_scanner; }
    /// Dev aid (screenshots): scrolls the report to its end.
    void scrollToEnd();

   signals:
    void showInstanceRequested(const QString& instanceId);

   private:
    void rebuild(const Storage::StorageReport& report);
    void updateBusy();
    void askTrash(const Storage::CleanupGroup& group);
    void onTrashFinished(const Storage::TrashResult& result);

    Storage::StorageScanner* m_scanner;
    QWidget* m_controls;
    QPushButton* m_rescan;
    QStackedLayout* m_stack;
    QScrollArea* m_scroll;
    QWidget* m_host = nullptr;
    QVBoxLayout* m_hostLayout = nullptr;
    QPointer<QLabel> m_updating;
    QList<QPushButton*> m_cleanupButtons;
};

}  // namespace Zero
