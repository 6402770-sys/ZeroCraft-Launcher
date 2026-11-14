// SPDX-License-Identifier: GPL-3.0-only
#include "ModpackInstallDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTimer>
#include <QVBoxLayout>

#include <memory>

#include "Application.h"
#include "InstanceList.h"
#include "icons/IconList.h"
#include "minecraft/MinecraftInstance.h"
#include "modplatform/ModIndex.h"
#include "net/ApiRequest.h"
#include "net/HttpMetaCache.h"
#include "net/NetJob.h"
#include "settings/SettingsObject.h"
#include "ui/zero/PillTag.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/content/InstallDialogParts.h"
#include "ui/zero/content/RemoteImageCache.h"
#include "zero/content/CreateInstanceModel.h"
#include "zero/content/ModpackInstall.h"

namespace Zero::Content {

using namespace Parts;

// ---------------------------------------------------------------------------------------------------------------------
// Environment
// ---------------------------------------------------------------------------------------------------------------------

QList<InstalledPack> installedPacksFromApplication()
{
    QList<InstalledPack> out;
    auto* list = APPLICATION->instances();
    for (int i = 0; i < list->count(); ++i) {
        MinecraftInstance* inst = list->at(i);
        if (!inst->isManagedPack() || inst->getManagedPackID().isEmpty()) {
            continue;  // an imported pack without a provider id cannot be matched with a card
        }
        out.append({ managedKey(inst->getManagedPackType(), inst->getManagedPackID()), inst->id(), inst->name(), inst->getManagedPackVersionID(),
                     inst->getManagedPackVersionName() });
    }
    return out;
}

ModpackInstallEnvironment ModpackInstallEnvironment::fromApplication(ModpackSource* source)
{
    ModpackInstallEnvironment env;
    env.create = Create::CreateInstanceEnvironment::fromApplication();
    env.source = source;
    env.installedPacks = &installedPacksFromApplication;
    env.instanceIconKey = [](const QString& id) {
        auto* inst = APPLICATION->instances()->getInstanceById(id);
        return inst ? inst->iconKey() : QString();
    };
    env.instanceGroup = [](const QString& id) { return APPLICATION->instances()->getInstanceGroup(id); };
    env.fetchLogo = [](const QString& url, const QString& fileName, std::function<void(const QString&)> done) {
        auto entry = APPLICATION->metacache()->resolveEntry(QStringLiteral("ZeroContent"), QStringLiteral("logos/") + fileName);
        if (!entry) {
            done({});
            return;
        }
        auto* job = new NetJob(QStringLiteral("ZeroContent logo"), APPLICATION->network());
        job->setAskRetry(false);
        job->addNetAction(Net::ApiRequest::makeCached(QUrl(url), entry));
        const QString path = entry->getFullPath();
        QObject::connect(job, &NetJob::succeeded, job, [job, path, done]() {
            job->deleteLater();
            done(path);
        });
        QObject::connect(job, &NetJob::failed, job, [job, done](const QString&) {
            job->deleteLater();
            done({});
        });
        QObject::connect(job, &NetJob::aborted, job, [job, done]() {
            job->deleteLater();
            done({});
        });
        job->start();
    };
    env.installIcon = [](const QString& path, const QString& iconName) { APPLICATION->icons()->installIcon(path, iconName); };
    env.network = []() { return APPLICATION->network(); };
    return env;
}

// ---------------------------------------------------------------------------------------------------------------------
// Dialog
// ---------------------------------------------------------------------------------------------------------------------

ModpackInstallDialog::ModpackInstallDialog(ModpackInstallEnvironment env, const ModpackItemPtr& item, const QString& updateInstanceId,
                                           const QString& initialGroup, QWidget* parent)
    : QDialog(parent), m_env(std::move(env)), m_item(item)
{
    Q_ASSERT(m_item);
    setObjectName(QStringLiteral("ZeroModpackInstallDialog"));
    if (!m_env.images) {
        m_env.images = RemoteImageCache::instance();
    }

    // The instance that manages this pack: the note (Install) or the target (Update).
    const QList<InstalledPack> installed = m_env.installedPacks ? m_env.installedPacks() : QList<InstalledPack>{};
    if (!updateInstanceId.isEmpty()) {
        for (const auto& pack : installed) {
            if (pack.instanceId == updateInstanceId) {
                m_installed = pack;
                m_updateMode = true;
                break;
            }
        }
    }
    if (!m_installed) {
        const QString key = installedKey(*m_item);
        for (const auto& pack : installed) {
            if (pack.key == key) {
                m_installed = pack;
                break;
            }
        }
    }
    setWindowTitle(m_updateMode ? tr("Update %1").arg(m_item->name) : tr("Install %1").arg(m_item->name));

    buildUi();

    // group (NewInstanceDialog's rule: "" first, the initial group inserted when it is not a known one)
    QStringList groups = m_env.create.groups;
    groups.prepend(QString());
    int groupIndex = groups.indexOf(initialGroup);
    if (groupIndex == -1) {
        groupIndex = 1;
        groups.insert(groupIndex, initialGroup);
    }
    m_group->addItems(groups);
    m_group->setCurrentIndex(groupIndex);

    // location: only worth showing with a choice (the Create Instance dialog's rule)
    const QList<QPair<QString, QString>> dirs = m_env.create.instanceDirs ? m_env.create.instanceDirs() : QList<QPair<QString, QString>>{};
    for (const auto& d : dirs) {
        m_location->addItem(d.first, d.second);
    }
    const int dirIndex = m_location->findData(m_env.create.lastUsedDir);
    m_location->setCurrentIndex(dirIndex >= 0 ? dirIndex : 0);
    m_locationColumn->setVisible(m_location->count() > 1 && !m_updateMode);
    m_template->setVisible(m_env.create.templateAvailable && !m_updateMode);
    m_template->setChecked(m_env.create.templateAvailable);  // NewInstanceDialog: on whenever a template exists

    m_name->setText(suggestedInstanceName(*m_item));
    if (m_updateMode) {
        m_name->setText(m_installed->instanceName);
        m_nameColumn->hide();
        m_groupRow->hide();
        if (m_env.instanceIconKey) {
            m_iconKey = m_env.instanceIconKey(m_installed->instanceId);
        }
    }

    if (m_installed) {
        if (m_updateMode) {
            m_note->setText(m_installed->versionName.isEmpty()
                                ? tr("This updates %1. You will be asked whether to update it or to create a separate instance.")
                                      .arg(m_installed->instanceName)
                                : tr("This updates %1 (now %2). You will be asked whether to update it or to create a separate instance.")
                                      .arg(m_installed->instanceName, m_installed->versionName));
        } else {
            m_note->setText(tr("%1 is already installed. Installing again asks whether to update it or create a separate instance.").arg(m_installed->instanceName));
        }
    }
    m_noteRow->setVisible(m_installed.has_value());

    connect(m_versionBox, &QComboBox::currentIndexChanged, this, &ModpackInstallDialog::onVersionChanged);
    connect(m_name, &QLineEdit::textEdited, this, [this]() {
        m_nameEdited = true;
        refreshState();
    });
    connect(m_name, &QLineEdit::textChanged, this, &ModpackInstallDialog::refreshState);
    connect(m_location, &QComboBox::currentIndexChanged, this, &ModpackInstallDialog::refreshState);
    connect(m_retry, &QPushButton::clicked, this, &ModpackInstallDialog::requestVersions);
    connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_install, &QPushButton::clicked, this, &ModpackInstallDialog::accept);
    if (m_env.source) {
        connect(m_env.source, &ModpackSource::versionsFinished, this,
                [this](quint64 generation, const ModpackItemPtr& answered, const QList<ModpackVersion>& versions) {
                    if (generation == kGeneration && answered == m_item) {
                        setVersions(versions);
                    }
                });
        connect(m_env.source, &ModpackSource::versionsFailed, this, [this](quint64 generation, const QString& error) {
            if (generation == kGeneration) {
                setVersionState(VersionState::Failed, error);
            }
        });
    }
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        refreshIcons();
        update();
    });

    setTabOrder(m_versionBox, m_name);
    setTabOrder(m_name, m_group);
    setTabOrder(m_group, m_location);
    setTabOrder(m_location, m_template);
    setTabOrder(m_template, m_cancel);
    setTabOrder(m_cancel, m_install);

    refreshIcons();
    requestVersions();
    refreshState();
}

