// SPDX-License-Identifier: GPL-3.0-only
#include "CartReviewDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>
#include <optional>

#include "Application.h"
#include "settings/SettingsObject.h"
#include "ui/zero/PillTag.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroSpinner.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/content/InstallDialogParts.h"
#include "ui/zero/content/RemoteImageCache.h"
#include "zero/content/ModDependencies.h"
#include "zero/content/ModpackInstall.h"
#include "zero/content/PackOrder.h"

namespace Zero::Content {

using namespace Parts;

namespace {
constexpr int kRowHeight = 48;
constexpr int kRowIcon = 32;
constexpr int kComboWidth = 156;
constexpr int kRemoveSize = 28;
constexpr int kNameMax = 230;       ///< px of an item's name in a row
constexpr int kDepRowIcon = 24;
constexpr int kDepRowHeight = 32;
constexpr int kRequiredByMax = 190;  ///< px of the "Required by ..." text of a dependency row

int groupIndex(Kind kind)
{
    return kind == Kind::Mod ? 0 : kind == Kind::ResourcePack ? 1 : 2;
}

QString versionText(const ModPlatform::IndexedVersion& v)
{
    QString text = v.version.isEmpty() ? v.versionNumber : v.version;
    if (v.versionType == ModPlatform::IndexedVersionType::Beta || v.versionType == ModPlatform::IndexedVersionType::Alpha) {
        text += QStringLiteral("  (%1)").arg(versionTypeText(versionFromIndexed(v).type));
    }
    return text.isEmpty() ? QObject::tr("Latest") : text;
}

QString versionNumber(const ModPlatform::IndexedVersion& v)
{
    return v.versionNumber.isEmpty() ? v.version : v.versionNumber;
}
}  // namespace

CartReviewEnvironment CartReviewEnvironment::fromApplication(std::function<ModpackSource*(const CartEntry&)> sourceFor,
                                                             std::function<QList<InstalledMod>()> installedMods,
                                                             std::function<QString(const CartEntry&)> installedFileId)
{
    CartReviewEnvironment env;
    env.sourceFor = std::move(sourceFor);
    env.installedMods = std::move(installedMods);
    env.installedFileId = std::move(installedFileId);
    env.makeResolver = [](QObject* parent) -> DependencyResolver* { return new InstanceDependencyResolver(parent); };
    env.dependencyCheckEnabled = []() { return !APPLICATION->settings()->get("ModDependenciesDisabled").toBool(); };
    return env;
}

// ---------------------------------------------------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------------------------------------------------

CartReviewDialog::CartReviewDialog(CartReviewEnvironment env, InstallCartModel* cart, const TargetInstance& target, QWidget* parent)
    : QDialog(parent), m_env(std::move(env)), m_cart(cart), m_target(target)
{
    setObjectName(QStringLiteral("ZeroCartReviewDialog"));
    if (!m_env.images) {
        m_env.images = RemoteImageCache::instance();
    }
    m_depTimer.setSingleShot(true);
    connect(&m_depTimer, &QTimer::timeout, this, &CartReviewDialog::runDependencyCheck);
    m_depWatchdog.setSingleShot(true);
    connect(&m_depWatchdog, &QTimer::timeout, this, [this]() {
        if (m_depState != DepState::Checking) {
            return;
        }
        ++m_token;  // a late answer of the abandoned run is dropped
        if (m_resolver) {
            m_resolver->cancel();
        }
        setDependencyState(DepState::Failed, tr("The check took too long."));
    });

    buildUi();

    if (m_env.makeResolver) {
        m_resolver = m_env.makeResolver(this);  // a child: destroyed (and cancelled) with the dialog
    }
    if (m_resolver) {
        connect(m_resolver, &DependencyResolver::resolved, this, &CartReviewDialog::onResolved);
        connect(m_resolver, &DependencyResolver::failed, this, &CartReviewDialog::onResolveFailed);
    }
    connect(m_depRetry, &QPushButton::clicked, this, [this]() { scheduleDependencyCheck(0); });
    connect(m_skip, &QCheckBox::toggled, this, &CartReviewDialog::refreshState);
    connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_primary, &QPushButton::clicked, this, &CartReviewDialog::accept);
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        refreshIcons();
        update();
    });

    if (m_cart) {
        for (const CartEntry& entry : m_cart->entries()) {
            if (!entry.item) {
                continue;
            }
            auto row = std::make_unique<Row>();
            row->entry = entry;
            row->installedFileId = m_env.installedFileId ? m_env.installedFileId(entry) : QString();
            Row& ref = *row;
            m_rows.push_back(std::move(row));
            buildRow(ref);
        }
    }
    // The versions are requested after every row exists (an answer may arrive at once and looks at all of them).
    for (const auto& row : m_rows) {
        if (row->entry.kind == Kind::Mod && !m_target.modReady()) {
            setRowState(*row, RowState::NeedsModded);
        } else if (row->entry.kind != Kind::Mod && !m_target.packReady()) {
            setRowState(*row, RowState::NoVersion);
        } else {
            requestVersions(*row);
        }
    }

    setTabOrder(m_skip, m_cancel);
    setTabOrder(m_cancel, m_primary);
    refreshIcons();
    refreshTexts();
    refreshState();
}

