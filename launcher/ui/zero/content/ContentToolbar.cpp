// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the Discover toolbar.
#include "ContentToolbar.h"

#include <QAction>
#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QStyleOptionComboBox>
#include <QStylePainter>
#include <QToolButton>
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {
namespace Content {

namespace {

/// The "Filters" button: a secondary lg button with the sliders icon and a count pill on its right. The pill's room is a qss
/// padding rule keyed on the zeroHasBadge property (obsidian.qss); the pill itself is painted here.
class FiltersButton : public QPushButton {
   public:
    explicit FiltersButton(QWidget* parent) : QPushButton(parent)
    {
        setObjectName(QStringLiteral("zeroFiltersButton"));
        setText(QObject::tr("Filters"));
        setAccessibleName(QObject::tr("Filters"));
        Style::setProperty(this, "zeroVariant", "secondary");
        Style::setProperty(this, "zeroSize", "lg");
        Style::setProperty(this, "zeroHasBadge", false);
    }
    void setCount(int count)
    {
        if (count == m_count)
            return;
        const bool had = m_count > 0;
        m_count = count;
        if (had != (count > 0))
            Style::setProperty(this, "zeroHasBadge", count > 0);
        setAccessibleName(count > 0 ? QObject::tr("Filters, %n active", nullptr, count) : QObject::tr("Filters"));
        updateGeometry();
        update();
    }

   protected:
    void paintEvent(QPaintEvent* e) override
    {
        QPushButton::paintEvent(e);
        if (m_count <= 0)
            return;
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QFont font = Style::font(Role::Caption);
        font.setWeight(QFont::DemiBold);
        const QFontMetrics fm(font);
        const QString label = m_count > 99 ? QStringLiteral("99+") : QString::number(m_count);
        const int h = 18;
        const int w = std::max(h, fm.horizontalAdvance(label) + 10);
        const int bevel = (t.bevel && !isDown()) ? 2 : 0;  // the raised block's bottom edge
        const QRectF pill(width() - kPillRight - w, (height() - bevel - h) / 2.0, w, h);
        p.setPen(Qt::NoPen);
        p.setBrush(t.accent);
        p.drawPath(Paint::panelPath(pill, h / 2.0));
        p.setFont(font);
        p.setPen(t.onAccent);
        p.drawText(pill, Qt::AlignCenter, label);
    }

   private:
    static constexpr int kPillRight = 10;
    int m_count = 0;
};

/// The sort control: a ZERO combo box that reads "Sort: <option>" with the arrow-down-up icon.
class SortCombo : public QComboBox {
   public:
    explicit SortCombo(QWidget* parent) : QComboBox(parent)
    {
        setObjectName(QStringLiteral("zeroSortCombo"));
        setAccessibleName(QObject::tr("Sort"));
        setSizeAdjustPolicy(QComboBox::AdjustToContents);
        setIconSize(QSize(16, 16));
        connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    }
    QSize sizeHint() const override
    {
        QSize s = QComboBox::sizeHint();
        s.rwidth() += fontMetrics().horizontalAdvance(prefix()) + iconSize().width() + Tokens::spaceSm;
        return s;
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QStylePainter p(this);
        QStyleOptionComboBox opt;
        initStyleOption(&opt);
        opt.currentText = prefix() + currentText();
        opt.currentIcon = Icons::icon(QStringLiteral("arrow-down-up"), Theme::current().textSecondary);
        opt.iconSize = iconSize();
        p.drawComplexControl(QStyle::CC_ComboBox, opt);
        p.drawControl(QStyle::CE_ComboBoxLabel, opt);
    }

   private:
    static QString prefix() { return QObject::tr("Sort: "); }
};

}  // namespace

ContentToolbar::ContentToolbar(QWidget* parent)
    : QWidget(parent),
      m_search(new QLineEdit(this)),
      m_filters(new FiltersButton(this)),
      m_caption(new QLabel(this)),
      m_sort(new SortCombo(this)),
      m_viewToggle(new QWidget(this)),
      m_gridButton(new QToolButton(m_viewToggle)),
      m_listButton(new QToolButton(m_viewToggle))
{
    setObjectName(QStringLiteral("zeroContentToolbar"));
    setFixedHeight(Tokens::controlHeightLg);

    m_search->setObjectName(QStringLiteral("zeroContentSearch"));
    m_search->setFixedHeight(Tokens::controlHeightLg);
    m_search->setMinimumWidth(kSearchMinWidth);
    m_search->setMaximumWidth(kSearchMaxWidth);
    m_search->setClearButtonEnabled(true);
    m_search->setPlaceholderText(tr("Search"));
    m_search->setAccessibleName(tr("Search"));
    m_search->installEventFilter(this);

    m_filters->setFixedHeight(Tokens::controlHeightLg);
    m_filters->setIconSize(QSize(16, 16));
    connect(m_filters, &QPushButton::clicked, this, &ContentToolbar::filtersRequested);

    Style::applyRole(m_caption, Role::Body);
    Style::setProperty(m_caption, "zeroTone", "secondary");
    m_caption->setAccessibleName(tr("Results"));
    m_caption->hide();

    m_sort->setFixedHeight(Tokens::controlHeightLg);
    m_sort->hide();
    connect(m_sort, qOverload<int>(&QComboBox::activated), this, [this](int index) { emit sortChanged(m_sort->itemData(index).toString()); });

    for (auto* b : { m_gridButton, m_listButton }) {
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setFixedSize(Tokens::controlHeightLg, Tokens::controlHeightLg);
        b->setIconSize(QSize(18, 18));
        Style::setProperty(b, "zeroVariant", "ghost");
    }
    m_gridButton->setAccessibleName(tr("Grid view"));
    m_gridButton->setToolTip(tr("Grid view"));
    m_listButton->setAccessibleName(tr("List view"));
    m_listButton->setToolTip(tr("List view"));
    m_gridButton->setChecked(true);
    auto* toggle = new QHBoxLayout(m_viewToggle);
    toggle->setContentsMargins(0, 0, 0, 0);
    toggle->setSpacing(2);
    toggle->addWidget(m_gridButton);
    toggle->addWidget(m_listButton);
    m_viewToggle->hide();
    connect(m_gridButton, &QToolButton::clicked, this, [this]() {
        if (m_viewMode != ViewMode::Grid) {
            m_viewMode = ViewMode::Grid;
            emit viewModeChanged(m_viewMode);
        }
    });
    connect(m_listButton, &QToolButton::clicked, this, [this]() {
        if (m_viewMode != ViewMode::List) {
            m_viewMode = ViewMode::List;
            emit viewModeChanged(m_viewMode);
        }
    });

    auto* row = new QHBoxLayout(this);
    m_row = row;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(Tokens::spaceSm);
    row->addWidget(m_search, 1);
    row->addWidget(m_filters);
    row->addStretch(2);
    row->addWidget(m_caption);
    row->addSpacing(Tokens::spaceSm);
    row->addWidget(m_sort);
    row->addWidget(m_viewToggle);

    m_searchTimer.setSingleShot(true);
    m_searchTimer.setInterval(kSearchDebounceMs);
    connect(&m_searchTimer, &QTimer::timeout, this, &ContentToolbar::flushSearch);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (text.isEmpty())
            flushSearch();  // the clear button (or deleting everything) applies at once
        else
            m_searchTimer.start();
    });
    connect(m_search, &QLineEdit::returnPressed, this, &ContentToolbar::flushSearch);

    auto* find = new QShortcut(QKeySequence::Find, this);  // while the toolbar is visible, anywhere in its window
    find->setContext(Qt::WindowShortcut);
    connect(find, &QShortcut::activated, this, &ContentToolbar::focusSearch);

    connect(&Theme::instance(), &Theme::changed, this, &ContentToolbar::refreshIcons);
    refreshIcons();
}

