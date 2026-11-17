// SPDX-License-Identifier: GPL-3.0-only
#include "PerformanceDialog.h"

#include <QCheckBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QRegularExpression>
#include <QShowEvent>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>

#include "Application.h"
#include "HardwareInfo.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/mod/Mod.h"
#include "minecraft/mod/ModFolderModel.h"
#include "java/JavaInstallList.h"
#include "settings/SettingsObject.h"
#include "ui/zero/PillTag.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroSpinner.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/content/InstallDialogParts.h"
#include "ui/zero/content/RemoteImageCache.h"
#include "ui/zero/pages/JavaDoctorView.h"
#include "zero/content/CartInstall.h"
#include "zero/content/ModDependencies.h"
#include "zero/content/ModpackSource.h"

namespace Zero::Perf {

using namespace Zero::Content;
using namespace Zero::Content::Parts;

namespace {

constexpr int kRowHeight = 48;
constexpr int kRowIcon = 32;
constexpr int kSkeletonBlocks = 3;  ///< x2 rows each

/// One row of the checklist: a click anywhere on it flips the box (when the row can be ticked).
class RowWidget : public QWidget {
   public:
    using QWidget::QWidget;
    QCheckBox* box = nullptr;

   protected:
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (box && box->isEnabled() && event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
            box->toggle();
        }
        QWidget::mouseReleaseEvent(event);
    }
};

PillTag::Tone toneOf(RowState state)
{
    switch (state) {
        case RowState::Installed:
        case RowState::InstalledAlternative:
            return PillTag::Tone::Success;
        case RowState::ConflictOptiFine:
            return PillTag::Tone::Warning;
        case RowState::Unavailable:
        case RowState::Offered:
            break;
    }
    return PillTag::Tone::Plain;
}

QLabel* makeLabel(const QString& text, Role role, const char* tone, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    Style::applyRole(label, role);
    if (tone) {
        Style::setProperty(label, "zeroTone", tone);
    }
    return label;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// The production environment
// ---------------------------------------------------------------------------------------------------------------------

namespace {

/// Reads the mods folder once it is loaded (the same wait the dependency check does: update() and its updateFinished(), then the parse tasks
/// that read the mod ids), at most `kMaxMs` in all; answers exactly once.
class InstalledReader : public QObject {
   public:
    static constexpr int kMaxMs = 6000;

    InstalledReader(const QString& instanceId, std::function<void(const InstalledSnapshot&)> done) : m_id(instanceId), m_done(std::move(done))
    {
        m_limit.setSingleShot(true);
        connect(&m_limit, &QTimer::timeout, this, [this]() { finish(); });
    }

    void start()
    {
        auto* inst = APPLICATION->instances()->getInstanceById(m_id);
        auto* model = inst ? inst->loaderModList() : nullptr;
        if (!model) {
            finish();
            return;
        }
        m_model = model;
        m_limit.start(kMaxMs);
        connect(model, &ResourceFolderModel::updateFinished, this, [this]() { afterUpdate(); });
        connect(model, &ResourceFolderModel::parseFinished, this, [this]() { afterParse(); });
        model->update();
    }

   private:
    void afterUpdate()
    {
        if (m_model && m_model->hasPendingParseTasks()) {
            return;  // parseFinished() follows
        }
        finish();
    }
    void afterParse()
    {
        if (m_model && !m_model->hasPendingParseTasks()) {
            finish();
        }
    }
    void finish()
    {
        if (m_finished) {
            return;
        }
        m_finished = true;
        m_limit.stop();
        InstalledSnapshot snapshot;
        if (m_model) {
            for (Resource* resource : m_model->allResources()) {
                auto* mod = dynamic_cast<Mod*>(resource);
                if (!mod) {
                    continue;
                }
                const bool enabled = mod->enabled();
                const QString modId = mod->modId();
                if (!modId.isEmpty()) {
                    snapshot.modIds.append(modId);
                }
                if (enabled) {
                    ++snapshot.count;
                    snapshot.names.append(mod->fileinfo().fileName());
                    snapshot.names.append(mod->name());
                }
                if (const auto metadata = mod->metadata(); metadata && metadata->isValid() && metadata->provider == ModPlatform::ResourceProvider::MODRINTH) {
                    Zero::Content::InstalledMod entry;
                    entry.key = managedKey(managedPackType(Provider::Modrinth), metadata->projectId.toString());
                    entry.fileId = metadata->fileId.toString();
                    entry.name = metadata->name;
                    snapshot.withMetadata.append(entry);
                }
            }
        }
        auto done = std::move(m_done);
        deleteLater();
        done(snapshot);
    }

    QString m_id;
    std::function<void(const InstalledSnapshot&)> m_done;
    QPointer<ModFolderModel> m_model;
    QTimer m_limit;
    bool m_finished = false;
};

}  // namespace

PerformanceEnvironment PerformanceEnvironment::fromApplication(const QString& instanceId,
                                                               std::function<void(const Zero::Content::CartInstallPlan&)> install)
{
    PerformanceEnvironment env;
    auto* inst = APPLICATION->instances()->getInstanceById(instanceId);
    env.target = targetFromInstance(inst);
    env.managedPack = inst && inst->isManagedPack();
    env.ramMb = int(HardwareInfo::totalRamMiB());
    env.makeCatalog = [](QObject* parent) -> PerformanceCatalog* { return new ModrinthPerformanceCatalog(parent); };
    env.makeResolver = [](QObject* parent) -> DependencyResolver* { return new InstanceDependencyResolver(parent); };
    env.dependencyCheckEnabled = []() { return !APPLICATION->settings()->get("ModDependenciesDisabled").toBool(); };
    env.loadInstalled = [instanceId](QObject* context, std::function<void(const InstalledSnapshot&)> done) {
        // The reader may outlive nothing it needs: it is parentless, answers once and deletes itself; the answer is dropped when `context` is gone.
        QPointer<QObject> guard(context);
        auto* reader = new InstalledReader(instanceId, [guard, done = std::move(done)](const InstalledSnapshot& snapshot) {
            if (guard) {
                done(snapshot);
            }
        });
        reader->start();
    };
    env.maxMemoryMb = [instanceId]() {
        auto* instance = APPLICATION->instances()->getInstanceById(instanceId);
        if (!instance) {
            return 0;
        }
        auto* settings = instance->settings();
        return currentMaxMb(settings->get("OverrideMemory").toBool(), settings->get("MaxMemAlloc").toInt(),
                            APPLICATION->settings()->get("MaxMemAlloc").toInt());
    };
    env.checkJava = [instanceId](QObject* context, std::function<void(const std::optional<QString>&)> done) {
        const auto evaluate = [instanceId, done]() {
            auto* instance = APPLICATION->instances()->getInstanceById(instanceId);
            if (!instance) {
                done(std::nullopt);
                return;
            }
            const Java::InstanceJava java = JavaGather::instanceJava(instance);
            const Java::Diagnosis diagnosis = Java::diagnose(java, JavaGather::runtimes(), JavaGather::global());
            done(javaProblemText(java, diagnosis));
        };
        // Java Doctor's own detection (a process per candidate, off the GUI thread); an answer that is already there is used at once.
        Task::Ptr task = APPLICATION->javalist()->getLoadTask(true);
        if (!task || task->isFinished()) {
            evaluate();
            return;
        }
        QObject::connect(task.get(), &Task::finished, context, evaluate, Qt::SingleShotConnection);
    };
    env.javaMajor = [instanceId]() {
        auto* instance = APPLICATION->instances()->getInstanceById(instanceId);
        if (!instance) {
            return 0;
        }
        const Java::InstanceJava java = JavaGather::instanceJava(instance);
        const Java::Diagnosis diagnosis = Java::diagnose(java, JavaGather::runtimes(), JavaGather::global());
        if (diagnosis.effective && diagnosis.effective->major > 0) {
            return diagnosis.effective->major;
        }
        if (diagnosis.downloadMajor > 0) {
            return diagnosis.downloadMajor;
        }
        // The version the last launch stored ("17.0.8", "1.8.0_392").
        QString stored = java.storedVersion.trimmed();
        if (stored.startsWith(QStringLiteral("1."))) {
            stored.remove(0, 2);
        }
        return stored.section(QRegularExpression(QStringLiteral("[^0-9]")), 0, 0).toInt();
    };
    env.cpuCores = []() { return QThread::idealThreadCount(); };
    env.currentJvmArgs = [instanceId]() {
        auto* instance = APPLICATION->instances()->getInstanceById(instanceId);
        return instance ? instance->settings()->get(QStringLiteral("JvmArgs")).toString() : QString();
    };
    env.writeJvmArgs = [instanceId](const QString& args) {
        auto* instance = APPLICATION->instances()->getInstanceById(instanceId);
        return instance ? applyJvmArgs(instance->settings(), args) : JvmArgsPrevious{};
    };
    env.isRunning = [instanceId]() {
        auto* instance = APPLICATION->instances()->getInstanceById(instanceId);
        return instance && instance->isRunning();
    };
    env.install = std::move(install);
    env.writeMemory = [instanceId](int maxMb, int minMb) {
        auto* instance = APPLICATION->instances()->getInstanceById(instanceId);
        if (!instance) {
            return;
        }
        auto* settings = instance->settings();
        settings->set("OverrideMemory", true);
        settings->set("MaxMemAlloc", maxMb);
        settings->set("MinMemAlloc", minMb);
    };
    return env;
}

// ---------------------------------------------------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------------------------------------------------

PerformanceDialog::PerformanceDialog(PerformanceEnvironment env, QWidget* parent) : QDialog(parent), m_env(std::move(env))
{
    setObjectName(QStringLiteral("ZeroPerformanceDialog"));
    if (!m_env.images) {
        m_env.images = RemoteImageCache::instance();
    }
    m_loader = m_env.target.modReady() ? loaderOf(m_env.target.loaders) : Loader::None;
    m_depTimer.setSingleShot(true);
    connect(&m_depTimer, &QTimer::timeout, this, &PerformanceDialog::runDependencyCheck);

    buildUi();

    if (m_env.makeResolver) {
        m_resolver = m_env.makeResolver(this);  // a child: destroyed (and cancelled) with the dialog
    }
    if (m_resolver) {
        connect(m_resolver, &DependencyResolver::resolved, this, &PerformanceDialog::onResolved);
        connect(m_resolver, &DependencyResolver::failed, this, &PerformanceDialog::onResolveFailed);
    }
    connect(m_depRetry, &QPushButton::clicked, this, [this]() { scheduleDependencyCheck(0); });
    connect(m_retry, &QPushButton::clicked, this, [this]() { startCatalog(); });
    connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_apply, &QPushButton::clicked, this, &PerformanceDialog::accept);
    connect(m_memoryBox, &QCheckBox::clicked, this, [this]() {
        m_memoryTouched = true;
        refreshSummary();
    });
    connect(m_memoryBox, &QCheckBox::toggled, this, &PerformanceDialog::refreshSummary);
    // Only `clicked` (mouse and keyboard): `toggled` fires before it, and a refresh then would put the box back to its default before
    // `m_jvmTouched` is set. Programmatic changes in refreshJvmArgs are signal-blocked anyway.
    connect(m_jvmBox, &QCheckBox::clicked, this, [this]() {
        m_jvmTouched = true;
        refreshSummary();
    });
    connect(m_jvmDetailsButton, &QPushButton::toggled, this, [this](bool shown) {
        m_jvmDetails->setVisible(shown);
        m_jvmDetailsButton->setText(shown ? tr("Hide details") : tr("What they do"));
        fitBody();
    });
    connect(m_javaFix, &QPushButton::clicked, this, [this]() {
        m_javaFixRequested = true;
        reject();
    });
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        refreshIcons();
        update();
    });

    setTabOrder(m_cancel, m_apply);
    refreshIcons();

    // The mods section: vanilla has one line; a loader asks Modrinth.
    if (m_loader == Loader::None) {
        setModsState(ModsState::Vanilla);
    } else {
        setModsState(ModsState::Loading);
    }
    startInstalled();
    if (m_loader != Loader::None) {
        startCatalog();
    }
    startJava();
    refreshSummary();
}