CartReviewDialog::~CartReviewDialog()
{
    m_depTimer.stop();
    m_depWatchdog.stop();
    ++m_token;  // whatever the resolver still delivers is stale
    if (m_resolver) {
        m_resolver->cancel();
    }
}

void CartReviewDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* body = new QWidget(this);
    auto* column = new QVBoxLayout(body);
    column->setContentsMargins(Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceLg);
    column->setSpacing(Tokens::spaceMd);

    // ---- title and "To <instance> . 1.21.1 Fabric" ----
    m_title = new QLabel(body);
    Style::applyRole(m_title, Role::Title);
    column->addWidget(m_title);
    m_subtitle = new QLabel(body);
    Style::applyRole(m_subtitle, Role::Caption);
    Style::setProperty(m_subtitle, "zeroTone", "secondary");
    column->addWidget(m_subtitle);

    // ---- note (the target runs) ----
    const NoteParts note = buildNoteRow(body);
    m_noteRow = note.row;
    m_noteIcon = note.icon;
    m_note = note.text;
    m_noteRow->hide();
    column->addWidget(m_noteRow);

    // ---- the list: groups, dependencies, "Turn them on" ----
    m_scroll = new QScrollArea(body);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_content = new QWidget;
    m_scroll->setWidget(m_content);
    m_content->setAutoFillBackground(false);
    m_scroll->viewport()->setAutoFillBackground(false);
    auto* list = new QVBoxLayout(m_content);
    list->setContentsMargins(0, 0, 0, 0);
    list->setSpacing(Tokens::spaceLg);

    m_groups[0] = buildGroup(Kind::Mod, &m_groupRows[0]);
    list->addWidget(m_groups[0]);

    // dependencies belong to the mods: right under their group
    m_depBlock = new QWidget(m_content);
    auto* depColumn = new QVBoxLayout(m_depBlock);
    depColumn->setContentsMargins(0, 0, 0, 0);
    depColumn->setSpacing(Tokens::spaceSm);
    depColumn->addWidget(overline(tr("Dependencies"), m_depBlock));
    auto* depStatusRow = new QHBoxLayout;
    depStatusRow->setContentsMargins(0, 0, 0, 0);
    depStatusRow->setSpacing(Tokens::spaceSm);
    m_depStatus = new QLabel(m_depBlock);
    Style::applyRole(m_depStatus, Role::Body);
    Style::setProperty(m_depStatus, "zeroTone", "secondary");
    m_depStatus->setWordWrap(true);
    depStatusRow->addWidget(m_depStatus, 1);
    m_depRetry = new QPushButton(tr("Retry"), m_depBlock);
    Style::setProperty(m_depRetry, "zeroVariant", "ghost");
    m_depRetry->setAutoDefault(false);
    m_depRetry->setAccessibleName(tr("Retry the dependency check"));
    m_depRetry->hide();
    depStatusRow->addWidget(m_depRetry, 0, Qt::AlignTop);
    depColumn->addLayout(depStatusRow);
    m_depSkeleton = new DependencySkeleton(m_depBlock);
    m_depSkeleton->hide();
    depColumn->addWidget(m_depSkeleton);
    m_depRows = new QWidget(m_depBlock);
    m_depRowsLayout = new QVBoxLayout(m_depRows);
    m_depRowsLayout->setContentsMargins(0, 0, 0, 0);
    m_depRowsLayout->setSpacing(Tokens::spaceSm);
    m_depRows->hide();
    depColumn->addWidget(m_depRows);
    m_depInstalled = new QLabel(m_depBlock);
    Style::applyRole(m_depInstalled, Role::Caption);
    Style::setProperty(m_depInstalled, "zeroTone", "secondary");
    m_depInstalled->setWordWrap(true);
    m_depInstalled->hide();
    depColumn->addWidget(m_depInstalled);
    m_skip = new QCheckBox(tr("Add without dependencies"), m_depBlock);
    m_skip->setAccessibleName(tr("Add without dependencies"));
    m_skip->hide();
    depColumn->addWidget(m_skip);
    m_depBlock->hide();
    list->addWidget(m_depBlock);

    m_groups[1] = buildGroup(Kind::ResourcePack, &m_groupRows[1]);
    list->addWidget(m_groups[1]);
    m_turnOn = new QCheckBox(tr("Turn them on"), m_groups[1]);
    m_turnOn->setChecked(true);
    m_turnOn->setAccessibleName(tr("Turn them on"));
    m_turnOn->setToolTip(tr("Puts the packs at the top of the game's resource pack list"));
    m_turnOn->setVisible(Packs::supportsOrdering(m_target.mcVersion));
    m_groups[1]->layout()->addWidget(m_turnOn);

    m_groups[2] = buildGroup(Kind::Shader, &m_groupRows[2]);
    list->addWidget(m_groups[2]);
    list->addStretch(1);
    column->addWidget(m_scroll, 1);
    root->addWidget(body, 1);

    // ---- footer ----
    const FooterParts footer = buildFooter(this, cartPrimaryText(0, 0, 0));
    m_footer = footer.bar;
    m_cancel = footer.cancel;
    m_primary = footer.primary;
    root->addWidget(m_footer);

    setFixedWidth(kWidth);
}

