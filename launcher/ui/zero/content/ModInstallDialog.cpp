// SPDX-License-Identifier: GPL-3.0-only
#include "ModInstallDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTimer>
#include <QVBoxLayout>

#include <memory>

#include "Application.h"
#include "icons/IconList.h"
#include "settings/SettingsObject.h"
#include "ui/zero/PillTag.h"
#include "ui/zero/ZeroFormat.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/content/InstallDialogParts.h"
#include "ui/zero/content/RemoteImageCache.h"
#include "zero/content/ModDependencies.h"
#include "zero/content/ModpackInstall.h"
#include "zero/content/PackOrder.h"
#include "zero/content/WorldInstall.h"

namespace Zero::Content {

using namespace Parts;

namespace {
constexpr int kInstanceIcon = 20;
constexpr int kRowIcon = 24;
constexpr int kRowHeight = 32;
constexpr int kRequiredByMax = 220;  ///< px of the "Required by ..." text of a row
}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Environment
// ---------------------------------------------------------------------------------------------------------------------

ModInstallEnvironment ModInstallEnvironment::fromApplication(ModpackSource* source, std::function<QList<InstalledMod>()> installed, bool packs,
                                                            bool shaders, bool worlds)
{
    ModInstallEnvironment env;
    env.source = source;
    env.installedMods = std::move(installed);
    env.packs = packs || shaders || worlds;
    env.shaders = shaders;
    env.worlds = worlds;
    if (!env.packs) {
        env.makeResolver = [](QObject* parent) -> DependencyResolver* { return new InstanceDependencyResolver(parent); };
    }
    env.dependencyCheckEnabled = []() { return !APPLICATION->settings()->get("ModDependenciesDisabled").toBool(); };
    env.instanceIcon = [](const QString& key) { return APPLICATION->icons()->getIcon(key); };
    return env;
}

// ---------------------------------------------------------------------------------------------------------------------
// Dialog
// ---------------------------------------------------------------------------------------------------------------------

ModInstallDialog::ModInstallDialog(ModInstallEnvironment env, const ContentItemPtr& item, const TargetInstance& target,
                                   const QString& installedFileId, QWidget* parent)
    : QDialog(parent), m_env(std::move(env)), m_item(item), m_target(target), m_update(!installedFileId.isEmpty())
{
    Q_ASSERT(m_item);
    setObjectName(QStringLiteral("ZeroModInstallDialog"));
    if (!m_env.images) {
        m_env.images = RemoteImageCache::instance();
    }
    setWindowTitle(m_update ? tr("Update %1").arg(m_item->name) : tr("Add %1").arg(m_item->name));

    buildUi();

    if (m_env.makeResolver) {
        m_resolver = m_env.makeResolver(this);  // a child: destroyed (and cancelled) with the dialog
    }
    if (m_resolver) {
        connect(m_resolver, &DependencyResolver::resolved, this, &ModInstallDialog::onResolved);
        connect(m_resolver, &DependencyResolver::failed, this, &ModInstallDialog::onResolveFailed);
    }

    connect(m_versionBox, &QComboBox::currentIndexChanged, this, &ModInstallDialog::onVersionChanged);
    connect(m_retry, &QPushButton::clicked, this, &ModInstallDialog::startDependencyCheck);
    connect(m_versionRetry, &QPushButton::clicked, this, &ModInstallDialog::requestVersions);
    connect(m_skip, &QCheckBox::toggled, this, &ModInstallDialog::refreshState);
    connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_primary, &QPushButton::clicked, this, &ModInstallDialog::accept);
    if (m_env.source) {
        connect(m_env.source, &ModpackSource::versionsFinished, this,
                [this](quint64 generation, const ContentItemPtr& answered, const QList<ModpackVersion>&) {
                    if (generation == kGeneration && answered == m_item && m_item->pack) {
                        setVersions(m_item->pack->versions);
                    }
                });
        connect(m_env.source, &ModpackSource::versionsFailed, this, [this](quint64 generation, const QString& error) {
            if (generation == kGeneration) {
                setVersionState(VersionState::Failed, error.isEmpty() ? tr("Could not load the versions.") : tr("Could not load the versions: %1").arg(error), true);
            }
        });
    }
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        refreshIcons();
        update();
    });

    setTabOrder(m_versionBox, m_retry);
    setTabOrder(m_retry, m_skip);
    setTabOrder(m_skip, m_cancel);
    setTabOrder(m_cancel, m_primary);

    refreshIcons();
    requestVersions();
    refreshState();
}

