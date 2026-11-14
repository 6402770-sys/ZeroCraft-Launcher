// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the filter popover.
#include "FilterPanel.h"

#include <QAbstractButton>
#include <QApplication>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <algorithm>
#include <utility>
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/pages/FlowLayout.h"

namespace Zero {
namespace Content {

namespace {

constexpr int kMargin = Tokens::shadowBlur + Tokens::shadowOffsetY;  ///< transparent margin that holds the shadow
constexpr int kPillHeight = 28;
constexpr int kSwitchRowHeight = 36;
constexpr int kSegmentHeight = 32;
constexpr const char* kFacetProp = "zeroFacetId";
constexpr const char* kValueProp = "zeroOptionValue";
constexpr int kValueRole = Qt::UserRole + 1;
constexpr int kDisabledOpacityPercent = 40;

QLabel* makeLabel(const QString& text, Role role, QWidget* parent, const char* tone = nullptr)
{
    auto* l = new QLabel(text, parent);
    Style::applyRole(l, role);
    if (tone)
        Style::setProperty(l, "zeroTone", tone);
    return l;
}

/// Shadow behind the panel: a plain panel shape carrying the popover shadow (mouse transparent).
class PopoverShadow : public QWidget {
   public:
    explicit PopoverShadow(QWidget* parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        auto* fx = new QGraphicsDropShadowEffect(this);
        fx->setBlurRadius(Tokens::shadowBlur);
        fx->setOffset(0, Tokens::shadowOffsetY);
        fx->setColor(QColor(0, 0, 0, Tokens::shadowAlpha));
        setGraphicsEffect(fx);
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::current().surfaceSolid());  // the popup is a window of its own: never see-through
        p.drawPath(Paint::panelPath(QRectF(rect()), Theme::current().radiusLg));
    }
};

class PopoverPanel : public QWidget {
   public:
    using QWidget::QWidget;

   protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(t.border, 1));
        p.setBrush(t.surfaceSolid());  // the popup is a window of its own: never see-through
        p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusLg));
    }
};

/// A checkable toggle chip (Multi options).
class PillButton : public QAbstractButton {
   public:
    PillButton(const QString& facetId, const FacetOption& option, QWidget* parent)
        : QAbstractButton(parent), m_hover(new Motion::StateAnimator(this)), m_check(new Motion::StateAnimator(this))
    {
        setCheckable(true);
        setText(option.label);
        setAccessibleName(option.label);
        setProperty(kFacetProp, facetId);
        setProperty(kValueProp, option.value);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        setFixedHeight(kPillHeight);
        connect(m_hover, &Motion::StateAnimator::progressChanged, this, qOverload<>(&QWidget::update));
        connect(m_check, &Motion::StateAnimator::progressChanged, this, qOverload<>(&QWidget::update));
        connect(this, &QAbstractButton::toggled, this, [this](bool on) { m_check->setActive(on); });
        connect(&Theme::instance(), &Theme::changed, this, [this]() {
            updateGeometry();
            update();
        });
    }

    QSize sizeHint() const override
    {
        const QFontMetrics fm(Style::font(Role::Label));
        return { Tokens::spaceMd * 2 + kIcon + Tokens::spaceXs + fm.horizontalAdvance(text()), kPillHeight };
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

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
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        const qreal hover = isEnabled() ? m_hover->progress() : 0.0;
        const qreal on = m_check->progress();
        QColor bg = mix(mix(t.elevated, t.higher, hover), mix(t.elevated, t.accent2, 0.16), on);
        if (isDown())
            bg = t.pressed;
        const QColor border = mix(mix(t.border, t.borderStrong, hover), t.accent2, on * 0.8);
        const QColor fg = mix(mix(t.textSecondary, t.text, hover), t.text, on);

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        if (!isEnabled())
            p.setOpacity(kDisabledOpacityPercent / 100.0);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setPen(QPen(border, 1));
        p.setBrush(bg);
        p.drawPath(Paint::panelPath(r, t.radiusMd));
        if (hasFocus()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(t.accent2, 2));
            p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(1, 1, -1, -1), t.radiusMd));
        }
        const QFont font = Style::font(Role::Label);
        const QFontMetrics fm(font);
        const qreal textW = fm.horizontalAdvance(text());
        const qreal group = textW + (kIcon + Tokens::spaceXs) * on;
        qreal x = (width() - group) / 2.0;
        if (on > 0.0) {
            p.save();
            p.setOpacity(p.opacity() * on);
            Icons::icon(QStringLiteral("check"), t.accent2).paint(&p, QRect(int(x), (height() - kIcon) / 2, kIcon, kIcon));
            p.restore();
            x += (kIcon + Tokens::spaceXs) * on;
        }
        p.setFont(font);
        p.setPen(fg);
        p.drawText(QRectF(x, 0, textW + 1, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
    }

   private:
    static constexpr int kIcon = 14;
    Motion::StateAnimator* m_hover;
    Motion::StateAnimator* m_check;
};

