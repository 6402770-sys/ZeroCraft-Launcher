// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroNavRail.h"

#include <QAbstractButton>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QLabel>
#include <QPainter>
#include <QEvent>
#include <QVBoxLayout>
#include "ZeroIcons.h"
#include "ZeroMotion.h"
#include "ZeroPaint.h"
#include "ZeroStyle.h"
#include "ZeroThemeState.h"

namespace Zero {

namespace {
constexpr int kPillInset = 8;       // horizontal inset of the hover/active pill inside the rail
constexpr int kLabelMinWidth = 140; // below this the label is hidden (collapsed rail)
constexpr int kIconPad = 12;        // pill-left to icon
constexpr int kIconGap = 10;        // icon to label
}  // namespace

class NavItem : public QAbstractButton {
   public:
    NavItem(const QString& id, const QString& iconName, const QString& label, QWidget* parent)
        : QAbstractButton(parent), m_id(id), m_icon(iconName), m_hover(new Motion::StateAnimator(this))
    {
        setText(label);
        setCheckable(false);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        setProperty("zeroRipple", true);  // press ripple (see ZeroRipple), clipped to the pill
        setProperty("zeroRippleInset", kPillInset);
        setFixedHeight(Tokens::navItemHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(m_hover, &Motion::StateAnimator::progressChanged, this, qOverload<>(&QWidget::update));
        connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    }

    const QString& id() const { return m_id; }
    void setCurrentItem(bool c)
    {
        m_current = c;
        update();
    }
    bool showsLabel() const { return width() >= kLabelMinWidth; }
    /// Pill rectangle of this item in rail coordinates (the rail paints the active pill, see ZeroNavRail::paintEvent).
    QRectF pillRect() const { return QRectF(geometry()).adjusted(kPillInset, 0, -kPillInset, 0); }

    QSize sizeHint() const override { return { Tokens::navRailWidth, Tokens::navItemHeight }; }

   protected:
    bool event(QEvent* e) override
    {
        // Collapsed rail: the tooltip is the label.
        if (e->type() == QEvent::ToolTip && showsLabel())
            return true;
        return QAbstractButton::event(e);
    }
    void enterEvent(QEnterEvent* e) override
    {
        QAbstractButton::enterEvent(e);
        m_hover->setActive(true);
    }
    void leaveEvent(QEvent* e) override
    {
        QAbstractButton::leaveEvent(e);
        m_hover->setActive(false);
    }
    void focusInEvent(QFocusEvent* e) override
    {
        m_keyboardFocus = e->reason() == Qt::TabFocusReason || e->reason() == Qt::BacktabFocusReason;
        QAbstractButton::focusInEvent(e);
        update();
    }
    void focusOutEvent(QFocusEvent* e) override
    {
        m_keyboardFocus = false;
        QAbstractButton::focusOutEvent(e);
        update();
    }
    void resizeEvent(QResizeEvent* e) override
    {
        QAbstractButton::resizeEvent(e);
        setToolTip(showsLabel() ? QString() : text());
    }

    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        const qreal h = m_hover->progress();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF pill = QRectF(rect()).adjusted(kPillInset, 0, -kPillInset, 0);
        QColor fg = mix(t.textSecondary, t.text, h);
        if (m_current) {
            fg = t.text;  // the active pill + indicator are painted (and glide) by the rail, underneath this item
        } else if (h > 0.0) {
            p.setPen(Qt::NoPen);
            p.setBrush(withAlpha(t.surfaceHover, h));
            p.drawPath(Paint::panelPath(pill, t.radiusMd));
        }
        if (m_keyboardFocus && hasFocus()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(t.textTertiary, 1));
            p.drawPath(Paint::panelPath(pill.adjusted(0.5, 0.5, -0.5, -0.5), t.radiusMd));
        }

        const bool label = showsLabel();
        const int iconSize = Tokens::navIconSize;
        const int iconX = label ? int(pill.left()) + kIconPad : int(pill.center().x() - iconSize / 2.0);
        const QRect iconRect(iconX, (height() - iconSize) / 2, iconSize, iconSize);
        Icons::icon(m_icon, fg).paint(&p, iconRect);

