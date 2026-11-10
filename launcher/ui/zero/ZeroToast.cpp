// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroToast.h"

#include <QEvent>
#include <QFontMetrics>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QVariantAnimation>
#include <utility>
#include "ZeroIcons.h"
#include "ZeroMotion.h"
#include "ZeroStyle.h"
#include "ZeroPaint.h"
#include "ZeroThemeState.h"

namespace Zero {

namespace {
constexpr int kShadowMargin = 40;  // room for blur 24 + offset 8
constexpr int kTextMaxWidth = 300;
constexpr int kSlideDistance = 24;  // px the toast slides in from / out to (right)
constexpr int kEnterMs = 220;
constexpr int kExitMs = 160;

QList<QPointer<ZeroToast>>& toastsFor(QWidget* host)
{
    static QHash<QWidget*, QList<QPointer<ZeroToast>>> all;
    return all[host];
}
}  // namespace

/// Draws only the drop shadow behind a toast: a plain rounded rectangle carrying a QGraphicsDropShadowEffect.
/// Kept as a mouse-transparent sibling so the blank shadow margin never swallows clicks meant for the window.
class ToastShadow : public QWidget {
   public:
    explicit ToastShadow(QWidget* parent) : QWidget(parent), m_source(new Source(this))
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        auto* fx = new QGraphicsDropShadowEffect(m_source);
        fx->setBlurRadius(Tokens::shadowBlur);
        fx->setOffset(0, Tokens::shadowOffsetY);
        fx->setColor(QColor(0, 0, 0, Tokens::shadowAlpha));
        m_source->setGraphicsEffect(fx);
    }
    void setCardRect(const QRect& card)
    {
        setGeometry(card.adjusted(-kShadowMargin, -kShadowMargin, kShadowMargin, kShadowMargin));
        m_source->setGeometry(kShadowMargin, kShadowMargin, card.width(), card.height());
    }

   private:
    class Source : public QWidget {
       public:
        using QWidget::QWidget;

       protected:
        void paintEvent(QPaintEvent*) override
        {
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::current().elevated);
            p.drawPath(Paint::panelPath(QRectF(rect()), Theme::current().radiusLg));
        }
    };
    Source* m_source;
};

/// Per-window helper: restacks the toasts when the window resizes.
class ToastHost : public QObject {
   public:
    explicit ToastHost(QWidget* host) : QObject(host), m_host(host) { host->installEventFilter(this); }
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_host && event->type() == QEvent::Resize)
            ZeroToast::restack(m_host, false);  // window resize: follow instantly, no glide
        return false;
    }
    static ToastHost* ensure(QWidget* host)
    {
        static QHash<QWidget*, QPointer<ToastHost>> hosts;
        if (auto h = hosts.value(host))
            return h.data();
        auto* h = new ToastHost(host);
        hosts.insert(host, h);
        return h;
    }

   private:
    QWidget* m_host;
};

namespace {

/// 8x8 original gem bitmap drawn at 2x (16x16 px) with a 1 px darker outline. Legend: o outline, h highlight, b base, d shade.
class PixelGem : public QWidget {
   public:
    PixelGem(const QColor& color, QWidget* parent) : QWidget(parent), m_color(color) { setFixedSize(16, 16); }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        static const char* const rows[8] = { "..oooo..", ".ohhbbo.", "ohhbbbbo", "ohbbbbdo", "obbbbbdo", ".obbbdo.", "..obdo..", "...oo..." };
        const auto& t = Theme::current();
        const QColor outline = mix(m_color, t.bevelDark, 0.7);
        const QColor light = mix(m_color, Qt::white, 0.45);
        const QColor shade = mix(m_color, t.bevelDark, 0.3);
        QPainter p(this);
        p.setPen(Qt::NoPen);
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                QColor c;
                switch (rows[y][x]) {
                    case 'o': c = outline; break;
                    case 'h': c = light; break;
                    case 'b': c = m_color; break;
                    case 'd': c = shade; break;
                    default: continue;
                }
                p.fillRect(x * 2, y * 2, 2, 2, c);
            }
        }
    }

   private:
    QColor m_color;
};

}  // namespace

