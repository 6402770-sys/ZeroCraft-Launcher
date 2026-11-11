// SPDX-License-Identifier: GPL-3.0-only
#include "JavaDoctorView.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QMessageBox>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QScrollBar>
#include <QStackedLayout>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include "Application.h"
#include "ElidedLabel.h"
#include "FileSystem.h"
#include "IconTile.h"
#include "Json.h"
#include "InstanceList.h"
#include "SysInfo.h"
#include "icons/IconList.h"
#include "java/JavaInstall.h"
#include "java/JavaInstallList.h"
#include "java/JavaUtils.h"
#include "java/JavaVersion.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "settings/SettingsObject.h"
#include "ui/zero/PillTag.h"
#include "ui/zero/ZeroCard.h"
#include "ui/zero/ZeroEmptyState.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroToast.h"

namespace Zero {

using namespace Java;

namespace {

// ---------------------------------------------------------------------------------------------------------------------------------
// Widgets (same look as the Storage page)
// ---------------------------------------------------------------------------------------------------------------------------------

QLabel* makeLabel(const QString& text, Role role, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    Style::applyRole(label, role);
    return label;
}

ElidedLabel* makeElided(const QString& text, Role role, QWidget* parent)
{
    auto* label = new ElidedLabel(parent);
    Style::applyRole(label, role);
    label->setFullText(text);
    return label;
}

QPushButton* makeButton(const QString& text, const QString& icon, const char* variant, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    Style::setProperty(button, "zeroVariant", variant);
    button->setIconSize(QSize(16, 16));
    if (!icon.isEmpty()) {
        auto apply = [button, icon]() { button->setIcon(Icons::icon(icon, Theme::current().text)); };
        apply();
        QObject::connect(&Theme::instance(), &Theme::changed, button, apply);
    }
    return button;
}

/// Row with hover highlight and keyboard focus; emits `activated` on click or Enter/Space (not when a child button took the click).
class HoverRow : public QWidget {
    Q_OBJECT
   public:
    explicit HoverRow(QWidget* parent) : QWidget(parent)
    {
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    }

   signals:
    void activated();

   protected:
    void paintEvent(QPaintEvent*) override
    {
        if (!m_hover && !hasFocus())
            return;
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(hasFocus() ? QPen(t.borderStrong, 1) : Qt::NoPen);
        p.setBrush(t.surfaceHover);
        p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusMd));
    }
    void enterEvent(QEnterEvent*) override
    {
        m_hover = true;
        update();
    }
    void leaveEvent(QEvent*) override
    {
        m_hover = false;
        update();
    }
    void focusInEvent(QFocusEvent* e) override
    {
        QWidget::focusInEvent(e);
        update();
    }
    void focusOutEvent(QFocusEvent* e) override
    {
        QWidget::focusOutEvent(e);
        update();
    }
    void mousePressEvent(QMouseEvent* e) override
    {
        m_pressed = e->button() == Qt::LeftButton;
        e->accept();
    }
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (m_pressed && e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()))
            emit activated();
        m_pressed = false;
    }
    void keyPressEvent(QKeyEvent* e) override
    {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Space)
            emit activated();
        else
            QWidget::keyPressEvent(e);
    }

   private:
    bool m_hover = false;
    bool m_pressed = false;
};

