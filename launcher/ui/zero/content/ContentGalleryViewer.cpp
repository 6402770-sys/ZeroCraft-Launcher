// SPDX-License-Identifier: GPL-3.0-only
#include "ContentGalleryViewer.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>

#include "ContentDraw.h"
#include "RemoteImageCache.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero::Content {

namespace {
constexpr int kMargin = 56;
constexpr int kCaptionHeight = 56;
}  // namespace

ContentGalleryViewer::ContentGalleryViewer(Zero::RemoteImageCache* images, QWidget* parent)
    : QWidget(parent)
    , m_images(images)
    , m_previous(new QToolButton(this))
    , m_next(new QToolButton(this))
    , m_close(new QToolButton(this))
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_StyledBackground, false);
    hide();
    for (auto* button : { m_previous, m_next, m_close }) {
        Style::setProperty(button, "zeroVariant", "secondary");
        button->setIconSize(QSize(18, 18));
        button->setFixedSize(36, 36);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
    }
    m_previous->setToolTip(tr("Previous picture"));
    m_previous->setAccessibleName(tr("Previous picture"));
    m_next->setToolTip(tr("Next picture"));
    m_next->setAccessibleName(tr("Next picture"));
    m_close->setToolTip(tr("Close"));
    m_close->setAccessibleName(tr("Close viewer"));
    auto refreshIcons = [this]() {
        const auto& t = Theme::current();
        m_previous->setIcon(Icons::icon(QStringLiteral("chevron-left"), t.text));
        m_next->setIcon(Icons::icon(QStringLiteral("chevron-right"), t.text));
        m_close->setIcon(Icons::icon(QStringLiteral("x"), t.text));
    };
    refreshIcons();
    connect(&Theme::instance(), &Theme::changed, this, refreshIcons);
    connect(m_previous, &QToolButton::clicked, this, [this]() { step(-1); });
    connect(m_next, &QToolButton::clicked, this, [this]() { step(+1); });
    connect(m_close, &QToolButton::clicked, this, &ContentGalleryViewer::closeViewer);
    if (m_images) {
        connect(m_images, &Zero::RemoteImageCache::imageReady, this, [this]() {
            if (isVisible()) {
                update();
            }
        });
    }
}

void ContentGalleryViewer::setHost(QWidget* host)
{
    if (m_host) {
        m_host->removeEventFilter(this);
    }
    m_host = host;
    if (!host) {
        return;
    }
    setParent(host);
    host->installEventFilter(this);
    place();
}

bool ContentGalleryViewer::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_host && event->type() == QEvent::Resize) {
        place();
    }
    return QWidget::eventFilter(watched, event);
}

void ContentGalleryViewer::place()
{
    if (m_host) {
        setGeometry(m_host->rect());
    }
}

void ContentGalleryViewer::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_close->move(width() - 16 - m_close->width(), 16);
    m_previous->move(16, (height() - m_previous->height()) / 2);
    m_next->move(width() - 16 - m_next->width(), (height() - m_next->height()) / 2);
}

QRect ContentGalleryViewer::pictureArea() const
{
    return QRect(kMargin, kMargin, std::max(0, width() - 2 * kMargin), std::max(0, height() - 2 * kMargin - kCaptionHeight));
}

void ContentGalleryViewer::open(const QList<GalleryEntry>& entries, int index)
{
    if (entries.isEmpty()) {
        return;
    }
    m_entries = entries;
    place();
    resize(size());
    raise();
    show();
    setFocus(Qt::OtherFocusReason);
    showIndex(index);
}

void ContentGalleryViewer::closeViewer()
{
    if (!isVisible()) {
        return;
    }
    hide();
    emit closed();
}

void ContentGalleryViewer::step(int direction)
{
    if (m_entries.isEmpty()) {
        return;
    }
    const int n = int(m_entries.size());
    showIndex(((m_index + direction) % n + n) % n);
}

void ContentGalleryViewer::showIndex(int index)
{
    m_index = std::clamp(index, 0, std::max(0, int(m_entries.size()) - 1));
    const bool several = m_entries.size() > 1;
    m_previous->setVisible(several);
    m_next->setVisible(several);
    prefetch();
    update();
    emit indexChanged(m_index);
}

