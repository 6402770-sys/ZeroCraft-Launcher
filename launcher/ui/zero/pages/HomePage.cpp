// SPDX-License-Identifier: GPL-3.0-only
#include "HomePage.h"
#include "ui/zero/ZeroPlayButton.h"

#include <QCollator>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QPointer>
#include <QSet>
#include <QStackedLayout>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
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
#include "zero/history/SessionHistory.h"
#include "zero/session/PlayTracker.h"
#include "zero/session/ProcessMemory.h"
#include "zero/session/SessionSummary.h"
#include "HomeServersSection.h"
#include "HomeUpdatesSection.h"
#include "LastSessionCard.h"
#include "NowPlayingCard.h"
#include "minecraft/launch/MinecraftTarget.h"
#include "ui/zero/ZeroMotion.h"
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

/// 6 px status dot.
class StatusDot : public QWidget {
   public:
    StatusDot(const QColor& color, QWidget* parent) : QWidget(parent), m_color(color) { setFixedSize(6, 6); }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(m_color);
        p.drawEllipse(rect());
    }

   private:
    QColor m_color;
};

/// Instance name followed on the same baseline by the outcome text; the name keeps its width (elided only when it
/// would take the whole line), the outcome elides in what is left.
class NameAndOutcome : public QWidget {
   public:
    NameAndOutcome(ElidedLabel* name, ElidedLabel* outcome, QWidget* parent) : QWidget(parent), m_name(name), m_outcome(outcome)
    {
        name->setParent(this);
        outcome->setParent(this);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setMinimumWidth(0);
    }
    QSize sizeHint() const override { return { 100, std::max(m_name->fontMetrics().height(), m_outcome->fontMetrics().height()) }; }
    QSize minimumSizeHint() const override { return { 0, sizeHint().height() }; }

   protected:
    void resizeEvent(QResizeEvent*) override { layoutChildren(); }
    void showEvent(QShowEvent*) override { layoutChildren(); }

   private:
    void layoutChildren()
    {
        const QFontMetrics nm = m_name->fontMetrics();
        const QFontMetrics om = m_outcome->fontMetrics();
        const int baseline = (height() + nm.ascent() - nm.descent()) / 2;
        const int nameW = std::min(nm.horizontalAdvance(m_name->fullText()) + 2, width());
        m_name->setGeometry(0, baseline - nm.ascent(), nameW, nm.height());
        const int x = nameW + Tokens::spaceSm;
        m_outcome->setGeometry(x, baseline - om.ascent(), std::max(0, width() - x), om.height());
        m_outcome->setVisible(width() - x > 24);
    }

    ElidedLabel* m_name;
    ElidedLabel* m_outcome;
};

/// One clickable row of the "Recent activity" card: height 48, `surfaceHover` background while hovered, 1 px `border`
/// divider at the bottom (except for the last row).
class ActivityRow : public QWidget {
   public:
    ActivityRow(bool first, bool last, QWidget* parent) : QWidget(parent), m_first(first), m_last(last)
    {
        setFixedHeight(48);
        setAttribute(Qt::WA_Hover, true);
        setCursor(Qt::PointingHandCursor);
    }

    std::function<void()> onClick;

   protected:
    void enterEvent(QEnterEvent* event) override
    {
        QWidget::enterEvent(event);
        m_hover = true;
        update();
    }
    void leaveEvent(QEvent* event) override
    {
        QWidget::leaveEvent(event);
        m_hover = false;
        update();
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        m_pressed = event->button() == Qt::LeftButton;
        event->accept();
    }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (m_pressed && event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()) && onClick)
            onClick();
        m_pressed = false;
    }
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (m_hover) {
            // follows the card's rounded corners: only the first / last row round their outer corners
            const qreal r = std::max(0, t.radiusLg - 1);
            QPainterPath path;
            path.addRoundedRect(QRectF(rect()), r, r);
            if (!m_first)
                path.addRect(QRectF(0, 0, width(), height() / 2.0));
            if (!m_last)
                path.addRect(QRectF(0, height() / 2.0, width(), height() / 2.0));
            p.fillPath(path.simplified(), t.surfaceHover);
        }
        if (!m_last)
            p.fillRect(QRect(0, height() - 1, width(), 1), t.border);
    }

   private:
    bool m_first;
    bool m_last;
    bool m_hover = false;
    bool m_pressed = false;
};