ModpackInstallDialog::~ModpackInstallDialog() = default;

void ModpackInstallDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* body = new QWidget(this);
    auto* column = new QVBoxLayout(body);
    column->setContentsMargins(Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceLg);
    column->setSpacing(Tokens::spaceLg);

    // ---- pack header ----
    const HeaderParts header = buildHeader(body, *m_item, m_env.images, tr("Modpack"), QStringLiteral("package"));
    m_packIcon = header.icon;
    m_packName = header.name;
    m_packAuthor = header.author;
    m_headerPills = header.pills;
    column->addWidget(header.root);

    // ---- note ----
    const NoteParts note = buildNoteRow(body);
    m_noteRow = note.row;
    m_noteIcon = note.icon;
    m_note = note.text;
    column->addWidget(m_noteRow);

    // ---- version ----
    auto* versionColumn = new QVBoxLayout;
    versionColumn->setContentsMargins(0, 0, 0, 0);
    versionColumn->setSpacing(Tokens::spaceXs);
    auto* versionLabel = overline(tr("Version"), body);
    m_versionBox = new QComboBox(body);
    m_versionBox->setAccessibleName(tr("Modpack version"));
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
    m_retry = new QPushButton(tr("Retry"), body);
    Style::setProperty(m_retry, "zeroVariant", "ghost");
    m_retry->setAutoDefault(false);
    m_retry->hide();
    statusRow->addWidget(m_retry, 0, Qt::AlignTop);
    versionColumn->addLayout(statusRow);
    column->addLayout(versionColumn);

    // ---- name ----
    m_nameColumn = new QWidget(body);
    auto* nameLayout = new QVBoxLayout(m_nameColumn);
    nameLayout->setContentsMargins(0, 0, 0, 0);
    nameLayout->setSpacing(Tokens::spaceXs);
    auto* nameLabel = overline(tr("Name"), m_nameColumn);
    m_name = new QLineEdit(m_nameColumn);
    m_name->setAccessibleName(tr("Instance name"));
    m_name->setPlaceholderText(tr("Instance name"));
    m_name->setMaxLength(Create::kMaxNameLength + 64);  // longer than allowed so the inline error can explain, but bounded
    m_name->installEventFilter(this);
    nameLabel->setBuddy(m_name);
    m_nameError = new QLabel(m_nameColumn);
    Style::applyRole(m_nameError, Role::Caption);
    Style::setProperty(m_nameError, "zeroTone", "danger");
    m_nameError->setFixedHeight(QFontMetrics(Style::font(Role::Caption)).height());
    m_nameError->setVisible(false);  // only takes room while it has something to say
    nameLayout->addWidget(nameLabel);
    nameLayout->addWidget(m_name);
    nameLayout->addWidget(m_nameError);
    column->addWidget(m_nameColumn);

    // ---- group | location ----
    m_groupRow = new QWidget(body);
    auto* groupLayout = new QHBoxLayout(m_groupRow);
    groupLayout->setContentsMargins(0, 0, 0, 0);
    groupLayout->setSpacing(Tokens::spaceLg);
    auto* groupColumn = new QVBoxLayout;
    groupColumn->setContentsMargins(0, 0, 0, 0);
    groupColumn->setSpacing(Tokens::spaceXs);
    auto* groupLabel = overline(tr("Group"), m_groupRow);
    m_group = new QComboBox(m_groupRow);
    m_group->setEditable(true);
    m_group->setAccessibleName(tr("Group"));
    m_group->lineEdit()->setPlaceholderText(tr("No group"));
    m_group->setInsertPolicy(QComboBox::NoInsert);
    groupLabel->setBuddy(m_group);
    groupColumn->addWidget(groupLabel);
    groupColumn->addWidget(m_group);
    groupLayout->addLayout(groupColumn, 1);
    m_locationColumn = new QWidget(m_groupRow);
    auto* locationLayout = new QVBoxLayout(m_locationColumn);
    locationLayout->setContentsMargins(0, 0, 0, 0);
    locationLayout->setSpacing(Tokens::spaceXs);
    auto* locationLabel = overline(tr("Location"), m_locationColumn);
    m_location = new QComboBox(m_locationColumn);
    m_location->setAccessibleName(tr("Instance location"));
    m_location->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_location->setMinimumContentsLength(16);
    locationLabel->setBuddy(m_location);
    locationLayout->addWidget(locationLabel);
    locationLayout->addWidget(m_location);
    groupLayout->addWidget(m_locationColumn, 1);
    column->addWidget(m_groupRow);

    m_template = new QCheckBox(tr("Use instance template"), body);
    m_template->setAccessibleName(tr("Use instance template"));
    if (!m_env.create.templateToolTip.isEmpty()) {
        m_template->setToolTip(m_env.create.templateToolTip);
    }
    column->addWidget(m_template);
    column->addStretch(1);
    root->addWidget(body, 1);

    // ---- footer ----
    const FooterParts footer = buildFooter(this, m_updateMode ? tr("Update") : tr("Install"));
    m_footer = footer.bar;
    m_cancel = footer.cancel;
    m_install = footer.primary;
    root->addWidget(m_footer);

    setFixedWidth(kWidth);
}

