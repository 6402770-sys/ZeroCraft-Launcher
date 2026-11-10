// SPDX-License-Identifier: GPL-3.0-only
#include "InstanceDetailsPanel.h"

#include <QAction>
#include <QEvent>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedLayout>
#include <QVBoxLayout>
#include "Application.h"
#include "BaseInstance.h"
#include "InstanceInfo.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "ElidedLabel.h"
#include "IconTile.h"
#include "icons/IconList.h"
#include "ui/zero/ZeroEmptyState.h"
#include "ui/zero/ZeroFormat.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {

namespace {
QLabel* makeLabel(const QString& text, Role role, QWidget* parent, const char* tone = nullptr)
{
    auto* label = new QLabel(text, parent);
    Style::applyRole(label, role);
    if (tone)
        Style::setProperty(label, "zeroTone", tone);
    return label;
}
}  // namespace

InstanceDetailsPanel::InstanceDetailsPanel(QWidget* parent) : ZeroCard(parent)
{
    setHoverEnabled(false);
    setFixedWidth(kWidth);
    setContentsMargins(0, 0, 0, 0);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(1, 1, 1, 1);
    m_stack = new QStackedLayout;
    outer->addLayout(m_stack);

    m_stack->addWidget(new ZeroEmptyState(QStringLiteral("box"), tr("Select an instance"),
                                          tr("Pick an instance to see its details and actions."), {}, this));

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget;
    scroll->setWidget(content);
    content->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    m_stack->addWidget(scroll);

    auto* lay = new QVBoxLayout(content);
    lay->setContentsMargins(Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceSm);
    lay->setSpacing(0);

    m_icon = new IconTile(120, 96, content);
    m_icon->setClickable(true);
    m_icon->setToolTip(tr("Change icon"));
    connect(m_icon, &QAbstractButton::clicked, this, &InstanceDetailsPanel::changeIconRequested);
    lay->addWidget(m_icon, 0, Qt::AlignHCenter);
    lay->addSpacing(Tokens::spaceMd);

    m_name = new ElidedLabel(content);
    Style::applyRole(m_name, Role::Title);
    m_name->setAlignment(Qt::AlignCenter);
    m_name->installEventFilter(this);
    lay->addWidget(m_name);

    m_nameEdit = new QLineEdit(content);
    m_nameEdit->setAlignment(Qt::AlignCenter);
    m_nameEdit->setMaxLength(128);
    m_nameEdit->hide();
    m_nameEdit->installEventFilter(this);
    lay->addWidget(m_nameEdit);

    m_meta = makeLabel({}, Role::Caption, content);
    m_meta->setAlignment(Qt::AlignCenter);
    lay->addSpacing(2);
    lay->addWidget(m_meta);

    m_running = new QLabel(tr("● Running"), content);
    Style::setProperty(m_running, "zeroBadge", "success");
    m_running->hide();
    lay->addSpacing(Tokens::spaceSm);
    lay->addWidget(m_running, 0, Qt::AlignHCenter);
    lay->addSpacing(Tokens::spaceMd);

    // stats rows
    auto* stats = new QGridLayout;
    stats->setContentsMargins(0, 0, 0, 0);
    stats->setHorizontalSpacing(Tokens::spaceMd);
    stats->setVerticalSpacing(Tokens::spaceSm);
    stats->setColumnStretch(1, 1);
    m_lastPlayed = makeLabel({}, Role::Body, content);
    m_playtime = makeLabel({}, Role::Body, content);
    m_group = new ElidedLabel(content);
    Style::applyRole(m_group, Role::Body);
    const struct {
        QString caption;
        QLabel* value;
    } rows[] = { { tr("Last played"), m_lastPlayed }, { tr("Total playtime"), m_playtime }, { tr("Group"), m_group } };
    int row = 0;
    for (const auto& r : rows) {
        stats->addWidget(makeLabel(r.caption, Role::Caption, content), row, 0);
        r.value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        r.value->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        stats->addWidget(r.value, row, 1);
        ++row;
    }
    lay->addLayout(stats);
    lay->addSpacing(Tokens::spaceMd);

    // Play / Stop
    m_play = new QPushButton(tr("Play"), content);
    Style::setProperty(m_play, "zeroVariant", "primary");
    Style::setProperty(m_play, "zeroSize", "lg");
    m_play->setIconSize(QSize(16, 16));
    connect(m_play, &QPushButton::clicked, this, [this]() {
        if (m_isRunning) {
            if (m_actions.kill)
                m_actions.kill->trigger();
        } else if (m_actions.launch) {
            m_actions.launch->trigger();
        }
    });
    lay->addWidget(m_play);
    lay->addSpacing(Tokens::spaceSm);

    // action list is created in setActions() (needs the actions)
    lay->addStretch(1);
    m_actionHost = lay;

    connect(&Theme::instance(), &Theme::changed, this, &InstanceDetailsPanel::refreshIcons);
    refreshPlayButton(false, false);
    setInstance(nullptr);
}

QPushButton* InstanceDetailsPanel::addActionButton(QWidget* parent, const QString& icon, const QString& text, QAction* action, bool danger)
{
    auto* button = new QPushButton(text, parent);
    Style::setProperty(button, "zeroVariant", danger ? "danger" : "ghost");
    button->setStyleSheet(QStringLiteral("text-align: left;"));
    button->setIconSize(QSize(16, 16));
    if (action)
        connect(button, &QPushButton::clicked, action, &QAction::trigger);
    m_bindings.append({ action, button });
    m_iconSpecs.append({ button, icon, danger });
    return button;
}