ModInstallDialog::~ModInstallDialog()
{
    ++m_token;  // whatever the resolver still delivers is stale
    if (m_resolver) {
        m_resolver->cancel();
    }
}

void ModInstallDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* body = new QWidget(this);
    auto* column = new QVBoxLayout(body);
    column->setContentsMargins(Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceLg);
    column->setSpacing(Tokens::spaceLg);

    // ---- header ----
    const HeaderParts header =
        m_env.worlds  ? buildHeader(body, *m_item, m_env.images, tr("World"), QStringLiteral("earth"))
        : m_env.shaders ? buildHeader(body, *m_item, m_env.images, tr("Shader"), QStringLiteral("package"))
        : m_env.packs ? buildHeader(body, *m_item, m_env.images, tr("Resource pack"), QStringLiteral("package"))
                      : buildHeader(body, *m_item, m_env.images, tr("Mod"), QStringLiteral("blocks"));
    m_icon = header.icon;
    m_name = header.name;
    m_author = header.author;
    m_headerPills = header.pills;
    column->addWidget(header.root);

    // ---- "Adding to <icon> <instance> . 1.21.1 Fabric" ----
    auto* targetRow = new QWidget(body);
    auto* targetLayout = new QHBoxLayout(targetRow);
    targetLayout->setContentsMargins(0, 0, 0, 0);
    targetLayout->setSpacing(Tokens::spaceSm);
    m_targetCaption = new QLabel(m_update ? tr("Updating in") : tr("Adding to"), targetRow);
    Style::applyRole(m_targetCaption, Role::Caption);
    Style::setProperty(m_targetCaption, "zeroTone", "secondary");
    targetLayout->addWidget(m_targetCaption);
    m_targetIcon = new QLabel(targetRow);
    m_targetIcon->setFixedSize(kInstanceIcon, kInstanceIcon);
    targetLayout->addWidget(m_targetIcon);
    m_targetName = new QLabel(targetRow);
    Style::applyRole(m_targetName, Role::Label);
    m_targetName->setText(QFontMetrics(m_targetName->font()).elidedText(m_target.name, Qt::ElideRight, 220));
    m_targetName->setToolTip(m_target.name);
    targetLayout->addWidget(m_targetName);
    m_targetVersion = new QLabel(QStringLiteral("· %1").arg(m_target.summaryFor(!m_env.packs)), targetRow);
    Style::applyRole(m_targetVersion, Role::Caption);
    Style::setProperty(m_targetVersion, "zeroTone", "secondary");
    targetLayout->addWidget(m_targetVersion);
    targetLayout->addStretch(1);
    targetRow->setAccessibleName(tr("Target instance: %1, %2").arg(m_target.name, m_target.summaryFor(!m_env.packs)));
    column->addWidget(targetRow);

    // ---- note (the target runs) ----
    const NoteParts note = buildNoteRow(body);
    m_noteRow = note.row;
    m_noteIcon = note.icon;
    m_note = note.text;
    m_noteRow->hide();
    column->addWidget(m_noteRow);

    // ---- version ----
    auto* versionColumn = new QVBoxLayout;
    versionColumn->setContentsMargins(0, 0, 0, 0);
    versionColumn->setSpacing(Tokens::spaceXs);
    auto* versionLabel = overline(tr("Version"), body);
    m_versionBox = new QComboBox(body);
    m_versionBox->setAccessibleName(m_env.worlds ? tr("World file") : m_env.shaders ? tr("Shader version") : m_env.packs ? tr("Pack version") : tr("Mod version"));
    m_versionBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_versionBox->setMinimumContentsLength(20);
    versionLabel->setBuddy(m_versionBox);
    versionColumn->addWidget(versionLabel);
    versionColumn->addWidget(m_versionBox);
    m_versionPills = new QWidget(body);
    auto* versionPills = new QHBoxLayout(m_versionPills);
    versionPills->setContentsMargins(0, Tokens::spaceXs, 0, 0);
    versionPills->setSpacing(6);
    versionColumn->addWidget(m_versionPills);
    auto* statusRow = new QHBoxLayout;
    statusRow->setContentsMargins(0, 0, 0, 0);
    statusRow->setSpacing(Tokens::spaceSm);
    m_versionStatus = new QLabel(body);
    Style::applyRole(m_versionStatus, Role::Caption);
    Style::setProperty(m_versionStatus, "zeroTone", "secondary");
    m_versionStatus->setWordWrap(true);
    statusRow->addWidget(m_versionStatus, 1);
    m_versionRetry = new QPushButton(tr("Retry"), body);
    Style::setProperty(m_versionRetry, "zeroVariant", "ghost");
    m_versionRetry->setAutoDefault(false);
    m_versionRetry->setAccessibleName(tr("Retry loading the versions"));
    m_versionRetry->hide();
    statusRow->addWidget(m_versionRetry, 0, Qt::AlignTop);
    versionColumn->addLayout(statusRow);
    // shaders: the game, not the launcher, picks the active shader (nothing is written to its settings here)
    m_shaderNote = new QLabel(tr("Then pick it in the game's shader settings."), body);
    Style::applyRole(m_shaderNote, Role::Caption);
    Style::setProperty(m_shaderNote, "zeroTone", "secondary");
    m_shaderNote->setWordWrap(true);
    m_shaderNote->setVisible(m_env.shaders);
    versionColumn->addWidget(m_shaderNote);
    // worlds: how the chosen file fits the target (a pure rule, WorldInstall.h), or that the file is not ours to download
    m_worldNote = new QLabel(body);
    Style::applyRole(m_worldNote, Role::Caption);
    Style::setProperty(m_worldNote, "zeroTone", "secondary");
    m_worldNote->setWordWrap(true);
    m_worldNote->hide();
    versionColumn->addWidget(m_worldNote);
    column->addLayout(versionColumn);

    // ---- dependencies ----
    m_depBlock = new QWidget(body);
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
    m_retry = new QPushButton(tr("Retry"), m_depBlock);
    Style::setProperty(m_retry, "zeroVariant", "ghost");
    m_retry->setAutoDefault(false);
    m_retry->setAccessibleName(tr("Retry the dependency check"));
    m_retry->hide();
    depStatusRow->addWidget(m_retry, 0, Qt::AlignTop);
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
    m_depBlock->setVisible(!m_env.packs);  // resource packs have no dependencies
    column->addWidget(m_depBlock);
    // resource packs: put the pack on top of the game's pack list (options.txt); only where the game keeps that list the 1.13+ way
    m_turnOn = new QCheckBox(tr("Turn it on"), body);
    m_turnOn->setChecked(true);
    m_turnOn->setAccessibleName(tr("Turn it on"));
    m_turnOn->setToolTip(tr("Puts the pack at the top of the game's resource pack list"));
    m_turnOn->setVisible(m_env.packs && !m_env.shaders && !m_env.worlds && !m_update && Packs::supportsOrdering(m_target.mcVersion));  // an update keeps its place
    column->addWidget(m_turnOn);
    column->addStretch(1);
    root->addWidget(body, 1);

    // ---- footer ----
    const FooterParts footer =
        buildFooter(this, m_env.worlds    ? worldPrimaryActionText()
                          : m_env.shaders ? shaderPrimaryActionText(m_update)
                          : m_env.packs   ? packPrimaryActionText(m_update)
                                          : primaryActionText(1, m_update));
    m_footer = footer.bar;
    m_cancel = footer.cancel;
    m_primary = footer.primary;
    root->addWidget(m_footer);

    setFixedWidth(kWidth);
}

