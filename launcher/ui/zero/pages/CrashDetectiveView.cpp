// SPDX-License-Identifier: GPL-3.0-only
#include "CrashDetectiveView.h"

#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedLayout>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>
#include "Application.h"
#include "DesktopServices.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "ui/zero/ZeroCard.h"
#include "ui/zero/ZeroEmptyState.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroSpinner.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "zero/diagnostics/CrashDetective.h"
#include "zero/diagnostics/CrashReportCollector.h"

namespace Zero {

using namespace Diagnostics;

namespace {

constexpr int kColumnMaxWidth = 960;

QLabel* makeLabel(const QString& text, Role role, QWidget* parent, const char* tone = nullptr)
{
    auto* label = new QLabel(text, parent);
    Style::applyRole(label, role);
    if (tone)
        Style::setProperty(label, "zeroTone", tone);
    return label;
}

QLabel* makeWrapped(const QString& text, Role role, QWidget* parent, const char* tone = nullptr)
{
    auto* label = makeLabel(text, role, parent, tone);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

/// A QLabel showing a tinted Lucide icon that follows the active theme.
QLabel* makeIconLabel(const QString& icon, int size, QWidget* parent, QColor ThemeSpec::*color)
{
    auto* label = new QLabel(parent);
    label->setFixedSize(size, size);
    label->setAlignment(Qt::AlignCenter);
    auto apply = [label, icon, size, color]() { label->setPixmap(Icons::pixmap(icon, Theme::current().*color, size, label->devicePixelRatioF())); };
    apply();
    QObject::connect(&Theme::instance(), &Theme::changed, label, apply);
    return label;
}

QPushButton* makeButton(const QString& text, const QString& icon, const char* variant, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    Style::setProperty(button, "zeroVariant", variant);
    button->setIconSize(QSize(16, 16));
    if (!icon.isEmpty()) {
        auto apply = [button, icon, variant]() {
            const auto& t = Theme::current();
            button->setIcon(Icons::icon(icon, QByteArray(variant) == "primary" ? t.onAccent : t.text));
        };
        apply();
        QObject::connect(&Theme::instance(), &Theme::changed, button, apply);
    }
    return button;
}

/// Read-only, wrapping, monospace text block that grows to fit its content (no inner scrolling).
class MonoBlock : public QTextEdit {
   public:
    explicit MonoBlock(QWidget* parent) : QTextEdit(parent)
    {
        Style::setProperty(this, "zeroMono", true);
        setReadOnly(true);
        setFrameShape(QFrame::NoFrame);
        setLineWrapMode(QTextEdit::WidgetWidth);
        setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        f.setPixelSize(Tokens::caption.px);
        setFont(f);
        document()->setDocumentMargin(0);
        viewport()->setAutoFillBackground(false);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
    }

    void setBlockText(const QString& text)
    {
        setPlainText(text);
        fit();
    }

    QSize sizeHint() const override { return { 100, m_height }; }
    QSize minimumSizeHint() const override { return { 0, m_height }; }

   protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QTextEdit::resizeEvent(event);
        fit();
    }
    void showEvent(QShowEvent* event) override
    {
        QTextEdit::showEvent(event);
        fit();
    }

   private:
    void fit()
    {
        document()->setTextWidth(std::max(1, viewport()->width()));
        const int h = int(std::ceil(document()->size().height())) + 2;
        if (h != m_height) {
            m_height = h;
            setFixedHeight(h);
            updateGeometry();
        }
    }
    int m_height = 20;
};

QString confidenceText(Confidence c)
{
    switch (c) {
        case Confidence::Confirmed:
            return QObject::tr("Confirmed");
        case Confidence::Likely:
            return QObject::tr("Likely");
        case Confidence::Possible:
            return QObject::tr("Possible");
    }
    return {};
}

/// Never green for a problem. Only an explicit, critical cause is loud; everything else stays neutral.
const char* badgeTone(Severity s, Confidence c)
{
    if (c != Confidence::Confirmed)
        return "neutral";
    switch (s) {
        case Severity::Critical:
            return "danger";
        case Severity::Warning:
            return "warning";
        case Severity::Info:
            return "info";
    }
    return "neutral";
}

struct ActionInfo {
    QString text;
    QString icon;
};

ActionInfo actionInfo(Action a)
{
    switch (a) {
        case Action::OpenInstanceSettings:
            return { QObject::tr("Instance settings"), QStringLiteral("settings") };
        case Action::OpenModsFolder:
            return { QObject::tr("Open mods folder"), QStringLiteral("folder-open") };
        case Action::OpenLogsFolder:
            return { QObject::tr("Open logs folder"), QStringLiteral("folder-open") };
        case Action::OpenCrashReport:
            return { QObject::tr("Open crash report"), QStringLiteral("file-text") };
        case Action::OpenGameFolder:
            return { QObject::tr("Open game folder"), QStringLiteral("folder-open") };
        case Action::OpenJavaDoctor:
            return { QObject::tr("Open Java Doctor"), QStringLiteral("cpu") };
    }
    return {};
}

/// One finding: severity icon, title, confidence badge, explanation, suggestions, actions, collapsible evidence.
class FindingCard : public ZeroCard {
   public:
    FindingCard(const Finding& finding, bool crashFileAvailable, const std::function<void(Action)>& onAction, QWidget* parent) : ZeroCard(parent)
    {
        setHoverEnabled(false);
        setContentsMargins(0, 0, 0, 0);
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(Tokens::spaceXl, Tokens::spaceLg, Tokens::spaceXl, Tokens::spaceLg);
        root->setSpacing(Tokens::spaceMd);

        // header: icon, title, badge
        auto* head = new QHBoxLayout;
        head->setContentsMargins(0, 0, 0, 0);
        head->setSpacing(Tokens::spaceMd);
        QString icon = QStringLiteral("info");
        QColor ThemeSpec::*color = &ThemeSpec::info;
        if (finding.severity == Severity::Critical) {
            icon = QStringLiteral("circle-x");
            color = &ThemeSpec::danger;
        } else if (finding.severity == Severity::Warning) {
            icon = QStringLiteral("triangle-alert");
            color = &ThemeSpec::warning;
        }
        head->addWidget(makeIconLabel(icon, 20, this, color), 0, Qt::AlignTop);
        auto* title = makeWrapped(finding.title, Role::Heading, this);
        head->addWidget(title, 1);
        auto* badge = new QLabel(confidenceText(finding.confidence), this);
        badge->setProperty("zeroBadge", QString::fromLatin1(badgeTone(finding.severity, finding.confidence)));
        badge->setToolTip(finding.confidence == Confidence::Confirmed ? QObject::tr("The log states this cause explicitly.")
                          : finding.confidence == Confidence::Likely ? QObject::tr("The log points to this, but it is an inference.")
                                                                     : QObject::tr("A weak signal: worth trying, not a diagnosis."));
        badge->setAlignment(Qt::AlignCenter);
        head->addWidget(badge, 0, Qt::AlignTop);
        root->addLayout(head);

        // explanation, aligned with the title (icon 20 + gap 12)
        auto* body = new QVBoxLayout;
        body->setContentsMargins(20 + Tokens::spaceMd, 0, 0, 0);
        body->setSpacing(Tokens::spaceMd);
        body->addWidget(makeWrapped(finding.explanation, Role::Body, this, "secondary"));

        if (!finding.suggestions.isEmpty()) {
            body->addWidget(makeLabel(QObject::tr("What to try"), Role::Overline, this));
            auto* list = new QVBoxLayout;
            list->setContentsMargins(0, 0, 0, 0);
            list->setSpacing(Tokens::spaceXs);
            for (const QString& tip : finding.suggestions) {
                auto* row = new QHBoxLayout;
                row->setContentsMargins(0, 0, 0, 0);
                row->setSpacing(Tokens::spaceSm);
                auto* bullet = makeLabel(QStringLiteral("•"), Role::Body, this, "tertiary");
                bullet->setFixedWidth(10);
                row->addWidget(bullet, 0, Qt::AlignTop);
                row->addWidget(makeWrapped(tip, Role::Body, this), 1);
                list->addLayout(row);
            }
            body->addLayout(list);
        }

        // actions
        auto* actions = new QHBoxLayout;
        actions->setContentsMargins(0, Tokens::spaceXs, 0, 0);
        actions->setSpacing(Tokens::spaceSm);
        int shown = 0;
        for (Action a : finding.actions) {
            if (a == Action::OpenCrashReport && !crashFileAvailable)
                continue;
            const ActionInfo info = actionInfo(a);
            auto* button = makeButton(info.text, info.icon, "secondary", this);
            QObject::connect(button, &QPushButton::clicked, this, [onAction, a]() { onAction(a); });
            actions->addWidget(button);
            ++shown;
        }
        actions->addStretch(1);
        if (shown > 0)
            body->addLayout(actions);

        // evidence
        if (!finding.evidence.isEmpty()) {
            QStringList lines;
            for (const auto& e : finding.evidence)
                lines << QStringLiteral("%1:%2   %3").arg(e.source).arg(e.line).arg(e.text);

            auto* toggle = makeButton({}, {}, "ghost", this);
            m_toggle = toggle;
            m_evidenceCount = int(finding.evidence.size());
            m_panel = new QFrame(this);
            m_panel->setProperty("zeroInset", true);
            auto* panelLayout = new QVBoxLayout(m_panel);
            panelLayout->setContentsMargins(Tokens::spaceMd, Tokens::spaceMd, Tokens::spaceMd, Tokens::spaceMd);
            m_block = new MonoBlock(m_panel);
            m_block->setBlockText(lines.join(QLatin1Char('\n')));
            panelLayout->addWidget(m_block);
            m_panel->setVisible(false);
            QObject::connect(toggle, &QPushButton::clicked, this, [this]() { setEvidenceVisible(!m_panel->isVisible()); });
            QObject::connect(&Theme::instance(), &Theme::changed, this, [this]() { refreshToggle(); });
            auto* toggleRow = new QHBoxLayout;
            toggleRow->setContentsMargins(0, 0, 0, 0);
            toggleRow->addWidget(toggle);
            toggleRow->addStretch(1);
            body->addLayout(toggleRow);
            body->addWidget(m_panel);
            refreshToggle();
        }
        root->addLayout(body);
    }