QWidget* CartReviewDialog::buildGroup(Kind kind, QVBoxLayout** rowsLayout)
{
    auto* group = new QWidget(m_content);
    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Tokens::spaceXs);
    const QString title = kind == Kind::Mod ? tr("Mods") : kind == Kind::ResourcePack ? tr("Resource packs") : tr("Shaders");
    auto* header = overline(title, group);
    header->setObjectName(QStringLiteral("zeroCartGroupTitle"));
    layout->addWidget(header);
    auto* rows = new QVBoxLayout;
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(0);
    layout->addLayout(rows);
    *rowsLayout = rows;
    group->hide();
    return group;
}

void CartReviewDialog::buildRow(Row& row)
{
    const int index = groupIndex(row.entry.kind);
    const ContentItem& item = *row.entry.item;
    row.widget = new QWidget(m_groups[index]);
    row.widget->setFixedHeight(kRowHeight);
    row.widget->setObjectName(QStringLiteral("zeroCartRow"));
    auto* layout = new QHBoxLayout(row.widget);
    layout->setContentsMargins(0, Tokens::spaceXs, 0, Tokens::spaceXs);
    layout->setSpacing(Tokens::spaceMd);
    row.icon = new PackIconWidget(item.iconUrl, m_env.images, row.widget, row.entry.kind == Kind::Mod ? QStringLiteral("blocks") : QStringLiteral("package"), kRowIcon);
    layout->addWidget(row.icon);

    auto* texts = new QVBoxLayout;
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(0);
    row.name = new QLabel(row.widget);
    row.name->setObjectName(QStringLiteral("zeroCartRowName"));
    Style::applyRole(row.name, Role::Label);
    row.name->setText(QFontMetrics(row.name->font()).elidedText(item.name, Qt::ElideRight, kNameMax));
    row.name->setToolTip(item.name);
    row.name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);  // the text is elided by hand: the label may shrink with the row
    texts->addWidget(row.name);
    auto* statusRow = new QHBoxLayout;
    statusRow->setContentsMargins(0, 0, 0, 0);
    statusRow->setSpacing(Tokens::spaceSm);
    row.spinner = new ZeroSpinner(ZeroSpinner::Small, row.widget);
    row.spinner->hide();
    statusRow->addWidget(row.spinner);
    row.status = new QLabel(row.widget);
    row.status->setObjectName(QStringLiteral("zeroCartRowStatus"));
    Style::applyRole(row.status, Role::Caption);
    Style::setProperty(row.status, "zeroTone", "secondary");
    row.status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusRow->addWidget(row.status, 1);
    texts->addLayout(statusRow);
    layout->addLayout(texts, 1);

    row.combo = new QComboBox(row.widget);
    row.combo->setFixedWidth(kComboWidth);
    row.combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    row.combo->setMinimumContentsLength(10);
    row.combo->setAccessibleName(tr("Version of %1").arg(item.name));
    row.combo->hide();
    layout->addWidget(row.combo);

    row.retry = new QPushButton(tr("Retry"), row.widget);
    Style::setProperty(row.retry, "zeroVariant", "ghost");
    row.retry->setAutoDefault(false);
    row.retry->setAccessibleName(tr("Retry loading the versions of %1").arg(item.name));
    row.retry->hide();
    layout->addWidget(row.retry);

    row.remove = new QToolButton(row.widget);
    Style::setProperty(row.remove, "zeroVariant", "ghost");
    Style::setProperty(row.remove, "zeroSize", "icon");
    row.remove->setFixedSize(kRemoveSize, kRemoveSize);
    row.remove->setIconSize(QSize(14, 14));
    row.remove->setToolTip(tr("Remove"));
    row.remove->setAccessibleName(tr("Remove %1").arg(item.name));
    row.remove->setCursor(Qt::PointingHandCursor);
    row.remove->setAutoRaise(false);
    layout->addWidget(row.remove);

    m_groupRows[index]->addWidget(row.widget);
    m_groups[index]->show();

    const QString key = row.entry.key;
    connect(row.remove, &QToolButton::clicked, this, [this, key]() { removeRow(key); });
    connect(row.retry, &QPushButton::clicked, this, [this, key]() {
        if (Row* r = this->row(key)) {
            requestVersions(*r);
        }
    });
    connect(row.combo, &QComboBox::currentIndexChanged, this, [this, key]() {
        if (Row* r = this->row(key)) {
            onVersionChanged(*r);
        }
    });
}