void ModInstallDialog::refreshIcons()
{
    const auto& t = Theme::current();
    const bool warn = m_noteRow && m_noteRow->isVisibleTo(this) && targetRunning();
    m_noteIcon->setPixmap(Icons::pixmap(warn ? QStringLiteral("triangle-alert") : QStringLiteral("info"), warn ? t.warning : t.info, 16,
                                        devicePixelRatioF()));
    QIcon icon;
    if (m_env.instanceIcon) {
        icon = m_env.instanceIcon(m_target.iconKey);
    } else if (APPLICATION && APPLICATION->icons()) {
        icon = APPLICATION->icons()->getIcon(m_target.iconKey);
    }
    m_targetIcon->setPixmap(icon.isNull() ? QPixmap() : icon.pixmap(QSize(kInstanceIcon, kInstanceIcon), devicePixelRatioF()));
}

bool ModInstallDialog::targetRunning() const
{
    return m_env.isRunning ? m_env.isRunning(m_target.id) : m_target.running;
}

// ---- versions ----

void ModInstallDialog::requestVersions()
{
    // What the item already carries (the details panel loaded it under the target's filter) needs no request.
    if (m_item->pack && m_item->pack->versionsLoaded) {
        setVersions(m_item->pack->versions);
        return;
    }
    if (!m_env.source) {
        setVersionState(VersionState::Failed, m_env.worlds ? tr("The files of this world are not available.")
                                              : m_env.shaders ? tr("The versions of this shader are not available.")
                                              : m_env.packs ? tr("The versions of this pack are not available.")
                                                            : tr("The versions of this mod are not available."));
        return;
    }
    setVersionState(VersionState::Loading);
    m_env.source->versions(m_item, kGeneration);
}