void ContentGalleryViewer::prefetch()
{
    if (!m_images || m_entries.isEmpty()) {
        return;
    }
    // The picture in view goes first; its neighbours are fetched in the background so stepping feels instant.
    for (int offset : { 0, 1, -1 }) {
        const int i = m_index + offset;
        if (i < 0 || i >= m_entries.size()) {
            continue;
        }
        m_images->request(m_entries.at(i).url, Zero::RemoteImageCache::kBuckets[Zero::RemoteImageCache::kBucketCount - 1], this,
                          offset == 0 ? Zero::RemoteImageCache::Priority::Visible : Zero::RemoteImageCache::Priority::Background);
    }
}

void ContentGalleryViewer::paintEvent(QPaintEvent*)
{
    if (m_entries.isEmpty()) {
        return;
    }
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.fillRect(rect(), Draw::withAlpha(t.bg, 0.9));

    const GalleryEntry& entry = m_entries.at(m_index);
    const QRect area = pictureArea();
    QPixmap picture;
    if (m_images && !m_images->hasFailed(entry.url)) {
        picture = m_images->request(entry.url, Zero::RemoteImageCache::kBuckets[Zero::RemoteImageCache::kBucketCount - 1], this);
    }
    if (picture.isNull()) {
        p.setPen(t.textSecondary);
        p.setFont(Style::font(Role::Body));
        const bool failed = m_images && m_images->hasFailed(entry.url);
        p.drawText(area, Qt::AlignCenter, failed ? tr("This picture could not be loaded.") : tr("Loading picture…"));
    } else {
        QSizeF fitted = picture.size();
        fitted.scale(QSizeF(area.size()), Qt::KeepAspectRatio);
        const QRectF to(area.left() + (area.width() - fitted.width()) / 2, area.top() + (area.height() - fitted.height()) / 2, fitted.width(),
                        fitted.height());
        p.save();
        p.setClipPath(Paint::panelPath(to, t.radiusMd));
        p.drawPixmap(to, picture, QRectF(picture.rect()));
        p.restore();
    }

    // Caption: title (or description), and the counter.
    const QRect caption(kMargin, height() - kMargin - kCaptionHeight + 12, width() - 2 * kMargin, kCaptionHeight - 12);
    p.setFont(Style::font(Role::Heading));
    p.setPen(t.text);
    const QFontMetrics fm(p.font());
    if (!entry.title.isEmpty()) {
        p.drawText(QRect(caption.left(), caption.top(), caption.width(), fm.height()), Qt::AlignHCenter | Qt::AlignVCenter,
                   fm.elidedText(entry.title, Qt::ElideRight, caption.width()));
    }
    p.setFont(Style::font(Role::Caption));
    p.setPen(t.textSecondary);
    const QFontMetrics cm(p.font());
    QString second = entry.description;
    if (m_entries.size() > 1) {
        const QString counter = tr("%1 / %2").arg(m_index + 1).arg(m_entries.size());
        second = second.isEmpty() ? counter : counter + QStringLiteral("  ·  ") + second;
    }
    p.drawText(QRect(caption.left(), caption.top() + fm.height() + 2, caption.width(), cm.height()), Qt::AlignHCenter | Qt::AlignVCenter,
               cm.elidedText(second, Qt::ElideRight, caption.width()));
}

void ContentGalleryViewer::mousePressEvent(QMouseEvent* event)
{
    // A click outside the picture area closes the viewer; on the picture it does nothing.
    if (!pictureArea().contains(event->position().toPoint())) {
        closeViewer();
    }
}

void ContentGalleryViewer::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
        case Qt::Key_Left:
            step(-1);
            break;
        case Qt::Key_Right:
            step(+1);
            break;
        case Qt::Key_Escape:
            closeViewer();
            break;
        default:
            QWidget::keyPressEvent(event);
            return;
    }
    event->accept();
}

}  // namespace Zero::Content
