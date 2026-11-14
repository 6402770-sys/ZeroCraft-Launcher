// SPDX-License-Identifier: GPL-3.0-only
#include "FeaturedStrip.h"

#include <QDateTime>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QWheelEvent>

#include <algorithm>

#include "ContentHeroCard.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroSkeleton.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero::Content {

FeaturedStrip::FeaturedStrip(Zero::RemoteImageCache* images, QWidget* parent)
    : QWidget(parent)
    , m_images(images)
    , m_label(new QLabel(this))
    , m_previous(new QToolButton(this))
    , m_next(new QToolButton(this))
    , m_scroll(new QScrollArea(this))
    , m_inner(new QWidget)
    , m_scrollAnim(new QPropertyAnimation(this))
    , m_effect(new QGraphicsOpacityEffect(this))
{
    Style::applyRole(m_label, Role::Heading);
    m_label->setAccessibleName(tr("Featured modpacks"));

    for (auto* button : { m_previous, m_next }) {
        Style::setProperty(button, "zeroVariant", "ghost");
        button->setIconSize(QSize(16, 16));
        button->setFixedSize(28, 28);
        button->setCursor(Qt::PointingHandCursor);
    }
    m_previous->setToolTip(tr("Previous"));
    m_previous->setAccessibleName(tr("Previous featured pack"));
    m_next->setToolTip(tr("Next"));
    m_next->setAccessibleName(tr("Next featured pack"));
    auto refreshIcons = [this]() {
        const auto& t = Theme::current();
        m_previous->setIcon(Icons::icon(QStringLiteral("chevron-left"), t.textSecondary));
        m_next->setIcon(Icons::icon(QStringLiteral("chevron-right"), t.textSecondary));
    };
    refreshIcons();
    connect(&Theme::instance(), &Theme::changed, this, refreshIcons);
    connect(m_previous, &QToolButton::clicked, this, [this]() { step(-1); });
    connect(m_next, &QToolButton::clicked, this, [this]() { step(+1); });

    m_scroll->setWidget(m_inner);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(false);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
    m_scroll->viewport()->setAutoFillBackground(false);
    m_inner->setAutoFillBackground(false);
    m_scroll->viewport()->installEventFilter(this);
    connect(m_scroll->horizontalScrollBar(), &QScrollBar::valueChanged, this, &FeaturedStrip::updateChevrons);

    m_scrollAnim->setTargetObject(m_scroll->horizontalScrollBar());
    m_scrollAnim->setPropertyName("value");
    m_scrollAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_collapse.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_collapse, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_progress = v.toReal();
        applyProgress();
    });
    connect(&m_collapse, &QVariantAnimation::finished, this, [this]() { applyProgress(); });

    setGraphicsEffect(m_effect);
    m_effect->setEnabled(false);
    hide();
    updateChevrons();
}

QString FeaturedStrip::label() const
{
    return m_label->text();
}

void FeaturedStrip::setLabel(const QString& label)
{
    m_label->setText(label);
}

int FeaturedStrip::heroWidthFor(int stripWidth)
{
    return std::clamp(int(stripWidth * 0.42), std::min(380, std::max(stripWidth, 1)), 480);
}

int FeaturedStrip::heroWidth() const
{
    return heroWidthFor(width());
}

int FeaturedStrip::fullHeight() const
{
    return kHeaderHeight + Tokens::spaceSm + heroWidth() * 9 / 16 + Tokens::spaceLg;
}

void FeaturedStrip::setItems(const QList<ModpackItemPtr>& items)
{
    qDeleteAll(m_cards);
    m_cards.clear();
    m_items = items.mid(0, kMaxCards);
    for (const auto& item : std::as_const(m_items)) {
        auto* card = new ContentHeroCard(m_images, m_inner);
        card->setItem(item);
        card->setInstalled(m_installed.contains(installedKey(*item)));
        card->setSelected(!m_selected.isEmpty() && item->key() == m_selected);
        connect(card, &ContentHeroCard::activated, this, &FeaturedStrip::activated);
        connect(card, &ContentHeroCard::installRequested, this, &FeaturedStrip::installRequested);
        card->show();
        m_cards.append(card);
    }
    m_hasContent = !m_items.isEmpty() || m_loading;
    if (m_skeleton) {
        m_skeleton->setVisible(m_items.isEmpty() && m_loading);
    }
    relayout();
    applyProgress();
}

void FeaturedStrip::setLoading(bool loading)
{
    m_loading = loading;
    if (loading && !m_skeleton) {
        m_skeleton = new Zero::ZeroSkeleton(Zero::ZeroSkeleton::Shape::Cinematic, tr("Loading featured packs…"), m_scroll->viewport());
    }
    if (m_skeleton) {
        m_skeleton->setVisible(m_items.isEmpty() && loading);
    }
    m_hasContent = !m_items.isEmpty() || loading;
    relayout();
    applyProgress();
}