// ---------------------------------------------------------------------------------------------------------------------
// Rows and versions
// ---------------------------------------------------------------------------------------------------------------------

CartReviewDialog::Row* CartReviewDialog::row(const QString& key) const
{
    for (const auto& r : m_rows) {
        if (r->entry.key == key) {
            return r.get();
        }
    }
    return nullptr;
}

CartReviewDialog::Row* CartReviewDialog::rowByGeneration(quint64 generation) const
{
    for (const auto& r : m_rows) {
        if (r->generation == generation && r->state == RowState::Loading) {
            return r.get();
        }
    }
    return nullptr;
}

void CartReviewDialog::connectSource(ModpackSource* source)
{
    if (!source || m_connected.contains(source)) {
        return;
    }
    m_connected.append(source);
    connect(source, &ModpackSource::versionsFinished, this,
            [this](quint64 generation, const ContentItemPtr& item, const QList<ModpackVersion>&) { onVersionsFinished(generation, item); });
    connect(source, &ModpackSource::versionsForFinished, this,
            [this](quint64 generation, const ContentItemPtr& item, const QList<ModPlatform::IndexedVersion>& versions) {
                onVersionsForFinished(generation, item, versions);
            });
    connect(source, &ModpackSource::versionsFailed, this, [this](quint64 generation, const QString&) { onVersionsFailed(generation); });
}

void CartReviewDialog::requestVersions(Row& row)
{
    row.generation = 0;
    setRowState(row, RowState::Loading);
    ModpackSource* source = m_env.sourceFor ? m_env.sourceFor(row.entry) : nullptr;
    if (!source || !row.entry.item->pack) {
        setRowState(row, RowState::Failed);
        return;
    }
    // The review is about the CART's target instance, which need not be the one the section is filtered for: the versions the item
    // (or a plain versions() request) carries answer the section's filter, so they count only when it is the target's filter.
    VersionFilter want;
    want.mcVersions = { m_target.mcVersion };
    want.loaders = row.entry.kind == Kind::Mod ? m_target.loaders : ModPlatform::ModLoaderTypes();
    const bool sameFilter = source->versionFilter() == want;
    // The list the item carries may come from an earlier filter of the section (its target changed since): it counts only when it was
    // loaded for this target; a list of unknown origin counts when the section's filter is the target's.
    const auto& loadedFor = row.entry.item->versionsFilter;
    const bool cacheFits = loadedFor ? *loadedFor == want : sameFilter;
    if (cacheFits && row.entry.item->pack->versionsLoaded) {
        setRowVersions(row, row.entry.item->pack->versions);
        return;
    }
    connectSource(source);
    row.generation = ++m_nextGeneration;
    if (sameFilter) {
        source->versions(row.entry.item, row.generation);  // may answer at once (a fake does)
    } else {
        source->versionsFor(row.entry.item, row.generation, want);
    }
}

void CartReviewDialog::onVersionsForFinished(quint64 generation, const ContentItemPtr& item, const QList<ModPlatform::IndexedVersion>& versions)
{
    Row* r = rowByGeneration(generation);
    if (!r || r->entry.item != item) {
        return;
    }
    r->generation = 0;
    setRowVersions(*r, versions);
}

void CartReviewDialog::onVersionsFinished(quint64 generation, const ContentItemPtr& item)
{
    Row* r = rowByGeneration(generation);
    if (!r || r->entry.item != item) {
        return;  // a request of the section, or of a row that was removed or asked again
    }
    r->generation = 0;
    if (!item->pack) {
        setRowState(*r, RowState::Failed);
        return;
    }
    setRowVersions(*r, item->pack->versions);
}

void CartReviewDialog::onVersionsFailed(quint64 generation)
{
    Row* r = rowByGeneration(generation);
    if (!r) {
        return;
    }
    r->generation = 0;
    setRowState(*r, RowState::Failed);
}

void CartReviewDialog::setRowVersions(Row& row, const QList<ModPlatform::IndexedVersion>& all)
{
    // A resource pack or shader carries no mod loader ("minecraft" on Modrinth): only the Minecraft version decides.
    row.versions = compatibleVersions(all, m_target.mcVersion, row.entry.kind == Kind::Mod ? m_target.loaders : ModPlatform::ModLoaderTypes());
    {
        const QSignalBlocker block(row.combo);
        row.combo->clear();
        for (int i = 0; i < row.versions.size(); ++i) {
            row.combo->addItem(versionText(row.versions.at(i)), i);
        }
        row.combo->setCurrentIndex(defaultModVersionIndex(row.versions));
    }
    setRowState(row, row.versions.isEmpty() ? RowState::NoVersion : RowState::Ready);
}