void ModInstallDialog::setVersions(const QList<ModPlatform::IndexedVersion>& all)
{
    // A resource pack carries no mod loader ("minecraft" on Modrinth): only the Minecraft version decides. A world is offered in every
    // file the project has (an older world opens in a newer game; the note under the version says how the file fits), newest first as the
    // provider sent them; a file without a download URL is still listed (the primary action then opens the project's page).
    if (m_env.worlds) {
        m_versions = all;
    } else {
        m_versions = compatibleVersions(all, m_target.mcVersion, m_env.packs ? ModPlatform::ModLoaderTypes() : m_target.loaders);
    }
    {
        const QSignalBlocker block(m_versionBox);
        m_versionBox->clear();
        for (int i = 0; i < m_versions.size(); ++i) {
            const ModPlatform::IndexedVersion& v = m_versions.at(i);
            QString text = v.version.isEmpty() ? v.versionNumber : v.version;
            if (v.versionType == ModPlatform::IndexedVersionType::Beta || v.versionType == ModPlatform::IndexedVersionType::Alpha) {
                text += QStringLiteral("  (%1)").arg(versionTypeText(versionFromIndexed(v).type));
            }
            m_versionBox->addItem(text.isEmpty() ? tr("Latest") : text, i);
        }
        if (m_env.worlds) {
            QList<QStringList> fileVersions;
            for (const ModPlatform::IndexedVersion& v : m_versions)
                fileVersions.append(v.mcVersion);
            m_versionBox->setCurrentIndex(defaultWorldFileIndex(fileVersions, m_target.mcVersion));
        } else {
            m_versionBox->setCurrentIndex(defaultModVersionIndex(m_versions));
        }
    }
    if (m_versions.isEmpty() && m_env.worlds) {
        setVersionState(VersionState::Failed, tr("This world has no files to add."));
    } else if (m_versions.isEmpty()) {
        setVersionState(VersionState::Failed, (m_env.shaders ? tr("No version of this shader fits %1.")
                                               : m_env.packs ? tr("No version of this pack fits %1.")
                                                             : tr("No version of this mod fits %1."))
                                                  .arg(m_target.summaryFor(!m_env.packs)));
    } else {
        setVersionState(VersionState::Ready);
    }
}

