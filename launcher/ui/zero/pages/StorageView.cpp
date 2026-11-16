// SPDX-License-Identifier: GPL-3.0-only
#include "StorageView.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QComboBox>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QStackedLayout>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QTimer>
#include <algorithm>
#include <utility>
#include "Application.h"
#include "ElidedLabel.h"
#include "IconTile.h"
#include "InstanceList.h"
#include "icons/IconList.h"
#include "minecraft/MinecraftInstance.h"
#include "settings/SettingsObject.h"
#include "ui/zero/ZeroCard.h"
#include "ui/zero/ZeroEmptyState.h"
#include "ui/zero/ZeroFormat.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroToast.h"
#include "ui/zero/ZeroSkeleton.h"
#include "ui/zero/ZeroMotion.h"
#include "zero/backup/WorldBackupJobs.h"

namespace Zero {

using namespace Storage;

namespace {

QPair<QList<InstanceSource>, LauncherSource> sourcesFromApplication()
{
    QList<InstanceSource> out;
    auto* list = APPLICATION->instances();
    for (int i = 0; i < list->count(); ++i) {
        MinecraftInstance* inst = list->at(i);
        InstanceSource s;
        s.id = inst->id();
        s.name = inst->name();
        s.instanceRoot = inst->instanceRoot();
        s.gameRoot = inst->gameRoot();
        s.worldsDir = inst->worldDir();
        s.modsDir = inst->modsRoot();
        s.packDirs = { inst->resourcePacksDir(), inst->texturePacksDir(), inst->shaderPacksDir() };
        s.running = inst->isRunning() || APPLICATION->launchPhase(inst->id()) != Application::LaunchPhase::Idle;
        out.append(s);
    }
    LauncherSource launcher;
    launcher.dataRoot = APPLICATION->dataRoot();
    launcher.instancesDir = QDir(launcher.dataRoot).absoluteFilePath(APPLICATION->settings()->get("InstanceDir").toString());
    return { out, launcher };
}

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
        auto apply = [button, icon]() { button->setIcon(Icons::icon(icon, Theme::current().text)); };
        apply();
        QObject::connect(&Theme::instance(), &Theme::changed, button, apply);
    }
    return button;
}

QToolButton* makeIconButton(const QString& icon, const QString& tip, QWidget* parent)
{
    auto* button = new QToolButton(parent);
    button->setAutoRaise(false);
    button->setFixedSize(28, 28);
    button->setIconSize(QSize(16, 16));
    button->setStyleSheet(QStringLiteral("QToolButton { padding: 0px; }"));  // the ghost padding would squeeze the icon
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(tip);
    button->setAccessibleName(tip);
    Style::setProperty(button, "zeroVariant", "ghost");
    auto apply = [button, icon]() { button->setIcon(Icons::icon(icon, Theme::current().text)); };
    apply();
    QObject::connect(&Theme::instance(), &Theme::changed, button, apply);
    return button;
}

}  // namespace

/// Thin usage bar: `border` track, `accent` fill covering `ratio` of the width.
class Track : public QWidget {
   public:
    void setRatio(double ratio)
    {
        m_ratio = std::clamp(ratio, 0.0, 1.0);
        m_grow = 1.0;
        m_grown = true;
        update();
    }
    Track(double ratio, QWidget* parent) : QWidget(parent), m_ratio(std::clamp(ratio, 0.0, 1.0))
    {
        setFixedHeight(Tokens::progressHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        const double r = height() / 2.0;
        p.setBrush(t.border);
        p.drawRoundedRect(QRectF(rect()), r, r);
        const double shown = m_ratio * m_grow;
        if (shown > 0) {
            p.setBrush(t.accent);
            p.drawRoundedRect(QRectF(0, 0, std::max<double>(height(), width() * shown), height()), r, r);
        }
    }

    void showEvent(QShowEvent* event) override
    {
        QWidget::showEvent(event);
        // The first time a bar shows it fills from the left; bars further down start a little later.
        if (m_grown || !Motion::enabled() || m_ratio <= 0)
            return;
        m_grown = true;
        m_grow = 0.0;
        auto* anim = new QVariantAnimation(this);
        anim->setDuration(Tokens::durReveal * 2);
        anim->setEasingCurve(Motion::easeGlide());
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            m_grow = v.toReal();
            update();
        });
        const int delay = std::clamp(mapTo(window(), QPoint(0, 0)).y() / 4, 0, 300);
        QTimer::singleShot(delay, anim, [anim]() { anim->start(QAbstractAnimation::DeleteWhenStopped); });
    }

   private:
    double m_ratio;
    double m_grow = 1.0;
    bool m_grown = false;
};