void CartReviewDialog::setRowState(Row& row, RowState state)
{
    row.state = state;
    row.spinner->setVisible(state == RowState::Loading);
    row.combo->setVisible(state == RowState::Ready);
    row.retry->setVisible(state == RowState::Failed);
    refreshRowText(row);
    if (row.entry.kind == Kind::Mod) {
        scheduleDependencyCheck(0);  // the set of versions the dependencies are asked for changed
    } else {
        resumeDependencyCheck();  // a pack or a shader may be the last row the due run waited for
    }
    refreshState();
}

std::optional<ModPlatform::IndexedVersion> CartReviewDialog::chosenVersion(const Row& row) const
{
    if (row.state != RowState::Ready) {
        return std::nullopt;
    }
    const int index = row.combo->currentData().toInt();
    if (row.combo->currentIndex() < 0 || index < 0 || index >= row.versions.size()) {
        return std::nullopt;
    }
    return row.versions.at(index);
}

void CartReviewDialog::refreshRowText(Row& row)
{
    QString text;
    QString tone = QStringLiteral("secondary");
    switch (row.state) {
        case RowState::Loading:
            text = cartFindingVersionsText();
            break;
        case RowState::Ready: {
            const auto version = chosenVersion(row);
            if (!row.installedFileId.isEmpty() && version) {
                text = cartUpdateText(versionNumber(*version));
                tone = QStringLiteral("info");
            } else if (!row.entry.item->author.isEmpty()) {
                text = tr("by %1").arg(row.entry.item->author);
            }
            break;
        }
        case RowState::NoVersion:
            text = cartNoVersionText(m_target.summaryFor(row.entry.kind == Kind::Mod));
            tone = QStringLiteral("warning");
            break;
        case RowState::Failed:
            text = cartVersionsFailedText();
            tone = QStringLiteral("danger");
            break;
        case RowState::NeedsModded:
            text = cartNeedsModdedText();
            tone = QStringLiteral("warning");
            break;
    }
    row.status->setText(QFontMetrics(row.status->font()).elidedText(text, Qt::ElideRight, kNameMax + 60));
    row.status->setToolTip(text);
    Style::setProperty(row.status, "zeroTone", tone);
    row.status->setVisible(!text.isEmpty());
}

void CartReviewDialog::onVersionChanged(Row& row)
{
    refreshRowText(row);
    if (row.entry.kind == Kind::Mod) {
        scheduleDependencyCheck(m_env.dependencyDelayMs);  // 300 ms: the user may still be scrolling through the combo
    }
    refreshState();
}

void CartReviewDialog::removeRow(const QString& key)
{
    const auto it = std::find_if(m_rows.begin(), m_rows.end(), [&key](const std::unique_ptr<Row>& r) { return r->entry.key == key; });
    if (it == m_rows.end()) {
        return;
    }
    const Kind kind = (*it)->entry.kind;
    QWidget* widget = (*it)->widget;
    widget->hide();
    widget->setParent(nullptr);
    widget->deleteLater();
    m_rows.erase(it);
    if (m_cart) {
        m_cart->remove(key);
    }
    if (m_rows.empty()) {
        reject();
        return;
    }
    const int index = groupIndex(kind);
    m_groups[index]->setVisible(countOf(kind) > 0);
    if (kind == Kind::Mod) {
        scheduleDependencyCheck(m_env.dependencyDelayMs);
    } else {
        resumeDependencyCheck();  // the removed pack or shader may have been the last row loading
    }
    refreshTexts();
    refreshState();
}

int CartReviewDialog::countOf(Kind kind) const
{
    return int(std::count_if(m_rows.begin(), m_rows.end(), [kind](const std::unique_ptr<Row>& r) { return r->entry.kind == kind; }));
}

bool CartReviewDialog::hasMods() const
{
    return countOf(Kind::Mod) > 0;
}

bool CartReviewDialog::anyLoading() const
{
    return std::any_of(m_rows.begin(), m_rows.end(), [](const std::unique_ptr<Row>& r) { return r->state == RowState::Loading; });
}

// ---------------------------------------------------------------------------------------------------------------------
// Dependencies: one run for every mod of the dialog
// ---------------------------------------------------------------------------------------------------------------------

void CartReviewDialog::scheduleDependencyCheck(int delayMs)
{
    ++m_token;  // whatever the running check answers is stale
    if (m_resolver) {
        m_resolver->cancel();
    }
    m_depTimer.stop();
    m_depWatchdog.stop();
    m_depAwaitingVersions = false;  // the timer below decides again
    const bool candidates = std::any_of(m_rows.begin(), m_rows.end(), [](const std::unique_ptr<Row>& r) {
        return r->entry.kind == Kind::Mod && (r->state == RowState::Loading || r->state == RowState::Ready);
    });
    if (!candidates) {
        setDependencyState(DepState::Idle);
        return;
    }
    setDependencyState(DepState::Checking);
    m_depTimer.start(std::max(0, delayMs));
}

void CartReviewDialog::runDependencyCheck()
{
    if (anyLoading()) {
        // The last row to settle starts the run: a mod row schedules it (setRowState), any other row resumes it
        // (resumeDependencyCheck). Before this flag a pack or a shader settling last left "Checking dependencies…" forever.
        m_depAwaitingVersions = true;
        return;
    }
    m_depAwaitingVersions = false;
    startDependencyCheckNow();
}

