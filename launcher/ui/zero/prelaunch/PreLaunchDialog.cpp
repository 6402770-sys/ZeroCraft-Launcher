// SPDX-License-Identifier: GPL-3.0-only
#include "PreLaunchDialog.h"

#include <QCheckBox>
#include <QDir>
#include <QFuture>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPointer>
#include <QPromise>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <mutex>
#include <optional>

#include "Application.h"
#include "minecraft/MinecraftInstance.h"
#include "settings/SettingsObject.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroSpinner.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/content/InstallDialogParts.h"
#include "ui/zero/instance/PackRollbackPrompt.h"

namespace Zero::PreLaunchUi {

using namespace Zero::Content::Parts;
using PreLaunch::Category;
using PreLaunch::Decision;
using PreLaunch::Item;
using PreLaunch::Severity;

namespace {

constexpr int kRowIcon = 20;
constexpr int kMinBodyHeight = 48;

QLabel* makeLabel(const QString& text, Role role, const char* tone, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    Style::applyRole(label, role);
    if (tone) {
        Style::setProperty(label, "zeroTone", tone);
    }
    return label;
}

/// The Lucide icon of a row: an alert for malware and system traces (the bundle has no shield-alert), the item's own icon for the rest.
QString iconFor(const Item& item)
{
    switch (item.category) {
        case Category::Malware:
        case Category::System:
            return QStringLiteral("triangle-alert");
        case Category::Mods:
        case Category::Packs:
            break;
    }
    static const QStringList names = Icons::availableNames();
    if (!item.iconName.isEmpty() && names.contains(item.iconName)) {
        return item.iconName;
    }
    return item.category == Category::Packs ? QStringLiteral("images") : QStringLiteral("puzzle");
}

/// One full scan per instance at a time: a scan whose dialog was closed keeps running (it cannot be cancelled) and the next one waits for it
/// (then answers from the cache it wrote).
std::shared_ptr<QMutex> scanLockFor(const QString& key)
{
    static QMutex guard;
    static QHash<QString, std::shared_ptr<QMutex>> locks;
    QMutexLocker locker(&guard);
    auto& lock = locks[key];
    if (!lock) {
        lock = std::make_shared<QMutex>();
    }
    return lock;
}

ScanFunction scanOf(const PreLaunchEnvironment& env)
{
    if (env.scan) {
        return env.scan;
    }
    return [](const PreLaunch::Env& e, const PreLaunch::RunOptions& o) { return PreLaunch::runChecks(e, o); };
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------------------------------------------------

PreLaunchDialog::PreLaunchDialog(PreLaunchEnvironment env, QWidget* parent) : QDialog(parent), m_env(std::move(env))
{
    setObjectName(QStringLiteral("ZeroPreLaunchDialog"));
    setWindowTitle(tr("Before you play"));
    setModal(true);
    buildUi();
    connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_play, &QPushButton::clicked, this, &PreLaunchDialog::onPlay);
    connect(m_optOut, &QCheckBox::toggled, this, [this](bool on) {
        if (m_session) {
            m_session->setOptOut(on);
        }
    });
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        if (m_state == State::Results) {
            rebuildRows();
        }
        update();
    });
    setTabOrder(m_optOut, m_cancel);
    setTabOrder(m_cancel, m_play);
    setState(State::Empty);
}

PreLaunchDialog::~PreLaunchDialog()
{
    m_decided = true;  // a scan that is still running answers into nothing: the watcher goes with the dialog and the worker only owns its copies
}

void PreLaunchDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* body = new QWidget(this);
    auto* column = new QVBoxLayout(body);
    column->setContentsMargins(Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceLg);
    column->setSpacing(Tokens::spaceMd);

    m_title = new QLabel(tr("Before you play · %1").arg(m_env.instanceName), body);
    Style::applyRole(m_title, Role::Title);
    m_title->setObjectName(QStringLiteral("zeroPreLaunchTitle"));
    column->addWidget(m_title);

    m_summary = makeLabel(QString(), Role::Body, "secondary", body);
    m_summary->setObjectName(QStringLiteral("zeroPreLaunchSummary"));
    m_summary->setWordWrap(true);
    column->addWidget(m_summary);

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

    // ---- checking ----
    m_checkingBox = new QWidget(m_content);
    m_checkingBox->setObjectName(QStringLiteral("zeroPreLaunchChecking"));
    auto* checkingColumn = new QVBoxLayout(m_checkingBox);
    checkingColumn->setContentsMargins(0, 0, 0, 0);
    checkingColumn->setSpacing(Tokens::spaceXs);
    auto* checkingRow = new QHBoxLayout;
    checkingRow->setContentsMargins(0, 0, 0, 0);
    checkingRow->setSpacing(Tokens::spaceMd);
    m_spinner = new ZeroSpinner(ZeroSpinner::Small, m_checkingBox);
    checkingRow->addWidget(m_spinner, 0, Qt::AlignVCenter);
    m_checkingLabel = makeLabel(QString(), Role::Body, nullptr, m_checkingBox);
    m_checkingLabel->setWordWrap(true);
    m_checkingLabel->setObjectName(QStringLiteral("zeroPreLaunchCheckingText"));
    checkingRow->addWidget(m_checkingLabel, 1);
    checkingColumn->addLayout(checkingRow);
    m_progressLabel = makeLabel(QString(), Role::Caption, "secondary", m_checkingBox);
    m_progressLabel->setObjectName(QStringLiteral("zeroPreLaunchProgress"));
    m_progressLabel->setContentsMargins(Tokens::spaceLg + Tokens::spaceMd, 0, 0, 0);  // under the text, not under the spinner
    checkingColumn->addWidget(m_progressLabel);
    m_checkingBox->hide();
    sections->addWidget(m_checkingBox);

    // ---- results ----
    m_resultsHost = new QWidget(m_content);
    m_resultsLayout = new QVBoxLayout(m_resultsHost);
    m_resultsLayout->setContentsMargins(0, 0, 0, 0);
    m_resultsLayout->setSpacing(Tokens::spaceLg);
    m_resultsHost->hide();
    sections->addWidget(m_resultsHost);
    sections->addStretch(1);

    column->addWidget(m_scroll, 1);
    root->addWidget(body, 1);

    // ---- footer: opt-out on the left, Cancel, Play ----
    const FooterParts footer = buildFooter(this, tr("Play"));
    m_cancel = footer.cancel;
    m_play = footer.primary;
    m_play->setObjectName(QStringLiteral("zeroPreLaunchPlay"));
    m_cancel->setObjectName(QStringLiteral("zeroPreLaunchCancel"));
    m_optOut = new QCheckBox(tr("Don't check this instance again"), footer.bar);
    m_optOut->setObjectName(QStringLiteral("zeroPreLaunchOptOut"));
    m_optOut->setToolTip(tr("Skips the mod and pack checks for this instance. The malware check always runs."));
    auto* footerLayout = qobject_cast<QHBoxLayout*>(footer.bar->layout());
    footerLayout->insertWidget(0, m_optOut);
    root->addWidget(footer.bar);

    setFixedWidth(kWidth);
}

// ---------------------------------------------------------------------------------------------------------------------
// States
// ---------------------------------------------------------------------------------------------------------------------

void PreLaunchDialog::setState(State state)
{
    m_state = state;
    m_checkingBox->setVisible(state == State::Checking);
    m_resultsHost->setVisible(state == State::Results);
    m_optOut->setVisible(state == State::Results);
    refreshChrome();
    fitBody();
}

void PreLaunchDialog::refreshChrome()
{
    const bool results = m_state == State::Results && m_session;
    m_summary->setVisible(results);
    m_summary->setText(results ? m_session->summary() : QString());
    if (results) {
        const bool primary = m_session->playIsPrimary();
        m_play->setText(m_session->playLabel());
        Style::setProperty(m_play, "zeroVariant", primary ? "primary" : "secondary");
        m_play->setAutoDefault(primary);
        m_play->setDefault(primary);
        m_cancel->setAutoDefault(!primary);
        m_cancel->setDefault(!primary);
        m_play->setEnabled(true);
    } else {
        m_play->setText(tr("Play"));
        Style::setProperty(m_play, "zeroVariant", "primary");
        m_play->setAutoDefault(true);
        m_play->setDefault(true);
        m_cancel->setAutoDefault(false);
        m_cancel->setDefault(false);
        m_play->setEnabled(false);
    }
}

void PreLaunchDialog::showResults(const QList<Item>& items)
{
    m_session = std::make_unique<PreLaunch::Session>(m_env.env, m_env.hooks, items, m_env.rerun);
    if (m_optOut->isChecked()) {
        m_session->setOptOut(true);
    }
    setState(State::Results);
    rebuildRows();
}

