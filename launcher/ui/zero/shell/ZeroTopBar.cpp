// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroTopBar.h"

#include <QFocusEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QHelpEvent>
#include <QPainter>
#include <QToolTip>
#include "AccountChip.h"
#include "ShellPage.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroTransition.h"

namespace Zero {

namespace {
constexpr int kTitleGap = 10;  // title to subtitle
constexpr int kCrumbGap = 6;   // crumb to chevron to title
constexpr int kChevron = 12;
constexpr int kMinTitleWidth = 120;
}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// ChromeTitle
// ---------------------------------------------------------------------------------------------------------------------

struct ChromeTitle::Parts {
    int baseline = 0;
    QRect crumb;         // hit rectangle of the crumb text (empty = none)
    QPoint crumbOrigin;  // text origin (baseline left)
    QRect chevron;
    QPoint titleOrigin;
    QString titleText;  // possibly elided
    QPoint subtitleOrigin;
    QString subtitleText;  // possibly elided, empty = not drawn
};

ChromeTitle::ChromeTitle(QWidget* parent) : QWidget(parent), m_hover(new Motion::StateAnimator(this))
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setMouseTracking(true);
    connect(m_hover, &Motion::StateAnimator::progressChanged, this, qOverload<>(&QWidget::update));
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        updateGeometry();
        update();
    });
}

void ChromeTitle::setContent(const QString& crumb, const QString& title, const QString& subtitle)
{
    m_crumb = crumb;
    m_title = title;
    m_subtitle = subtitle;
    setFocusPolicy(crumb.isEmpty() ? Qt::NoFocus : Qt::StrongFocus);
    setCursor(Qt::ArrowCursor);
    m_hover->setActive(false);
    refreshAccessibility();
    updateGeometry();
    update();
}

void ChromeTitle::setSubtitle(const QString& subtitle)
{
    if (m_subtitle == subtitle)
        return;
    m_subtitle = subtitle;
    refreshAccessibility();
    updateGeometry();
    update();
}

void ChromeTitle::refreshAccessibility()
{
    setAccessibleName(m_crumb.isEmpty() ? m_title : tr("%1, back to %2").arg(m_title, m_crumb));
    setAccessibleDescription(m_subtitle);
}

ChromeTitle::Parts ChromeTitle::layoutParts() const
{
    Parts parts;
    const QFont titleFont = Style::font(Role::Title);
    const QFont captionFont = Style::font(Role::Caption);
    const QFontMetrics tm(titleFont);
    const QFontMetrics cm(captionFont);
    parts.baseline = (height() - tm.height()) / 2 + tm.ascent();

    int x = 0;
    if (!m_crumb.isEmpty()) {
        const int cw = tm.horizontalAdvance(m_crumb);
        parts.crumbOrigin = QPoint(x, parts.baseline);
        parts.crumb = QRect(x - 4, parts.baseline - tm.ascent() - 2, cw + 8, tm.height() + 4);
        x += cw + kCrumbGap;
        parts.chevron = QRect(x, parts.baseline - (tm.ascent() + kChevron) / 2 + 1, kChevron, kChevron);
        x += kChevron + kCrumbGap;
    }

    const int avail = qMax(kMinTitleWidth, width()) - x;
    const int fullTitle = tm.horizontalAdvance(m_title);
    const int fullSub = m_subtitle.isEmpty() ? 0 : cm.horizontalAdvance(m_subtitle);
    int titleW = fullTitle;
    int subW = fullSub;
    if (fullTitle + (fullSub ? kTitleGap + fullSub : 0) > avail) {
        // the subtitle gives way first, then the title
        subW = fullSub ? qMax(0, avail - fullTitle - kTitleGap) : 0;
        if (subW < 48)
            subW = 0;
        titleW = qMin(fullTitle, avail - (subW ? kTitleGap + subW : 0));
    }
    parts.titleOrigin = QPoint(x, parts.baseline);
    parts.titleText = tm.elidedText(m_title, Qt::ElideRight, qMax(0, titleW));
    if (subW > 0) {
        parts.subtitleOrigin = QPoint(x + titleW + kTitleGap, parts.baseline);
        parts.subtitleText = cm.elidedText(m_subtitle, Qt::ElideRight, subW);
    }
    return parts;
}

QRect ChromeTitle::crumbRect() const
{
    return m_crumb.isEmpty() ? QRect() : layoutParts().crumb;
}