        if (label) {
            p.setFont(Style::font(Role::Label));
            p.setPen(fg);
            const int textX = iconRect.right() + 1 + kIconGap;
            const QRect textRect(textX, 0, int(pill.right()) - textX - kIconPad / 2, height());
            const QString elided = QFontMetrics(p.font()).elidedText(text(), Qt::ElideRight, textRect.width());
            p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, elided);
        }
    }

   private:
    QString m_id;
    QString m_icon;
    Motion::StateAnimator* m_hover;
    bool m_current = false;
    bool m_keyboardFocus = false;
};

ZeroNavRail::ZeroNavRail(QWidget* parent)
    : QWidget(parent),
      m_layout(new QVBoxLayout(this)),
      m_wordmark(new QLabel(this)),
      m_caption(new QLabel(this)),
      m_anim(new QPropertyAnimation(this, "railWidth", this)),
      m_glide(new QVariantAnimation(this))
{
    setFixedWidth(Tokens::navRailWidth);
    m_layout->setContentsMargins(0, Tokens::spaceXl, 0, Tokens::spaceMd);
    m_layout->setSpacing(2);

    // Wordmark: display role (weight 600), 3 px letter spacing; caption "Launcher" below it.
    Style::applyRole(m_wordmark, Role::Display);
    QFont wf = m_wordmark->font();
    wf.setLetterSpacing(QFont::AbsoluteSpacing, 3.0);
    m_wordmark->setFont(wf);
    m_wordmark->setContentsMargins(kPillInset + kIconPad, 0, 0, 0);
    m_layout->addWidget(m_wordmark);
    Style::applyRole(m_caption, Role::Caption);
    m_caption->setText(tr("Launcher"));
    m_caption->setContentsMargins(kPillInset + kIconPad, 0, 0, 0);
    m_layout->addWidget(m_caption);
    m_layout->addSpacing(Tokens::spaceXl);
    refreshWordmark();

    m_anim->setEasingCurve(Motion::easeGlide());
    m_glide->setEasingCurve(Motion::easeGlide());
    m_glide->setStartValue(0.0);
    m_glide->setEndValue(1.0);
    connect(m_glide, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_glideT = v.toReal();
        updatePill();
    });
    connect(m_glide, &QVariantAnimation::finished, this, [this]() {
        m_gliding = false;  // idle again: the pill follows the item geometry live
        updatePill();
    });
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void ZeroNavRail::addItem(const QString& id, const QString& iconName, const QString& label)
{
    if (m_items.contains(id)) {
        qWarning() << "ZeroNavRail: duplicate item id" << id;
        return;
    }
    auto* item = new NavItem(id, iconName, label, this);
    m_items.insert(id, item);
    item->installEventFilter(this);  // geometry changes move the idle pill
    connect(item, &QAbstractButton::clicked, this, [this, id]() { setCurrent(id); });
    m_layout->addWidget(item);
    // keep the bottom holder (if any) before items added after it
}

QAbstractButton* ZeroNavRail::addActionItem(const QString& id, const QString& iconName, const QString& label)
{
    auto* item = new NavItem(id, iconName, label, this);
    m_layout->addWidget(item);
    return item;
}

void ZeroNavRail::addSpacing(int px)
{
    m_layout->addSpacing(px);
}

void ZeroNavRail::addStretch()
{
    m_layout->addStretch(1);
    if (!m_hasStretch) {
        m_hasStretch = true;
        ensureBottomHolder();
    }
}

void ZeroNavRail::ensureBottomHolder()
{
    if (m_bottomHolder)
        return;
    m_bottomHolder = new QWidget(this);
    m_bottomLayout = new QVBoxLayout(m_bottomHolder);
    m_bottomLayout->setContentsMargins(Tokens::spaceSm, 0, Tokens::spaceSm, Tokens::spaceSm);
    m_bottomLayout->setSpacing(0);
    m_bottomHolder->setVisible(false);
    m_layout->addWidget(m_bottomHolder);
}

