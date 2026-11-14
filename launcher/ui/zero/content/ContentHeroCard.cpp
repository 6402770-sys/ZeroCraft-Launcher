// SPDX-License-Identifier: GPL-3.0-only
#include "ContentHeroCard.h"

#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include "ContentDraw.h"
#include "RemoteImageCache.h"
#include "ui/zero/PillTag.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero::Content {

namespace {
constexpr int kPad = Tokens::spaceLg;
constexpr int kPillHeight = 18;
}  // namespace

ContentHeroCard::ContentHeroCard(Zero::RemoteImageCache* images, QWidget* parent)
    : QWidget(parent), m_images(images), m_install(new QPushButton(tr("Install"), this))
{
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    Style::setProperty(m_install, "zeroVariant", "primary");
    m_install->setCursor(Qt::ArrowCursor);
    connect(m_install, &QPushButton::clicked, this, [this]() {
        if (m_item) {
            emit installRequested(m_item);
        }
    });
    placeButton();
}

void ContentHeroCard::setItem(const ModpackItemPtr& item)
{
    m_item = item;
    m_install->setAccessibleName(item ? tr("Install %1").arg(item->name) : tr("Install"));
    setAccessibleName(item ? tr("%1, featured modpack").arg(item->name) : QString());
    update();
}

void ContentHeroCard::setInstalled(bool installed)
{
    if (installed != m_installed) {
        m_installed = installed;
        update();
    }
}

void ContentHeroCard::setSelected(bool selected)
{
    if (selected != m_selected) {
        m_selected = selected;
        update();
    }
}

void ContentHeroCard::placeButton()
{
    const QSize hint = m_install->sizeHint();
    m_install->setGeometry(width() - kPad - hint.width(), height() - 14 - hint.height(), hint.width(), hint.height());
}

void ContentHeroCard::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    placeButton();
}

void ContentHeroCard::enterEvent(QEnterEvent* event)
{
    m_hover = true;
    update();
    QWidget::enterEvent(event);
}

void ContentHeroCard::leaveEvent(QEvent* event)
{
    m_hover = false;
    update();
    QWidget::leaveEvent(event);
}

void ContentHeroCard::focusInEvent(QFocusEvent* event)
{
    update();
    QWidget::focusInEvent(event);
}

void ContentHeroCard::focusOutEvent(QFocusEvent* event)
{
    update();
    QWidget::focusOutEvent(event);
}

void ContentHeroCard::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()) && m_item) {
        emit activated(m_item);
    }
    QWidget::mouseReleaseEvent(event);
}

void ContentHeroCard::keyPressEvent(QKeyEvent* event)
{
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space) && m_item) {
        emit activated(m_item);
        return;
    }
    QWidget::keyPressEvent(event);
}