PerformanceDialog::~PerformanceDialog()
{
    m_depTimer.stop();
    ++m_depToken;  // whatever the resolver still delivers is stale
    ++m_catalogToken;
    if (m_resolver) {
        m_resolver->cancel();
    }
    if (m_catalog) {
        m_catalog->cancel();
    }
}

void PerformanceDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* body = new QWidget(this);
    auto* column = new QVBoxLayout(body);
    column->setContentsMargins(Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceLg);
    column->setSpacing(Tokens::spaceMd);

    m_title = new QLabel(dialogTitle(m_env.target.name), body);
    Style::applyRole(m_title, Role::Title);
    m_title->setObjectName(QStringLiteral("zeroPerfTitle"));
    column->addWidget(m_title);

    m_scroll = new QScrollArea(body);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_content = new QWidget;
    m_scroll->setWidget(m_content);
    m_content->setAutoFillBackground(false);
    m_scroll->viewport()->setAutoFillBackground(false);
    auto* sections = new QVBoxLayout(m_content);
    sections->setContentsMargins(0, 0, 0, 0);
    sections->setSpacing(Tokens::spaceLg);

    // ---- MODS ----
    auto* mods = new QWidget(m_content);
    auto* modsColumn = new QVBoxLayout(mods);
    modsColumn->setContentsMargins(0, 0, 0, 0);
    modsColumn->setSpacing(Tokens::spaceXs);
    m_modsTitle = overline(modsTitle(m_loader, m_env.target.mcVersion), mods);
    m_modsTitle->setObjectName(QStringLiteral("zeroPerfModsTitle"));
    modsColumn->addWidget(m_modsTitle);

    m_modsLine = makeLabel(vanillaModsText(), Role::Body, "secondary", mods);
    m_modsLine->setWordWrap(true);
    m_modsLine->hide();
    modsColumn->addWidget(m_modsLine);

    m_skeleton = new QWidget(mods);
    auto* skeletonColumn = new QVBoxLayout(m_skeleton);
    skeletonColumn->setContentsMargins(0, 0, 0, 0);
    skeletonColumn->setSpacing(Tokens::spaceSm);
    for (int i = 0; i < kSkeletonBlocks; ++i) {
        skeletonColumn->addWidget(new DependencySkeleton(m_skeleton));
    }
    m_skeleton->hide();
    modsColumn->addWidget(m_skeleton);

    m_errorRow = new QWidget(mods);
    auto* errorLayout = new QHBoxLayout(m_errorRow);
    errorLayout->setContentsMargins(0, 0, 0, 0);
    errorLayout->setSpacing(Tokens::spaceSm);
    m_errorLabel = makeLabel(QString(), Role::Body, "secondary", m_errorRow);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setObjectName(QStringLiteral("zeroPerfError"));
    errorLayout->addWidget(m_errorLabel, 1);
    m_retry = new QPushButton(tr("Retry"), m_errorRow);
    Style::setProperty(m_retry, "zeroVariant", "ghost");
    m_retry->setAutoDefault(false);
    m_retry->setAccessibleName(tr("Retry loading the performance mods"));
    errorLayout->addWidget(m_retry, 0, Qt::AlignTop);
    m_errorRow->hide();
    modsColumn->addWidget(m_errorRow);

    m_rowsHost = new QWidget(mods);
    m_rowsLayout = new QVBoxLayout(m_rowsHost);
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(0);
    m_rowsHost->hide();
    modsColumn->addWidget(m_rowsHost);

    // what the ticked mods need
    m_depRow = new QWidget(mods);
    auto* depLayout = new QHBoxLayout(m_depRow);
    depLayout->setContentsMargins(0, Tokens::spaceXs, 0, 0);
    depLayout->setSpacing(Tokens::spaceSm);
    m_depSpinner = new ZeroSpinner(ZeroSpinner::Small, m_depRow);
    depLayout->addWidget(m_depSpinner, 0, Qt::AlignTop);
    m_depLabel = makeLabel(QString(), Role::Caption, "secondary", m_depRow);
    m_depLabel->setWordWrap(true);
    m_depLabel->setObjectName(QStringLiteral("zeroPerfDependencies"));
    depLayout->addWidget(m_depLabel, 1);
    m_depRetry = new QPushButton(tr("Retry"), m_depRow);
    Style::setProperty(m_depRetry, "zeroVariant", "ghost");
    m_depRetry->setAutoDefault(false);
    m_depRetry->setAccessibleName(tr("Retry the dependency check"));
    depLayout->addWidget(m_depRetry, 0, Qt::AlignTop);
    m_depRow->hide();
    modsColumn->addWidget(m_depRow);

    const NoteParts note = buildNoteRow(mods);
    m_noteRow = note.row;
    m_noteIcon = note.icon;
    m_note = note.text;
    m_note->setText(modpackNote());
    m_noteRow->hide();
    modsColumn->addWidget(m_noteRow);
    sections->addWidget(mods);

    // ---- MEMORY ----
    m_memorySection = new QWidget(m_content);
    auto* memoryColumn = new QVBoxLayout(m_memorySection);
    memoryColumn->setContentsMargins(0, 0, 0, 0);
    memoryColumn->setSpacing(Tokens::spaceXs);
    memoryColumn->addWidget(overline(tr("Memory"), m_memorySection));
    m_ramLabel = makeLabel(QString(), Role::Body, "secondary", m_memorySection);
    m_ramLabel->setObjectName(QStringLiteral("zeroPerfRam"));
    memoryColumn->addWidget(m_ramLabel);
    m_memoryRow = new QWidget(m_memorySection);
    auto* memoryRowLayout = new QHBoxLayout(m_memoryRow);
    memoryRowLayout->setContentsMargins(0, Tokens::spaceXs, 0, 0);
    memoryRowLayout->setSpacing(Tokens::spaceMd);
    m_memoryBox = new QCheckBox(m_memoryRow);
    m_memoryBox->setObjectName(QStringLiteral("zeroPerfMemoryBox"));
    memoryRowLayout->addWidget(m_memoryBox);
    memoryRowLayout->addStretch(1);
    m_memoryNow = makeLabel(QString(), Role::Caption, "secondary", m_memoryRow);
    m_memoryNow->setObjectName(QStringLiteral("zeroPerfMemoryNow"));
    memoryRowLayout->addWidget(m_memoryNow);
    memoryColumn->addWidget(m_memoryRow);
    m_memoryFine = makeLabel(QString(), Role::Body, nullptr, m_memorySection);
    m_memoryFine->setObjectName(QStringLiteral("zeroPerfMemoryFine"));
    memoryColumn->addWidget(m_memoryFine);
    m_memorySection->hide();
    sections->addWidget(m_memorySection);

    // ---- JAVA ----
    m_javaSection = new QWidget(m_content);
    auto* javaColumn = new QVBoxLayout(m_javaSection);
    javaColumn->setContentsMargins(0, 0, 0, 0);
    javaColumn->setSpacing(Tokens::spaceXs);
    javaColumn->addWidget(overline(tr("Java"), m_javaSection));
    auto* javaRow = new QHBoxLayout;
    javaRow->setContentsMargins(0, 0, 0, 0);
    javaRow->setSpacing(Tokens::spaceMd);
    m_javaLabel = makeLabel(QString(), Role::Body, nullptr, m_javaSection);
    m_javaLabel->setWordWrap(true);
    m_javaLabel->setObjectName(QStringLiteral("zeroPerfJava"));
    javaRow->addWidget(m_javaLabel, 1);
    m_javaFix = new QPushButton(tr("Fix"), m_javaSection);
    Style::setProperty(m_javaFix, "zeroVariant", "secondary");
    m_javaFix->setAutoDefault(false);
    m_javaFix->setAccessibleName(tr("Fix the Java in Java Doctor"));
    javaRow->addWidget(m_javaFix, 0, Qt::AlignTop);
    javaColumn->addLayout(javaRow);
    m_javaSection->hide();
    sections->addWidget(m_javaSection);

    // ---- JAVA ARGUMENTS ----
    m_jvmSection = new QWidget(m_content);
    auto* jvmColumn = new QVBoxLayout(m_jvmSection);
    jvmColumn->setContentsMargins(0, 0, 0, 0);
    jvmColumn->setSpacing(Tokens::spaceXs);
    jvmColumn->addWidget(overline(tr("Java arguments"), m_jvmSection));
    m_jvmRow = new QWidget(m_jvmSection);
    auto* jvmRowLayout = new QVBoxLayout(m_jvmRow);
    jvmRowLayout->setContentsMargins(0, Tokens::spaceXs, 0, 0);
    jvmRowLayout->setSpacing(Tokens::spaceXs);
    m_jvmBox = new QCheckBox(m_jvmRow);
    m_jvmBox->setObjectName(QStringLiteral("zeroPerfJvmBox"));
    jvmRowLayout->addWidget(m_jvmBox);
    m_jvmCurrent = makeLabel(QString(), Role::Caption, "secondary", m_jvmRow);
    m_jvmCurrent->setObjectName(QStringLiteral("zeroPerfJvmCurrent"));
    m_jvmCurrent->setContentsMargins(Tokens::spaceXl, 0, 0, 0);
    jvmRowLayout->addWidget(m_jvmCurrent);
    m_jvmDetailsButton = new QPushButton(tr("What they do"), m_jvmRow);
    m_jvmDetailsButton->setObjectName(QStringLiteral("zeroPerfJvmDetailsButton"));
    m_jvmDetailsButton->setCheckable(true);
    Style::setProperty(m_jvmDetailsButton, "zeroVariant", "ghost");
    m_jvmDetailsButton->setAutoDefault(false);
    m_jvmDetailsButton->setAccessibleName(tr("What the recommended Java arguments do"));
    auto* detailsButtonRow = new QHBoxLayout;
    detailsButtonRow->setContentsMargins(Tokens::spaceXl, 0, 0, 0);
    detailsButtonRow->addWidget(m_jvmDetailsButton);
    detailsButtonRow->addStretch(1);
    jvmRowLayout->addLayout(detailsButtonRow);
    m_jvmDetails = new QWidget(m_jvmRow);
    m_jvmDetailsLayout = new QVBoxLayout(m_jvmDetails);
    m_jvmDetailsLayout->setContentsMargins(Tokens::spaceXl, 0, 0, 0);
    m_jvmDetailsLayout->setSpacing(Tokens::spaceXs);
    m_jvmDetails->hide();
    jvmRowLayout->addWidget(m_jvmDetails);
    jvmColumn->addWidget(m_jvmRow);
    m_jvmFine = makeLabel(tr("Java arguments are already tuned."), Role::Body, nullptr, m_jvmSection);
    m_jvmFine->setObjectName(QStringLiteral("zeroPerfJvmFine"));
    jvmColumn->addWidget(m_jvmFine);
    m_jvmSection->hide();
    sections->addWidget(m_jvmSection);
    sections->addStretch(1);

    column->addWidget(m_scroll, 1);
    root->addWidget(body, 1);

    // ---- footer: summary, Cancel, Apply ----
    const FooterParts footer = buildFooter(this, tr("Apply"));
    m_cancel = footer.cancel;
    m_apply = footer.primary;
    auto* footerLayout = qobject_cast<QHBoxLayout*>(footer.bar->layout());
    delete footerLayout->takeAt(0);  // the stretch: the summary takes its place
    m_summary = makeLabel(QString(), Role::Body, "secondary", footer.bar);
    m_summary->setObjectName(QStringLiteral("zeroPerfSummary"));
    footerLayout->insertWidget(0, m_summary, 1);
    root->addWidget(footer.bar);

    setFixedWidth(kWidth);
}

