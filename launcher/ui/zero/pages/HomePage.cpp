// SPDX-License-Identifier: GPL-3.0-only
#include "HomePage.h"
#include "ui/zero/ZeroPlayButton.h"

#include <QCollator>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedLayout>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include "Application.h"
#include "BaseInstance.h"
#include "ArtworkCache.h"
#include "ElidedLabel.h"
#include "IconTile.h"
#include "InstanceInfo.h"
#include "InstanceList.h"
#include "icons/IconList.h"
#include "minecraft/MinecraftInstance.h"
#include "ui/zero/ZeroCard.h"
#include "ui/zero/ZeroEmptyState.h"
#include "ui/zero/ZeroFormat.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "zero/profiles/InstanceProfile.h"

namespace Zero {

namespace {

constexpr int kRecentColumns = 6;
constexpr int kContentMaxWidth = 1280;

QLabel* makeLabel(const QString& text, Role role, QWidget* parent = nullptr, const char* tone = nullptr)
{
    auto* label = new QLabel(text, parent);
    Style::applyRole(label, role);
    if (tone)
        Style::setProperty(label, "zeroTone", tone);
    return label;
}

ElidedLabel* makeElided(Role role, QWidget* parent = nullptr, const char* tone = nullptr)
{
    auto* label = new ElidedLabel(parent);
    Style::applyRole(label, role);
    if (tone)
        Style::setProperty(label, "zeroTone", tone);
    return label;
}

/// A QLabel showing a tinted Lucide icon that follows the active theme.
QLabel* makeIconLabel(const QString& icon, int size, QWidget* parent, QColor ThemeSpec::*color)
{
    auto* label = new QLabel(parent);
    label->setFixedSize(size, size);
    label->setAlignment(Qt::AlignCenter);
    auto apply = [label, icon, size, color]() {
        label->setPixmap(Icons::pixmap(icon, Theme::current().*color, size, label->devicePixelRatioF()));
    };
    apply();
    QObject::connect(&Theme::instance(), &Theme::changed, label, apply);
    return label;
}

MinecraftInstance* byId(const QString& id)
{
    return APPLICATION->instances()->getInstanceById(id);
}

/// Small instance card of the "Recently played" row.
class RecentCard : public ZeroCard {
   public:
    RecentCard(MinecraftInstance* instance, QWidget* parent) : ZeroCard(parent)
    {
        setClickable(true);
        setFixedHeight(152);
        setContentsMargins(0, 0, 0, 0);

        const bool running = instance->isRunning();
        const auto& t = Theme::current();
        const InstanceInfo info = InstanceInfo::of(instance);

        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceMd);
        lay->setSpacing(2);

        auto* top = new QHBoxLayout;
        top->setContentsMargins(0, 0, 0, 0);
        auto* icon = new IconTile(44, 32, this);
        icon->setInstanceIcon(APPLICATION->icons()->getIcon(instance->iconKey()));
        top->addWidget(icon);
        top->addStretch(1);

        m_play = new QToolButton(this);
        m_play->setFixedSize(Tokens::controlHeight, Tokens::controlHeight);
        m_play->setIconSize(QSize(14, 14));
        m_play->setToolTip(running ? QObject::tr("Stop") : QObject::tr("Play"));
        m_play->setEnabled(running || instance->canLaunch());
        Style::setProperty(m_play, "zeroVariant", running ? "danger" : "secondary");
        m_play->setIcon(Icons::icon(running ? QStringLiteral("square") : QStringLiteral("play"), running ? t.danger : t.text));
        top->addWidget(m_play, 0, Qt::AlignTop);
        lay->addLayout(top);
        lay->addSpacing(Tokens::spaceSm);

        auto* name = makeElided(Role::Label, this);
        QFont f = name->font();
        f.setWeight(QFont::DemiBold);
        name->setFont(f);
        name->setFullText(instance->name());
        lay->addWidget(name);

