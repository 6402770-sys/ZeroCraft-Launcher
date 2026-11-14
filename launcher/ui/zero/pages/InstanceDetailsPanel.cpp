// SPDX-License-Identifier: GPL-3.0-only
#include "InstanceDetailsPanel.h"
#include "ui/zero/ZeroPlayButton.h"

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QCompleter>
#include <QEvent>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedLayout>
#include <QStringListModel>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>
#include <algorithm>
#include <functional>
#include "Application.h"
#include "ArtworkCache.h"
#include "BaseInstance.h"
#include "ElidedLabel.h"
#include "FlowLayout.h"
#include "IconTile.h"
#include "InstanceInfo.h"
#include "InstanceList.h"
#include "icons/IconList.h"
#include "minecraft/MinecraftInstance.h"
#include "ui/zero/ZeroEmptyState.h"
#include "ui/zero/ZeroFormat.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroTransition.h"
#include "zero/backup/PackSnapshot.h"
#include "zero/mods/ModHealth.h"
#include "zero/profiles/InstanceProfile.h"

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

/// One tag with a small x to remove it. Not a Q_OBJECT: it reports through a callback.
class TagPill : public QWidget {
   public:
    TagPill(const QString& tag, std::function<void()> onRemove, QWidget* parent) : QWidget(parent), m_tag(tag), m_onRemove(std::move(onRemove))
    {
        setFixedHeight(24);
        setMouseTracking(true);
        setAttribute(Qt::WA_Hover, true);
        setAccessibleName(tag);
        setToolTip(QObject::tr("Remove tag \"%1\" with the x").arg(tag));
        connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    }

    QSize sizeHint() const override
    {
        const QFontMetrics fm(Style::font(Role::Caption));
        return { 8 + fm.horizontalAdvance(m_tag) + 4 + kCross + 6, 24 };
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

   protected:
    QRect crossRect() const { return QRect(width() - 6 - kCross - 4, 0, kCross + 8, height()); }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        const bool over = crossRect().contains(event->position().toPoint());
        if (over != m_overCross) {
            m_overCross = over;
            setCursor(over ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
    }
    void leaveEvent(QEvent* event) override
    {
        QWidget::leaveEvent(event);
        if (m_overCross) {
            m_overCross = false;
            setCursor(Qt::ArrowCursor);
            update();
        }
    }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && crossRect().contains(event->position().toPoint()) && m_onRemove)
            m_onRemove();
    }
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(t.border, 1));
        p.setBrush(t.elevated);
        p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusMd));
        const QFont font = Style::font(Role::Caption);
        const QFontMetrics fm(font);
        p.setFont(font);
        p.setPen(t.text);
        p.drawText(QPointF(8, height() / 2.0 + (fm.ascent() - fm.descent()) / 2.0), m_tag);
        Icons::icon(QStringLiteral("x"), m_overCross ? t.text : t.textTertiary)
            .paint(&p, QRect(width() - 6 - kCross, (height() - kCross) / 2, kCross, kCross));
    }

   private:
    static constexpr int kCross = 10;
    QString m_tag;
    std::function<void()> m_onRemove;
    bool m_overCross = false;
};
}  // namespace

/// The artwork as a 16:9 cover with the instance icon as a clickable badge at its bottom-left.
class ArtworkHeader : public QWidget {
   public:
    explicit ArtworkHeader(QWidget* parent) : QWidget(parent), m_art(new ArtworkView(this)), m_badge(new IconTile(44, 32, this))
    {
        QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
        m_badge->setClickable(true);
        m_badge->setOutlined(true);
        m_badge->setToolTip(QObject::tr("Change icon"));
    }
    ArtworkView* art() const { return m_art; }
    IconTile* badge() const { return m_badge; }
    int heightForWidth(int width) const override { return width * 9 / 16; }
    QSize sizeHint() const override { return { InstanceDetailsPanel::kWidth - 34, (InstanceDetailsPanel::kWidth - 34) * 9 / 16 }; }

   protected:
    void resizeEvent(QResizeEvent*) override
    {
        m_art->setGeometry(rect());
        m_badge->move(8, height() - 8 - m_badge->height());
    }