// ---------------------------------------------------------------------------------------------------------------------
// Loading: installed mods, Modrinth, Java
// ---------------------------------------------------------------------------------------------------------------------

void PerformanceDialog::startInstalled()
{
    m_installedReady = false;
    if (m_loader == Loader::None || !m_env.loadInstalled) {
        onInstalled({});  // vanilla has no mods to read
        return;
    }
    m_env.loadInstalled(this, [this](const InstalledSnapshot& installed) { onInstalled(installed); });
}

void PerformanceDialog::onInstalled(const InstalledSnapshot& installed)
{
    m_installed = installed;
    m_installedReady = true;
    tryBuildRows();
    refreshMemory();
    refreshSummary();
    fitBody();
}

void PerformanceDialog::startCatalog()
{
    ++m_catalogToken;
    m_catalogReady = false;
    m_found.clear();
    clearRows();
    setModsState(ModsState::Loading);
    if (!m_catalog && m_env.makeCatalog) {
        m_catalog = m_env.makeCatalog(this);
        if (m_catalog) {
            connect(m_catalog, &PerformanceCatalog::loaded, this, &PerformanceDialog::onCatalogLoaded);
            connect(m_catalog, &PerformanceCatalog::failed, this, &PerformanceDialog::onCatalogFailed);
        }
    }
    if (!m_catalog) {
        setModsState(ModsState::Failed, tr("Performance mods cannot be looked up right now."));
        return;
    }
    PerformanceCatalog::Request request;
    request.slugs = slugsFor(m_loader);
    request.mcVersion = m_env.target.mcVersion;
    request.loaders = m_env.target.loaders;
    m_catalog->load(m_catalogToken, request);  // may answer at once (a fake does)
}