        auto* caption = makeElided(Role::Caption, this);
        caption->setFullText(info.summary());
        lay->addWidget(caption);

        auto* played = makeElided(Role::Caption, this, "tertiary");
        played->setFullText(running ? QObject::tr("Running now") : Format::relativeTime(instance->lastLaunch()));
        lay->addWidget(played);
        lay->addStretch(1);
    }

    QToolButton* playButton() const { return m_play; }

   private:
    QToolButton* m_play;
};

}  // namespace

HomePage::HomePage(QWidget* parent) : ShellPage(parent), m_refreshTimer(new QTimer(this))
{
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(0);
    connect(m_refreshTimer, &QTimer::timeout, this, &HomePage::refresh);

    m_stack = new QStackedLayout(this);

    // ----- empty library -----
    auto* empty = new ZeroEmptyState(QStringLiteral("library-big"), tr("Your library is empty"),
                                     tr("Create an instance, install a modpack or import one you already have."), tr("New instance"), this);
    connect(empty, &ZeroEmptyState::actionTriggered, this, &HomePage::newInstanceRequested);
    connect(empty->addSecondaryAction(tr("Import")), &QPushButton::clicked, this, &HomePage::importRequested);
    m_stack->addWidget(empty);

    // ----- content -----
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* inner = new QWidget;
    scroll->setWidget(inner);
    inner->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    m_stack->addWidget(scroll);

    auto* outerRow = new QHBoxLayout(inner);
    outerRow->setContentsMargins(Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl);
    outerRow->addStretch(1);
    auto* column = new QWidget(inner);
    column->setMaximumWidth(kContentMaxWidth);
    outerRow->addWidget(column, 1000);
    outerRow->addStretch(1);

    auto* col = new QVBoxLayout(column);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(Tokens::spaceXxl);

    // ----- hero -----
    m_hero = new ZeroCard(column);
    m_hero->setHoverEnabled(false);
    m_hero->setMinimumHeight(200);
    auto* heroRow = new QHBoxLayout(m_hero);
    heroRow->setContentsMargins(Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl);
    heroRow->setSpacing(Tokens::spaceXl);

    m_heroIcon = new IconTile(128, 96, m_hero);
    heroRow->addWidget(m_heroIcon, 0, Qt::AlignVCenter);
    // with artwork the icon tile is replaced by a 256x144 cover-cropped artwork panel
    m_heroArt = new ArtworkView(m_hero);
    m_heroArt->setFixedSize(256, 144);
    m_heroArt->hide();
    heroRow->addWidget(m_heroArt, 0, Qt::AlignVCenter);

    auto* heroText = new QVBoxLayout;
    heroText->setContentsMargins(0, 0, 0, 0);
    heroText->setSpacing(Tokens::spaceXs);
    heroText->addStretch(1);
    heroText->addWidget(makeLabel(tr("Continue playing"), Role::Overline, m_hero));
    m_heroName = makeElided(Role::Display, m_hero);
    heroText->addWidget(m_heroName);
    m_heroMeta = makeElided(Role::Caption, m_hero);
    heroText->addWidget(m_heroMeta);
    heroText->addSpacing(Tokens::spaceLg);

    auto* buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(Tokens::spaceSm);
    m_heroPlay = new ZeroPlayButton(m_hero);
    m_heroPlay->setMinimumWidth(148);
    connect(m_heroPlay, &ZeroPlayButton::playRequested, this, &HomePage::playRequested);
    connect(m_heroPlay, &ZeroPlayButton::stopRequested, this, &HomePage::stopRequested);
    buttons->addWidget(m_heroPlay);
    m_heroEdit = new QPushButton(tr("Edit"), m_hero);
    Style::setProperty(m_heroEdit, "zeroVariant", "secondary");
    connect(m_heroEdit, &QPushButton::clicked, this, [this]() {
        if (!m_heroId.isEmpty())
            emit editRequested(m_heroId);
    });
    buttons->addWidget(m_heroEdit);
    m_heroFolder = new QPushButton(tr("Folder"), m_hero);
    Style::setProperty(m_heroFolder, "zeroVariant", "ghost");
    m_heroFolder->setIconSize(QSize(16, 16));
    connect(m_heroFolder, &QPushButton::clicked, this, [this]() {
        if (!m_heroId.isEmpty())
            emit folderRequested(m_heroId);
    });
    buttons->addWidget(m_heroFolder);
    buttons->addStretch(1);
    heroText->addLayout(buttons);
    heroText->addStretch(1);
    heroRow->addLayout(heroText, 1);
    col->addWidget(m_hero);

    // ----- favorites -----
    m_favSection = new QWidget(column);
    auto* favCol = new QVBoxLayout(m_favSection);
    favCol->setContentsMargins(0, 0, 0, 0);
    favCol->setSpacing(Tokens::spaceMd);
    favCol->addWidget(makeLabel(tr("Favorites"), Role::Heading, m_favSection));
    m_favGrid = new QGridLayout;
    m_favGrid->setContentsMargins(0, 0, 0, 0);
    m_favGrid->setSpacing(Tokens::spaceLg);
    for (int c = 0; c < kRecentColumns; ++c)
        m_favGrid->setColumnStretch(c, 1);
    favCol->addLayout(m_favGrid);
    m_favSection->hide();
    col->addWidget(m_favSection);

    // ----- recently played -----
    m_recentSection = new QWidget(column);
    auto* recentCol = new QVBoxLayout(m_recentSection);
    recentCol->setContentsMargins(0, 0, 0, 0);
    recentCol->setSpacing(Tokens::spaceMd);
    auto* recentHead = new QHBoxLayout;
    recentHead->setContentsMargins(0, 0, 0, 0);
    recentHead->addWidget(makeLabel(tr("Recently played"), Role::Heading, m_recentSection));
    recentHead->addStretch(1);
    auto* viewLibrary = new QPushButton(tr("View library"), m_recentSection);
    Style::setProperty(viewLibrary, "zeroVariant", "ghost");
    connect(viewLibrary, &QPushButton::clicked, this, &HomePage::viewLibraryRequested);
    recentHead->addWidget(viewLibrary);
    recentCol->addLayout(recentHead);
    m_recentGrid = new QGridLayout;
    m_recentGrid->setContentsMargins(0, 0, 0, 0);
    m_recentGrid->setSpacing(Tokens::spaceLg);
    for (int c = 0; c < kRecentColumns; ++c)
        m_recentGrid->setColumnStretch(c, 1);
    recentCol->addLayout(m_recentGrid);
    col->addWidget(m_recentSection);

    // ----- shortcuts -----
    auto* shortcuts = new QWidget(column);
    auto* shortcutsCol = new QVBoxLayout(shortcuts);
    shortcutsCol->setContentsMargins(0, 0, 0, 0);
    shortcutsCol->setSpacing(Tokens::spaceMd);
    shortcutsCol->addWidget(makeLabel(tr("Shortcuts"), Role::Heading, shortcuts));
    auto* tiles = new QGridLayout;
    tiles->setContentsMargins(0, 0, 0, 0);
    tiles->setSpacing(Tokens::spaceLg);
    struct Tile {
        QString icon;
        QString label;
        QString caption;
        void (HomePage::*signal)();
    };
    const Tile defs[] = {
        { QStringLiteral("plus"), tr("New instance"), tr("Start from scratch"), &HomePage::newInstanceRequested },
        { QStringLiteral("package"), tr("Import"), tr("From a file or link"), &HomePage::importRequested },
        { QStringLiteral("user"), tr("Accounts"), tr("Manage your profiles"), &HomePage::accountsRequested },
        { QStringLiteral("folder-open"), tr("Instances folder"), tr("Open in the file manager"), &HomePage::instancesFolderRequested },
    };
    int column_ = 0;
    for (const auto& def : defs) {
        auto* card = new ZeroCard(shortcuts);
        card->setClickable(true);
        card->setFixedHeight(72);
        card->setContentsMargins(0, 0, 0, 0);
        auto* row = new QHBoxLayout(card);
        row->setContentsMargins(Tokens::spaceLg, Tokens::spaceMd, Tokens::spaceLg, Tokens::spaceMd);
        row->setSpacing(Tokens::spaceMd);
        auto* iconHolder = new QLabel(card);
        iconHolder->setFixedSize(36, 36);
        iconHolder->setAlignment(Qt::AlignCenter);
        auto* holderLayout = new QHBoxLayout(iconHolder);
        holderLayout->setContentsMargins(0, 0, 0, 0);
        holderLayout->addWidget(makeIconLabel(def.icon, 18, iconHolder, &ThemeSpec::textSecondary), 0, Qt::AlignCenter);
        // elevated rounded square behind the icon
        iconHolder->setProperty("zeroTile", true);
        row->addWidget(iconHolder);
        auto* texts = new QVBoxLayout;
        texts->setContentsMargins(0, 0, 0, 0);
        texts->setSpacing(0);
        texts->addStretch(1);
        auto* title = makeElided(Role::Label, card);
        title->setFullText(def.label);
        texts->addWidget(title);
        auto* sub = makeElided(Role::Caption, card);
        sub->setFullText(def.caption);
        texts->addWidget(sub);
        texts->addStretch(1);
        row->addLayout(texts, 1);
        connect(card, &ZeroCard::clicked, this, def.signal);
        tiles->addWidget(card, 0, column_);
        tiles->setColumnStretch(column_, 1);
        ++column_;
    }
    shortcutsCol->addLayout(tiles);
    col->addWidget(shortcuts);
    col->addStretch(1);

    // live updates: instances added / removed / renamed, running state, last launch (all via the model's signals)
    auto* instances = APPLICATION->instances();
    connect(instances, &QAbstractItemModel::rowsInserted, this, &HomePage::scheduleRefresh);
    connect(instances, &QAbstractItemModel::rowsRemoved, this, &HomePage::scheduleRefresh);
    connect(instances, &QAbstractItemModel::modelReset, this, &HomePage::scheduleRefresh);
    connect(instances, &QAbstractItemModel::layoutChanged, this, &HomePage::scheduleRefresh);
    connect(instances, &QAbstractItemModel::dataChanged, this, &HomePage::scheduleRefresh);
    connect(Profile::Notifier::instance(), &Profile::Notifier::profileChanged, this, &HomePage::scheduleRefresh);
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        m_recentSignature.clear();
        m_favSignature.clear();
        refreshIcons();
        scheduleRefresh();
    });

    refreshIcons();
    refresh();
}

