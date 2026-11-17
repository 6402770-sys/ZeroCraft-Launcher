// SPDX-License-Identifier: GPL-3.0-only
#include "TimelinePage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedLayout>
#include <QVBoxLayout>

#include "Application.h"
#include "BaseInstance.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "ui/zero/ZeroEmptyState.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroTokens.h"
#include "ui/zero/instance/ModsPage.h"
#include "ui/zero/instance/ResourcePacksPage.h"
#include "zero/downloads/DownloadCenter.h"

namespace Zero {
namespace TimelineUi {

using History::Timeline::Entry;
using History::Timeline::Kind;
using History::Timeline::Tone;

namespace {

constexpr int kIconSize = 16;
constexpr int kButtonHeight = 28;  // small buttons get their height in code (obsidian.qss, zeroSize "sm")
constexpr int kRowMinHeight = 44;

QString iconNameFor(const Entry& e)
{
    switch (e.kind) {
        case Kind::ModsUpdate:
            return QStringLiteral("refresh-cw");
        case Kind::ModpackUpdate:
            return QStringLiteral("layers");
        case Kind::BringBack:
            return QStringLiteral("arrow-left");
        case Kind::PackOrder:
            return QStringLiteral("arrow-up-down");
        case Kind::Download:
            return QStringLiteral("download");
        case Kind::Session:
            return (e.tone == Tone::Error || e.tone == Tone::Warning) ? QStringLiteral("triangle-alert") : QStringLiteral("play");
    }
    return QStringLiteral("clock");
}

QColor colorFor(Tone tone)
{
    const auto& t = Theme::current();
    switch (tone) {
        case Tone::Success:
            return t.success;
        case Tone::Warning:
            return t.warning;
        case Tone::Error:
            return t.danger;
        case Tone::Muted:
            return t.textTertiary;
        case Tone::Normal:
            break;
    }
    return t.textSecondary;
}

}  // namespace

Requests* Requests::instance()
{
    static Requests* requests = new Requests();  // lives for the whole process
    return requests;
}

// ---- environment of the application page -------------------------------------------------------------------------------------------

namespace {

TimelineEnvironment environmentFor(MinecraftInstance* inst, ModsUi::ModsPage* mods, PacksUi::ResourcePacksPage* packs)
{
    QPointer<MinecraftInstance> guarded(inst);
    QPointer<ModsUi::ModsPage> modsPage(mods);
    QPointer<PacksUi::ResourcePacksPage> packsPage(packs);
    const QString id = inst->id();
    TimelineEnvironment env;
    env.read = [id, guarded]() {
        History::Timeline::Locations where;
        where.dataRoot = APPLICATION->dataRoot();
        where.instanceId = id;
        if (guarded) {
            where.instanceRoot = guarded->instanceRoot();
            where.packId = guarded->getManagedPackID();
        }
        return History::Timeline::readSources(where, APPLICATION->zeroDownloads(), History::SessionHistory::global());
    };
    env.busyReason = [id]() -> QString {
        auto* instance = APPLICATION->instances()->getInstanceById(id);
        if (APPLICATION->launchPhase(id) != Application::LaunchPhase::Idle || (instance && instance->isRunning()))
            return QObject::tr("Close the game first.");
        if (auto* downloads = APPLICATION->zeroDownloads(); downloads && downloads->isBusy(id))
            return Downloads::instanceBusyText();
        return {};
    };
    env.undo = [id, modsPage, packsPage](const Entry& entry) {
        switch (entry.kind) {
            case Kind::ModsUpdate:
                if (modsPage)
                    modsPage->requestRollback(entry.undoRef);
                break;
            case Kind::PackOrder:
                if (packsPage)
                    packsPage->undo(entry.undoRef);
                break;
            case Kind::ModpackUpdate:
            case Kind::BringBack:
                emit Requests::instance()->undoModpackRequested(id);
                break;
            default:
                break;
        }
    };
    return env;
}

}  // namespace

TimelinePage::TimelinePage(MinecraftInstance* inst, ModsUi::ModsPage* mods, PacksUi::ResourcePacksPage* packs, QWidget* parent)
    : TimelinePage(environmentFor(inst, mods, packs), parent)
{
    // The game ended (or started): the sessions and the Undo state changed.
    connect(inst, &BaseInstance::runningStatusChanged, this, [this]() { scheduleRefresh(); });
    if (auto* history = History::SessionHistory::global())
        connect(history, &History::SessionHistory::changed, this, [this]() { scheduleRefresh(); });
    if (auto* downloads = APPLICATION->zeroDownloads())
        connect(downloads, &Downloads::DownloadCenter::historyChanged, this, [this]() { scheduleRefresh(); });
}

TimelinePage::TimelinePage(const TimelineEnvironment& env, QWidget* parent) : QWidget(parent), m_env(env)
{
    setObjectName(QStringLiteral("zeroTimelinePage"));
    if (!m_env.now)
        m_env.now = []() { return QDateTime::currentDateTime(); };
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(0);
    connect(&m_refreshTimer, &QTimer::timeout, this, [this]() {
        if (isVisible())
            refresh();
        else
            m_dirty = true;
    });
    build();
    connect(&Theme::instance(), &Theme::changed, this, [this]() { scheduleRefresh(); });
}

TimelinePage::~TimelinePage() = default;

void TimelinePage::build()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    m_stack = new QStackedLayout;
    root->addLayout(m_stack, 1);

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("zeroTimelineScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_stack->addWidget(m_scroll);

    m_empty = new ZeroEmptyState(QStringLiteral("clock"), tr("No changes yet"),
                                 tr("Updates, installs and pack order changes show up here."), {}, this);
    m_stack->addWidget(m_empty);
}

void TimelinePage::openedImpl()
{
    m_limit = History::Timeline::kShownFirst;
    refresh();
}

void TimelinePage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_dirty)
        refresh();
}