void PerformanceDialog::onCatalogLoaded(quint64 token, const FoundMap& found)
{
    if (token != m_catalogToken) {
        return;  // the answer of a request that was replaced
    }
    m_found = found;
    m_catalogReady = true;
    tryBuildRows();
}

void PerformanceDialog::onCatalogFailed(quint64 token, const QString& reason)
{
    if (token != m_catalogToken) {
        return;
    }
    setModsState(ModsState::Failed, reason);
    refreshMemory();
    refreshSummary();
}

void PerformanceDialog::startJava()
{
    m_javaChecked = false;
    if (!m_env.checkJava) {
        m_javaChecked = true;
        return;
    }
    m_env.checkJava(this, [this](const std::optional<QString>& problem) {
        m_javaChecked = true;
        m_javaLabel->setText(problem.value_or(QString()));
        m_javaSection->setVisible(problem.has_value());
        fitBody();
        refreshSummary();  // the Java arguments need to know the Java
    });
}

// ---------------------------------------------------------------------------------------------------------------------
// The mods section
// ---------------------------------------------------------------------------------------------------------------------

void PerformanceDialog::setModsState(ModsState state, const QString& error)
{
    m_modsState = state;
    m_error = error;
    m_modsLine->setVisible(state == ModsState::Vanilla);
    m_skeleton->setVisible(state == ModsState::Loading);
    m_errorRow->setVisible(state == ModsState::Failed);
    m_rowsHost->setVisible(state == ModsState::Ready && !m_rows.empty());
    m_noteRow->setVisible(state == ModsState::Ready && m_env.managedPack);
    if (state == ModsState::Failed) {
        m_errorLabel->setText(tr("Couldn't load performance mods from Modrinth. Check your connection and try again."));
        m_errorLabel->setToolTip(error);
    }
    if (state != ModsState::Ready) {
        m_depRow->hide();
    }
    refreshIcons();
    refreshSummary();
    fitBody();
}