/// A switch row (Toggle facets, "Releases only"): label on the left, a switch on the right; the whole row toggles.
class SwitchRow : public QAbstractButton {
   public:
    SwitchRow(const QString& facetId, const QString& label, QWidget* parent)
        : QAbstractButton(parent), m_hover(new Motion::StateAnimator(this)), m_check(new Motion::StateAnimator(this))
    {
        setCheckable(true);
        setText(label);
        setAccessibleName(label);
        setProperty(kFacetProp, facetId);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        setFixedHeight(kSwitchRowHeight);
        connect(m_hover, &Motion::StateAnimator::progressChanged, this, qOverload<>(&QWidget::update));
        connect(m_check, &Motion::StateAnimator::progressChanged, this, qOverload<>(&QWidget::update));
        connect(this, &QAbstractButton::toggled, this, [this](bool on) { m_check->setActive(on); });
        connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    }
    QSize sizeHint() const override { return { 200, kSwitchRowHeight }; }

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
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        const qreal hover = isEnabled() ? m_hover->progress() : 0.0;
        const qreal on = m_check->progress();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        if (!isEnabled())
            p.setOpacity(kDisabledOpacityPercent / 100.0);
        const QRectF row = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        if (hover > 0.0 || hasFocus()) {
            p.setPen(Qt::NoPen);
            p.setBrush(withAlpha(t.higher, hover));
            p.drawPath(Paint::panelPath(row, t.radiusMd));
        }
        if (hasFocus()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(t.accent2, 2));
            p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(1, 1, -1, -1), t.radiusMd));
        }
        p.setFont(Style::font(Role::Body));
        p.setPen(t.text);
        p.drawText(QRect(Tokens::spaceSm, 0, width() - kTrackW - 3 * Tokens::spaceSm, height()), Qt::AlignVCenter | Qt::AlignLeft, text());

        const QRectF track(width() - Tokens::spaceSm - kTrackW, (height() - kTrackH) / 2.0, kTrackW, kTrackH);
        p.setPen(QPen(mix(t.borderStrong, t.accent, on), 1));
        p.setBrush(mix(t.bg, t.accent, on));
        p.drawPath(Paint::panelPath(track.adjusted(0.5, 0.5, -0.5, -0.5), kTrackH / 2.0));
        const qreal knob = kTrackH - 6;
        const qreal kx = track.left() + 3 + (track.width() - 6 - knob) * on;
        p.setPen(Qt::NoPen);
        p.setBrush(mix(t.textSecondary, t.onAccent, on));
        p.drawPath(Paint::panelPath(QRectF(kx, track.top() + 3, knob, knob), knob / 2.0));
    }

   private:
    static constexpr int kTrackW = 34;
    static constexpr int kTrackH = 18;
    Motion::StateAnimator* m_hover;
    Motion::StateAnimator* m_check;
};

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------

