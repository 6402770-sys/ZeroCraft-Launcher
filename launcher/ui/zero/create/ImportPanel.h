// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the Import body of Create Instance: a drop zone for one local .zip / .mrpack (dashed `borderStrong`,
// Browse...), and a link field for http(s) URLs. One source is active at a time: choosing a file clears the link and typing
// a link forgets the file. curseforge:// links need the classic importer's lookup, so they only offer a way there.
#pragma once

#include <QWidget>
#include <functional>

#include "zero/content/CreateInstanceModel.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QAction;

namespace Zero {
namespace Create {

class ImportDropZone : public QWidget {
    Q_OBJECT
   public:
    explicit ImportDropZone(QWidget* parent = nullptr);

    /// Shows the chosen file (name + format caption), or the empty prompt when `fileName` is empty.
    void setFile(const QString& fileName, const QString& caption);
    QPushButton* browseButton() const { return m_browse; }

   signals:
    void browseRequested();
    void fileDropped(const QString& path);

   protected:
    void paintEvent(QPaintEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

   private:
    void refresh();

    QLabel* m_icon;
    QLabel* m_title;
    QLabel* m_caption;
    QPushButton* m_browse;
    QString m_fileName;
    bool m_dragging = false;
};

class ImportPanel : public QWidget {
    Q_OBJECT
   public:
    /// `chooseFile` opens the file picker and returns the chosen path (empty when cancelled).
    explicit ImportPanel(std::function<QString(QWidget*)> chooseFile, QWidget* parent = nullptr);

    /// The active source: the chosen file, else the link (None when neither).
    const ImportSource& source() const { return m_source; }
    /// As if `path` had been dropped or browsed.
    void setLocalFile(const QString& path);
    /// As if `text` had been typed into the link field.
    void setUrlText(const QString& text);

    ImportDropZone* dropZone() const { return m_zone; }
    QLineEdit* urlField() const { return m_url; }
    QPushButton* classicButton() const { return m_classic; }
    /// The caption under the link field (format, error or the CurseForge note).
    QString urlCaption() const;
    QString fileError() const;

   signals:
    void sourceChanged();
    void classicImportRequested(const QString& url);

   private:
    void refresh();
    void refreshIcons();

    std::function<QString(QWidget*)> m_chooseFile;
    ImportDropZone* m_zone;
    QLabel* m_fileError;
    QLineEdit* m_url;
    QAction* m_urlIcon = nullptr;
    QLabel* m_urlCaption;
    QPushButton* m_classic;
    ImportSource m_file;
    ImportSource m_link;
    ImportSource m_source;
};

}  // namespace Create
}  // namespace Zero