void CartReviewDialog::resumeDependencyCheck()
{
    if (!m_depAwaitingVersions || m_depState != DepState::Checking || m_depTimer.isActive() || anyLoading()) {
        return;
    }
    m_depAwaitingVersions = false;
    startDependencyCheckNow();
}

void CartReviewDialog::startDependencyCheckNow()
{
    DependencyResolver::Request request;
    request.instanceId = m_target.id;
    for (const auto& r : m_rows) {
        if (r->entry.kind != Kind::Mod) {
            continue;
        }
        if (const auto version = chosenVersion(*r)) {
            if (r->entry.item->pack) {
                request.also.append({ r->entry.item->pack, *version });
            }
        }
    }
    if (request.also.isEmpty()) {
        setDependencyState(DepState::Idle);
        return;
    }
    const bool enabled = !m_env.dependencyCheckEnabled || m_env.dependencyCheckEnabled();
    if (!enabled || !m_resolver) {
        setDependencyState(DepState::Disabled);
        return;
    }
    // The safety net: whatever the resolver does, this run ends (answer, failure, or this timeout as a failure with Retry). Armed
    // before resolve() so an answer given at once stops it again.
    m_depWatchdog.start(std::max(1, m_env.dependencyTimeoutMs));
    m_resolver->resolve(m_token, request);  // may answer at once (a fake does)
}

void CartReviewDialog::onResolved(quint64 token, const QList<PlannedMod>& dependencies)
{
    if (token != m_token || m_depState != DepState::Checking) {
        return;
    }
    QSet<QString> inCart;
    const QList<InstalledMod> installed = m_env.installedMods ? m_env.installedMods() : QList<InstalledMod>();
    QStringList already;
    for (const auto& r : m_rows) {
        const auto version = r->entry.kind == Kind::Mod ? chosenVersion(*r) : std::nullopt;
        if (!version || !r->entry.item->pack) {
            continue;
        }
        inCart.insert(projectKeyOf(r->entry.item->pack));
        for (const QString& name : alreadyInstalledNames(r->entry.item->pack->provider, *version, installed)) {
            if (!already.contains(name)) {
                already.append(name);
            }
        }
    }
    const MergedDependencies merged = mergeDependencies(dependencies, inCart);
    m_deps = merged.toAdd;
    for (const QString& name : merged.alreadyInstalled) {
        if (!already.contains(name)) {
            already.append(name);
        }
    }
    m_alreadyInstalled = already;
    setDependencyState(DepState::Done);
}

void CartReviewDialog::onResolveFailed(quint64 token, const QString& reason)
{
    if (token != m_token || m_depState != DepState::Checking) {
        return;
    }
    setDependencyState(DepState::Failed, reason);
}

void CartReviewDialog::setDependencyState(DepState state, const QString& message)
{
    m_depState = state;
    if (state != DepState::Checking) {
        m_depWatchdog.stop();
        m_depAwaitingVersions = false;
    }
    if (state != DepState::Done) {
        m_deps.clear();
        m_alreadyInstalled.clear();
    }
    clearLayout(m_depRowsLayout);

    QString status;
    QString tone = QStringLiteral("secondary");
    switch (state) {
        case DepState::Idle:
            break;
        case DepState::Checking:
            status = tr("Checking dependencies…");
            break;
        case DepState::Done:
            if (m_deps.isEmpty()) {
                status = tr("No other mods needed");
            }
            break;
        case DepState::Failed:
            status = message.isEmpty() ? tr("Couldn't check dependencies") : tr("Couldn't check dependencies: %1").arg(message);
            tone = QStringLiteral("danger");
            break;
        case DepState::Disabled:
            status = tr("Dependency check is turned off in Settings");
            break;
    }
    m_depStatus->setText(status);
    Style::setProperty(m_depStatus, "zeroTone", tone);
    m_depStatus->setVisible(!status.isEmpty());
    m_depSkeleton->setVisible(state == DepState::Checking);
    m_depRetry->setVisible(state == DepState::Failed);
    {
        const QSignalBlocker block(m_skip);
        m_skip->setChecked(false);  // "Add without dependencies" is never preset
    }
    m_skip->setVisible(state == DepState::Failed);

    for (const PlannedMod& dep : std::as_const(m_deps)) {
        auto* row = new QWidget(m_depRows);
        row->setFixedHeight(kDepRowHeight);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(Tokens::spaceMd);
        layout->addWidget(new PackIconWidget(dep.pack->logoUrl, m_env.images, row, QStringLiteral("blocks"), kDepRowIcon));
        auto* name = new QLabel(dep.name().isEmpty() ? dep.pack->addonId.toString() : dep.name(), row);
        name->setObjectName(QStringLiteral("zeroDependencyName"));
        Style::applyRole(name, Role::Label);
        layout->addWidget(name);
        const QString number = versionNumber(dep.version);
        auto* version = new QLabel(number, row);
        version->setObjectName(QStringLiteral("zeroDependencyVersion"));
        Style::applyRole(version, Role::Caption);
        Style::setProperty(version, "zeroTone", "secondary");
        version->setVisible(!number.isEmpty());
        layout->addWidget(version);
        layout->addStretch(1);
        const QString requiredBy = requiredByText(dep.requiredBy);
        auto* by = new QLabel(QFontMetrics(Style::font(Role::Caption)).elidedText(requiredBy, Qt::ElideRight, kRequiredByMax), row);
        by->setObjectName(QStringLiteral("zeroDependencyRequiredBy"));
        by->setToolTip(requiredBy);
        Style::applyRole(by, Role::Caption);
        Style::setProperty(by, "zeroTone", "secondary");
        by->setProperty("fullText", requiredBy);
        by->setVisible(!requiredBy.isEmpty());
        layout->addWidget(by);
        row->setAccessibleName(requiredBy.isEmpty() ? name->text() : QStringLiteral("%1, %2").arg(name->text(), requiredBy));
        m_depRowsLayout->addWidget(row);
    }
    m_depRows->setVisible(!m_deps.isEmpty());

    const QString installed = m_alreadyInstalled.isEmpty() ? QString() : tr("Already installed: %1").arg(m_alreadyInstalled.join(QStringLiteral(", ")));
    m_depInstalled->setText(installed);
    m_depInstalled->setVisible(state == DepState::Done && !installed.isEmpty());
    m_depBlock->setVisible(state != DepState::Idle);
    refreshState();
}