void InstanceDetailsPanel::setActions(const LibraryActions& actions)
{
    m_actions = actions;
    auto* lay = qobject_cast<QVBoxLayout*>(m_actionHost);
    auto* parent = lay->parentWidget();
    // insert before the trailing stretch
    const int at = lay->count() - 1;
    int i = at;
    auto insert = [&](QPushButton* b) { lay->insertWidget(i++, b); };
    insert(addActionButton(parent, QStringLiteral("pencil"), tr("Edit"), actions.edit));
    insert(addActionButton(parent, QStringLiteral("folder-open"), tr("Open folder"), actions.folder));
    insert(addActionButton(parent, QStringLiteral("list-filter"), tr("Change group"), actions.group));
    insert(addActionButton(parent, QStringLiteral("copy"), tr("Copy"), actions.copy));
    auto* exportButton = addActionButton(parent, QStringLiteral("archive"), tr("Export"), nullptr);
    if (actions.exportInstance && actions.exportInstance->menu())
        exportButton->setMenu(actions.exportInstance->menu());
    m_bindings.last().action = actions.exportInstance;
    insert(exportButton);
    insert(addActionButton(parent, QStringLiteral("terminal"), tr("Create shortcut"), actions.shortcut));
    insert(addActionButton(parent, QStringLiteral("trash-2"), tr("Delete"), actions.remove, true));

    for (QAction* a : { actions.launch, actions.kill, actions.edit, actions.folder, actions.group, actions.copy, actions.exportInstance,
                        actions.shortcut, actions.remove }) {
        if (a)
            connect(a, &QAction::changed, this, &InstanceDetailsPanel::syncButtons);
    }
    refreshIcons();
    syncButtons();
}

void InstanceDetailsPanel::refreshIcons()
{
    const auto& t = Theme::current();
    for (const auto& spec : m_iconSpecs)
        spec.button->setIcon(Icons::icon(spec.icon, spec.danger ? t.danger : t.textSecondary));
    m_play->setIcon(Icons::icon(m_isRunning ? QStringLiteral("square") : QStringLiteral("play"), m_isRunning ? t.danger : t.onAccent));
}

void InstanceDetailsPanel::refreshPlayButton(bool running, bool canLaunch)
{
    const auto& t = Theme::current();
    if (running != m_isRunning || m_play->property("zeroVariant").toString().isEmpty() || m_play->text().isEmpty()) {
        m_isRunning = running;
        m_play->setText(running ? tr("Stop") : tr("Play"));
        Style::setProperty(m_play, "zeroVariant", running ? "danger" : "primary");
        m_play->setIcon(Icons::icon(running ? QStringLiteral("square") : QStringLiteral("play"), running ? t.danger : t.onAccent));
    }
    const bool enabled = running ? (m_actions.kill && m_actions.kill->isEnabled()) : (canLaunch && m_actions.launch && m_actions.launch->isEnabled());
    m_play->setEnabled(enabled);
}

void InstanceDetailsPanel::syncButtons()
{
    for (const auto& b : std::as_const(m_bindings)) {
        if (b.action)
            b.button->setEnabled(b.action->isEnabled());
    }
    if (m_instance)
        refreshPlayButton(m_instance->isRunning(), m_instance->canLaunch());
}

void InstanceDetailsPanel::setInstance(MinecraftInstance* instance)
{
    m_instance = instance;
    if (!instance) {
        cancelRename();
        m_stack->setCurrentIndex(0);
        return;
    }
    m_stack->setCurrentIndex(1);

    m_icon->setInstanceIcon(APPLICATION->icons()->getIcon(instance->iconKey()));
    m_nameText = instance->name();
    m_name->setFullText(m_nameText);

    const InstanceInfo info = InstanceInfo::of(instance);
    m_meta->setText(info.summary());
    m_meta->setVisible(!info.summary().isEmpty());

    const bool running = instance->isRunning();
    m_running->setVisible(running);
    m_lastPlayed->setText(Format::relativeTime(instance->lastLaunch()));
    m_playtime->setText(Format::playtime(instance->totalTimePlayed()));
    const QString group = APPLICATION->instances()->getInstanceGroup(instance->id());
    m_group->setFullText(group.isEmpty() ? QStringLiteral("—") : group);

    refreshPlayButton(running, instance->canLaunch());
}

void InstanceDetailsPanel::beginRename()
{
    if (!m_instance || m_renaming)
        return;
    m_renaming = true;
    m_nameEdit->setText(m_nameText);
    m_name->hide();
    m_nameEdit->show();
    m_nameEdit->setFocus();
    m_nameEdit->selectAll();
}

void InstanceDetailsPanel::cancelRename()
{
    if (!m_renaming)
        return;
    m_renaming = false;
    m_nameEdit->hide();
    m_name->show();
}

void InstanceDetailsPanel::commitRename()
{
    if (!m_renaming)
        return;
    const QString text = m_nameEdit->text().trimmed();
    cancelRename();
    if (!text.isEmpty() && text != m_nameText)
        emit renameRequested(text);
}

bool InstanceDetailsPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_name && event->type() == QEvent::MouseButtonDblClick) {
        beginRename();
        return true;
    }
    if (watched == m_nameEdit) {
        if (event->type() == QEvent::KeyPress) {
            auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                commitRename();
                return true;
            }
            if (key->key() == Qt::Key_Escape) {
                cancelRename();
                return true;
            }
        } else if (event->type() == QEvent::FocusOut) {
            commitRename();
        }
    }
    return ZeroCard::eventFilter(watched, event);
}

}  // namespace Zero