SegmentedControl::SegmentedControl(const QList<FacetOption>& options, QWidget* parent) : QWidget(parent), m_options(options)
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover, true);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(kSegmentHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

QSize SegmentedControl::sizeHint() const
{
    const QFontMetrics fm(Style::font(Role::Label));
    if (m_natural) {
        int sum = 0;
        for (int i = 0; i < m_options.size(); ++i)
            sum += naturalWidth(i);
        return { std::max(sum, 120), kSegmentHeight };
    }
    // segmentRect() gives every segment the same width, so the widest label decides (a sum truncated "Resource Packs").
    int widest = 0;
    for (const FacetOption& o : m_options)
        widest = std::max(widest, fm.horizontalAdvance(o.label));
    const int w = int(m_options.size()) * (widest + 2 * Tokens::spaceMd);
    return { std::max(w, 120), kSegmentHeight };
}

void SegmentedControl::setSelected(const QString& value)
{
    if (m_selected == value)
        return;
    m_selected = value;
    for (int i = 0; i < m_options.size(); ++i)
        if (m_options[i].value == value)
            m_focus = i;
    update();
}

void SegmentedControl::setOptions(const QList<FacetOption>& options)
{
    m_options = options;
    m_hover = -1;
    m_pressed = -1;
    int keep = -1;
    for (int i = 0; i < m_options.size(); ++i)
        if (m_options[i].value == m_selected)
            keep = i;
    if (keep < 0) {
        m_selected = m_options.isEmpty() ? QString() : m_options.first().value;
        keep = 0;
    }
    m_focus = keep;
    updateGeometry();
    update();
}

void SegmentedControl::setNaturalWidths(bool natural)
{
    if (m_natural == natural)
        return;
    m_natural = natural;
    updateGeometry();
    update();
}

int SegmentedControl::naturalWidth(int index) const
{
    const QFontMetrics fm(Style::font(Role::Label));
    return fm.horizontalAdvance(m_options.at(index).label) + 2 * Tokens::spaceMd;
}

QRect SegmentedControl::segmentRect(int index) const
{
    if (m_natural && !m_options.isEmpty()) {
        const int n = int(m_options.size());
        int sum = 0;
        for (int i = 0; i < n; ++i)
            sum += naturalWidth(i);
        // the width beyond (or short of) the natural sum is shared equally; the last segment takes the rounding rest
        const int extra = width() - sum;
        int x = 0;
        for (int i = 0; i < index; ++i)
            x += naturalWidth(i) + extra / n;
        const int w = index == n - 1 ? width() - x : naturalWidth(index) + extra / n;
        return QRect(x, 0, w, height());
    }
    const int n = std::max<int>(1, m_options.size());
    const int x0 = index * width() / n;
    const int x1 = (index + 1) * width() / n;
    return QRect(x0, 0, x1 - x0, height());
}

int SegmentedControl::indexAt(const QPoint& pos) const
{
    for (int i = 0; i < m_options.size(); ++i)
        if (segmentRect(i).contains(pos))
            return i;
    return -1;
}

void SegmentedControl::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (!isEnabled())
        p.setOpacity(kDisabledOpacityPercent / 100.0);
    const QRectF outer = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const QPainterPath shape = Paint::panelPath(outer, t.radiusMd);
    p.setPen(Qt::NoPen);
    p.setBrush(t.elevated);
    p.drawPath(shape);
    p.save();
    p.setClipPath(shape);
    const QFont font = Style::font(Role::Label);
    const QFontMetrics fm(font);
    for (int i = 0; i < m_options.size(); ++i) {
        const QRect seg = segmentRect(i);
        const bool on = m_options[i].value == m_selected;
        if (on) {
            p.setPen(Qt::NoPen);
            p.setBrush(mix(t.elevated, t.accent2, 0.22));
            p.drawRect(seg);
        } else if (i == m_hover && isEnabled()) {
            p.setPen(Qt::NoPen);
            p.setBrush(t.higher);
            p.drawRect(seg);
        }
        if (i > 0) {
            p.setPen(QPen(t.border, 1));
            p.drawLine(seg.left(), 6, seg.left(), height() - 7);
        }
        p.setFont(font);
        p.setPen(on ? t.text : t.textSecondary);
        p.drawText(seg.adjusted(Tokens::spaceSm, 0, -Tokens::spaceSm, 0), Qt::AlignCenter,
                   fm.elidedText(m_options[i].label, Qt::ElideRight, seg.width() - 2 * Tokens::spaceSm));
    }
    p.restore();
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(mix(t.border, t.accent2, hasFocus() ? 0.8 : 0.0), hasFocus() ? 2 : 1));
    p.drawPath(hasFocus() ? Paint::panelPath(QRectF(rect()).adjusted(1, 1, -1, -1), t.radiusMd) : shape);
    if (hasFocus() && m_focus >= 0 && m_focus < m_options.size()) {
        p.setPen(QPen(t.accent2, 1, Qt::DotLine));
        p.drawRect(segmentRect(m_focus).adjusted(3, 3, -3, -3));
    }
}

void SegmentedControl::mousePressEvent(QMouseEvent* event)
{
    m_pressed = indexAt(event->position().toPoint());
    if (m_pressed >= 0)
        m_focus = m_pressed;
    setFocus(Qt::MouseFocusReason);
    update();
}

void SegmentedControl::mouseReleaseEvent(QMouseEvent* event)
{
    const int i = indexAt(event->position().toPoint());
    const int pressed = std::exchange(m_pressed, -1);
    if (i >= 0 && i == pressed && onActivated)
        onActivated(m_options[i].value);
    update();
}

void SegmentedControl::mouseMoveEvent(QMouseEvent* event)
{
    const int i = indexAt(event->position().toPoint());
    if (i != m_hover) {
        m_hover = i;
        update();
    }
}

void SegmentedControl::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    m_hover = -1;
    update();
}