QSize ChromeTitle::sizeHint() const
{
    const QFontMetrics tm(Style::font(Role::Title));
    const QFontMetrics cm(Style::font(Role::Caption));
    int w = tm.horizontalAdvance(m_title);
    if (!m_crumb.isEmpty())
        w += tm.horizontalAdvance(m_crumb) + 2 * kCrumbGap + kChevron;
    if (!m_subtitle.isEmpty())
        w += kTitleGap + cm.horizontalAdvance(m_subtitle);
    return { w, tm.height() };
}

QSize ChromeTitle::minimumSizeHint() const
{
    return { kMinTitleWidth, QFontMetrics(Style::font(Role::Title)).height() };
}

void ChromeTitle::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    const Parts parts = layoutParts();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QFont titleFont = Style::font(Role::Title);

    if (!m_crumb.isEmpty()) {
        const qreal h = qMax(m_hover->progress(), m_pressed ? 1.0 : 0.0);
        p.setFont(titleFont);
        p.setPen(mix(t.textSecondary, t.text, h));
        p.drawText(parts.crumbOrigin, m_crumb);
        Icons::icon(QStringLiteral("chevron-right"), t.textTertiary).paint(&p, parts.chevron);
        if (m_keyboardFocus && hasFocus()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(t.accent2, 1));
            p.drawPath(Paint::panelPath(QRectF(parts.crumb).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusSm));
        }
    }

    p.setFont(titleFont);
    p.setPen(t.text);
    p.drawText(parts.titleOrigin, parts.titleText);

    if (!parts.subtitleText.isEmpty()) {
        p.setFont(Style::font(Role::Caption));
        p.setPen(t.textSecondary);
        p.drawText(parts.subtitleOrigin, parts.subtitleText);
    }
}

bool ChromeTitle::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        // an elided subtitle shows in full
        const Parts parts = layoutParts();
        if (!m_subtitle.isEmpty() && parts.subtitleText != m_subtitle)
            QToolTip::showText(static_cast<QHelpEvent*>(event)->globalPos(), m_subtitle, this);
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(event);
}

void ChromeTitle::mouseMoveEvent(QMouseEvent* event)
{
    const bool over = !m_crumb.isEmpty() && crumbRect().contains(event->position().toPoint());
    m_hover->setActive(over);
    setCursor(over ? Qt::PointingHandCursor : Qt::ArrowCursor);
    QWidget::mouseMoveEvent(event);
}

void ChromeTitle::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !m_crumb.isEmpty() && crumbRect().contains(event->position().toPoint())) {
        m_pressed = true;
        update();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ChromeTitle::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_pressed && event->button() == Qt::LeftButton) {
        m_pressed = false;
        update();
        if (crumbRect().contains(event->position().toPoint()))
            emit crumbClicked();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ChromeTitle::leaveEvent(QEvent* event)
{
    m_hover->setActive(false);
    setCursor(Qt::ArrowCursor);
    QWidget::leaveEvent(event);
}

void ChromeTitle::keyPressEvent(QKeyEvent* event)
{
    if (!m_crumb.isEmpty() && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space)) {
        emit crumbClicked();
        return;
    }
    QWidget::keyPressEvent(event);
}

void ChromeTitle::focusInEvent(QFocusEvent* event)
{
    m_keyboardFocus = event->reason() == Qt::TabFocusReason || event->reason() == Qt::BacktabFocusReason;
    QWidget::focusInEvent(event);
    update();
}

void ChromeTitle::focusOutEvent(QFocusEvent* event)
{
    m_keyboardFocus = false;
    QWidget::focusOutEvent(event);
    update();
}

// ---------------------------------------------------------------------------------------------------------------------
// ZeroTopBar
// ---------------------------------------------------------------------------------------------------------------------

/// 1 x 24 px vertical rule in the `border` color.
class TopBarDivider : public QWidget {
   public:
    explicit TopBarDivider(QWidget* parent) : QWidget(parent)
    {
        setFixedSize(1, 24);
        connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    }

   protected:
    void paintEvent(QPaintEvent*) override { QPainter(this).fillRect(rect(), Theme::current().border); }
};

