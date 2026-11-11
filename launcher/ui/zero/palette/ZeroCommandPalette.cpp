// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroCommandPalette.h"

#include <QAccessible>
#include <QAccessibleWidget>
#include <QDateTime>
#include <QHash>
#include <QPointer>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>

#include "PaletteDraw.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {

namespace {
constexpr int kInputHeight = 52;
constexpr int kFooterHeight = 28;
constexpr int kSideInset = 8;   // highlight inset from the panel edge
constexpr int kContentPad = 12;  // inside the highlight
constexpr int kIconBox = 20;
constexpr int kMaxCommands = 60;

QString sectionTitle(Palette::Kind kind)
{
    switch (kind) {
        case Palette::Kind::Instance:
            return QObject::tr("Instances");
        case Palette::Kind::Navigation:
            return QObject::tr("Navigation");
        case Palette::Kind::Action:
            return QObject::tr("Actions");
        case Palette::Kind::Setting:
            return QObject::tr("Settings");
        case Palette::Kind::Appearance:
            return QObject::tr("Appearance");
    }
    return {};
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Input row, footer, divider
// ---------------------------------------------------------------------------------------------------------------------

class PaletteInputRow : public QWidget {
   public:
    explicit PaletteInputRow(QWidget* parent) : QWidget(parent), m_edit(new QLineEdit(this))
    {
        setFixedHeight(kInputHeight);
        m_edit->setFrame(false);
        m_edit->setPlaceholderText(QObject::tr("Type a command, instance or setting…"));
        m_edit->setStyleSheet(QStringLiteral("QLineEdit, QLineEdit:hover, QLineEdit:focus { background: transparent; border: none; padding: 0; }"));
        m_edit->setFont(Style::font(Role::Heading));
        m_edit->setAccessibleName(QObject::tr("Command palette"));
        connect(&Theme::instance(), &Theme::changed, this, [this]() {
            m_edit->setFont(Style::font(Role::Heading));
            update();
        });
    }
    QLineEdit* edit() const { return m_edit; }

   protected:
    void resizeEvent(QResizeEvent*) override
    {
        const int left = 16 + 18 + 12;
        const int right = 16 + PaletteDraw::keycapWidth(QObject::tr("Esc")) + 12;
        m_edit->setGeometry(left, 0, qMax(0, width() - left - right), height());
    }
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        Icons::icon(QStringLiteral("search"), t.textTertiary).paint(&p, QRect(16, (height() - 18) / 2, 18, 18));
        const QString esc = QObject::tr("Esc");
        PaletteDraw::paintKeycap(p, width() - 16 - PaletteDraw::keycapWidth(esc), height() / 2, esc);
    }

   private:
    QLineEdit* m_edit;
};

class PaletteFooter : public QWidget {
   public:
    explicit PaletteFooter(QWidget* parent) : QWidget(parent) { setFixedHeight(kFooterHeight); }
    void setText(const QString& text)
    {
        m_text = text;
        update();
    }
    QString text() const { return m_text; }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        const QFont font = Style::font(Role::Caption);
        p.setFont(font);
        p.setPen(t.textTertiary);
        p.drawText(QRect(16, 0, width() - 32, height()), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(font).elidedText(m_text, Qt::ElideRight, width() - 32));
    }

   private:
    QString m_text;
};

namespace {
class PaletteLine : public QWidget {
   public:
    explicit PaletteLine(QWidget* parent) : QWidget(parent) { setFixedHeight(1); }

   protected:
    void paintEvent(QPaintEvent*) override { QPainter(this).fillRect(rect(), Theme::current().border); }
};
}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// PaletteResults
// ---------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------------------------
// Accessibility: the rows are painted, so screen readers and UI Automation get one interface per row from here.
// ---------------------------------------------------------------------------------------------------------------------

namespace {
class PaletteRowAccessible : public QAccessibleInterface, public QAccessibleActionInterface {
   public:
    PaletteRowAccessible(PaletteResults* list, int row) : m_list(list), m_row(row), m_generation(list->generation()) {}

