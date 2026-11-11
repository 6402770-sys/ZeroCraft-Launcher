// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - full-size screenshot viewer: an in-window overlay over the Screenshots page (a child widget, not a window). The
// image is decoded on the thread pool (the thumbnail is shown scaled up meanwhile). The only thing it ever changes on disk is moving
// the shown file to the Recycle Bin after a confirmation; a failed trash keeps the file (never a permanent delete).
#pragma once

#include <QImage>
#include <QList>
#include <QPixmap>
#include <QPointer>
#include <QWidget>
#include "zero/gallery/ScreenshotScanner.h"

class QPushButton;

namespace Zero {

class ThumbnailCache;

class ScreenshotViewer : public QWidget {
    Q_OBJECT
   public:
    ScreenshotViewer(ThumbnailCache* cache, QWidget* parent);

    bool isOpen() const { return m_open; }
    /// The list to browse (the page's current filtered order). Keeps the shown image when it is still in the list.
    void setEntries(const QList<Gallery::ScreenshotEntry>& entries);
    /// Shows the image with this path (it must be in the list) and opens the overlay.
    void openPath(const QString& path);
    void closeViewer();
    /// Path of the shown image, empty when closed.
    QString currentPath() const;
    bool fullImageLoaded() const { return m_fullLoaded; }

   signals:
    /// The viewer closed; `lastPath` is the image it showed (focus returns to its grid item).
    void closed(const QString& lastPath);
    void showInstanceRequested(const QString& instanceId);
    /// A file was moved to the Recycle Bin: the page rescans.
    void fileTrashed(const QString& path);

   protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override { event->accept(); }
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void showIndex(int index);
    void step(int delta);
    void startFullLoad();
    void onFullLoaded(quint64 token, const QImage& image);
    void layoutBar();
    void refreshIcons();
    void updateInfo();
    QRect imageArea() const;
    void copyImage();
    void openExternally();
    void showInFolder();
    void askTrash();

    ThumbnailCache* m_cache;
    QList<Gallery::ScreenshotEntry> m_entries;
    int m_index = -1;
    bool m_open = false;
    bool m_fullLoaded = false;
    quint64 m_token = 0;
    QImage m_image;
    QPixmap m_scaled;  // m_image scaled to the image area (rebuilt on resize / new image)
    QSize m_scaledFor;
    QPointer<QWidget> m_previousFocus;

    QWidget* m_bar;
    class QLabel* m_name;
    class QLabel* m_meta;
    QPushButton* m_copy;
    QPushButton* m_open_;
    QPushButton* m_folder;
    QPushButton* m_instance;
    QPushButton* m_delete;
    QPushButton* m_close;
};

}  // namespace Zero
