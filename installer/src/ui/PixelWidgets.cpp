// SPDX-License-Identifier: GPL-3.0-only
#include "PixelWidgets.h"

#include "Motion.h"
#include "SetupTokens.h"

#include <QFocusEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QTimer>
#include <algorithm>

namespace ZeroSetup {

namespace T = Tok;

static bool keyboardFocus(Qt::FocusReason r)
{
    return r == Qt::TabFocusReason || r == Qt::BacktabFocusReason || r == Qt::ShortcutFocusReason;
}

QFont uiFont(int pixelSize, QFont::Weight weight)
{
    QFont f;
    f.setFamilies({ QString::fromLatin1(T::fontPrimary), QString::fromLatin1(T::fontFallback) });
    f.setPixelSize(pixelSize);
    f.setWeight(weight);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

QPainterPath notchedRect(const QRectF& r, qreal n)
{
    QPainterPath p;
    p.moveTo(r.left() + n, r.top());
    p.lineTo(r.right() - n, r.top());
    p.lineTo(r.right() - n, r.top() + n);
    p.lineTo(r.right(), r.top() + n);
    p.lineTo(r.right(), r.bottom() - n);
    p.lineTo(r.right() - n, r.bottom() - n);
    p.lineTo(r.right() - n, r.bottom());
    p.lineTo(r.left() + n, r.bottom());
    p.lineTo(r.left() + n, r.bottom() - n);
    p.lineTo(r.left(), r.bottom() - n);
    p.lineTo(r.left(), r.top() + n);
    p.lineTo(r.left() + n, r.top() + n);
    p.closeSubpath();
    return p;
}

void paintBlock(QPainter& p, const QRectF& r, const QColor& fill, bool pressed, bool raised)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(Qt::NoPen);
    const qreal n = T::radiusNotch;
    if (!raised) {
        p.setBrush(fill);
        p.drawPath(notchedRect(r, n));
        p.restore();
        return;
    }
    const QRectF shape = pressed ? r.adjusted(0, 2, 0, 0) : r;
    const qreal bevel = pressed ? 1 : T::bevelBottom;
    p.setBrush(T::c(T::bevelDark));
    p.drawPath(notchedRect(shape, n));
    p.setClipPath(notchedRect(shape, n));
    p.fillRect(QRectF(shape.left(), shape.top(), shape.width(), shape.height() - bevel), fill);
    if (!pressed)
        p.fillRect(QRectF(shape.left() + n, shape.top(), shape.width() - 2 * n, 1), QColor(255, 255, 255, T::bevelLightAlpha));
    p.restore();
}

void paintFocusRing(QPainter& p, const QRectF& r)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(Qt::NoPen);
    p.setBrush(T::c(T::accent2));
    QPainterPath outer = notchedRect(r, T::radiusNotch + 1);
    QPainterPath inner = notchedRect(r.adjusted(T::focusRing, T::focusRing, -T::focusRing, -T::focusRing), T::radiusNotch);
    p.drawPath(outer.subtracted(inner));
    p.restore();
}

QString lineEditStyle()
{
    return QStringLiteral(
               "QLineEdit { background: %1; color: %2; border: 2px solid %3; padding: 0 8px; selection-background-color: %4; selection-color: %5; }"
               "QLineEdit:focus { border: 2px solid %6; }")
        .arg(T::c(T::surface).name(), T::c(T::text).name(), T::c(T::borderStrong).name(), T::c(T::accent2).name(), T::c(T::bevelDark).name(),
             T::c(T::accent2).name());
}

// ------------------------------------------------------------------------------------------ PixelButton

PixelButton::PixelButton(const QString& text, ButtonKind kind, QSize body, QWidget* parent)
    : QAbstractButton(parent), m_kind(kind), m_size(body + QSize(2 * T::buttonMargin, 2 * T::buttonMargin))
{
    setText(text);
    setFixedSize(m_size);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover);
}

void PixelButton::setKind(ButtonKind kind)
{
    m_kind = kind;
    update();
}