namespace {

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

QString percentText(qint64 bytes, qint64 total)
{
    if (total <= 0)
        return {};
    const double share = double(bytes) * 100.0 / double(total);
    return share < 1.0 ? QObject::tr("<1%") : QObject::tr("%1%").arg(int(share + 0.5));
}

struct CategoryInfo {
    QString name;
    QString description;
};

CategoryInfo categoryInfo(Category c)
{
    switch (c) {
        case Category::Worlds: return { QObject::tr("Worlds"), QObject::tr("Singleplayer saves in your instances") };
        case Category::Mods: return { QObject::tr("Mods"), QObject::tr("Mod files") };
        case Category::Packs: return { QObject::tr("Resource & shader packs"), QObject::tr("Resource, texture and shader packs") };
        case Category::Screenshots: return { QObject::tr("Screenshots"), QObject::tr("Screenshots taken in game") };
        case Category::Logs: return { QObject::tr("Logs & crash reports"), QObject::tr("Game logs and crash reports") };
        case Category::OtherInstance:
            return { QObject::tr("Other instance files"), QObject::tr("Configs, game versions and everything else inside instances") };
        case Category::Backups: return { QObject::tr("World backups"), QObject::tr("Smart Backup archives") };
        case Category::Libraries:
            return { QObject::tr("Game libraries & assets"), QObject::tr("Shared Minecraft files, downloaded again when needed") };
        case Category::Java: return { QObject::tr("Java runtimes"), QObject::tr("Runtimes downloaded by the launcher") };
        case Category::Cache: return { QObject::tr("Launcher cache"), QObject::tr("Metadata and download cache") };
        case Category::OtherLauncher:
            return { QObject::tr("Other launcher data"), QObject::tr("Settings, icons, themes and launcher logs") };
    }
    return {};
}

/// Short name of an instance category for the per-instance caption ("Logs", not "Logs & crash reports").
QString shortCategoryName(Category c)
{
    switch (c) {
        case Category::Packs: return QObject::tr("Packs");
        case Category::Logs: return QObject::tr("Logs");
        case Category::OtherInstance: return QObject::tr("Other");
        default: return categoryInfo(c).name;
    }
}

void openFolder(const QString& path)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

/// Explorer with `path` selected (a missing path opens its parent folder).
void revealInExplorer(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        openFolder(info.absolutePath());
        return;
    }
    QProcess process;
    process.setProgram(QStringLiteral("explorer.exe"));
    process.setNativeArguments(QStringLiteral("/select,\"%1\"").arg(QDir::toNativeSeparators(info.absoluteFilePath())));
    process.startDetached();
}

bool instanceBusy(const QString& instanceId)
{
    auto* inst = APPLICATION->instances()->getInstanceById(instanceId);
    return inst && (inst->isRunning() || APPLICATION->launchPhase(instanceId) != Application::LaunchPhase::Idle);
}

QString itemKindName(ItemKind kind)
{
    switch (kind) {
        case ItemKind::World: return QObject::tr("World");
        case ItemKind::Mod: return QObject::tr("Mod");
        case ItemKind::Pack: return QObject::tr("Resource or shader pack");
        case ItemKind::Logs: return QObject::tr("Logs folder");
        case ItemKind::CrashReports: return QObject::tr("Crash reports folder");
        case ItemKind::Screenshots: return QObject::tr("Screenshots folder");
        case ItemKind::WorldBackups: return QObject::tr("World backups (ZERO)");
        case ItemKind::Snapshots: return QObject::tr("Modpack snapshots (ZERO)");
        case ItemKind::ZeroData: return QObject::tr("ZERO data");
        case ItemKind::Folder: return QObject::tr("Folder");
        case ItemKind::BigFile: return QObject::tr("Large file");
    }
    return {};
}

