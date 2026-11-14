// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - single-choice chip row (see ChoiceChips.h).
#include "ChoiceChips.h"

#include <QAbstractButton>
#include <QKeyEvent>
#include <QPainter>

#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/pages/FlowLayout.h"

namespace Zero {
namespace Create {

/// One chip. Checkable button; the row keeps exactly one (or none) checked.
class ChoiceChip : public QAbstractButton {
   public:
    ChoiceChip(const QString& id, const QString& label, QWidget* parent)
        : QAbstractButton(parent), m_id(id), m_hover(new Motion::StateAnimator(this)), m_active(new Motion::StateAnimator(this))
    {
        setText(label);
        setAccessibleName(label);
        setCheckable(true);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        setFixedHeight(ChoiceChipRow::kChipHeight);
        connect(m_hover, &Motion::StateAnimator::progressChanged, this, qOverload<>(&QWidget::update));
        connect(m_active, &Motion::StateAnimator::progressChanged, this, qOverload<>(&QWidget::update));
        connect(this, &QAbstractButton::toggled, this, [this](bool on) { m_active->setActive(on); });
        connect(&Theme::instance(), &Theme::changed, this, [this]() {
            updateGeometry();
            update();
        });
    }

    const QString& id() const { return m_id; }

    QSize sizeHint() const override
    {
        const QFontMetrics fm(Style::font(Role::Label));
        return { Tokens::spaceMd * 2 + kDotSlot + fm.horizontalAdvance(text()), ChoiceChipRow::kChipHeight };
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
    /// A chosen chip stays chosen when clicked again (single choice).
    void nextCheckState() override
    {
        if (!isChecked())
            setChecked(true);
    }

    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        const qreal hover = m_hover->progress();
        const qreal act = m_active->progress();
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

        // [dot] label, centred. The dot slot is always part of the width, so choosing a chip never moves its neighbours.
        const QFont font = Style::font(Role::Label);
        const QFontMetrics fm(font);
        const qreal group = fm.horizontalAdvance(text()) + kDotSlot * act;
        qreal x = (width() - group) / 2.0;
        const int cy = height() / 2;
        if (act > 0.0) {
            p.setPen(Qt::NoPen);
            p.setBrush(withAlpha(t.accent2, act));
            p.drawEllipse(QPointF(x + 3, cy), 3, 3);
            x += kDotSlot * act;
        }
        p.setFont(font);
        p.setPen(fg);
        p.drawText(QPointF(x, cy + (fm.ascent() - fm.descent()) / 2.0), text());
    }

   private:
    static constexpr int kDotSlot = 12;  // 6 px dot + 6 px gap

    QString m_id;
    Motion::StateAnimator* m_hover;
    Motion::StateAnimator* m_active;
};

ChoiceChipRow::ChoiceChipRow(const QString& accessibleName, QWidget* parent) : QWidget(parent)
{
    setAccessibleName(accessibleName);
    m_flow = new FlowLayout(this, Tokens::spaceSm, Tokens::spaceSm);
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

void ChoiceChipRow::setOptions(const QList<QPair<QString, QString>>& options)
{
    QStringList wanted;
    for (const auto& o : options)
        wanted.append(o.first);
    if (wanted == ids()) {
        for (int i = 0; i < m_chips.size(); ++i) {
            if (m_chips.at(i)->text() != options.at(i).second) {
                m_chips.at(i)->setText(options.at(i).second);
                m_chips.at(i)->setAccessibleName(options.at(i).second);
                m_chips.at(i)->updateGeometry();
            }
        }
        return;
    }
    for (ChoiceChip* chip : std::as_const(m_chips)) {
        m_flow->removeWidget(chip);
        chip->deleteLater();
    }
    m_chips.clear();
    for (const auto& o : options) {
        auto* chip = new ChoiceChip(o.first, o.second, this);
        chip->installEventFilter(this);
        connect(chip, &QAbstractButton::clicked, this, [this, id = o.first]() { choose(id, true); });
        m_flow->addWidget(chip);
        m_chips.append(chip);
    }
    // Tab order follows the reading order.
    for (int i = 1; i < m_chips.size(); ++i)
        setTabOrder(m_chips.at(i - 1), m_chips.at(i));
    choose(wanted.contains(m_current) ? m_current : QString(), false);
    updateGeometry();
}

void ChoiceChipRow::setCurrent(const QString& id)
{
    choose(id, false);
}

void ChoiceChipRow::choose(const QString& id, bool notify)
{
    QString chosen;
    for (ChoiceChip* chip : std::as_const(m_chips)) {
        const bool on = chip->id() == id;
        if (chip->isChecked() != on)
            chip->setChecked(on);
        if (on)
            chosen = id;
    }
    const bool changed = chosen != m_current;
    m_current = chosen;
    if (notify && changed)
        emit currentChanged(m_current);
}

QStringList ChoiceChipRow::ids() const
{
    QStringList out;
    for (const ChoiceChip* chip : m_chips)
        out.append(chip->id());
    return out;
}

QStringList ChoiceChipRow::labels() const
{
    QStringList out;
    for (const ChoiceChip* chip : m_chips)
        out.append(chip->text());
    return out;
}

QAbstractButton* ChoiceChipRow::chip(const QString& id) const
{
    for (ChoiceChip* chip : m_chips)
        if (chip->id() == id)
            return chip;
    return nullptr;
}

int ChoiceChipRow::heightForWidth(int width) const
{
    return m_flow->heightForWidth(width);
}

bool ChoiceChipRow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        const int step = key->key() == Qt::Key_Left ? -1 : key->key() == Qt::Key_Right ? 1 : 0;
        if (step != 0 && key->modifiers() == Qt::NoModifier) {
            const int index = m_chips.indexOf(static_cast<ChoiceChip*>(watched));
            const int next = index + step;
            if (index >= 0 && next >= 0 && next < m_chips.size()) {
                m_chips.at(next)->setFocus(Qt::TabFocusReason);
                choose(m_chips.at(next)->id(), true);
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

}  // namespace Create
}  // namespace Zero
