// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - removable filter chips.
#include "FilterChipStrip.h"

#include <QAbstractButton>
#include <QKeyEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVariantAnimation>
#include <algorithm>
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {
namespace Content {

namespace {

constexpr int kMaxChipWidth = 260;

/// One filter: its label and an x. The whole chip is the remove button ("Remove filter <label>"). Default chips (part of the
/// section defaults) are outlined only, inactive chips (their facet is disabled for the current provider) are struck through.
class ChipButton : public QAbstractButton {
   public:
    ChipButton(const Chip& chip, const QString& reason, QWidget* parent) : QAbstractButton(parent), m_hover(new Motion::StateAnimator(this))
    {
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        setFixedHeight(FilterChipStrip::kChipHeight);
        setChip(chip, reason);
        connect(m_hover, &Motion::StateAnimator::progressChanged, this, qOverload<>(&QWidget::update));
        connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    }

    const Chip& chip() const { return m_chip; }
    void setChip(const Chip& chip, const QString& reason)
    {
        m_chip = chip;
        setText(chip.label);
        setAccessibleName(QObject::tr("Remove filter %1").arg(chip.label));
        setToolTip(reason.isEmpty() ? accessibleName() : accessibleName() + QStringLiteral(" - ") + reason);
        update();
    }
    void setAppear(qreal appear)
    {
        m_appear = appear;
        update();
    }

    /// The width the chip takes when fully grown.
    int fullWidth() const
    {
        const QFontMetrics fm(Style::font(Role::Label));
        return std::min(kMaxChipWidth, kPadLeft + fm.horizontalAdvance(text()) + Tokens::spaceXs + 2 + kIcon + kPadRight);
    }

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
    void keyPressEvent(QKeyEvent* e) override
    {
        switch (e->key()) {
            case Qt::Key_Return:
            case Qt::Key_Enter:
            case Qt::Key_Delete:
            case Qt::Key_Backspace:
                click();
                return;
            default:
                QAbstractButton::keyPressEvent(e);
        }
    }
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        const qreal hover = m_hover->progress();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setOpacity(m_appear);
        p.setClipRect(rect());  // growing in / out: the content is clipped by the chip's current width
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QColor fill = isDown() ? t.pressed : mix(t.elevated, t.higher, hover);
        if (m_chip.isDefault)
            fill = withAlpha(fill, 0.0);  // outlined only
        p.setPen(QPen(mix(t.border, t.borderStrong, hover), 1));
        p.setBrush(fill);
        p.drawPath(Paint::panelPath(r, t.radiusMd));
        if (hasFocus()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(t.accent2, 2));
            p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(1, 1, -1, -1), t.radiusMd));
        }
        QFont font = Style::font(Role::Label);
        font.setStrikeOut(m_chip.isInactive);
        const QFontMetrics fm(font);
        p.setFont(font);
        p.setPen(m_chip.isInactive ? t.textTertiary : (m_chip.isDefault ? t.textSecondary : t.text));
        const int textW = fullWidth() - kPadLeft - Tokens::spaceXs - 2 - kIcon - kPadRight;
        p.drawText(QRect(kPadLeft, 0, textW, height()), Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(text(), Qt::ElideRight, textW));
        const QRect icon(kPadLeft + textW + Tokens::spaceXs + 2, (height() - kIcon) / 2, kIcon, kIcon);
        Icons::icon(QStringLiteral("x"), mix(t.textSecondary, t.danger, hover)).paint(&p, icon);
    }

   private:
    static constexpr int kIcon = 12;
    static constexpr int kPadLeft = 10;
    static constexpr int kPadRight = 7;
    Chip m_chip;
    Motion::StateAnimator* m_hover;
    qreal m_appear = 1.0;
};

QString chipKey(const Chip& c)
{
    return c.facetId + QLatin1Char('\n') + c.value + (c.isQuery ? QStringLiteral("\nq") : QString());
}

}  // namespace

class FilterChipStrip::Canvas : public QWidget {
   public:
    using QWidget::QWidget;
};

struct FilterChipStrip::Item {
    ChipButton* chip = nullptr;
    qreal appear = 1.0;
    bool leaving = false;
    bool fadingOut = false;
    QVariantAnimation* anim = nullptr;
};