void PerformanceDialog::tryBuildRows()
{
    if (m_modsState == ModsState::Loading && m_catalogReady && m_installedReady) {
        buildRows();
    }
}

void PerformanceDialog::clearRows()
{
    for (const auto& row : m_rows) {
        row->widget->hide();
        row->widget->deleteLater();
    }
    m_rows.clear();
    m_deps.clear();
    m_depState = DepState::Done;
    m_depTimer.stop();
    ++m_depToken;
    if (m_resolver) {
        m_resolver->cancel();
    }
}

void PerformanceDialog::buildRows()
{
    const QList<Row> rows = Perf::buildRows(m_loader, m_env.target.mcVersion, m_env.target.loaders, m_found, m_installed);
    for (const Row& row : rows) {
        auto ui = std::make_unique<RowUi>();
        ui->row = row;
        auto* widget = new RowWidget(m_rowsHost);
        ui->widget = widget;
        widget->setFixedHeight(kRowHeight);
        widget->setObjectName(QStringLiteral("zeroPerfRow"));
        auto* layout = new QHBoxLayout(widget);
        layout->setContentsMargins(0, Tokens::spaceXs, 0, Tokens::spaceXs);
        layout->setSpacing(Tokens::spaceMd);

        ui->check = new QCheckBox(widget);
        widget->box = ui->check;
        ui->check->setAccessibleName(tr("Add %1").arg(row.name));
        ui->check->setEnabled(row.canTick());
        ui->check->setChecked(row.tickedByDefault());
        layout->addWidget(ui->check);

        const auto found = m_found.constFind(row.slug);
        const QString iconUrl = found != m_found.constEnd() && found->pack ? found->pack->logoUrl : QString();
        ui->icon = new PackIconWidget(iconUrl, m_env.images, widget, QStringLiteral("gauge"), kRowIcon);
        layout->addWidget(ui->icon);

        auto* texts = new QVBoxLayout;
        texts->setContentsMargins(0, 0, 0, 0);
        texts->setSpacing(0);
        ui->name = makeLabel(row.name, Role::Label, nullptr, widget);
        ui->name->setObjectName(QStringLiteral("zeroPerfRowName"));
        texts->addWidget(ui->name);
        ui->benefit = makeLabel(row.benefit, Role::Caption, "secondary", widget);
        ui->benefit->setObjectName(QStringLiteral("zeroPerfRowBenefit"));
        ui->benefit->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        texts->addWidget(ui->benefit);
        layout->addLayout(texts, 1);

        const QString pillText = rowStateText(row, m_env.target.mcVersion);
        if (!pillText.isEmpty()) {
            ui->pill = new PillTag(pillText, toneOf(row.state), widget);
            layout->addWidget(ui->pill, 0, Qt::AlignVCenter);
        }
        widget->setCursor(row.canTick() ? Qt::PointingHandCursor : Qt::ArrowCursor);
        if (!row.canTick()) {
            ui->name->setProperty("zeroTone", "secondary");
        }
        connect(ui->check, &QCheckBox::toggled, this, &PerformanceDialog::onTicksChanged);
        m_rowsLayout->addWidget(widget);
        m_rows.push_back(std::move(ui));
    }
    setModsState(ModsState::Ready);
    afterTicksChanged(0);
}