ZeroToast::ZeroToast(QWidget* host, const QString& text, Kind kind, const QString& actionText, std::function<void()> action)
    : QWidget(host)
{
    setAttribute(Qt::WA_StyledBackground, false);
    setObjectName(QStringLiteral("zeroToast"));

    const auto& t = Theme::current();
    QString iconName;
    QColor iconColor;
    switch (kind) {
        case Kind::Success:
            iconName = "circle-check";
            iconColor = t.success;
            break;
        case Kind::Warning:
            iconName = "triangle-alert";
            iconColor = t.warning;
            break;
        case Kind::Error:
            iconName = "circle-x";
            iconColor = t.danger;
            break;
        default:
            iconName = "info";
            iconColor = t.info;
            break;
    }

    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(Tokens::spaceMd, Tokens::spaceMd, Tokens::spaceMd, Tokens::spaceMd);
    lay->setSpacing(Tokens::spaceMd);

    if (t.corners == Corners::Notched) {
        // Minecraft theme: a pixel gem (emerald / gold / redstone / lapis) instead of the vector status icon.
        lay->addWidget(new PixelGem(iconColor, this), 0, Qt::AlignVCenter);
    } else {
        auto* icon = new QLabel(this);
        icon->setFixedSize(16, 16);
        icon->setPixmap(Icons::pixmap(iconName, iconColor, 16, devicePixelRatioF()));
        lay->addWidget(icon, 0, Qt::AlignTop);
    }

    auto* label = new QLabel(text, this);
    Style::applyRole(label, Role::Body);
    label->setWordWrap(true);
    // QLabel word-wrap sizing is aspect-ratio driven and wraps short text needlessly: size to the text, capped.
    label->setFixedWidth(qMin(kTextMaxWidth, QFontMetrics(label->font()).horizontalAdvance(text) + 6));
    label->setTextInteractionFlags(Qt::NoTextInteraction);
    lay->addWidget(label, 1, Qt::AlignVCenter);

    if (!actionText.isEmpty()) {
        auto* btn = new QPushButton(actionText, this);
        Style::setProperty(btn, "zeroVariant", "ghost");
        btn->setCursor(Qt::PointingHandCursor);
        connect(btn, &QPushButton::clicked, this, [this, action]() {
            if (action)
                action();
            dismiss();
        });
        lay->addWidget(btn, 0, Qt::AlignVCenter);
    }

    auto* close = new QToolButton(this);
    Style::setProperty(close, "zeroVariant", "ghost");
    close->setIcon(Icons::icon("x", t.textSecondary));
    close->setIconSize(QSize(14, 14));
    close->setCursor(Qt::PointingHandCursor);
    close->setToolTip(tr("Dismiss"));
    close->setAutoRaise(true);
    connect(close, &QToolButton::clicked, this, &ZeroToast::dismiss);
    lay->addWidget(close, 0, Qt::AlignTop);

    m_timer.setSingleShot(true);
    m_timer.setInterval(Tokens::durToast);
    connect(&m_timer, &QTimer::timeout, this, &ZeroToast::dismiss);

    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

ZeroToast::~ZeroToast()
{
    if (m_shadow)
        delete m_shadow;
}

ZeroToast* ZeroToast::show(QWidget* window, const QString& text, Kind kind, const QString& actionText, std::function<void()> action)
{
    if (!window)
        return nullptr;
    QWidget* host = window->window();
    auto* toast = new ZeroToast(host, text, kind, actionText, std::move(action));
    toast->presentIn(host);
    return toast;
}

int ZeroToast::count(QWidget* window)
{
    if (!window)
        return 0;
    int n = 0;
    for (const auto& t : toastsFor(window->window()))
        if (t && !t->m_dismissing)
            n++;
    return n;
}

void ZeroToast::presentIn(QWidget* host)
{
    ToastHost::ensure(host);
    toastsFor(host).append(this);

    m_shadow = new ToastShadow(host);
    adjustSize();
    m_presence = 0.0;  // starts slid out + transparent; the first placement is instant, the older toasts glide up
    restack(host);

    m_shadow->show();
    QWidget::show();
    m_shadow->stackUnder(this);
    raise();
    applyPresence(0.0);
    animatePresence(1.0, kEnterMs, Motion::easeEnter());
    m_timer.start();
}

void ZeroToast::setSlot(const QPoint& target, bool animate)
{
    if (!m_placed || !animate || Motion::duration(Tokens::durBase) == 0) {
        if (m_slotAnim)
            m_slotAnim->stop();
        m_slotPos = target;
        m_slotTarget = target;
        m_placed = true;
        layoutCard();
        return;
    }
    if (target == m_slotTarget)
        return;
    m_slotTarget = target;
    if (!m_slotAnim) {
        m_slotAnim = new QVariantAnimation(this);
        m_slotAnim->setEasingCurve(Motion::easeGlide());
        connect(m_slotAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            m_slotPos = v.toPointF();
            layoutCard();
        });
    }
    m_slotAnim->stop();
    m_slotAnim->setDuration(Motion::duration(Tokens::durBase));
    m_slotAnim->setStartValue(m_slotPos);
    m_slotAnim->setEndValue(QPointF(target));
    m_slotAnim->start();
}