QString itemIconName(ItemKind kind)
{
    switch (kind) {
        case ItemKind::World: return QStringLiteral("earth");
        case ItemKind::Mod: return QStringLiteral("puzzle");
        case ItemKind::Pack: return QStringLiteral("package");
        case ItemKind::Logs: return QStringLiteral("scroll-text");
        case ItemKind::CrashReports: return QStringLiteral("triangle-alert");
        case ItemKind::Screenshots: return QStringLiteral("image");
        case ItemKind::WorldBackups: return QStringLiteral("archive");
        case ItemKind::Snapshots: return QStringLiteral("file-archive");
        case ItemKind::ZeroData: return QStringLiteral("layers");
        case ItemKind::Folder: return QStringLiteral("folder-open");
        case ItemKind::BigFile: return QStringLiteral("file-text");
    }
    return QStringLiteral("file-text");
}

/// A section card with a Heading title; returns the card and its vertical layout.
ZeroCard* makeSection(const QString& title, QWidget* parent, QVBoxLayout** layoutOut, const QString& subtitle = {})
{
    auto* card = new ZeroCard(parent);
    card->setHoverEnabled(false);
    auto* lay = new QVBoxLayout(card);
    lay->setContentsMargins(Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg);
    lay->setSpacing(Tokens::spaceMd);
    lay->addWidget(makeLabel(title, Role::Heading, card));
    if (!subtitle.isEmpty()) {
        auto* sub = makeLabel(subtitle, Role::Caption, card);
        sub->setWordWrap(true);
        lay->addWidget(sub);
    }
    *layoutOut = lay;
    return card;
}

}  // namespace

StorageView::StorageView(QWidget* parent)
    : QWidget(parent), m_scanner(new StorageScanner(&sourcesFromApplication, this))
{
    qRegisterMetaType<Zero::Storage::StorageReport>();
    qRegisterMetaType<Zero::Storage::TrashResult>();

    m_controls = new QWidget(this);
    auto* controlsRow = new QHBoxLayout(m_controls);
    controlsRow->setContentsMargins(0, 0, 0, 0);
    m_rescan = makeButton(tr("Rescan"), QStringLiteral("refresh-cw"), "ghost", m_controls);
    m_rescan->setToolTip(tr("Measure disk usage again"));
    controlsRow->addWidget(m_rescan);
    m_controls->hide();
    connect(m_rescan, &QPushButton::clicked, m_scanner, &StorageScanner::rescan);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    m_stack = new QStackedLayout;
    outer->addLayout(m_stack, 1);

    auto* loading = new QWidget(this);
    auto* loadingLayout = new QVBoxLayout(loading);
    loadingLayout->setContentsMargins(0, 0, Tokens::spaceSm, 0);
    loadingLayout->setSpacing(Tokens::spaceSm);
    m_loadingText = makeLabel(tr("Measuring disk usage…"), Role::Label, loading);
    m_loadingTrack = new Track(0.0, loading);
    loadingLayout->addWidget(m_loadingText);
    loadingLayout->addWidget(m_loadingTrack);
    loadingLayout->addSpacing(Tokens::spaceMd);
    loadingLayout->addWidget(new ZeroSkeleton(ZeroSkeleton::Shape::Report, tr("Measuring disk usage… Large worlds can take a moment."), loading), 1);
    m_stack->addWidget(loading);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_stack->addWidget(m_scroll);

    connect(m_scanner, &StorageScanner::scanStarted, this, [this]() {
        if (m_scanner->hasReport()) {
            if (m_updating) {
                m_updating->setText(tr("Updating…"));
                m_updating->show();
            }
        } else {
            m_loadingText->setText(tr("Measuring disk usage…"));
            m_loadingTrack->setRatio(0.0);
            m_stack->setCurrentIndex(0);
        }
        updateBusy();
    });
    connect(m_scanner, &StorageScanner::scanProgress, this, &StorageView::showProgress);
    connect(m_scanner, &StorageScanner::reportReady, this, [this](const StorageReport& report) {
        m_report = report;
        rebuild(report);
        m_stack->setCurrentIndex(1);
        updateBusy();
    });
    connect(m_scanner, &StorageScanner::trashFinished, this, &StorageView::onTrashFinished);
}

QString StorageView::description()
{
    return tr("Disk space used by your instances, worlds, backups and launcher data");
}

void StorageView::open()
{
    if (!m_scanner->isScanning())
        m_scanner->rescan();
    updateBusy();
}

void StorageView::scrollToEnd()
{
    QApplication::processEvents();
    m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum());
}

void StorageView::updateBusy()
{
    const bool busy = m_scanner->isScanning() || m_scanner->isTrashing();
    m_rescan->setEnabled(!busy);
    for (auto* b : m_cleanupButtons)
        b->setEnabled(!busy);
    for (QWidget* b : std::as_const(m_itemButtons))
        b->setEnabled(!busy && !b->property("zeroLocked").toBool());
}