PerformanceDialog::RowUi* PerformanceDialog::rowUi(const QString& slug) const
{
    for (const auto& row : m_rows) {
        if (row->row.slug == slug) {
            return row.get();
        }
    }
    return nullptr;
}

int PerformanceDialog::tickedCount() const
{
    int count = 0;
    for (const auto& row : m_rows) {
        if (row->check->isEnabled() && row->check->isChecked()) {
            ++count;
        }
    }
    return count;
}

void PerformanceDialog::onTicksChanged()
{
    afterTicksChanged(m_env.dependencyDelayMs);
}

void PerformanceDialog::afterTicksChanged(int delayMs)
{
    refreshMemory();
    m_deps.clear();
    if (tickedCount() == 0) {
        m_depTimer.stop();
        ++m_depToken;
        if (m_resolver) {
            m_resolver->cancel();
        }
        m_depState = DepState::Done;
        m_depRow->hide();
        refreshSummary();
        fitBody();
        return;
    }
    scheduleDependencyCheck(delayMs);
}

// ---------------------------------------------------------------------------------------------------------------------
// Dependencies
// ---------------------------------------------------------------------------------------------------------------------

void PerformanceDialog::scheduleDependencyCheck(int delayMs)
{
    ++m_depToken;  // an answer for the previous run is stale from now on
    if (m_resolver) {
        m_resolver->cancel();
    }
    m_depState = DepState::Checking;
    m_deps.clear();
    m_depRow->show();
    m_depSpinner->show();
    m_depRetry->hide();
    m_depLabel->setText(tr("Checking what these need…"));
    m_depTimer.start(std::max(0, delayMs));
    refreshSummary();
    fitBody();
}

void PerformanceDialog::runDependencyCheck()
{
    QList<DependencyResolver::Selected> selected;
    for (const auto& row : m_rows) {
        if (row->check->isEnabled() && row->check->isChecked() && row->row.pack) {
            selected.append({ row->row.pack, row->row.version });
        }
    }
    const bool enabled = !m_env.dependencyCheckEnabled || m_env.dependencyCheckEnabled();
    if (selected.isEmpty() || !m_resolver || !enabled) {
        onResolved(m_depToken, {});  // nothing to ask (or the check is turned off): no extra mods
        return;
    }
    DependencyResolver::Request request;
    request.instanceId = m_env.target.id;
    request.also = selected;
    m_resolver->resolve(m_depToken, request);
}

void PerformanceDialog::onResolved(quint64 token, const QList<PlannedMod>& dependencies)
{
    if (token != m_depToken || m_depState != DepState::Checking) {
        return;
    }
    QSet<QString> inList;
    for (const auto& row : m_rows) {
        if (row->check->isEnabled() && row->check->isChecked() && row->row.pack) {
            inList.insert(projectKeyOf(row->row.pack));
        }
    }
    m_deps = mergeDependencies(dependencies, inList).toAdd;
    m_depState = DepState::Done;
    m_depSpinner->hide();
    m_depRetry->hide();
    if (m_deps.isEmpty()) {
        m_depRow->hide();
    } else {
        QStringList names;
        for (const PlannedMod& dep : std::as_const(m_deps)) {
            names << dep.name();
        }
        m_depLabel->setText(tr("Also adds %1, which they need").arg(names.join(QStringLiteral(", "))));
        m_depRow->show();
    }
    refreshMemory();
    refreshSummary();
    fitBody();
}

void PerformanceDialog::onResolveFailed(quint64 token, const QString& reason)
{
    if (token != m_depToken || m_depState != DepState::Checking) {
        return;
    }
    m_depState = DepState::Failed;
    m_deps.clear();
    m_depSpinner->hide();
    m_depRetry->show();
    m_depLabel->setText(tr("Couldn't check what these need"));
    m_depLabel->setToolTip(reason);
    m_depRow->show();
    refreshSummary();
    fitBody();
}

// ---------------------------------------------------------------------------------------------------------------------
// Memory, summary, Apply
// ---------------------------------------------------------------------------------------------------------------------

void PerformanceDialog::refreshMemory()
{
    const bool wasOffered = m_memoryRow->isVisibleTo(m_memorySection) && m_memorySection->isVisibleTo(this);
    if (!m_installedReady || m_modsState == ModsState::Loading || m_env.ramMb <= 0) {
        m_memorySection->hide();
        m_advice = {};
        return;
    }
    const int mods = m_installed.count + tickedCount();  // installed + ticked (the dependencies are not counted)
    const int current = m_env.maxMemoryMb ? m_env.maxMemoryMb() : 0;
    m_advice = adviseMemory(m_env.ramMb, mods, m_loader != Loader::None, current);
    m_ramLabel->setText(ramText(m_env.ramMb));
    const bool offered = m_advice.needsChange();
    m_memoryRow->setVisible(offered);
    m_memoryFine->setVisible(!offered);
    if (offered) {
        m_memoryBox->setText(memoryGiveText(m_advice.recommendedMaxMb));
        m_memoryBox->setAccessibleName(memoryGiveText(m_advice.recommendedMaxMb));
        m_memoryNow->setText(Perf::memoryNowText(m_advice.currentMaxMb));
        if (!wasOffered && !m_memoryTouched) {
            const QSignalBlocker blocker(m_memoryBox);
            m_memoryBox->setChecked(true);
        }
    } else {
        m_memoryFine->setText(memoryFineText(m_advice.currentMaxMb));
    }
    m_memorySection->show();
    fitBody();
}

Summary PerformanceDialog::currentSummary() const
{
    Summary summary;
    summary.mods = tickedCount();
    summary.dependencies = int(m_deps.size());
    if (const auto memory = memoryToWrite()) {
        summary.memory = true;
        summary.memoryFromMb = memory->currentMaxMb;
        summary.memoryToMb = memory->recommendedMaxMb;
    }
    summary.javaArgs = jvmArgsToWrite().has_value();
    return summary;
}