void ModpackInstallDialog::refreshIcons()
{
    const auto& t = Theme::current();
    m_noteIcon->setPixmap(Icons::pixmap(QStringLiteral("info"), t.info, 16, devicePixelRatioF()));
}

// ---- versions ----

void ModpackInstallDialog::requestVersions()
{
    // What the item already carries (Modrinth / CurseForge versions loaded for the details panel, a Technic zip pack) needs no request.
    bool loaded = false;
    QList<ModpackVersion> known = knownVersions(*m_item, &loaded);
    if (!loaded && m_item->provider == Provider::Technic && m_item->technic && !m_item->technic->isSolder()) {
        // A single-zip pack has one version: the one the details name (an empty name still installs).
        ModpackVersion version;
        version.id = m_item->technic->version;
        version.name = m_item->technic->version;
        version.gameVersions = QStringList{ m_item->technic->minecraftVersion };
        version.downloadUrl = m_item->technic->downloadUrl;
        known = { version };
        loaded = true;
    }
    if (loaded) {
        setVersions(known);
        return;
    }
    if (!m_env.source) {
        setVersionState(VersionState::Failed, tr("The versions of this pack are not available."));
        return;
    }
    setVersionState(VersionState::Loading);
    m_env.source->versions(m_item, kGeneration);
}