// ---------------------------------------------------------------------------------------------------------------------
// State and texts
// ---------------------------------------------------------------------------------------------------------------------

CartInstallPlan CartReviewDialog::plan() const
{
    QList<CartPlanRow> rows;
    for (const auto& r : m_rows) {
        const auto version = chosenVersion(*r);
        if (!version || !r->entry.item->pack) {
            continue;  // loading, failed, skipped
        }
        CartPlanRow planned;
        planned.kind = r->entry.kind;
        planned.cartKey = r->entry.key;
        planned.pack = r->entry.item->pack;
        planned.version = *version;
        planned.update = !r->installedFileId.isEmpty();
        rows.append(planned);
    }
    const bool withDependencies = m_depState == DepState::Done;  // not while checking, after a failure, or when the check is off
    return buildCartPlan(rows, withDependencies ? m_deps : QList<PlannedMod>(), turnThemOn());
}

bool CartReviewDialog::isChecking() const
{
    return anyLoading() || dependencyPending();
}

bool CartReviewDialog::canInstall() const
{
    if (isChecking() || targetRunning() || plan().isEmpty()) {
        return false;
    }
    return m_depState != DepState::Failed || m_skip->isChecked();
}

bool CartReviewDialog::turnThemOn() const
{
    return m_turnOn && m_turnOn->isVisibleTo(this) && m_groups[1]->isVisibleTo(this) && m_turnOn->isChecked();
}

bool CartReviewDialog::targetRunning() const
{
    return m_env.isRunning ? m_env.isRunning(m_target.id) : m_target.running;
}

void CartReviewDialog::refreshTexts()
{
    const int mods = countOf(Kind::Mod);
    const int packs = countOf(Kind::ResourcePack);
    const int shaders = countOf(Kind::Shader);
    const QString title = cartTitleText(mods, packs, shaders);
    m_title->setText(title);
    setWindowTitle(title);
    m_subtitle->setText(cartSubtitleText(m_target.name, m_target.summaryFor(mods > 0)));
    m_subtitle->setToolTip(m_subtitle->text());
    m_groups[1]->setVisible(packs > 0);
    m_groups[2]->setVisible(shaders > 0);
    m_groups[0]->setVisible(mods > 0);
}

void CartReviewDialog::refreshState()
{
    if (!m_primary) {
        return;
    }
    const bool running = targetRunning();
    m_noteRow->setVisible(running);
    if (running) {
        m_note->setText(cartCloseText(m_target.name));
        Style::setProperty(m_note, "zeroTone", "warning");
    }
    refreshIcons();
    if (isChecking()) {
        m_primary->setText(cartCheckingText());
    } else {
        const CartInstallPlan planned = plan();
        m_primary->setText(cartPrimaryText(planned.countOf(Kind::Mod), planned.countOf(Kind::ResourcePack), planned.countOf(Kind::Shader)));
    }
    m_primary->setEnabled(canInstall());
    fitList();
}

void CartReviewDialog::refreshIcons()
{
    const auto& t = Theme::current();
    const bool warn = m_noteRow && m_noteRow->isVisibleTo(this) && targetRunning();
    m_noteIcon->setPixmap(Icons::pixmap(warn ? QStringLiteral("triangle-alert") : QStringLiteral("info"), warn ? t.warning : t.info, 16,
                                        devicePixelRatioF()));
    for (const auto& r : m_rows) {
        if (r->remove) {
            r->remove->setIcon(Icons::icon(QStringLiteral("x"), t.textSecondary));
        }
    }
}

