// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroInstanceDelegate.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QFontMetrics>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QTextLayout>
#include <QVariantAnimation>
#include "ArtworkCache.h"
#include "BaseInstance.h"
#include "InstanceInfo.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroThemeState.h"
#include "zero/profiles/InstanceProfile.h"

namespace Zero {

namespace {

/// Splits `text` into at most `maxLines` lines that fit `width`; the last line is elided when text remains.
QStringList wrapElided(const QString& text, const QFont& font, int width, int maxLines)
{
    QStringList lines;
    if (text.isEmpty() || width <= 0)
        return lines;
    const QFontMetrics fm(font);
    QTextLayout layout(text, font);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    layout.beginLayout();
    for (int i = 0; i < maxLines; ++i) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(width);
        const int start = line.textStart();
        const int len = line.textLength();
        if (i == maxLines - 1 && start + len < text.size()) {
            lines << fm.elidedText(text.mid(start), Qt::ElideRight, width);
            break;
        }
        lines << text.mid(start, len).trimmed();
    }
    layout.endLayout();
    return lines;
}

}  // namespace

ZeroInstanceDelegate::ZeroInstanceDelegate(QObject* parent) : ListViewDelegate(parent) {}

QRect ZeroInstanceDelegate::starRect(const QRect& cell)
{
    return QRect(cell.left() + 4, cell.top() + 4, kStarHit, kStarHit);
}

void ZeroInstanceDelegate::attachTo(QAbstractItemView* view)
{
    if (m_view)
        m_view->viewport()->removeEventFilter(this);
    m_view = view;
    if (view)
        view->viewport()->installEventFilter(this);
}

ZeroInstanceDelegate::Hover* ZeroInstanceDelegate::find(const QModelIndex& index) const
{
    for (Hover* h : std::as_const(m_hovers))
        if (h->index == index)
            return h;
    return nullptr;
}

qreal ZeroInstanceDelegate::hoverProgress(const QModelIndex& index) const
{
    const Hover* h = find(index);
    return h ? h->progress : 0.0;
}

bool ZeroInstanceDelegate::eventFilter(QObject* watched, QEvent* event)
{
    if (m_view && watched == m_view->viewport()) {
        auto overStar = [this](const QPoint& pos, QModelIndex* hit) {
            const QModelIndex index = m_view->indexAt(pos);
            const bool over = index.isValid() && starRect(m_view->visualRect(index)).contains(pos);
            if (hit)
                *hit = over ? index : QModelIndex();
            return over;
        };
        switch (event->type()) {
            case QEvent::MouseMove: {
                const QPoint pos = static_cast<QMouseEvent*>(event)->position().toPoint();
                setHovered(m_view->indexAt(pos));
                QModelIndex hot;
                overStar(pos, &hot);
                if (hot != m_starHot) {
                    const QModelIndex old = m_starHot;
                    m_starHot = hot;
                    repaintCard(old);
                    repaintCard(hot);
                    m_view->viewport()->setCursor(hot.isValid() ? Qt::PointingHandCursor : Qt::ArrowCursor);
                }
                break;
            }
            case QEvent::Leave:
            case QEvent::Hide: {
                setHovered({});
                if (m_starHot.isValid()) {
                    const QModelIndex old = m_starHot;
                    m_starHot = QModelIndex();
                    repaintCard(old);
                    m_view->viewport()->setCursor(Qt::ArrowCursor);
                }
                break;
            }
            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonDblClick: {
                // The star is a button on the card: its click never selects, drags or launches the instance.
                auto* mouse = static_cast<QMouseEvent*>(event);
                QModelIndex hit;
                if (mouse->button() == Qt::LeftButton && overStar(mouse->position().toPoint(), &hit)) {
                    m_starPressed = hit;
                    return true;
                }
                break;
            }
            case QEvent::MouseButtonRelease: {
                auto* mouse = static_cast<QMouseEvent*>(event);
                if (mouse->button() == Qt::LeftButton && m_starPressed.isValid()) {
                    QModelIndex hit;
                    const bool still = overStar(mouse->position().toPoint(), &hit) && hit == m_starPressed;
                    const QModelIndex pressed = m_starPressed;
                    m_starPressed = QModelIndex();
                    if (still)
                        emit favoriteToggleRequested(pressed.data(InstanceList::InstanceIDRole).toString());
                    return true;
                }
                break;
            }
            default:
                break;
        }
    }
    return ListViewDelegate::eventFilter(watched, event);
}