ZeroTopBar::ZeroTopBar(QWidget* parent)
    : QWidget(parent),
      m_titleBlock(new ChromeTitle(this)),
      m_centerHost(new QWidget(this)),
      m_actionsHost(new QWidget(this)),
      m_actionsLayout(new QHBoxLayout(m_actionsHost)),
      m_divider(new TopBarDivider(this)),
      m_chip(new AccountChip(this))
{
    setFixedHeight(kHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* centerLayout = new QHBoxLayout(m_centerHost);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    m_centerHost->hide();  // nothing until a centre widget is set

    m_actionsLayout->setContentsMargins(0, 0, 0, 0);
    m_actionsLayout->setSpacing(Tokens::spaceSm);
    m_actionsHost->hide();
    m_divider->hide();

    // The bottom pixel row belongs to the border line.
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(Tokens::spaceXl, 0, Tokens::spaceXl, 1);
    row->setSpacing(Tokens::spaceLg);
    row->addWidget(m_titleBlock, 1);
    row->addWidget(m_centerHost, 0);
    row->addWidget(m_actionsHost, 0);
    row->addWidget(m_divider, 0, Qt::AlignVCenter);
    row->addWidget(m_chip, 0, Qt::AlignVCenter);

    connect(m_titleBlock, &ChromeTitle::crumbClicked, this, &ZeroTopBar::breadcrumbClicked);
    connect(m_chip, &QAbstractButton::clicked, this,
            [this]() { emit accountMenuRequested(m_chip->mapToGlobal(QPoint(m_chip->width(), m_chip->height()))); });
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

bool ZeroTopBar::dividerVisible() const
{
    return !m_divider->isHidden();
}

void ZeroTopBar::paintEvent(QPaintEvent*)
{
    QPainter(this).fillRect(QRect(0, height() - 1, width(), 1), Theme::current().border);
}

void ZeroTopBar::setCenterWidget(QWidget* widget)
{
    if (m_center)
        delete m_center.data();
    m_center = widget;
    if (widget) {
        widget->setParent(m_centerHost);
        m_centerHost->layout()->addWidget(widget);
        widget->show();
    }
    m_centerHost->setVisible(widget != nullptr);
}

void ZeroTopBar::showPage(ShellPage* page, bool animate)
{
    if (m_page)
        disconnect(m_page, nullptr, this, nullptr);
    m_page = page;
    if (!page)
        return;
    connect(page, &ShellPage::chromeChanged, this, &ZeroTopBar::refresh);
    const Transition::Snapshot before = animate ? Transition::capture(this) : Transition::Snapshot();
    apply(page);
    if (!before.isNull()) {
        layout()->activate();
        Transition::run(this, before, Transition::Kind::Crossfade, Tokens::durFast);
    }
}

void ZeroTopBar::refresh()
{
    ShellPage* page = m_page;
    if (!page)
        return;
    const bool structural = page->title() != m_titleBlock->title() || page->breadcrumbParent() != m_titleBlock->crumb() ||
                            page->topBarActions() != m_actions.data();
    if (!structural) {
        m_titleBlock->setSubtitle(page->subtitle());  // live text (instance count): no transition
        return;
    }
    const Transition::Snapshot before = Transition::capture(this);
    apply(page);
    if (!before.isNull()) {
        layout()->activate();
        Transition::run(this, before, Transition::Kind::Crossfade, Tokens::durFast);
    }
}

void ZeroTopBar::apply(ShellPage* page)
{
    m_titleBlock->setContent(page->breadcrumbParent(), page->title(), page->subtitle());

    QWidget* wanted = page->topBarActions();
    if (wanted != m_actions.data()) {
        releaseActions();
        if (wanted) {
            m_actions = wanted;
            m_actionsOwner = page;
            wanted->setParent(m_actionsHost);
            m_actionsLayout->addWidget(wanted);
            wanted->show();
        }
    }
    m_actionsHost->setVisible(m_actions != nullptr);
    m_divider->setVisible(m_actions != nullptr);
}

void ZeroTopBar::releaseActions()
{
    if (!m_actions)
        return;
    QWidget* w = m_actions;
    m_actionsLayout->removeWidget(w);
    w->hide();
    w->setParent(m_actionsOwner ? static_cast<QWidget*>(m_actionsOwner.data()) : nullptr);  // back to its page, hidden
    if (!m_actionsOwner)
        w->deleteLater();
    m_actions = nullptr;
    m_actionsOwner = nullptr;
}

}  // namespace Zero