   private:
    ArtworkView* m_art;
    IconTile* m_badge;
};

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

    // ----- visual: icon tile, or the artwork with the icon as a badge -----
    m_icon = new IconTile(120, 96, content);
    m_icon->setClickable(true);
    m_icon->setToolTip(tr("Change icon"));
    connect(m_icon, &QAbstractButton::clicked, this, &InstanceDetailsPanel::changeIconRequested);
    lay->addWidget(m_icon, 0, Qt::AlignHCenter);

    m_artHeader = new ArtworkHeader(content);
    connect(m_artHeader->badge(), &QAbstractButton::clicked, this, &InstanceDetailsPanel::changeIconRequested);
    m_artHeader->hide();
    lay->addWidget(m_artHeader);
    lay->addSpacing(Tokens::spaceMd);

    // ----- name row: [spacer] name [favorite star] -----
    auto* nameRow = new QHBoxLayout;
    nameRow->setContentsMargins(0, 0, 0, 0);
    nameRow->setSpacing(Tokens::spaceXs);
    nameRow->addSpacing(28 + Tokens::spaceXs);
    auto* nameBox = new QVBoxLayout;
    nameBox->setContentsMargins(0, 0, 0, 0);
    nameBox->setSpacing(0);

    m_name = new ElidedLabel(content);
    Style::applyRole(m_name, Role::Title);
    m_name->setAlignment(Qt::AlignCenter);
    m_name->installEventFilter(this);
    nameBox->addWidget(m_name);

    m_nameEdit = new QLineEdit(content);
    m_nameEdit->setAlignment(Qt::AlignCenter);
    m_nameEdit->setMaxLength(128);
    m_nameEdit->hide();
    m_nameEdit->installEventFilter(this);
    nameBox->addWidget(m_nameEdit);
    nameRow->addLayout(nameBox, 1);

    m_star = new QToolButton(content);
    m_star->setFixedSize(28, 28);
    m_star->setIconSize(QSize(16, 16));
    m_star->setAutoRaise(false);
    m_star->setCursor(Qt::PointingHandCursor);
    Style::setProperty(m_star, "zeroVariant", "ghost");
    connect(m_star, &QToolButton::clicked, this, &InstanceDetailsPanel::favoriteToggleRequested);
    nameRow->addWidget(m_star, 0, Qt::AlignVCenter);
    lay->addLayout(nameRow);

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
    m_play = new ZeroPlayButton(content);
    connect(m_play, &ZeroPlayButton::playRequested, this, [this]() {
        if (m_actions.launch)
            m_actions.launch->trigger();
    });
    connect(m_play, &ZeroPlayButton::stopRequested, this, [this]() {
        if (m_actions.kill)
            m_actions.kill->trigger();
    });
    lay->addWidget(m_play);
    lay->addSpacing(Tokens::spaceLg);

    // ----- tags -----
    lay->addWidget(makeLabel(tr("Tags"), Role::Overline, content));
    lay->addSpacing(Tokens::spaceSm);
    m_tagsHost = new QWidget(content);
    m_tagFlow = new FlowLayout(m_tagsHost, Tokens::spaceSm - 2, Tokens::spaceSm - 2);
    m_tagInput = new QLineEdit(m_tagsHost);
    m_tagInput->setPlaceholderText(tr("Add tag"));
    m_tagInput->setMaxLength(Profile::kMaxTagLength);
    m_tagInput->setFixedSize(96, 24);
    m_tagInput->setStyleSheet(QStringLiteral("QLineEdit { min-height: 0px; padding: 0 8px; }"));
    m_tagInput->installEventFilter(this);
    m_tagFlow->addWidget(m_tagInput);
    m_tagModel = new QStringListModel(this);
    m_tagCompleter = new QCompleter(m_tagModel, this);
    m_tagCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    m_tagCompleter->setFilterMode(Qt::MatchContains);
    m_tagCompleter->setCompletionMode(QCompleter::PopupCompletion);
    m_tagInput->setCompleter(m_tagCompleter);
    connect(m_tagInput, &QLineEdit::returnPressed, this, [this]() { addTag(m_tagInput->text()); });
    connect(m_tagCompleter, qOverload<const QString&>(&QCompleter::activated), this, [this](const QString& text) { addTag(text); });
    lay->addWidget(m_tagsHost);
    lay->addSpacing(Tokens::spaceLg);

    // ----- description (Prism notes) -----
    lay->addWidget(makeLabel(tr("Description"), Role::Overline, content));
    lay->addSpacing(Tokens::spaceSm);
    m_desc = new QPlainTextEdit(content);
    m_desc->setPlaceholderText(tr("Add a description or notes…"));
    m_desc->setTabChangesFocus(true);
    m_desc->installEventFilter(this);
    lay->addWidget(m_desc);
    m_descTimer = new QTimer(this);
    m_descTimer->setSingleShot(true);
    m_descTimer->setInterval(kDescriptionIdleMs);
    connect(m_descTimer, &QTimer::timeout, this, &InstanceDetailsPanel::flushDescription);
    connect(m_desc, &QPlainTextEdit::textChanged, this, [this]() {
        adjustDescriptionHeight();
        if (m_loadingDesc)
            return;
        m_descDirty = true;
        m_descTimer->start();
    });
    connect(m_desc->document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this, [this]() { adjustDescriptionHeight(); });
    adjustDescriptionHeight();
    lay->addSpacing(Tokens::spaceLg);

    // action list is created in setActions() (needs the actions)
    lay->addStretch(1);
    m_actionHost = lay;

    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        refreshIcons();
        refreshProfile(false);
    });
    connect(Profile::Notifier::instance(), &Profile::Notifier::profileChanged, this, [this](const QString& id) {
        if (m_instance && m_instance->id() == id)
            refreshProfile(false);
    });
    refreshPlayButton(false, false);
    setInstance(nullptr);
}