void ZeroInstanceDelegate::setHovered(const QModelIndex& index)
{
    if (m_hovered == index)
        return;
    // The previous card eases out on its own while the new one eases in.
    if (m_hovered.isValid())
        animate(m_hovered, false);
    m_hovered = index;
    if (index.isValid())
        animate(index, true);
}

void ZeroInstanceDelegate::animate(const QModelIndex& index, bool on)
{
    Hover* h = find(index);
    if (!h) {
        if (!on)
            return;
        h = new Hover;
        h->index = index;
        m_hovers.append(h);
    }
    const qreal target = on ? 1.0 : 0.0;
    if (h->anim)
        h->anim->stop();
    if (Motion::duration(Tokens::durFast) == 0) {
        h->progress = target;
        repaintCard(index);
        if (!on) {
            m_hovers.removeOne(h);
            if (h->anim)
                h->anim->deleteLater();
            delete h;
        }
        return;
    }
    if (!h->anim) {
        h->anim = new QVariantAnimation(this);
        QVariantAnimation* anim = h->anim;
        connect(anim, &QVariantAnimation::valueChanged, this, [this, anim](const QVariant& v) {
            for (Hover* hv : std::as_const(m_hovers)) {
                if (hv->anim == anim) {
                    hv->progress = v.toReal();
                    repaintCard(hv->index);
                    return;
                }
            }
        });
        connect(anim, &QVariantAnimation::finished, this, [this, anim]() {
            for (Hover* hv : std::as_const(m_hovers)) {
                if (hv->anim == anim && hv->progress <= 0.0) {
                    m_hovers.removeOne(hv);
                    delete hv;
                    anim->deleteLater();
                    return;
                }
            }
        });
    }
    h->anim->setDuration(Motion::duration(Tokens::durFast));
    h->anim->setEasingCurve(on ? Motion::easeEnter() : Motion::easeExit());
    h->anim->setStartValue(h->progress);
    h->anim->setEndValue(target);
    h->anim->start();
}

void ZeroInstanceDelegate::repaintCard(const QModelIndex& index)
{
    if (!m_view || !index.isValid())
        return;
    // Only this card plus its lift (2 px up) and shadow (to ~9 px below / 7 px aside).
    m_view->viewport()->update(m_view->visualRect(index).adjusted(-8, -4, 8, 12));
}

QSize ZeroInstanceDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const
{
    return { kCellWidth, kCellHeight };
}

void ZeroInstanceDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const auto& t = Theme::current();
    const bool selected = option.state & QStyle::State_Selected;
    // Animated hover progress when attached to a view; the view's plain MouseOver flag otherwise.
    const qreal hover = m_view ? hoverProgress(index) : ((option.state & QStyle::State_MouseOver) ? 1.0 : 0.0);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRect cell = option.rect;
    if (hover > 0.0) {
        // Card lift: 2 px up with a soft painted shadow (stacked translucent panels) that grows with the progress.
        const QRectF base = QRectF(cell).adjusted(0.5, 0.5, -0.5, -0.5).translated(0, 3 - 2 * hover);
        painter->setPen(Qt::NoPen);
        for (int i = 6; i >= 1; --i) {
            painter->setBrush(QColor(0, 0, 0, int(Tokens::shadowAlpha * 0.16 * hover * (7 - i) / 6.0)));
            painter->drawPath(Paint::panelPath(base.adjusted(-i, -i, i, i), t.radiusLg + i));
        }
        painter->translate(0, -2.0 * hover);
    }
    const QRectF card = QRectF(cell).adjusted(0.5, 0.5, -0.5, -0.5);
    const QPainterPath cardPath = Paint::panelPath(card, t.radiusLg);

    const QColor bg = selected ? t.elevated : mix(t.surface, t.surfaceHover, hover);
    const QColor border = selected ? t.accent : mix(t.border, t.borderStrong, hover);
    painter->fillPath(cardPath, bg);

    auto* instance = static_cast<MinecraftInstance*>(index.data(InstanceList::InstancePointerRole).value<void*>());
    const QString artwork = instance ? Profile::artworkPath(instance) : QString();
    const QRect iconArea(cell.left(), cell.top(), cell.width(), kIconAreaHeight);
    QPixmap cover;
    if (!artwork.isEmpty())
        cover = Artwork::cover(artwork, iconArea.size(), painter->device()->devicePixelRatioF());
    const bool hasArtwork = !cover.isNull();

    // icon area: elevated (or the artwork, cover-cropped); the top corners follow the card radius
    painter->save();
    painter->setClipPath(cardPath);
    if (hasArtwork)
        painter->drawPixmap(iconArea.topLeft(), cover);
    else
        painter->fillRect(iconArea, selected ? t.higher : t.elevated);
    painter->restore();

    painter->setPen(QPen(border, 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(cardPath);
    Paint::paintBevel(*painter, QRectF(cell), false);

    const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
    if (hasArtwork) {
        // the instance icon becomes a small badge at the bottom-left of the artwork
        const QRectF badge(cell.left() + 8, iconArea.bottom() - 8 - kBadgeSize + 1, kBadgeSize, kBadgeSize);
        painter->setPen(QPen(t.border, 1));
        painter->setBrush(t.surface);
        painter->drawPath(Paint::panelPath(badge.adjusted(0.5, 0.5, -0.5, -0.5), t.radiusMd));
        if (!icon.isNull())
            icon.paint(painter, badge.adjusted(3, 3, -3, -3).toRect());
    } else {
        const QRect iconRect(cell.left() + (cell.width() - kIconSize) / 2, cell.top() + (kIconAreaHeight - kIconSize) / 2, kIconSize,
                             kIconSize);
        if (!icon.isNull())
            icon.paint(painter, iconRect);
    }

    // favorite star (top-left): solid in `highlight` when favorite; otherwise a hollow star that fades in on hover and lights up
    // under the pointer
    if (instance) {
        const bool favorite = Profile::isFavorite(instance);
        const bool hot = m_starHot.isValid() && m_starHot == index;
        const qreal visible = favorite || hot ? 1.0 : hover;
        if (visible > 0.0) {
            const QRect hit = starRect(cell);
            painter->save();
            painter->setOpacity(painter->opacity() * visible);
            if (hasArtwork) {
                painter->setPen(Qt::NoPen);
                painter->setBrush(withAlpha(t.bg, 0.62));
                painter->drawEllipse(QRectF(hit).adjusted(1, 1, -1, -1));
            }
            const QRect glyph(hit.center().x() - 7, hit.center().y() - 7, 16, 16);
            if (favorite)
                Icons::filledIcon(QStringLiteral("star"), t.highlight).paint(painter, glyph);
            else
                Icons::icon(QStringLiteral("star"), hot ? t.highlight : t.textSecondary).paint(painter, glyph);
            painter->restore();
        }
    }

    // status dot (top right): running = success, crashed / broken = warning
    if (instance) {
        QColor dot;
        if (instance->isRunning())
            dot = t.success;
        else if (instance->hasCrashed() || instance->hasVersionBroken())
            dot = t.warning;
        if (dot.isValid()) {
            const QPointF center(cell.right() - 14, cell.top() + 14);
            painter->setPen(Qt::NoPen);
            painter->setBrush(withAlpha(dot, Tokens::statusTint * 2));
            painter->drawEllipse(center, 8, 8);
            painter->setBrush(dot);
            painter->drawEllipse(center, 4, 4);
        }
    }

    // text block
    const int pad = 10;
    const int textWidth = cell.width() - 2 * pad;
    QFont nameFont = Style::font(Role::Label);
    nameFont.setWeight(QFont::DemiBold);
    const QFontMetrics nameMetrics(nameFont);
    const QStringList nameLines = wrapElided(index.data(Qt::DisplayRole).toString(), nameFont, textWidth, 2);

    painter->setFont(nameFont);
    painter->setPen(t.text);
    int y = cell.top() + kIconAreaHeight + 8;
    for (const QString& line : nameLines) {
        painter->drawText(QRect(cell.left() + pad, y, textWidth, nameMetrics.height()), Qt::AlignHCenter | Qt::AlignVCenter, line);
        y += nameMetrics.height();
    }

    const QFont captionFont = Style::font(Role::Caption);
    const QFontMetrics captionMetrics(captionFont);
    const QString caption = InstanceInfo::of(instance).summary();
    painter->setFont(captionFont);
    painter->setPen(t.textSecondary);
    painter->drawText(QRect(cell.left() + pad, cell.bottom() - 8 - captionMetrics.height(), textWidth, captionMetrics.height()),
                      Qt::AlignHCenter | Qt::AlignVCenter, captionMetrics.elidedText(caption, Qt::ElideRight, textWidth));

    painter->restore();
}

void ZeroInstanceDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex&) const
{
    editor->setGeometry(option.rect.left() + 4, option.rect.top() + kIconAreaHeight + 4, option.rect.width() - 8, 44);
}

}  // namespace Zero