void SegmentedControl::focusInEvent(QFocusEvent* event)
{
    QWidget::focusInEvent(event);
    update();
}

void SegmentedControl::focusOutEvent(QFocusEvent* event)
{
    QWidget::focusOutEvent(event);
    update();
}

void SegmentedControl::keyPressEvent(QKeyEvent* event)
{
    const int n = int(m_options.size());
    switch (event->key()) {
        case Qt::Key_Left:
        case Qt::Key_Up:
            m_focus = (m_focus - 1 + n) % std::max(1, n);
            break;
        case Qt::Key_Right:
        case Qt::Key_Down:
            m_focus = (m_focus + 1) % std::max(1, n);
            break;
        case Qt::Key_Home:
            m_focus = 0;
            break;
        case Qt::Key_End:
            m_focus = std::max(0, n - 1);
            break;
        case Qt::Key_Space:
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (m_focus >= 0 && m_focus < n && onActivated)
                onActivated(m_options[m_focus].value);
            break;
        default:
            QWidget::keyPressEvent(event);
            return;
    }
    update();
}

// ---------------------------------------------------------------------------------------------------------------------

bool FilterPanel::isReleaseVersion(const QString& version)
{
    static const QRegularExpression re(QStringLiteral("^\\d+\\.\\d+(\\.\\d+)?$"));
    return re.match(version).hasMatch();
}

FilterPanel::FilterPanel(QWidget* parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint),
      m_shadow(new PopoverShadow(this)),
      m_panel(new PopoverPanel(this)),
      m_scroll(new QScrollArea(m_panel)),
      m_open(new QVariantAnimation(this))
{
    setObjectName(QStringLiteral("zeroFilterPanel"));
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoMouseReplay, true);  // a click on the Filters button closes the popup instead of reopening it
    setAccessibleName(tr("Filters"));

    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->viewport()->setAutoFillBackground(false);

    auto* footer = new QWidget(m_panel);
    auto* footerLay = new QHBoxLayout(footer);
    footerLay->setContentsMargins(Tokens::spaceLg, Tokens::spaceMd, Tokens::spaceLg, Tokens::spaceMd);
    footerLay->setSpacing(Tokens::spaceSm);
    auto* reset = new QPushButton(tr("Reset"), footer);
    Style::setProperty(reset, "zeroVariant", "ghost");
    m_result = makeLabel({}, Role::Caption, footer, "secondary");
    m_result->setAlignment(Qt::AlignCenter);
    m_result->setAccessibleName(tr("Results"));
    m_result->hide();
    auto* done = new QPushButton(tr("Done"), footer);
    Style::setProperty(done, "zeroVariant", "primary");
    footerLay->addWidget(reset);
    footerLay->addWidget(m_result, 1);
    footerLay->addStretch(0);
    footerLay->addWidget(done);
    m_reset = reset;
    m_done = done;
    connect(reset, &QPushButton::clicked, this, [this]() { edit(clearAll(m_state, m_defaults), true); });
    connect(done, &QPushButton::clicked, this, &QWidget::close);

    auto* rule = new QFrame(m_panel);
    rule->setFrameShape(QFrame::HLine);

    auto* panelLay = new QVBoxLayout(m_panel);
    panelLay->setContentsMargins(1, 1, 1, 1);
    panelLay->setSpacing(0);
    panelLay->addWidget(m_scroll, 1);
    panelLay->addWidget(rule);
    panelLay->addWidget(footer);

    m_applyTimer.setSingleShot(true);
    m_applyTimer.setInterval(kApplyDebounceMs);
    connect(&m_applyTimer, &QTimer::timeout, this, &FilterPanel::flush);

    m_open->setStartValue(0.0);
    m_open->setEndValue(1.0);
    connect(m_open, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        const qreal p = v.toReal();
        setWindowOpacity(p);
        layoutPanel(qRound((1.0 - p) * kRisePx));
    });
    connect(&Theme::instance(), &Theme::changed, this, [this]() { update(); });
}

const Facet* FilterPanel::facetById(const QString& id) const
{
    for (const Facet& f : m_facets)
        if (f.id == id)
            return &f;
    return nullptr;
}

void FilterPanel::setFacets(const QList<Facet>& facets)
{
    m_facets = facets;
    rebuild();
    syncWidgets();
}

void FilterPanel::setDefaults(const FilterState& defaults)
{
    m_defaults = defaults;
    syncWidgets();
}

void FilterPanel::setState(const FilterState& state)
{
    m_applyTimer.stop();
    m_dirty = false;
    m_state = state;
    syncWidgets();
}