std::optional<MemoryAdvice> PerformanceDialog::memoryToWrite() const
{
    if (m_memorySection->isVisibleTo(this) && m_advice.needsChange() && m_memoryBox->isChecked()) {
        return m_advice;
    }
    return std::nullopt;
}

std::optional<JvmArgsAdvice> PerformanceDialog::jvmArgsToWrite() const
{
    if (m_jvmSection->isVisibleTo(this) && m_jvm.offered() && m_jvmBox->isChecked()) {
        return m_jvm;
    }
    return std::nullopt;
}

void PerformanceDialog::refreshJvmArgs()
{
    if (!m_jvmSection) {
        return;
    }
    JvmArgsAdvice advice;  // NotSupported until Java Doctor answered
    if (m_javaChecked) {
        const int major = m_env.javaMajor ? m_env.javaMajor() : 0;
        const int cores = m_env.cpuCores ? m_env.cpuCores() : QThread::idealThreadCount();
        const QString current = m_env.currentJvmArgs ? m_env.currentJvmArgs() : QString();
        const auto memory = memoryToWrite();
        const int heapMb = memory ? memory->recommendedMaxMb : (m_env.maxMemoryMb ? m_env.maxMemoryMb() : 0);
        advice = adviseJvmArgs(major, heapMb, cores, current);
    }
    const bool stateChanged = advice.state != m_jvm.state;
    const bool flagsChanged = advice.args() != m_jvm.args();
    const bool currentChanged = advice.current != m_jvm.current;
    m_jvm = advice;

    const bool offered = advice.offered();
    const bool shown = advice.state != JvmArgsState::NotSupported;
    m_jvmRow->setVisible(offered);
    m_jvmFine->setVisible(advice.state == JvmArgsState::AlreadyRecommended);
    if (offered) {
        const QString text = advice.state == JvmArgsState::HasCustomArgs ? tr("Replace your Java arguments with the recommended ones")
                                                                          : tr("Use recommended Java arguments");
        m_jvmBox->setText(text);
        m_jvmBox->setAccessibleName(text);
        if (stateChanged) {
            m_jvmTouched = false;
        }
        if (stateChanged || !m_jvmTouched) {
            const QSignalBlocker blocker(m_jvmBox);
            m_jvmBox->setChecked(advice.state == JvmArgsState::NoCustomArgs);
        }
        const bool custom = advice.state == JvmArgsState::HasCustomArgs;
        m_jvmCurrent->setVisible(custom);
        if (custom && (currentChanged || stateChanged)) {
            const QString line = tr("Currently: %1");
            const QFontMetrics metrics(m_jvmCurrent->font());
            const int room = kWidth - 3 * Tokens::spaceXl;
            const QString elided = metrics.elidedText(advice.current, Qt::ElideRight, room - metrics.horizontalAdvance(line.arg(QString())));
            m_jvmCurrent->setText(line.arg(elided));
            m_jvmCurrent->setToolTip(advice.current);
        }
        if (flagsChanged || stateChanged) {
            clearLayout(m_jvmDetailsLayout);
            for (const JvmFlag& flag : advice.flags) {
                auto* line = makeLabel(QStringLiteral("%1 \u2014 %2").arg(flag.flag, flag.why), Role::Caption, "secondary", m_jvmDetails);
                line->setWordWrap(true);
                line->setTextInteractionFlags(Qt::TextSelectableByMouse);
                m_jvmDetailsLayout->addWidget(line);
            }
        }
    }
    const bool changed = shown != m_jvmSection->isVisibleTo(this) || stateChanged || flagsChanged;
    m_jvmSection->setVisible(shown);
    if (changed) {
        fitBody();
    }
}

bool PerformanceDialog::instanceRunning() const
{
    return m_env.isRunning ? m_env.isRunning() : m_env.target.running;
}

void PerformanceDialog::refreshSummary()
{
    if (!m_summary) {
        return;
    }
    refreshJvmArgs();
    const bool checking = m_modsState == ModsState::Loading || m_depState == DepState::Checking;
    m_summary->setText(checking ? cartCheckingText() : Perf::summaryText(currentSummary()));
    m_apply->setEnabled(canApply());
}

bool PerformanceDialog::canApply() const
{
    if (m_modsState == ModsState::Loading || m_depState == DepState::Checking) {
        return false;
    }
    if (tickedCount() > 0 && m_depState != DepState::Done) {
        return false;  // the dependency check failed: retry it or untick
    }
    return !currentSummary().isEmpty();
}

CartInstallPlan PerformanceDialog::plan() const
{
    QList<CartPlanRow> rows;
    for (const auto& row : m_rows) {
        if (row->check->isEnabled() && row->check->isChecked() && row->row.pack) {
            CartPlanRow planRow;
            planRow.kind = Kind::Mod;
            planRow.cartKey = row->row.slug;
            planRow.pack = row->row.pack;
            planRow.version = row->row.version;
            rows.append(planRow);
        }
    }
    return buildCartPlan(rows, m_deps, false);
}

void PerformanceDialog::accept()
{
    if (!canApply()) {
        return;  // checking, nothing to change: Enter does nothing
    }
    if (instanceRunning()) {
        m_outcome.refusedRunning = true;  // the caller says "Close <instance> first"
        QDialog::reject();
        return;
    }
    m_depTimer.stop();
    if (m_resolver) {
        m_resolver->cancel();
    }
    const CartInstallPlan toInstall = plan();
    if (!toInstall.isEmpty()) {
        if (!m_env.install) {
            return;
        }
        // Background job: the call returns at once. Memory and Java arguments do not depend on the mods.
        m_env.install(toInstall);
        m_outcome.modsQueued = toInstall.count();
    }
    if (const auto memory = memoryToWrite(); memory && m_env.writeMemory) {
        m_env.writeMemory(memory->recommendedMaxMb, memory->recommendedMinMb);
        m_outcome.memoryWritten = true;
    }
    if (const auto jvm = jvmArgsToWrite(); jvm && m_env.writeJvmArgs) {
        m_outcome.previousJvmArgs = m_env.writeJvmArgs(jvm->argsString());
        m_outcome.javaArgsWritten = true;
    }
    m_outcome.applied = m_outcome.modsQueued > 0 || m_outcome.memoryWritten || m_outcome.javaArgsWritten;
    QDialog::accept();
}

