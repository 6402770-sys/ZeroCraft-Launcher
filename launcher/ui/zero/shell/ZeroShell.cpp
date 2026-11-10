// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroShell.h"

#include <QHBoxLayout>
#include <QLayout>
#include <QPainter>
#include <QShortcut>
#include <QStackedWidget>
#include <QVBoxLayout>
#include "AccountChip.h"
#include "ShellPage.h"
#include "ZeroTopBar.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroNavRail.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroTransition.h"
#include "ui/zero/ZeroTokens.h"

namespace Zero {

ZeroShell::ZeroShell(QWidget* parent)
    : QWidget(parent), m_rail(new ZeroNavRail(this)), m_topBar(new ZeroTopBar(this)), m_stack(new QStackedWidget(this))
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_rail);

    auto* column = new QVBoxLayout;
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(m_topBar);
    column->addWidget(m_stack, 1);
    layout->addLayout(column, 1);

    connect(m_rail, &ZeroNavRail::currentChanged, this, &ZeroShell::showPage);
    connect(m_topBar, &ZeroTopBar::accountMenuRequested, this, &ZeroShell::accountMenuRequested);
    connect(m_topBar, &ZeroTopBar::breadcrumbClicked, this, [this]() {
        if (ShellPage* page = m_pages.value(m_current))
            page->breadcrumbActivated();
    });
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void ZeroShell::paintEvent(QPaintEvent*)
{
    // Textured themes carry the stone tile behind the (transparent) top bar and pages; flat themes let the window color show.
    const auto& t = Theme::current();
    if (!t.texture)
        return;
    QPainter p(this);
    Paint::paintBackground(p, rect(), t.bg);
}

void ZeroShell::addPage(const QString& id, const QString& iconName, const QString& label, ShellPage* page, Placement placement)
{
    if (placement == Placement::Bottom && !m_hasStretch) {
        m_rail->addStretch();
        m_hasStretch = true;
    }
    m_pages.insert(id, page);
    m_stack->addWidget(page);
    m_rail->addItem(id, iconName, label);

    // Alt+1..Alt+9 in the order the pages were added, window-wide.
    const int n = m_pages.size();
    if (n <= 9) {
        const QKeySequence keys(Qt::ALT | static_cast<Qt::Key>(Qt::Key_0 + n));
        m_shortcuts.insert(id, keys);
        m_rail->setItemShortcutHint(id, keys.toString(QKeySequence::NativeText));
        auto* shortcut = new QShortcut(keys, this);
        shortcut->setContext(Qt::WindowShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, id]() { setCurrentPage(id); });
    }

    if (m_current.isEmpty())
        setCurrentPage(id);
}

void ZeroShell::finishRail()
{
    if (m_railFinished)
        return;
    m_railFinished = true;

    if (!m_hasStretch) {
        m_rail->addStretch();
        m_hasStretch = true;
    }

    auto* more = m_rail->addActionItem(QStringLiteral("more"), QStringLiteral("ellipsis"), tr("More"));
    connect(more, &QAbstractButton::clicked, this,
            [this, more]() { emit moreMenuRequested(more->mapToGlobal(QPoint(more->width() + Tokens::spaceSm, more->height()))); });

    auto* settings = m_rail->addActionItem(QStringLiteral("settings"), QStringLiteral("settings"), tr("Settings"));
    connect(settings, &QAbstractButton::clicked, this, &ZeroShell::settingsRequested);
}

void ZeroShell::setCurrentPage(const QString& id)
{
    if (!m_pages.contains(id))
        return;
    m_rail->setCurrent(id);  // emits currentChanged -> showPage (no-op when already current)
    showPage(id);
}

void ZeroShell::showPage(const QString& id)
{
    ShellPage* page = m_pages.value(id);
    if (!page || id == m_current)
        return;
    const bool first = m_current.isEmpty();
    // Page enter: snapshot overlay (old page fades out, new one rises 10 px + fades in). The real page is current and
    // interactive immediately; with motion off no overlay is created.
    const Transition::Snapshot before = first ? Transition::Snapshot() : Transition::capture(m_stack);
    m_current = id;
    m_stack->setCurrentWidget(page);
    m_topBar->showPage(page, !first);  // title / subtitle / actions crossfade
    if (!before.isNull()) {
        m_stack->layout()->activate();  // the new page needs its geometry before its picture is taken
        Transition::run(m_stack, before, Transition::Kind::Rise, Tokens::durBase);
    }
    emit pageChanged(id);
}

void ZeroShell::setAccount(const QString& name, const QString& caption, const QPixmap& face)
{
    m_topBar->accountChip()->setAccount(name, caption, face);
}

void ZeroShell::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateCollapsed();
}

void ZeroShell::updateCollapsed()
{
    m_rail->setCollapsed(width() < kCollapseBelow);
}

}  // namespace Zero
