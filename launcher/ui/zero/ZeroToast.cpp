// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroToast.h"

#include <QEvent>
#include <QFontMetrics>
#include <QGraphicsDropShadowEffect>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include "ZeroIcons.h"
#include "ZeroMotion.h"
#include "ZeroStyle.h"
#include "ZeroThemeState.h"

namespace Zero {

namespace {
constexpr int kShadowMargin = 40;  // room for blur 24 + offset 8
constexpr int kTextMaxWidth = 300;

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
            p.drawRoundedRect(rect(), Tokens::radiusLg, Tokens::radiusLg);
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
            ZeroToast::restack(m_host);
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

    auto* icon = new QLabel(this);
    icon->setFixedSize(16, 16);
    icon->setPixmap(Icons::pixmap(iconName, iconColor, 16, devicePixelRatioF()));
    lay->addWidget(icon, 0, Qt::AlignTop);

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
    restack(host);

    m_shadow->show();
    QWidget::show();
    m_shadow->stackUnder(this);
    raise();
    Motion::fadeIn(this);
    Motion::fadeIn(m_shadow);
    m_timer.start();
}

void ZeroToast::place(const QPoint& cardTopLeft)
{
    move(cardTopLeft);
    if (m_shadow)
        static_cast<ToastShadow*>(m_shadow.data())->setCardRect(QRect(cardTopLeft, size()));
}

void ZeroToast::restack(QWidget* host)
{
    auto& list = toastsFor(host);
    list.removeIf([](const QPointer<ZeroToast>& p) { return p.isNull(); });
    int bottom = host->height() - Tokens::spaceXl;
    for (const auto& p : std::as_const(list)) {  // oldest at the bottom, newer above
        if (p->m_dismissing)
            continue;
        p->adjustSize();
        const QSize s = p->size();
        p->place(QPoint(host->width() - Tokens::spaceXl - s.width(), bottom - s.height()));
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
    QWidget* host = parentWidget();
    QPointer<ZeroToast> self(this);
    Motion::fadeOut(m_shadow, Tokens::durPage);
    Motion::fadeOut(this, Tokens::durPage, [self, host]() {
        if (!self)
            return;
        emit self->dismissed();
        self->deleteLater();
        if (host)
            QMetaObject::invokeMethod(host, [host]() { ZeroToast::restack(host); }, Qt::QueuedConnection);
    });
}

void ZeroToast::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(t.elevated);
    p.setPen(QPen(t.border, 1));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), Tokens::radiusLg, Tokens::radiusLg);
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
