// SPDX-License-Identifier: GPL-3.0-only
#include "PlaytimeView.h"

#include <QHBoxLayout>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedLayout>
#include <QToolTip>
#include <QVBoxLayout>
#include <algorithm>
#include "Application.h"
#include "BaseInstance.h"
#include "ElidedLabel.h"
#include "IconTile.h"
#include "InstanceList.h"
#include "icons/IconList.h"
#include "ui/zero/ZeroCard.h"
#include "ui/zero/ZeroEmptyState.h"
#include "ui/zero/ZeroFormat.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "zero/history/SessionHistory.h"

namespace Zero {

using namespace History;
using namespace History::Playtime;

namespace {

constexpr int kChartHeight = 180;
constexpr int kTopInstances = 10;
constexpr int kGutter = 52;      ///< right column of the chart with the gridline labels
constexpr int kAxisHeight = 22;  ///< weekday initials
constexpr int kPlotTop = 8;
// NOTE: the theme has no "subtle accent" token (statusTint, 12 %, is invisible on a bar): other days use the accent at reduced opacity.
constexpr double kBarAlpha = 0.55;

QLabel* makeLabel(const QString& text, Role role, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    Style::applyRole(label, role);
    return label;
}

void useSecondaryColor(QLabel* label)
{
    auto apply = [label]() {
        QPalette pal = label->palette();
        pal.setColor(QPalette::WindowText, Theme::current().textSecondary);
        label->setPalette(pal);
    };
    apply();
    QObject::connect(&Theme::instance(), &Theme::changed, label, apply);
}

QLabel* makeSecondary(const QString& text, Role role, QWidget* parent)
{
    auto* label = makeLabel(text, role, parent);
    useSecondaryColor(label);
    return label;
}

ElidedLabel* makeElided(const QString& text, Role role, QWidget* parent)
{
    auto* label = new ElidedLabel(parent);
    Style::applyRole(label, role);
    label->setFullText(text);
    return label;
}

QString sessionsText(int n)
{
    return n == 1 ? QObject::tr("1 session") : QObject::tr("%1 sessions").arg(n);
}

/// Thin share bar: `border` track, `accent` fill.
class ShareBar : public QWidget {
   public:
    ShareBar(double ratio, QWidget* parent) : QWidget(parent), m_ratio(std::clamp(ratio, 0.0, 1.0))
    {
        setFixedHeight(Tokens::progressHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        const double r = height() / 2.0;
        p.setBrush(t.border);
        p.drawRoundedRect(QRectF(rect()), r, r);
        if (m_ratio > 0) {
            p.setBrush(t.accent);
            p.drawRoundedRect(QRectF(0, 0, std::max<double>(height(), width() * m_ratio), height()), r, r);
        }
    }

   private:
    double m_ratio;
};

/// A clickable row with a hover / focus background.
class ClickRow : public QWidget {
    Q_OBJECT
   public:
    explicit ClickRow(QWidget* parent) : QWidget(parent)
    {
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    }

   signals:
    void activated();

   protected:
    void paintEvent(QPaintEvent*) override
    {
        if (!underMouse() && !hasFocus())
            return;
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(hasFocus() ? QPen(t.borderStrong, 1) : Qt::NoPen);
        p.setBrush(t.surfaceHover);
        p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusMd));
    }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
            emit activated();
    }
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space)
            emit activated();
        else
            QWidget::keyPressEvent(event);
    }
    void enterEvent(QEnterEvent* event) override
    {
        QWidget::enterEvent(event);
        update();
    }
    void leaveEvent(QEvent* event) override
    {
        QWidget::leaveEvent(event);
        update();
    }
    void focusInEvent(QFocusEvent* event) override
    {
        QWidget::focusInEvent(event);
        update();
    }
    void focusOutEvent(QFocusEvent* event) override
    {
        QWidget::focusOutEvent(event);
        update();
    }
};

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// PlaytimeChart
// ---------------------------------------------------------------------------------------------------------------------