void ModInstallDialog::setVersionState(VersionState state, const QString& message, bool retryable)
{
    m_versionState = state;
    m_versionBox->setEnabled(state == VersionState::Ready);
    m_versionRetry->setVisible(state == VersionState::Failed && retryable);
    switch (state) {
        case VersionState::Loading: {
            const QSignalBlocker block(m_versionBox);
            m_versionBox->clear();
            m_versionBox->addItem(tr("Loading versions…"));
            m_versionStatus->clear();
            break;
        }
        case VersionState::Ready:
            m_versionStatus->clear();
            break;
        case VersionState::Failed: {
            const QSignalBlocker block(m_versionBox);
            m_versionBox->clear();
            m_versionStatus->setText(message.isEmpty() ? tr("Could not load the versions.") : message);
            Style::setProperty(m_versionStatus, "zeroTone", "danger");
            break;
        }
    }
    if (state != VersionState::Failed) {
        Style::setProperty(m_versionStatus, "zeroTone", "secondary");
    }
    m_versionStatus->setVisible(!m_versionStatus->text().isEmpty());
    onVersionChanged();
}

std::optional<ModPlatform::IndexedVersion> ModInstallDialog::selectedVersion() const
{
    if (m_versionState != VersionState::Ready) {
        return std::nullopt;
    }
    const int index = m_versionBox->currentData().toInt();
    if (m_versionBox->currentIndex() < 0 || index < 0 || index >= m_versions.size()) {
        return std::nullopt;
    }
    return m_versions.at(index);
}

void ModInstallDialog::onVersionChanged()
{
    auto* layout = m_versionPills->layout();
    clearLayout(layout);
    if (const auto version = selectedVersion()) {
        const ModpackVersion shown = versionFromIndexed(*version);
        auto* row = static_cast<QHBoxLayout*>(layout);
        if (const QString type = versionTypeText(shown.type); !type.isEmpty()) {
            row->addWidget(new PillTag(type, versionTypeTone(shown.type), m_versionPills));
        }
        if (const QString mc = minecraftText(shown); !mc.isEmpty()) {
            row->addWidget(new PillTag(tr("Minecraft %1").arg(mc), PillTag::Tone::Plain, m_versionPills));
        }
        for (const QString& loader : loaderLabels(shown.loaders)) {
            row->addWidget(new PillTag(loader, PillTag::Tone::Plain, m_versionPills));
        }
        if (shown.size >= 0) {
            row->addWidget(new PillTag(Format::bytes(shown.size), PillTag::Tone::Plain, m_versionPills));
        }
        row->addStretch(1);
    }
    m_versionPills->setVisible(layout->count() > 0);
    refreshWorldNote();
    startDependencyCheck();  // a new version = a new check; the answer for the old one is stale
}

// ---- worlds ----

bool ModInstallDialog::opensWebsite() const
{
    const auto version = selectedVersion();
    return m_env.worlds && version && !worldFileDownloadable(*version);
}

QUrl ModInstallDialog::websiteUrl() const
{
    return webUrl(m_item->websiteUrl);
}

QString ModInstallDialog::worldNoteText() const
{
    return m_worldNote->isVisibleTo(this) ? m_worldNote->text() : QString();
}

void ModInstallDialog::refreshWorldNote()
{
    if (!m_env.worlds || !m_worldNote) {
        return;
    }
    QString text;
    QString tone = QStringLiteral("secondary");
    if (const auto version = selectedVersion()) {
        if (!worldFileDownloadable(*version)) {
            text = worldOnlyOnWebsiteText();
        } else {
            const WorldFit fit = worldFit(version->mcVersion, m_target.mcVersion, m_target.name);
            text = fit.text;
            tone = fit.tone == WorldFit::Tone::Warning ? QStringLiteral("warning") : QStringLiteral("secondary");
        }
    }
    m_worldNote->setText(text);
    Style::setProperty(m_worldNote, "zeroTone", tone);
    m_worldNote->setVisible(!text.isEmpty());
}

// ---- dependencies ----