/// A section card with a Heading title; returns the card and its vertical layout.
ZeroCard* makeSection(const QString& title, QWidget* parent, QVBoxLayout** layoutOut)
{
    auto* card = new ZeroCard(parent);
    card->setHoverEnabled(false);
    auto* lay = new QVBoxLayout(card);
    lay->setContentsMargins(Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg);
    lay->setSpacing(Tokens::spaceMd);
    lay->addWidget(makeLabel(title, Role::Heading, card));
    *layoutOut = lay;
    return card;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Gathering (reads settings and the instance list; never writes, never starts a download or a metadata update)
// ---------------------------------------------------------------------------------------------------------------------------------

bool isManaged(const QString& path)
{
    const QString root = normalizedPath(APPLICATION->javaPath());
    if (root.isEmpty())
        return false;
    return normalizedPath(path).startsWith(root + QLatin1Char('/'));
}

QList<Runtime> runtimesFromList()
{
    QList<Runtime> out;
    QSet<QString> seen;
    auto* list = APPLICATION->javalist();
    for (int i = 0; i < list->count(); ++i) {
        auto install = std::dynamic_pointer_cast<JavaInstall>(list->at(i));
        if (!install || install->path.isEmpty())
            continue;
        // The list also holds the bare "javaw" found through PATH: show the file it stands for, once.
        QString path = FS::ResolveExecutable(install->path);
        if (path.isNull())
            path = install->path;
        if (seen.contains(normalizedPath(path)))
            continue;
        seen.insert(normalizedPath(path));
        Runtime r;
        r.path = path;
        r.versionString = install->id.toString();
        r.major = install->id.major();
        r.is64bit = install->is_64bit;
        r.managed = isManaged(install->path);
        out.append(r);
    }
    return out;
}

GlobalJava globalFromApplication()
{
    auto* settings = APPLICATION->settings();
    GlobalJava g;
    g.autoSwitch = settings->get("AutomaticJavaSwitch").toBool();
    g.autoDownload = settings->get("AutomaticJavaDownload").toBool();
    g.downloadSupported = !SysInfo::getSupportedJavaArchitecture().isEmpty();
    const QVariantMap pinnedMap = Json::toMap(settings->get("PinnedJavaPaths").toString());  // what JavaUtils::getPinnedPath reads
    for (auto it = pinnedMap.cbegin(); it != pinnedMap.cend(); ++it) {
        const int major = it.key().toInt();
        const QString pinned = it.value().toString();
        if (major <= 0 || pinned.isEmpty())
            continue;
        const QString resolved = FS::ResolveExecutable(pinned);
        if (resolved.isNull()) {
            g.pinned.insert(major, pinned);
            g.pinnedMissing.insert(major);
        } else {
            g.pinned.insert(major, resolved);
        }
    }
    return g;
}

InstanceJava instanceJavaFrom(MinecraftInstance* inst)
{
    InstanceJava j;
    j.id = inst->id();
    j.name = inst->name();

    // What the version needs: the files on disk first (works for instances whose component list was never loaded; loading it would
    // resolve metadata and rewrite files), then the loaded launch profile when there is one (it is more current).
    Requirements req = readRequirements(inst->instanceRoot(), QDir(APPLICATION->dataRoot()).filePath(QStringLiteral("meta")));
    if (auto* pack = inst->getPackProfile()) {
        const QString loadedVersion = pack->getComponentVersion(QStringLiteral("net.minecraft"));
        if (!loadedVersion.isEmpty()) {
            req.mcVersion = loadedVersion;
            if (auto profile = pack->getProfile(); profile && !profile->getCompatibleJavaMajors().isEmpty()) {
                req.majors = profile->getCompatibleJavaMajors();
                if (const QString name = profile->getCompatibleJavaName(); !name.isEmpty())
                    req.javaName = name;
            }
        }
    }
    j.mcVersion = req.mcVersion;
    j.compatibleMajors = req.majors;
    j.javaDownloadName = req.javaName;
    if (!req.javaName.isEmpty()) {
        const QString managed = QDir(APPLICATION->javaPath()).absoluteFilePath(FS::PathCombine(req.javaName, "bin", JavaUtils::javaExecutable));
        if (QFileInfo::exists(managed))
            j.downloadedJavaPath = managed;
    }

    auto* s = inst->settings();
    j.overrideLocation = s->get("OverrideJavaLocation").toBool();
    j.javaPath = s->get("JavaPath").toString();
    j.javaPathExists = !j.javaPath.isEmpty() && QFileInfo::exists(j.javaPath);
    const QString resolved = FS::ResolveExecutable(j.javaPath);
    j.javaPathResolved = resolved.isNull() ? QString() : resolved;
    j.storedVersion = s->get("JavaVersion").toString();
    j.storedArch = s->get("JavaArchitecture").toString();
    j.ignoreCompatibility = s->get("IgnoreJavaCompatibility").toBool();
    j.maxMemMb = s->get("MaxMemAlloc").toInt();
    return j;
}

bool isBusy(const QString& instanceId)
{
    auto* inst = APPLICATION->instances()->getInstanceById(instanceId);
    return inst && (inst->isRunning() || APPLICATION->launchPhase(instanceId) != Application::LaunchPhase::Idle);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Words
// ---------------------------------------------------------------------------------------------------------------------------------

QString majorsText(const QList<int>& majors)
{
    const QList<int> sorted = sortedMajors(majors);
    QStringList parts;
    for (int m : sorted)
        parts << QString::number(m);
    if (parts.size() <= 1)
        return parts.join(QString());
    const QString last = parts.takeLast();
    return QObject::tr("%1 or %2").arg(parts.join(QStringLiteral(", ")), last);
}

QString needsText(const InstanceJava& j)
{
    QStringList parts;
    if (!j.mcVersion.isEmpty())
        parts << QObject::tr("Minecraft %1").arg(j.mcVersion);
    parts << (j.compatibleMajors.isEmpty() ? QObject::tr("Java requirement unknown") : QObject::tr("needs Java %1").arg(majorsText(j.compatibleMajors)));
    return parts.join(QStringLiteral(" · "));
}

QString javaName(const Runtime& r)
{
    if (!r.versionString.isEmpty())
        return QObject::tr("Java %1").arg(r.versionString);
    if (r.major > 0)
        return QObject::tr("Java %1").arg(r.major);
    return QObject::tr("Java");
}

QString sourceText(const Diagnosis& d, const InstanceJava& j)
{
    switch (d.source) {
        case Source::AutomaticPinned:
        case Source::AutomaticDetected: return QObject::tr("picked automatically");
        case Source::AutomaticDownload: return QObject::tr("downloaded by ZERO");
        case Source::Configured: break;
    }
    return j.overrideLocation ? QObject::tr("set for this instance") : QObject::tr("default Java");
}

QString reasonText(const Diagnosis& d, const InstanceJava& j)
{
    const QString inUse = d.effective ? javaName(*d.effective) : QObject::tr("Java");
    switch (d.reason) {
        case Reason::Ok:
        case Reason::RequirementUnknown:
            return d.effective ? QObject::tr("Uses %1 (%2)").arg(inUse, sourceText(d, j)) : QObject::tr("Java is picked when you launch");
        case Reason::IncompatibleMajor:
            return QObject::tr("%1 is set, but this version needs Java %2").arg(inUse, majorsText(j.compatibleMajors));
        case Reason::IncompatibleIgnored: return QObject::tr("Incompatible Java, but the compatibility check is turned off");
        case Reason::ConfiguredPathMissing:
            return j.overrideLocation ? QObject::tr("The Java set for this instance no longer exists")
                                      : QObject::tr("The default Java does not exist. Set one in the Java settings");
        case Reason::NoCompatibleInstalledDownloadOff: return QObject::tr("No compatible Java installed and automatic downloads are off");
        case Reason::WillDownload: return QObject::tr("Java %1 will be downloaded on the next launch").arg(d.downloadMajor);
        case Reason::ThirtyTwoBitMemory:
            return QObject::tr("32-bit Java with %1 MB of memory: the game may not start").arg(j.maxMemMb);
        case Reason::PinnedPathMissing:
            return QObject::tr("The Java %1 path pinned in the settings does not exist. %2 is used instead").arg(d.pinnedMissingMajor).arg(inUse);
        case Reason::RuntimeUnknown: return QObject::tr("This Java has not been checked yet. It is checked when you launch");
    }
    return {};
}

int rank(Status s)
{
    switch (s) {
        case Status::WillFail: return 0;
        case Status::Warning: return 1;
        case Status::Unknown: return 2;
        case Status::Ready: return 3;
    }
    return 3;
}

PillTag* statusPill(Status s, QWidget* parent)
{
    switch (s) {
        case Status::Ready: return new PillTag(QObject::tr("Ready"), PillTag::Tone::Success, parent);
        case Status::Warning: return new PillTag(QObject::tr("Check"), PillTag::Tone::Warning, parent);
        case Status::WillFail: return new PillTag(QObject::tr("Will fail"), PillTag::Tone::Danger, parent);
        case Status::Unknown: break;
    }
    return new PillTag(QCoreApplication::translate("JavaDoctorView", "Unknown"), PillTag::Tone::Plain, parent);
}

void openFolder(const QString& path)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

/// The folder of an installation: the parent of `bin` when the executable sits in one.
QString runtimeFolder(const QString& exePath)
{
    QDir dir = QFileInfo(exePath).absoluteDir();
    if (dir.dirName().compare(QLatin1String("bin"), Qt::CaseInsensitive) == 0 && dir.cdUp())
        return dir.absolutePath();
    return dir.absolutePath();
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------------------

JavaDoctorView::JavaDoctorView(QWidget* parent) : QWidget(parent)
{
    m_controls = new QWidget(this);
    auto* controlsRow = new QHBoxLayout(m_controls);
    controlsRow->setContentsMargins(0, 0, 0, 0);
    m_rescan = makeButton(tr("Rescan"), QStringLiteral("refresh-cw"), "ghost", m_controls);
    m_rescan->setToolTip(tr("Look for Java runtimes again"));
    controlsRow->addWidget(m_rescan);
    m_controls->hide();
    connect(m_rescan, &QPushButton::clicked, this, &JavaDoctorView::scan);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    m_stack = new QStackedLayout;
    outer->addLayout(m_stack, 1);

    m_stack->addWidget(new ZeroEmptyState(QStringLiteral("cpu"), tr("Looking for Java runtimes…"), tr("This only takes a moment."), {}, this));

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_stack->addWidget(m_scroll);
}

QString JavaDoctorView::description()
{
    return tr("Java runtimes on this PC and the Java each instance needs");
}

void JavaDoctorView::open()
{
    if (!m_loading)
        scan();
    updateBusy();
}

void JavaDoctorView::updateBusy()
{
    m_rescan->setEnabled(!m_loading);
}

void JavaDoctorView::scan()
{
    if (m_loading)
        return;
    m_loading = true;
    if (!m_evaluated)
        m_stack->setCurrentIndex(0);
    updateBusy();

    // The detection runs in Prism's own task (a process per candidate, off the GUI thread); when it is already running we join it.
    m_task = APPLICATION->javalist()->getLoadTask(true);
    if (!m_task || m_task->isFinished()) {
        finishScan();
        return;
    }
    connect(m_task.get(), &Task::finished, this, &JavaDoctorView::finishScan, Qt::SingleShotConnection);
}

void JavaDoctorView::finishScan()
{
    m_task.reset();
    m_loading = false;
    evaluate();
    updateBusy();
}

void JavaDoctorView::evaluate()
{
    m_runtimes = runtimesFromList();
    m_global = globalFromApplication();

    m_rows.clear();
    auto* instances = APPLICATION->instances();
    for (int i = 0; i < instances->count(); ++i) {
        MinecraftInstance* inst = instances->at(i);
        if (!inst)
            continue;
        Row row;
        row.java = instanceJavaFrom(inst);
        row.diagnosis = diagnose(row.java, m_runtimes, m_global);
        m_rows.append(row);
    }
    std::stable_sort(m_rows.begin(), m_rows.end(), [](const Row& a, const Row& b) {
        if (rank(a.diagnosis.status) != rank(b.diagnosis.status))
            return rank(a.diagnosis.status) < rank(b.diagnosis.status);
        return a.java.name.compare(b.java.name, Qt::CaseInsensitive) < 0;
    });

    m_evaluated = true;
    rebuild();
    m_stack->setCurrentIndex(1);
    emit evaluationFinished();
}

void JavaDoctorView::rebuild()
{
    m_host = new QWidget;
    auto* page = new QVBoxLayout(m_host);
    page->setContentsMargins(0, 0, Tokens::spaceSm, Tokens::spaceXl);
    page->setSpacing(Tokens::spaceLg);
    QWidget* host = m_host;

    // ----- automatic Java -----
    {
        QVBoxLayout* lay = nullptr;
        auto* card = makeSection(tr("Automatic Java"), host, &lay);
        lay->setSpacing(Tokens::spaceSm);
        auto addRow = [&](const QString& title, const QString& caption, bool on) {
            auto* row = new QWidget(card);
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(0, Tokens::spaceXs, 0, Tokens::spaceXs);
            h->setSpacing(Tokens::spaceMd);
            auto* texts = new QVBoxLayout;
            texts->setContentsMargins(0, 0, 0, 0);
            texts->setSpacing(0);
            texts->addWidget(makeElided(title, Role::Label, row));
            texts->addWidget(makeElided(caption, Role::Caption, row));
            h->addLayout(texts, 1);
            auto* value = makeLabel(on ? tr("On") : tr("Off"), Role::Label, row);
            value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            h->addWidget(value);
            lay->addWidget(row);
        };
        addRow(tr("Automatic Java selection"), tr("ZERO picks a compatible Java when you launch"), m_global.autoSwitch);
        addRow(tr("Automatic Java downloads"), tr("Missing Java versions are downloaded when needed"), m_global.autoDownload);
        auto* settings = makeButton(tr("Java settings"), QStringLiteral("settings"), "ghost", card);
        connect(settings, &QPushButton::clicked, this, &JavaDoctorView::javaSettingsRequested);
        lay->addSpacing(Tokens::spaceXs);
        lay->addWidget(settings, 0, Qt::AlignLeft);
        page->addWidget(card);
    }

    // ----- instances -----
    {
        QVBoxLayout* lay = nullptr;
        auto* card = makeSection(tr("Instances"), host, &lay);
        lay->setSpacing(Tokens::spaceXs);
        if (m_rows.isEmpty())
            lay->addWidget(makeLabel(tr("No instances yet."), Role::Caption, card));
        QList<QWidget*> actionSlots;
        for (const Row& r : std::as_const(m_rows)) {
            auto* row = new HoverRow(card);
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(Tokens::spaceSm, Tokens::spaceSm, Tokens::spaceSm, Tokens::spaceSm);
            h->setSpacing(Tokens::spaceMd);

            auto* tile = new IconTile(36, 24, row);
            if (auto* instance = APPLICATION->instances()->getInstanceById(r.java.id))
                tile->setInstanceIcon(APPLICATION->icons()->getIcon(instance->iconKey()));
            h->addWidget(tile);

            auto* texts = new QVBoxLayout;
            texts->setContentsMargins(0, 0, 0, 0);
            texts->setSpacing(0);
            texts->addWidget(makeElided(r.java.name, Role::Label, row));
            texts->addWidget(makeElided(needsText(r.java), Role::Caption, row));
            texts->addWidget(makeElided(reasonText(r.diagnosis, r.java), Role::Caption, row));
            h->addLayout(texts, 1);

            h->addWidget(statusPill(r.diagnosis.status, row));
            if (r.diagnosis.recommendation) {
                const Runtime rec = *r.diagnosis.recommendation;
                auto* use = makeButton(tr("Use Java %1").arg(rec.major), QString(), "secondary", row);
                use->setToolTip(tr("Use %1 for this instance").arg(javaName(rec)));
                const QString id = r.java.id;
                connect(use, &QPushButton::clicked, this, [this, id, rec]() { askUseJava(id, rec); });
                h->addWidget(use);
                actionSlots << use;
            } else {
                auto* slot = new QWidget(row);  // keeps the status pills of all rows in one column
                h->addWidget(slot);
                actionSlots << slot;
            }

            const QString id = r.java.id;
            connect(row, &HoverRow::activated, this, [this, id]() { emit showInstanceRequested(id); });
            lay->addWidget(row);
        }
        int slotWidth = 0;
        for (QWidget* w : std::as_const(actionSlots))
            if (qobject_cast<QPushButton*>(w))
                slotWidth = std::max(slotWidth, w->sizeHint().width());
        for (QWidget* w : std::as_const(actionSlots)) {
            if (slotWidth == 0)
                w->hide();
            else
                w->setFixedWidth(slotWidth);
        }
        page->addWidget(card);
    }

    // ----- runtimes -----
    {
        QVBoxLayout* lay = nullptr;
        auto* card = makeSection(tr("Runtimes"), host, &lay);
        lay->setSpacing(Tokens::spaceXs);
        if (m_runtimes.isEmpty()) {
            lay->addWidget(makeLabel(tr("No Java found on this PC."), Role::Label, card));
            auto* cap = makeLabel(tr("Turn on automatic Java downloads, or install Java and press Rescan."), Role::Caption, card);
            cap->setWordWrap(true);
            lay->addWidget(cap);
        }
        QHash<QString, int> usedBy;
        for (const Row& r : std::as_const(m_rows))
            if (r.diagnosis.effective)
                usedBy[normalizedPath(r.diagnosis.effective->path)] += 1;
        for (const Runtime& rt : std::as_const(m_runtimes)) {
            auto* row = new QWidget(card);
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(Tokens::spaceSm, Tokens::spaceSm, Tokens::spaceSm, Tokens::spaceSm);
            h->setSpacing(Tokens::spaceMd);

            auto* texts = new QVBoxLayout;
            texts->setContentsMargins(0, 0, 0, 0);
            texts->setSpacing(0);
            texts->addWidget(makeElided(javaName(rt), Role::Label, row));
            texts->addWidget(makeElided(QDir::toNativeSeparators(rt.path), Role::Caption, row));
            h->addLayout(texts, 1);

            const int used = usedBy.value(normalizedPath(rt.path));
            if (used > 0) {
                auto* usedLabel = makeLabel(used == 1 ? tr("Used by 1 instance") : tr("Used by %1 instances").arg(used), Role::Caption, row);
                h->addWidget(usedLabel);
            }
            h->addWidget(new PillTag(rt.is64bit ? tr("64-bit") : tr("32-bit"), PillTag::Tone::Plain, row));
            if (rt.managed)
                h->addWidget(new PillTag(tr("Downloaded by ZERO"), PillTag::Tone::Info, row));
            auto* open = makeButton(tr("Open folder"), QStringLiteral("folder-open"), "ghost", row);
            const QString folder = runtimeFolder(rt.path);
            connect(open, &QPushButton::clicked, this, [folder]() { openFolder(folder); });
            h->addWidget(open);
            lay->addWidget(row);
        }
        page->addWidget(card);
    }

    page->addStretch(1);
    // The old content may hold the button whose click led here: delete it later, and keep the scroll position.
    const int scrollValue = m_scroll->verticalScrollBar()->value();
    if (QWidget* old = m_scroll->takeWidget())
        old->deleteLater();
    m_scroll->setWidget(m_host);
    if (scrollValue > 0)
        QTimer::singleShot(0, m_scroll, [scroll = m_scroll, scrollValue]() { scroll->verticalScrollBar()->setValue(scrollValue); });
}

void JavaDoctorView::askUseJava(const QString& instanceId, const Runtime& runtime)
{
    QWidget* host = window();
    auto* inst = APPLICATION->instances()->getInstanceById(instanceId);
    if (!inst)
        return;
    if (isBusy(instanceId)) {
        ZeroToast::show(host, tr("Close %1 before changing its Java").arg(inst->name()), ZeroToast::Kind::Warning);
        return;
    }

    QMessageBox box(QMessageBox::Question, tr("Change Java?"), tr("Use %1 for %2?").arg(javaName(runtime), inst->name()), QMessageBox::NoButton,
                    host);
    box.setTextFormat(Qt::PlainText);
    box.setInformativeText(tr("%1\n\nThis only changes this instance's Java setting. You can change it back in the instance settings.")
                               .arg(QDir::toNativeSeparators(runtime.path)));
    QPushButton* confirm = box.addButton(tr("Use this Java"), QMessageBox::AcceptRole);
    QPushButton* cancel = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.exec();
    if (box.clickedButton() != confirm)
        return;

    // The game may have been started while the dialog was open.
    if (isBusy(instanceId)) {
        ZeroToast::show(host, tr("Close %1 before changing its Java").arg(inst->name()), ZeroToast::Kind::Warning);
        return;
    }

    // Exactly what the instance Java settings page saves for a manually chosen path (JavaSettingsWidget::saveSettings and its
    // "path edited" handler): override on, the path, and the automatic flag off. The stored version / architecture / signature are
    // left alone: the signature includes the path, so the next launch checks the new Java and refreshes them (CheckJava).
    auto* settings = inst->settings();
    settings->set("OverrideJavaLocation", true);
    settings->set("JavaPath", runtime.path);
    settings->set("AutomaticJava", false);

    ZeroToast::show(host, tr("%1 now uses %2").arg(inst->name(), javaName(runtime)), ZeroToast::Kind::Success);
    evaluate();
}

}  // namespace Zero

#include "JavaDoctorView.moc"