void FilterPanel::setResultText(const QString& text)
{
    m_result->setText(text);
    m_result->setVisible(!text.isEmpty());
}

void FilterPanel::setReleasesOnly(bool on)
{
    if (m_releasesOnly == on)
        return;
    m_releasesOnly = on;
    syncWidgets();
}

void FilterPanel::rebuild()
{
    m_pills.clear();
    m_segments.clear();
    m_switches.clear();
    m_groups.clear();
    m_reasons.clear();
    m_lists.clear();
    m_releasesSwitch = nullptr;
    if (m_content)
        m_content->deleteLater();
    m_content = new QWidget;
    m_content->setAutoFillBackground(false);
    auto* lay = new QVBoxLayout(m_content);
    lay->setContentsMargins(Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg);
    lay->setSpacing(Tokens::spaceSm);

    bool first = true;
    bool statusHeader = false;
    bool previousToggle = false;
    for (const Facet& f : m_facets) {
        const bool toggle = f.kind == FacetKind::Toggle;
        if (!first && !(toggle && previousToggle))  // switches sit close together under one header
            lay->addSpacing(Tokens::spaceMd);
        first = false;
        previousToggle = toggle;
        if (toggle) {
            if (!statusHeader) {
                lay->addWidget(makeLabel(tr("Status"), Role::Overline, m_content));
                statusHeader = true;
            }
            auto* row = new SwitchRow(f.id, f.label, m_content);
            row->installEventFilter(this);
            row->setEnabled(f.isEnabled());
            if (!f.isEnabled())
                row->setToolTip(f.disabledReason);
            connect(row, &QAbstractButton::clicked, this, [this, id = f.id]() {
                if (m_syncing)
                    return;
                if (const Facet* facet = facetById(id))
                    userToggled(*facet, kToggleOn);
            });
            m_switches.insert(f.id, row);
            m_groups.insert(f.id, row);
            lay->addWidget(row);
            continue;
        }
        auto* header = makeLabel(f.label, Role::Overline, m_content);
        lay->addWidget(header);
        if (!f.isEnabled()) {
            header->setToolTip(f.disabledReason);
            auto* reason = makeLabel(f.disabledReason, Role::Caption, m_content, "tertiary");
            reason->setWordWrap(true);
            reason->setToolTip(f.disabledReason);
            m_reasons.insert(f.id, reason);
            lay->addWidget(reason);
        }
        QWidget* group = nullptr;
        if (f.kind == FacetKind::Single)
            group = buildSegmented(f, m_content);
        else if (f.id == FacetId::mcVersion)
            group = buildVersionList(f, m_content);
        else
            group = buildPills(f, m_content);
        group->setEnabled(f.isEnabled());
        if (!f.isEnabled())
            group->setToolTip(f.disabledReason);
        m_groups.insert(f.id, group);
        lay->addWidget(group);
    }
    lay->addStretch(1);
    m_scroll->setWidget(m_content);

    // Tab order: the groups in reading order, then the footer. A pill group is one stop (roving focus inside).
    QWidget* previous = nullptr;
    auto chain = [&previous](QWidget* w) {
        if (w && w->focusPolicy() != Qt::NoFocus && previous != w) {
            if (previous)
                QWidget::setTabOrder(previous, w);
            previous = w;
        }
    };
    for (const Facet& f : m_facets) {
        const auto pills = m_pills.value(f.id);
        for (auto* pill : pills)
            chain(pill);
        chain(m_segments.value(f.id));
        chain(m_switches.value(f.id));
        for (const ListEntry& e : m_lists)
            if (e.facetId == f.id) {
                chain(m_releasesSwitch);
                chain(e.filter);
                chain(e.list);
            }
    }
    chain(m_reset);
    chain(m_done);
}

QWidget* FilterPanel::buildPills(const Facet& facet, QWidget* parent)
{
    auto* box = new QWidget(parent);
    auto* outer = new QVBoxLayout(box);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(Tokens::spaceXs);
    QList<QAbstractButton*> pills;
    QWidget* groupBox = nullptr;
    FlowLayout* flow = nullptr;
    QString group;
    bool started = false;
    for (const FacetOption& o : facet.options) {
        if (!started || o.group != group) {  // a sub-heading per option group (provider category headers)
            started = true;
            group = o.group;
            if (!group.isEmpty())
                outer->addWidget(makeLabel(group, Role::Caption, box, "tertiary"));
            groupBox = new QWidget(box);
            flow = new FlowLayout(groupBox, Tokens::spaceSm - 2, Tokens::spaceSm - 2);
            outer->addWidget(groupBox);
        }
        auto* pill = new PillButton(facet.id, o, groupBox);
        pill->setFocusPolicy(Qt::ClickFocus);
        pill->installEventFilter(this);
        connect(pill, &QAbstractButton::clicked, this, [this, pill, fid = facet.id, value = o.value]() {
            if (m_syncing)
                return;
            updateRoving(fid, pill);
            if (const Facet* f = facetById(fid))
                userToggled(*f, value);
        });
        flow->addWidget(pill);
        pills << pill;
    }
    m_pills.insert(facet.id, pills);
    if (!pills.isEmpty())
        pills.first()->setFocusPolicy(Qt::StrongFocus);  // one tab stop per group
    return box;
}

