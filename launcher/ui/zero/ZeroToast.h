// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - transient notifications.
#pragma once

#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <functional>

class QLabel;
class QPushButton;
class QToolButton;

namespace Zero {


/// Bottom-right toast stack inside a window. Each toast: `elevated` bg, 1px border, radius `lg`, padding `md`,
/// status icon, text, optional ghost action button and a close button. Auto-dismisses after 4 s (the timer restarts
/// when the pointer leaves it). Fades in/out through ZeroMotion (instant when ZeroReduceMotion is on).
class ZeroToast : public QWidget {
    Q_OBJECT
   public:
    enum class Kind { Info, Success, Warning, Error };

    /// Shows a toast in `window` (its top-level widget is used). If `actionText` is non-empty an action button is
    /// shown; clicking it runs `action` and dismisses the toast. Returns the toast (owned by the window).
    static ZeroToast* show(QWidget* window, const QString& text, Kind kind = Kind::Info, const QString& actionText = {},
                           std::function<void()> action = {});

    /// Number of live toasts attached to `window` (used by tests).
    static int count(QWidget* window);

    ~ZeroToast() override;

   public slots:
    void dismiss();

   signals:
    void dismissed();

   protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

   private:
    ZeroToast(QWidget* host, const QString& text, Kind kind, const QString& actionText, std::function<void()> action);
    void presentIn(QWidget* host);
    void place(const QPoint& cardTopLeft);
    static void restack(QWidget* host);

    friend class ToastHost;
    QPointer<QWidget> m_shadow;  // ToastShadow (private to the .cpp)
    QTimer m_timer;
    bool m_dismissing = false;
};

}  // namespace Zero