void ModpackInstallDialog::setVersions(const QList<ModpackVersion>& versions)
{
    m_versions = versions;
    const Provider provider = m_item->provider;
    {
        const QSignalBlocker block(m_versionBox);
        m_versionBox->clear();
        auto* model = qobject_cast<QStandardItemModel*>(m_versionBox->model());
        for (int i = 0; i < versions.size(); ++i) {
            const ModpackVersion& v = versions.at(i);
            QString text = v.name.isEmpty() ? (v.number.isEmpty() ? tr("Latest") : v.number) : v.name;
            if (v.type == ModpackVersion::Type::Beta || v.type == ModpackVersion::Type::Alpha) {
                text += QStringLiteral("  (%1)").arg(versionTypeText(v.type));
            } else if (v.recommended) {
                text += QStringLiteral("  (%1)").arg(tr("recommended"));
            }
            m_versionBox->addItem(text, i);
            if (model && !isInstallable(v, provider)) {
                if (auto* item = model->item(i)) {
                    item->setEnabled(false);
                }
            }
        }
        m_versionBox->setCurrentIndex(defaultVersionIndex(versions, provider));
    }
    if (versions.isEmpty() || defaultVersionIndex(versions, provider) < 0) {
        setVersionState(VersionState::Failed, tr("This provider offers no downloadable version of this pack."));
        m_retry->hide();
    } else {
        setVersionState(VersionState::Ready);
    }
    onVersionChanged();
}