void HomePage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refresh();  // relative times ("2 hours ago") age while the page is hidden
}

void HomePage::scheduleRefresh()
{
    m_refreshTimer->start();
}

void HomePage::refreshIcons()
{
    const auto& t = Theme::current();
    m_heroFolder->setIcon(Icons::icon(QStringLiteral("folder-open"), t.textSecondary));
}

void HomePage::refresh()
{
    auto* list = APPLICATION->instances();
    if (list->count() == 0) {
        m_stack->setCurrentIndex(0);
        m_heroId.clear();
        m_recentSignature.clear();
        m_favSignature.clear();
        return;
    }
    m_stack->setCurrentIndex(1);

    QList<MinecraftInstance*> all;
    for (int i = 0; i < list->count(); ++i)
        all.append(list->at(i));

    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);

    QList<MinecraftInstance*> recent;
    for (auto* inst : all) {
        if (inst->lastLaunch() > 0)
            recent.append(inst);
    }
    std::sort(recent.begin(), recent.end(), [&](MinecraftInstance* a, MinecraftInstance* b) {
        if (a->lastLaunch() != b->lastLaunch())
            return a->lastLaunch() > b->lastLaunch();
        return collator.compare(a->name(), b->name()) < 0;
    });

    MinecraftInstance* hero = nullptr;
    if (!recent.isEmpty()) {
        hero = recent.first();
    } else {
        hero = *std::min_element(all.begin(), all.end(),
                                 [&](MinecraftInstance* a, MinecraftInstance* b) { return collator.compare(a->name(), b->name()) < 0; });
    }
    refreshHero(hero);

    // favorites: sorted by last played like the recent row (never-played ones by name after them)
    QList<MinecraftInstance*> favorites;
    for (auto* inst : all) {
        if (Profile::isFavorite(inst))
            favorites.append(inst);
    }
    std::sort(favorites.begin(), favorites.end(), [&](MinecraftInstance* a, MinecraftInstance* b) {
        if (a->lastLaunch() != b->lastLaunch())
            return a->lastLaunch() > b->lastLaunch();
        return collator.compare(a->name(), b->name()) < 0;
    });
    if (favorites.size() > kRecentColumns)
        favorites = favorites.mid(0, kRecentColumns);
    rebuildCards(m_favSection, m_favGrid, m_favSignature, favorites);

    if (recent.size() > kRecentColumns)
        recent = recent.mid(0, kRecentColumns);
    rebuildCards(m_recentSection, m_recentGrid, m_recentSignature, recent);
}