void ContentToolbar::refreshIcons()
{
    const auto& t = Theme::current();
    const QIcon search = Icons::icon(QStringLiteral("search"), t.textTertiary);
    if (m_searchIcon)
        m_searchIcon->setIcon(search);
    else
        m_searchIcon = m_search->addAction(search, QLineEdit::LeadingPosition);
    m_filters->setIcon(Icons::icon(QStringLiteral("sliders-horizontal"), t.text));
    m_gridButton->setIcon(Icons::icon(QStringLiteral("layout-grid"), t.text));
    m_listButton->setIcon(Icons::icon(QStringLiteral("list"), t.text));
}

bool ContentToolbar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        if (!m_search->text().isEmpty()) {
            m_search->clear();  // textChanged applies the empty search at once
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ContentToolbar::setSearchPlaceholder(const QString& text)
{
    m_search->setPlaceholderText(text);
    m_search->setAccessibleName(text);
}

void ContentToolbar::setSearchText(const QString& text)
{
    m_searchTimer.stop();
    m_emittedSearch = text.trimmed();
    const QSignalBlocker block(m_search);
    if (m_search->text() != text)
        m_search->setText(text);
}

QString ContentToolbar::searchText() const
{
    return m_search->text().trimmed();
}

void ContentToolbar::setFilterCount(int count)
{
    m_filterCount = std::max(0, count);
    static_cast<FiltersButton*>(m_filters)->setCount(m_filterCount);
}

void ContentToolbar::setResultsCaption(const QString& text)
{
    m_caption->setText(text);
    m_caption->setVisible(!text.isEmpty());
}

QString ContentToolbar::resultsCaption() const
{
    return m_caption->text();
}

void ContentToolbar::setSortOptions(const QList<SortOption>& options, const QString& currentId)
{
    const QSignalBlocker block(m_sort);
    m_sort->clear();
    for (const SortOption& o : options)
        m_sort->addItem(o.label, o.id);
    const int i = m_sort->findData(currentId);
    m_sort->setCurrentIndex(i >= 0 ? i : 0);
    m_sort->setVisible(!options.isEmpty());
    m_sort->updateGeometry();
}

QString ContentToolbar::sortId() const
{
    return m_sort->currentData().toString();
}

void ContentToolbar::setViewModesAvailable(bool available)
{
    m_viewToggle->setVisible(available);
}

void ContentToolbar::setViewMode(ViewMode mode)
{
    m_viewMode = mode;
    (mode == ViewMode::Grid ? m_gridButton : m_listButton)->setChecked(true);
}

void ContentToolbar::setLeadingWidget(QWidget* widget)
{
    if (widget == m_leading)
        return;
    if (m_leading) {
        m_row->removeWidget(m_leading);
        m_leading->hide();
        m_leading->setParent(nullptr);
    }
    m_leading = widget;
    if (m_leading) {
        m_leading->setParent(this);
        m_row->insertWidget(0, m_leading);
        m_leading->show();
    }
}

void ContentToolbar::focusSearch()
{
    m_search->setFocus(Qt::ShortcutFocusReason);
    m_search->selectAll();
}

void ContentToolbar::flushSearch()
{
    m_searchTimer.stop();
    const QString text = m_search->text().trimmed();
    if (text == m_emittedSearch)
        return;
    m_emittedSearch = text;
    emit searchChanged(text);
}

}  // namespace Content
}  // namespace Zero