void StorageView::showProgress(int done, int total, const QString& current)
{
    if (total <= 0)
        return;
    const int step = std::min(done + 1, total);
    const QString text = current.isEmpty() ? tr("Measuring launcher data… (%1 of %2)").arg(step).arg(total)
                                           : tr("Measuring %1 (%2 of %3)").arg(current).arg(step).arg(total);
    m_loadingText->setText(text);
    m_loadingTrack->setRatio(double(done) / double(total));
    if (m_updating) {
        m_updating->setText(tr("Updating… %1 of %2").arg(step).arg(total));
        m_updating->show();
    }
}

void StorageView::onTrashFinished(const TrashResult& result)
{
    QWidget* host = window();
    const QString itemName = std::exchange(m_pendingItemName, QString());
    if (!result.error.isEmpty()) {
        ZeroToast::show(host, result.error, ZeroToast::Kind::Error);
    } else if (!itemName.isEmpty()) {
        if (result.trashedCount > 0)
            ZeroToast::show(host, tr("Moved %1 to the Recycle Bin. Freed %2.").arg(itemName, Format::bytes(result.trashedBytes)), ZeroToast::Kind::Success);
        else if (!result.problems.isEmpty() && result.problems.first().error.contains(QLatin1String("running")))
            ZeroToast::show(host, tr("Close the game first"), ZeroToast::Kind::Warning);
        else
            ZeroToast::show(host, tr("%1 could not be moved to the Recycle Bin").arg(itemName), ZeroToast::Kind::Warning);
    } else {
        if (result.trashedCount > 0)
            ZeroToast::show(host, (result.trashedCount == 1 ? tr("Moved 1 file (%1) to the Recycle Bin").arg(Format::bytes(result.trashedBytes))
                                                : tr("Moved %1 files (%2) to the Recycle Bin").arg(result.trashedCount).arg(Format::bytes(result.trashedBytes))),
                            ZeroToast::Kind::Success);
        const int problems = result.failedCount + result.skippedCount;
        if (problems > 0)
            ZeroToast::show(host, (problems == 1 ? tr("1 file could not be moved") : tr("%1 files could not be moved").arg(problems)), ZeroToast::Kind::Warning);
    }
    m_scanner->rescan();
    updateBusy();
}

void StorageView::askTrash(const CleanupGroup& group)
{
    if (group.files.isEmpty())
        return;
    QSet<QString> instances;
    for (const auto& f : group.files)
        instances.insert(f.instanceId);
    const int n = group.files.size();
    const QString size = Format::bytes(group.totalBytes);
    const QString text = group.kind == CleanupKind::OldLogs ? (n == 1 ? tr("Move 1 old log file (%1) to the Recycle Bin?").arg(size) : tr("Move %1 old log files (%2) to the Recycle Bin?").arg(n).arg(size))
                                                            : (n == 1 ? tr("Move 1 old crash report (%1) to the Recycle Bin?").arg(size) : tr("Move %1 old crash reports (%2) to the Recycle Bin?").arg(n).arg(size));
    QMessageBox box(QMessageBox::Question, tr("Move to Recycle Bin?"), text, QMessageBox::NoButton, window());
    box.setTextFormat(Qt::PlainText);
    box.setInformativeText((instances.size() == 1 ? tr("From 1 instance. You can restore them from the Recycle Bin.")
                                                   : tr("From %1 instances. You can restore them from the Recycle Bin.").arg(instances.size())));
    QPushButton* confirm = box.addButton(tr("Move to Recycle Bin"), QMessageBox::AcceptRole);
    QPushButton* cancel = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.exec();
    if (box.clickedButton() == confirm) {
        m_scanner->trash(group.files);
        updateBusy();
    }
}

void StorageView::askTrashItem(const LargeItem& item)
{
    if (instanceBusy(item.instanceId)) {
        ZeroToast::show(window(), tr("Close the game first"), ZeroToast::Kind::Warning);
        return;
    }
    QMessageBox box(QMessageBox::Question, tr("Move to Recycle Bin?"),
                    tr("Move \"%1\" (%2) from %3 to the Recycle Bin?").arg(item.name, Format::bytes(item.sizeBytes), item.instanceName), QMessageBox::NoButton, window());
    box.setTextFormat(Qt::PlainText);
    box.setInformativeText(item.kind == ItemKind::World
                               ? tr("This world will be gone from the game (you can get it back from the Recycle Bin).")
                               : tr("You can restore it from the Recycle Bin."));
    QPushButton* confirm = box.addButton(tr("Move to Recycle Bin"), QMessageBox::AcceptRole);
    QPushButton* cancel = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.exec();
    if (box.clickedButton() == confirm) {
        m_pendingItemName = item.name;
        m_scanner->trashItems({ item });
        updateBusy();
    }
}