void PreLaunchDialog::startChecking(int modCount)
{
    if (m_watcher) {
        return;  // already scanning
    }
    if (modCount < 0) {
        modCount = int(QDir(m_env.env.modsDir()).entryList({ QStringLiteral("*.jar") }, QDir::Files).size());
    }
    m_modCount = modCount;
    m_progressTotal = 0;
    m_checkingLabel->setText(modCount <= 0 ? tr("Checking this instance…")
                             : modCount == 1 ? tr("Checking 1 mod for known malware…")
                                             : tr("Checking %1 mods for known malware…").arg(modCount));
    m_progressLabel->setText(QString());
    setState(State::Checking);

    m_watcher = new QFutureWatcher<PreLaunch::CheckResult>(this);
    connect(m_watcher, &QFutureWatcherBase::progressRangeChanged, this, [this](int, int maximum) {
        m_progressTotal = maximum;
        if (maximum > 0 && maximum != m_modCount) {
            m_modCount = maximum;
            m_checkingLabel->setText(maximum == 1 ? tr("Checking 1 mod for known malware…")
                                                  : tr("Checking %1 mods for known malware…").arg(maximum));
        }
    });
    connect(m_watcher, &QFutureWatcherBase::progressValueChanged, this, [this](int value) {
        if (m_progressTotal > 0) {
            m_progressLabel->setText(tr("%1 of %2").arg(value).arg(m_progressTotal));
        }
    });
    connect(m_watcher, &QFutureWatcherBase::finished, this, &PreLaunchDialog::onScanFinished);

    // The worker owns copies of everything it touches (the environment, the scan function, its promise): the dialog may be gone when it ends.
    const ScanFunction scan = scanOf(m_env);
    const QString key = m_env.env.instanceRoot;
    QFuture<PreLaunch::CheckResult> future =
        QtConcurrent::run([scan, env = m_env.env, key](QPromise<PreLaunch::CheckResult>& promise) {
            const std::shared_ptr<QMutex> lock = scanLockFor(key);
            QMutexLocker locker(lock.get());
            PreLaunch::RunOptions options;
            options.progress = [&promise](int done, int total) {
                promise.setProgressRange(0, total);
                promise.setProgressValue(done);
            };
            promise.addResult(scan(env, options));
        });
    m_watcher->setFuture(future);
}

void PreLaunchDialog::onScanFinished()
{
    if (m_decided || !m_watcher) {
        return;  // closed meanwhile: the answer is ignored
    }
    PreLaunch::CheckResult result;
    if (m_watcher->future().resultCount() > 0) {
        result = m_watcher->future().result();
    }
    m_watcher->deleteLater();
    m_watcher = nullptr;
    if (result.items.isEmpty()) {
        finish(Decision::Play);  // nothing to show: the launch goes on
        return;
    }
    showResults(result.items);
}

