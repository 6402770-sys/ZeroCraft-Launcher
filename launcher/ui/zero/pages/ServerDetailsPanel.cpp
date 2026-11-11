// SPDX-License-Identifier: GPL-3.0-only
#include "ServerDetailsPanel.h"

#include <QClipboard>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedLayout>
#include <QToolButton>
#include <QVBoxLayout>
#include "ElidedLabel.h"
#include "ServerRows.h"
#include "ui/zero/PillTag.h"
#include "ui/zero/ZeroEmptyState.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroToast.h"

namespace Zero {

namespace {
QLabel* makeLabel(const QString& text, Role role, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);  // everything a server sends is shown as plain text
    Style::applyRole(label, role);
    return label;
}

QLabel* makeWrapped(Role role, Qt::Alignment alignment, QWidget* parent)
{
    auto* label = makeLabel({}, role, parent);
    label->setWordWrap(true);
    label->setAlignment(alignment);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    return label;
}

QPushButton* makeAction(const QString& text, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    Style::setProperty(button, "zeroVariant", "ghost");
    button->setStyleSheet(QStringLiteral("text-align: left;"));
    button->setIconSize(QSize(16, 16));
    return button;
}
}  // namespace

ServerDetailsPanel::ServerDetailsPanel(QWidget* parent) : ZeroCard(parent)
{
    setHoverEnabled(false);
    setFixedWidth(kWidth);
    setContentsMargins(0, 0, 0, 0);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(1, 1, 1, 1);
    m_stack = new QStackedLayout;
    outer->addLayout(m_stack);

    m_stack->addWidget(new ZeroEmptyState(QStringLiteral("compass"), tr("Select a server"), tr("Pick a server to see its details."), {}, this));

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

    m_icon = new ServerIconTile(96, 40, content);
    lay->addWidget(m_icon, 0, Qt::AlignHCenter);
    lay->addSpacing(Tokens::spaceMd);

    m_name = new ElidedLabel(content);
    m_name->setTextFormat(Qt::PlainText);
    Style::applyRole(m_name, Role::Title);
    m_name->setAlignment(Qt::AlignCenter);
    lay->addWidget(m_name);
    lay->addSpacing(2);

    // address (monospace in every theme: no role) + copy
    m_address = new ElidedLabel(content);
    m_address->setTextFormat(Qt::PlainText);
    m_address->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPixelSize(Tokens::body.px);
    m_address->setFont(mono);
    m_copy = new QToolButton(content);
    m_copy->setAutoRaise(false);
    m_copy->setFixedSize(22, 22);
    m_copy->setIconSize(QSize(14, 14));
    m_copy->setStyleSheet(QStringLiteral("QToolButton { padding: 0px; }"));  // the ghost padding would squeeze the icon
    m_copy->setCursor(Qt::PointingHandCursor);
    m_copy->setToolTip(tr("Copy address"));
    Style::setProperty(m_copy, "zeroVariant", "ghost");
    connect(m_copy, &QToolButton::clicked, this, &ServerDetailsPanel::copyAddress);
    auto* addressRow = new QHBoxLayout;
    addressRow->setContentsMargins(0, 0, 0, 0);
    addressRow->setSpacing(Tokens::spaceXs);
    addressRow->addWidget(m_address, 1);
    addressRow->addWidget(m_copy);
    lay->addLayout(addressRow);
    lay->addSpacing(Tokens::spaceMd);

    // status pill + the disclaimer
    m_status = new PillTag(tr("Pinging…"), PillTag::Tone::Plain, content);
    lay->addWidget(m_status, 0, Qt::AlignHCenter);
    lay->addSpacing(Tokens::spaceSm);
    m_disclaimer = makeWrapped(Role::Caption, Qt::AlignHCenter | Qt::AlignTop, content);
    m_disclaimer->setText(serverStatusDisclaimer());
    lay->addWidget(m_disclaimer);
    lay->addSpacing(Tokens::spaceLg);

    // stats rows (same grid as the other details panels)
    auto* stats = new QGridLayout;
    stats->setContentsMargins(0, 0, 0, 0);
    stats->setHorizontalSpacing(Tokens::spaceMd);
    stats->setVerticalSpacing(Tokens::spaceSm);
    stats->setColumnStretch(1, 1);
    m_players = makeLabel({}, Role::Body, content);
    m_latency = makeLabel({}, Role::Body, content);
    m_version = new ElidedLabel(content);
    m_version->setTextFormat(Qt::PlainText);
    Style::applyRole(m_version, Role::Body);
    const struct {
        QString caption;
        QWidget* value;
    } rows[] = { { tr("Players"), m_players }, { tr("Latency"), m_latency }, { tr("Version"), m_version } };
    int row = 0;
    for (const auto& r : rows) {
        stats->addWidget(makeLabel(r.caption, Role::Caption, content), row, 0, Qt::AlignVCenter);
        auto* label = static_cast<QLabel*>(r.value);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        stats->addWidget(label, row, 1);
        ++row;
    }
    lay->addLayout(stats);
    lay->addSpacing(Tokens::spaceLg);

    // MOTD
    m_motdBlock = new QWidget(content);
    auto* motdLay = new QVBoxLayout(m_motdBlock);
    motdLay->setContentsMargins(0, 0, 0, 0);
    motdLay->setSpacing(Tokens::spaceXs);
    motdLay->addWidget(makeLabel(tr("MOTD"), Role::Caption, m_motdBlock));
    m_motd = makeWrapped(Role::Body, Qt::AlignLeft | Qt::AlignTop, m_motdBlock);
    motdLay->addWidget(m_motd);
    motdLay->addSpacing(Tokens::spaceLg);
    lay->addWidget(m_motdBlock);

    // instances
    m_instancesBlock = new QWidget(content);
    auto* instancesOuter = new QVBoxLayout(m_instancesBlock);
    instancesOuter->setContentsMargins(0, 0, 0, 0);
    instancesOuter->setSpacing(Tokens::spaceXs);
    instancesOuter->addWidget(makeLabel(tr("Saved in"), Role::Caption, m_instancesBlock));
    m_instances = new QVBoxLayout;
    m_instances->setContentsMargins(0, 0, 0, 0);
    m_instances->setSpacing(2);
    instancesOuter->addLayout(m_instances);
    instancesOuter->addSpacing(Tokens::spaceLg);
    lay->addWidget(m_instancesBlock);

    // actions
    m_play = new QPushButton(tr("Play on server"), content);
    Style::setProperty(m_play, "zeroVariant", "primary");
    m_play->setIconSize(QSize(16, 16));
    connect(m_play, &QPushButton::clicked, this, [this]() { chooseInstance(true); });
    lay->addWidget(m_play);
    lay->addSpacing(Tokens::spaceSm);
    m_showInstance = makeAction(tr("Show instance in Library"), content);
    connect(m_showInstance, &QPushButton::clicked, this, [this]() { chooseInstance(false); });
    lay->addWidget(m_showInstance);
    lay->addStretch(1);

    connect(&Theme::instance(), &Theme::changed, this, &ServerDetailsPanel::refreshIcons);
    refreshIcons();
    clear();
}

