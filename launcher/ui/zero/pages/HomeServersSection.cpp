// SPDX-License-Identifier: GPL-3.0-only
#include "HomeServersSection.h"

#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include "Application.h"
#include "ElidedLabel.h"
#include "InstanceList.h"
#include "ServerRows.h"
#include "minecraft/MinecraftInstance.h"
#include "ui/zero/PillTag.h"
#include "ui/zero/ZeroCard.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "zero/favorites/FavoriteStore.h"
#include "zero/servers/HomeServers.h"

namespace Zero {

namespace {

QList<Servers::InstanceSource> sourcesFromApplication()
{
    QList<Servers::InstanceSource> out;
    auto* app = APPLICATION_DYN;  // no Application in unit tests
    if (!app || !app->instances())
        return out;
    auto* list = app->instances();
    for (int i = 0; i < list->count(); ++i) {
        MinecraftInstance* inst = list->at(i);
        out.append({ inst->id(), inst->name(), QDir(inst->gameRoot()).filePath(QStringLiteral("servers.dat")) });
    }
    return out;
}

PillTag::Tone toneFor(Servers::StatusTone tone)
{
    switch (tone) {
        case Servers::StatusTone::Online:
            return PillTag::Tone::Success;
        case Servers::StatusTone::Offline:
            return PillTag::Tone::Plain;
        case Servers::StatusTone::Checking:
            break;
    }
    return PillTag::Tone::Plain;
}

}  // namespace

HomeServersSection::HomeServersSection(Servers::ServerVaultScanner::SourceProvider provider, Favorites::FavoriteStore* store, QWidget* parent)
    : QWidget(parent)
    , m_scanner(new Servers::ServerVaultScanner(provider ? provider : Servers::ServerVaultScanner::SourceProvider(&sourcesFromApplication), this))
    , m_pinger(new Servers::ServerPinger(this))
    , m_store(store ? store : Favorites::FavoriteStore::instance())
{
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(Tokens::spaceMd);

    auto* head = new QHBoxLayout;
    head->setContentsMargins(0, 0, 0, 0);
    auto* title = new QLabel(tr("Servers"), this);
    Style::applyRole(title, Role::Heading);
    head->addWidget(title);
    head->addStretch(1);
    auto* seeAll = new QPushButton(tr("See all"), this);
    Style::setProperty(seeAll, "zeroVariant", "ghost");
    connect(seeAll, &QPushButton::clicked, this, &HomeServersSection::seeAllRequested);
    m_seeAll = seeAll;
    head->addWidget(seeAll);
    col->addLayout(head);

    m_card = new ZeroCard(this);
    m_card->setHoverEnabled(false);
    m_card->setContentsMargins(0, 0, 0, 0);  // the rows layout sets the padding
    m_rowsLayout = new QVBoxLayout(m_card);
    m_rowsLayout->setContentsMargins(Tokens::spaceLg, Tokens::spaceSm, Tokens::spaceLg, Tokens::spaceSm);
    m_rowsLayout->setSpacing(0);
    col->addWidget(m_card);
    hide();

    connect(m_scanner, &Servers::ServerVaultScanner::listChanged, this, [this]() {
        const auto shown = Servers::favoriteServersForHome(m_scanner->servers(), m_store ? m_store->keys(Favorites::Kind::Server) : QSet<QString>());
        QStringList addresses;
        for (const auto& s : shown)
            addresses << s.address;
        m_pinger->pingAll(addresses);  // only addresses without a result yet are asked
        rebuild();
    });
    connect(m_pinger, &Servers::ServerPinger::result, this, &HomeServersSection::rebuild);
    if (m_store) {
        connect(m_store, &Favorites::FavoriteStore::changed, this, [this](Favorites::Kind kind, const QString&, bool) {
            if (kind == Favorites::Kind::Server && m_scanner->hasScanned())
                rebuild();
        });
    }
    connect(&Theme::instance(), &Theme::changed, this, &HomeServersSection::rebuild);
}

bool HomeServersSection::seeAllVisible() const
{
    return !m_seeAll->isHidden();
}

bool HomeServersSection::isBusy(const QString& instanceId) const
{
    auto* app = APPLICATION_DYN;
    if (!app || !app->instances())
        return false;
    auto* inst = app->instances()->getInstanceById(instanceId);
    return inst && (inst->isRunning() || app->launchPhase(instanceId) != Application::LaunchPhase::Idle);
}

void HomeServersSection::activate()
{
    if (!m_store || m_store->count(Favorites::Kind::Server) == 0) {
        m_rows = 0;
        m_addresses.clear();
        hide();
        return;
    }
    m_pinger->refresh({});  // forget the old answers: the pill shows "Checking..." until the new ones arrive
    m_scanner->rescan();
}

void HomeServersSection::rebuild()
{
    while (auto* item = m_rowsLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_addresses.clear();
    const auto shown = Servers::favoriteServersForHome(m_scanner->servers(), m_store ? m_store->keys(Favorites::Kind::Server) : QSet<QString>());
    m_rows = int(shown.size());
    setVisible(!shown.isEmpty());
    m_seeAll->setVisible(true);

    for (const auto& s : shown) {
        m_addresses << s.address;
        auto* row = new QWidget(m_card);
        row->setFixedHeight(52);
        auto* lay = new QHBoxLayout(row);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(Tokens::spaceMd);

        auto* icon = new ServerIconTile(32, 18, row);
        icon->setIcon(s.iconPng);
        lay->addWidget(icon);

        auto* texts = new QVBoxLayout;
        texts->setContentsMargins(0, 0, 0, 0);
        texts->setSpacing(0);
        texts->addStretch(1);
        auto* name = new ElidedLabel(row);
        Style::applyRole(name, Role::Label);
        name->setFullText(s.displayName());
        texts->addWidget(name);
        auto* address = new ElidedLabel(row);
        Style::applyRole(address, Role::Caption);
        Style::setProperty(address, "zeroTone", "secondary");
        address->setFullText(s.address);
        texts->addWidget(address);
        texts->addStretch(1);
        lay->addLayout(texts, 1);

        const auto it = m_pinger->results().constFind(s.address);
        const Servers::PingResult* result = it != m_pinger->results().constEnd() ? &it.value() : nullptr;
        const Servers::StatusPill pill = Servers::statusPill(result);
        lay->addWidget(new PillTag(pill.text, toneFor(pill.tone), row));

        QSet<QString> busy;
        for (const auto& ref : s.instances) {
            if (isBusy(ref.id))
                busy.insert(ref.id);
        }
        const auto target = Servers::joinInstance(s, busy);
        auto* join = new QPushButton(tr("Join"), row);
        Style::setProperty(join, "zeroVariant", "secondary");
        join->setEnabled(target.has_value() && s.valid);
        if (target) {
            const QString address_ = s.address;
            const QString instanceId = target->id;
            join->setToolTip(tr("Play on this server with %1").arg(target->name));
            connect(join, &QPushButton::clicked, this, [this, address_, instanceId]() { emit joinRequested(address_, instanceId); });
        } else {
            join->setToolTip(tr("Every instance with this server is already running"));
        }
        lay->addWidget(join);
        m_rowsLayout->addWidget(row);
    }
}

}  // namespace Zero
