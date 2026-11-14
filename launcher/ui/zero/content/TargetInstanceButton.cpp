// SPDX-License-Identifier: GPL-3.0-only
#include "TargetInstanceButton.h"

#include <QMenu>
#include <QPainter>

#include <algorithm>
#include "Application.h"
#include "icons/IconList.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {
namespace Content {

namespace {
QFont textFont()
{
    return Style::font(Role::Body);
}
QString separatorDot()
{
    return QStringLiteral(" · ");
}
}  // namespace

TargetInstanceButton::TargetInstanceButton(QWidget* parent, IconProvider icons) : QPushButton(parent), m_icons(std::move(icons))
{
    setObjectName(QStringLiteral("zeroTargetInstanceButton"));
    Style::setProperty(this, "zeroVariant", "secondary");
    Style::setProperty(this, "zeroSize", "lg");
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);  // may shrink to icon + chevron when the toolbar is short of room
    connect(this, &QPushButton::clicked, this, &TargetInstanceButton::openMenu);
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    refreshAccessibility();
}

QIcon TargetInstanceButton::iconFor(const QString& key) const
{
    if (m_icons)
        return m_icons(key);
    return APPLICATION->icons()->getIcon(key);
}

const TargetInstance* TargetInstanceButton::current() const
{
    for (const auto& t : m_targets) {
        if (t.id == m_currentId)
            return &t;
    }
    return nullptr;
}

void TargetInstanceButton::setTargets(const QList<TargetInstance>& targets)
{
    m_targets = targets;
    refreshAccessibility();
    updateGeometry();
    update();
}

void TargetInstanceButton::setCurrentId(const QString& id)
{
    if (id == m_currentId)
        return;
    m_currentId = id;
    refreshAccessibility();
    updateGeometry();
    update();
}

void TargetInstanceButton::setNeedsLoader(bool needsLoader)
{
    m_needsLoader = needsLoader;
}

void TargetInstanceButton::setCompact(bool compact)
{
    if (compact == m_compact)
        return;
    m_compact = compact;
    refreshAccessibility();
    updateGeometry();
    update();
}

void TargetInstanceButton::refreshAccessibility()
{
    const auto* t = current();
    setAccessibleName(t ? tr("Target instance: %1, %2").arg(t->name, t->summaryFor(m_needsLoader)) : tr("Target instance: none"));
    setToolTip(t ? tr("For %1 · %2").arg(t->name, t->summaryFor(m_needsLoader)) : QString());  // the full text when the toolbar elides it
}

int TargetInstanceButton::hintWidth(bool compact) const
{
    const QFontMetrics fm(textFont());
    int w = kPad + kIcon + Tokens::spaceSm;
    if (!compact)
        w += fm.horizontalAdvance(tr("For")) + Tokens::spaceXs;
    if (const auto* t = current()) {
        w += std::min(compact ? kCompactNameMax : kNameMax, fm.horizontalAdvance(t->name));
        const QString summary = t->summaryFor(m_needsLoader);
        if (!summary.isEmpty() && !compact)
            w += fm.horizontalAdvance(separatorDot() + summary);
    } else {
        w += fm.horizontalAdvance(tr("Choose an instance"));
    }
    w += Tokens::spaceSm + kChevron + kPad;
    return w;
}

QSize TargetInstanceButton::sizeHint() const
{
    return QSize(hintWidth(m_compact), Tokens::controlHeightLg);
}

QSize TargetInstanceButton::minimumSizeHint() const
{
    return QSize(kPad + kIcon + Tokens::spaceSm + kChevron + kPad, Tokens::controlHeightLg);
}

void TargetInstanceButton::paintEvent(QPaintEvent* e)
{
    QPushButton::paintEvent(e);  // the secondary button's background, border and focus border
    const auto& th = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const int bevel = (th.bevel && !isDown()) ? 2 : 0;
    const int cy = (height() - bevel) / 2;
    const QColor tone = isEnabled() ? th.textSecondary : th.textTertiary;
    const QColor main = isEnabled() ? th.text : th.textTertiary;
    const QFont font = textFont();
    p.setFont(font);
    const QFontMetrics fm(font);
    int x = kPad;
    // Short of room (or asked to be compact): "For" and the version move out, the name keeps what is left.
    const bool compact = m_compact || width() < hintWidth(false);

    const auto* t = current();
    if (t) {
        const QIcon icon = iconFor(t->iconKey);
        if (!icon.isNull())
            icon.paint(&p, QRect(x, cy - kIcon / 2, kIcon, kIcon), Qt::AlignCenter, isEnabled() ? QIcon::Normal : QIcon::Disabled);
    }
    x += kIcon + Tokens::spaceSm;

    const auto drawRun = [&](const QString& text, const QColor& color, int maxW) {
        const QString shown = fm.elidedText(text, Qt::ElideRight, std::max(0, maxW));
        p.setPen(color);
        const int w = fm.horizontalAdvance(shown);
        p.drawText(QRect(x, 0, w + 1, height() - bevel), Qt::AlignVCenter | Qt::AlignLeft, shown);
        x += w;
    };
    const int chevronX = width() - kPad - kChevron;
    if (!compact) {
        drawRun(tr("For"), tone, chevronX - x);
        x += Tokens::spaceXs;
    }
    if (t) {
        drawRun(t->name, main, std::min(compact ? kCompactNameMax : kNameMax, chevronX - Tokens::spaceSm - x));
        const QString summary = t->summaryFor(m_needsLoader);
        if (!summary.isEmpty() && !compact)
            drawRun(separatorDot() + summary, tone, chevronX - Tokens::spaceSm - x);
    } else {
        drawRun(tr("Choose an instance"), tone, chevronX - Tokens::spaceSm - x);
    }
    const QIcon chevron = Icons::icon(QStringLiteral("chevron-down"), tone);
    chevron.paint(&p, QRect(chevronX, cy - kChevron / 2, kChevron, kChevron));
}

QMenu* TargetInstanceButton::buildMenu(QWidget* parent)
{
    auto* menu = new QMenu(parent);
    for (const auto& t : m_targets) {
        if (!t.readyFor(m_needsLoader))
            continue;
        QAction* a = menu->addAction(iconFor(t.iconKey), QStringLiteral("%1 — %2").arg(t.name, t.summaryFor(m_needsLoader)));
        a->setCheckable(true);
        a->setChecked(t.id == m_currentId);
        a->setData(t.id);
        const QString id = t.id;
        connect(a, &QAction::triggered, this, [this, id]() {
            setCurrentId(id);
            emit targetChosen(id);
        });
    }
    const int n = m_needsLoader ? countWithoutLoader(m_targets)
                                : int(std::count_if(m_targets.begin(), m_targets.end(), [](const TargetInstance& t) { return !t.packReady(); }));
    if (n > 0) {
        if (!menu->isEmpty())
            menu->addSeparator();
        // English-only UI (no .qm plural forms): spell both forms out.
        QAction* note = menu->addAction(m_needsLoader ? (n == 1 ? tr("1 instance without a mod loader") : tr("%1 instances without a mod loader").arg(n))
                                                      : (n == 1 ? tr("1 instance without a Minecraft version") : tr("%1 instances without a Minecraft version").arg(n)));
        note->setEnabled(false);
    }
    return menu;
}

void TargetInstanceButton::openMenu()
{
    QMenu* menu = buildMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(mapToGlobal(QPoint(0, height() + 4)));
}

}  // namespace Content
}  // namespace Zero
