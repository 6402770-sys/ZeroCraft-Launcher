// SPDX-License-Identifier: GPL-3.0-only
#include "ToolsPage.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QStackedLayout>
#include <QVBoxLayout>
#include "CrashDetectiveView.h"
#include "ElidedLabel.h"
#include "JavaDoctorView.h"
#include "ModHealthView.h"
#include "PlaytimeView.h"
#include "StorageView.h"
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

    auto makeToolCard = [this, overview, grid](int index, const QString& icon, const QString& name, const QString& description) {
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
        auto applyIcon = [tile, icon]() { tile->setPixmap(Icons::pixmap(icon, Theme::current().text, 22, tile->devicePixelRatioF())); };
        applyIcon();
        connect(&Theme::instance(), &Theme::changed, tile, applyIcon);
        row->addWidget(tile, 0, Qt::AlignTop);
        auto* texts = new QVBoxLayout;
        texts->setContentsMargins(0, 0, 0, 0);
        texts->setSpacing(Tokens::spaceXs);
        auto* title = new QLabel(name, card);
        Style::applyRole(title, Role::Heading);
        texts->addWidget(title);
        auto* caption = new QLabel(description, card);
        Style::applyRole(caption, Role::Caption);
        caption->setWordWrap(true);
        texts->addWidget(caption);
        texts->addStretch(1);
        row->addLayout(texts, 1);
        grid->addWidget(card, index / 2, index % 2);
        return card;
    };
    auto* detectiveCard = makeToolCard(0, QStringLiteral("stethoscope"), tr("Crash Detective"),
                                       tr("Reads your logs and crash reports and explains why the game stopped, with fixes you can try."));
    connect(detectiveCard, &ZeroCard::clicked, this, [this]() { openCrashDetective(m_detective->currentInstanceId(), false); });
    auto* storageCard = makeToolCard(1, QStringLiteral("hard-drive"), tr("Storage"),
                                     tr("See what uses disk space across instances, worlds, backups and caches, and clean up safely."));
    connect(storageCard, &ZeroCard::clicked, this, &ToolsPage::openStorage);
    auto* javaCard = makeToolCard(2, QStringLiteral("cpu"), tr("Java Doctor"),
                                  tr("Checks which Java each instance will use, catches problems before you launch and suggests a fix."));
    connect(javaCard, &ZeroCard::clicked, this, &ToolsPage::openJavaDoctor);
    auto* modsCard = makeToolCard(3, QStringLiteral("package"), tr("Mod Health"),
                                  tr("Finds missing dependencies, conflicts, duplicates and mods made for another version — without guessing."));
    connect(modsCard, &ZeroCard::clicked, this, [this]() { openModHealth(QString()); });
    auto* playtimeCard = makeToolCard(4, QStringLiteral("clock"), tr("Playtime"), PlaytimeView::description());
    connect(playtimeCard, &ZeroCard::clicked, this, &ToolsPage::openPlaytime);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    col->addLayout(grid);
    col->addStretch(1);
    m_stack->addWidget(centered(overview, this));

    // ----- Crash Detective -----
    m_detective = new CrashDetectiveView(this);
    connect(m_detective, &CrashDetectiveView::instanceSettingsRequested, this, &ToolsPage::instanceSettingsRequested);
    connect(m_detective, &CrashDetectiveView::javaDoctorRequested, this, &ToolsPage::openJavaDoctor);
    m_stack->addWidget(centered(m_detective, this));

    // ----- Storage -----
    m_storage = new StorageView(this);
    connect(m_storage, &StorageView::showInstanceRequested, this, &ToolsPage::showInstanceRequested);
    connect(m_storage, &StorageView::showWorldsRequested, this, &ToolsPage::showWorldsRequested);
    m_stack->addWidget(centered(m_storage, this));

    // ----- Java Doctor -----
    m_java = new JavaDoctorView(this);
    connect(m_java, &JavaDoctorView::showInstanceRequested, this, &ToolsPage::showInstanceRequested);
    connect(m_java, &JavaDoctorView::javaSettingsRequested, this, &ToolsPage::javaSettingsRequested);
    m_stack->addWidget(centered(m_java, this));

    // ----- Mod Health -----
    m_mods = new ModHealthView(this);
    m_stack->addWidget(centered(m_mods, this));

    // ----- Playtime -----
    m_playtime = new PlaytimeView(this);
    connect(m_playtime, &PlaytimeView::showInstanceRequested, this, &ToolsPage::showInstanceRequested);
    m_stack->addWidget(centered(m_playtime, this));
}

void ToolsPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!m_detective->isAnalyzing())
        m_detective->refreshInstances();
}

bool ToolsPage::isToolOpen() const
{
    return m_stack->currentIndex() != 0;
}

QString ToolsPage::title() const
{
    switch (m_stack->currentIndex()) {
        case 1: return tr("Crash Detective");
        case 2: return tr("Storage");
        case 3: return tr("Java Doctor");
        case 4: return tr("Mod Health");
        case 5: return tr("Playtime");
        default: return tr("Tools");
    }
}

QString ToolsPage::subtitle() const
{
    switch (m_stack->currentIndex()) {
        case 1: return CrashDetectiveView::description();
        case 2: return StorageView::description();
        case 3: return JavaDoctorView::description();
        case 4: return ModHealthView::description();
        case 5: return PlaytimeView::description();
        default: return tr("Utilities for keeping your instances healthy");
    }
}

QString ToolsPage::breadcrumbParent() const
{
    return isToolOpen() ? tr("Tools") : QString();
}

QWidget* ToolsPage::topBarActions() const
{
    switch (m_stack->currentIndex()) {
        case 1: return m_detective->controlsWidget();  // spinner, instance combo, Analyze
        case 2: return m_storage->controlsWidget();    // Rescan
        case 3: return m_java->controlsWidget();       // Rescan
        case 4: return m_mods->controlsWidget();       // Check again
        default: return nullptr;
    }
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

void ToolsPage::openStorage()
{
    m_stack->setCurrentIndex(2);
    emit chromeChanged();
    m_storage->open();
}

void ToolsPage::openJavaDoctor()
{
    m_stack->setCurrentIndex(3);
    emit chromeChanged();
    m_java->open();
}

void ToolsPage::openModHealth(const QString& instanceId)
{
    m_stack->setCurrentIndex(4);
    emit chromeChanged();
    m_mods->open(instanceId);
}

void ToolsPage::openPlaytime()
{
    m_stack->setCurrentIndex(5);
    emit chromeChanged();
    m_playtime->open();
}

}  // namespace Zero