InstanceDetailsPanel::~InstanceDetailsPanel()
{
    flushDescription();
}

QWidget* InstanceDetailsPanel::artworkHeader() const
{
    return m_artHeader;
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
    m_checkMods = addActionButton(parent, QStringLiteral("package"), tr("Check mods"), nullptr);
    connect(m_checkMods, &QPushButton::clicked, this, &InstanceDetailsPanel::checkModsRequested);
    m_checkMods->setVisible(m_instance && Mods::readInstanceContext(m_instance->instanceRoot()).loader != Mods::InstanceLoader::None);
    insert(m_checkMods);
    m_boost = addActionButton(parent, QStringLiteral("gauge"), tr("Boost performance"), nullptr);
    m_boost->setToolTip(tr("Add performance mods and give Minecraft the memory it needs"));
    connect(m_boost, &QPushButton::clicked, this, &InstanceDetailsPanel::boostPerformanceRequested);
    m_boost->setVisible(m_instance && !m_instance->isRunning());  // while the game runs nothing in its folders may change
    insert(m_boost);
    m_rollback = addActionButton(parent, QStringLiteral("arrow-left"), tr("Roll back update"), nullptr);
    connect(m_rollback, &QPushButton::clicked, this, &InstanceDetailsPanel::rollbackRequested);
    m_rollback->hide();
    insert(m_rollback);
    insert(addActionButton(parent, QStringLiteral("list-filter"), tr("Change group"), actions.group));
    m_setArtwork = addActionButton(parent, QStringLiteral("image"), tr("Set artwork…"), nullptr);
    connect(m_setArtwork, &QPushButton::clicked, this, &InstanceDetailsPanel::setArtworkRequested);
    insert(m_setArtwork);
    m_removeArtwork = addActionButton(parent, QStringLiteral("circle-x"), tr("Remove artwork"), nullptr);
    connect(m_removeArtwork, &QPushButton::clicked, this, &InstanceDetailsPanel::removeArtworkRequested);
    m_removeArtwork->hide();
    insert(m_removeArtwork);
    insert(addActionButton(parent, QStringLiteral("copy"), tr("Copy"), actions.copy));
    auto* exportButton = addActionButton(parent, QStringLiteral("archive"), tr("Export"), nullptr);
    connect(exportButton, &QPushButton::clicked, this, [this]() {
        if (m_instance)
            emit exportRequested(m_instance->id());
    });
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
    refreshRollback();
}

void InstanceDetailsPanel::refreshRollback()
{
    if (!m_rollback)
        return;
    Backup::Pack::Info snapshot;
    if (m_instance && m_instance->isManagedPack()) {
        snapshot = Backup::Pack::newestActive(Backup::Pack::listSnapshots(Backup::Pack::snapshotsDirFor(APPLICATION->dataRoot(), m_instance->id())),
                                              m_instance->getManagedPackID());
    }
    m_rollback->setVisible(snapshot.valid);
    if (!snapshot.valid)
        return;
    const QString back = snapshot.meta.oldVersion.isEmpty() ? tr("the previous version") : snapshot.meta.oldVersion;
    const bool running = m_instance->isRunning();
    m_rollback->setEnabled(!running);
    m_rollback->setToolTip(running ? tr("Close the game to roll back") : tr("Back to %1 (saved %2)").arg(back, Format::relativeTime(snapshot.createdMs)));
}