void ModpackInstallDialog::setVersionState(VersionState state, const QString& error)
{
    m_versionState = state;
    m_versionBox->setEnabled(state == VersionState::Ready);
    m_retry->setVisible(state == VersionState::Failed);
    switch (state) {
        case VersionState::Loading: {
            const QSignalBlocker block(m_versionBox);
            m_versionBox->clear();
            m_versionBox->addItem(tr("Loading versions..."));
            m_versionStatus->clear();
            break;
        }
        case VersionState::Ready:
            m_versionStatus->clear();
            break;
        case VersionState::Failed: {
            const QSignalBlocker block(m_versionBox);
            m_versionBox->clear();
            m_versionStatus->setText(error.isEmpty() ? tr("Could not load the versions.") : tr("Could not load the versions: %1").arg(error));
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

void ModpackInstallDialog::onVersionChanged()
{
    auto* layout = m_versionPills->layout();
    clearLayout(layout);
    if (const auto version = selectedVersion()) {
        auto* row = static_cast<QHBoxLayout*>(layout);
        if (const QString type = versionTypeText(version->type); !type.isEmpty()) {
            row->addWidget(new PillTag(type, versionTypeTone(version->type), m_versionPills));
        }
        if (const QString mc = minecraftText(*version); !mc.isEmpty()) {
            row->addWidget(new PillTag(tr("Minecraft %1").arg(mc), PillTag::Tone::Plain, m_versionPills));
        }
        for (const QString& loader : loaderLabels(version->loaders)) {
            row->addWidget(new PillTag(loader, PillTag::Tone::Plain, m_versionPills));
        }
        row->addStretch(1);
    }
    m_versionPills->setVisible(layout->count() > 0);
    refreshState();
}

std::optional<ModpackVersion> ModpackInstallDialog::selectedVersion() const
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

// ---- state ----

QString ModpackInstallDialog::instName() const
{
    return m_name->text().trimmed();
}

QString ModpackInstallDialog::instGroup() const
{
    return m_group->currentText();
}

QString ModpackInstallDialog::instDir() const
{
    return m_location->currentData().toString();
}

bool ModpackInstallDialog::copyTemplateDir() const
{
    return m_template->isVisibleTo(this) && m_template->isChecked();
}

QString ModpackInstallDialog::updateInstanceId() const
{
    return m_updateMode && m_installed ? m_installed->instanceId : QString();
}

QString ModpackInstallDialog::noteText() const
{
    return m_noteRow->isVisibleTo(this) ? m_note->text() : QString();
}

QString ModpackInstallDialog::nameError() const
{
    return m_nameError->text();
}

QString ModpackInstallDialog::versionStatus() const
{
    return m_versionStatus->isVisibleTo(this) ? m_versionStatus->text() : QString();
}

QStringList ModpackInstallDialog::versionPills() const
{
    QStringList out;
    for (auto* pill : m_versionPills->findChildren<PillTag*>()) {
        out.append(pill->text());
    }
    return out;
}

QStringList ModpackInstallDialog::headerPills() const
{
    QStringList out;
    for (auto* pill : m_headerPills->findChildren<PillTag*>()) {
        out.append(pill->text());
    }
    return out;
}

QString ModpackInstallDialog::packName() const
{
    return m_packName->text();
}

QString ModpackInstallDialog::packAuthor() const
{
    return m_packAuthor->isVisibleTo(this) ? m_packAuthor->text() : QString();
}

bool ModpackInstallDialog::canInstall() const
{
    if (m_busy) {
        return false;
    }
    const auto version = selectedVersion();
    if (!version || !isInstallable(*version, m_item->provider)) {
        return false;
    }
    if (m_updateMode) {
        return true;
    }
    return Create::validateName(m_name->text()).isEmpty() && !instDir().isEmpty();
}

void ModpackInstallDialog::refreshState()
{
    if (!m_install) {
        return;
    }
    const QString error = m_updateMode ? QString() : Create::validateName(m_name->text());
    const bool showError = !error.isEmpty() && (m_nameEdited || !m_name->text().isEmpty());
    m_nameError->setText(showError ? error : QString());
    m_nameError->setVisible(showError);
    m_name->setToolTip(showError ? error : QString());
    m_name->setAccessibleDescription(showError ? error : QString());
    m_install->setEnabled(canInstall());
}

bool ModpackInstallDialog::isSettled() const
{
    return m_versionState != VersionState::Loading && m_packIcon->settled();
}

void ModpackInstallDialog::callWhenSettled(const std::function<void()>& done, int timeoutMs)
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

// ---- accept ----

void ModpackInstallDialog::accept()
{
    if (!canInstall()) {
        return;
    }
    // The pack logo becomes the instance icon now, not before: download it, install it into the icon list, then close.
    const QString iconName = logoIconName(*m_item);
    if (m_updateMode || iconName.isEmpty() || !m_env.fetchLogo || !m_env.installIcon || m_logoDone) {
        QDialog::accept();
        return;
    }
    m_busy = true;
    m_install->setEnabled(false);
    m_install->setText(tr("Preparing..."));
    auto settled = std::make_shared<bool>(false);
    QPointer<ModpackInstallDialog> self(this);
    auto finish = [self, settled, iconName](const QString& path) {
        if (*settled) {
            return;
        }
        *settled = true;
        if (self) {
            self->finishAccept(path);
        }
    };
    QTimer::singleShot(kLogoTimeoutMs, this, [finish]() { finish({}); });  // no logo is no reason to keep the user waiting
    m_env.fetchLogo(m_item->iconUrl, logoFileName(*m_item), finish);
}

void ModpackInstallDialog::finishAccept(const QString& logoPath)
{
    m_busy = false;
    m_logoDone = true;
    if (!logoPath.isEmpty()) {
        m_env.installIcon(logoPath, logoIconName(*m_item));
        m_iconKey = logoIconKey(*m_item);
    }
    QDialog::accept();
}

InstanceTask* ModpackInstallDialog::extractTask()
{
    const auto version = selectedVersion();
    if (!version) {
        return nullptr;
    }
    InstallRequest request;
    request.item = m_item;
    request.version = *version;
    request.name = instName();
    request.iconKey = m_iconKey;
    if (m_updateMode && m_installed) {
        request.updateInstanceId = m_installed->instanceId;
        request.updateVersionName = m_installed->versionName;
        request.group = m_env.instanceGroup ? m_env.instanceGroup(m_installed->instanceId) : QString();
    } else {
        request.group = instGroup();
        request.targetDir = instDir();
        request.copyTemplate = copyTemplateDir();
    }
    const auto plan = planInstall(request);
    if (!plan) {
        return nullptr;
    }
    // Provider tasks parent their own dialogs (askIfShouldUpdate, optional files) to this dialog: MainWindow runs the task while it lives.
    return createInstallTask(*plan, this, m_env.network ? m_env.network() : nullptr);
}

// ---- events ----

void ModpackInstallDialog::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    Paint::paintBackground(p, rect(), Theme::current().bg);
}

void ModpackInstallDialog::keyPressEvent(QKeyEvent* event)
{
    // Enter installs only from the name field (and on the Install button, which handles its own key): QDialog would otherwise click the
    // default button from the version or group box.
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (focusWidget() == m_name && canInstall()) {
            accept();
        }
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

bool ModpackInstallDialog::eventFilter(QObject* watched, QEvent* event)
{
    // NewInstanceDialog: the first time the name field gets focus its (suggested) text is selected, ready to overwrite.
    if (watched == m_name && event->type() == QEvent::FocusIn && !m_nameSelectedOnce) {
        m_nameSelectedOnce = true;
        QTimer::singleShot(0, m_name, &QLineEdit::selectAll);
    }
    return QDialog::eventFilter(watched, event);
}

}  // namespace Zero::Content