void TimelinePage::scheduleRefresh()
{
    m_refreshTimer.start();
}

void TimelinePage::refresh()
{
    m_dirty = false;
    rebuild();
}

void TimelinePage::rebuild()
{
    const History::Timeline::Sources sources = m_env.read ? m_env.read() : History::Timeline::Sources();
    const QList<Entry> all = History::Timeline::build(sources);
    m_total = int(all.size());
    const QString busy = m_env.busyReason ? m_env.busyReason() : QString();
    const QDateTime now = m_env.now();

    const int scrollPos = m_scroll->verticalScrollBar()->value();
    // A new content widget each time; the old one goes away later (this may run inside the clicked() of one of its buttons).
    if (QWidget* old = m_scroll->takeWidget())
        old->deleteLater();
    m_list = new QWidget;
    m_list->setObjectName(QStringLiteral("zeroTimelineList"));
    m_listLayout = new QVBoxLayout(m_list);
    m_listLayout->setContentsMargins(0, Tokens::spaceSm, Tokens::spaceSm, Tokens::spaceLg);
    m_listLayout->setSpacing(0);
    m_showMore = nullptr;

    if (all.isEmpty()) {
        m_scroll->setWidget(m_list);
        m_stack->setCurrentWidget(m_empty);
        return;
    }

    const QList<History::Timeline::Group> groups = History::Timeline::groupByDay(all.mid(0, m_limit), now);
    bool firstGroup = true;
    for (const History::Timeline::Group& group : groups) {
        auto* header = new QLabel(group.label, m_list);
        header->setObjectName(QStringLiteral("zeroTimelineGroup"));
        Style::applyRole(header, Role::Overline);
        header->setContentsMargins(0, firstGroup ? 0 : Tokens::spaceLg, 0, Tokens::spaceXs);
        m_listLayout->addWidget(header);
        firstGroup = false;
        for (const Entry& entry : group.entries) {
            auto* row = new QWidget(m_list);
            row->setObjectName(QStringLiteral("zeroTimelineRow"));
            row->setMinimumHeight(kRowMinHeight);
            row->setAccessibleName(entry.caption.isEmpty() ? entry.title : entry.title + QStringLiteral(", ") + entry.caption);
            auto* line = new QHBoxLayout(row);
            line->setContentsMargins(0, Tokens::spaceXs, 0, Tokens::spaceXs);
            line->setSpacing(Tokens::spaceMd);

            auto* icon = new QLabel(row);
            icon->setObjectName(QStringLiteral("zeroTimelineIcon"));
            icon->setFixedSize(kIconSize, kIconSize);
            icon->setPixmap(Icons::pixmap(iconNameFor(entry), colorFor(entry.tone), kIconSize, devicePixelRatioF()));
            line->addWidget(icon, 0, Qt::AlignVCenter);

            auto* text = new QVBoxLayout;
            text->setContentsMargins(0, 0, 0, 0);
            text->setSpacing(0);
            auto* title = new QLabel(entry.title, row);
            title->setObjectName(QStringLiteral("zeroTimelineTitle"));
            title->setTextFormat(Qt::PlainText);
            Style::applyRole(title, Role::Label);
            text->addWidget(title);
            if (!entry.caption.isEmpty()) {
                auto* caption = new QLabel(entry.caption, row);
                caption->setObjectName(QStringLiteral("zeroTimelineCaption"));
                caption->setTextFormat(Qt::PlainText);
                Style::applyRole(caption, Role::Caption);
                text->addWidget(caption);
            }
            line->addLayout(text, 1);

            if (entry.undoable) {
                auto* undo = new QPushButton(tr("Undo"), row);
                undo->setObjectName(QStringLiteral("zeroTimelineUndo"));
                Style::setProperty(undo, "zeroVariant", "secondary");
                Style::setProperty(undo, "zeroSize", "sm");
                undo->setFixedHeight(kButtonHeight);
                undo->setAutoDefault(false);
                undo->setAccessibleName(tr("Undo: %1").arg(entry.title));
                if (!busy.isEmpty()) {
                    undo->setEnabled(false);
                    undo->setToolTip(busy);
                }
                connect(undo, &QPushButton::clicked, this, [this, entry]() { onUndo(entry); });
                line->addWidget(undo, 0, Qt::AlignVCenter);
            }
            m_listLayout->addWidget(row);
        }
    }

    if (m_total > m_limit) {
        m_showMore = new QPushButton(tr("Show more"), m_list);
        m_showMore->setObjectName(QStringLiteral("zeroTimelineShowMore"));
        Style::setProperty(m_showMore, "zeroVariant", "ghost");
        m_showMore->setAutoDefault(false);
        connect(m_showMore, &QPushButton::clicked, this, [this]() {
            m_limit += History::Timeline::kShownFirst;
            refresh();
        });
        auto* wrap = new QHBoxLayout;
        wrap->setContentsMargins(0, Tokens::spaceMd, 0, 0);
        wrap->addWidget(m_showMore);
        wrap->addStretch(1);
        m_listLayout->addLayout(wrap);
    }
    m_listLayout->addStretch(1);

    m_scroll->setWidget(m_list);
    m_stack->setCurrentWidget(m_scroll);
    m_scroll->verticalScrollBar()->setValue(scrollPos);
}