void ModInstallDialog::startDependencyCheck()
{
    ++m_token;
    if (m_resolver) {
        m_resolver->cancel();
    }
    const auto version = selectedVersion();
    if (!version) {
        setDependencyState(DepState::Idle);
        return;
    }
    const bool enabled = !m_env.dependencyCheckEnabled || m_env.dependencyCheckEnabled();
    if (m_env.packs || !enabled || !m_resolver || !m_item->pack) {  // packs: nothing to check (the block is hidden)
        setDependencyState(DepState::Disabled);
        return;
    }
    setDependencyState(DepState::Checking);
    DependencyResolver::Request request;
    request.pack = m_item->pack;
    request.version = *version;
    request.instanceId = m_target.id;
    m_resolver->resolve(m_token, request);  // may answer at once (a fake does)
}

void ModInstallDialog::onResolved(quint64 token, const QList<PlannedMod>& dependencies)
{
    if (token != m_token || m_depState != DepState::Checking) {
        return;
    }
    const auto version = selectedVersion();
    if (!version) {
        return;
    }
    m_deps.clear();
    QStringList skipped;
    for (const PlannedMod& dep : dependencies) {
        if (dep.maybeInstalled) {
            skipped.append(dep.name().isEmpty() ? dep.pack->addonId.toString() : dep.name());
        } else {
            m_deps.append(dep);
        }
    }
    const QList<InstalledMod> installed = m_env.installedMods ? m_env.installedMods() : QList<InstalledMod>();
    m_alreadyInstalled = alreadyInstalledNames(m_item->pack->provider, *version, installed);
    for (const QString& name : skipped) {
        if (!m_alreadyInstalled.contains(name)) {
            m_alreadyInstalled.append(name);
        }
    }
    setDependencyState(DepState::Done);
}

void ModInstallDialog::onResolveFailed(quint64 token, const QString& reason)
{
    if (token != m_token || m_depState != DepState::Checking) {
        return;
    }
    setDependencyState(DepState::Failed, reason);
}