void ZeroNavRail::setBottomWidget(QWidget* widget)
{
    if (!m_hasStretch)
        addStretch();
    ensureBottomHolder();
    while (auto* it = m_bottomLayout->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    if (widget)
        m_bottomLayout->addWidget(widget);
    m_bottomHolder->setVisible(widget != nullptr);
}

void ZeroNavRail::setCurrent(const QString& id)
{
    if (m_current == id || (!id.isEmpty() && !m_items.contains(id)))
        return;
    const QRectF oldPill = currentPill();  // what is painted right now (also mid-glide)
    m_current = id;
    for (auto* item : std::as_const(m_items))
        item->setCurrentItem(item->id() == id);
    startGlide(oldPill);
    emit currentChanged(id);
}

QRectF ZeroNavRail::targetPill() const
{
    NavItem* item = m_items.value(m_current);
    if (!item || !item->isVisible() || item->height() <= 0 || item->width() <= 0)
        return {};
    return item->pillRect();
}

QRectF ZeroNavRail::currentPill() const
{
    const QRectF target = targetPill();
    if (!m_gliding || target.isNull() || m_glideFrom.isNull())
        return target;
    const qreal t = m_glideT;
    return QRectF(m_glideFrom.x() + (target.x() - m_glideFrom.x()) * t, m_glideFrom.y() + (target.y() - m_glideFrom.y()) * t,
                  m_glideFrom.width() + (target.width() - m_glideFrom.width()) * t,
                  m_glideFrom.height() + (target.height() - m_glideFrom.height()) * t);
}

void ZeroNavRail::startGlide(const QRectF& from)
{
    m_glide->stop();
    m_gliding = false;
    const QRectF target = targetPill();
    if (from.isNull() || target.isNull() || from == target || Motion::duration(Tokens::durBase) == 0) {
        updatePill();
        return;
    }
    m_glideFrom = from;
    m_glideT = 0.0;
    m_gliding = true;
    m_glide->setDuration(Motion::duration(Tokens::durBase));
    m_glide->start();
    updatePill();
}

void ZeroNavRail::updatePill()
{
    // Repaint only where the pill was and where it is now (plus the indicator strip at the left edge).
    const QRectF now = currentPill();
    const QRectF dirty = now.united(m_lastPill);
    m_lastPill = now;
    if (dirty.isNull())
        return;
    update(dirty.adjusted(-2, -2, 2, 2).toAlignedRect());
    update(QRect(0, int(dirty.top()) - 2, 4, int(dirty.height()) + 4));
}

bool ZeroNavRail::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::Hide:
            if (!m_gliding && static_cast<NavItem*>(watched)->id() == m_current)
                updatePill();  // collapse / resize / layout changes while idle
            break;
        default:
            break;
    }
    return QWidget::eventFilter(watched, event);
}

void ZeroNavRail::setItemVisible(const QString& id, bool visible)
{
    if (auto* item = m_items.value(id))
        item->setVisible(visible);
}

void ZeroNavRail::setCollapsed(bool collapsed)
{
    if (m_collapsed == collapsed)
        return;
    m_collapsed = collapsed;
    const int target = collapsed ? Tokens::navRailCollapsedWidth : Tokens::navRailWidth;
    m_anim->stop();
    if (Motion::duration(Tokens::durBase) == 0) {
        setRailWidth(target);
    } else {
        m_anim->setDuration(Tokens::durBase);
        m_anim->setStartValue(width());
        m_anim->setEndValue(target);
        m_anim->start();
    }
    refreshWordmark();
    emit collapsedChanged(collapsed);
}

void ZeroNavRail::setRailWidth(int w)
{
    setFixedWidth(w);
    refreshWordmark();
}

void ZeroNavRail::refreshWordmark()
{
    const bool wide = !m_collapsed && width() >= kLabelMinWidth;
    m_wordmark->setText(wide ? QStringLiteral("ZERO") : QStringLiteral("Z"));
    m_wordmark->setContentsMargins(wide ? kPillInset + kIconPad : 0, 0, 0, 0);
    m_wordmark->setAlignment(wide ? (Qt::AlignLeft | Qt::AlignVCenter) : Qt::AlignCenter);
    m_caption->setVisible(wide);
}

void ZeroNavRail::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    Paint::paintBackground(p, rect(), t.surface);
    p.fillRect(QRect(width() - 1, 0, 1, height()), t.border);

    // The one active pill (+ 2 px selection indicator at the rail's left edge), gliding between items.
    const QRectF pill = currentPill();
    if (!pill.isEmpty()) {
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(t.elevated);
        p.drawPath(Paint::panelPath(pill, t.radiusMd));
        Paint::paintBevel(p, pill.adjusted(-1, -1, 1, 1));
        p.setBrush(t.accent2);
        p.drawRoundedRect(QRectF(0, pill.top() + 8, 2, pill.height() - 16), 1, 1);
    }
}

}  // namespace Zero