QWidget* FilterPanel::buildSegmented(const Facet& facet, QWidget* parent)
{
    auto* seg = new SegmentedControl(facet.options, parent);
    seg->setAccessibleName(facet.label);
    seg->onActivated = [this, id = facet.id](const QString& value) {
        if (m_syncing)
            return;
        if (const Facet* f = facetById(id))
            userToggled(*f, value);
    };
    m_segments.insert(facet.id, seg);
    return seg;
}

QWidget* FilterPanel::buildVersionList(const Facet& facet, QWidget* parent)
{
    auto* box = new QWidget(parent);
    auto* lay = new QVBoxLayout(box);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(Tokens::spaceXs);

    auto* releases = new SwitchRow(facet.id + QStringLiteral(".releases"), tr("Releases only"), box);
    releases->installEventFilter(this);
    connect(releases, &QAbstractButton::clicked, this, [this, releases]() {
        if (m_syncing)
            return;
        m_releasesOnly = releases->isChecked();
        syncWidgets();
        emit releasesOnlyChanged(m_releasesOnly);
    });
    m_releasesSwitch = releases;
    lay->addWidget(releases);

    auto* filter = new QLineEdit(box);
    // "Filter Minecraft version": the proper noun keeps its capital; other labels start lowercase after "Filter".
    QString what = facet.label;
    if (what.size() > 1 && what.at(0).isUpper() && what.at(1).isLower() && !what.startsWith(QLatin1String("Minecraft")))
        what[0] = what.at(0).toLower();
    filter->setPlaceholderText(tr("Filter %1").arg(what));
    filter->setClearButtonEnabled(true);
    filter->addAction(Icons::icon(QStringLiteral("search"), Theme::current().textTertiary), QLineEdit::LeadingPosition);
    filter->setAccessibleName(tr("Filter %1").arg(facet.label));
    lay->addWidget(filter);

    auto* list = new QListWidget(box);
    list->setAccessibleName(facet.label);
    list->setUniformItemSizes(true);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    for (const FacetOption& o : facet.options) {
        auto* item = new QListWidgetItem(o.label, list);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);  // the check is drawn, the panel toggles it
        item->setCheckState(Qt::Unchecked);
        item->setData(kValueRole, o.value);
        item->setSizeHint(QSize(0, kListRowHeight));
    }
    list->setFixedHeight(kListRows * kListRowHeight + 2);
    lay->addWidget(list);
    list->installEventFilter(this);
    connect(list, &QListWidget::itemClicked, this, [this, fid = facet.id](QListWidgetItem* item) {
        if (m_syncing)
            return;
        if (const Facet* f = facetById(fid))
            userToggled(*f, item->data(kValueRole).toString());
    });

    const ListEntry entry{ facet.id, list, filter };
    m_lists << entry;
    connect(filter, &QLineEdit::textChanged, this, [this, entry]() { applyListFilter(entry); });
    return box;
}

void FilterPanel::applyListFilter(const ListEntry& entry)
{
    const QString needle = entry.filter->text().trimmed();
    const QStringList selected = m_state.values(entry.facetId);
    for (int i = 0; i < entry.list->count(); ++i) {
        QListWidgetItem* item = entry.list->item(i);
        const QString value = item->data(kValueRole).toString();
        const bool keep = selected.contains(value);  // a selected version always stays listed
        const bool releaseOk = !m_releasesOnly || isReleaseVersion(value) || keep;
        const bool matches = needle.isEmpty() || item->text().contains(needle, Qt::CaseInsensitive);
        item->setHidden(!(releaseOk && matches));
    }
}

