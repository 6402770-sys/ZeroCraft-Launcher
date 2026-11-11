// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroNavRail.h"

#include <QPixmap>
#include <QtMath>
#include <QAbstractButton>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QIcon>
#include <QPainter>
#include <QVariantAnimation>
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
constexpr int kHeaderHeight = 60;   // logo header: same height as the top bar, so the two bottom lines are one rule
constexpr int kLogoSize = 20;
constexpr int kPixelLogoSize = 24;  // target for the pixel mark, rounded to a multiple of 16 device pixels
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
        connect(&m_pop, &QVariantAnimation::valueChanged, this, qOverload<>(&QWidget::update));
    }

    const QString& id() const { return m_id; }
    void setCurrentItem(bool c)
    {
        const bool became = c && !m_current;
        m_current = c;
        if (became && Motion::enabled()) {
            // The icon of the section you go to pops (1 -> 1.18 -> 1, with a little overshoot on the way back).
            m_pop.stop();
            m_pop.setDuration(Tokens::durSlow);
            m_pop.setStartValue(0.0);
            m_pop.setEndValue(1.0);
            m_pop.start();
        }
        update();
    }
    bool showsLabel() const { return width() >= kLabelMinWidth; }
    /// Shortcut shown in the tooltip next to the label ("Library  Alt+2"). Items with a hint always have a tooltip; the
    /// others only while the rail is collapsed (then the tooltip is the label).
    void setShortcutHint(const QString& hint)
    {
        m_hint = hint;
        updateTip();
    }
    /// One sentence about what the section is for, shown under the label in the tooltip.
    void setDescription(const QString& description)
    {
        m_description = description;
        updateTip();
    }
    /// Pill rectangle of this item in rail coordinates (the rail paints the active pill, see ZeroNavRail::paintEvent).
    QRectF pillRect() const { return QRectF(geometry()).adjusted(kPillInset, 0, -kPillInset, 0); }

    QSize sizeHint() const override { return { Tokens::navRailWidth, Tokens::navItemHeight }; }

   protected:
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
        updateTip();
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
        qreal scale = 1.0;
        if (m_pop.state() == QAbstractAnimation::Running) {
            const qreal t = m_pop.currentValue().toReal();
            scale = t < 0.4 ? 1.0 + 0.18 * Motion::easeEnter().valueForProgress(t / 0.4)
                            : 1.0 + 0.18 * (1.0 - Motion::easeRelease().valueForProgress((t - 0.4) / 0.6));
        }
        if (scale != 1.0) {
            p.save();
            p.translate(QRectF(iconRect).center());
            p.scale(scale, scale);
            p.translate(-QRectF(iconRect).center());
            Icons::icon(m_icon, fg).paint(&p, iconRect);
            p.restore();
        } else {
            Icons::icon(m_icon, fg).paint(&p, iconRect);
        }

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
    void updateTip()
    {
        QString head = m_hint.isEmpty() ? text() : text() + QStringLiteral("  ") + m_hint;
        if (!m_description.isEmpty())
            setToolTip(QStringLiteral("<b>%1</b><br>%2").arg(head.toHtmlEscaped(), m_description.toHtmlEscaped()));
        else if (m_hint.isEmpty())
            setToolTip(showsLabel() ? QString() : text());
        else
            setToolTip(head);
    }

    QString m_id;
    QString m_icon;
    QString m_hint;
    QString m_description;
    Motion::StateAnimator* m_hover;
    QVariantAnimation m_pop;
    bool m_current = false;
    bool m_keyboardFocus = false;
};

/// Top 60 px of the rail: logo mark + "ZERO" wordmark (logo only when collapsed) over a 1 px `border` line that lines up
/// with the top bar's bottom line.
class RailHeader : public QWidget {
   public:
    explicit RailHeader(QWidget* parent) : QWidget(parent)
    {
        setFixedHeight(kHeaderHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.fillRect(QRect(0, height() - 1, width(), 1), t.border);

        const int contentH = height() - 1;
        const bool wide = width() >= kLabelMinWidth;
        // Pixel themes draw the pixel-art mark at a whole multiple of its 16 px grid (crisp at every scale factor).
        const bool pixelMark = t.id == QLatin1String("minecraft");
        const qreal dpr = devicePixelRatioF();
        const int markDevice = pixelMark ? qMax(16, qRound(kPixelLogoSize * dpr / 16.0) * 16) : 0;
        const int logoSize = pixelMark ? qCeil(markDevice / dpr) : kLogoSize;
        const int logoX = wide ? kPillInset + kIconPad - (logoSize - kLogoSize) / 2 : (width() - logoSize) / 2;
        const QRect logo(logoX, (contentH - logoSize) / 2, logoSize, logoSize);
        if (pixelMark) {
            static const QIcon pixel(QStringLiteral(":/zero/zero-logo-minecraft.svg"));
            QPixmap pm = pixel.pixmap(QSize(markDevice, markDevice), 1.0);
            pm.setDevicePixelRatio(dpr);
            p.setRenderHint(QPainter::SmoothPixmapTransform, false);
            p.drawPixmap(logo.topLeft(), pm);
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        } else {
            static const QIcon mark(QStringLiteral(":/zero/zero-logo.svg"));  // full-colour app mark: drawn as is
            mark.paint(&p, logo);
        }
        if (!wide)
            return;

        // Obsidian: 16 px, weight 600, 2 px letter spacing. Pixel themes: the pixel font at 20 px (Title role), normal weight.
        QFont f = Style::font(Role::Title);
        if (!t.usesPixelFont(Role::Title)) {
            f.setPixelSize(16);
            f.setWeight(QFont::DemiBold);
            f.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
        }
        const QFontMetrics fm(f);
        const int x = logo.right() + 1 + 10;
        const int baseline = (contentH - fm.height()) / 2 + fm.ascent();
        p.setFont(f);
        p.setPen(t.text);
        p.drawText(QPoint(x, baseline), QStringLiteral("ZERO"));
    }
};

ZeroNavRail::ZeroNavRail(QWidget* parent)
    : QWidget(parent),
      m_layout(new QVBoxLayout(this)),
      m_anim(new QPropertyAnimation(this, "railWidth", this)),
      m_glide(new QVariantAnimation(this))
{
    setFixedWidth(Tokens::navRailWidth);
    m_layout->setContentsMargins(0, 0, 0, Tokens::spaceMd);
    m_layout->setSpacing(2);

    m_layout->addWidget(new RailHeader(this));
    m_layout->addSpacing(Tokens::spaceMd);

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

void ZeroNavRail::setItemDescription(const QString& id, const QString& description)
{
    // pages are in m_items; action items (More) are only children of the rail
    for (auto* button : findChildren<QAbstractButton*>(QString(), Qt::FindDirectChildrenOnly))
        if (auto* item = dynamic_cast<NavItem*>(button); item && item->id() == id)
            item->setDescription(description);
}

void ZeroNavRail::setItemShortcutHint(const QString& id, const QString& hint)
{
    if (auto* item = m_items.value(id))
        item->setShortcutHint(hint);
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
    emit collapsedChanged(collapsed);
}

void ZeroNavRail::setRailWidth(int w)
{
    setFixedWidth(w);
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