    bool isValid() const override { return m_list && m_list->generation() == m_generation && m_row < m_list->rowCount(); }
    QObject* object() const override { return nullptr; }
    QWindow* window() const override { return m_list ? m_list->window()->windowHandle() : nullptr; }
    QAccessibleInterface* parent() const override { return m_list ? QAccessible::queryAccessibleInterface(m_list.data()) : nullptr; }
    QAccessibleInterface* child(int) const override { return nullptr; }
    QAccessibleInterface* childAt(int, int) const override { return nullptr; }
    int childCount() const override { return 0; }
    int indexOfChild(const QAccessibleInterface*) const override { return -1; }
    QString text(QAccessible::Text t) const override
    {
        if (!isValid())
            return {};
        if (t == QAccessible::Name)
            return m_list->rowTitle(m_row);
        if (t == QAccessible::Description)
            return m_list->rowDescription(m_row);
        return {};
    }
    void setText(QAccessible::Text, const QString&) override {}
    QRect rect() const override
    {
        if (!isValid())
            return {};
        const QRect r = m_list->rowRect(m_row);
        return QRect(m_list->mapToGlobal(r.topLeft()), r.size());
    }
    QAccessible::Role role() const override
    {
        return isValid() && m_list->row(m_row).header ? QAccessible::StaticText : QAccessible::ListItem;
    }
    QAccessible::State state() const override
    {
        QAccessible::State s;
        if (!isValid()) {
            s.invalid = true;
            return s;
        }
        s.invisible = !m_list->isVisible();
        s.offscreen = !m_list->rect().intersects(m_list->rowRect(m_row));
        if (!m_list->row(m_row).header) {
            s.selectable = true;
            s.selected = m_list->isSelectedRow(m_row);
        }
        return s;
    }
    void* interface_cast(QAccessible::InterfaceType type) override
    {
        return type == QAccessible::ActionInterface ? static_cast<QAccessibleActionInterface*>(this) : nullptr;
    }
    QStringList actionNames() const override
    {
        return isValid() && !m_list->row(m_row).header ? QStringList{ pressAction() } : QStringList{};
    }
    void doAction(const QString& name) override
    {
        if (name == pressAction() && isValid())
            m_list->activateRow(m_row);
    }
    QStringList keyBindingsForAction(const QString&) const override { return {}; }

   private:
    QPointer<PaletteResults> m_list;
    int m_row;
    quint64 m_generation;
};

class PaletteResultsAccessible : public QAccessibleWidget {
   public:
    explicit PaletteResultsAccessible(PaletteResults* list) : QAccessibleWidget(list, QAccessible::List) {}
    ~PaletteResultsAccessible() override { clearRows(); }

    int childCount() const override { return list()->rowCount(); }
    QAccessibleInterface* child(int index) const override
    {
        if (index < 0 || index >= list()->rowCount())
            return nullptr;
        sync();
        if (const auto it = m_rows.constFind(index); it != m_rows.constEnd())
            return QAccessible::accessibleInterface(it.value());
        auto* iface = new PaletteRowAccessible(list(), index);
        m_rows.insert(index, QAccessible::registerAccessibleInterface(iface));
        return iface;
    }
    int indexOfChild(const QAccessibleInterface* child) const override
    {
        sync();
        for (auto it = m_rows.cbegin(); it != m_rows.cend(); ++it)
            if (QAccessible::accessibleInterface(it.value()) == child)
                return it.key();
        return -1;
    }
    QAccessibleInterface* childAt(int x, int y) const override
    {
        const QPoint p = list()->mapFromGlobal(QPoint(x, y));
        for (int i = 0; i < list()->rowCount(); ++i)
            if (list()->rowRect(i).contains(p))
                return child(i);
        return nullptr;
    }

   private:
    PaletteResults* list() const { return static_cast<PaletteResults*>(widget()); }
    void sync() const
    {
        if (m_generation == list()->generation())
            return;
        clearRows();
        m_generation = list()->generation();
    }
    void clearRows() const
    {
        for (QAccessible::Id id : std::as_const(m_rows))
            QAccessible::deleteAccessibleInterface(id);
        m_rows.clear();
    }

    mutable QHash<int, QAccessible::Id> m_rows;
    mutable quint64 m_generation = 0;
};

QAccessibleInterface* paletteAccessibleFactory(const QString& className, QObject* object)
{
    if (className == QLatin1String("Zero::PaletteResults") && object && object->isWidgetType())
        return new PaletteResultsAccessible(static_cast<PaletteResults*>(object));
    return nullptr;
}
}  // namespace