    void setEvidenceVisible(bool visible)
    {
        if (!m_panel)
            return;
        m_panel->setVisible(visible);
        refreshToggle();
    }

   private:
    void refreshToggle()
    {
        if (!m_toggle)
            return;
        const bool open = m_panel && m_panel->isVisible();
        m_toggle->setText(open ? QObject::tr("Hide evidence") : m_evidenceCount == 1 ? QObject::tr("Show evidence (1 line)") : QObject::tr("Show evidence (%1 lines)").arg(m_evidenceCount));
        m_toggle->setIconSize(QSize(14, 14));
        m_toggle->setIcon(Icons::icon(open ? QStringLiteral("chevron-down") : QStringLiteral("chevron-right"), Theme::current().textSecondary));
    }

    QPushButton* m_toggle = nullptr;
    QFrame* m_panel = nullptr;
    MonoBlock* m_block = nullptr;
    int m_evidenceCount = 0;
};

}  // namespace

QString CrashDetectiveView::description()
{
    return tr("Why the game stopped, analyzed offline");
}

CrashDetectiveView::CrashDetectiveView(QWidget* parent)
    : QWidget(parent),
      m_controls(new QWidget(this)),
      m_instances(new QComboBox(this)),
      m_analyze(new QPushButton(tr("Analyze"), this)),
      m_spinner(new ZeroSpinner(ZeroSpinner::Small, this))
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Controls (shown by the Tools page in the top bar): spinner, instance combo, Analyze. Hidden until the top bar borrows them.
    auto* controls = new QHBoxLayout(m_controls);
    controls->setContentsMargins(0, 0, 0, 0);
    controls->setSpacing(Tokens::spaceSm);
    m_instances->setFixedWidth(220);  // leaves room for the subtitle in the top bar
    m_instances->setToolTip(tr("Instance to analyze"));
    controls->addWidget(m_spinner);
    controls->addWidget(m_instances);
    Style::setProperty(m_analyze, "zeroVariant", "primary");
    m_analyze->setIconSize(QSize(16, 16));
    auto applyAnalyzeIcon = [this]() { m_analyze->setIcon(Icons::icon(QStringLiteral("stethoscope"), Theme::current().onAccent)); };
    applyAnalyzeIcon();
    connect(&Theme::instance(), &Theme::changed, this, applyAnalyzeIcon);
    controls->addWidget(m_analyze);
    m_spinner->setVisible(false);
    m_controls->hide();

