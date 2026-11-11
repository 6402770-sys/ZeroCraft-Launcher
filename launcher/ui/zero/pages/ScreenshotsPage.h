// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Screenshot Gallery: every screenshot of every instance in one searchable grid, with a full-size viewer overlay.
// The page only reads (through Zero::Gallery::ScreenshotScanner); the one thing that ever changes on disk is the viewer's "Delete…",
// which moves a file to the Recycle Bin after a confirmation.
#pragma once

#include <QElapsedTimer>
#include <QString>
#include <functional>
#include "ui/zero/shell/ShellPage.h"
#include "zero/gallery/ScreenshotScanner.h"

class QComboBox;
class QLineEdit;
class QListView;
class QPushButton;
class QStackedWidget;
class QTimer;
class QAction;

namespace Zero {

class ScreenshotDelegate;
class ScreenshotModel;
class ScreenshotViewer;
class ThumbnailCache;
class ZeroEmptyState;

class ScreenshotsPage : public ShellPage {
    Q_OBJECT
   public:
    /// A rescan on show is skipped when the last scan is younger than this.
    static constexpr int kStaleMs = 30000;
    static constexpr int kInstanceDebounceMs = 500;

    explicit ScreenshotsPage(QWidget* parent = nullptr);

    QString title() const override { return tr("Screenshots"); }
    /// "N screenshots in M instances", "Scanning…" during the first scan, "<shown> of N screenshots" while filtering, "No screenshots".
    QString subtitle() const override { return m_subtitle; }

    void focusSearch();
    void setSearchText(const QString& text);
    /// Starts a scan now.
    void rescan();
    /// Runs `done` once the first scan has finished and the thumbnails of the visible tiles are ready, or after `maxMs` at the latest.
    void callWhenIdle(const std::function<void()>& done, int maxMs);
    /// Opens the viewer on the first screenshot with this file name (any instance); false when there is none.
    bool openViewerByFileName(const QString& fileName);
    /// Runs `done` once the viewer shows its full-size image, or after `maxMs` at the latest.
    void callWhenViewerReady(const std::function<void()>& done, int maxMs);

    Gallery::ScreenshotScanner* scanner() const { return m_scanner; }
    ScreenshotViewer* viewer() const { return m_viewer; }
    QListView* listView() const { return m_list; }
    ScreenshotModel* model() const { return m_model; }
    ThumbnailCache* thumbnails() const { return m_cache; }

   signals:
    /// "Show instance in Library": MainWindow selects the instance and switches pages.
    void showInstanceRequested(const QString& instanceId);

   protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void onListChanged();
    void rebuildInstanceCombo();
    QString effectiveInstanceFilter() const;
    void applyFilters();
    void updateState();
    void clearFilters();
    void refreshIcons();
    void scheduleRescan();
    void syncNames();
    void openViewerAt(const QModelIndex& index);
    bool visibleThumbnailsReady() const;

    Gallery::ScreenshotScanner* m_scanner;
    ThumbnailCache* m_cache;
    ScreenshotModel* m_model;
    QString m_subtitle;

    QWidget* m_toolbar;
    QLineEdit* m_search;
    QAction* m_searchAction = nullptr;
    QComboBox* m_instanceCombo;
    QComboBox* m_sortCombo;
    QPushButton* m_refresh;

    QStackedWidget* m_body;  // 0 = grid / no matches, 1 = no screenshots at all, 2 = scanning
    QStackedWidget* m_collection;  // 0 = grid, 1 = no matches
    QListView* m_list = nullptr;
    ScreenshotDelegate* m_delegate = nullptr;
    ZeroEmptyState* m_noMatches;
    ScreenshotViewer* m_viewer;

    QTimer* m_rescanTimer;
    QElapsedTimer m_sinceScan;
    bool m_scanRequested = false;
    bool m_stale = false;
    QString m_instanceFilter;  // instance id, empty = all (kept even while that instance has no screenshots)
    Gallery::SortMode m_sort = Gallery::SortMode::Newest;
};

}  // namespace Zero