void TimelinePage::onUndo(const Entry& entry)
{
    const QString busy = m_env.busyReason ? m_env.busyReason() : QString();
    if (!busy.isEmpty()) {
        refresh();
        return;
    }
    QPointer<TimelinePage> self(this);
    if (m_env.undo)
        m_env.undo(entry);
    if (self)
        refresh();
}

// ---- for the tests -----------------------------------------------------------------------------------------------------------------

bool TimelinePage::isEmptyStateShown() const
{
    return m_stack->currentWidget() == m_empty;
}

int TimelinePage::rowCount() const
{
    return m_list ? int(m_list->findChildren<QWidget*>(QStringLiteral("zeroTimelineRow")).size()) : 0;
}

namespace {
QStringList labelTexts(const QWidget* list, const char* name)
{
    QStringList out;
    if (!list)
        return out;
    for (const auto* label : list->findChildren<QLabel*>(QLatin1String(name)))
        out << label->text();
    return out;
}
}  // namespace

QStringList TimelinePage::groupLabels() const
{
    return labelTexts(m_list, "zeroTimelineGroup");
}

QStringList TimelinePage::rowTitles() const
{
    return labelTexts(m_list, "zeroTimelineTitle");
}

QStringList TimelinePage::rowCaptions() const
{
    // One entry per row (an empty text for a row without a caption).
    QStringList out;
    if (!m_list)
        return out;
    for (const auto* row : m_list->findChildren<QWidget*>(QStringLiteral("zeroTimelineRow"))) {
        const auto* caption = row->findChild<QLabel*>(QStringLiteral("zeroTimelineCaption"));
        out << (caption ? caption->text() : QString());
    }
    return out;
}

QList<QPushButton*> TimelinePage::undoButtons() const
{
    return m_list ? m_list->findChildren<QPushButton*>(QStringLiteral("zeroTimelineUndo")) : QList<QPushButton*>();
}

}  // namespace TimelineUi
}  // namespace Zero
