// SPDX-License-Identifier: GPL-3.0-only
#include "FilterChipBar.h"

#include <QAbstractButton>
#include <QHBoxLayout>
#include <QPainter>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include "ui/zero/ZeroSmoothScroll.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {

using LibraryFilter::Chip;
using LibraryFilter::ChipInfo;
using LibraryFilter::Kind;

/// One chip. Not a Q_OBJECT: it only uses the signals of QAbstractButton.
class FilterChip : public QAbstractButton {
   public:
    explicit FilterChip(QWidget* parent)
        : QAbstractButton(parent), m_hover(new Motion::StateAnimator(this)), m_activeAnim(new Motion::StateAnimator(this))
    {
        setFocusPolicy(Qt::TabFocus);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        setFixedHeight(FilterChipBar::kChipHeight);
        connect(m_hover, &Motion::StateAnimator::progressChanged, this, qOverload<>(&QWidget::update));
        connect(m_activeAnim, &Motion::StateAnimator::progressChanged, this, qOverload<>(&QWidget::update));
        connect(&Theme::instance(), &Theme::changed, this, [this]() {
            updateGeometry();
            update();
        });
    }

    void setInfo(const ChipInfo& info)
    {
        m_chip = info.chip;
        m_count = info.count;
        setText(info.label);
        setAccessibleName(info.label);
        switch (info.chip.kind) {
            case Kind::Favorites:
                setToolTip(QObject::tr("Instances you starred"));
                break;
            case Kind::Running:
                setToolTip(QObject::tr("Instances that are running now"));
                break;
            case Kind::Loader:
                setToolTip(QObject::tr("%1 instances").arg(info.chip.value));
                break;
            case Kind::Tag:
                setToolTip(QObject::tr("Instances tagged \"%1\"").arg(info.chip.value));
                break;
            default:
                setToolTip({});
        }
        updateGeometry();
        update();
    }
    const Chip& chip() const { return m_chip; }

    void setActiveChip(bool active)
    {
        if (m_isActive == active)
            return;
        m_isActive = active;
        m_activeAnim->setActive(active);
        setAccessibleDescription(active ? QObject::tr("Selected") : QString());
    }
    bool isActiveChip() const { return m_isActive; }

    QSize sizeHint() const override
    {
        const QFontMetrics fm(Style::font(Role::Label));
        return { 12 + 12 + contentWidth(fm) + kDotSlot, FilterChipBar::kChipHeight };
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

   protected:
    void enterEvent(QEnterEvent* event) override
    {
        QAbstractButton::enterEvent(event);
        m_hover->setActive(true);
    }
    void leaveEvent(QEvent* event) override
    {
        QAbstractButton::leaveEvent(event);
        m_hover->setActive(false);
    }
    void focusInEvent(QFocusEvent* event) override
    {
        QAbstractButton::focusInEvent(event);
        update();
    }
    void focusOutEvent(QFocusEvent* event) override
    {
        QAbstractButton::focusOutEvent(event);
        update();
    }

    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        const qreal hover = m_hover->progress();
        const qreal act = m_activeAnim->progress();
        const bool pressed = isDown();

        QColor bg = mix(mix(t.surface, t.surfaceHover, hover), t.elevated, act);
        if (pressed)
            bg = t.pressed;
        const QColor border = mix(mix(t.border, t.borderStrong, hover), t.borderStrong, act * 0.5);
        const QColor fg = mix(t.textSecondary, t.text, qMax(act, hover * 0.8));

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setPen(QPen(border, 1));
        p.setBrush(bg);
        p.drawPath(Paint::panelPath(r, t.radiusMd));
        Paint::paintBevel(p, QRectF(rect()), pressed || act > 0.5);
        if (hasFocus()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(t.accent2, 2));
            p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(1, 1, -1, -1), t.radiusMd));
        }

        // Content: [dot] [star] label [count]. The dot slot is part of the width of every chip, so selecting a chip never
        // moves its neighbours; the group stays centered and shifts by half the slot while the dot fades in.
        const QFont font = Style::font(Role::Label);
        const QFontMetrics fm(font);
        const int content = contentWidth(fm);
        const qreal group = content + kDotSlot * act;
        qreal x = (width() - group) / 2.0;
        const int cy = height() / 2;
        if (act > 0.0) {
            QColor dot = t.accent2;
            dot.setAlphaF(act);
            p.setPen(Qt::NoPen);
            p.setBrush(dot);
            p.drawEllipse(QPointF(x + 3, cy), 3, 3);
            x += kDotSlot * act;
        }
        if (m_chip.kind == Kind::Favorites) {
            Icons::filledIcon(QStringLiteral("star"), t.highlight).paint(&p, QRect(int(x), cy - 6, 12, 12));
            x += 16;
        }
        p.setFont(font);
        p.setPen(fg);
        p.drawText(QPointF(x, cy + (fm.ascent() - fm.descent()) / 2.0), text());
        x += fm.horizontalAdvance(text());
        if (m_chip.kind == Kind::Tag && m_count > 0) {
            const QFont captionFont = Style::font(Role::Caption);
            const QFontMetrics cm(captionFont);
            p.setFont(captionFont);
            p.setPen(t.textTertiary);
            p.drawText(QPointF(x + 6, cy + (cm.ascent() - cm.descent()) / 2.0), QString::number(m_count));
        }
    }

   private:
    static constexpr int kDotSlot = 12;  // 6 px dot + 6 px gap

    int contentWidth(const QFontMetrics& fm) const
    {
        int w = fm.horizontalAdvance(text());
        if (m_chip.kind == Kind::Favorites)
            w += 16;
        if (m_chip.kind == Kind::Tag && m_count > 0)
            w += 6 + QFontMetrics(Style::font(Role::Caption)).horizontalAdvance(QString::number(m_count));
        return w;
    }

    Motion::StateAnimator* m_hover;
    Motion::StateAnimator* m_activeAnim;
    Chip m_chip;
    int m_count = 0;
    bool m_isActive = false;
};