void PixelButton::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing);
    const QRectF body = QRectF(rect()).adjusted(T::buttonMargin, T::buttonMargin, -T::buttonMargin, -T::buttonMargin);
    const bool down = isDown() && isEnabled();
    QRgb base, hover, press, label;
    switch (m_kind) {
        case ButtonKind::Primary: base = T::accent; hover = T::accentHover; press = T::accentPressed; label = T::onAccent; break;
        case ButtonKind::Danger: base = T::danger; hover = T::dangerHover; press = T::dangerPressed; label = T::bevelDark; break;
        default: base = T::elevated; hover = T::higher; press = T::pressed; label = T::text; break;
    }
    if (!isEnabled())
        p.setOpacity(T::disabledOpacity);
    const QColor fill = T::c(down ? press : (m_hover && isEnabled() ? hover : base));
    paintBlock(p, body, fill, down);
    p.setFont(uiFont(T::pxButton, QFont::DemiBold));
    p.setPen(T::c(label));
    QRectF tr = body.adjusted(0, 0, 0, -T::bevelBottom);
    if (down)
        tr = body.adjusted(0, 2, 0, -1);
    p.drawText(tr, Qt::AlignCenter, text());
    if (m_ring && hasFocus())
        paintFocusRing(p, body.adjusted(-T::buttonMargin + 1, -T::buttonMargin + 1, T::buttonMargin - 1, T::buttonMargin - 1));
}

void PixelButton::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        click();
        return;
    }
    QAbstractButton::keyPressEvent(e);
}

void PixelButton::focusInEvent(QFocusEvent* e)
{
    m_ring = keyboardFocus(e->reason());  // the ring is for keyboard users
    QAbstractButton::focusInEvent(e);
    update();
}
void PixelButton::focusOutEvent(QFocusEvent* e)
{
    QAbstractButton::focusOutEvent(e);
    update();
}
void PixelButton::enterEvent(QEnterEvent* e)
{
    m_hover = true;
    QAbstractButton::enterEvent(e);
    update();
}
void PixelButton::leaveEvent(QEvent* e)
{
    m_hover = false;
    QAbstractButton::leaveEvent(e);
    update();
}

// ------------------------------------------------------------------------------------------ PixelCheck

PixelCheck::PixelCheck(const QString& text, QWidget* parent) : QAbstractButton(parent)
{
    setText(text);
    setCheckable(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover);
}

QSize PixelCheck::sizeHint() const
{
    const QFontMetrics fm(uiFont(T::pxBody));
    return QSize(8 + 20 + 10 + fm.horizontalAdvance(text()) + 8, 30);
}

void PixelCheck::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setRenderHint(QPainter::Antialiasing, false);
    const QRectF box(8, (height() - 20) / 2.0, 20, 20);
    const bool on = isChecked();
    p.setPen(Qt::NoPen);
    p.setBrush(T::c(m_hover ? T::accentHover : (on ? T::accent : T::borderStrong)));
    p.drawPath(notchedRect(box, 2));
    p.setBrush(T::c(on ? T::accent : T::surface));
    p.drawPath(notchedRect(box.adjusted(2, 2, -2, -2), 1));
    if (on) {  // a pixel tick: 2 px strokes on a 2 px grid
        p.setBrush(T::c(T::onAccent));
        const qreal x = box.left(), y = box.top();
        const QRectF px[] = { QRectF(x + 5, y + 9, 2, 2), QRectF(x + 7, y + 11, 2, 2), QRectF(x + 9, y + 9, 2, 2), QRectF(x + 11, y + 7, 2, 2),
                              QRectF(x + 13, y + 5, 2, 2) };
        for (const QRectF& r : px)
            p.drawRect(r);
    }
    p.setFont(uiFont(T::pxBody));
    p.setPen(T::c(T::text));
    p.drawText(QRectF(box.right() + 10, 0, width() - box.right() - 10, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
    if (m_ring && hasFocus())
        paintFocusRing(p, QRectF(rect()).adjusted(1, 1, -1, -1));
}

void PixelCheck::focusInEvent(QFocusEvent* e)
{
    m_ring = keyboardFocus(e->reason());
    QAbstractButton::focusInEvent(e);
    update();
}
void PixelCheck::focusOutEvent(QFocusEvent* e)
{
    QAbstractButton::focusOutEvent(e);
    update();
}
void PixelCheck::enterEvent(QEnterEvent* e)
{
    m_hover = true;
    QAbstractButton::enterEvent(e);
    update();
}
void PixelCheck::leaveEvent(QEvent* e)
{
    m_hover = false;
    QAbstractButton::leaveEvent(e);
    update();
}

// ------------------------------------------------------------------------------------------ PixelLink

PixelLink::PixelLink(const QString& text, QWidget* parent) : QAbstractButton(parent)
{
    setText(text);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover);
    setFixedSize(sizeHint());
}

