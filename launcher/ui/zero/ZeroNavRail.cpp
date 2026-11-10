// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroNavRail.h"

#include <QAbstractButton>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>
#include "ZeroIcons.h"
#include "ZeroMotion.h"
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
            fg = t.text;
            p.setPen(Qt::NoPen);
            p.setBrush(t.elevated);
            p.drawRoundedRect(pill, Tokens::radiusMd, Tokens::radiusMd);
            // 2 px accent indicator at the rail's left edge.
            p.setBrush(t.accent);
            p.drawRoundedRect(QRectF(0, 8, 2, height() - 16), 1, 1);
        } else if (h > 0.0) {
            p.setPen(Qt::NoPen);
            p.setBrush(withAlpha(t.surfaceHover, h));
            p.drawRoundedRect(pill, Tokens::radiusMd, Tokens::radiusMd);
        }
        if (m_keyboardFocus && hasFocus()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(t.textTertiary, 1));
            p.drawRoundedRect(pill.adjusted(0.5, 0.5, -0.5, -0.5), Tokens::radiusMd, Tokens::radiusMd);
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
    : QWidget(parent), m_layout(new QVBoxLayout(this)), m_wordmark(new QLabel(this)), m_caption(new QLabel(this)), m_anim(new QPropertyAnimation(this, "railWidth", this))
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

    m_anim->setEasingCurve(QEasingCurve::OutCubic);
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
    m_current = id;
    for (auto* item : std::as_const(m_items))
        item->setCurrentItem(item->id() == id);
    emit currentChanged(id);
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
    if (Motion::duration(Tokens::durPage) == 0) {
        setRailWidth(target);
    } else {
        m_anim->setDuration(Tokens::durPage);
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
    p.fillRect(rect(), t.surface);
    p.fillRect(QRect(width() - 1, 0, 1, height()), t.border);
}

}  // namespace Zero