namespace {
/// Scroll area that turns the vertical wheel into horizontal scrolling.
class ChipScroll : public QScrollArea {
   public:
    using QScrollArea::QScrollArea;

   protected:
    void wheelEvent(QWheelEvent* event) override
    {
        const int delta = event->angleDelta().x() != 0 ? event->angleDelta().x() : event->angleDelta().y();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta / 2);
        event->accept();
    }
};
}  // namespace

FilterChipBar::FilterChipBar(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(kChipHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_scroll = new ChipScroll(this);
    m_scroll->setProperty(SmoothScroll::kOptOutProperty, true);  // ChipScroll maps the wheel to horizontal itself
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->viewport()->setAutoFillBackground(false);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));

    m_content = new QWidget;
    m_content->setAutoFillBackground(false);
    m_scroll->setWidget(m_content);
    m_row = new QHBoxLayout(m_content);
    m_row->setContentsMargins(0, 0, 0, 0);
    m_row->setSpacing(Tokens::spaceSm);
    m_row->addStretch(1);

    // Scroll arrows: only shown while there is more to see in that direction.
    auto makeArrow = [this](const QString& tooltip) {
        auto* button = new QToolButton(this);
        button->setAutoRaise(false);
        button->setFixedSize(kChipHeight, kChipHeight);
        button->setIconSize(QSize(16, 16));
        button->setToolTip(tooltip);
        button->setFocusPolicy(Qt::NoFocus);
        button->hide();
        Style::setProperty(button, "zeroVariant", "ghost");
        return button;
    };
    m_left = makeArrow(tr("Show earlier filters"));
    m_right = makeArrow(tr("Show more filters"));
    connect(m_left, &QToolButton::clicked, this, [this]() { scrollBy(-kScrollStep); });
    connect(m_right, &QToolButton::clicked, this, [this]() { scrollBy(kScrollStep); });

    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(Tokens::spaceXs);
    outer->addWidget(m_left);
    outer->addWidget(m_scroll, 1);
    outer->addWidget(m_right);

    auto* bar = m_scroll->horizontalScrollBar();
    connect(bar, &QScrollBar::rangeChanged, this, &FilterChipBar::scheduleArrowUpdate);
    connect(bar, &QScrollBar::valueChanged, this, &FilterChipBar::scheduleArrowUpdate);
    connect(&Theme::instance(), &Theme::changed, this, &FilterChipBar::refreshArrowIcons);
    refreshArrowIcons();
}