void FilterPanel::userToggled(const Facet& facet, const QString& value)
{
    FilterState next = m_state;
    QStringList vals = next.selected.value(facet.id);
    switch (facet.kind) {
        case FacetKind::Multi:
            if (!vals.removeOne(value))
                vals << value;
            break;
        case FacetKind::Single: {
            const bool required = !m_defaults.values(facet.id).isEmpty();  // a facet with a default value cannot be emptied
            if (vals.contains(value) && !required)
                vals.clear();
            else
                vals = QStringList{ value };
            break;
        }
        case FacetKind::Toggle:
            vals = vals.isEmpty() ? QStringList{ kToggleOn } : QStringList();
            break;
    }
    if (vals.isEmpty())
        next.selected.remove(facet.id);
    else
        next.selected[facet.id] = vals;
    edit(next, false);
}

void FilterPanel::edit(const FilterState& next, bool immediate)
{
    const bool changed = next != m_state;
    m_state = next;
    syncWidgets();
    if (immediate) {
        m_applyTimer.stop();
        m_dirty = false;
        if (changed)
            emit stateChanged(m_state);
    } else if (changed) {
        m_dirty = true;
        m_applyTimer.start();
    }
}

void FilterPanel::flush()
{
    m_applyTimer.stop();
    if (!m_dirty)
        return;
    m_dirty = false;
    emit stateChanged(m_state);
}

void FilterPanel::syncWidgets()
{
    const bool wasSyncing = std::exchange(m_syncing, true);
    for (auto it = m_pills.cbegin(); it != m_pills.cend(); ++it) {
        const QStringList sel = m_state.values(it.key());
        for (auto* pill : it.value())
            pill->setChecked(sel.contains(pill->property(kValueProp).toString()));
    }
    for (auto it = m_segments.cbegin(); it != m_segments.cend(); ++it) {
        QStringList sel = m_state.values(it.key());
        if (sel.isEmpty())
            sel = m_defaults.values(it.key());  // the effective value of a facet that always has one
        it.value()->setSelected(sel.value(0));
    }
    for (auto it = m_switches.cbegin(); it != m_switches.cend(); ++it)
        it.value()->setChecked(m_state.isSelected(it.key(), kToggleOn));
    if (m_releasesSwitch)
        m_releasesSwitch->setChecked(m_releasesOnly);
    for (const ListEntry& e : m_lists) {
        const QStringList sel = m_state.values(e.facetId);
        for (int i = 0; i < e.list->count(); ++i) {
            QListWidgetItem* item = e.list->item(i);
            item->setCheckState(sel.contains(item->data(kValueRole).toString()) ? Qt::Checked : Qt::Unchecked);
        }
        applyListFilter(e);
    }
    m_syncing = wasSyncing;
}

void FilterPanel::updateRoving(const QString& facetId, QAbstractButton* current)
{
    for (auto* pill : m_pills.value(facetId))
        pill->setFocusPolicy(pill == current ? Qt::StrongFocus : Qt::ClickFocus);
}

QList<QAbstractButton*> FilterPanel::pillsOf(const QString& facetId) const
{
    return m_pills.value(facetId);
}

SegmentedControl* FilterPanel::segmentedOf(const QString& facetId) const
{
    return m_segments.value(facetId);
}

QAbstractButton* FilterPanel::switchOf(const QString& facetId) const
{
    return m_switches.value(facetId);
}

QListWidget* FilterPanel::listOf(const QString& facetId) const
{
    for (const ListEntry& e : m_lists)
        if (e.facetId == facetId)
            return e.list;
    return nullptr;
}

QLineEdit* FilterPanel::listFilterOf(const QString& facetId) const
{
    for (const ListEntry& e : m_lists)
        if (e.facetId == facetId)
            return e.filter;
    return nullptr;
}

void FilterPanel::layoutPanel(int riseOffset)
{
    const QRect panel(kMargin, kMargin - Tokens::shadowOffsetY + riseOffset, width() - 2 * kMargin, height() - 2 * kMargin);
    m_panel->setGeometry(panel);
    m_shadow->setGeometry(panel);
    m_shadow->lower();
}