void ModInstallDialog::setDependencyState(DepState state, const QString& message)
{
    m_depState = state;
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
    m_retry->setVisible(state == DepState::Failed);
    {
        const QSignalBlocker block(m_skip);
        m_skip->setChecked(false);  // "Add without dependencies" is never preset
    }
    m_skip->setVisible(state == DepState::Failed);

    for (const PlannedMod& dep : std::as_const(m_deps)) {
        auto* row = new QWidget(m_depRows);
        row->setFixedHeight(kRowHeight);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(Tokens::spaceMd);
        layout->addWidget(new PackIconWidget(dep.pack->logoUrl, m_env.images, row, QStringLiteral("blocks"), kRowIcon));
        auto* name = new QLabel(dep.name().isEmpty() ? dep.pack->addonId.toString() : dep.name(), row);
        name->setObjectName(QStringLiteral("zeroDependencyName"));
        Style::applyRole(name, Role::Label);
        layout->addWidget(name);
        const QString number = dep.version.versionNumber.isEmpty() ? dep.version.version : dep.version.versionNumber;
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
    refreshState();
}

// ---- state ----

ModInstallPlan ModInstallDialog::plan() const
{
    const auto version = selectedVersion();
    if (!version || !m_item->pack) {
        return {};
    }
    if (m_env.packs) {
        return buildPackInstallPlan(m_item->pack, *version, m_update);
    }
    const bool withDependencies = m_depState == DepState::Done;  // not while checking, after a failure, or when the check is off
    return buildInstallPlan(m_item->pack, *version, m_update, withDependencies ? m_deps : QList<PlannedMod>());
}

bool ModInstallDialog::canInstall() const
{
    if (opensWebsite()) {
        return websiteUrl().isValid();  // nothing is added: the project's page opens (also while the target runs)
    }
    if (!selectedVersion() || targetRunning()) {
        return false;
    }
    switch (m_depState) {
        case DepState::Done:
        case DepState::Disabled:
            return true;
        case DepState::Failed:
            return m_skip->isChecked();
        case DepState::Idle:
        case DepState::Checking:
            break;
    }
    return false;
}

void ModInstallDialog::refreshState()
{
    if (!m_primary) {
        return;
    }
    const bool running = targetRunning();
    m_noteRow->setVisible(running);
    if (running) {
        m_note->setText((m_env.worlds ? tr("Close %1 to add worlds") : m_env.shaders ? tr("Close %1 to add shaders") : m_env.packs ? tr("Close %1 to add packs") : tr("Close %1 to add mods"))
                            .arg(m_target.name));
        Style::setProperty(m_note, "zeroTone", "warning");
    }
    refreshIcons();
    const int count = plan().count();
    m_primary->setText(m_env.worlds    ? (opensWebsite() ? worldOpenWebsiteText() : worldPrimaryActionText())
                       : m_env.shaders ? shaderPrimaryActionText(m_update)
                       : m_env.packs   ? packPrimaryActionText(m_update)
                                       : primaryActionText(count > 0 ? count : 1, m_update));
    m_primary->setEnabled(canInstall());
}

bool ModInstallDialog::turnItOn() const
{
    return m_turnOn && m_turnOn->isVisibleTo(this) && m_turnOn->isChecked();
}

QString ModInstallDialog::modName() const
{
    return m_name->text();
}

QString ModInstallDialog::modAuthor() const
{
    return m_author->isVisibleTo(this) ? m_author->text() : QString();
}

QStringList ModInstallDialog::headerPills() const
{
    QStringList out;
    for (auto* pill : m_headerPills->findChildren<PillTag*>()) {
        out.append(pill->text());
    }
    return out;
}

QStringList ModInstallDialog::versionPills() const
{
    QStringList out;
    for (auto* pill : m_versionPills->findChildren<PillTag*>()) {
        out.append(pill->text());
    }
    return out;
}

QString ModInstallDialog::targetLine() const
{
    return QStringLiteral("%1 %2 %3").arg(m_targetCaption->text(), m_target.name, m_targetVersion->text());
}

QString ModInstallDialog::noteText() const
{
    return m_noteRow->isVisibleTo(this) ? m_note->text() : QString();
}

QString ModInstallDialog::versionStatus() const
{
    return m_versionStatus->isVisibleTo(this) ? m_versionStatus->text() : QString();
}

bool ModInstallDialog::isChecking() const
{
    return m_depState == DepState::Checking && m_depSkeleton->isVisibleTo(this);
}

QStringList ModInstallDialog::dependencyNames() const
{
    QStringList out;
    for (auto* label : m_depRows->findChildren<QLabel*>(QStringLiteral("zeroDependencyName"))) {
        out.append(label->text());
    }
    return out;
}

QStringList ModInstallDialog::dependencyRequiredBy() const
{
    QStringList out;
    for (auto* label : m_depRows->findChildren<QLabel*>(QStringLiteral("zeroDependencyRequiredBy"))) {
        out.append(label->property("fullText").toString());
    }
    return out;
}

QString ModInstallDialog::dependencyStatus() const
{
    return m_depStatus->isVisibleTo(this) ? m_depStatus->text() : QString();
}

QString ModInstallDialog::alreadyInstalledText() const
{
    return m_depInstalled->isVisibleTo(this) ? m_depInstalled->text() : QString();
}

bool ModInstallDialog::isSettled() const
{
    if (m_versionState == VersionState::Loading || m_depState == DepState::Checking || !m_icon->settled()) {
        return false;
    }
    for (auto* icon : m_depRows->findChildren<PackIconWidget*>()) {
        if (!icon->settled()) {
            return false;
        }
    }
    return true;
}

void ModInstallDialog::callWhenSettled(const std::function<void()>& done, int timeoutMs)
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

void ModInstallDialog::accept()
{
    if (!canInstall()) {
        return;
    }
    if (opensWebsite()) {
        // The author turned third-party downloads off: the page of the project is where the file is. Nothing is added.
        QDesktopServices::openUrl(websiteUrl());
        QDialog::reject();
        return;
    }
    if (m_resolver) {
        m_resolver->cancel();
    }
    QDialog::accept();
}

void ModInstallDialog::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    Paint::paintBackground(p, rect(), Theme::current().bg);
}

}  // namespace Zero::Content
