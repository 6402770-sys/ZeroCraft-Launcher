// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroShortcutSheet.h"

#include <QCoreApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QVBoxLayout>

#include "PaletteDraw.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {

namespace {
constexpr int kTitleHeight = 52;
constexpr int kPadding = 24;
constexpr int kColumnGap = 32;
constexpr int kHeaderHeight = 28;
constexpr int kEntryHeight = 30;
constexpr int kGroupGap = 12;
constexpr int kTopPad = 12;
constexpr int kBottomPad = 20;

int groupHeight(const ShortcutGroup& g)
{
    return kHeaderHeight + g.entries.size() * kEntryHeight;
}
}  // namespace

/// Title row + the groups laid out in two columns (each group goes to the shorter column).
class SheetContent : public QWidget {
   public:
    // No Q_OBJECT here: give tr() the same context lupdate records (the class name) instead of the base class's.
    Q_DECLARE_TR_FUNCTIONS(Zero::SheetContent)
   public:  // the macro leaves the section private
    explicit SheetContent(QWidget* parent) : QWidget(parent) { setFocusPolicy(Qt::StrongFocus); }

    void setGroups(const QList<ShortcutGroup>& groups)
    {
        m_groups = groups;
        layoutColumns();
        setFixedHeight(height_());
        update();
    }
    int height_() const { return kTitleHeight + 1 + kTopPad + qMax(m_heights[0], m_heights[1]) + kBottomPad; }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setFont(Style::font(Role::Heading));
        p.setPen(t.text);
        p.drawText(QRect(kPadding, 0, width() - 2 * kPadding, kTitleHeight), Qt::AlignLeft | Qt::AlignVCenter, tr("Keyboard shortcuts"));
        const QString esc = tr("Esc");
        PaletteDraw::paintKeycap(p, width() - 16 - PaletteDraw::keycapWidth(esc), kTitleHeight / 2, esc);
        p.fillRect(QRect(0, kTitleHeight, width(), 1), t.border);

        const int colW = (width() - 2 * kPadding - kColumnGap) / 2;
        const QFont body = Style::font(Role::Body);
        const QFontMetrics fm(body);
        int y[2] = { kTitleHeight + 1 + kTopPad, kTitleHeight + 1 + kTopPad };
        for (int gi = 0; gi < m_groups.size(); ++gi) {
            const ShortcutGroup& g = m_groups.at(gi);
            const int c = m_column.at(gi);
            const int x = kPadding + c * (colW + kColumnGap);
            p.setFont(Style::font(Role::Overline));
            p.setPen(t.textTertiary);
            p.drawText(QRect(x, y[c], colW, kHeaderHeight), Qt::AlignLeft | Qt::AlignVCenter, g.title.toUpper());
            y[c] += kHeaderHeight;
            for (const ShortcutEntry& e : g.entries) {
                const int cy = y[c] + kEntryHeight / 2;
                const int capsLeft = PaletteDraw::paintKeycapsRight(p, x + colW, cy, e.keys);
                p.setFont(body);
                p.setPen(t.text);
                p.drawText(QRect(x, y[c], capsLeft - x - 12, kEntryHeight), Qt::AlignLeft | Qt::AlignVCenter,
                           fm.elidedText(e.label, Qt::ElideRight, capsLeft - x - 12));
                y[c] += kEntryHeight;
            }
            y[c] += kGroupGap;
        }
    }

   private:
    void layoutColumns()
    {
        m_heights[0] = m_heights[1] = 0;
        m_column.clear();
        for (const ShortcutGroup& g : std::as_const(m_groups)) {
            const int c = m_heights[0] <= m_heights[1] ? 0 : 1;
            m_column.append(c);
            m_heights[c] += groupHeight(g) + kGroupGap;
        }
        m_heights[0] = qMax(0, m_heights[0] - kGroupGap);
        m_heights[1] = qMax(0, m_heights[1] - kGroupGap);
    }

    QList<ShortcutGroup> m_groups;
    QList<int> m_column;
    int m_heights[2] = { 0, 0 };
};

ZeroShortcutSheet::ZeroShortcutSheet(QWidget* host) : PaletteOverlay(host), m_content(new SheetContent(panel()))
{
    auto* layout = new QVBoxLayout(panel());
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addWidget(m_content);
}

void ZeroShortcutSheet::setGroups(const QList<ShortcutGroup>& groups)
{
    m_groups = groups;
    m_content->setGroups(groups);
    updatePanelGeometry();
}

void ZeroShortcutSheet::aboutToOpen()
{
    if (m_provider)
        m_groups = m_provider();
    m_content->setGroups(m_groups);
    updatePanelGeometry();
}

QWidget* ZeroShortcutSheet::initialFocus()
{
    return m_content;
}

int ZeroShortcutSheet::panelHeight() const
{
    return 2 + m_content->height();
}

}  // namespace Zero
