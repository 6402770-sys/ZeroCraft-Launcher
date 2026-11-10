// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroShell.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QStackedWidget>
#include "AccountChip.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroNavRail.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroTokens.h"

namespace Zero {

ZeroShell::ZeroShell(QWidget* parent) : QWidget(parent), m_rail(new ZeroNavRail(this)), m_chip(new AccountChip), m_stack(new QStackedWidget(this))
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_rail);
    layout->addWidget(m_stack, 1);

    connect(m_rail, &ZeroNavRail::currentChanged, this, &ZeroShell::showPage);
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void ZeroShell::paintEvent(QPaintEvent*)
{
    // Textured themes carry the stone tile behind the (transparent) pages; flat themes let the window color show.
    const auto& t = Theme::current();
    if (!t.texture)
        return;
    QPainter p(this);
    Paint::paintBackground(p, rect(), t.bg);
}

void ZeroShell::addPage(const QString& id, const QString& iconName, const QString& label, QWidget* page)
{
    m_pages.insert(id, page);
    m_stack->addWidget(page);
    m_rail->addItem(id, iconName, label);
    if (m_current.isEmpty())
        setCurrentPage(id);
}

void ZeroShell::finishRail()
{
    if (m_railFinished)
        return;
    m_railFinished = true;

    m_rail->addStretch();
    m_rail->setBottomWidget(m_chip);
    connect(m_chip, &QAbstractButton::clicked, this,
            [this]() { emit accountMenuRequested(m_chip->mapToGlobal(QPoint(m_chip->width() + Tokens::spaceSm, m_chip->height()))); });

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
    QWidget* page = m_pages.value(id);
    if (!page || id == m_current)
        return;
    const bool first = m_current.isEmpty();
    m_current = id;
    m_stack->setCurrentWidget(page);
    if (!first)
        Motion::fadeIn(page, Tokens::durPage);  // instant with reduce-motion
    emit pageChanged(id);
}

void ZeroShell::setAccount(const QString& name, const QString& caption, const QPixmap& face)
{
    m_chip->setAccount(name, caption, face);
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
