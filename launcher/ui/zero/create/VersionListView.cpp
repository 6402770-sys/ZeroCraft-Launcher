// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - painted version list with loading / error / empty states (see VersionListView.h).
#include "VersionListView.h"

#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListView>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QStackedLayout>
#include <QVBoxLayout>

#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {
namespace Create {

namespace {
constexpr int kPanelPadding = Tokens::spaceXs;  // panel edge -> rows
constexpr int kRowInsetY = Tokens::spaceXs / 2;  // gap between two painted rows (half above, half below)
constexpr int kTextPadding = Tokens::spaceMd;  // row edge -> text
constexpr int kPillGap = Tokens::spaceSm;
}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------------------------------------------------------

QVariant VersionRowModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const ListRow& r = m_rows.at(index.row());
    switch (role) {
        case Qt::DisplayRole:
        case Qt::AccessibleTextRole: {
            // What a screen reader says for the row: every painted part, in reading order.
            QStringList parts{ r.title };
            if (!r.typePill.isEmpty())
                parts << r.typePill;
            if (!r.statusPill.isEmpty())
                parts << r.statusPill;
            if (r.date.isValid())
                parts << VersionRowDelegate::dateText(r.date);
            return parts.join(QStringLiteral(", "));
        }
        case KeyRole:
            return r.key;
        default:
            return {};
    }
}

void VersionRowModel::setRows(const QList<ListRow>& rows)
{
    beginResetModel();
    m_rows = rows;
    endResetModel();
}

int VersionRowModel::indexOfKey(const QString& key) const
{
    if (key.isEmpty())
        return -1;
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).key == key)
            return i;
    return -1;
}

// ---------------------------------------------------------------------------------------------------------------------
// Delegate
// ---------------------------------------------------------------------------------------------------------------------

QString VersionRowDelegate::dateText(const QDateTime& date)
{
    if (!date.isValid())
        return {};
    return QLocale(QLocale::English).toString(date.toLocalTime().date(), QStringLiteral("MMM d, yyyy"));
}

QSize VersionRowDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const
{
    return { option.rect.width(), kRowHeight };
}

void VersionRowDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const auto* model = qobject_cast<const VersionRowModel*>(index.model());
    if (!model || index.row() >= model->rowCount())
        return;
    const ListRow& r = model->row(index.row());
    const auto& t = Theme::current();
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QRect row = option.rect.adjusted(0, kRowInsetY / 2, 0, -kRowInsetY / 2);
    if (selected || hovered) {
        const QRectF frame = QRectF(row).adjusted(0.5, 0.5, -0.5, -0.5);
        painter->setPen(selected ? QPen(t.accent, 1) : QPen(Qt::NoPen));
        painter->setBrush(selected ? t.elevated : t.surfaceHover);
        painter->drawPath(Paint::panelPath(frame, t.radiusMd));
    }

    // Layout by priority: title, status pill, type pill, date. What does not fit is left out (date first, then type).
    const QFont titleFont = Style::font(Role::Label);
    const QFont captionFont = Style::font(Role::Caption);
    const QFontMetrics titleMetrics(titleFont);
    const QFontMetrics captionMetrics(captionFont);
    const QRect inner = row.adjusted(kTextPadding, 0, -kTextPadding, 0);
    const QString date = dateText(r.date);
    const int titleWidth = titleMetrics.horizontalAdvance(r.title);
    const int statusWidth = r.statusPill.isEmpty() ? 0 : PillTag::widthForText(r.statusPill) + kPillGap;
    const int typeWidth = r.typePill.isEmpty() ? 0 : PillTag::widthForText(r.typePill) + kPillGap;
    const int dateWidth = date.isEmpty() ? 0 : captionMetrics.horizontalAdvance(date) + Tokens::spaceLg;
    bool showType = typeWidth > 0, showDate = dateWidth > 0;
    auto needed = [&]() { return titleWidth + statusWidth + (showType ? typeWidth : 0) + (showDate ? dateWidth : 0); };
    if (needed() > inner.width())
        showDate = false;
    if (needed() > inner.width())
        showType = false;

    const int cy = row.center().y();
    int x = inner.left();
    const int titleRoom = qMax(0, inner.width() - statusWidth - (showType ? typeWidth : 0) - (showDate ? dateWidth : 0));
    painter->setFont(titleFont);
    painter->setPen(selected ? t.text : mix(t.textSecondary, t.text, 0.85));
    const QString title = titleMetrics.elidedText(r.title, Qt::ElideRight, titleRoom);
    painter->drawText(QRect(x, row.top(), titleRoom, row.height()), Qt::AlignLeft | Qt::AlignVCenter, title);
    x += qMin(titleWidth, titleRoom);
    const qreal pillTop = cy - Tokens::badgeHeight / 2.0;
    if (showType) {
        x += kPillGap;
        PillTag::paintPill(painter, QRectF(x, pillTop, typeWidth - kPillGap, Tokens::badgeHeight), r.typePill, PillTag::Tone::Plain);
        x += typeWidth - kPillGap;
    }
    if (statusWidth > 0) {
        x += kPillGap;
        PillTag::paintPill(painter, QRectF(x, pillTop, statusWidth - kPillGap, Tokens::badgeHeight), r.statusPill, PillTag::Tone::Success);
    }
    if (showDate) {
        painter->setFont(captionFont);
        painter->setPen(t.textSecondary);
        painter->drawText(inner, Qt::AlignRight | Qt::AlignVCenter, date);
    }
    painter->restore();
}