void HomePage::refreshHero(MinecraftInstance* hero)
{
    m_heroId = hero->id();
    m_heroIcon->setInstanceIcon(APPLICATION->icons()->getIcon(hero->iconKey()));
    const QString artwork = Profile::artworkPath(hero);
    m_heroArt->setArtwork(artwork);
    m_heroArt->setVisible(!artwork.isEmpty());
    m_heroIcon->setVisible(artwork.isEmpty());
    m_heroName->setFullText(hero->name());

    QStringList meta;
    const InstanceInfo info = InstanceInfo::of(hero);
    if (!info.summary().isEmpty())
        meta << info.summary();
    if (hero->lastLaunch() > 0)
        meta << tr("Played %1").arg(Format::relativeTime(hero->lastLaunch()));
    else
        meta << tr("Never played");
    if (hero->totalTimePlayed() > 0)
        meta << Format::playtime(hero->totalTimePlayed());
    m_heroMeta->setFullText(meta.join(QStringLiteral(" · ")));

    m_heroPlay->setInstanceId(m_heroId);
    m_heroPlay->setCanLaunch(hero->canLaunch());
    m_heroEdit->setEnabled(hero->canEdit());
}

void HomePage::rebuildCards(QWidget* section, QGridLayout* grid, QStringList& signatureRef, const QList<MinecraftInstance*>& instances)
{
    QStringList signature;
    for (auto* inst : instances) {
        signature << inst->id() << inst->name() << inst->iconKey() << (inst->isRunning() ? "1" : "0") << (inst->canLaunch() ? "1" : "0")
                  << InstanceInfo::of(inst).summary() << Format::relativeTime(inst->lastLaunch());
    }
    if (signature == signatureRef)
        return;
    signatureRef = signature;

    while (auto* item = grid->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    section->setVisible(!instances.isEmpty());

    int column = 0;
    for (auto* inst : instances) {
        auto* card = new RecentCard(inst, section);
        const QString id = inst->id();
        const bool running = inst->isRunning();
        connect(card, &ZeroCard::clicked, this, [this, id]() { emit openInLibraryRequested(id); });
        connect(card->playButton(), &QToolButton::clicked, this, [this, id, running]() {
            if (running)
                emit stopRequested(id);
            else
                emit playRequested(id);
        });
        grid->addWidget(card, 0, column++);
    }
    // empty placeholders keep every column (and its spacing) in the layout, so a short row has the same card width as a full one
    for (; column < kRecentColumns; ++column)
        grid->addWidget(new QWidget(section), 0, column);
}

}  // namespace Zero
