// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the in-page picture viewer of the Discover details (docs/zero/CONTENT.md, "Modpacks" > Details > Gallery): a dark
// overlay that covers its host, one picture fitted inside it (through the RemoteImageCache, 1280 px bucket), the picture's title,
// a counter and previous / next / close buttons. Left / Right / Esc work from the keyboard; a click outside the picture closes it.
#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class QToolButton;

namespace Zero {
class RemoteImageCache;
}

namespace Zero::Content {

/// One picture of a project's gallery.
struct GalleryEntry {
    QString url;  ///< the picture the viewer shows
    QString title;
    QString description;
    QString thumbUrl;  ///< the smaller picture of the thumbnail grid; empty = `url`
};

class ContentGalleryViewer : public QWidget {
    Q_OBJECT
   public:
    explicit ContentGalleryViewer(Zero::RemoteImageCache* images, QWidget* parent = nullptr);

    /// The widget the overlay covers (it follows the host's size). Reparents the viewer to `host`.
    void setHost(QWidget* host);

    void open(const QList<GalleryEntry>& entries, int index);
    void closeViewer();
    bool isOpen() const { return isVisible(); }
    int index() const { return m_index; }
    int count() const { return int(m_entries.size()); }
    void step(int direction);
    QToolButton* previousButton() const { return m_previous; }
    QToolButton* nextButton() const { return m_next; }
    QToolButton* closeButton() const { return m_close; }
    /// The rectangle the picture is fitted into.
    QRect pictureArea() const;

   signals:
    void closed();
    void indexChanged(int index);

   protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void showIndex(int index);
    void prefetch();
    void place();

    Zero::RemoteImageCache* m_images;
    QWidget* m_host = nullptr;
    QList<GalleryEntry> m_entries;
    int m_index = 0;
    QToolButton* m_previous;
    QToolButton* m_next;
    QToolButton* m_close;
};

}  // namespace Zero::Content