PaletteResults::PaletteResults(QWidget* parent) : QWidget(parent)
{
    static const bool accessibleFactoryInstalled = (QAccessible::installFactory(paletteAccessibleFactory), true);
    Q_UNUSED(accessibleFactoryInstalled)
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    m_glide.setDuration(Tokens::durFast);
    m_glide.setEasingCurve(Motion::easeGlide());
    connect(&m_glide, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_highlightY = v.toReal();
        update();
    });
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

int PaletteResults::contentHeight() const
{
    if (m_rows.isEmpty())
        return kEmptyHeight;
    return m_tops.last() + (m_rows.last().header ? kHeaderHeight : kRowHeight) + kPad;
}

int PaletteResults::viewHeight() const
{
    return qMin(contentHeight(), kMaxViewHeight);
}

void PaletteResults::setRows(const QList<Row>& rows, const QList<PaletteCommand>* commands, const QString& emptyText)
{
    m_rows = rows;
    m_commands = commands;
    m_emptyText = emptyText;
    m_tops.clear();
    m_commandRows.clear();
    int y = kPad;
    for (int i = 0; i < m_rows.size(); ++i) {
        m_tops.append(y);
        if (m_rows.at(i).header) {
            y += kHeaderHeight;
        } else {
            m_commandRows.append(i);
            y += kRowHeight;
        }
    }
    m_scroll = 0;
    ++m_generation;
    m_glide.stop();
    m_selectedRow = m_commandRows.isEmpty() ? -1 : m_commandRows.first();
    m_highlightY = m_selectedRow >= 0 ? m_tops.at(m_selectedRow) : 0;
    setFixedHeight(viewHeight());
    update();
    if (QAccessible::isActive()) {
        QAccessibleEvent reorder(this, QAccessible::ObjectReorder);
        QAccessible::updateAccessibility(&reorder);
    }
    emit selectionChanged();
}

QString PaletteResults::rowTitle(int index) const
{
    const Row& r = m_rows.at(index);
    if (r.header)
        return r.text;
    return m_commands && r.command >= 0 && r.command < m_commands->size() ? m_commands->at(r.command).title : QString();
}

QString PaletteResults::rowDescription(int index) const
{
    const Row& r = m_rows.at(index);
    if (r.header || !m_commands || r.command < 0 || r.command >= m_commands->size())
        return {};
    const PaletteCommand& c = m_commands->at(r.command);
    QStringList parts;
    if (!c.subtitle.isEmpty())
        parts << c.subtitle;
    if (!c.badge.isEmpty())
        parts << c.badge;
    if (!c.shortcut.isEmpty())
        parts << c.shortcut.toString(QKeySequence::NativeText);
    return parts.join(QStringLiteral(" · "));
}

QRect PaletteResults::rowRect(int index) const
{
    return QRect(0, m_tops.at(index) - m_scroll, width(), m_rows.at(index).header ? kHeaderHeight : kRowHeight);
}

void PaletteResults::activateRow(int index)
{
    if (index >= 0 && index < m_rows.size() && !m_rows.at(index).header)
        emit activated(m_rows.at(index).command, false);
}

int PaletteResults::selectedCommand() const
{
    return m_selectedRow >= 0 ? m_rows.at(m_selectedRow).command : -1;
}

int PaletteResults::selectedPosition() const
{
    return m_commandRows.indexOf(m_selectedRow);
}

QList<int> PaletteResults::commandOrder() const
{
    QList<int> out;
    for (int row : m_commandRows)
        out.append(m_rows.at(row).command);
    return out;
}

QStringList PaletteResults::headers() const
{
    QStringList out;
    for (const Row& r : m_rows)
        if (r.header)
            out.append(r.text);
    return out;
}

QRect PaletteResults::selectedRowRect() const
{
    if (m_selectedRow < 0)
        return {};
    return QRect(0, m_tops.at(m_selectedRow) - m_scroll, width(), kRowHeight);
}

void PaletteResults::moveSelection(int delta)
{
    if (m_commandRows.isEmpty())
        return;
    const int count = m_commandRows.size();
    int pos = selectedPosition();
    pos = pos < 0 ? 0 : ((pos + delta) % count + count) % count;
    selectPosition(pos, true);
}