void FilterChipBar::refreshArrowIcons()
{
    const auto& t = Theme::current();
    m_left->setIcon(Icons::icon(QStringLiteral("chevron-left"), t.textSecondary));
    m_right->setIcon(Icons::icon(QStringLiteral("chevron-right"), t.textSecondary));
}

void FilterChipBar::scheduleArrowUpdate()
{
    if (m_arrowUpdate)
        return;
    m_arrowUpdate = true;
    QTimer::singleShot(0, this, [this]() {
        m_arrowUpdate = false;
        const auto* bar = m_scroll->horizontalScrollBar();
        m_left->setVisible(bar->value() > bar->minimum());
        m_right->setVisible(bar->value() < bar->maximum());
    });
}

void FilterChipBar::scrollBy(int delta)
{
    auto* bar = m_scroll->horizontalScrollBar();
    const int target = qBound(bar->minimum(), bar->value() + delta, bar->maximum());
    const int duration = Motion::duration(Tokens::durBase);
    if (duration == 0) {
        bar->setValue(target);
        return;
    }
    auto* anim = new QPropertyAnimation(bar, "value", this);
    anim->setDuration(duration);
    anim->setEasingCurve(Motion::easeGlide());
    anim->setStartValue(bar->value());
    anim->setEndValue(target);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void FilterChipBar::setChips(const QList<ChipInfo>& chips, const Chip& active)
{
    QList<FilterChip*> old = m_chips;
    QList<FilterChip*> next;
    for (const ChipInfo& info : chips) {
        FilterChip* chip = nullptr;
        for (FilterChip* candidate : std::as_const(old)) {
            if (candidate->chip() == info.chip) {
                chip = candidate;
                old.removeOne(candidate);
                break;
            }
        }
        if (!chip) {
            chip = new FilterChip(m_content);
            connect(chip, &QAbstractButton::clicked, this, [this, chip]() {
                setActive(chip->chip());
                emit chipActivated(chip->chip());
            });
        }
        chip->setInfo(info);
        next << chip;
    }
    for (FilterChip* gone : std::as_const(old)) {
        m_row->removeWidget(gone);
        gone->hide();
        gone->deleteLater();
    }
    m_chips = next;
    relayout();

    bool found = false;
    for (FilterChip* chip : std::as_const(m_chips))
        found = found || chip->chip() == active;
    m_active = found ? active : Chip{};
    for (FilterChip* chip : std::as_const(m_chips))
        chip->setActiveChip(chip->chip() == m_active);
}

void FilterChipBar::relayout()
{
    // chips in order, the stretch last (taking an item out of the layout does not touch its widget)
    while (m_row->count() > 0)
        delete m_row->takeAt(0);
    for (FilterChip* chip : std::as_const(m_chips)) {
        m_row->addWidget(chip);
        chip->show();
    }
    m_row->addStretch(1);
}

void FilterChipBar::setActive(const Chip& chip)
{
    bool found = false;
    for (FilterChip* c : std::as_const(m_chips))
        found = found || c->chip() == chip;
    m_active = found ? chip : Chip{};
    for (FilterChip* c : std::as_const(m_chips)) {
        const bool on = c->chip() == m_active;
        c->setActiveChip(on);
        if (on)
            QTimer::singleShot(0, c, [this, c]() { m_scroll->ensureWidgetVisible(c, 24, 0); });
    }
}

int FilterChipBar::chipCount() const
{
    return int(m_chips.size());
}

QStringList FilterChipBar::labels() const
{
    QStringList out;
    for (FilterChip* c : m_chips)
        out << c->text();
    return out;
}

QWidget* FilterChipBar::chipWidget(const Chip& chip) const
{
    for (FilterChip* c : m_chips) {
        if (c->chip() == chip)
            return c;
    }
    return nullptr;
}

bool FilterChipBar::isChipActive(const Chip& chip) const
{
    for (FilterChip* c : m_chips) {
        if (c->chip() == chip)
            return c->isActiveChip();
    }
    return false;
}

int FilterChipBar::scrollValue() const
{
    return m_scroll->horizontalScrollBar()->value();
}

bool FilterChipBar::leftArrowVisible() const
{
    return !m_left->isHidden();
}

bool FilterChipBar::rightArrowVisible() const
{
    return !m_right->isHidden();
}

int FilterChipBar::scrollMaximum() const
{
    return m_scroll->horizontalScrollBar()->maximum();
}

}  // namespace Zero