void InstanceDetailsPanel::refreshIcons()
{
    const auto& t = Theme::current();
    for (const auto& spec : m_iconSpecs)
        spec.button->setIcon(Icons::icon(spec.icon, spec.danger ? t.danger : t.textSecondary));
}

void InstanceDetailsPanel::refreshPlayButton(bool running, bool canLaunch)
{
    if (m_boost)
        m_boost->setVisible(m_instance && !running);
    m_play->setInstanceId(m_instance ? m_instance->id() : QString());
    m_play->setCanLaunch(canLaunch && m_actions.launch && m_actions.launch->isEnabled());
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
    // Selecting a different instance crossfades the panel (snapshot overlay: the new content is live immediately).
    const bool switching = m_instance && instance && m_instance != instance;
    const Transition::Snapshot before = switching ? Transition::capture(this) : Transition::Snapshot();
    applyInstance(instance);
    if (!before.isNull())
        Transition::run(this, before, Transition::Kind::Crossfade, Tokens::durFast);
}

void InstanceDetailsPanel::applyInstance(MinecraftInstance* instance)
{
    const bool same = instance && instance == m_instance;
    if (!same) {
        flushDescription();  // a pending edit belongs to the instance we are leaving
        m_tagInput->clear();
    }
    m_instance = instance;
    if (!instance) {
        cancelRename();
        m_stack->setCurrentIndex(0);
        m_descInstance = nullptr;
        return;
    }
    m_stack->setCurrentIndex(1);

    m_icon->setInstanceIcon(APPLICATION->icons()->getIcon(instance->iconKey()));
    m_artHeader->badge()->setInstanceIcon(APPLICATION->icons()->getIcon(instance->iconKey()));
    m_nameText = instance->name();
    m_name->setFullText(m_nameText);

    const bool running = instance->isRunning();
    m_running->setVisible(running);
    m_lastPlayed->setText(Format::relativeTime(instance->lastLaunch()));
    m_playtime->setText(Format::playtime(instance->totalTimePlayed()));
    const QString group = APPLICATION->instances()->getInstanceGroup(instance->id());
    m_group->setFullText(group.isEmpty() ? QStringLiteral("—") : group);

    refreshProfile(!same);
    refreshPlayButton(running, instance->canLaunch());
}

/// Favorite star, artwork, meta line (with the mod count), tags and description. `reloadEditors` = a different instance was
/// selected: the description editor and the tags are reloaded unconditionally; otherwise an edit in progress is left alone.
void InstanceDetailsPanel::refreshProfile(bool reloadEditors)
{
    if (!m_instance)
        return;
    const auto& t = Theme::current();

    const bool favorite = Profile::isFavorite(m_instance);
    m_star->setIcon(favorite ? Icons::filledIcon(QStringLiteral("star"), t.highlight) : Icons::icon(QStringLiteral("star"), t.textTertiary));
    m_star->setToolTip(favorite ? tr("Remove from favorites") : tr("Add to favorites"));
    m_star->setAccessibleName(m_star->toolTip());

    const QString artwork = Profile::artworkPath(m_instance);
    const bool hasArtwork = !artwork.isEmpty();
    m_icon->setVisible(!hasArtwork);
    m_artHeader->setVisible(hasArtwork);
    m_artHeader->art()->setArtwork(artwork);
    if (m_removeArtwork)
        m_removeArtwork->setVisible(hasArtwork);

    if (m_boost)
        m_boost->setVisible(!m_instance->isRunning());
    if (m_checkMods)  // only for instances with a mod loader (a small read of mmc-pack.json)
        m_checkMods->setVisible(Mods::readInstanceContext(m_instance->instanceRoot()).loader != Mods::InstanceLoader::None);
    refreshRollback();

    const InstanceInfo info = InstanceInfo::of(m_instance);
    QStringList meta;
    if (!info.summary().isEmpty())
        meta << info.summary();
    if (const auto mods = Profile::modCount(m_instance))
        meta << (*mods == 1 ? tr("1 mod") : tr("%1 mods").arg(*mods));
    m_meta->setText(meta.join(QStringLiteral(" · ")));
    m_meta->setVisible(!meta.isEmpty());

    // tags
    const QStringList tags = Profile::tags(m_instance);
    if (reloadEditors || tags != m_shownTags)
        rebuildTags();
    m_tagInput->setEnabled(tags.size() < Profile::kMaxTags);
    m_tagInput->setToolTip(tags.size() < Profile::kMaxTags ? QString() : tr("An instance can have up to %1 tags.").arg(Profile::kMaxTags));
    refreshTagCompletions();

    // description: never overwrite what the user is typing
    const QString notes = Profile::description(m_instance);
    const bool editing = m_desc->hasFocus() || m_descDirty;
    if (reloadEditors || (!editing && m_desc->toPlainText() != notes)) {
        m_loadingDesc = true;
        m_desc->setPlainText(notes);
        m_loadingDesc = false;
        m_descDirty = false;
        m_descTimer->stop();
        m_descInstance = m_instance;
    }
    adjustDescriptionHeight();
}

