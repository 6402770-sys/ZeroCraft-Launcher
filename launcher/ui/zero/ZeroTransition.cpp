// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroTransition.h"

#include <QEvent>
#include <QImage>
#include <QPaintEvent>
#include <QPainter>
#include <QPointer>
#include <QRegion>
#include <QVariantAnimation>
#include <QWidget>
#include <algorithm>
#include "ZeroMotion.h"

namespace Zero {
namespace Transition {

namespace {

constexpr int kBlockPx = 16;       // dissolve block edge (logical px)
constexpr int kRisePx = 10;        // distance the new content rises in Kind::Rise
const char* const kOverlayName = "zeroTransitionOverlay";

int defaultDuration(Kind kind)
{
    switch (kind) {
        case Kind::Crossfade:
            return Tokens::durFast;
        case Kind::Rise:
            return Tokens::durBase;
        case Kind::PixelDissolve:
            return Tokens::durDissolve;
    }
    return Tokens::durBase;
}

QWidget* overlayOf(QWidget* host)
{
    return host ? host->findChild<QWidget*>(QString::fromLatin1(kOverlayName), Qt::FindDirectChildrenOnly) : nullptr;
}

/// What `host` shows right now, as pixels of its top-level window (so the window background, the stone texture and
/// any sibling that paints behind a transparent host are part of the picture).
QPixmap grabHost(QWidget* host)
{
    QWidget* window = host->window();
    if (window == host)
        return host->grab();
    return window->grab(QRect(host->mapTo(window, QPoint(0, 0)), host->size()));
}

class Overlay : public QWidget {
   public:
    Overlay(QWidget* host, Kind kind, const QPixmap& from, int ms)
        : QWidget(host), m_kind(kind), m_from(from), m_anim(new QVariantAnimation(this))
    {
        setObjectName(QString::fromLatin1(kOverlayName));
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setFocusPolicy(Qt::NoFocus);
        setGeometry(host->rect());
        host->installEventFilter(this);

        if (kind == Kind::PixelDissolve) {
            const qreal dpr = from.devicePixelRatio();
            m_blockDev = std::max(1, qRound(kBlockPx * dpr));
            m_image = from.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
            m_cols = (m_image.width() + m_blockDev - 1) / m_blockDev;
            m_rows = (m_image.height() + m_blockDev - 1) / m_blockDev;
            m_order = dissolveOrder(m_cols, m_rows);
            m_from = QPixmap();  // the image is the picture now
        }

        m_anim->setDuration(ms);
        m_anim->setStartValue(0.0);
        m_anim->setEndValue(1.0);
        m_anim->setEasingCurve(QEasingCurve::Linear);
        connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { advance(v.toReal()); });
        connect(m_anim, &QVariantAnimation::finished, this, [this]() {
            hide();
            deleteLater();
        });

        raise();
        show();
        m_anim->start();
    }

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == parentWidget() && event->type() == QEvent::Resize)
            setGeometry(parentWidget()->rect());
        return false;
    }

    void paintEvent(QPaintEvent* event) override
    {
        QPainter p(this);
        const qreal t = m_progress;
        switch (m_kind) {
            case Kind::Crossfade:
                p.setOpacity(1.0 - QEasingCurve(QEasingCurve::InOutSine).valueForProgress(t));
                p.drawPixmap(0, 0, m_from);
                break;
            case Kind::Rise: {
                // The old picture is the opaque base (the overlay must hide the live widgets); the new picture
                // rises and fades in over it, which also fades the old one out.
                p.drawPixmap(0, 0, m_from);
                if (!m_to.isNull()) {
                    const qreal e = Motion::easeEnter().valueForProgress(t);
                    p.setOpacity(e);
                    p.drawPixmap(0, qRound(kRisePx * (1.0 - e)), m_to);
                }
                break;
            }
            case Kind::PixelDissolve:
                for (const QRect& r : event->region())
                    p.drawImage(QRectF(r), m_image, QRectF(QPointF(r.topLeft()) * m_image.devicePixelRatio(), QSizeF(r.size()) * m_image.devicePixelRatio()));
                break;
        }
    }

   private:
    void advance(qreal t)
    {
        m_progress = t;
        if (m_kind == Kind::Rise && m_to.isNull() && t > 0.0) {
            // The picture of the new content is taken on the first tick, not at creation: by now the event loop has
            // run the pending layouts (item views lay out lazily), so it matches what the live widgets settle on.
            // The overlay is hidden for the grab so it does not show up in its own picture.
            hide();
            m_to = grabHost(parentWidget());
            show();
        }
        if (m_kind != Kind::PixelDissolve) {
            update();
            return;
        }
        const int total = int(m_order.size());
        const int target = std::min(total, int(t * total));
        if (target <= m_removed)
            return;
        QRegion dirty;
        {
            QPainter ip(&m_image);
            ip.setCompositionMode(QPainter::CompositionMode_Source);
            const qreal dpr = m_image.devicePixelRatio();
            for (int i = m_removed; i < target; ++i) {
                const quint32 b = m_order[i];
                const QRect dev((b % m_cols) * m_blockDev, (b / m_cols) * m_blockDev, m_blockDev, m_blockDev);
                ip.fillRect(dev, Qt::transparent);
                dirty += QRectF(dev.x() / dpr, dev.y() / dpr, dev.width() / dpr, dev.height() / dpr).toAlignedRect();
            }
        }
        m_removed = target;
        update(dirty);
    }

    Kind m_kind;
    QPixmap m_from;
    QPixmap m_to;
    QImage m_image;  // dissolve only: the old picture with removed blocks made transparent
    QList<quint32> m_order;
    int m_blockDev = 1, m_cols = 0, m_rows = 0, m_removed = 0;
    qreal m_progress = 0.0;
    QVariantAnimation* m_anim;
};

}  // namespace

QList<quint32> dissolveOrder(int cols, int rows)
{
    QList<quint32> order;
    const qint64 n = qint64(std::max(0, cols)) * std::max(0, rows);
    order.reserve(int(n));
    for (qint64 i = 0; i < n; ++i)
        order.append(quint32(i));
    // Fisher-Yates with a fixed xorshift32 stream: the same grid always dissolves in the same order.
    quint32 s = 0x9E3779B9u;
    for (qint64 i = n - 1; i > 0; --i) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        std::swap(order[i], order[qint64(s % quint32(i + 1))]);
    }
    return order;
}

Snapshot capture(QWidget* host)
{
    if (!host || !Motion::enabled() || !host->isVisible() || host->width() <= 0 || host->height() <= 0)
        return {};
    return { grabHost(host) };
}

void cancel(QWidget* host)
{
    while (QWidget* old = overlayOf(host)) {
        old->hide();
        delete old;
    }
}

int overlayCount(QWidget* host)
{
    return host ? int(host->findChildren<QWidget*>(QString::fromLatin1(kOverlayName), Qt::FindDirectChildrenOnly).size()) : 0;
}

void run(QWidget* host, const Snapshot& from, Kind kind, int ms)
{
    if (!host)
        return;
    cancel(host);  // also when this call ends up doing nothing: a stale overlay must not outlive a newer change
    if (from.isNull() || !Motion::enabled() || !host->isVisible())
        return;
    if (ms <= 0)
        ms = defaultDuration(kind);
    ms = Motion::duration(ms);
    if (ms <= 0)
        return;
    new Overlay(host, kind, from.pixmap, ms);
}

void run(QWidget* host, Kind kind, int ms, const std::function<void()>& change)
{
    const Snapshot before = capture(host);
    if (change)
        change();
    run(host, before, kind, ms);
}

}  // namespace Transition
}  // namespace Zero
