// SPDX-License-Identifier: GPL-3.0-only
#include "SelectionBar.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>

#include <algorithm>

#include "ContentDraw.h"
#include "RemoteImageCache.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "zero/content/InstallCart.h"

namespace Zero::Content {

/// The moving part: `elevated` surface with a 1 px top border, the row of widgets inside.
class SelectionBar::Surface : public QWidget {
   public:
    explicit Surface(QWidget* parent) : QWidget(parent) { setObjectName(QStringLiteral("zeroSelectionBarSurface")); }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        const auto& t = Theme::current();
        p.fillRect(rect(), t.elevated);
        p.fillRect(QRect(0, 0, width(), 1), t.border);
    }
};

/// Up to four overlapping item icons, newest on top.
class SelectionBar::IconStack : public QWidget {
   public:
    IconStack(InstallCartModel* cart, Zero::RemoteImageCache* images, QWidget* parent) : QWidget(parent), m_cart(cart), m_images(images)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        connect(m_images, &Zero::RemoteImageCache::imageReady, this, [this]() { update(); });
    }

    int count() const { return m_count; }
    void refresh()
    {
        m_count = m_cart ? std::min(int(m_cart->count()), kMaxIcons) : 0;
        setFixedSize(m_count > 0 ? kIconSize + (m_count - 1) * (kIconSize - kIconOverlap) : 0, kIconSize);
        update();
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        if (!m_cart) {
            return;
        }
        const auto& t = Theme::current();
        const qreal dpr = devicePixelRatioF();
        const auto& entries = m_cart->entries();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        for (int i = 0; i < m_count && i < entries.size(); ++i) {
            const QRect tile(i * (kIconSize - kIconOverlap), 0, kIconSize, kIconSize);
            // The ring in the bar's colour separates a tile from the one under it.
            p.setPen(Qt::NoPen);
            p.setBrush(t.elevated);
            p.drawPath(Paint::panelPath(QRectF(tile).adjusted(-2, -2, 2, 2), t.radiusMd + 2));
            const QPainterPath shape = Paint::panelPath(QRectF(tile), t.radiusMd);
            p.setBrush(t.higher);
            p.drawPath(shape);
            const ContentItemPtr& item = entries.at(i).item;
            QPixmap pixmap;
            if (item && !item->iconUrl.isEmpty() && m_images && !m_images->hasFailed(item->iconUrl)) {
                pixmap = m_images->request(item->iconUrl, Zero::RemoteImageCache::bucketFor(kIconSize, dpr), this);
            }
            if (!pixmap.isNull()) {
                p.save();
                p.setClipPath(shape);
                Draw::drawCover(&p, tile, pixmap);
                p.restore();
            } else {
                Icons::icon(QStringLiteral("package"), t.textTertiary).paint(&p, QRect(tile.center().x() - 7, tile.center().y() - 7, 14, 14));
            }
        }
    }

   private:
    QPointer<InstallCartModel> m_cart;
    Zero::RemoteImageCache* m_images;
    int m_count = 0;
};

SelectionBar::SelectionBar(InstallCartModel* cart, Zero::RemoteImageCache* images, QWidget* parent) : QWidget(parent), m_cart(cart)
{
    setObjectName(QStringLiteral("zeroSelectionBar"));
    setFixedHeight(kHeight);
    setAccessibleName(tr("Selection"));
    m_surface = new Surface(this);
    auto* row = new QHBoxLayout(m_surface);
    row->setContentsMargins(Tokens::spaceLg, 0, Tokens::spaceLg, 0);
    row->setSpacing(Tokens::spaceMd);

    m_icons = new IconStack(cart, images ? images : Zero::RemoteImageCache::instance(), m_surface);
    m_more = new QLabel(m_surface);
    Style::applyRole(m_more, Role::Caption);
    m_summary = new QLabel(m_surface);
    Style::applyRole(m_summary, Role::Label);
    m_clear = new QPushButton(tr("Clear"), m_surface);
    Style::setProperty(m_clear, "zeroVariant", "ghost");
    m_clear->setCursor(Qt::PointingHandCursor);
    m_review = new QPushButton(m_surface);
    Style::setProperty(m_review, "zeroVariant", "primary");
    m_review->setCursor(Qt::PointingHandCursor);

    row->addWidget(m_icons);
    row->addWidget(m_more);
    row->addWidget(m_summary);
    row->addWidget(m_clear);
    row->addStretch(1);
    row->addWidget(m_review);

    connect(m_clear, &QPushButton::clicked, this, [this]() {
        if (m_cart) {
            m_cart->clear();
        }
    });
    connect(m_review, &QPushButton::clicked, this, &SelectionBar::reviewRequested);

    m_slide.setDuration(kSlideMs);
    m_slide.setEasingCurve(Motion::easeEnter());
    m_slide.setStartValue(kHeight);
    m_slide.setEndValue(0);
    connect(&m_slide, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) { setOffset(value.toInt()); });
    connect(&Theme::instance(), &Theme::changed, this, [this]() { m_surface->update(); });

    if (m_cart) {
        connect(m_cart, &InstallCartModel::changed, this, &SelectionBar::sync);
    }
    setTargetName(QString());
    sync();
    hide();  // shown by sync() once the cart holds something
}

void SelectionBar::setTargetName(const QString& name)
{
    m_targetName = name;
    if (name.isEmpty()) {
        m_review->setText(tr("Review and add"));
    } else {
        const QFontMetrics metrics(m_review->font());
        m_review->setText(tr("Review and add to %1").arg(metrics.elidedText(name, Qt::ElideRight, kTargetNameMax)));
    }
    m_review->setAccessibleName(name.isEmpty() ? tr("Review and add") : tr("Review and add to %1").arg(name));
}

QString SelectionBar::summaryText() const
{
    return m_summary->text();
}

int SelectionBar::iconCount() const
{
    return m_icons->count();
}

QString SelectionBar::overflowText() const
{
    return m_more->text();
}

void SelectionBar::sync()
{
    const int count = m_cart ? m_cart->count() : 0;
    m_icons->refresh();
    m_icons->setVisible(count > 0);
    const int more = count - kMaxIcons;
    m_more->setText(more > 0 ? QStringLiteral("+%1").arg(more) : QString());
    m_more->setVisible(more > 0);
    m_summary->setText(m_cart ? m_cart->summaryText() : QString());

    const bool want = count > 0;
    if (want == m_shown) {
        return;
    }
    m_shown = want;
    m_slide.stop();
    if (!want) {
        hide();
        setOffset(0);
        return;
    }
    show();
    const int ms = Motion::duration(kSlideMs);
    if (ms <= 0) {
        setOffset(0);
        return;
    }
    setOffset(kHeight);
    m_slide.setDuration(ms);
    m_slide.start();
}

void SelectionBar::setOffset(int offset)
{
    m_offset = offset;
    placeSurface();
}

void SelectionBar::placeSurface()
{
    m_surface->setGeometry(0, m_offset, width(), kHeight);
}

void SelectionBar::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    placeSurface();
}

}  // namespace Zero::Content