void CartReviewDialog::fitList()
{
    if (!m_content || !m_content->layout()) {
        return;
    }
    QLayout* layout = m_content->layout();
    layout->activate();
    const int width = kWidth - 2 * Tokens::spaceXl;
    const int wanted = layout->hasHeightForWidth() ? layout->totalHeightForWidth(width) : layout->sizeHint().height();
    const int height = std::min(std::max(wanted, kRowHeight), kMaxListHeight);
    if (m_scroll->height() != height || m_scroll->minimumHeight() != height) {
        m_scroll->setFixedHeight(height);
        if (isVisible()) {
            adjustSize();
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Parts for tests and dev aids
// ---------------------------------------------------------------------------------------------------------------------

QString CartReviewDialog::titleText() const
{
    return m_title->text();
}

QString CartReviewDialog::subtitleText() const
{
    return m_subtitle->text();
}

QString CartReviewDialog::noteText() const
{
    return m_noteRow->isVisibleTo(this) ? m_note->text() : QString();
}

QStringList CartReviewDialog::groupTitles() const
{
    QStringList out;
    for (QWidget* group : m_groups) {
        if (group && group->isVisibleTo(this)) {
            if (auto* title = group->findChild<QLabel*>(QStringLiteral("zeroCartGroupTitle"))) {
                out.append(title->text());
            }
        }
    }
    return out;
}

QStringList CartReviewDialog::rowKeys() const
{
    QStringList out;
    for (const auto& r : m_rows) {
        out.append(r->entry.key);
    }
    return out;
}

CartReviewDialog::RowState CartReviewDialog::rowState(const QString& key) const
{
    const Row* r = row(key);
    return r ? r->state : RowState::Failed;
}

QString CartReviewDialog::rowName(const QString& key) const
{
    const Row* r = row(key);
    return r ? r->entry.item->name : QString();
}

QString CartReviewDialog::rowStatus(const QString& key) const
{
    const Row* r = row(key);
    return r && r->status->isVisibleTo(this) ? r->status->toolTip() : QString();
}

QComboBox* CartReviewDialog::versionBox(const QString& key) const
{
    const Row* r = row(key);
    return r ? r->combo : nullptr;
}

QToolButton* CartReviewDialog::removeButton(const QString& key) const
{
    const Row* r = row(key);
    return r ? r->remove : nullptr;
}

QPushButton* CartReviewDialog::retryButton(const QString& key) const
{
    const Row* r = row(key);
    return r ? r->retry : nullptr;
}

QStringList CartReviewDialog::dependencyNames() const
{
    QStringList out;
    for (auto* label : m_depRows->findChildren<QLabel*>(QStringLiteral("zeroDependencyName"))) {
        out.append(label->text());
    }
    return out;
}

QStringList CartReviewDialog::dependencyRequiredBy() const
{
    QStringList out;
    for (auto* label : m_depRows->findChildren<QLabel*>(QStringLiteral("zeroDependencyRequiredBy"))) {
        out.append(label->property("fullText").toString());
    }
    return out;
}

QString CartReviewDialog::dependencyStatus() const
{
    return m_depStatus->isVisibleTo(this) ? m_depStatus->text() : QString();
}

QString CartReviewDialog::alreadyInstalledText() const
{
    return m_depInstalled->isVisibleTo(this) ? m_depInstalled->text() : QString();
}

bool CartReviewDialog::dependencyBlockVisible() const
{
    return m_depBlock->isVisibleTo(this);
}

bool CartReviewDialog::isSettled() const
{
    if (isChecking()) {
        return false;
    }
    for (const auto& r : m_rows) {
        if (!r->icon->settled()) {
            return false;
        }
    }
    for (auto* icon : m_depRows->findChildren<PackIconWidget*>()) {
        if (!icon->settled()) {
            return false;
        }
    }
    return true;
}

void CartReviewDialog::callWhenSettled(const std::function<void()>& done, int timeoutMs)
{
    auto* timer = new QTimer(this);
    auto elapsed = std::make_shared<int>(0);
    timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, [this, timer, elapsed, done, timeoutMs]() {
        *elapsed += timer->interval();
        if (isSettled() || *elapsed >= timeoutMs) {
            timer->stop();
            timer->deleteLater();
            done();
        }
    });
    timer->start();
}

void CartReviewDialog::accept()
{
    if (!canInstall()) {
        return;  // checking, nothing to add, the target runs: Enter does nothing
    }
    m_depTimer.stop();
    m_depWatchdog.stop();
    if (m_resolver) {
        m_resolver->cancel();
    }
    QDialog::accept();
}

void CartReviewDialog::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    Paint::paintBackground(p, rect(), Theme::current().bg);
}

}  // namespace Zero::Content