void StorageView::fillLargest()
{
    if (!m_largestHost)
        return;
    auto* lay = qobject_cast<QVBoxLayout*>(m_largestHost->layout());
    m_itemButtons.erase(std::remove_if(m_itemButtons.begin(), m_itemButtons.end(), [this](QWidget* w) { return m_largestHost->isAncestorOf(w); }),
                        m_itemButtons.end());
    while (QLayoutItem* old = lay->takeAt(0)) {
        if (QWidget* w = old->widget())
            w->deleteLater();
        delete old;
    }
    const QList<LargeItem> items = aggregateLargest(m_report.largest, m_largestInstance, kLargestLimit);
    if (items.isEmpty()) {
        lay->addWidget(makeLabel(tr("Nothing big found."), Role::Caption, m_largestHost));
        return;
    }
    const qint64 biggest = items.first().sizeBytes;
    for (const LargeItem& item : items) {
        auto* row = new QWidget(m_largestHost);
        auto* col = new QVBoxLayout(row);
        col->setContentsMargins(0, Tokens::spaceXs, 0, Tokens::spaceXs);
        col->setSpacing(Tokens::spaceXs);
        auto* h = new QHBoxLayout;
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(Tokens::spaceMd);
        h->addWidget(makeIconLabel(itemIconName(item.kind), 20, row, &ThemeSpec::textSecondary));
        auto* texts = new QVBoxLayout;
        texts->setContentsMargins(0, 0, 0, 0);
        texts->setSpacing(0);
        texts->addWidget(makeElided(item.name, Role::Label, row));
        texts->addWidget(makeElided(QStringLiteral("%1 · %2").arg(item.instanceName, itemKindName(item.kind)), Role::Caption, row));
        h->addLayout(texts, 1);
        auto* size = makeLabel(Format::bytes(item.sizeBytes), Role::Label, row);
        size->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        h->addWidget(size);

        const QString path = item.path;
        auto* show = makeIconButton(QStringLiteral("folder-open"), tr("Show in folder"), row);
        connect(show, &QToolButton::clicked, this, [path]() { revealInExplorer(path); });
        h->addWidget(show);
        if (item.kind == ItemKind::WorldBackups) {
            auto* manage = makeButton(tr("Manage backups"), QString(), "ghost", row);
            connect(manage, &QPushButton::clicked, this, &StorageView::showWorldsRequested);
            h->addWidget(manage);
        }
        if (isTrashableKind(item.kind)) {
            auto* trash = makeButton(tr("Move to Recycle Bin"), QStringLiteral("trash-2"), "secondary", row);
            const bool locked = item.running || instanceBusy(item.instanceId);
            trash->setProperty("zeroLocked", locked);
            trash->setEnabled(!locked && !m_scanner->isScanning() && !m_scanner->isTrashing());
            if (locked)
                trash->setToolTip(tr("Close the game first"));
            connect(trash, &QPushButton::clicked, this, [this, item]() { askTrashItem(item); });
            m_itemButtons.append(trash);
            h->addWidget(trash);
        } else {
            h->addWidget(makeLabel(tr("Managed by ZERO"), Role::Caption, row));
        }
        col->addLayout(h);
        col->addWidget(new Track(biggest > 0 ? double(item.sizeBytes) / double(biggest) : 0, row));
        lay->addWidget(row);
    }
}