void PaletteResults::selectPosition(int position, bool scroll)
{
    if (position < 0 || position >= m_commandRows.size())
        return;
    select(m_commandRows.at(position), scroll, true);
}

void PaletteResults::select(int row, bool scroll, bool animate)
{
    if (row == m_selectedRow)
        return;
    m_selectedRow = row;
    if (scroll)
        ensureVisible(row);
    const qreal target = m_tops.at(row);
    m_glide.stop();
    if (animate && Motion::enabled()) {
        m_glide.setStartValue(m_highlightY);
        m_glide.setEndValue(target);
        m_glide.start();
    } else {
        m_highlightY = target;
    }
    update();
    emit selectionChanged();
    if (QAccessible::isActive()) {
        if (QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(this)) {
            if (QAccessibleInterface* child = iface->child(row)) {
                QAccessibleEvent selected(child, QAccessible::Selection);
                QAccessible::updateAccessibility(&selected);
            }
        }
    }
}

void PaletteResults::ensureVisible(int row)
{
    int top = m_tops.at(row);
    const int bottom = top + kRowHeight;
    if (row > 0 && m_rows.at(row - 1).header)
        top = m_tops.at(row - 1);
    if (row == 0 || (row == 1 && m_rows.at(0).header))
        top = 0;
    const int view = height();
    if (top < m_scroll)
        m_scroll = top;
    else if (bottom + kPad > m_scroll + view)
        m_scroll = bottom + kPad - view;
    m_scroll = qBound(0, m_scroll, qMax(0, contentHeight() - view));
}

int PaletteResults::rowAt(int y) const
{
    const int cy = y + m_scroll;
    for (int i = 0; i < m_rows.size(); ++i) {
        const int h = m_rows.at(i).header ? kHeaderHeight : kRowHeight;
        if (cy >= m_tops.at(i) && cy < m_tops.at(i) + h)
            return i;
    }
    return -1;
}

void PaletteResults::mouseMoveEvent(QMouseEvent* event)
{
    const int row = rowAt(int(event->position().y()));
    if (row >= 0 && !m_rows.at(row).header)
        select(row, false, true);
    event->accept();
}

void PaletteResults::leaveEvent(QEvent*) {}

void PaletteResults::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const int row = rowAt(int(event->position().y()));
    if (row >= 0 && !m_rows.at(row).header)
        emit activated(m_rows.at(row).command, event->modifiers().testFlag(Qt::ControlModifier));
}

void PaletteResults::wheelEvent(QWheelEvent* event)
{
    const int maxScroll = qMax(0, contentHeight() - height());
    if (maxScroll == 0) {
        event->accept();
        return;
    }
    int delta = event->pixelDelta().y();
    if (delta == 0)
        delta = event->angleDelta().y() * kRowHeight / 120;
    m_scroll = qBound(0, m_scroll - delta, maxScroll);
    update();
    event->accept();
}