QString activityOutcomeText(const History::Session& s)
{
    const qint64 seconds = s.durationMs() / 1000;
    const QString duration = seconds < 60 ? QString() : Format::playtime(seconds);
    switch (s.outcome) {
        case History::Outcome::Played:
            return duration.isEmpty() ? QObject::tr("Played for less than a minute") : QObject::tr("Played for %1").arg(duration);
        case History::Outcome::Stopped:
            return duration.isEmpty() ? QObject::tr("Stopped after less than a minute") : QObject::tr("Stopped after %1").arg(duration);
        case History::Outcome::Crashed: {
            QString text = duration.isEmpty() ? QObject::tr("Crashed after less than a minute") : QObject::tr("Crashed after %1").arg(duration);
            if (s.exitCode)
                text += QObject::tr(" · exit code %1").arg(*s.exitCode);
            return text;
        }
        case History::Outcome::Failed:
            return QObject::tr("Launch failed");
    }
    return {};
}

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

    // ----- now playing / last session (above the hero) -----
    m_nowHolder = new QWidget(column);
    m_nowLayout = new QVBoxLayout(m_nowHolder);
    m_nowLayout->setContentsMargins(0, 0, 0, 0);
    m_nowLayout->setSpacing(Tokens::spaceMd);
    m_nowHolder->hide();
    col->addWidget(m_nowHolder);
    m_lastHolder = new QWidget(column);
    m_lastLayout = new QVBoxLayout(m_lastHolder);
    m_lastLayout->setContentsMargins(0, 0, 0, 0);
    m_lastLayout->setSpacing(0);
    m_lastHolder->hide();
    col->addWidget(m_lastHolder);

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

    // ----- updates (mod updates of all instances; hides itself when there are none) -----
    m_updates = new HomeUpdatesSection(nullptr, {}, column);
    connect(m_updates, &HomeUpdatesSection::openInstanceRequested, this, &HomePage::modsRequested);
    col->addWidget(m_updates);

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

    // ----- favorite servers -----
    m_servers = new HomeServersSection({}, nullptr, column);
    connect(m_servers, &HomeServersSection::seeAllRequested, this, &HomePage::serversRequested);
    connect(m_servers, &HomeServersSection::joinRequested, this, [](const QString& address, const QString& instanceId) {
        auto* inst = byId(instanceId);
        if (!inst || inst->isRunning() || APPLICATION->launchPhase(instanceId) != Application::LaunchPhase::Idle)
            return;
        // exactly what the Servers page does for "Join"
        APPLICATION->launch(inst, LaunchMode::Normal, std::make_shared<MinecraftTarget>(MinecraftTarget::parse(address, false)));
    });
    col->addWidget(m_servers);

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

    // ----- recent activity (session history) -----
    m_activitySection = new QWidget(column);
    auto* activityCol = new QVBoxLayout(m_activitySection);
    activityCol->setContentsMargins(0, 0, 0, 0);
    activityCol->setSpacing(Tokens::spaceMd);
    activityCol->addWidget(makeLabel(tr("Recent activity"), Role::Heading, m_activitySection));
    m_activityCard = new ZeroCard(m_activitySection);
    m_activityCard->setHoverEnabled(false);
    m_activityRows = new QVBoxLayout(m_activityCard);
    m_activityRows->setContentsMargins(1, 1, 1, 1);  // inside the card's 1 px border
    m_activityRows->setSpacing(0);
    activityCol->addWidget(m_activityCard);
    m_activitySection->hide();
    col->addWidget(m_activitySection);

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
    if (auto* history = History::SessionHistory::global())
        connect(history, &History::SessionHistory::changed, this, &HomePage::scheduleRefresh);
    if (auto* tracker = Session::PlayTracker::global())
        connect(tracker, &Session::PlayTracker::runningChanged, this, &HomePage::syncNowPlaying);
    if (auto* store = Session::LastSessionStore::global())
        connect(store, &Session::LastSessionStore::changed, this, &HomePage::syncLastSession);
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        m_lastSignature.clear();
        syncLastSession();
        m_activitySignature.clear();
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
    m_updates->refresh();
    m_servers->activate();  // pings when the page is shown, never on a timer
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
        emit heroVideoChanged(QString());
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
    rebuildActivity();
    syncSessionCards();
}