// ---------------------------------------------------------------------------------------------------------------------
// Look
// ---------------------------------------------------------------------------------------------------------------------

void PerformanceDialog::refreshIcons()
{
    if (!m_noteIcon) {
        return;
    }
    const auto& t = Theme::current();
    m_noteIcon->setPixmap(Icons::pixmap(QStringLiteral("info"), t.info, 16, devicePixelRatioF()));
}

void PerformanceDialog::fitBody()
{
    if (!m_content || !m_content->layout()) {
        return;
    }
    QLayout* layout = m_content->layout();
    layout->activate();
    const int width = kWidth - 2 * Tokens::spaceXl;
    const int wanted = layout->hasHeightForWidth() ? layout->totalHeightForWidth(width) : layout->sizeHint().height();
    const int height = std::min(std::max(wanted, kRowHeight), kMaxBodyHeight);
    if (m_scroll->height() != height || m_scroll->minimumHeight() != height) {
        m_scroll->setFixedHeight(height);
        if (isVisible()) {
            adjustSize();
        }
    }
}

void PerformanceDialog::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    Paint::paintBackground(p, rect(), Theme::current().bg);
}

void PerformanceDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    fitBody();
    adjustSize();
}

// ---------------------------------------------------------------------------------------------------------------------
// Parts for tests and dev aids
// ---------------------------------------------------------------------------------------------------------------------

bool PerformanceDialog::isSettled() const
{
    if (m_modsState == ModsState::Loading || !m_installedReady || !m_javaChecked || dependencyCheckPending()) {
        return false;
    }
    for (const auto& row : m_rows) {
        if (!row->icon->settled()) {
            return false;
        }
    }
    return true;
}

void PerformanceDialog::callWhenSettled(const std::function<void()>& done, int timeoutMs)
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

QString PerformanceDialog::titleText() const
{
    return m_title->text();
}

QString PerformanceDialog::modsTitleText() const
{
    return m_modsTitle->text();
}

QString PerformanceDialog::modsLineText() const
{
    return m_modsLine->isVisibleTo(this) ? m_modsLine->text() : QString();
}

QString PerformanceDialog::errorText() const
{
    return m_modsState == ModsState::Failed ? m_errorLabel->text() : QString();
}

QString PerformanceDialog::noteText() const
{
    return m_noteRow->isVisibleTo(this) ? m_note->text() : QString();
}

QStringList PerformanceDialog::rowSlugs() const
{
    QStringList out;
    for (const auto& row : m_rows) {
        out << row->row.slug;
    }
    return out;
}

RowState PerformanceDialog::rowState(const QString& slug) const
{
    const RowUi* row = rowUi(slug);
    return row ? row->row.state : RowState::Unavailable;
}

QString PerformanceDialog::rowName(const QString& slug) const
{
    const RowUi* row = rowUi(slug);
    return row ? row->name->text() : QString();
}

QString PerformanceDialog::rowPillText(const QString& slug) const
{
    const RowUi* row = rowUi(slug);
    return row && row->pill ? row->pill->text() : QString();
}

QCheckBox* PerformanceDialog::rowCheck(const QString& slug) const
{
    const RowUi* row = rowUi(slug);
    return row ? row->check : nullptr;
}

int PerformanceDialog::skeletonRowCount() const
{
    return m_skeleton->isVisibleTo(this) ? kSkeletonBlocks * 2 : 0;
}

bool PerformanceDialog::memorySectionVisible() const
{
    return m_memorySection->isVisibleTo(this);
}

QString PerformanceDialog::ramLineText() const
{
    return m_ramLabel->text();
}

QString PerformanceDialog::memoryText() const
{
    if (m_memoryRow->isVisibleTo(m_memorySection)) {
        return m_memoryBox->text();
    }
    return m_memoryFine->isVisibleTo(m_memorySection) ? m_memoryFine->text() : QString();
}

QString PerformanceDialog::memoryNowText() const
{
    return m_memoryRow->isVisibleTo(m_memorySection) ? m_memoryNow->text() : QString();
}

bool PerformanceDialog::javaSectionVisible() const
{
    return m_javaSection->isVisibleTo(this);
}

QString PerformanceDialog::javaText() const
{
    return m_javaLabel->text();
}

bool PerformanceDialog::jvmArgsSectionVisible() const
{
    return m_jvmSection->isVisibleTo(this);
}

QString PerformanceDialog::jvmArgsBoxText() const
{
    return m_jvmRow->isVisibleTo(m_jvmSection) ? m_jvmBox->text() : QString();
}

QString PerformanceDialog::jvmArgsCurrentText() const
{
    return m_jvmCurrent->isVisibleTo(m_jvmRow) ? m_jvmCurrent->text() : QString();
}

QString PerformanceDialog::jvmArgsCurrentToolTip() const
{
    return m_jvmCurrent->toolTip();
}

QString PerformanceDialog::jvmArgsFineText() const
{
    return m_jvmFine->isVisibleTo(m_jvmSection) ? m_jvmFine->text() : QString();
}

bool PerformanceDialog::jvmArgsDetailsVisible() const
{
    return m_jvmDetails->isVisibleTo(m_jvmRow);
}

QStringList PerformanceDialog::jvmArgsExplanations() const
{
    QStringList lines;
    for (int i = 0; i < m_jvmDetailsLayout->count(); ++i) {
        if (auto* label = qobject_cast<QLabel*>(m_jvmDetailsLayout->itemAt(i)->widget()); label && !label->isHidden()) {
            lines << label->text();
        }
    }
    return lines;
}

QString PerformanceDialog::summaryText() const
{
    return m_summary->text();
}

QStringList PerformanceDialog::tickedSlugs() const
{
    QStringList out;
    for (const auto& row : m_rows) {
        if (row->check->isEnabled() && row->check->isChecked()) {
            out << row->row.slug;
        }
    }
    return out;
}

QStringList PerformanceDialog::dependencyNames() const
{
    QStringList out;
    for (const PlannedMod& dep : m_deps) {
        out << dep.name();
    }
    return out;
}

}  // namespace Zero::Perf