void PreLaunchDialog::finish(Decision decision)
{
    m_decision = decision;
    m_decided = true;
    if (decision == Decision::Play) {
        accept();
    } else {
        reject();
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Rows
// ---------------------------------------------------------------------------------------------------------------------

void PreLaunchDialog::rebuildRows()
{
    if (!m_session) {
        return;
    }
    const int scrollValue = m_scroll->verticalScrollBar()->value();
    clearLayout(m_resultsLayout);
    m_rows.clear();
    m_sectionTitles.clear();

    struct Group {
        Severity severity;
        QString title;
    };
    const Group groups[] = { { Severity::Block, tr("Must fix") }, { Severity::Warn, tr("Worth a look") } };
    for (const Group& group : groups) {
        QList<const Item*> items;
        for (const Item& item : m_session->items()) {
            if (item.severity == group.severity) {
                items.append(&item);
            }
        }
        if (items.isEmpty()) {
            continue;
        }
        m_sectionTitles << group.title;
        auto* section = new QWidget(m_resultsHost);
        section->setObjectName(QStringLiteral("zeroPreLaunchSection"));
        auto* column = new QVBoxLayout(section);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(Tokens::spaceMd);
        column->addWidget(overline(group.title, section));
        for (const Item* item : std::as_const(items)) {
            buildRow(*item, section, column, group.title);
        }
        m_resultsLayout->addWidget(section);
    }
    refreshChrome();
    fitBody();
    // New widgets are shown on the next pass of the event loop (until then a layout skips them): measure again then.
    QTimer::singleShot(0, this, [this, scrollValue]() {
        fitBody();
        m_scroll->verticalScrollBar()->setValue(scrollValue);
    });
}

void PreLaunchDialog::buildRow(const Item& item, QWidget* parent, QVBoxLayout* into, const QString& section)
{
    const auto& theme = Theme::current();
    const QColor tone = item.fixed ? theme.textSecondary : (item.severity == Severity::Block ? theme.danger : theme.warning);

    auto* row = new QWidget(parent);
    row->setObjectName(QStringLiteral("zeroPreLaunchRow"));
    row->setProperty("zeroItemId", item.id);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Tokens::spaceMd);

    RowUi ui;
    ui.id = item.id;
    ui.section = section;
    ui.title = item.title;
    ui.detail = item.detail;
    ui.iconName = iconFor(item);
    ui.fixed = item.fixed;
    ui.fixedText = item.fixed ? (item.fixedText.isEmpty() ? tr("Fixed") : item.fixedText) : QString();
    ui.error = item.fixed ? QString() : item.error;
    ui.widget = row;

    auto* icon = new QLabel(row);
    icon->setFixedSize(kRowIcon, kRowIcon);
    icon->setPixmap(Icons::pixmap(ui.iconName, tone, kRowIcon, devicePixelRatioF()));
    layout->addWidget(icon, 0, Qt::AlignTop);

    auto* column = new QVBoxLayout;
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(Tokens::spaceXs);

    auto* title = makeLabel(item.title, Role::Label, item.fixed ? "secondary" : nullptr, row);
    title->setObjectName(QStringLiteral("zeroPreLaunchRowTitle"));
    title->setWordWrap(true);
    column->addWidget(title);

    if (!item.detail.isEmpty()) {
        auto* detail = makeLabel(item.detail, Role::Caption, "secondary", row);
        detail->setObjectName(QStringLiteral("zeroPreLaunchRowDetail"));
        detail->setWordWrap(true);
        detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
        column->addWidget(detail);
    }
    if (!ui.error.isEmpty()) {
        auto* error = makeLabel(ui.error, Role::Caption, "danger", row);
        error->setObjectName(QStringLiteral("zeroPreLaunchRowError"));
        error->setWordWrap(true);
        error->setTextInteractionFlags(Qt::TextSelectableByMouse);
        column->addWidget(error);
    }

    auto* actions = new QHBoxLayout;
    actions->setContentsMargins(0, Tokens::spaceXs, 0, 0);
    actions->setSpacing(Tokens::spaceSm);
    actions->addStretch(1);
    if (item.fixed) {
        auto* check = new QLabel(row);
        check->setFixedSize(16, 16);
        check->setPixmap(Icons::pixmap(QStringLiteral("check"), theme.success, 16, devicePixelRatioF()));
        actions->addWidget(check);
        auto* text = makeLabel(ui.fixedText, Role::Caption, "success", row);
        text->setObjectName(QStringLiteral("zeroPreLaunchRowFixed"));
        actions->addWidget(text);
    } else {
        for (int i = 0; i < item.fixes.size(); ++i) {
            const PreLaunch::Fix& fix = item.fixes.at(i);
            auto* button = new QPushButton(fix.label, row);
            button->setObjectName(QStringLiteral("zeroPreLaunchFix"));
            Style::setProperty(button, "zeroVariant", fix.danger ? "danger" : "secondary");
            Style::setProperty(button, "zeroSize", "sm");
            button->setAutoDefault(false);
            button->setAccessibleName(QStringLiteral("%1: %2").arg(fix.label, item.title));
            const QString id = item.id;
            connect(button, &QPushButton::clicked, this, [this, id, i]() { onFix(id, i); });
            actions->addWidget(button);
            ui.buttons.append(button);
        }
    }
    column->addLayout(actions);
    layout->addLayout(column, 1);

    into->addWidget(row);
    m_rows.push_back(std::move(ui));
}

void PreLaunchDialog::onFix(const QString& itemId, int fixIndex)
{
    if (!m_session || m_decided) {
        return;
    }
    const PreLaunch::Session::FixResult result = m_session->applyFix(itemId, fixIndex);
    if (result.leavesCheck) {
        finish(Decision::OpenMods);
        return;
    }
    rebuildRows();  // a recheck may have removed or replaced rows; an error is on the item
}

// ---------------------------------------------------------------------------------------------------------------------
// Play
// ---------------------------------------------------------------------------------------------------------------------

bool PreLaunchDialog::confirmPlayAnyway()
{
    ModsUi::ChoicePrompt prompt;
    prompt.title = tr("Play with malware on this instance?");
    prompt.body = tr("The game would run the flagged file. Only do this if you are sure it is a false alarm.");
    prompt.buttons = { { tr("Play anyway"), ModsUi::ChoiceButton::Kind::Danger }, { tr("Go back"), ModsUi::ChoiceButton::Kind::Secondary } };
    prompt.defaultButton = 1;
    const ModsUi::AskFunction ask = m_env.ask ? m_env.ask : ModsUi::AskFunction(ModsUi::askWithDialog);
    return ask(this, prompt) == 0;
}

void PreLaunchDialog::onPlay()
{
    if (!m_session || m_state != State::Results || m_decided) {
        return;
    }
    const Decision decision = m_session->play([this]() { return confirmPlayAnyway(); });
    if (decision == Decision::Play) {
        finish(Decision::Play);
    }
    // Cancel here = "Go back": the dialog stays as it was
}

// ---------------------------------------------------------------------------------------------------------------------
// Look
// ---------------------------------------------------------------------------------------------------------------------

void PreLaunchDialog::fitBody()
{
    if (!m_content || !m_content->layout()) {
        return;
    }
    QLayout* layout = m_content->layout();
    layout->activate();
    const int width = kWidth - 2 * Tokens::spaceXl;
    const int wanted = layout->hasHeightForWidth() ? layout->totalHeightForWidth(width) : layout->sizeHint().height();
    const int height = std::min(std::max(wanted, kMinBodyHeight), kMaxBodyHeight);
    if (m_scroll->height() != height || m_scroll->minimumHeight() != height) {
        m_scroll->setFixedHeight(height);
        if (isVisible()) {
            adjustSize();
        }
    }
}

void PreLaunchDialog::done(int result)
{
    m_decided = true;  // closed (Cancel, Esc, a decision): a scan that ends later is ignored
    QDialog::done(result);
}

void PreLaunchDialog::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    Paint::paintBackground(p, rect(), Theme::current().bg);
}

void PreLaunchDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    fitBody();
    adjustSize();
}

// ---------------------------------------------------------------------------------------------------------------------
// Parts for tests and dev aids
// ---------------------------------------------------------------------------------------------------------------------

const PreLaunchDialog::RowUi* PreLaunchDialog::rowUi(const QString& id) const
{
    for (const RowUi& row : m_rows) {
        if (row.id == id) {
            return &row;
        }
    }
    return nullptr;
}

QString PreLaunchDialog::titleText() const
{
    return m_title->text();
}

QString PreLaunchDialog::summaryText() const
{
    return m_summary->isVisibleTo(this) ? m_summary->text() : QString();
}

QString PreLaunchDialog::checkingText() const
{
    return m_state == State::Checking ? m_checkingLabel->text() : QString();
}

QString PreLaunchDialog::progressText() const
{
    return m_state == State::Checking ? m_progressLabel->text() : QString();
}

QStringList PreLaunchDialog::sectionTitles() const
{
    return m_sectionTitles;
}

QStringList PreLaunchDialog::rowIds() const
{
    QStringList ids;
    for (const RowUi& row : m_rows) {
        ids << row.id;
    }
    return ids;
}

QString PreLaunchDialog::rowTitle(const QString& id) const
{
    const RowUi* row = rowUi(id);
    return row ? row->title : QString();
}

QString PreLaunchDialog::rowDetail(const QString& id) const
{
    const RowUi* row = rowUi(id);
    return row ? row->detail : QString();
}

QString PreLaunchDialog::rowSection(const QString& id) const
{
    const RowUi* row = rowUi(id);
    return row ? row->section : QString();
}

bool PreLaunchDialog::rowFixed(const QString& id) const
{
    const RowUi* row = rowUi(id);
    return row && row->fixed;
}

QString PreLaunchDialog::rowFixedText(const QString& id) const
{
    const RowUi* row = rowUi(id);
    return row ? row->fixedText : QString();
}

QString PreLaunchDialog::rowError(const QString& id) const
{
    const RowUi* row = rowUi(id);
    return row ? row->error : QString();
}

