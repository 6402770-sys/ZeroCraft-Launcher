// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Create Instance type cards (see TypeCardRow.h).
#include "TypeCardRow.h"

#include <QHBoxLayout>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTextLayout>

#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {
namespace Create {

namespace {
constexpr int kIconSize = 20;
constexpr int kIndicatorHeight = 2;

/// The 2 px bar under the chosen card. Mouse-transparent; it only paints.
class Indicator : public QWidget {
   public:
    explicit Indicator(QWidget* parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), Theme::current().accent2);
    }
};

/// Up to `maxLines` lines of `text` wrapped to `width`; the last line is elided when the text is longer.
QStringList wrapLines(const QString& text, const QFont& font, int width, int maxLines)
{
    QStringList lines;
    if (width <= 0)
        return lines;
    QTextLayout layout(text, font);
    layout.beginLayout();
    while (lines.size() < maxLines) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(width);
        lines.append(text.mid(line.textStart(), line.textLength()).trimmed());
        if (lines.size() == maxLines && line.textStart() + line.textLength() < text.size())
            lines.last() = QFontMetrics(font).elidedText(text.mid(line.textStart()), Qt::ElideRight, width);
    }
    layout.endLayout();
    return lines;
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// TypeCard
// ---------------------------------------------------------------------------------------------------------------------

