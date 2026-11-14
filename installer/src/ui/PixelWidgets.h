// SPDX-License-Identifier: GPL-3.0-only
// Custom-painted controls in ZERO's Minecraft theme: bevelled block buttons, pixel check box, link, XP-style
// segmented progress bar, title-strip buttons and the in-window prompt.
#pragma once

#include <QAbstractButton>
#include <QLineEdit>
#include <QPainterPath>
#include <QVector>
#include <functional>

namespace ZeroSetup {

QFont uiFont(int pixelSize, QFont::Weight weight = QFont::Normal);
/// A rectangle with the 2 px stepped ("notched") corners of the Minecraft theme.
QPainterPath notchedRect(const QRectF& r, qreal notch);
/// Paints a raised block (fill, light top edge, dark bottom bevel). pressed collapses the bevel.
void paintBlock(QPainter& p, const QRectF& r, const QColor& fill, bool pressed, bool raised = true);
/// The accent2 focus ring around r.
void paintFocusRing(QPainter& p, const QRectF& r);
/// Stylesheet for the QLineEdit of the options page (colours from the tokens).
QString lineEditStyle();

enum class ButtonKind { Primary, Secondary, Danger };

class PixelButton : public QAbstractButton
{
    Q_OBJECT
   public:
    PixelButton(const QString& text, ButtonKind kind, QSize body, QWidget* parent = nullptr);
    void setKind(ButtonKind kind);
    QSize sizeHint() const override { return m_size; }

   protected:
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void focusInEvent(QFocusEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

   private:
    ButtonKind m_kind;
    QSize m_size;
    bool m_ring = false;
    bool m_hover = false;
};

class PixelCheck : public QAbstractButton
{
    Q_OBJECT
   public:
    explicit PixelCheck(const QString& text, QWidget* parent = nullptr);
    QSize sizeHint() const override;

   protected:
    void paintEvent(QPaintEvent*) override;
    void focusInEvent(QFocusEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

   private:
    bool m_ring = false;
    bool m_hover = false;
};

/// Text-only button in accent2 ("Options").
class PixelLink : public QAbstractButton
{
    Q_OBJECT
   public:
    explicit PixelLink(const QString& text, QWidget* parent = nullptr);
    QSize sizeHint() const override;

   protected:
    void paintEvent(QPaintEvent*) override;
    void focusInEvent(QFocusEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

   private:
    bool m_ring = false;
    bool m_hover = false;
};

enum class TitleGlyph { Minimize, Close };

class TitleButton : public QAbstractButton
{
    Q_OBJECT
   public:
    TitleButton(TitleGlyph glyph, QWidget* parent = nullptr);

   protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

   private:
    TitleGlyph m_glyph;
    bool m_hover = false;
};

/// 20 segments that light up as the work completes; a newly lit segment flashes light for 300 ms.
class XpBar : public QWidget
{
    Q_OBJECT
   public:
    explicit XpBar(QWidget* parent = nullptr);
    void setFraction(double f);  // 0..1
    double fraction() const { return m_fraction; }
    /// screenshots: pretend the newest segment lit this long ago
    void setNewestSegmentAge(int ms);

   protected:
    void paintEvent(QPaintEvent*) override;

   private:
    double m_fraction = 0;
    int m_lit = 0;
    QVector<qint64> m_litAt;
    class QTimer* m_timer;
};

/// A modal question drawn inside the window (dimmed backdrop, notched panel, two buttons).
class PromptOverlay : public QWidget
{
    Q_OBJECT
   public:
    explicit PromptOverlay(QWidget* parent);
    /// onAnswer(true) = primary button. Esc answers false.
    /// escIsPrimary: Esc answers like the primary button (use it when the primary button is the safe choice).
    void ask(const QString& title, const QString& text, const QString& primary, const QString& secondary, std::function<void(bool)> onAnswer,
             ButtonKind primaryKind = ButtonKind::Primary, bool escIsPrimary = false);
    bool isOpen() const { return isVisible(); }

   protected:
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void mousePressEvent(QMouseEvent*) override { }

   private:
    void answer(bool primary);
    QString m_title, m_text;
    PixelButton* m_primary;
    PixelButton* m_secondary;
    std::function<void(bool)> m_cb;
    QRect m_panel;
    bool m_escPrimary = false;
};

}  // namespace ZeroSetup