QSize PixelLink::sizeHint() const
{
    return QSize(QFontMetrics(uiFont(T::pxCaption, QFont::DemiBold)).horizontalAdvance(text()) + 10, 24);
}

void PixelLink::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setFont(uiFont(T::pxCaption, QFont::DemiBold));
    p.setPen(T::c(m_hover ? T::accentHover : T::accent2));
    p.drawText(rect(), Qt::AlignCenter, text());
    if (m_hover) {
        const QFontMetrics fm(font());
        p.fillRect(QRect(5, height() / 2 + 8, width() - 10, 1), p.pen().color());
    }
    if (m_ring && hasFocus())
        paintFocusRing(p, QRectF(rect()).adjusted(0, 1, 0, -1));
}
void PixelLink::focusInEvent(QFocusEvent* e)
{
    m_ring = keyboardFocus(e->reason());
    QAbstractButton::focusInEvent(e);
    update();
}
void PixelLink::focusOutEvent(QFocusEvent* e)
{
    QAbstractButton::focusOutEvent(e);
    update();
}
void PixelLink::enterEvent(QEnterEvent* e)
{
    m_hover = true;
    QAbstractButton::enterEvent(e);
    update();
}
void PixelLink::leaveEvent(QEvent* e)
{
    m_hover = false;
    QAbstractButton::leaveEvent(e);
    update();
}

// ------------------------------------------------------------------------------------------ TitleButton

TitleButton::TitleButton(TitleGlyph glyph, QWidget* parent) : QAbstractButton(parent), m_glyph(glyph)
{
    setFixedSize(T::titleButtonW, T::titleButtonH);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::ArrowCursor);
    setAttribute(Qt::WA_Hover);
}

void TitleButton::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    if (m_hover)
        p.fillRect(rect(), T::c(m_glyph == TitleGlyph::Close ? T::danger : T::surfaceHover));
    const QColor col = T::c(m_hover && m_glyph == TitleGlyph::Close ? T::bevelDark : (m_hover ? T::text : T::textTertiary));
    p.setPen(Qt::NoPen);
    p.setBrush(col);
    const int cx = width() / 2, cy = height() / 2;
    if (m_glyph == TitleGlyph::Minimize) {
        p.drawRect(cx - 5, cy + 2, 10, 2);
    } else {  // pixel cross from 2 px blocks
        for (int i = 0; i < 5; ++i) {
            p.drawRect(cx - 5 + i * 2, cy - 5 + i * 2, 2, 2);
            p.drawRect(cx + 3 - i * 2, cy - 5 + i * 2, 2, 2);
        }
    }
}
void TitleButton::enterEvent(QEnterEvent* e)
{
    m_hover = true;
    QAbstractButton::enterEvent(e);
    update();
}
void TitleButton::leaveEvent(QEvent* e)
{
    m_hover = false;
    QAbstractButton::leaveEvent(e);
    update();
}

// ------------------------------------------------------------------------------------------ XpBar

XpBar::XpBar(QWidget* parent) : QWidget(parent), m_litAt(T::barSegments, -1000000), m_timer(new QTimer(this))
{
    setFixedSize(T::barW, T::barH);
    m_timer->setInterval(T::frameMs);
    connect(m_timer, &QTimer::timeout, this, [this] {
        update();
        bool any = false;
        for (qint64 t : std::as_const(m_litAt))
            any = any || Motion::now() - t < T::flashMs;
        if (!any)
            m_timer->stop();
    });
}

void XpBar::setFraction(double f)
{
    m_fraction = std::clamp(f, 0.0, 1.0);
    const int lit = int(m_fraction * T::barSegments + 1e-9);
    for (int i = m_lit; i < lit; ++i)
        m_litAt[i] = Motion::now();
    m_lit = std::max(m_lit, lit);
    if (lit < m_lit)  // going backwards (a new run)
        m_lit = lit;
    if (Motion::enabled() && !Motion::frozen() && !m_timer->isActive())
        m_timer->start();
    update();
}

void XpBar::setNewestSegmentAge(int ms)
{
    if (m_lit > 0)
        m_litAt[m_lit - 1] = Motion::now() - ms;
    update();
}

void XpBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(Qt::NoPen);
    const QRectF r(rect());
    p.setBrush(T::c(T::bevelDark));
    p.drawPath(notchedRect(r, 2));
    p.setBrush(T::c(T::surface));
    p.drawRect(r.adjusted(2, 2, -2, -2));
    const int n = T::barSegments;
    const qreal inner = r.width() - 2 * T::barPad;
    const qreal segW = (inner - (n - 1) * T::barGap) / n;
    const qreal segH = r.height() - 2 * T::barPad;
    const double scaled = m_fraction * n;
    for (int i = 0; i < n; ++i) {
        const qreal x = r.left() + T::barPad + i * (segW + T::barGap);
        const QRectF seg(x, r.top() + T::barPad, segW, segH);
        if (i < int(scaled + 1e-9)) {
            QColor fill = T::c(T::accent);
            const qint64 age = Motion::now() - m_litAt[i];
            if (Motion::enabled() && age < T::flashMs) {  // light flash fading back to grass
                const double k = 1.0 - double(age) / T::flashMs;
                fill = T::mix(T::accent, T::text, 0.85 * k);
            }
            p.fillRect(seg, fill);
            p.fillRect(QRectF(seg.left(), seg.top(), seg.width(), 2), QColor(255, 255, 255, 60));
            p.fillRect(QRectF(seg.left(), seg.bottom() - 2, seg.width(), 2), T::c(T::accentPressed));
        } else {
            p.fillRect(seg, T::c(T::elevated));
            const double part = scaled - i;
            if (part > 0 && part < 1)  // the segment being filled right now glows in proportion
                p.fillRect(seg, T::withAlpha(T::accent, 0.15 + 0.5 * part));
        }
    }
}

// ------------------------------------------------------------------------------------------ PromptOverlay

PromptOverlay::PromptOverlay(QWidget* parent) : QWidget(parent)
{
    hide();
    setFocusPolicy(Qt::StrongFocus);
    m_primary = new PixelButton(QString(), ButtonKind::Primary, QSize(150, 36), this);
    m_secondary = new PixelButton(QString(), ButtonKind::Secondary, QSize(150, 36), this);
    connect(m_primary, &QAbstractButton::clicked, this, [this] { answer(true); });
    connect(m_secondary, &QAbstractButton::clicked, this, [this] { answer(false); });
}

void PromptOverlay::ask(const QString& title, const QString& text, const QString& primary, const QString& secondary,
                        std::function<void(bool)> onAnswer, ButtonKind primaryKind, bool escIsPrimary)
{
    m_escPrimary = escIsPrimary;
    m_title = title;
    m_text = text;
    m_cb = std::move(onAnswer);
    m_primary->setText(primary);
    m_primary->setKind(primaryKind);
    m_secondary->setText(secondary);
    setGeometry(parentWidget()->rect());
    const int w = 480, h = 190;
    m_panel = QRect((width() - w) / 2, (height() - h) / 2, w, h);
    const int by = m_panel.bottom() - 24 - m_primary->height() + 8;
    m_secondary->move(m_panel.right() - 22 - m_secondary->width(), by);
    m_primary->move(m_secondary->x() - 8 - m_primary->width(), by);
    raise();
    show();
    m_primary->setFocus(Qt::OtherFocusReason);
    update();
}

void PromptOverlay::answer(bool primary)
{
    hide();
    auto cb = std::move(m_cb);
    m_cb = nullptr;
    if (cb)
        cb(primary);
}

void PromptOverlay::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        answer(m_escPrimary);
        return;
    }
    QWidget::keyPressEvent(e);
}

void PromptOverlay::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.fillRect(rect(), T::withAlpha(T::bg, T::dimAlpha / 255.0));
    paintBlock(p, QRectF(m_panel), T::c(T::surface), false, true);
    p.setPen(T::c(T::borderStrong));
    p.setPen(T::c(T::text));
    p.setFont(uiFont(T::pxPromptTitle, QFont::DemiBold));
    p.drawText(QRectF(m_panel.left() + 24, m_panel.top() + 18, m_panel.width() - 48, 28), Qt::AlignLeft | Qt::AlignVCenter, m_title);
    p.setPen(T::c(T::textSecondary));
    p.setFont(uiFont(T::pxBody));
    p.drawText(QRectF(m_panel.left() + 24, m_panel.top() + 54, m_panel.width() - 48, 70), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_text);
}

}  // namespace ZeroSetup
