// SPDX-License-Identifier: GPL-3.0-only
#include "ToolsPage.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QStackedLayout>
#include <QVBoxLayout>
#include "CrashDetectiveView.h"
#include "ElidedLabel.h"
#include "ui/zero/ZeroCard.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {

namespace {

constexpr int kContentMaxWidth = 1080;

/// Centers `content` in a column of at most kContentMaxWidth with the standard page padding.
QWidget* centered(QWidget* content, QWidget* parent)
{
    auto* host = new QWidget(parent);
    auto* row = new QHBoxLayout(host);
    row->setContentsMargins(Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl, 0);
    row->addStretch(1);
    content->setParent(host);
    content->setMaximumWidth(kContentMaxWidth);
    row->addWidget(content, 1000);
    row->addStretch(1);
    return host;
}

}  // namespace

ToolsPage::ToolsPage(QWidget* parent) : ShellPage(parent)
{
    m_stack = new QStackedLayout(this);

    // ----- overview -----
    auto* overview = new QWidget(this);
    auto* col = new QVBoxLayout(overview);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(Tokens::spaceLg);

    auto* card = new ZeroCard(overview);
    card->setClickable(true);
    card->setMinimumHeight(96);
    card->setContentsMargins(0, 0, 0, 0);
    auto* row = new QHBoxLayout(card);
    row->setContentsMargins(Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg);
    row->setSpacing(Tokens::spaceLg);
    auto* tile = new QLabel(card);
    tile->setFixedSize(44, 44);
    tile->setProperty("zeroTile", true);
    tile->setAlignment(Qt::AlignCenter);
    auto applyIcon = [tile]() { tile->setPixmap(Icons::pixmap(QStringLiteral("stethoscope"), Theme::current().text, 22, tile->devicePixelRatioF())); };
    applyIcon();
    connect(&Theme::instance(), &Theme::changed, tile, applyIcon);
    row->addWidget(tile, 0, Qt::AlignTop);
    auto* texts = new QVBoxLayout;
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(Tokens::spaceXs);
    auto* title = new QLabel(tr("Crash Detective"), card);
    Style::applyRole(title, Role::Heading);
    texts->addWidget(title);
    auto* caption = new QLabel(tr("Reads your logs and crash reports and explains why the game stopped, with fixes you can try."), card);
    Style::applyRole(caption, Role::Caption);
    caption->setWordWrap(true);
    texts->addWidget(caption);
    texts->addStretch(1);
    row->addLayout(texts, 1);
    connect(card, &ZeroCard::clicked, this, [this]() { openCrashDetective(m_detective->currentInstanceId(), false); });
    grid->addWidget(card, 0, 0);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    col->addLayout(grid);
    col->addStretch(1);
    m_stack->addWidget(centered(overview, this));

    // ----- Crash Detective -----
    m_detective = new CrashDetectiveView(this);
    connect(m_detective, &CrashDetectiveView::instanceSettingsRequested, this, &ToolsPage::instanceSettingsRequested);
    m_stack->addWidget(centered(m_detective, this));
}

void ToolsPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!m_detective->isAnalyzing())
        m_detective->refreshInstances();
}

bool ToolsPage::isToolOpen() const
{
    return m_stack->currentIndex() == 1;
}

QString ToolsPage::title() const
{
    return isToolOpen() ? tr("Crash Detective") : tr("Tools");
}

QString ToolsPage::subtitle() const
{
    return isToolOpen() ? CrashDetectiveView::description() : tr("Utilities for keeping your instances healthy");
}

QString ToolsPage::breadcrumbParent() const
{
    return isToolOpen() ? tr("Tools") : QString();
}

QWidget* ToolsPage::topBarActions() const
{
    return isToolOpen() ? m_detective->controlsWidget() : nullptr;  // spinner, instance combo, Analyze
}

void ToolsPage::showOverview()
{
    if (m_stack->currentIndex() == 0)
        return;
    m_stack->setCurrentIndex(0);
    emit chromeChanged();
}

void ToolsPage::setPreferredInstance(const QString& instanceId)
{
    if (!m_detective->hasResult() && !m_detective->isAnalyzing() && !instanceId.isEmpty())
        m_detective->setInstance(instanceId);
}

void ToolsPage::openCrashDetective(const QString& instanceId, bool analyzeNow, std::optional<int> exitCode)
{
    m_detective->refreshInstances();
    if (!instanceId.isEmpty())
        m_detective->setInstance(instanceId);
    m_stack->setCurrentIndex(1);
    emit chromeChanged();
    if (analyzeNow)
        m_detective->analyze(exitCode);
}

}  // namespace Zero