FilterChipStrip::Flow FilterChipStrip::flow(const QList<int>& widths, int available, int gap, int maxLines, int rowHeight)
{
    auto wrap = [&](int width, QList<QPoint>* positions) {
        int x = 0;
        int line = 0;
        for (int w : widths) {
            if (x > 0 && x + w > width) {
                ++line;
                x = 0;
            }
            if (positions)
                positions->append(QPoint(x, line * (rowHeight + gap)));
            x += w + gap;
        }
        return widths.isEmpty() ? 0 : line + 1;
    };
    Flow out;
    int width = std::max(1, available);
    if (wrap(width, nullptr) > maxLines) {
        // Just wide enough for `maxLines` lines (a single line always fits at the sum of all widths).
        int hi = std::max(width, gap * int(widths.size()));
        for (int w : widths)
            hi += w;
        int lo = width;
        while (lo < hi) {
            const int mid = (lo + hi) / 2;
            if (wrap(mid, nullptr) <= maxLines)
                hi = mid;
            else
                lo = mid + 1;
        }
        width = lo;
    }
    out.lines = wrap(width, &out.positions);
    out.contentWidth = width;
    return out;
}

FilterChipStrip::FilterChipStrip(QWidget* parent)
    : QWidget(parent),
      m_scroll(new QScrollArea(this)),
      m_canvas(new Canvas),
      m_clearAll(new QPushButton(tr("Clear all"), m_canvas)),
      m_showEverything(new QPushButton(tr("Show everything"), m_canvas))
{
    setObjectName(QStringLiteral("zeroFilterChipStrip"));
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(false);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll->setWidget(m_canvas);
    m_scroll->viewport()->setAutoFillBackground(false);
    m_canvas->setAutoFillBackground(false);

    for (QPushButton* b : { m_clearAll, m_showEverything }) {
        Style::setProperty(b, "zeroVariant", "ghost");
        b->setFixedHeight(kChipHeight);
        b->hide();
    }
    connect(m_clearAll, &QPushButton::clicked, this, [this]() { applyChange(clearAll(m_state, m_defaults)); });
    connect(m_showEverything, &QPushButton::clicked, this, [this]() { applyChange(showEverything(m_state)); });

    setFixedHeight(0);
    hide();
}

void FilterChipStrip::setFacets(const QList<Facet>& facets)
{
    m_facets = facets;
    refresh();
}

void FilterChipStrip::setDefaults(const FilterState& defaults)
{
    m_defaults = defaults;
    refresh();
}

void FilterChipStrip::setState(const FilterState& state)
{
    m_state = state;
    refresh();
}

QList<Chip> FilterChipStrip::chips() const
{
    QList<Chip> out;
    for (const Item* i : m_items)
        if (!i->leaving)
            out << i->chip->chip();
    return out;
}

QStringList FilterChipStrip::chipLabels() const
{
    QStringList out;
    for (const Chip& c : chips())
        out << c.label;
    return out;
}

QList<QAbstractButton*> FilterChipStrip::chipButtons() const
{
    QList<QAbstractButton*> out;
    for (const Item* i : m_items)
        if (!i->leaving)
            out << i->chip;
    return out;
}

void FilterChipStrip::applyChange(const FilterState& next)
{
    // Keyboard users keep their place: focus moves to the chip that takes the removed one's slot.
    int focusIndex = -1;
    {
        const auto buttons = chipButtons();
        for (int i = 0; i < buttons.size(); ++i)
            if (buttons[i]->hasFocus())
                focusIndex = i;
    }
    m_state = next;
    refresh();
    if (focusIndex >= 0) {
        const auto buttons = chipButtons();
        if (!buttons.isEmpty())
            buttons[std::min<int>(focusIndex, int(buttons.size()) - 1)]->setFocus(Qt::OtherFocusReason);
    }
    emit stateChanged(m_state);
}

void FilterChipStrip::removeItem(Item* item)
{
    m_items.removeOne(item);
    item->chip->hide();
    item->chip->deleteLater();  // also deletes its animation
    delete item;
    relayout();
}