void HomePage::syncSessionCards()
{
    syncNowPlaying();
    syncLastSession();
}

void HomePage::rebuildActivity()
{
    constexpr int kMaxRows = 6;
    QList<History::Session> shown;
    if (auto* history = History::SessionHistory::global()) {
        for (const auto& s : history->sessions()) {
            if (!byId(s.instanceId))
                continue;
            shown.append(s);
            if (shown.size() == kMaxRows)
                break;
        }
    }

    QStringList signature;
    for (const auto& s : shown) {
        auto* inst = byId(s.instanceId);
        signature << s.instanceId << QString::number(s.startedMs) << QString::number(s.endedMs) << History::outcomeToString(s.outcome)
                  << (s.exitCode ? QString::number(*s.exitCode) : QString()) << inst->name() << inst->iconKey()
                  << Format::relativeTime(s.endedMs);
    }
    if (signature == m_activitySignature)
        return;
    m_activitySignature = signature;

    while (auto* item = m_activityRows->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_activitySection->setVisible(!shown.isEmpty());

    const auto& t = Theme::current();
    // when any row has a Diagnose button every row reserves its slot, so the time column stays aligned
    const bool anyBad = std::any_of(shown.cbegin(), shown.cend(), [](const History::Session& s) {
        return s.outcome == History::Outcome::Crashed || s.outcome == History::Outcome::Failed;
    });
    for (int i = 0; i < shown.size(); ++i) {
        const History::Session& s = shown.at(i);
        auto* inst = byId(s.instanceId);
        const QString id = s.instanceId;
        const bool bad = s.outcome == History::Outcome::Crashed || s.outcome == History::Outcome::Failed;

        auto* row = new ActivityRow(i == 0, i == shown.size() - 1, m_activityCard);
        row->onClick = [this, id]() { emit openInLibraryRequested(id); };
        auto* lay = new QHBoxLayout(row);
        lay->setContentsMargins(Tokens::spaceLg, 0, Tokens::spaceLg, 0);
        lay->setSpacing(Tokens::spaceMd);

        auto* icon = new QLabel(row);
        icon->setFixedSize(20, 20);
        icon->setPixmap(APPLICATION->icons()->getIcon(inst->iconKey()).pixmap(QSize(20, 20), devicePixelRatioF()));
        icon->setScaledContents(true);
        icon->setAttribute(Qt::WA_TransparentForMouseEvents);
        lay->addWidget(icon);

        const QColor dot = s.outcome == History::Outcome::Played    ? t.success
                           : s.outcome == History::Outcome::Stopped ? t.textTertiary
                                                                    : t.danger;
        auto* dotWidget = new StatusDot(dot, row);
        dotWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
        lay->addWidget(dotWidget);

        auto* name = makeElided(Role::Label, row);
        QFont f = name->font();
        f.setWeight(QFont::DemiBold);
        name->setFont(f);
        name->setFullText(inst->name());
        name->setAttribute(Qt::WA_TransparentForMouseEvents);

        auto* outcome = makeElided(Role::Caption, row, bad ? "danger" : "secondary");
        outcome->setFullText(activityOutcomeText(s));
        outcome->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto* texts = new NameAndOutcome(name, outcome, row);
        texts->setAttribute(Qt::WA_TransparentForMouseEvents);
        lay->addWidget(texts, 1);

        auto* time = makeLabel(Format::relativeTime(s.endedMs), Role::Caption, row, "tertiary");
        time->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        time->setAttribute(Qt::WA_TransparentForMouseEvents);
        lay->addWidget(time);

        if (bad || anyBad) {
            auto* diagnose = new QPushButton(tr("Diagnose"), row);
            Style::setProperty(diagnose, "zeroVariant", "ghost");
            diagnose->setIconSize(QSize(16, 16));
            diagnose->setIcon(Icons::icon(QStringLiteral("stethoscope"), t.textSecondary));
            diagnose->setCursor(Qt::PointingHandCursor);
            const std::optional<int> exitCode = s.exitCode;
            connect(diagnose, &QPushButton::clicked, this, [this, id, exitCode]() { emit diagnoseRequested(id, exitCode); });
            lay->addWidget(diagnose);
            if (!bad) {
                QSizePolicy policy = diagnose->sizePolicy();
                policy.setRetainSizeWhenHidden(true);
                diagnose->setSizePolicy(policy);
                diagnose->hide();
            }
        }
        m_activityRows->addWidget(row);
    }
}

void HomePage::syncNowPlaying()
{
    auto* tracker = Session::PlayTracker::global();
    QSet<QString> wanted;
    if (tracker) {
        for (const auto& s : tracker->running())
            wanted.insert(s.instanceId);
    }
    // sessions that ended: the card fades out, then goes
    for (auto it = m_nowCards.begin(); it != m_nowCards.end();) {
        if (wanted.contains(it.key())) {
            ++it;
            continue;
        }
        NowPlayingCard* card = it.value();
        it = m_nowCards.erase(it);
        card->setEnabled(false);
        QPointer<HomePage> self(this);
        Motion::fadeOut(card, Tokens::durBase, [self, card]() {
            card->deleteLater();
            if (self && self->m_nowCards.isEmpty())
                self->m_nowHolder->hide();
        });
    }
    // sessions that started: the card fades in
    if (tracker) {
        for (const auto& s : tracker->running()) {
            if (auto* existing = m_nowCards.value(s.instanceId)) {
                existing->setPid(s.pid);
                continue;
            }
            auto* card = new NowPlayingCard(
                s.instanceId, s.instanceName, s.startedMs, s.pid, [tracker]() { return tracker->nowMs(); },
                [](qint64 pid) { return Session::processWorkingSetBytes(pid); }, m_nowHolder);
            if (auto* inst = byId(s.instanceId))
                card->setArtwork(Profile::artworkPath(inst), APPLICATION->icons()->getIcon(inst->iconKey()));
            connect(card, &NowPlayingCard::screenshotsRequested, this, &HomePage::screenshotsRequested);
            connect(card, &NowPlayingCard::folderRequested, this, &HomePage::folderRequested);
            connect(card, &NowPlayingCard::stopRequested, this, &HomePage::stopRequested);
            m_nowCards.insert(s.instanceId, card);
            m_nowLayout->addWidget(card);
            m_nowHolder->show();
            Motion::fadeIn(card, Tokens::durBase);
        }
    }
}

void HomePage::syncLastSession()
{
    auto* store = Session::LastSessionStore::global();
    const bool has = store && store->current() && byId(store->current()->instanceId);
    QStringList signature;
    if (has) {
        const auto& s = *store->current();
        signature << s.instanceId << QString::number(s.endedMs) << QString::number(s.worldsBackedUp) << s.screenshots.join(QLatin1Char('|'));
    }
    if (signature == m_lastSignature)
        return;
    const bool sameSession = has && !m_lastSignature.isEmpty() && m_lastSignature.mid(0, 2) == signature.mid(0, 2);
    m_lastSignature = signature;
    if (m_lastCard) {
        m_lastCard->hide();
        m_lastCard->deleteLater();
        m_lastCard = nullptr;
    }
    m_lastHolder->setVisible(has);
    if (!has)
        return;
    m_lastCard = new LastSessionCard(*store->current(), m_lastHolder);
    connect(m_lastCard, &LastSessionCard::dismissed, this, []() {
        if (auto* s = Session::LastSessionStore::global())
            s->dismiss();
    });
    connect(m_lastCard, &LastSessionCard::screenshotRequested, this, &HomePage::screenshotRequested);
    connect(m_lastCard, &LastSessionCard::moreRequested, this, &HomePage::screenshotsRequested);
    m_lastLayout->addWidget(m_lastCard);
    if (!sameSession)
        Motion::fadeIn(m_lastCard, Tokens::durBase);
}

void HomePage::refreshHero(MinecraftInstance* hero)
{
    m_heroId = hero->id();
    emit heroVideoChanged(Profile::backgroundVideoPath(hero));
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
