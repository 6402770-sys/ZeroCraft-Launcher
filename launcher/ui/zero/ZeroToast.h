// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - transient notifications.
#pragma once

#include <QEasingCurve>
#include <QPointF>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <functional>

class QLabel;
class QVariantAnimation;
class QPushButton;
class QToolButton;

namespace Zero {


/// Bottom-right toast stack inside a window. Each toast: `elevated` bg, 1px border, radius `lg`, padding `md`,
/// status icon, text, optional ghost action button and a close button. Auto-dismisses after 4 s (the timer restarts
/// when the pointer leaves it). Enter: slide in from 24 px right + fade (220 ms OutCubic); exit: slide out + fade
/// (160 ms InCubic); the remaining toasts glide to their new slots (200 ms OutQuint). All instant with motion off.
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
    void setSlot(const QPoint& target, bool animate);  // where the card rests (glides there when `animate`)
    void layoutCard();                                  // card position = slot + slide offset of the presence
    void applyPresence(qreal presence);                 // 0 = hidden (slid out, transparent) .. 1 = shown
    void animatePresence(qreal to, int ms, const QEasingCurve& curve, std::function<void()> finished = {});
    void finishDismiss();
    static void restack(QWidget* host, bool animate = true);

    friend class ToastHost;
    QPointer<QWidget> m_shadow;  // ToastShadow (private to the .cpp)
    QTimer m_timer;
    QVariantAnimation* m_presenceAnim = nullptr;
    std::function<void()> m_presenceDone;  // runs when the presence animation reaches its end
    QVariantAnimation* m_slotAnim = nullptr;
    QPointF m_slotPos;     // animated resting position of the card
    QPoint m_slotTarget;
    qreal m_presence = 1.0;
    bool m_placed = false;
    bool m_dismissing = false;
};

}  // namespace Zero