void FeaturedStrip::setInstalledKeys(const QSet<QString>& keys)
{
    m_installed = keys;
    for (auto* card : std::as_const(m_cards)) {
        if (card->item()) {
            card->setInstalled(keys.contains(installedKey(*card->item())));
        }
    }
}

void FeaturedStrip::setSelectedKey(const QString& key)
{
    m_selected = key;
    for (auto* card : std::as_const(m_cards)) {
        card->setSelected(!key.isEmpty() && card->item() && card->item()->key() == key);
    }
}

void FeaturedStrip::setCollapsed(bool collapsed, bool animate)
{
    if (collapsed == m_collapsed) {
        return;
    }
    m_collapsed = collapsed;
    m_collapse.stop();
    const qreal target = collapsed ? 0.0 : 1.0;
    const int ms = Motion::duration(kCollapseMs);
    if (!animate || ms <= 0 || !m_hasContent || !parentWidget() || !parentWidget()->isVisible()) {
        m_progress = target;
        applyProgress();
        return;
    }
    m_collapse.setDuration(ms);
    m_collapse.setStartValue(m_progress);
    m_collapse.setEndValue(target);
    applyProgress();
    m_collapse.start();
}

void FeaturedStrip::applyProgress()
{
    const bool visible = m_hasContent && m_progress > 0.0;
    if (!visible) {
        setFixedHeight(0);
        hide();
        return;
    }
    const int full = fullHeight();
    setFixedHeight(qBound(1, qRound(full * m_progress), full));
    m_effect->setEnabled(m_progress < 1.0);
    m_effect->setOpacity(m_progress);
    if (isHidden()) {
        show();
    }
    relayout();
}

void FeaturedStrip::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    relayout();
    if (m_progress >= 1.0 && m_hasContent && m_collapse.state() != QAbstractAnimation::Running) {
        const int full = fullHeight();
        if (height() != full) {
            setFixedHeight(full);  // the width changed: the hero size changed with it
        }
    }
}

void FeaturedStrip::relayout()
{
    const int w = width();
    m_label->setGeometry(0, 0, std::max(0, w - 2 * 28 - 8), kHeaderHeight);
    m_next->move(w - 28, 0);
    m_previous->move(w - 56 - 4, 0);
    const int heroW = heroWidth();
    const int heroH = heroW * 9 / 16;
    m_scroll->setGeometry(0, kHeaderHeight + Tokens::spaceSm, w, heroH);
    const int count = int(m_cards.size());
    const int innerW = count > 0 ? count * heroW + (count - 1) * kGap : w;
    m_inner->resize(std::max(innerW, w), heroH);
    for (int i = 0; i < count; ++i) {
        m_cards[i]->setGeometry(i * (heroW + kGap), 0, heroW, heroH);
    }
    if (m_skeleton) {
        m_skeleton->setGeometry(0, 0, w, heroH);
    }
    const bool chevrons = count > 1 && innerW > w;
    m_previous->setVisible(chevrons);
    m_next->setVisible(chevrons);
    m_label->setVisible(!m_label->text().isEmpty());
    updateChevrons();
}

void FeaturedStrip::updateChevrons()
{
    const int max = m_scroll->horizontalScrollBar()->maximum();
    const int value = m_scroll->horizontalScrollBar()->value();
    m_previous->setEnabled(value > 0);
    m_next->setEnabled(value < max);
}

int FeaturedStrip::firstVisibleIndex() const
{
    const int pitch = heroWidth() + kGap;
    if (pitch <= 0 || m_cards.isEmpty()) {
        return 0;
    }
    return std::clamp(int(qRound(double(m_scroll->horizontalScrollBar()->value()) / pitch)), 0, int(m_cards.size()) - 1);
}

void FeaturedStrip::scrollToIndex(int index, bool animate)
{
    QScrollBar* bar = m_scroll->horizontalScrollBar();
    const int target = std::clamp(index * (heroWidth() + kGap), 0, bar->maximum());
    m_scrollAnim->stop();
    const int ms = Motion::duration(kScrollMs);
    if (!animate || ms <= 0) {
        bar->setValue(target);
        return;
    }
    m_scrollAnim->setDuration(ms);
    m_scrollAnim->setStartValue(bar->value());
    m_scrollAnim->setEndValue(target);
    m_scrollAnim->start();
}

void FeaturedStrip::refreshImages()
{
    for (auto* card : std::as_const(m_cards)) {
        card->update();
    }
}

void FeaturedStrip::step(int direction)
{
    scrollToIndex(firstVisibleIndex() + direction);
}

bool FeaturedStrip::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_scroll->viewport() && event->type() == QEvent::Wheel) {
        auto* wheel = static_cast<QWheelEvent*>(event);
        // A horizontal gesture moves one card; the vertical wheel keeps scrolling the page.
        const int dx = wheel->angleDelta().x();
        if (dx != 0 && std::abs(dx) > std::abs(wheel->angleDelta().y())) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - m_lastWheel > 220) {
                m_lastWheel = now;
                step(dx < 0 ? +1 : -1);
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

}  // namespace Zero::Content