    m_caption = new QLabel(this);
    Style::applyRole(m_caption, Role::Caption);
    m_caption->setWordWrap(true);
    m_caption->setVisible(false);
    outer->addWidget(m_caption);
    outer->addSpacing(Tokens::spaceMd);

    // result area
    auto* stackHost = new QWidget(this);
    m_stack = new QStackedLayout(stackHost);
    m_stack->setContentsMargins(0, 0, 0, 0);

    m_initial = new ZeroEmptyState(QStringLiteral("stethoscope"), tr("Find out why the game stopped"),
                                   tr("Choose an instance and select Analyze. Crash Detective reads the latest log and the newest crash reports and "
                                      "explains the causes it recognises."),
                                   {}, stackHost);
    m_stack->addWidget(m_initial);

    auto* scroll = new QScrollArea(stackHost);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_findingsHost = new QWidget;
    scroll->setWidget(m_findingsHost);
    m_findingsLayout = new QVBoxLayout(m_findingsHost);
    m_findingsLayout->setContentsMargins(0, 0, Tokens::spaceSm, Tokens::spaceXl);
    m_findingsLayout->setSpacing(Tokens::spaceLg);
    m_stack->addWidget(scroll);

    m_nothing = new ZeroEmptyState(QStringLiteral("search"), tr("No known cause recognised"),
                                   tr("The logs were read, but nothing in them matches a problem Crash Detective knows. That does not mean the game is "
                                      "fine: the cause may simply not be one it can recognise."),
                                   tr("Open log"), stackHost);
    connect(m_nothing, &ZeroEmptyState::actionTriggered, this, &CrashDetectiveView::openBestLog);
    connect(m_nothing->addSecondaryAction(tr("Open logs folder")), &QPushButton::clicked, this, [this]() { runAction(Action::OpenLogsFolder); });
    m_stack->addWidget(m_nothing);

