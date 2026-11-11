// SPDX-License-Identifier: GPL-3.0-only
#include "WorldDetailsPanel.h"

#include <QClipboard>
#include <QDateTime>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedLayout>
#include <QToolButton>
#include <QVBoxLayout>
#include "DesktopServices.h"
#include "ElidedLabel.h"
#include "WorldBackupsSection.h"
#include "WorldIcon.h"
#include "ui/zero/ZeroEmptyState.h"
#include "ui/zero/ZeroFormat.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroToast.h"
#include "zero/backup/WorldBackupJobs.h"

namespace Zero {

namespace {
QLabel* makeLabel(const QString& text, Role role, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    Style::applyRole(label, role);
    return label;
}

ElidedLabel* makeValue(QWidget* parent)
{
    auto* label = new ElidedLabel(parent);
    Style::applyRole(label, Role::Body);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return label;
}

QPushButton* makeAction(const QString& text, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    Style::setProperty(button, "zeroVariant", "ghost");
    button->setStyleSheet(QStringLiteral("text-align: left;"));
    button->setIconSize(QSize(16, 16));
    return button;
}
}  // namespace

WorldDetailsPanel::WorldDetailsPanel(QWidget* parent) : ZeroCard(parent)
{
    setHoverEnabled(false);
    setFixedWidth(kWidth);
    setContentsMargins(0, 0, 0, 0);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(1, 1, 1, 1);
    m_stack = new QStackedLayout;
    outer->addLayout(m_stack);

    m_stack->addWidget(new ZeroEmptyState(QStringLiteral("earth"), tr("Select a world"), tr("Pick a world to see its details."), {}, this));

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

    m_icon = new WorldIconTile(96, 40, content);
    lay->addWidget(m_icon, 0, Qt::AlignHCenter);
    lay->addSpacing(Tokens::spaceMd);

    m_name = new ElidedLabel(content);
    Style::applyRole(m_name, Role::Title);
    m_name->setAlignment(Qt::AlignCenter);
    lay->addWidget(m_name);

    m_folder = new ElidedLabel(content);
    Style::applyRole(m_folder, Role::Caption);
    m_folder->setAlignment(Qt::AlignCenter);
    lay->addSpacing(2);
    lay->addWidget(m_folder);
    lay->addSpacing(Tokens::spaceLg);

    // stats rows (same grid as the Library details panel)
    auto* stats = new QGridLayout;
    stats->setContentsMargins(0, 0, 0, 0);
    stats->setHorizontalSpacing(Tokens::spaceMd);
    stats->setVerticalSpacing(Tokens::spaceSm);
    stats->setColumnStretch(1, 1);

    m_instance = makeValue(content);
    m_mode = makeValue(content);
    m_version = makeValue(content);
    // the seed is monospace in every theme: no role (a role would put the theme font back on every theme change)
    m_seed = new ElidedLabel(content);
    m_seed->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPixelSize(Tokens::body.px);
    m_seed->setFont(mono);
    m_copySeed = new QToolButton(content);
    m_copySeed->setAutoRaise(false);
    m_copySeed->setFixedSize(22, 22);
    m_copySeed->setIconSize(QSize(14, 14));
    m_copySeed->setStyleSheet(QStringLiteral("QToolButton { padding: 0px; }"));  // the ghost padding would squeeze the icon
    m_copySeed->setCursor(Qt::PointingHandCursor);
    m_copySeed->setToolTip(tr("Copy seed"));
    Style::setProperty(m_copySeed, "zeroVariant", "ghost");
    connect(m_copySeed, &QToolButton::clicked, this, &WorldDetailsPanel::copySeed);
    auto* seedCell = new QWidget(content);
    auto* seedRow = new QHBoxLayout(seedCell);
    seedRow->setContentsMargins(0, 0, 0, 0);
    seedRow->setSpacing(Tokens::spaceXs);
    seedRow->addWidget(m_seed, 1);
    seedRow->addWidget(m_copySeed);
    m_size = makeLabel({}, Role::Body, content);
    m_lastPlayed = makeLabel({}, Role::Body, content);

    const struct {
        QString caption;
        QWidget* value;
    } rows[] = { { tr("Instance"), m_instance }, { tr("Game mode"), m_mode }, { tr("Version"), m_version },
                 { tr("Seed"), seedCell },       { tr("Size"), m_size },      { tr("Last played"), m_lastPlayed } };
    int row = 0;
    for (const auto& r : rows) {
        stats->addWidget(makeLabel(r.caption, Role::Caption, content), row, 0, Qt::AlignVCenter);
        if (auto* label = qobject_cast<QLabel*>(r.value)) {
            label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        }
        stats->addWidget(r.value, row, 1);
        ++row;
    }
    lay->addLayout(stats);
    lay->addSpacing(Tokens::spaceLg);

    // backups
    m_backups = new WorldBackupsSection(content);
    lay->addWidget(m_backups);
    lay->addSpacing(Tokens::spaceLg);

    // actions
    m_openFolder = makeAction(tr("Open folder"), content);
    connect(m_openFolder, &QPushButton::clicked, this, [this]() {
        if (hasWorld())
            DesktopServices::openPath(QFileInfo(m_entry.folderPath));
    });
    lay->addWidget(m_openFolder);
    m_openBackups = makeAction(tr("Open backups folder"), content);
    m_openBackups->setVisible(false);  // only while the folder exists
    connect(m_openBackups, &QPushButton::clicked, this, [this]() {
        if (hasWorld() && m_jobs)
            DesktopServices::openPath(QFileInfo(m_jobs->backupDir(m_entry)));
    });
    connect(m_backups, &WorldBackupsSection::folderStateChanged, m_openBackups, &QWidget::setVisible);
    lay->addWidget(m_openBackups);
    m_showInstance = makeAction(tr("Show instance in Library"), content);
    connect(m_showInstance, &QPushButton::clicked, this, [this]() {
        if (hasWorld())
            emit showInstanceRequested(m_entry.instanceId);
    });
    lay->addWidget(m_showInstance);
    lay->addStretch(1);

    connect(&Theme::instance(), &Theme::changed, this, &WorldDetailsPanel::refreshIcons);
    refreshIcons();
    clear();
}

void WorldDetailsPanel::refreshIcons()
{
    const auto& t = Theme::current();
    m_openFolder->setIcon(Icons::icon(QStringLiteral("folder-open"), t.textSecondary));
    m_openBackups->setIcon(Icons::icon(QStringLiteral("folder-open"), t.textSecondary));
    m_showInstance->setIcon(Icons::icon(QStringLiteral("library-big"), t.textSecondary));
    m_copySeed->setIcon(Icons::icon(QStringLiteral("copy"), t.textSecondary));
}

void WorldDetailsPanel::setBackupJobs(Backup::WorldBackupJobs* jobs)
{
    m_jobs = jobs;
    m_backups->setJobs(jobs);
}

void WorldDetailsPanel::setInstanceRunning(bool running)
{
    m_backups->setInstanceRunning(running);
}

void WorldDetailsPanel::clear()
{
    m_entry = {};
    m_backups->clear();
    m_openBackups->setVisible(false);
    m_stack->setCurrentIndex(0);
}

void WorldDetailsPanel::setWorld(const Worlds::WorldEntry& entry)
{
    const bool otherWorld = entry.key() != m_entry.key();
    m_entry = entry;
    if (otherWorld)
        m_openBackups->setVisible(false);  // until the listing of this world says its folder exists
    m_backups->setWorld(entry);
    m_icon->setIconPath(entry.iconPath);
    m_name->setFullText(entry.name);
    m_folder->setFullText(entry.folderName);
    m_instance->setFullText(entry.instanceName);
    QString mode = entry.gameMode.isEmpty() ? tr("Unknown") : entry.gameMode;
    if (entry.hardcore)
        mode += tr(" · Hardcore");
    m_mode->setFullText(mode);
    m_version->setFullText(entry.versionName.isEmpty() ? tr("Unknown") : entry.versionName);
    const bool hasSeed = entry.seed != 0;  // World reports 0 when level.dat holds no seed
    m_seed->setFullText(hasSeed ? QString::number(entry.seed) : tr("Unknown"));
    m_copySeed->setVisible(hasSeed);
    m_size->setText(entry.sizeBytes >= 0 ? Format::bytes(entry.sizeBytes) : tr("Calculating…"));
    m_lastPlayed->setText(Format::relativeTime(entry.lastPlayedMs));
    m_lastPlayed->setToolTip(entry.lastPlayedMs > 0 ? QLocale().toString(QDateTime::fromMSecsSinceEpoch(entry.lastPlayedMs), QLocale::ShortFormat)
                                                    : QString());
    m_stack->setCurrentIndex(1);
}

void WorldDetailsPanel::copySeed()
{
    if (!hasWorld() || m_entry.seed == 0)
        return;
    QGuiApplication::clipboard()->setText(QString::number(m_entry.seed));
    ZeroToast::show(window(), tr("Seed copied"), ZeroToast::Kind::Success);
}

}  // namespace Zero
