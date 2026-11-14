// SPDX-License-Identifier: GPL-3.0-only
/*
 *  ZERO Launcher - guided tour overlay. See TourOverlay.h.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "TourOverlay.h"

#include <QApplication>
#include <QChildEvent>
#include <QEasingCurve>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "settings/SettingsObject.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroTokens.h"

namespace Zero {
namespace Onboarding {

namespace {

constexpr int kShadowPad = Tokens::shadowBlur;  // room around the card for its shadow and the pointer
constexpr int kInnerPad = 16;                   // card padding

QRectF lerp(const QRectF& a, const QRectF& b, double t)
{
    return QRectF(a.x() + (b.x() - a.x()) * t, a.y() + (b.y() - a.y()) * t, a.width() + (b.width() - a.width()) * t,
                  a.height() + (b.height() - a.height()) * t);
}

}  // namespace

/// The callout card: an elevated surface with a 1 px border, a soft shadow and an 8 px pointer toward the target, holding the overline
/// ("STEP n OF N"), the title, the text and the footer row. The widget is bigger than the card by kShadowPad on every side.
class TourCard : public QWidget {
   public:
    QLabel* overline = nullptr;
    QLabel* title = nullptr;
    QLabel* body = nullptr;
    QPushButton* skip = nullptr;
    QPushButton* back = nullptr;
    QPushButton* next = nullptr;

    explicit TourCard(QWidget* parent) : QWidget(parent)
    {
        setObjectName(QStringLiteral("zeroTourCard"));
        auto* col = new QVBoxLayout(this);
        col->setContentsMargins(kShadowPad + kInnerPad, kShadowPad + kInnerPad, kShadowPad + kInnerPad, kShadowPad + kInnerPad);
        col->setSpacing(0);

        overline = new QLabel(this);
        Style::applyRole(overline, Role::Overline);
        title = new QLabel(this);
        Style::applyRole(title, Role::Heading);
        title->setWordWrap(true);
        body = new QLabel(this);
        Style::applyRole(body, Role::Body);
        Style::setProperty(body, "zeroTone", "secondary");
        body->setWordWrap(true);
        body->setAlignment(Qt::AlignLeft | Qt::AlignTop);

        skip = new QPushButton(QObject::tr("Skip tour"), this);
        Style::setProperty(skip, "zeroVariant", "ghost");
        back = new QPushButton(QObject::tr("Back"), this);
        Style::setProperty(back, "zeroVariant", "secondary");
        next = new QPushButton(QObject::tr("Next"), this);
        Style::setProperty(next, "zeroVariant", "primary");
        for (QPushButton* b : { skip, back, next }) {
            b->setAutoDefault(false);
            b->setDefault(false);
            b->setFocusPolicy(Qt::StrongFocus);
            b->setCursor(Qt::PointingHandCursor);
        }

        auto* footer = new QHBoxLayout;
        footer->setContentsMargins(0, 0, 0, 0);
        footer->setSpacing(Tokens::spaceSm);
        footer->addWidget(skip);
        footer->addStretch(1);
        footer->addWidget(back);
        footer->addWidget(next);

        col->addWidget(overline);
        col->addSpacing(Tokens::spaceXs);
        col->addWidget(title);
        col->addSpacing(Tokens::spaceSm);
        col->addWidget(body);
        col->addSpacing(kInnerPad);
        col->addLayout(footer);
    }

    /// Sizes the widget for the current texts: kCardWidth wide (plus the shadow margin), as tall as the wrapped texts need.
    void fit()
    {
        layout()->invalidate();
        layout()->activate();
        const int w = kCardWidth + 2 * kShadowPad;
        setFixedSize(w, layout()->totalHeightForWidth(w));
        layout()->activate();
    }

    QSize cardSize() const { return QSize(width() - 2 * kShadowPad, height() - 2 * kShadowPad); }

    void setPointer(TourSide side, int pos)
    {
        m_side = side;
        m_pointer = pos;
        update();
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF card(kShadowPad, kShadowPad, width() - 2 * kShadowPad, height() - 2 * kShadowPad);

        // popover shadow (blur 24, offset 0 / 8, 55 % black), approximated by stacked translucent panels
        constexpr int layers = 10;
        const qreal perLayer = (Tokens::shadowAlpha / 255.0) * 0.14;
        p.setPen(Qt::NoPen);
        for (int i = layers; i >= 1; --i) {
            const qreal grow = Tokens::shadowBlur * (qreal(i) / layers) * 0.5;
            QColor c(0, 0, 0);
            c.setAlphaF(std::min(1.0, perLayer));
            p.setBrush(c);
            p.drawPath(Paint::panelPath(card.translated(0, Tokens::shadowOffsetY / 2.0).adjusted(-grow, -grow, grow, grow), t.radiusXl + grow));
        }

        p.setBrush(t.border);
        p.drawPath(Paint::panelPath(card, t.radiusXl));
        p.setBrush(ThemeSpec::opaque(t.elevated));
        p.drawPath(Paint::panelPath(card.adjusted(1, 1, -1, -1), std::max(0, t.radiusXl - 1)));
        Paint::paintBevel(p, card);

        // the pointer: drawn pointing left in local coordinates, then turned for a card below its target
        p.save();
        const double ps = kPointerSize;
        if (m_side == TourSide::Right) {
            p.translate(card.left(), card.top() + m_pointer);
        } else {
            p.translate(card.left() + m_pointer, card.top());
            p.rotate(90);
        }
        QPolygonF outer;
        outer << QPointF(-ps, 0) << QPointF(0.6, -ps - 0.6) << QPointF(0.6, ps + 0.6);
        p.setBrush(t.border);
        p.drawPolygon(outer);
        const double inset = std::sqrt(2.0);  // 1 px border measured across the slanted edges
        QPolygonF inner;
        inner << QPointF(-ps + inset, 0) << QPointF(1.0, -(ps - inset + 1.0)) << QPointF(1.0, ps - inset + 1.0);
        p.setBrush(ThemeSpec::opaque(t.elevated));
        p.drawPolygon(inner);
        p.restore();
    }

   private:
    TourSide m_side = TourSide::Right;
    int m_pointer = 0;
};

TourOverlay::TourOverlay(QWidget* host, const QList<TourStep>& steps, SettingsObject* settings)
    : QWidget(host), m_host(host), m_settings(settings)
{
    setObjectName(QStringLiteral("zeroTourOverlay"));
    setAccessibleName(tr("Guided tour"));
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);

    // a step without a usable target (missing widget, hidden control) is dropped: the count adjusts
    for (const TourStep& step : steps) {
        bool usable = false;
        if (step.available) {
            usable = step.available();
        } else if (step.target) {
            QWidget* t = step.target();
            usable = t && host && host->isAncestorOf(t) && t->isVisibleTo(host);
        }
        if (usable)
            m_steps.append(step);
    }

    m_card = new TourCard(this);
    m_next = m_card->next;
    m_back = m_card->back;
    m_skipButton = m_card->skip;
    connect(m_next, &QPushButton::clicked, this, &TourOverlay::next);
    connect(m_back, &QPushButton::clicked, this, &TourOverlay::back);
    connect(m_skipButton, &QPushButton::clicked, this, &TourOverlay::skip);
    m_card->hide();

    m_glide.setStartValue(0.0);
    m_glide.setEndValue(1.0);
    m_glide.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_glide, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_spot = lerp(m_spotFrom, QRectF(m_spotTo), v.toDouble());
        update();
    });
    connect(&m_glide, &QVariantAnimation::finished, this, [this]() {
        m_spot = QRectF(m_spotTo);
        update();
    });

    m_cardAnim.setStartValue(0.0);
    m_cardAnim.setEndValue(1.0);
    m_cardAnim.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_cardAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { setCardProgress(v.toDouble()); });
    connect(&m_cardAnim, &QVariantAnimation::finished, this, [this]() { setCardProgress(1.0); });

    m_poll = new QTimer(this);
    m_poll->setInterval(kPollMs);
    connect(m_poll, &QTimer::timeout, this, [this]() {
        refreshTarget();
        // the focus stays on the card (a closed palette gives it back to the shell's old widget)
        QWidget* fw = QApplication::focusWidget();
        if (!m_finishing && fw && m_host && fw->window() == m_host->window() && fw != this && !isAncestorOf(fw))
            m_next->setFocus(Qt::OtherFocusReason);
    });

    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        QTimer::singleShot(0, this, [this]() {
            if (m_finishing || m_index < 0)
                return;
            m_card->fit();
            placeCardNow();
            update();
        });
    });

    if (host) {
        setGeometry(host->rect());
        host->installEventFilter(this);
    }
    qApp->installEventFilter(this);  // keys and shortcuts, and the move / resize of the highlighted widget

    show();
    raise();
    if (m_steps.isEmpty()) {
        QTimer::singleShot(0, this, [this]() { end(false); });
        return;
    }
    m_poll->start();
    showStep(0, true);
}

TourOverlay::~TourOverlay()
{
    if (qApp)
        qApp->removeEventFilter(this);
}

QString TourOverlay::currentStepId() const
{
    return m_index >= 0 && m_index < m_steps.size() ? m_steps.at(m_index).id : QString();
}

QStringList TourOverlay::stepIds() const
{
    QStringList ids;
    for (const TourStep& s : m_steps)
        ids << s.id;
    return ids;
}

QRect TourOverlay::cardRect() const
{
    return m_placement.card;
}

QString TourOverlay::overlineText() const
{
    return m_card->overline->text();
}

QString TourOverlay::titleText() const
{
    return m_card->title->text();
}

QString TourOverlay::bodyText() const
{
    return m_card->body->text();
}

int TourOverlay::bodyLines() const
{
    const QFontMetrics fm(m_card->body->font());
    const int width = m_card->body->width();
    if (width <= 0)
        return 0;
    return fm.boundingRect(QRect(0, 0, width, 10000), Qt::TextWordWrap, m_card->body->text()).height() / std::max(1, fm.lineSpacing());
}

QWidget* TourOverlay::liveTarget(int index) const
{
    if (index < 0 || index >= m_steps.size() || !m_host)
        return nullptr;
    const TourStep& step = m_steps.at(index);
    QWidget* t = step.target ? step.target() : nullptr;
    if (!t || !m_host->isAncestorOf(t) || !t->isVisibleTo(m_host))
        return nullptr;
    return t;
}

QRect TourOverlay::mapped(const QWidget* target) const
{
    return QRect(target->mapTo(m_host, QPoint(0, 0)), target->size());
}

void TourOverlay::showStep(int index, bool animate)
{
    if (m_finishing || m_steps.isEmpty() || !m_host)
        return;
    const int count = int(m_steps.size());
    index = std::clamp(index, 0, count - 1);
    const int from = m_index;
    const int dir = (from >= 0 && index < from) ? -1 : 1;
    const bool animating = animate && Motion::enabled();

    // the old card, as a picture: it fades out while the new one fades in
    QPixmap oldCard;
    QPoint oldPos;
    if (animating && from >= 0 && m_card->isVisible()) {
        oldCard = m_card->grab();
        oldPos = m_card->pos();
    }

    if (from >= 0 && from != index && m_steps.at(from).leave)
        m_steps.at(from).leave();

    int at = index;
    QWidget* target = nullptr;
    while (at >= 0 && at < count) {
        m_index = at;
        if (m_steps.at(at).enter)
            m_steps.at(at).enter();
        target = liveTarget(at);
        if (target)
            break;
        if (m_steps.at(at).leave)
            m_steps.at(at).leave();  // its target is gone: skip it
        at += dir;
    }
    if (!target && from >= 0 && dir < 0) {
        at = from;  // nothing before it can be shown: stay where we were
        m_index = at;
        if (m_steps.at(at).enter)
            m_steps.at(at).enter();
        target = liveTarget(at);
    }
    if (!target) {
        m_index = -1;
        end(dir > 0);
        return;
    }
    m_index = at;
    m_shown = true;
    const TourStep& step = m_steps.at(at);
    m_target = target;
    m_side = step.side;

    m_card->overline->setText(tr("Step %1 of %2").arg(at + 1).arg(count));
    m_card->title->setText(step.title);
    m_card->body->setText(step.body);
    m_back->setVisible(at > 0);
    m_next->setText(at == count - 1 ? tr("Finish") : tr("Next"));
    m_card->fit();

    m_spotTo = spotlightRect(mapped(target), m_host->rect());
    m_glide.stop();
    if (animating && from >= 0) {
        m_spotFrom = m_spot;
        m_glide.setDuration(kGlideMs);
        m_glide.start();
    } else {
        m_spot = QRectF(m_spotTo);
    }

    m_cardAnim.stop();
    m_oldCard = oldCard;
    m_oldCardPos = oldPos;
    m_placement = placeCard(m_spotTo, m_card->cardSize(), m_host->size(), m_side);
    m_card->setPointer(m_side, m_placement.pointer);
    m_card->show();
    if (animating) {
        auto* fx = new QGraphicsOpacityEffect(m_card);
        m_card->setGraphicsEffect(fx);  // replaces (and deletes) the previous one
        m_cardFx = fx;
        setCardProgress(0.0);
        m_cardAnim.setDuration(kCardMs);
        m_cardAnim.start();
    } else {
        m_card->setGraphicsEffect(nullptr);
        setCardProgress(1.0);
    }
    m_next->setFocus(Qt::OtherFocusReason);
    update();
}

void TourOverlay::setCardProgress(double p)
{
    m_cardProgress = p;
    if (m_cardFx)
        m_cardFx->setOpacity(p);
    if (p >= 1.0) {
        m_oldCard = QPixmap();
        if (m_cardFx)
            m_card->setGraphicsEffect(nullptr);  // deletes the effect
    }
    applyCardOffset();
    update();
}

void TourOverlay::applyCardOffset()
{
    const int slide = int(std::lround((1.0 - m_cardProgress) * kSlidePx));
    m_card->move(m_placement.card.topLeft() - QPoint(kShadowPad, kShadowPad) + QPoint(0, slide));
}

void TourOverlay::placeCardNow()
{
    m_placement = placeCard(m_spotTo, m_card->cardSize(), m_host->size(), m_side);
    m_card->setPointer(m_side, m_placement.pointer);
    applyCardOffset();
}

void TourOverlay::refreshTarget(bool force)
{
    if (m_finishing || m_index < 0 || !m_host)
        return;
    QWidget* t = liveTarget(m_index);
    if (!t)
        return;  // hidden for now (a page change in progress): keep what is shown
    m_target = t;
    const QRect spot = spotlightRect(mapped(t), m_host->rect());
    if (!force && spot == m_spotTo)
        return;
    m_spotTo = spot;
    if (m_glide.state() != QAbstractAnimation::Running)
        m_spot = QRectF(spot);  // a running glide already heads for the new m_spotTo
    placeCardNow();
    update();
}

void TourOverlay::next()
{
    if (m_finishing)
        return;
    if (m_index >= m_steps.size() - 1)
        finish();
    else
        showStep(m_index + 1, true);
}

void TourOverlay::back()
{
    if (m_finishing || m_index <= 0)
        return;
    showStep(m_index - 1, true);
}

void TourOverlay::skip()
{
    end(false);
}

void TourOverlay::finish()
{
    end(true);
}

void TourOverlay::end(bool completed)
{
    if (m_finishing)
        return;
    m_finishing = true;
    m_poll->stop();
    m_glide.stop();
    m_cardAnim.stop();
    qApp->removeEventFilter(this);
    if (m_host)
        m_host->removeEventFilter(this);
    if (m_index >= 0 && m_index < m_steps.size() && m_steps.at(m_index).leave)
        m_steps.at(m_index).leave();  // e.g. back to the page the tour started on
    if (m_settings && m_shown)
        m_settings->set(QStringLiteral("ZeroTourDone"), true);
    m_oldCard = QPixmap();
    if (m_cardFx)
        m_card->setGraphicsEffect(nullptr);
    m_cardProgress = 1.0;
    applyCardOffset();

    auto done = [this, completed]() {
        hide();
        deleteLater();
        emit finished(completed);
    };
    const int ms = Motion::duration(kFadeOutMs);
    if (ms <= 0) {
        done();
        return;
    }
    auto* fx = new QGraphicsOpacityEffect(this);
    fx->setOpacity(1.0);
    setGraphicsEffect(fx);
    auto* fade = new QPropertyAnimation(fx, "opacity", this);
    fade->setDuration(ms);
    fade->setStartValue(1.0);
    fade->setEndValue(0.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    connect(fade, &QPropertyAnimation::finished, this, done);
    fade->start();
}

void TourOverlay::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // the dim layer: the theme's bg at 55 % over everything, with the spotlight cut out
    QColor dim = t.bg;
    dim.setAlpha(kDimAlpha);
    QPainterPath all;
    all.addRect(QRectF(rect()));
    const QRectF spot = m_spot;
    if (!spot.isEmpty()) {
        const QPainterPath hole = Paint::panelPath(spot, t.radiusLg);
        p.fillPath(all.subtracted(hole), dim);
        // a 2 px accent ring just outside the cut-out
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(t.accent, 2));
        p.drawPath(Paint::panelPath(spot.adjusted(-1, -1, 1, 1), t.radiusLg + 1));
    } else {
        p.fillPath(all, dim);
    }

    if (!m_oldCard.isNull() && m_cardProgress < 1.0) {
        p.setOpacity(1.0 - m_cardProgress);
        p.drawPixmap(m_oldCardPos, m_oldCard);
    }
}

void TourOverlay::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    raise();
}

void TourOverlay::mousePressEvent(QMouseEvent* e)
{
    e->accept();  // nothing reaches the shell
}

void TourOverlay::mouseReleaseEvent(QMouseEvent* e)
{
    e->accept();
}

void TourOverlay::wheelEvent(QWheelEvent* e)
{
    e->accept();
}

bool TourOverlay::focusNextPrevChild(bool next)
{
    // Tab cycles Skip tour / Back / Next; it never reaches the shell behind.
    QList<QWidget*> chain;
    chain << m_skipButton;
    if (!m_back->isHidden())
        chain << m_back;
    chain << m_next;
    const int at = int(chain.indexOf(focusWidget()));
    const int n = int(chain.size());
    const int to = at < 0 ? (next ? 0 : n - 1) : (at + (next ? 1 : n - 1)) % n;
    chain[to]->setFocus(next ? Qt::TabFocusReason : Qt::BacktabFocusReason);
    return true;
}

bool TourOverlay::inOurWindow(QObject* o) const
{
    auto* w = qobject_cast<QWidget*>(o);
    return w && m_host && w->window() == m_host->window();
}

bool TourOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (m_finishing)
        return false;
    if (watched == m_host.data()) {
        switch (event->type()) {
            case QEvent::Resize:
                setGeometry(m_host->rect());
                refreshTarget(true);
                break;
            case QEvent::ChildAdded:
            case QEvent::ChildRemoved:
                // anything the window gains later (toasts, the palette) must not end up above the tour
                QTimer::singleShot(0, this, [this]() {
                    if (!m_finishing)
                        raise();
                });
                break;
            default:
                break;
        }
        return false;
    }
    if (watched == m_target.data()) {
        switch (event->type()) {
            case QEvent::Move:
            case QEvent::Resize:
            case QEvent::Show:
            case QEvent::Hide:
                refreshTarget();
                break;
            default:
                break;
        }
        return false;
    }
    if (!inOurWindow(watched))
        return false;
    auto* w = static_cast<QWidget*>(watched);
    const bool ours = w == this || isAncestorOf(w);

    if (event->type() == QEvent::ShortcutOverride) {
        // The shell's shortcuts (Ctrl+K, Alt+N ...) must not fire over the tour: the key goes on as a plain key press.
        event->accept();
        return true;
    }
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        switch (key->key()) {
            case Qt::Key_Escape:
                if (!key->isAutoRepeat())
                    skip();
                return true;
            case Qt::Key_Right:
                if (!key->isAutoRepeat())
                    next();
                return true;
            case Qt::Key_Left:
                if (!key->isAutoRepeat())
                    back();
                return true;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                if (key->isAutoRepeat())
                    return true;
                if (auto* button = qobject_cast<QPushButton*>(w); ours && button && button != m_next)
                    button->click();  // Enter on the focused Skip tour / Back button
                else
                    next();
                return true;
            default:
                break;
        }
        if (!ours) {
            m_next->setFocus(Qt::OtherFocusReason);  // typing into the shell behind is blocked
            return true;
        }
    }
    return false;
}

}  // namespace Onboarding
}  // namespace Zero
