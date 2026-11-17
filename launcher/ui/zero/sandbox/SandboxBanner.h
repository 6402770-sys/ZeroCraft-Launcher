// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - "Try it in a copy" (docs/zero/ARCHITECTURE.md, "Test copies"): the tinted card shown on a test copy, in the Library details panel
// and, compact, in the header of the instance window. "Test copy of <X>" with "Bring changes back" (primary) and "Discard copy"; when the original
// is gone: "The original instance is gone" with "Keep as a normal instance" and "Discard copy". It only shows and reports clicks: the work is
// MainWindow's, reached through Requests (one object, so the instance window needs no pointer to the main window).
#pragma once

#include <QObject>
#include <QString>
#include <QWidget>

class BaseInstance;
class QLabel;
class QPushButton;

namespace Zero {
namespace SandboxUi {

/// What the user asked for on a banner. MainWindow connects once.
class Requests : public QObject {
    Q_OBJECT
   public:
    static Requests* instance();
   signals:
    void bringBackRequested(const QString& copyId);      ///< "Bring changes back" of this test copy
    void discardRequested(const QString& copyId);        ///< "Discard copy"
    void keepRequested(const QString& copyId);           ///< "Keep as a normal instance"
   private:
    Requests() = default;
};

class SandboxBanner : public QWidget {
    Q_OBJECT
   public:
    enum class Look { Card, Compact };
    explicit SandboxBanner(Look look, QWidget* parent = nullptr);

    /// `originalName` empty = the original instance is gone. `busyReason` non-empty disables the buttons that change the instances (it is their tooltip).
    void setState(const QString& copyId, const QString& originalName, const QString& busyReason = {});
    /// Only the busy state of the buttons (see setState).
    void setBusyReason(const QString& busyReason);
    /// Shows or hides the banner for `copy` (a test copy of an instance that exists / is gone); needs the application (instance list).
    void refresh(BaseInstance* copy);

    QString copyId() const { return m_copyId; }
    QLabel* titleLabel() const { return m_title; }
    QPushButton* bringBackButton() const { return m_bringBack; }
    QPushButton* keepButton() const { return m_keep; }
    QPushButton* discardButton() const { return m_discard; }

   signals:
    void bringBackClicked();
    void keepClicked();
    void discardClicked();

   protected:
    void paintEvent(QPaintEvent*) override;

   private:
    Look m_look;
    QString m_copyId;
    QLabel* m_title;
    QPushButton* m_bringBack;
    QPushButton* m_keep;
    QPushButton* m_discard;
};

}  // namespace SandboxUi
}  // namespace Zero