void ServerDetailsPanel::refreshIcons()
{
    const auto& t = Theme::current();
    m_play->setIcon(Icons::icon(QStringLiteral("play"), t.onAccent));
    m_showInstance->setIcon(Icons::icon(QStringLiteral("library-big"), t.textSecondary));
    m_copy->setIcon(Icons::icon(QStringLiteral("copy"), t.textSecondary));
}

void ServerDetailsPanel::clear()
{
    m_server = {};
    m_stack->setCurrentIndex(0);
}

void ServerDetailsPanel::setBusyInstances(const QSet<QString>& ids)
{
    if (ids == m_busy)
        return;
    m_busy = ids;
    refreshActions();
}

void ServerDetailsPanel::setServer(const Servers::VaultServer& server, const Servers::PingResult* result, bool pending)
{
    bool sameServer = server.instances.size() == m_server.instances.size();
    for (qsizetype i = 0; sameServer && i < server.instances.size(); ++i)
        sameServer = server.instances.at(i).id == m_server.instances.at(i).id && server.instances.at(i).name == m_server.instances.at(i).name;
    m_server = server;

    m_icon->setIcon(server.iconPng);
    m_name->setFullText(server.displayName());
    m_address->setFullText(server.address);

    using State = Servers::PingResult::State;
    if (result) {
        switch (result->state) {
            case State::Responded:
                m_status->setText(tr("Responded"));
                m_status->setTone(PillTag::Tone::Success);
                break;
            case State::NoResponse:
                m_status->setText(tr("No response"));
                m_status->setTone(PillTag::Tone::Plain);
                break;
            case State::InvalidAddress:
                m_status->setText(tr("Invalid address"));
                m_status->setTone(PillTag::Tone::Warning);
                break;
        }
    } else {
        m_status->setText(pending ? tr("Pinging…") : tr("Not checked"));
        m_status->setTone(PillTag::Tone::Plain);
    }

    const bool responded = result && result->state == State::Responded;
    const QString unknown = tr("Unknown");
    const QString none = QStringLiteral("—");
    m_players->setText(responded ? (playersText(*result).isEmpty() ? unknown : playersText(*result)) : none);
    m_latency->setText(responded && result->latencyMs >= 0 ? latencyText(*result) : none);
    m_version->setFullText(responded ? (result->versionName.isEmpty() ? unknown : result->versionName) : none);
    const QString motd = responded ? result->motd : QString();
    m_motd->setText(motd);
    m_motdBlock->setVisible(!motd.isEmpty());

    if (!sameServer) {
        while (QLayoutItem* item = m_instances->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        for (const auto& ref : server.instances) {
            auto* label = new ElidedLabel(m_instancesBlock);
            label->setTextFormat(Qt::PlainText);
            Style::applyRole(label, Role::Body);
            label->setFullText(ref.name);
            m_instances->addWidget(label);
        }
    }
    refreshActions();
    m_stack->setCurrentIndex(1);
}

void ServerDetailsPanel::refreshActions()
{
    if (!hasServer())
        return;
    QStringList freeInstances;
    for (const auto& ref : m_server.instances) {
        if (!m_busy.contains(ref.id))
            freeInstances << ref.id;
    }
    m_showInstance->setEnabled(!m_server.instances.isEmpty());
    if (!m_server.valid) {
        m_play->setEnabled(false);
        m_play->setToolTip(tr("This address isn't valid, so the game can't join it."));
    } else if (m_server.instances.isEmpty()) {
        m_play->setEnabled(false);
        m_play->setToolTip(QString());
    } else if (freeInstances.isEmpty()) {
        m_play->setEnabled(false);
        m_play->setToolTip(m_server.instances.size() == 1 ? tr("%1 is running. Close it first.").arg(m_server.instances.first().name)
                                                          : tr("Every instance with this server is running."));
    } else {
        m_play->setEnabled(true);
        m_play->setToolTip(m_server.instances.size() == 1 ? tr("Launch %1 and join this server").arg(m_server.instances.first().name)
                                                          : tr("Pick an instance, then launch it and join this server"));
    }
}

void ServerDetailsPanel::chooseInstance(bool play)
{
    if (!hasServer() || m_server.instances.isEmpty())
        return;
    auto trigger = [this, play](const QString& instanceId) {
        if (play)
            emit playRequested(m_server.address, instanceId);
        else
            emit showInstanceRequested(instanceId);
    };
    if (m_server.instances.size() == 1) {
        trigger(m_server.instances.first().id);
        return;
    }
    QPushButton* button = play ? m_play : m_showInstance;
    QMenu menu(this);
    for (const auto& ref : m_server.instances) {
        const bool busy = play && m_busy.contains(ref.id);
        QAction* action = menu.addAction(busy ? tr("%1 (running)").arg(ref.name) : ref.name);
        action->setEnabled(!busy);
        const QString id = ref.id;
        connect(action, &QAction::triggered, this, [trigger, id]() { trigger(id); });
    }
    menu.exec(button->mapToGlobal(QPoint(0, button->height())));
}

void ServerDetailsPanel::copyAddress()
{
    if (!hasServer())
        return;
    QGuiApplication::clipboard()->setText(m_server.address);
    ZeroToast::show(window(), tr("Address copied"), ZeroToast::Kind::Success);
}

}  // namespace Zero