void FilterChipStrip::refresh()
{
    const QList<Chip> wanted = chipsFor(m_state, m_facets, m_defaults);
    const int ms = Motion::duration(kAnimMs);
    auto animate = [this, ms](Item* item, qreal to) {
        if (item->anim)
            item->anim->stop();
        if (ms == 0) {
            item->appear = to;
            item->chip->setAppear(to);
            return;
        }
        if (!item->anim) {
            item->anim = new QVariantAnimation(item->chip);
            connect(item->anim, &QVariantAnimation::valueChanged, item->chip, [this, item](const QVariant& v) {
                item->appear = v.toReal();
                item->chip->setAppear(item->appear);
                relayout();
            });
            connect(item->anim, &QVariantAnimation::finished, item->chip, [this, item]() {
                if (item->leaving)
                    removeItem(item);
            });
        }
        item->anim->setDuration(ms);
        item->anim->setStartValue(item->appear);
        item->anim->setEndValue(to);
        item->anim->setEasingCurve(to > item->appear ? Motion::easeEnter() : Motion::easeExit());
        item->anim->start();
    };

    // Reuse the live chip with the same identity; create new ones; the rest leaves.
    QList<Item*> live;
    QList<Item*> pool;
    for (Item* i : m_items)
        if (!i->leaving)
            pool << i;
    for (const Chip& c : wanted) {
        const QString reason = [&]() {
            for (const Facet& f : m_facets)
                if (f.id == c.facetId)
                    return f.disabledReason;
            return QString();
        }();
        Item* reuse = nullptr;
        for (Item* i : pool)
            if (chipKey(i->chip->chip()) == chipKey(c)) {
                reuse = i;
                break;
            }
        if (reuse) {
            pool.removeOne(reuse);
            reuse->chip->setChip(c, reason);
        } else {
            reuse = new Item;
            reuse->chip = new ChipButton(c, reason, m_canvas);
            connect(reuse->chip, &QAbstractButton::clicked, this, [this, item = reuse]() {
                if (!item->leaving)
                    applyChange(withoutChip(m_state, item->chip->chip(), m_defaults));
            });
            reuse->appear = ms == 0 ? 1.0 : 0.0;
            reuse->chip->setAppear(reuse->appear);
            m_items.append(reuse);
            reuse->chip->show();
            if (ms > 0)
                animate(reuse, 1.0);
        }
        live << reuse;
    }
    // Chips that are no longer wanted fade where they stood: they keep their slot until they are gone.
    QList<Item*> out = live;
    for (int i = 0; i < m_items.size(); ++i) {
        Item* item = m_items[i];
        if (live.contains(item))
            continue;
        if (!item->leaving) {
            item->leaving = true;
            item->chip->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            item->chip->setFocusPolicy(Qt::NoFocus);
        }
        out.insert(std::min<int>(i, int(out.size())), item);
    }
    m_items = out;
    for (Item* item : QList<Item*>(m_items)) {
        if (!item->leaving || item->fadingOut)
            continue;
        item->fadingOut = true;
        if (ms == 0)
            removeItem(item);  // motion off: gone at once
        else
            animate(item, 0.0);
    }
    relayout();
}

void FilterChipStrip::relayout()
{
    QList<QWidget*> widgets;
    QList<int> widths;
    for (Item* i : m_items) {
        widgets << i->chip;
        widths << std::max(1, int(i->chip->fullWidth() * i->appear));
    }
    const bool anyLive = std::any_of(m_items.cbegin(), m_items.cend(), [](const Item* i) { return !i->leaving; });
    const bool offerShowEverything = anyLive && showEverything(m_defaults) != m_defaults && showEverything(m_state) != m_state;
    m_clearAll->setVisible(anyLive);
    m_showEverything->setVisible(offerShowEverything);
    if (anyLive) {
        widgets << m_clearAll;
        widths << m_clearAll->sizeHint().width();
    }
    if (offerShowEverything) {
        widgets << m_showEverything;
        widths << m_showEverything->sizeHint().width();
    }

    const int available = width() > 1 ? width() : 480;
    const Flow f = flow(widths, available);
    for (int i = 0; i < widgets.size(); ++i) {
        widgets[i]->setGeometry(QRect(f.positions[i], QSize(widths[i], kChipHeight)));
        widgets[i]->setVisible(true);
    }
    const int contentH = f.lines == 0 ? 0 : f.lines * kChipHeight + (f.lines - 1) * kGap;
    m_canvas->resize(f.contentWidth, contentH);
    m_lines = f.lines;
    m_scrolls = f.contentWidth > available;
    const int bar = m_scrolls ? m_scroll->horizontalScrollBar()->sizeHint().height() : 0;
    m_scroll->setGeometry(0, 0, width(), contentH + bar);
    setFixedHeight(contentH + bar);
    setVisible(!m_items.isEmpty());
}

void FilterChipStrip::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_scroll->setGeometry(0, 0, width(), height());
    relayout();
}

}  // namespace Content
}  // namespace Zero