void FilterPanel::place()
{
    if (!m_anchor)
        return;
    const QPoint below = m_anchor->mapToGlobal(QPoint(0, m_anchor->height() + Tokens::spaceXs));
    QScreen* screen = m_anchor->screen();
    const QRect avail = screen ? screen->availableGeometry() : QRect(below, QSize(1920, 1080));

    // Height: the content at the panel's width, at most 70 % of the window (and of the screen).
    const int innerWidth = kWidth - 2;
    m_content->ensurePolished();
    const int contentH = m_content->layout()->hasHeightForWidth() ? m_content->layout()->heightForWidth(innerWidth) : m_content->sizeHint().height();
    const int footerH = m_panel->layout()->itemAt(1)->sizeHint().height() + m_panel->layout()->itemAt(2)->sizeHint().height();
    const QWidget* window = m_anchor->window();
    const int basis = window && window->height() > 0 ? window->height() : avail.height();
    const int maxH = std::min(int(basis * 0.7), avail.height() - 2 * Tokens::spaceSm);
    const int panelH = std::min(contentH + footerH + 2, maxH);

    QPoint pos = below;
    pos.setX(std::clamp(pos.x(), avail.left() + Tokens::spaceSm, std::max(avail.left() + Tokens::spaceSm, avail.right() - kWidth - Tokens::spaceSm)));
    if (pos.y() + panelH > avail.bottom() - Tokens::spaceSm)  // no room below: open above the anchor
        pos.setY(std::max(avail.top() + Tokens::spaceSm, m_anchor->mapToGlobal(QPoint(0, 0)).y() - Tokens::spaceXs - panelH));
    setGeometry(pos.x() - kMargin, pos.y() - kMargin + Tokens::shadowOffsetY, kWidth + 2 * kMargin, panelH + 2 * kMargin);
    layoutPanel(0);
}

void FilterPanel::toggle(QWidget* anchor)
{
    if (isVisible())
        close();
    else
        popup(anchor);
}

void FilterPanel::popup(QWidget* anchor)
{
    if (m_facets.isEmpty() || !anchor)
        return;
    m_anchor = anchor;
    if (!m_content)
        rebuild();
    place();
    m_open->stop();
    const int ms = Motion::duration(kOpenMs);
    if (ms > 0) {
        setWindowOpacity(0.0);
        layoutPanel(kRisePx);
    } else {
        setWindowOpacity(1.0);
    }
    show();
    raise();
    activateWindow();
    if (ms > 0) {
        m_open->setDuration(ms);
        m_open->setEasingCurve(Motion::easeEnter());
        m_open->start();
    }
    // The first control in tab order.
    QWidget* firstControl = nullptr;
    for (const Facet& f : m_facets) {
        if (!f.isEnabled())
            continue;
        for (auto* pill : m_pills.value(f.id))
            if (pill->focusPolicy() == Qt::StrongFocus) {
                firstControl = pill;
                break;
            }
        if (!firstControl)
            firstControl = m_segments.value(f.id);
        if (!firstControl)
            firstControl = m_switches.value(f.id);
        if (!firstControl)
            firstControl = listFilterOf(f.id);
        if (firstControl)
            break;
    }
    (firstControl ? firstControl : static_cast<QWidget*>(m_done))->setFocus(Qt::PopupFocusReason);
}

void FilterPanel::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    m_open->stop();
    setWindowOpacity(1.0);
    flush();  // what was clicked just before closing still applies
    if (m_anchor)
        m_anchor->setFocus(Qt::PopupFocusReason);
    emit closed();
}

void FilterPanel::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool FilterPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (auto* button = qobject_cast<QAbstractButton*>(watched); button && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
            button->click();
            return true;
        }
        if (auto* pill = dynamic_cast<PillButton*>(watched)) {
            const QString fid = pill->property(kFacetProp).toString();
            const auto pills = m_pills.value(fid);
            const int n = int(pills.size());
            const int i = int(pills.indexOf(pill));
            int next = -1;
            switch (key->key()) {
                case Qt::Key_Left:
                case Qt::Key_Up:
                    next = (i - 1 + n) % n;
                    break;
                case Qt::Key_Right:
                case Qt::Key_Down:
                    next = (i + 1) % n;
                    break;
                case Qt::Key_Home:
                    next = 0;
                    break;
                case Qt::Key_End:
                    next = n - 1;
                    break;
                default:
                    break;
            }
            if (next >= 0) {
                updateRoving(fid, pills[next]);
                pills[next]->setFocus(Qt::OtherFocusReason);
                m_scroll->ensureWidgetVisible(pills[next]);
                return true;
            }
        }
        if (auto* list = qobject_cast<QListWidget*>(watched);
            list && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter || key->key() == Qt::Key_Space)) {
            if (QListWidgetItem* item = list->currentItem(); item && !item->isHidden()) {
                for (const ListEntry& e : m_lists)
                    if (e.list == list)
                        if (const Facet* f = facetById(e.facetId))
                            userToggled(*f, item->data(kValueRole).toString());
            }
            return true;
        }
    }
    if (event->type() == QEvent::FocusIn) {
        if (auto* w = qobject_cast<QWidget*>(watched); w && m_content && m_content->isAncestorOf(w))
            m_scroll->ensureWidgetVisible(w, Tokens::spaceSm, Tokens::spaceSm);
    }
    return QWidget::eventFilter(watched, event);
}

}  // namespace Content
}  // namespace Zero
