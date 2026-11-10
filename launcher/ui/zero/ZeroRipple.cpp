// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroRipple.h"

#include <QAbstractButton>
#include <QApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QRadialGradient>
#include <QVariantAnimation>
#include "ZeroMotion.h"
#include "ZeroPaint.h"
#include "ZeroTheme.h"
#include "ZeroThemeState.h"

namespace Zero {
namespace Ripple {

namespace {

const char* const kRippleName = "zeroRipple";
constexpr qreal kRippleAlpha = 0.10;
constexpr int kExpandMs = Tokens::durSlow;  // 280
constexpr int kFadeMs = Tokens::durBase;    // 200

class RippleWidget : public QWidget {
   public:
    RippleWidget(QAbstractButton* button, const QPoint& origin)
        : QWidget(button), m_origin(origin), m_expand(new QVariantAnimation(this)), m_fade(new QVariantAnimation(this))
    {
        setObjectName(QString::fromLatin1(kRippleName));
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setFocusPolicy(Qt::NoFocus);
        setGeometry(button->rect());
        m_inset = button->property("zeroRippleInset").toInt();

        // Farthest corner from the press point: the circle then covers the whole shape at the end.
        const QRect r = button->rect();
        m_maxRadius = 0;
        for (const QPoint& c : { r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight() })
            m_maxRadius = qMax(m_maxRadius, qreal(QLineF(origin, c).length()));

        m_expand->setDuration(kExpandMs);
        m_expand->setStartValue(0.0);
        m_expand->setEndValue(1.0);
        m_expand->setEasingCurve(Motion::easeEnter());
        connect(m_expand, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            m_radiusT = v.toReal();
            update();
        });
        connect(m_expand, &QVariantAnimation::finished, this, [this]() {
            m_expanded = true;
            maybeFinish();
        });

        m_fade->setDuration(kFadeMs);
        m_fade->setStartValue(1.0);
        m_fade->setEndValue(0.0);
        m_fade->setEasingCurve(Motion::easeExit());
        connect(m_fade, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            m_alpha = v.toReal();
            update();
        });
        connect(m_fade, &QVariantAnimation::finished, this, [this]() {
            m_faded = true;
            maybeFinish();
        });

        raise();
        show();
        m_expand->start();
    }

    /// Mouse released (or the button went away): start fading out.
    void release()
    {
        if (m_released)
            return;
        m_released = true;
        m_fade->start();
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF shape = QRectF(rect()).adjusted(m_inset, 0, -m_inset, 0);
        p.setClipPath(Paint::panelPath(shape, t.radiusMd));
        const qreal radius = m_maxRadius * m_radiusT;
        if (radius <= 0.5)
            return;
        QColor c = t.text;
        c.setAlphaF(kRippleAlpha * m_alpha);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(QPointF(m_origin), radius, radius);
    }

   private:
    void maybeFinish()
    {
        if (m_faded) {
            hide();
            deleteLater();
        }
    }

    QPoint m_origin;
    QVariantAnimation* m_expand;
    QVariantAnimation* m_fade;
    qreal m_maxRadius = 0;
    qreal m_radiusT = 0;
    qreal m_alpha = 1.0;
    int m_inset = 0;
    bool m_released = false, m_expanded = false, m_faded = false;
};

RippleWidget* rippleOf(QObject* button)
{
    // RippleWidget has no Q_OBJECT: look it up as a plain QWidget by name.
    return button ? static_cast<RippleWidget*>(button->findChild<QWidget*>(QString::fromLatin1(kRippleName), Qt::FindDirectChildrenOnly)) : nullptr;
}

bool wantsRipple(const QAbstractButton* b)
{
    return b->isEnabled() && (b->property("zeroRipple").toBool() || !b->property("zeroVariant").toString().isEmpty());
}

class RippleFilter : public QObject {
   public:
    using QObject::QObject;

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        switch (event->type()) {
            case QEvent::MouseButtonPress: {
                auto* button = qobject_cast<QAbstractButton*>(watched);
                auto* me = static_cast<QMouseEvent*>(event);
                if (!button || me->button() != Qt::LeftButton || !isThemeActive() || Theme::current().bevel || !Motion::enabled() || !wantsRipple(button))
                    return false;
                while (RippleWidget* old = rippleOf(button)) {
                    old->hide();
                    delete old;
                }
                new RippleWidget(button, me->position().toPoint());
                return false;
            }
            case QEvent::MouseButtonRelease:
            case QEvent::Hide:
            case QEvent::EnabledChange:
                if (auto* button = qobject_cast<QAbstractButton*>(watched)) {
                    if (RippleWidget* r = rippleOf(button))
                        r->release();
                }
                return false;
            default:
                return false;
        }
    }
};

}  // namespace

void install()
{
    static QPointer<RippleFilter> filter;
    if (filter || !qApp)
        return;
    filter = new RippleFilter(qApp);
    qApp->installEventFilter(filter);
}

int rippleCount(QObject* button)
{
    return button ? int(button->findChildren<QWidget*>(QString::fromLatin1(kRippleName), Qt::FindDirectChildrenOnly).size()) : 0;
}

}  // namespace Ripple
}  // namespace Zero