void ZeroToast::layoutCard()
{
    place(QPoint(qRound(m_slotPos.x() + (1.0 - m_presence) * kSlideDistance), qRound(m_slotPos.y())));
}

void ZeroToast::applyPresence(qreal presence)
{
    m_presence = presence;
    for (QWidget* w : { static_cast<QWidget*>(this), m_shadow.data() }) {
        if (!w)
            continue;
        if (presence >= 1.0) {
            // Settled: drop the effect so the toast paints directly again (and its own shadow effect is untouched).
            if (qobject_cast<QGraphicsOpacityEffect*>(w->graphicsEffect()))
                w->setGraphicsEffect(nullptr);
        } else {
            auto* fx = qobject_cast<QGraphicsOpacityEffect*>(w->graphicsEffect());
            if (!fx) {
                fx = new QGraphicsOpacityEffect(w);
                w->setGraphicsEffect(fx);
            }
            fx->setOpacity(presence);
        }
    }
    layoutCard();
}

void ZeroToast::animatePresence(qreal to, int ms, const QEasingCurve& curve, std::function<void()> finished)
{
    if (m_presenceAnim)
        m_presenceAnim->stop();
    if (Motion::duration(ms) == 0 || qFuzzyCompare(m_presence, to)) {
        applyPresence(to);
        if (finished)
            finished();
        return;
    }
    if (!m_presenceAnim) {
        m_presenceAnim = new QVariantAnimation(this);
        connect(m_presenceAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { applyPresence(v.toReal()); });
        connect(m_presenceAnim, &QVariantAnimation::finished, this, [this]() {
            auto done = std::exchange(m_presenceDone, {});
            if (done)
                done();
        });
    }
    m_presenceDone = std::move(finished);
    m_presenceAnim->setDuration(Motion::duration(ms));
    m_presenceAnim->setEasingCurve(curve);
    m_presenceAnim->setStartValue(m_presence);
    m_presenceAnim->setEndValue(to);
    m_presenceAnim->start();
}

void ZeroToast::place(const QPoint& cardTopLeft)
{
    move(cardTopLeft);
    if (m_shadow)
        static_cast<ToastShadow*>(m_shadow.data())->setCardRect(QRect(cardTopLeft, size()));
}

void ZeroToast::restack(QWidget* host, bool animate)
{
    auto& list = toastsFor(host);
    list.removeIf([](const QPointer<ZeroToast>& p) { return p.isNull(); });
    int bottom = host->height() - Tokens::spaceXl;
    for (const auto& p : std::as_const(list)) {  // oldest at the bottom, newer above
        if (p->m_dismissing)
            continue;
        p->adjustSize();
        const QSize s = p->size();
        p->setSlot(QPoint(host->width() - Tokens::spaceXl - s.width(), bottom - s.height()), animate);
        bottom -= s.height() + Tokens::spaceSm;
    }
    for (const auto& p : std::as_const(list))
        if (p && p->m_shadow) {
            p->m_shadow->stackUnder(p);
            p->raise();
        }
}

void ZeroToast::dismiss()
{
    if (m_dismissing)
        return;
    m_dismissing = true;
    m_timer.stop();
    if (QWidget* host = parentWidget())
        restack(host);  // the others glide into the freed slot while this one slides out
    animatePresence(0.0, kExitMs, Motion::easeExit(), [this]() { finishDismiss(); });
}

void ZeroToast::finishDismiss()
{
    hide();
    if (m_shadow)
        m_shadow->hide();
    emit dismissed();
    deleteLater();
}

void ZeroToast::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(t.elevated);
    p.setPen(QPen(t.border, 1));
    p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusLg));
}

void ZeroToast::enterEvent(QEnterEvent* event)
{
    QWidget::enterEvent(event);
    m_timer.stop();
}

void ZeroToast::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    if (!m_dismissing)
        m_timer.start();
}

}  // namespace Zero