    m_noLogs = new ZeroEmptyState(QStringLiteral("file-text"), tr("No logs found"),
                                  tr("This instance has no log files yet. Launch it once; if it stops before writing anything, open the game folder "
                                     "to look for files by hand."),
                                  tr("Open game folder"), stackHost);
    connect(m_noLogs, &ZeroEmptyState::actionTriggered, this, [this]() { runAction(Action::OpenGameFolder); });
    m_stack->addWidget(m_noLogs);

    outer->addWidget(stackHost, 1);

    connect(m_analyze, &QPushButton::clicked, this, [this]() { analyze(); });
    connect(m_instances, &QComboBox::currentIndexChanged, this, [this]() {
        if (!m_running)
            showInitial();
    });
    connect(&m_watcher, &QFutureWatcher<Result>::finished, this, [this]() {
        const Result result = m_watcher.result();
        if (result.token != m_token)
            return;  // superseded
        setRunning(false);
        showResult(result);
    });

    refreshInstances();
}

void CrashDetectiveView::refreshInstances()
{
    const QString keep = currentInstanceId();
    auto* list = APPLICATION->instances();
    QList<QPair<QString, QString>> items;
    for (int i = 0; i < list->count(); ++i)
        items.append({ list->at(i)->name(), list->at(i)->id() });
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.first.compare(b.first, Qt::CaseInsensitive) < 0; });

    QSignalBlocker block(m_instances);
    m_instances->clear();
    for (const auto& item : items)
        m_instances->addItem(item.first, item.second);
    const int index = m_instances->findData(keep);
    m_instances->setCurrentIndex(index >= 0 ? index : (items.isEmpty() ? -1 : 0));
    m_analyze->setEnabled(!items.isEmpty() && !m_running);
    m_instances->setEnabled(!items.isEmpty() && !m_running);
    if (items.isEmpty())
        m_initial->setBody(tr("Create an instance first. Crash Detective analyzes the logs of an instance."));
}

void CrashDetectiveView::setInstance(const QString& instanceId)
{
    if (m_running)
        return;
    const int index = m_instances->findData(instanceId);
    if (index < 0) {
        refreshInstances();
        const int again = m_instances->findData(instanceId);
        if (again < 0)
            return;
        m_instances->setCurrentIndex(again);
        return;
    }
    if (index != m_instances->currentIndex())
        m_instances->setCurrentIndex(index);  // currentIndexChanged resets the view
}

QString CrashDetectiveView::currentInstanceId() const
{
    return m_instances->currentData().toString();
}

bool CrashDetectiveView::hasResult() const
{
    return m_stack->currentIndex() != 0;
}

void CrashDetectiveView::showInitial()
{
    m_caption->setVisible(false);
    m_sources.clear();
    m_stack->setCurrentIndex(0);
}

void CrashDetectiveView::setRunning(bool running)
{
    m_running = running;
    m_spinner->setVisible(running);
    m_analyze->setEnabled(!running && m_instances->count() > 0);
    m_instances->setEnabled(!running && m_instances->count() > 0);
    m_analyze->setText(running ? tr("Analyzing") : tr("Analyze"));
}