QString PreLaunchDialog::rowIconName(const QString& id) const
{
    const RowUi* row = rowUi(id);
    return row ? row->iconName : QString();
}

QList<QPushButton*> PreLaunchDialog::rowButtons(const QString& id) const
{
    const RowUi* row = rowUi(id);
    return row ? row->buttons : QList<QPushButton*>();
}

// ---------------------------------------------------------------------------------------------------------------------
// The flow
// ---------------------------------------------------------------------------------------------------------------------

Decision runBeforeYouPlay(PreLaunchEnvironment env, QWidget* parent)
{
    if (!env.env.enabled) {
        return Decision::Play;
    }
    PreLaunch::RunOptions quick;
    quick.cachedOnly = true;
    const PreLaunch::CheckResult first = scanOf(env)(env.env, quick);
    if (first.complete && first.items.isEmpty()) {
        return Decision::Play;  // the normal launch: no dialog, no delay
    }
    PreLaunchDialog dialog(std::move(env), parent ? parent->window() : nullptr);
    if (first.complete) {
        dialog.showResults(first.items);
    } else {
        dialog.startChecking();  // an incomplete cachedOnly run returns no items: the full scan decides what is shown
    }
    dialog.exec();
    return dialog.decision();
}

PreLaunchEnvironment environmentFor(MinecraftInstance* instance)
{
    PreLaunchEnvironment out;
    out.instanceName = instance->name();
    PreLaunch::Env& env = out.env;
    env.instanceName = instance->name();
    env.instanceRoot = instance->instanceRoot();
    env.gameRoot = instance->gameRoot();
    env.enabled = APPLICATION->settings()->get("ZeroPreLaunchCheck").toBool();
    env.instanceChecksEnabled = !instance->settings()->get("ZeroSkipPreLaunchCheck").toBool();

    // The resource-pack rollback is one row of the dialog. The offer is read once and shared by the three hooks (the scan thread calls the first
    // one, the UI thread the others; a mutex keeps the shared copy tidy).
    struct SharedOffer {
        std::mutex lock;
        std::optional<PacksUi::RollbackOffer> offer;
    };
    auto shared = std::make_shared<SharedOffer>();
    const PacksUi::RollbackEnv rollbackEnv = PacksUi::rollbackEnvFor(instance);
    env.packOffer = [rollbackEnv, shared]() {
        PreLaunch::PackOffer result;
        std::lock_guard<std::mutex> guard(shared->lock);
        shared->offer = PacksUi::rollbackOffer(rollbackEnv);
        if (shared->offer) {
            result.valid = true;
            result.suspects = shared->offer->presentSuspects;
        }
        return result;
    };
    env.restorePacks = [rollbackEnv, shared]() -> QString {
        std::lock_guard<std::mutex> guard(shared->lock);
        if (!shared->offer) {
            shared->offer = PacksUi::rollbackOffer(rollbackEnv);
        }
        if (!shared->offer) {
            return QString();  // nothing left to restore
        }
        const auto outcome = PacksUi::restoreRollback(rollbackEnv, *shared->offer);
        if (outcome.ok()) {
            return QString();
        }
        return outcome.error.isEmpty() ? QObject::tr("options.txt could not be changed.") : outcome.error;
    };
    env.dismissPacks = [shared]() {
        std::lock_guard<std::mutex> guard(shared->lock);
        if (shared->offer) {
            PacksUi::dismissRollback(*shared->offer);
        }
    };

    // recycle / showFile / openUrl stay null: the Session's defaults are FS::trash, Explorer and DesktopServices::openUrl
    QPointer<MinecraftInstance> guarded(instance);
    out.hooks.setSkipInstance = [guarded](bool skip) {
        if (guarded) {
            guarded->settings()->set("ZeroSkipPreLaunchCheck", skip);
        }
    };
    return out;
}

bool checkBeforeLaunch(MinecraftInstance* instance, QWidget* parent)
{
    if (!instance || !APPLICATION->settings()->get("ZeroPreLaunchCheck").toBool()) {
        return true;
    }
    const Decision decision = runBeforeYouPlay(environmentFor(instance), parent);
    if (decision == Decision::OpenMods) {
        APPLICATION->showInstanceWindow(instance, QStringLiteral("mods"));
        return false;
    }
    return decision == Decision::Play;
}

}  // namespace Zero::PreLaunchUi