// ---------------------------------------------------------------------------------------------------------------------
// Skeleton
// ---------------------------------------------------------------------------------------------------------------------

RowSkeleton::RowSkeleton(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_timer.setInterval(33);
    connect(&m_timer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void RowSkeleton::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    m_clock.restart();
    if (Motion::enabled())
        m_timer.start();
}

void RowSkeleton::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    m_timer.stop();
}

void RowSkeleton::paintEvent(QPaintEvent*)
{
    // Same look as ZeroSkeleton's blocks (border colour, one highlight band sweeping left to right), in version-row shapes.
    constexpr int kSweepMs = 1400;
    static const qreal title[] = { 0.22, 0.16, 0.26, 0.19, 0.24, 0.14, 0.20, 0.17 };
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor base = t.border;
    QBrush brush(base);
    if (m_timer.isActive()) {
        const qreal phase = qreal(m_clock.elapsed() % kSweepMs) / kSweepMs;
        const qreal span = width() * 0.35;
        const qreal x = -span + phase * (width() + 2 * span);
        QLinearGradient g(QPointF(x - span, 0), QPointF(x + span, 0));
        g.setColorAt(0.0, base);
        g.setColorAt(0.5, t.borderStrong.lighter(118));
        g.setColorAt(1.0, base);
        brush = QBrush(g);
    }
    const int rows = qMax(1, height() / VersionRowDelegate::kRowHeight);
    for (int i = 0; i < rows; ++i) {
        const qreal cy = i * VersionRowDelegate::kRowHeight + VersionRowDelegate::kRowHeight / 2.0;
        const qreal w = width() - 2 * kTextPadding;
        p.fillPath(Paint::panelPath(QRectF(kTextPadding, cy - 6, qMax(40.0, w * title[i % 8]), 12), t.radiusSm), brush);
        p.fillPath(Paint::panelPath(QRectF(kTextPadding + qMax(40.0, w * title[i % 8]) + 10, cy - 8, 52, 16), t.radiusSm), brush);
        if (w > 220)
            p.fillPath(Paint::panelPath(QRectF(width() - kTextPadding - 72, cy - 5, 72, 10), t.radiusSm), brush);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// View
// ---------------------------------------------------------------------------------------------------------------------

VersionListView::VersionListView(const QString& accessibleName, QWidget* parent) : QWidget(parent), m_model(new VersionRowModel(this))
{
    setAccessibleName(accessibleName);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_stack = new QStackedLayout(this);
    m_stack->setContentsMargins(kPanelPadding, kPanelPadding, kPanelPadding, kPanelPadding);

    // loading: caption + skeleton rows
    m_loadingPage = new QWidget(this);
    auto* loading = new QVBoxLayout(m_loadingPage);
    loading->setContentsMargins(0, Tokens::spaceSm, 0, 0);
    loading->setSpacing(Tokens::spaceXs);
    m_loadingCaption = new QLabel(m_loadingPage);
    Style::applyRole(m_loadingCaption, Role::Caption);
    m_loadingCaption->setContentsMargins(kTextPadding, 0, kTextPadding, 0);
    m_skeleton = new RowSkeleton(m_loadingPage);
    loading->addWidget(m_loadingCaption);
    loading->addWidget(m_skeleton, 1);
    m_stack->addWidget(m_loadingPage);

    // list
    m_list = new QListView(this);
    m_list->setAccessibleName(accessibleName);
    m_list->setModel(m_model);
    m_list->setItemDelegate(new VersionRowDelegate(m_list));
    m_list->setUniformItemSizes(true);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setMouseTracking(true);
    m_list->setStyleSheet(QStringLiteral("QAbstractItemView { background: transparent; border: none; }"));
    m_list->viewport()->setAutoFillBackground(false);
    connect(m_list->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this](const QModelIndex& current) {
        if (m_settingCurrent)
            return;
        emit currentKeyChanged(current.isValid() ? current.data(VersionRowModel::KeyRole).toString() : QString());
    });
    m_stack->addWidget(m_list);

    // inline error + Retry
    m_errorPage = new QWidget(this);
    auto* error = new QVBoxLayout(m_errorPage);
    error->setContentsMargins(Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg);
    error->setSpacing(Tokens::spaceSm);
    error->addStretch(1);
    m_errorIcon = new QLabel(m_errorPage);
    m_errorIcon->setAlignment(Qt::AlignCenter);
    m_errorText = new QLabel(m_errorPage);
    Style::applyRole(m_errorText, Role::Body);
    m_errorText->setWordWrap(true);
    m_errorText->setAlignment(Qt::AlignCenter);
    m_errorText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_retry = new QPushButton(tr("Retry"), m_errorPage);
    m_retry->setAccessibleName(tr("Retry loading %1").arg(accessibleName.toLower()));
    Style::setProperty(m_retry, "zeroVariant", "secondary");
    m_retry->setAutoDefault(false);
    connect(m_retry, &QPushButton::clicked, this, &VersionListView::retryRequested);
    auto* retryRow = new QHBoxLayout;
    retryRow->addStretch(1);
    retryRow->addWidget(m_retry);
    retryRow->addStretch(1);
    error->addWidget(m_errorIcon);
    error->addWidget(m_errorText);
    error->addLayout(retryRow);
    error->addStretch(1);
    m_stack->addWidget(m_errorPage);

    // empty
    m_emptyText = new QLabel(this);
    Style::applyRole(m_emptyText, Role::Caption);
    m_emptyText->setAlignment(Qt::AlignCenter);
    m_emptyText->setWordWrap(true);
    m_emptyText->setContentsMargins(Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg);
    m_stack->addWidget(m_emptyText);

    refreshIcons();
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        refreshIcons();
        update();
    });
    setState(State::Loading);
}