void StorageView::rebuild(const StorageReport& report)
{
    m_cleanupButtons.clear();
    m_itemButtons.clear();
    m_host = new QWidget;
    m_hostLayout = new QVBoxLayout(m_host);
    m_hostLayout->setContentsMargins(0, 0, Tokens::spaceSm, Tokens::spaceXl);
    m_hostLayout->setSpacing(Tokens::spaceLg);
    QWidget* host = m_host;
    QVBoxLayout* page = m_hostLayout;

    // ----- summary -----
    {
        auto* card = new ZeroCard(host);
        card->setHoverEnabled(false);
        auto* row = new QHBoxLayout(card);
        row->setContentsMargins(Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg);
        row->setSpacing(Tokens::spaceXl);

        auto* left = new QVBoxLayout;
        left->setContentsMargins(0, 0, 0, 0);
        left->setSpacing(Tokens::spaceXs);
        left->addWidget(makeLabel(tr("Total"), Role::Overline, card));
        left->addWidget(makeLabel(Format::bytes(report.totalBytes), Role::Display, card));
        left->addWidget(makeElided(tr("Data folder: %1").arg(QDir::toNativeSeparators(report.dataRoot)), Role::Caption, card));
        m_updating = makeLabel(tr("Updating…"), Role::Caption, card);
        m_updating->setVisible(m_scanner->isScanning());
        left->addWidget(m_updating);
        left->addSpacing(Tokens::spaceXs);
        auto* open = makeButton(tr("Open folder"), QStringLiteral("folder-open"), "ghost", card);
        const QString root = report.dataRoot;
        connect(open, &QPushButton::clicked, this, [root]() { openFolder(root); });
        left->addWidget(open, 0, Qt::AlignLeft);
        row->addLayout(left, 1);

        if (report.volumeTotalBytes > 0 && report.volumeFreeBytes >= 0) {
            auto* right = new QVBoxLayout;
            right->setContentsMargins(0, 0, 0, 0);
            right->setSpacing(Tokens::spaceXs);
            right->addWidget(makeLabel(tr("Free on this drive"), Role::Caption, card));
            right->addWidget(makeLabel(tr("%1 free of %2").arg(Format::bytes(report.volumeFreeBytes), Format::bytes(report.volumeTotalBytes)), Role::Label, card));
            right->addSpacing(Tokens::spaceXs);
            const qint64 used = std::max<qint64>(0, report.volumeTotalBytes - report.volumeFreeBytes);
            right->addWidget(new Track(double(used) / double(report.volumeTotalBytes), card));
            right->addStretch(1);
            auto* rightHost = new QWidget(card);
            rightHost->setFixedWidth(280);
            rightHost->setLayout(right);
            row->addWidget(rightHost, 0, Qt::AlignTop);
        }
        page->addWidget(card);
    }

    // ----- where the space goes -----
    {
        QVBoxLayout* lay = nullptr;
        auto* card = makeSection(tr("Where the space goes"), host, &lay);
        QList<Category> cats;
        for (Category c : allCategories())
            if (report.totals.of(c) > 0)
                cats.append(c);
        std::stable_sort(cats.begin(), cats.end(), [&report](Category a, Category b) { return report.totals.of(a) > report.totals.of(b); });
        if (cats.isEmpty())
            lay->addWidget(makeLabel(tr("Nothing measured yet."), Role::Caption, card));
        for (Category c : cats) {
            const qint64 bytes = report.totals.of(c);
            const CategoryInfo info = categoryInfo(c);
            auto* rowHost = new QWidget(card);
            auto* col = new QVBoxLayout(rowHost);
            col->setContentsMargins(0, 0, 0, 0);
            col->setSpacing(Tokens::spaceSm);
            auto* top = new QHBoxLayout;
            top->setContentsMargins(0, 0, 0, 0);
            top->setSpacing(Tokens::spaceMd);
            auto* texts = new QVBoxLayout;
            texts->setContentsMargins(0, 0, 0, 0);
            texts->setSpacing(0);
            texts->addWidget(makeElided(info.name, Role::Label, rowHost));
            texts->addWidget(makeElided(info.description, Role::Caption, rowHost));
            top->addLayout(texts, 1);
            auto* nums = new QVBoxLayout;
            nums->setContentsMargins(0, 0, 0, 0);
            nums->setSpacing(0);
            auto* size = makeLabel(Format::bytes(bytes), Role::Label, rowHost);
            size->setAlignment(Qt::AlignRight);
            auto* pct = makeLabel(percentText(bytes, report.totalBytes), Role::Caption, rowHost);
            pct->setAlignment(Qt::AlignRight);
            nums->addWidget(size);
            nums->addWidget(pct);
            top->addLayout(nums);
            col->addLayout(top);
            col->addWidget(new Track(report.totalBytes > 0 ? double(bytes) / double(report.totalBytes) : 0, rowHost));
            lay->addWidget(rowHost);
        }
        page->addWidget(card);
    }

    // ----- instances -----
    {
        QVBoxLayout* lay = nullptr;
        auto* card = makeSection(tr("Instances"), host, &lay);
        lay->setSpacing(Tokens::spaceXs);
        if (report.instances.isEmpty())
            lay->addWidget(makeLabel(tr("No instances yet."), Role::Caption, card));
        const qint64 largest = report.instances.isEmpty() ? 0 : report.instances.first().total;
        for (const InstanceRow& inst : report.instances) {
            auto* row = new HoverRow(card);
            auto* col = new QVBoxLayout(row);
            col->setContentsMargins(Tokens::spaceSm, Tokens::spaceSm, Tokens::spaceSm, Tokens::spaceSm);
            col->setSpacing(Tokens::spaceSm);
            auto* top = new QHBoxLayout;
            top->setContentsMargins(0, 0, 0, 0);
            top->setSpacing(Tokens::spaceMd);

            auto* tile = new IconTile(36, 24, row);
            if (auto* instance = APPLICATION->instances()->getInstanceById(inst.id))
                tile->setInstanceIcon(APPLICATION->icons()->getIcon(instance->iconKey()));
            top->addWidget(tile);

            QList<Category> cats;
            for (Category c : allCategories())
                if (inst.bytes.of(c) > 0)
                    cats.append(c);
            std::stable_sort(cats.begin(), cats.end(), [&inst](Category a, Category b) { return inst.bytes.of(a) > inst.bytes.of(b); });
            QStringList parts;
            for (int i = 0; i < cats.size() && i < 3; ++i)
                parts << QStringLiteral("%1 %2").arg(shortCategoryName(cats[i]), Format::bytes(inst.bytes.of(cats[i])));
            auto* texts = new QVBoxLayout;
            texts->setContentsMargins(0, 0, 0, 0);
            texts->setSpacing(0);
            texts->addWidget(makeElided(inst.name, Role::Label, row));
            texts->addWidget(makeElided(parts.join(QStringLiteral(" · ")), Role::Caption, row));
            top->addLayout(texts, 1);

            auto* size = makeLabel(Format::bytes(inst.total), Role::Label, row);
            size->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            top->addWidget(size);

            auto* folder = makeIconButton(QStringLiteral("folder-open"), tr("Open instance folder"), row);
            const QString instRoot = inst.instanceRoot;
            connect(folder, &QToolButton::clicked, this, [instRoot]() { openFolder(instRoot); });
            top->addWidget(folder);
            col->addLayout(top);
            col->addWidget(new Track(largest > 0 ? double(inst.total) / double(largest) : 0, row));

            const QString id = inst.id;
            connect(row, &HoverRow::activated, this, [this, id]() { emit showInstanceRequested(id); });
            lay->addWidget(row);
        }
        page->addWidget(card);
    }

    // ----- largest items -----
    {
        QVBoxLayout* lay = nullptr;
        auto* card = makeSection(tr("Largest items"), host, &lay,
                                 tr("The biggest worlds, packs, folders and files across your instances. Moving one to the Recycle Bin asks first."));
        auto* combo = new QComboBox(card);
        combo->addItem(tr("All instances"), QString());
        int selected = 0;
        for (const InstanceRow& inst : report.instances) {
            combo->addItem(inst.name, inst.id);
            if (inst.id == m_largestInstance)
                selected = int(combo->count()) - 1;
        }
        combo->setCurrentIndex(selected);
        if (selected == 0)
            m_largestInstance.clear();
        combo->setAccessibleName(tr("Show the largest items of"));
        lay->addWidget(combo, 0, Qt::AlignLeft);
        m_largestHost = new QWidget(card);
        auto* items = new QVBoxLayout(m_largestHost);
        items->setContentsMargins(0, 0, 0, 0);
        items->setSpacing(Tokens::spaceXs);
        lay->addWidget(m_largestHost);
        connect(combo, &QComboBox::currentIndexChanged, this, [this, combo]() {
            m_largestInstance = combo->currentData().toString();
            fillLargest();
        });
        fillLargest();
        page->addWidget(card);
    }

    // ----- clean up -----
    {
        QVBoxLayout* lay = nullptr;
        auto* card = makeSection(tr("Clean up"), host, &lay,
                                 tr("Only files ZERO can safely remove. Everything goes to the Recycle Bin. Running instances are skipped."));

        auto addRow = [&](const QString& title, const QString& caption, qint64 bytes, QPushButton* button) {
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
            if (bytes >= 0) {
                auto* size = makeLabel(Format::bytes(bytes), Role::Label, row);
                size->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                h->addWidget(size);
            }
            if (button) {
                button->setParent(row);
                h->addWidget(button);
            }
            lay->addWidget(row);
        };

        for (CleanupKind kind : { CleanupKind::OldLogs, CleanupKind::OldCrashReports }) {
            const CleanupGroup* group = report.group(kind);
            const QString title = kind == CleanupKind::OldLogs ? tr("Old logs") : tr("Old crash reports");
            const QString age = kind == CleanupKind::OldLogs ? tr("Logs older than %1 days").arg(kCleanupAgeDays)
                                                             : tr("Crash reports older than %1 days").arg(kCleanupAgeDays);
            if (!group || group->files.isEmpty()) {
                addRow(title, tr("Nothing to clean up"), -1, nullptr);
                continue;
            }
            QSet<QString> instances;
            for (const auto& f : group->files)
                instances.insert(f.instanceId);
            const int nFiles = int(group->files.size()), nInstances = int(instances.size());
            const QString counts = tr("%1 in %2")
                                       .arg(nFiles == 1 ? tr("1 file") : tr("%1 files").arg(nFiles), nInstances == 1 ? tr("1 instance") : tr("%1 instances").arg(nInstances));
            const QString caption = QStringLiteral("%1 · %2").arg(age, counts);
            auto* button = makeButton(tr("Move to Recycle Bin…"), QStringLiteral("trash-2"), "secondary", card);
            const CleanupGroup copy = *group;
            connect(button, &QPushButton::clicked, this, [this, copy]() { askTrash(copy); });
            m_cleanupButtons.append(button);
            addRow(title, caption, group->totalBytes, button);
        }

        // World backups: information plus a way to open the folder
        {
            const QString backups = QDir(report.dataRoot).absoluteFilePath(QStringLiteral("zero/backups"));
            auto* button = makeButton(tr("Open folder"), QStringLiteral("folder-open"), "ghost", card);
            button->setEnabled(QFileInfo(backups).isDir());
            connect(button, &QPushButton::clicked, this, [backups]() { openFolder(backups); });
            addRow(tr("World backups"), tr("Kept automatically: 10 manual, 3 safety and 5 automatic per world"), report.totals.of(Category::Backups), button);
        }

        // Interrupted restores: information only, never deletion
        QList<RestoreLeftover> leftovers;
        for (const auto& l : report.leftovers)
            if (!Backup::WorldBackupJobs::isInstanceLocked(l.instanceId))
                leftovers.append(l);
        if (!leftovers.isEmpty()) {
            auto* head = new QHBoxLayout;
            head->setContentsMargins(0, Tokens::spaceSm, 0, 0);
            head->setSpacing(Tokens::spaceMd);
            head->addWidget(makeIconLabel(QStringLiteral("triangle-alert"), 20, card, &ThemeSpec::warning), 0, Qt::AlignTop);
            auto* texts = new QVBoxLayout;
            texts->setContentsMargins(0, 0, 0, 0);
            texts->setSpacing(Tokens::spaceXs);
            texts->addWidget(makeLabel(tr("Interrupted restore folders"), Role::Label, card));
            auto* cap = makeLabel(tr("Left by a restore that did not finish. One may be the only copy of a world: check it before deleting it by hand."),
                                  Role::Caption, card);
            cap->setWordWrap(true);
            texts->addWidget(cap);
            head->addLayout(texts, 1);
            lay->addLayout(head);
            for (const auto& l : leftovers) {
                auto* row = new QHBoxLayout;
                row->setContentsMargins(20 + Tokens::spaceMd, 0, 0, 0);
                row->setSpacing(Tokens::spaceMd);
                row->addWidget(makeElided(QStringLiteral("%1 · %2 · %3").arg(l.instanceName, QFileInfo(l.path).fileName(), Format::bytes(l.sizeBytes)),
                                          Role::Body, card),
                               1);
                auto* show = makeButton(tr("Show in folder"), QStringLiteral("folder-open"), "ghost", card);
                const QString parentDir = QFileInfo(l.path).absolutePath();
                connect(show, &QPushButton::clicked, this, [parentDir]() { openFolder(parentDir); });
                row->addWidget(show);
                lay->addLayout(row);
            }
        }
        page->addWidget(card);
    }

    page->addStretch(1);
    m_scroll->setWidget(m_host);  // deletes the previous content
}

}  // namespace Zero

#include "StorageView.moc"