void CrashDetectiveView::analyze(std::optional<int> exitCode)
{
    if (m_running)
        return;
    auto* instance = APPLICATION->instances()->getInstanceById(currentInstanceId());
    if (!instance)
        return;
    setRunning(true);
    const quint64 token = ++m_token;
    const QString id = instance->id();
    const QString gameRoot = instance->gameRoot();
    const qint64 lastLaunch = instance->lastLaunch();
    // Everything that touches the disk or scans megabytes of text happens off the UI thread.
    m_watcher.setFuture(QtConcurrent::run([=]() {
        Result r;
        r.instanceId = id;
        r.token = token;
        r.sources = CrashReportCollector::collectFromGameRoot(gameRoot, lastLaunch);
        r.findings = CrashDetective::analyze(r.sources, exitCode);
        return r;
    }));
}

QString CrashDetectiveView::describeSources(const QList<LogSource>& sources) const
{
    QStringList parts;
    for (const auto& s : sources) {
        QString p = QStringLiteral("%1 (%2").arg(s.name, QLocale().toString(s.modified, QLocale::ShortFormat));
        if (s.truncated)
            p += tr(", last %1 MB only").arg(CrashReportCollector::kMaxLatestLogBytes / (1024 * 1024));
        if (s.older)
            p += tr(", from before the last launch");
        p += QLatin1Char(')');
        parts << p;
    }
    return parts.join(QStringLiteral("  ·  "));
}

void CrashDetectiveView::showResult(const Result& result)
{
    m_sources = result.sources;
    while (QLayoutItem* item = m_findingsLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    if (result.sources.isEmpty()) {
        m_caption->setText(tr("No log files were found for this instance."));
        m_caption->setVisible(true);
        m_stack->setCurrentWidget(m_noLogs);
        emit analysisFinished();
        return;
    }

    const QString analyzed = tr("Analyzed: %1").arg(describeSources(result.sources));
    if (result.findings.isEmpty()) {
        m_caption->setText(analyzed);
        m_caption->setVisible(true);
        m_stack->setCurrentWidget(m_nothing);
        emit analysisFinished();
        return;
    }

    m_caption->setText((result.findings.size() == 1 ? tr("1 finding.") : tr("%1 findings.").arg(result.findings.size())) + QLatin1Char(' ') + analyzed);
    m_caption->setVisible(true);
    const bool crashFile = !crashSourcePath().isEmpty();
    for (const Finding& f : result.findings)
        m_findingsLayout->addWidget(new FindingCard(f, crashFile, [this](Action a) { runAction(a); }, m_findingsHost));
    m_findingsLayout->addStretch(1);
    m_stack->setCurrentIndex(1);
    emit analysisFinished();
}

QString CrashDetectiveView::crashSourcePath() const
{
    // Prefer a crash report / hs_err file over latest.log.
    for (const auto& s : m_sources)
        if (s.name != QLatin1String("latest.log") && !s.path.isEmpty())
            return s.path;
    for (const auto& s : m_sources)
        if (!s.path.isEmpty())
            return s.path;
    return {};
}

void CrashDetectiveView::openBestLog()
{
    for (const auto& s : m_sources)
        if (s.name == QLatin1String("latest.log") && !s.path.isEmpty()) {
            DesktopServices::openPath(QFileInfo(s.path));
            return;
        }
    const QString path = crashSourcePath();
    if (!path.isEmpty())
        DesktopServices::openPath(QFileInfo(path));
}

void CrashDetectiveView::runAction(Action action)
{
    auto* instance = APPLICATION->instances()->getInstanceById(currentInstanceId());
    if (!instance)
        return;
    switch (action) {
        case Action::OpenInstanceSettings:
            emit instanceSettingsRequested(instance->id());
            break;
        case Action::OpenModsFolder:
            DesktopServices::openPath(QFileInfo(instance->modsRoot()), true);
            break;
        case Action::OpenLogsFolder:
            DesktopServices::openPath(QFileInfo(QDir(instance->gameRoot()).filePath(QStringLiteral("logs"))), true);
            break;
        case Action::OpenCrashReport: {
            const QString path = crashSourcePath();
            if (!path.isEmpty())
                DesktopServices::openPath(QFileInfo(path));
            break;
        }
        case Action::OpenGameFolder:
            DesktopServices::openPath(QFileInfo(instance->gameRoot()), true);
            break;
        case Action::OpenJavaDoctor:
            emit javaDoctorRequested();
            break;
    }
}

void CrashDetectiveView::expandAllEvidence()
{
    for (auto* card : m_findingsHost->findChildren<ZeroCard*>())
        if (auto* fc = dynamic_cast<FindingCard*>(card))
            fc->setEvidenceVisible(true);
}

}  // namespace Zero