TypeCard::TypeCard(const QString& icon, const QString& title, const QString& description, QWidget* parent)
    : ZeroCard(parent), m_icon(icon), m_title(title), m_description(description), m_chosenAnim(new Motion::StateAnimator(this))
{
    setClickable(true);
    setFixedHeight(kHeight);
    setMinimumWidth(120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAccessibleName(title);
    setAccessibleDescription(description);
    setToolTip(description);
    connect(m_chosenAnim, &Motion::StateAnimator::progressChanged, this, qOverload<>(&QWidget::update));
}

void TypeCard::setChosen(bool chosen)
{
    if (m_chosen == chosen)
        return;
    m_chosen = chosen;
    m_chosenAnim->setActive(chosen);
    setAccessibleDescription(chosen ? tr("Selected. %1").arg(m_description) : m_description);
}

void TypeCard::focusInEvent(QFocusEvent* event)
{
    // The ring marks keyboard focus only: a mouse click already shows the choice, and on a card that is not the chosen
    // one the ring would look like a second selection.
    const auto reason = event->reason();
    m_focusVisible = reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason || reason == Qt::ShortcutFocusReason;
    setFocusRingVisible(m_focusVisible);
    ZeroCard::focusInEvent(event);
}

void TypeCard::focusOutEvent(QFocusEvent* event)
{
    m_focusVisible = false;
    ZeroCard::focusOutEvent(event);
}

void TypeCard::mousePressEvent(QMouseEvent* event)
{
    m_focusVisible = false;
    setFocusRingVisible(false);
    ZeroCard::mousePressEvent(event);
}

void TypeCard::paintEvent(QPaintEvent* event)
{
    ZeroCard::paintEvent(event);  // surface, hover, border, focus, bevel
    const auto& t = Theme::current();
    const qreal chosen = m_chosenAnim->progress();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (chosen > 0.0) {
        // `elevated` fill inside the 1 px border, then the bevel again on top of it.
        QColor fill = t.elevated;
        fill.setAlphaF(chosen);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(1, 1, -1, -1), qMax(0, t.radiusLg - 1)));
        Paint::paintBevel(p, QRectF(rect()));
    }

    const QRect content = contentsRect();
    const QColor iconColor = mix(t.textSecondary, t.accent, chosen);
    Icons::icon(m_icon, iconColor).paint(&p, QRect(content.left(), content.top(), kIconSize, kIconSize));

    const QFont titleFont = Style::font(Role::Heading);
    const QFontMetrics tm(titleFont);
    p.setFont(titleFont);
    p.setPen(t.text);
    const QRect titleRect(content.left() + kIconSize + Tokens::spaceSm, content.top(), content.width() - kIconSize - Tokens::spaceSm, kIconSize);
    p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, tm.elidedText(m_title, Qt::ElideRight, titleRect.width()));

    const QFont captionFont = Style::font(Role::Caption);
    const QFontMetrics cm(captionFont);
    p.setFont(captionFont);
    p.setPen(t.textSecondary);
    int y = content.top() + kIconSize + Tokens::spaceSm;
    for (const QString& line : wrapLines(m_description, captionFont, content.width(), 2)) {
        p.drawText(QRect(content.left(), y, content.width(), cm.height()), Qt::AlignLeft | Qt::AlignVCenter, line);
        y += cm.height();
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// TypeCardRow
// ---------------------------------------------------------------------------------------------------------------------

TypeCardRow::TypeCardRow(QWidget* parent) : QWidget(parent), m_indicator(new Indicator(this))
{
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(Tokens::spaceLg);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(TypeCard::kHeight);
    m_indicator->hide();
    m_glide.setEasingCurve(Motion::easeGlide());
    connect(&m_glide, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { m_indicator->setGeometry(v.toRect()); });
}

TypeCard* TypeCardRow::addCard(const QString& icon, const QString& title, const QString& description)
{
    auto* card = new TypeCard(icon, title, description, this);
    const int index = int(m_cards.size());
    m_cards.append(card);
    static_cast<QHBoxLayout*>(layout())->addWidget(card, 1);
    card->installEventFilter(this);
    connect(card, &ZeroCard::clicked, this, [this, index]() {
        setCurrent(index);
        emit chosen(index);
    });
    if (index > 0)
        setTabOrder(m_cards.at(index - 1), card);
    m_indicator->raise();
    return card;
}

void TypeCardRow::setCurrent(int index)
{
    if (index < 0 || index >= m_cards.size())
        return;
    const bool first = m_current < 0;
    m_current = index;
    // A card that has focus hands it to the chosen one (the ring, if showing, follows).
    for (TypeCard* card : std::as_const(m_cards)) {
        if (card->hasFocus() && card != m_cards.at(index)) {
            m_cards.at(index)->setFocus(card->isFocusVisible() ? Qt::TabFocusReason : Qt::OtherFocusReason);
            break;
        }
    }
    for (int i = 0; i < m_cards.size(); ++i)
        m_cards.at(i)->setChosen(i == index);
    moveIndicator(!first);
}

QRect TypeCardRow::indicatorTarget(int index) const
{
    const TypeCard* card = m_cards.value(index, nullptr);
    if (!card)
        return {};
    const QRect g = card->geometry();
    return { g.left() + Tokens::spaceLg, g.bottom() + 1 - kIndicatorHeight, qMax(0, g.width() - 2 * Tokens::spaceLg), kIndicatorHeight };
}

QRect TypeCardRow::indicatorGeometry() const
{
    return m_glide.state() == QAbstractAnimation::Running ? m_glide.endValue().toRect() : m_indicator->geometry();
}

void TypeCardRow::moveIndicator(bool animate)
{
    const QRect target = indicatorTarget(m_current);
    if (target.isEmpty())
        return;
    m_indicator->show();
    m_indicator->raise();
    const int ms = Motion::duration(Tokens::durBase);
    if (!animate || ms <= 0 || !isVisible() || m_indicator->geometry().isEmpty()) {
        m_glide.stop();
        m_indicator->setGeometry(target);
        return;
    }
    m_glide.stop();
    m_glide.setDuration(ms);
    m_glide.setStartValue(m_indicator->geometry());
    m_glide.setEndValue(target);
    m_glide.start();
}

void TypeCardRow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    moveIndicator(false);  // the layout already placed the cards
}

bool TypeCardRow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        const int step = key->key() == Qt::Key_Left ? -1 : key->key() == Qt::Key_Right ? 1 : 0;
        if (step != 0 && key->modifiers() == Qt::NoModifier) {
            const int index = m_cards.indexOf(static_cast<TypeCard*>(watched));
            const int next = index + step;
            if (index >= 0 && next >= 0 && next < m_cards.size()) {
                m_cards.at(next)->setFocus(Qt::TabFocusReason);
                setCurrent(next);
                emit chosen(next);
            }
            return true;
        }
    } else if (event->type() == QEvent::Move || event->type() == QEvent::Resize) {
        if (m_glide.state() != QAbstractAnimation::Running)
            moveIndicator(false);
    }
    return QWidget::eventFilter(watched, event);
}

}  // namespace Create
}  // namespace Zero