void InstanceDetailsPanel::rebuildTags()
{
    m_shownTags = Profile::tags(m_instance);
    m_tagFlow->removeWidget(m_tagInput);
    for (QWidget* pill : std::as_const(m_tagPills)) {
        m_tagFlow->removeWidget(pill);
        pill->hide();
        pill->deleteLater();
    }
    m_tagPills.clear();
    for (const QString& tag : std::as_const(m_shownTags)) {
        auto* pill = new TagPill(tag, [this, tag]() { removeTag(tag); }, m_tagsHost);
        m_tagFlow->addWidget(pill);
        pill->show();
        m_tagPills << pill;
    }
    m_tagFlow->addWidget(m_tagInput);
    m_tagsHost->updateGeometry();
}

void InstanceDetailsPanel::refreshTagCompletions()
{
    QList<BaseInstance*> all;
    auto* list = APPLICATION->instances();
    for (int i = 0; i < list->count(); ++i)
        all << list->at(i);
    QStringList suggestions;
    for (const auto& entry : Profile::allTags(all)) {
        const bool own = std::any_of(m_shownTags.cbegin(), m_shownTags.cend(),
                                     [&](const QString& t) { return t.compare(entry.tag, Qt::CaseInsensitive) == 0; });
        if (!own)
            suggestions << entry.tag;
    }
    m_tagModel->setStringList(suggestions);
}

void InstanceDetailsPanel::addTag(const QString& text)
{
    m_tagInput->clear();
    if (!m_instance || text.trimmed().isEmpty())
        return;
    Profile::setTags(m_instance, Profile::tags(m_instance) + QStringList{ text });
}

void InstanceDetailsPanel::removeTag(const QString& tag)
{
    if (!m_instance)
        return;
    QStringList tags = Profile::tags(m_instance);
    tags.removeAll(tag);
    Profile::setTags(m_instance, tags);
}

void InstanceDetailsPanel::adjustDescriptionHeight()
{
    // 3 lines at least, 6 at most; the editor scrolls beyond that.
    const int lineHeight = QFontMetrics(m_desc->font()).lineSpacing();
    const int docLines = int(qCeil(m_desc->document()->size().height()));
    const int lines = qBound(3, docLines, 6);
    // no scrollbar while everything fits (it would narrow the text and change the line count again)
    m_desc->setVerticalScrollBarPolicy(docLines > 6 ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
    const int chrome = 2 * 1 + 2 * 6 + int(2 * m_desc->document()->documentMargin());  // border + padding + document margin
    const int height = lines * lineHeight + chrome;
    if (m_desc->height() != height || m_desc->minimumHeight() != height) {
        m_desc->setMinimumHeight(height);
        m_desc->setMaximumHeight(height);
    }
}

void InstanceDetailsPanel::flushDescription()
{
    m_descTimer->stop();
    if (!m_descDirty)
        return;
    m_descDirty = false;
    if (m_descInstance)
        Profile::setDescription(m_descInstance, m_desc->toPlainText());
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
    if (watched == m_tagInput && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) {
            m_tagInput->clear();
            m_tagInput->clearFocus();
            return true;
        }
        if (key->key() == Qt::Key_Backspace && m_tagInput->text().isEmpty() && !m_shownTags.isEmpty()) {
            removeTag(m_shownTags.last());  // Backspace on an empty field removes the last tag
            return true;
        }
    }
    if (watched == m_desc && event->type() == QEvent::FocusOut)
        flushDescription();
    return ZeroCard::eventFilter(watched, event);
}

}  // namespace Zero