PlaytimeChart::PlaytimeChart(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(kChartHeight);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAccessibleName(tr("Playtime of the last 14 days"));
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void PlaytimeChart::setDays(const QList<Day>& days)
{
    m_days = days;
    m_hover = -1;
    updateAccessibleText();
    update();
}

void PlaytimeChart::updateAccessibleText()
{
    QStringList parts;
    for (int i = 0; i < m_days.size(); ++i)
        parts << tooltipFor(i);
    setAccessibleDescription(parts.join(QStringLiteral("; ")));
}

QString PlaytimeChart::tooltipFor(int index) const
{
    if (index < 0 || index >= m_days.size())
        return {};
    const Day& d = m_days.at(index);
    return QStringLiteral("%1 · %2 · %3")
        .arg(QLocale::c().toString(d.date, QStringLiteral("ddd d MMM")), formatDuration(d.ms), sessionsText(d.sessionCount));
}

PlaytimeChart::Geometry PlaytimeChart::geometry() const
{
    Geometry g;
    g.plot = QRectF(0, kPlotTop, std::max(0, width() - kGutter), std::max(0, height() - kPlotTop - kAxisHeight));
    g.slot = m_days.isEmpty() ? 0 : g.plot.width() / m_days.size();
    g.barWidth = std::min(28.0, g.slot * 0.62);
    qint64 maxMs = 0;
    for (const Day& d : m_days)
        maxMs = std::max(maxMs, d.ms);
    g.step = niceStepMs(maxMs);
    return g;
}

int PlaytimeChart::barAt(const QPoint& pos) const
{
    const Geometry g = geometry();
    if (g.slot <= 0 || pos.x() < 0 || pos.x() >= g.plot.right() || pos.y() < 0 || pos.y() > g.plot.bottom() + kAxisHeight)
        return -1;
    return std::clamp(int(pos.x() / g.slot), 0, int(m_days.size()) - 1);
}

QStringList PlaytimeChart::gridLabels() const
{
    const Geometry g = geometry();
    QStringList out;
    for (int k = 1; k <= 3; ++k)
        out << formatDuration(g.step * k);
    return out;
}

QStringList PlaytimeChart::axisLabels() const
{
    QStringList out;
    for (const Day& d : m_days)
        out << QLocale::c().dayName(d.date.dayOfWeek(), QLocale::ShortFormat).left(1);
    return out;
}

void PlaytimeChart::mouseMoveEvent(QMouseEvent* event)
{
    const int index = barAt(event->position().toPoint());
    if (index != m_hover) {
        m_hover = index;
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void PlaytimeChart::leaveEvent(QEvent* event)
{
    if (m_hover != -1) {
        m_hover = -1;
        update();
    }
    QWidget::leaveEvent(event);
}

bool PlaytimeChart::event(QEvent* e)
{
    if (e->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(e);
        const int index = barAt(help->pos());
        if (index >= 0)
            QToolTip::showText(help->globalPos(), tooltipFor(index), this);
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

void PlaytimeChart::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const Geometry g = geometry();
    if (g.slot <= 0)
        return;
    const double bottom = g.plot.bottom();
    const double fullHeight = g.plot.height();
    const double scaleMax = double(g.step) * 3.0;

    // gridlines and their labels (right-aligned in the gutter)
    p.setFont(Style::font(Role::Caption));
    for (int k = 1; k <= 3; ++k) {
        const double y = bottom - fullHeight * k / 3.0;
        QColor line = t.border;
        line.setAlphaF(0.6);
        p.setPen(QPen(line, 1));
        p.drawLine(QPointF(0, std::round(y) + 0.5), QPointF(g.plot.right(), std::round(y) + 0.5));
        p.setPen(t.textSecondary);
        p.drawText(QRectF(g.plot.right() + 6, y - 9, kGutter - 6, 18), Qt::AlignRight | Qt::AlignVCenter, formatDuration(g.step * k));
    }

    // bars
    const int today = int(m_days.size()) - 1;
    for (int i = 0; i < m_days.size(); ++i) {
        const Day& d = m_days.at(i);
        const double x = g.slot * (i + 0.5) - g.barWidth / 2;
        QColor color = t.accent;
        if (i == m_hover)
            color = t.accentHover;
        else if (i != today)
            color.setAlphaF(kBarAlpha);
        p.setPen(Qt::NoPen);
        if (d.ms <= 0) {
            // a 2 px stub so the day is still visible
            p.setBrush(i == m_hover ? t.borderStrong : t.border);
            p.drawRoundedRect(QRectF(x, bottom - 2, g.barWidth, 2), 1, 1);
        } else {
            const double h = std::clamp(fullHeight * double(d.ms) / scaleMax, 2.0, fullHeight);
            const double r = std::min<double>(Tokens::radiusSm - 1, h);  // 3 px
            QPainterPath path;
            path.moveTo(x, bottom);
            path.lineTo(x, bottom - h + r);
            path.quadTo(x, bottom - h, x + r, bottom - h);
            path.lineTo(x + g.barWidth - r, bottom - h);
            path.quadTo(x + g.barWidth, bottom - h, x + g.barWidth, bottom - h + r);
            path.lineTo(x + g.barWidth, bottom);
            path.closeSubpath();
            p.setBrush(color);
            p.drawPath(path);
        }
        // weekday initial
        p.setPen(i == today ? t.text : t.textSecondary);
        p.drawText(QRectF(g.slot * i, bottom + 4, g.slot, kAxisHeight - 4), Qt::AlignHCenter | Qt::AlignVCenter,
                   QLocale::c().dayName(d.date.dayOfWeek(), QLocale::ShortFormat).left(1));
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// PlaytimeView
// ---------------------------------------------------------------------------------------------------------------------

PlaytimeView::Sources PlaytimeView::applicationSources()
{
    Sources s;
    s.sessions = []() {
        auto* history = SessionHistory::global();
        return history ? history->sessions() : QList<Session>();
    };
    s.totals = []() {
        QList<InstanceTotal> out;
        if (!APPLICATION || !APPLICATION->instances())
            return out;
        auto* list = APPLICATION->instances();
        for (int i = 0; i < list->count(); ++i) {
            auto* inst = list->at(i);
            InstanceTotal t;
            t.id = inst->id();
            t.name = inst->name();
            t.totalSeconds = inst->totalTimePlayed();
            t.lastPlayedMs = inst->lastLaunch();
            out.append(t);
        }
        return out;
    };
    s.now = []() { return QDateTime::currentDateTime(); };
    s.icon = [](const QString& id) {
        if (!APPLICATION || !APPLICATION->instances())
            return QIcon();
        auto* inst = APPLICATION->instances()->getInstanceById(id);
        return inst ? APPLICATION->icons()->getIcon(inst->iconKey()) : QIcon();
    };
    return s;
}

PlaytimeView::PlaytimeView(QWidget* parent) : PlaytimeView(applicationSources(), parent) {}

PlaytimeView::PlaytimeView(Sources sources, QWidget* parent) : QWidget(parent), m_sources(std::move(sources))
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    m_stack = new QStackedLayout;
    outer->addLayout(m_stack, 1);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_stack->addWidget(m_scroll);

    m_empty = new ZeroEmptyState(QStringLiteral("clock"), tr("No playtime yet"), tr("Play an instance and your time shows up here."), {}, this);
    m_stack->addWidget(m_empty);

    if (auto* history = SessionHistory::global())
        connect(history, &SessionHistory::changed, this, &PlaytimeView::refresh);
    rebuild();
}

QString PlaytimeView::description()
{
    return tr("How long you've played, by day and by instance");
}

void PlaytimeView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refresh();
}

void PlaytimeView::refresh()
{
    rebuild();
}

bool PlaytimeView::isEmptyStateShown() const
{
    return m_stack->currentWidget() == m_empty;
}

QStringList PlaytimeView::tileTexts() const
{
    return m_tiles;
}

QStringList PlaytimeView::instanceRows() const
{
    return m_rows;
}

void PlaytimeView::clickRow(int index)
{
    if (index >= 0 && index < m_rowWidgets.size())
        emit static_cast<ClickRow*>(m_rowWidgets.at(index))->activated();
}

void PlaytimeView::rebuild()
{
    const QDateTime now = m_sources.now ? m_sources.now() : QDateTime::currentDateTime();
    const QList<Session> sessions = m_sources.sessions ? m_sources.sessions() : QList<Session>();
    const QList<InstanceTotal> totals = m_sources.totals ? m_sources.totals() : QList<InstanceTotal>();
    const qint64 allMs = allTimeMs(totals);

    m_tiles.clear();
    m_rows.clear();
    m_rowWidgets.clear();
    m_chart = nullptr;
    m_showAll = nullptr;

    if (sessions.isEmpty() && allMs <= 0) {
        m_stack->setCurrentWidget(m_empty);
        if (QWidget* old = m_scroll->takeWidget())
            old->deleteLater();
        return;
    }

    auto* host = new QWidget;
    auto* page = new QVBoxLayout(host);
    page->setContentsMargins(0, 0, Tokens::spaceSm, Tokens::spaceXl);
    page->setSpacing(Tokens::spaceLg);

    // ----- tiles -----
    {
        auto* rowHost = new QWidget(host);
        auto* row = new QHBoxLayout(rowHost);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(Tokens::spaceLg);
        auto addTile = [&](const QString& label, const QString& value, const QString& caption) {
            auto* card = new ZeroCard(rowHost);
            card->setHoverEnabled(false);
            auto* lay = new QVBoxLayout(card);
            lay->setContentsMargins(Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg);
            lay->setSpacing(Tokens::spaceXs);
            lay->addWidget(makeSecondary(label, Role::Caption, card));
            lay->addWidget(makeLabel(value, Role::Heading, card));
            auto* cap = makeElided(caption, Role::Caption, card);
            useSecondaryColor(cap);
            lay->addWidget(cap);
            row->addWidget(card, 1);
            m_tiles << label << value << caption;
        };
        const Longest longest = longestSession(sessions);
        addTile(tr("This week"), formatDuration(thisWeekMs(sessions, now)), tr("Last 7 days"));
        addTile(tr("All time"), formatDuration(allMs), tr("All instances"));
        addTile(tr("Sessions (30 days)"), QString::number(sessionsLast30(sessions, now)),
                longest.valid() ? tr("Longest: %1 · %2").arg(formatDuration(longest.ms), longest.instanceName) : tr("No sessions recorded yet"));
        page->addWidget(rowHost);
    }

    // ----- last 14 days -----
    {
        auto* card = new ZeroCard(host);
        card->setHoverEnabled(false);
        auto* lay = new QVBoxLayout(card);
        lay->setContentsMargins(Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg);
        lay->setSpacing(Tokens::spaceMd);
        lay->addWidget(makeLabel(tr("Last 14 days"), Role::Heading, card));
        m_chart = new PlaytimeChart(card);
        m_chart->setDays(days(sessions, now, 14));
        lay->addWidget(m_chart);
        page->addWidget(card);
    }

    // ----- by instance -----
    {
        auto* card = new ZeroCard(host);
        card->setHoverEnabled(false);
        auto* lay = new QVBoxLayout(card);
        lay->setContentsMargins(Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg);
        lay->setSpacing(Tokens::spaceXs);
        lay->addWidget(makeLabel(tr("By instance"), Role::Heading, card));
        lay->addSpacing(Tokens::spaceXs);
        const QList<InstanceShare> shares = byInstance(totals);
        if (shares.isEmpty())
            lay->addWidget(makeSecondary(tr("No time recorded per instance yet."), Role::Caption, card));
        const int visible = m_expanded ? int(shares.size()) : std::min<int>(kTopInstances, int(shares.size()));
        for (int i = 0; i < visible; ++i) {
            const InstanceShare& share = shares.at(i);
            const QString totalText = formatDuration(share.instance.totalSeconds * 1000);
            auto* row = new ClickRow(card);
            row->setAccessibleName(tr("%1, %2").arg(share.instance.name, totalText));
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(Tokens::spaceSm, Tokens::spaceSm, Tokens::spaceSm, Tokens::spaceSm);
            h->setSpacing(Tokens::spaceMd);
            auto* tile = new IconTile(36, 24, row);
            if (m_sources.icon)
                tile->setInstanceIcon(m_sources.icon(share.instance.id));
            h->addWidget(tile, 0, Qt::AlignTop);
            auto* col = new QVBoxLayout;
            col->setContentsMargins(0, 0, 0, 0);
            col->setSpacing(Tokens::spaceXs);
            auto* top = new QHBoxLayout;
            top->setContentsMargins(0, 0, 0, 0);
            top->setSpacing(Tokens::spaceMd);
            top->addWidget(makeElided(share.instance.name, Role::Label, row), 1);
            auto* total = makeLabel(totalText, Role::Label, row);
            total->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            top->addWidget(total);
            col->addLayout(top);
            col->addWidget(new ShareBar(share.share, row));
            const QString last = share.instance.lastPlayedMs > 0 ? tr("Last played %1").arg(Format::relativeTime(share.instance.lastPlayedMs, now))
                                                                  : tr("Never played");
            auto* caption = makeElided(last, Role::Caption, row);
            useSecondaryColor(caption);
            col->addWidget(caption);
            h->addLayout(col, 1);
            const QString id = share.instance.id;
            connect(row, &ClickRow::activated, this, [this, id]() { emit showInstanceRequested(id); });
            lay->addWidget(row);
            m_rows << QStringLiteral("%1\t%2").arg(share.instance.name, totalText);
            m_rowWidgets << row;
        }
        if (visible < shares.size()) {
            m_showAll = new QPushButton(tr("Show all (%1)").arg(shares.size()), card);
            Style::setProperty(m_showAll, "zeroVariant", "ghost");
            connect(m_showAll, &QPushButton::clicked, this, [this]() {
                m_expanded = true;
                rebuild();
            });
            lay->addWidget(m_showAll, 0, Qt::AlignLeft);
        }
        page->addWidget(card);
    }
    page->addStretch(1);

    const int scrollPos = m_scroll->verticalScrollBar()->value();
    if (QWidget* old = m_scroll->takeWidget())
        old->deleteLater();
    m_scroll->setWidget(host);
    m_stack->setCurrentWidget(m_scroll);
    m_scroll->verticalScrollBar()->setValue(scrollPos);
}

}  // namespace Zero

#include "PlaytimeView.moc"