void VersionListView::refreshIcons()
{
    const auto& t = Theme::current();
    m_errorIcon->setPixmap(Icons::pixmap(QStringLiteral("triangle-alert"), t.warning, 24, devicePixelRatioF()));
    m_retry->setIcon(Icons::icon(QStringLiteral("refresh-cw"), t.text));
}

void VersionListView::setState(State state)
{
    m_state = state;
    switch (state) {
        case State::Loading:
            m_stack->setCurrentWidget(m_loadingPage);
            break;
        case State::List:
            m_stack->setCurrentWidget(m_list);
            break;
        case State::Error:
            m_stack->setCurrentWidget(m_errorPage);
            break;
        case State::Empty:
            m_stack->setCurrentWidget(m_emptyText);
            break;
    }
}

void VersionListView::showLoading(const QString& caption)
{
    m_loadingCaption->setText(caption);
    m_loadingCaption->setVisible(!caption.isEmpty());
    m_skeleton->setAccessibleName(caption);
    setState(State::Loading);
}

void VersionListView::showError(const QString& message)
{
    m_errorText->setText(message);
    setState(State::Error);
}

void VersionListView::setRows(const QList<ListRow>& rows, const QString& emptyText)
{
    const QString keep = currentKey();
    m_settingCurrent = true;
    m_model->setRows(rows);
    m_settingCurrent = false;
    m_emptyText->setText(emptyText);
    setState(rows.isEmpty() ? State::Empty : State::List);
    setCurrentKey(keep);
}

void VersionListView::setCurrentKey(const QString& key)
{
    const int row = m_model->indexOfKey(key);
    m_settingCurrent = true;
    if (row < 0) {
        m_list->selectionModel()->clearSelection();
        m_list->selectionModel()->setCurrentIndex(QModelIndex(), QItemSelectionModel::NoUpdate);
    } else {
        const QModelIndex index = m_model->index(row);
        m_list->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
        m_list->scrollTo(index, QAbstractItemView::PositionAtCenter);
    }
    m_settingCurrent = false;
}

QString VersionListView::currentKey() const
{
    const QModelIndex current = m_list->selectionModel()->currentIndex();
    if (!current.isValid() || !m_list->selectionModel()->isSelected(current))
        return {};
    return current.data(VersionRowModel::KeyRole).toString();
}

QString VersionListView::errorText() const
{
    return m_errorText->text();
}

QString VersionListView::emptyText() const
{
    return m_emptyText->text();
}

void VersionListView::paintEvent(QPaintEvent*)
{
    // Flat `surface` panel: lists are text-dense, so they never sit on the stone texture.
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(t.border, 1));
    p.setBrush(t.surface);
    p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusLg));
}

}  // namespace Create
}  // namespace Zero