void PaletteResults::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (m_rows.isEmpty()) {
        p.setFont(Style::font(Role::Body));
        p.setPen(t.textTertiary);
        p.drawText(rect(), Qt::AlignCenter, m_emptyText);
        return;
    }

    // Selection highlight (glides between rows): higher fill + 2 px accent2 bar.
    if (m_selectedRow >= 0) {
        const QRectF hi(kSideInset, m_highlightY - m_scroll, width() - 2 * kSideInset, kRowHeight);
        p.setPen(Qt::NoPen);
        p.setBrush(t.higher);
        p.drawPath(Paint::panelPath(hi, t.radiusMd));
        p.setBrush(t.accent2);
        p.drawRect(QRectF(hi.left(), hi.top() + 10, 2, kRowHeight - 20));
    }

    const QFont overline = Style::font(Role::Overline);
    const QFont labelFont = Style::font(Role::Label);
    QFont strongFont = labelFont;
    strongFont.setWeight(QFont::DemiBold);
    const QFont caption = Style::font(Role::Caption);
    const QFontMetrics labelMetrics(labelFont), strongMetrics(strongFont), captionMetrics(caption);

    for (int i = 0; i < m_rows.size(); ++i) {
        const Row& row = m_rows.at(i);
        const int y = m_tops.at(i) - m_scroll;
        const int h = row.header ? kHeaderHeight : kRowHeight;
        if (y + h < 0 || y > height())
            continue;
        if (row.header) {
            p.setFont(overline);
            p.setPen(t.textTertiary);
            p.drawText(QRect(kSideInset + kContentPad, y + 6, width() - 2 * (kSideInset + kContentPad), h - 6), Qt::AlignLeft | Qt::AlignVCenter,
                       row.text.toUpper());
            continue;
        }
        const PaletteCommand& cmd = m_commands->at(row.command);
        const bool selected = i == m_selectedRow;
        const int cy = y + h / 2;
        const int rightEdge = width() - kSideInset - kContentPad;
        int rightLimit = rightEdge;

        // Right side: badge, then shortcut key caps or the hint of the selected row.
        if (!cmd.badge.isEmpty()) {
            p.setFont(caption);
            p.setPen(t.textSecondary);
            const int bw = captionMetrics.horizontalAdvance(cmd.badge);
            p.drawText(QRect(rightLimit - bw, y, bw, h), Qt::AlignRight | Qt::AlignVCenter, cmd.badge);
            rightLimit -= bw + 10;
        }
        if (!cmd.shortcut.isEmpty()) {
            rightLimit = PaletteDraw::paintKeycapsRight(p, rightLimit, cy, keyCaps(cmd.shortcut)) - 10;
        } else if (selected && !cmd.hint.isEmpty()) {
            p.setFont(caption);
            p.setPen(t.textTertiary);
            const int hw = captionMetrics.horizontalAdvance(cmd.hint);
            p.drawText(QRect(rightLimit - hw, y, hw, h), Qt::AlignRight | Qt::AlignVCenter, cmd.hint);
            rightLimit -= hw + 10;
        }

        // Icon.
        const int iconX = kSideInset + kContentPad;
        if (!cmd.icon.isNull()) {
            cmd.icon.paint(&p, QRect(iconX, cy - kIconBox / 2, kIconBox, kIconBox));
        } else if (!cmd.iconName.isEmpty()) {
            Icons::icon(cmd.iconName, selected ? t.text : t.textSecondary).paint(&p, QRect(iconX + 2, cy - 8, 16, 16));
        }

        // Title with the matched characters emphasised, then the caption subtitle.
        int x = iconX + kIconBox + 12;
        const int starRoom = cmd.favorite ? 18 : 0;  // a favorite instance gets a solid star after its title
        const int titleRoom = qMax(0, rightLimit - x - starRoom);
        const bool highlight = !row.positions.isEmpty();
        int used = 0;
        int ci = 0;
        while (ci < cmd.title.size() && used < titleRoom) {
            const bool matched = row.positions.contains(ci);
            int cj = ci;
            while (cj < cmd.title.size() && row.positions.contains(cj) == matched)
                ++cj;
            QString run = cmd.title.mid(ci, cj - ci);
            const QFontMetrics& fm = (highlight && matched) ? strongMetrics : labelMetrics;
            run = fm.elidedText(run, Qt::ElideRight, titleRoom - used);
            p.setFont((highlight && matched) ? strongFont : labelFont);
            p.setPen(!highlight || matched ? t.text : t.textSecondary);
            p.drawText(QPoint(x + used, cy + (fm.ascent() - fm.descent()) / 2), run);
            used += fm.horizontalAdvance(run);
            ci = cj;
        }
        x += used;
        if (cmd.favorite) {
            Icons::filledIcon(QStringLiteral("star"), t.highlight).paint(&p, QRect(x + 6, cy - 6, 12, 12));
            x += starRoom;
        }
        if (!cmd.subtitle.isEmpty()) {
            const int subX = x + 10;
            const int room = rightLimit - subX;
            if (room > 24) {
                p.setFont(caption);
                p.setPen(t.textTertiary);
                p.drawText(QRect(subX, y, room, h), Qt::AlignLeft | Qt::AlignVCenter, captionMetrics.elidedText(cmd.subtitle, Qt::ElideRight, room));
            }
        }
    }

    // Slim scroll indicator when the list is longer than the view.
    const int content = contentHeight();
    if (content > height()) {
        const qreal ratio = qreal(height()) / content;
        const qreal barH = qMax(24.0, height() * ratio);
        const qreal barY = (height() - barH) * (qreal(m_scroll) / (content - height()));
        p.setPen(Qt::NoPen);
        p.setBrush(t.borderStrong);
        p.drawRoundedRect(QRectF(width() - 5, barY, 3, barH), 1.5, 1.5);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// ZeroCommandPalette
// ---------------------------------------------------------------------------------------------------------------------

ZeroCommandPalette::ZeroCommandPalette(QWidget* host)
    : PaletteOverlay(host),
      m_inputRow(new PaletteInputRow(panel())),
      m_results(new PaletteResults(panel())),
      m_footer(new PaletteFooter(panel()))
{
    auto* line1 = new PaletteLine(panel());
    auto* line2 = new PaletteLine(panel());
    auto* layout = new QVBoxLayout(panel());
    layout->setContentsMargins(1, 1, 1, 1);  // inside the 1 px border
    layout->setSpacing(0);
    layout->addWidget(m_inputRow);
    layout->addWidget(line1);
    layout->addWidget(m_results);
    layout->addWidget(line2);
    layout->addWidget(m_footer);

    m_inputRow->edit()->installEventFilter(this);
    connect(m_inputRow->edit(), &QLineEdit::textChanged, this, [this]() {
        rebuild();
        relayout();
    });
    connect(m_results, &PaletteResults::selectionChanged, this, &ZeroCommandPalette::updateFooter);
    connect(m_results, &PaletteResults::activated, this, [this](int index, bool secondary) { runCommand(index, secondary); });
}

QLineEdit* ZeroCommandPalette::input() const
{
    return m_inputRow->edit();
}

QString ZeroCommandPalette::query() const
{
    return m_inputRow->edit()->text();
}

void ZeroCommandPalette::setQuery(const QString& query)
{
    m_inputRow->edit()->setText(query);
}

void ZeroCommandPalette::setCommands(const QList<PaletteCommand>& commands)
{
    m_commands = commands;
    rebuild();
    relayout();
}

void ZeroCommandPalette::aboutToOpen()
{
    if (m_provider)
        m_commands = m_provider();
    m_inputRow->edit()->clear();  // fires rebuild() through textChanged when there was a query
    rebuild();
    relayout();
}

QWidget* ZeroCommandPalette::initialFocus()
{
    return m_inputRow->edit();
}

int ZeroCommandPalette::panelHeight() const
{
    return 2 + kInputHeight + 1 + m_results->viewHeight() + 1 + kFooterHeight;
}

void ZeroCommandPalette::relayout()
{
    updatePanelGeometry();
}

void ZeroCommandPalette::rebuild()
{
    const QString q = query();
    QList<PaletteResults::Row> rows;
    auto header = [&rows](const QString& text) {
        PaletteResults::Row r;
        r.header = true;
        r.text = text;
        rows.append(r);
    };
    auto command = [&rows](int index, const QList<int>& positions) {
        PaletteResults::Row r;
        r.command = index;
        r.positions = positions;
        rows.append(r);
    };

    if (q.trimmed().isEmpty()) {
        // Continue (3 most recently played instances), Navigation (all), Suggested.
        QList<int> recent;
        for (int i = 0; i < m_commands.size(); ++i)
            if (m_commands.at(i).kind == Palette::Kind::Instance && m_commands.at(i).lastUsedMs > 0)
                recent.append(i);
        std::stable_sort(recent.begin(), recent.end(), [this](int a, int b) { return m_commands.at(a).lastUsedMs > m_commands.at(b).lastUsedMs; });
        recent = recent.mid(0, 3);
        if (!recent.isEmpty()) {
            header(tr("Continue"));
            for (int i : recent)
                command(i, {});
        }
        QList<int> nav, suggested;
        for (int i = 0; i < m_commands.size(); ++i) {
            if (m_commands.at(i).kind == Palette::Kind::Navigation)
                nav.append(i);
            if (m_commands.at(i).suggested)
                suggested.append(i);
        }
        if (!nav.isEmpty()) {
            header(tr("Navigation"));
            for (int i : nav)
                command(i, {});
        }
        if (!suggested.isEmpty()) {
            header(tr("Suggested"));
            for (int i : suggested)
                command(i, {});
        }
    } else {
        QList<Palette::Candidate> candidates;
        for (const PaletteCommand& c : std::as_const(m_commands)) {
            Palette::Candidate cand;
            cand.title = c.title;
            cand.keywords = c.keywords;
            cand.kind = c.kind;
            cand.lastUsedMs = c.lastUsedMs;
            candidates.append(cand);
        }
        QList<Palette::Ranked> ranked = Palette::rank(q, candidates, QDateTime::currentMSecsSinceEpoch());
        if (ranked.size() > kMaxCommands)
            ranked = ranked.mid(0, kMaxCommands);
        // Group by kind; the section with the best match comes first.
        QList<Palette::Kind> order;
        for (const auto& r : std::as_const(ranked)) {
            const Palette::Kind k = m_commands.at(r.index).kind;
            if (!order.contains(k))
                order.append(k);
        }
        for (Palette::Kind kind : std::as_const(order)) {
            header(sectionTitle(kind));
            for (const auto& r : std::as_const(ranked))
                if (m_commands.at(r.index).kind == kind)
                    command(r.index, r.positions);
        }
    }
    m_results->setRows(rows, &m_commands, tr("No results for \"%1\"").arg(q.trimmed()));
    m_lastQuery = q;
    updateFooter();
}

QStringList ZeroCommandPalette::resultIds() const
{
    QStringList ids;
    for (int i : m_results->commandOrder())
        ids.append(m_commands.at(i).id);
    return ids;
}

QString ZeroCommandPalette::selectedId() const
{
    const int i = m_results->selectedCommand();
    return i >= 0 ? m_commands.at(i).id : QString();
}

int ZeroCommandPalette::selectedIndex() const
{
    return m_results->selectedPosition();
}

QStringList ZeroCommandPalette::sectionTitles() const
{
    return m_results->headers();
}

QString ZeroCommandPalette::footerText() const
{
    return m_footer->text();
}

void ZeroCommandPalette::updateFooter()
{
    QStringList parts;
    parts << tr("↑↓ Navigate");
    const int i = m_results->selectedCommand();
    if (i >= 0) {
        const PaletteCommand& c = m_commands.at(i);
        parts << QStringLiteral("↵ %1").arg(c.primaryLabel.isEmpty() ? tr("Run") : c.primaryLabel);
        if (c.runSecondary)
            parts << QStringLiteral("Ctrl+↵ %1").arg(c.secondaryLabel.isEmpty() ? tr("Run alternative") : c.secondaryLabel);
    }
    parts << tr("Esc Close");
    m_footer->setText(parts.join(QStringLiteral(" · ")));
}

void ZeroCommandPalette::moveSelection(int delta)
{
    m_results->moveSelection(delta);
}

void ZeroCommandPalette::runSelected(bool secondary)
{
    const int i = m_results->selectedCommand();
    if (i >= 0)
        runCommand(i, secondary);
}

void ZeroCommandPalette::runCommand(int commandIndex, bool secondary)
{
    if (commandIndex < 0 || commandIndex >= m_commands.size())
        return;
    const PaletteCommand cmd = m_commands.at(commandIndex);  // copy: closing must not invalidate it
    const auto& fn = secondary ? cmd.runSecondary : cmd.run;
    if (!fn)
        return;
    closeOverlay();
    fn();
}

bool ZeroCommandPalette::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_inputRow->edit() && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        switch (key->key()) {
            case Qt::Key_Down:
                moveSelection(1);
                return true;
            case Qt::Key_Up:
                moveSelection(-1);
                return true;
            case Qt::Key_Tab:
                moveSelection(1);
                return true;
            case Qt::Key_Backtab:
                moveSelection(-1);
                return true;
            case Qt::Key_PageDown:
                moveSelection(6);
                return true;
            case Qt::Key_PageUp:
                moveSelection(-6);
                return true;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                runSelected(key->modifiers().testFlag(Qt::ControlModifier));
                return true;
            case Qt::Key_Escape:
                closeOverlay();
                return true;
            default:
                break;
        }
    }
    return PaletteOverlay::eventFilter(watched, event);
}

}  // namespace Zero