void ContentHeroCard::paintEvent(QPaintEvent*)
{
    if (!m_item) {
        return;
    }
    const auto& t = Theme::current();
    const ModpackItem& item = *m_item;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const qreal dpr = devicePixelRatioF();
    const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const QPainterPath shape = Paint::panelPath(frame, t.radiusLg);

    // Artwork (or the icon tile).
    p.save();
    p.setClipPath(shape);
    QLinearGradient base(rect().topLeft(), rect().bottomRight());
    base.setColorAt(0, t.elevated);
    base.setColorAt(1, t.higher);
    p.fillRect(rect(), base);
    QPixmap picture;
    qreal fade = 0.0;
    if (!item.artworkUrl.isEmpty() && m_images && !m_images->hasFailed(item.artworkUrl)) {
        picture = m_images->request(item.artworkUrl, Zero::RemoteImageCache::bucketFor(width(), dpr), this);
        fade = picture.isNull() ? 0.0 : m_images->fadeIn(item.artworkUrl);
    }
    if (picture.isNull() || fade < 1.0) {
        p.save();
        p.setOpacity(1.0 - fade);
        QPixmap icon;
        if (!item.iconUrl.isEmpty() && m_images && !m_images->hasFailed(item.iconUrl)) {
            icon = m_images->request(item.iconUrl, Zero::RemoteImageCache::bucketFor(96, dpr), this);
        }
        const QRect big(width() / 2 - 48, (height() - kPlateHeight) / 2 - 48 + 8, 96, 96);
        if (!icon.isNull()) {
            p.setClipPath(Paint::panelPath(QRectF(big), t.radiusLg), Qt::IntersectClip);
            Draw::drawCover(&p, big, icon);
        } else {
            Icons::icon(QStringLiteral("package"), t.textTertiary).paint(&p, QRect(big.center().x() - 16, big.center().y() - 16, 32, 32));
        }
        p.restore();
    }
    if (!picture.isNull()) {
        p.save();
        p.setOpacity(Motion::easeEnter().valueForProgress(fade));
        Draw::drawCover(&p, rect(), picture);
        p.restore();
    }
    // The plate: a soft edge, then solid, so the text reads on any picture.
    QLinearGradient scrim(0, height() - kPlateHeight - 36, 0, height() - kPlateHeight + 26);
    scrim.setColorAt(0, Draw::withAlpha(t.bg, 0.0));
    scrim.setColorAt(1, Draw::withAlpha(t.bg, 0.9));
    p.fillRect(QRect(0, height() - kPlateHeight - 36, width(), 62), scrim);
    p.fillRect(QRect(0, height() - kPlateHeight + 26, width(), kPlateHeight - 26), Draw::withAlpha(t.bg, 0.9));
    p.restore();

    // Border: selected = accent, hover = strong, else the normal border.
    const bool focused = hasFocus();
    p.setPen(QPen(m_selected ? t.accent : (m_hover || focused) ? t.borderStrong : t.border, 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(shape);
    if (focused) {
        p.setPen(QPen(t.accent2.isValid() ? t.accent2 : t.accent, 2));
        p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(1, 1, -1, -1), t.radiusLg));
    }

    // Provider badge and Installed on the artwork.
    {
        const QString provider = providerBadge(item.provider);
        const int w = PillTag::widthForText(provider);
        Draw::paintOverlayPill(&p, QRect(kPad - 4, 10, w, kPillHeight), provider, false);
        if (m_installed) {
            const QString text = tr("Installed");
            const int iw = PillTag::widthForText(text);
            Draw::paintOverlayPill(&p, QRect(width() - (kPad - 4) - iw, 10, iw, kPillHeight), text, true);
        }
    }

    // Text block, from the bottom up.
    const int textWidth = width() - 2 * kPad;
    int y = height() - 14 - kPillHeight;
    int x = kPad;
    const int buttonLeft = m_install->geometry().left() - 8;
    {
        QList<QPair<QString, PillTag::Tone>> pills;
        if (const QString range = minecraftRangeText(item.gameVersions); !range.isEmpty()) {
            pills.append({ range, PillTag::Tone::Plain });
        }
        const QStringList loaders = loaderLabels(item.loaderNames);
        for (int i = 0; i < loaders.size() && i < 2; ++i) {
            pills.append({ loaders.at(i), PillTag::Tone::Plain });
        }
        for (const auto& [text, tone] : pills) {
            const int w = PillTag::widthForText(text);
            if (x + w > buttonLeft) {
                break;
            }
            PillTag::paintPill(&p, QRectF(x, y, w, kPillHeight), text, tone);
            x += w + 6;
        }
        if (item.downloads >= 0) {
            const QFont caption = Style::font(Role::Caption);
            const QFontMetrics fm(caption);
            const QString count = formatCount(item.downloads);
            const int textW = fm.horizontalAdvance(count);
            if (x + 16 + textW <= buttonLeft) {
                p.setFont(caption);
                p.setPen(t.textSecondary);
                Icons::icon(QStringLiteral("download"), t.textSecondary).paint(&p, QRect(x + 2, y + (kPillHeight - 12) / 2, 12, 12));
                p.drawText(QRect(x + 18, y, textW + 2, kPillHeight), Qt::AlignLeft | Qt::AlignVCenter, count);
            }
        }
    }
    const QFont bodyFont = Style::font(Role::Body);
    const QFontMetrics bodyMetrics(bodyFont);
    // The Install button rises above the pill row: when the description reaches that height it stops short of the button.
    const bool reachesButton = y - 6 > m_install->geometry().top();
    const int descWidth = reachesButton ? std::max(80, buttonLeft - kPad) : textWidth;
    const QStringList lines = Draw::wrapLines(item.summary, bodyFont, descWidth, 2);
    y -= 6 + (lines.size() * bodyMetrics.lineSpacing());
    p.setFont(bodyFont);
    p.setPen(t.textSecondary);
    for (int i = 0; i < lines.size(); ++i) {
        p.drawText(QRect(kPad, y + i * bodyMetrics.lineSpacing(), descWidth, bodyMetrics.lineSpacing()), Qt::AlignLeft | Qt::AlignVCenter,
                   lines.at(i));
    }
    const QFont captionFont = Style::font(Role::Caption);
    const QFontMetrics captionMetrics(captionFont);
    if (!item.author.isEmpty()) {
        y -= captionMetrics.height() + 2;
        p.setFont(captionFont);
        p.setPen(t.textSecondary);
        p.drawText(QRect(kPad, y, textWidth, captionMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                   captionMetrics.elidedText(tr("by %1").arg(item.author), Qt::ElideRight, textWidth));
    }
    const QFont titleFont = Style::font(Role::Title);
    const QFontMetrics titleMetrics(titleFont);
    y -= titleMetrics.height() + 2;
    p.setFont(titleFont);
    p.setPen(t.text);
    p.drawText(QRect(kPad, y, textWidth, titleMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
               titleMetrics.elidedText(item.name, Qt::ElideRight, textWidth));
}

}  // namespace Zero::Content
