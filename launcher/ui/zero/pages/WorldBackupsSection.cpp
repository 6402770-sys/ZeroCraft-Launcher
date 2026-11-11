// SPDX-License-Identifier: GPL-3.0-only
#include "WorldBackupsSection.h"

#include <QCheckBox>
#include <QDateTime>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include "Application.h"
#include "ElidedLabel.h"
#include "ui/zero/ZeroFormat.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "settings/SettingsObject.h"
#include "ui/zero/ZeroToast.h"
#include "zero/backup/WorldBackupJobs.h"

namespace Zero {

namespace {

QString fullDate(qint64 ms)
{
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(ms);
    const QLocale locale;
    return locale.toString(dt.date(), QLocale::LongFormat) + QLatin1Char(' ') + locale.toString(dt.time(), QLocale::ShortFormat);
}

/// Small bordered label in the style of the badges of the world rows ("Safety", "Damaged").
class PillTag : public QWidget {
   public:
    PillTag(const QString& text, bool danger, QWidget* parent) : QWidget(parent), m_text(text), m_danger(danger)
    {
        setFixedHeight(Tokens::badgeHeight);
        connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    }
    QSize sizeHint() const override
    {
        return { QFontMetrics(Style::font(Role::Caption)).horizontalAdvance(m_text) + 2 * Tokens::spaceSm, Tokens::badgeHeight };
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(t.border, 1));
        p.setBrush(t.surface);
        p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusSm));
        p.setFont(Style::font(Role::Caption));
        p.setPen(m_danger ? t.danger : t.textSecondary);
        p.drawText(rect(), Qt::AlignCenter, m_text);
    }

   private:
    QString m_text;
    bool m_danger;
};

/// One backup: date + size on the left, pill, then the two icon buttons. A hairline under every row but the last.
class BackupRowWidget : public QWidget {
   public:
    BackupRowWidget(const Backup::BackupInfo& info, bool last, QWidget* parent) : QWidget(parent), m_last(last)
    {
        setFixedHeight(WorldBackupsSection::kRowHeight);
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(Tokens::spaceXs);

        auto* text = new QVBoxLayout;
        text->setContentsMargins(0, 0, 0, 0);
        text->setSpacing(0);
        auto* date = new ElidedLabel(this);
        Style::applyRole(date, Role::Label);
        date->setFullText(Format::relativeTime(info.createdMs));
        date->setToolTip(fullDate(info.createdMs));
        auto* size = new ElidedLabel(this);
        Style::applyRole(size, Role::Caption);
        size->setFullText(Format::bytes(info.fileSize));
        text->addStretch(1);
        text->addWidget(date);
        text->addWidget(size);
        text->addStretch(1);
        row->addLayout(text, 1);

        if (!info.valid)
            row->addWidget(new PillTag(QObject::tr("Damaged"), true, this));
        else if (info.kind == Backup::Kind::Safety)
            row->addWidget(new PillTag(QObject::tr("Safety"), false, this));
        else if (info.kind == Backup::Kind::Auto)
            row->addWidget(new PillTag(QObject::tr("Auto"), false, this));
        row->addSpacing(Tokens::spaceXs);

        restore = makeButton(QObject::tr("Restore this backup"));
        restore->setVisible(info.valid);
        remove = makeButton(QObject::tr("Delete this backup"));
        row->addWidget(restore);
        row->addWidget(remove);
    }

    QToolButton* restore = nullptr;
    QToolButton* remove = nullptr;

   protected:
    void paintEvent(QPaintEvent*) override
    {
        if (m_last)
            return;
        QPainter p(this);
        p.setPen(Theme::current().border);
        p.drawLine(0, height() - 1, width(), height() - 1);
    }

   private:
    QToolButton* makeButton(const QString& tip)
    {
        auto* button = new QToolButton(this);
        button->setAutoRaise(false);
        button->setFixedSize(28, 28);
        button->setIconSize(QSize(16, 16));
        button->setStyleSheet(QStringLiteral("QToolButton { padding: 0px; }"));  // the ghost padding would squeeze the icon
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(tip);
        button->setAccessibleName(tip);
        Style::setProperty(button, "zeroVariant", "ghost");
        return button;
    }
    bool m_last;
};

}  // namespace

WorldBackupsSection::WorldBackupsSection(QWidget* parent) : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(Tokens::spaceSm);

    auto* header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    auto* title = new QLabel(tr("Backups"), this);
    Style::applyRole(title, Role::Overline);
    m_count = new QLabel(this);
    Style::applyRole(m_count, Role::Caption);
    Style::setProperty(m_count, "zeroTone", "tertiary");
    m_count->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    header->addWidget(title);
    header->addStretch(1);
    header->addWidget(m_count);
    lay->addLayout(header);

    m_backup = new QPushButton(tr("Back up now"), this);
    Style::setProperty(m_backup, "zeroVariant", "secondary");
    m_backup->setIconSize(QSize(16, 16));
    connect(m_backup, &QPushButton::clicked, this, [this]() {
        if (m_jobs && !m_entry.folderPath.isEmpty())
            m_jobs->backup(m_entry);
    });
    lay->addWidget(m_backup);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_listHost = new QWidget;
    m_listLayout = new QVBoxLayout(m_listHost);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(0);
    m_scroll->setWidget(m_listHost);
    m_listHost->setAutoFillBackground(false);
    m_scroll->viewport()->setAutoFillBackground(false);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
    m_scroll->hide();
    lay->addWidget(m_scroll);

    // global setting (all instances): see Zero::Backup::AutoBackup
    auto* autoBackup = new QCheckBox(tr("Back up automatically after playing"), this);
    autoBackup->setToolTip(tr("Applies to all instances. Backs up the worlds you played when the game closes and keeps the 5 newest "
                              "automatic backups per world. Worlds over 2 GB are skipped."));
    autoBackup->setChecked(APPLICATION->settings()->get("ZeroAutoBackupWorlds").toBool());
    connect(autoBackup, &QCheckBox::toggled, this,
            [](bool on) { APPLICATION->settings()->set("ZeroAutoBackupWorlds", on); });
    lay->addWidget(autoBackup);

    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        refreshIcons();
        rebuildRows();
    });
    refreshIcons();
    refreshState();
}

void WorldBackupsSection::setJobs(Backup::WorldBackupJobs* jobs)
{
    if (m_jobs == jobs)
        return;
    m_jobs = jobs;
    if (!m_jobs)
        return;
    connect(m_jobs, &Backup::WorldBackupJobs::backupsListed, this, &WorldBackupsSection::onListed);
    connect(m_jobs, &Backup::WorldBackupJobs::jobStarted, this, [this](const QString& key) {
        if (key == m_entry.key())
            refreshState();
    });
    connect(m_jobs, &Backup::WorldBackupJobs::backupsChanged, this, [this](const QString& key) {
        if (key == m_entry.key()) {
            refreshState();
            requestList();
        }
    });
    connect(m_jobs, &Backup::WorldBackupJobs::jobFinished, this, [this](const QString& key, bool, const QString&) {
        if (key == m_entry.key())
            refreshState();
    });
}

void WorldBackupsSection::refreshIcons()
{
    m_backup->setIcon(Icons::icon(QStringLiteral("archive"), Theme::current().textSecondary));
}

void WorldBackupsSection::clear()
{
    m_entry = {};
    m_listKey.clear();
    m_backups.clear();
    m_listed = false;
    rebuildRows();
    refreshState();
}

void WorldBackupsSection::setWorld(const Worlds::WorldEntry& entry)
{
    const bool changed = entry.key() != m_listKey;
    m_entry = entry;
    if (changed) {
        m_listKey = entry.key();
        m_backups.clear();
        m_listed = false;
        rebuildRows();
        requestList();
    }
    refreshState();
}

void WorldBackupsSection::setInstanceRunning(bool running)
{
    if (m_running == running)
        return;
    m_running = running;
    refreshState();
}

void WorldBackupsSection::requestList()
{
    if (m_jobs && !m_entry.folderPath.isEmpty())
        m_jobs->refreshList(m_entry);
}

void WorldBackupsSection::onListed(const QString& key, const QList<Backup::BackupInfo>& backups)
{
    if (key != m_listKey || m_entry.folderPath.isEmpty())
        return;
    m_backups = backups;
    m_listed = true;
    rebuildRows();
    refreshState();
    emit folderStateChanged(m_jobs && QFileInfo::exists(m_jobs->backupDir(m_entry)));
}

void WorldBackupsSection::rebuildRows()
{
    for (const Row& r : std::as_const(m_rows)) {
        m_listLayout->removeWidget(r.widget);
        r.widget->hide();
        r.widget->deleteLater();
    }
    m_rows.clear();

    for (int i = 0; i < m_backups.size(); ++i) {
        const Backup::BackupInfo& info = m_backups.at(i);
        auto* widget = new BackupRowWidget(info, i == m_backups.size() - 1, m_listHost);
        Row row;
        row.info = info;
        row.widget = widget;
        row.restore = widget->restore;
        row.remove = widget->remove;
        connect(widget->restore, &QToolButton::clicked, this, [this, info]() { askRestore(info); });
        connect(widget->remove, &QToolButton::clicked, this, [this, info]() { askDelete(info); });
        const auto& t = Theme::current();
        widget->restore->setIcon(Icons::icon(QStringLiteral("refresh-cw"), t.textSecondary));
        widget->remove->setIcon(Icons::icon(QStringLiteral("trash-2"), t.textSecondary));
        m_listLayout->addWidget(widget);
        m_rows.append(row);
    }

    const int shown = qMin(int(m_rows.size()), kMaxVisibleRows);
    m_scroll->setFixedHeight(shown * kRowHeight);
    m_scroll->setVisible(!m_rows.isEmpty());
    refreshState();
}

void WorldBackupsSection::refreshState()
{
    const bool has = !m_entry.folderPath.isEmpty();
    const auto op = (m_jobs && has) ? m_jobs->operation(m_entry.key()) : Backup::WorldBackupJobs::Op::None;
    const bool busy = op != Backup::WorldBackupJobs::Op::None;
    using Op = Backup::WorldBackupJobs::Op;

    QString text = tr("Back up now");
    if (op == Op::Backup)
        text = tr("Backing up…");
    else if (op == Op::Restore)
        text = tr("Restoring…");
    else if (op == Op::Remove)
        text = tr("Deleting…");
    m_backup->setText(text);
    m_backup->setEnabled(has && m_jobs && !busy && !m_running);
    m_backup->setToolTip(m_running ? tr("Close the game to back up this world") : QString());

    for (const Row& r : std::as_const(m_rows)) {
        r.restore->setEnabled(!busy && !m_running);
        r.remove->setEnabled(!busy && !m_running);
    }

    if (!m_listed)
        m_count->clear();
    else if (m_backups.isEmpty())
        m_count->setText(tr("No backups yet"));
    else
        m_count->setText(m_backups.size() == 1 ? tr("1 backup") : tr("%1 backups").arg(m_backups.size()));
}

void WorldBackupsSection::askRestore(const Backup::BackupInfo& info)
{
    if (!m_jobs || m_entry.folderPath.isEmpty())
        return;
    QMessageBox box(QMessageBox::Question, tr("Restore world?"),
                    tr("Replace “%1” with the backup from %2? The current world is saved as a safety backup first.")
                        .arg(m_entry.name, fullDate(info.createdMs)),
                    QMessageBox::NoButton, window());
    box.setTextFormat(Qt::PlainText);
    QPushButton* restore = box.addButton(tr("Restore"), QMessageBox::AcceptRole);
    QPushButton* cancel = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.exec();
    if (box.clickedButton() == restore)
        m_jobs->restore(m_entry, info.zipPath);
}

void WorldBackupsSection::askDelete(const Backup::BackupInfo& info)
{
    if (!m_jobs || m_entry.folderPath.isEmpty())
        return;
    QMessageBox box(QMessageBox::Question, tr("Delete backup?"), tr("Delete this backup? It is moved to the Recycle Bin."),
                    QMessageBox::NoButton, window());
    box.setTextFormat(Qt::PlainText);
    QPushButton* remove = box.addButton(tr("Delete"), QMessageBox::DestructiveRole);
    QPushButton* cancel = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.exec();
    if (box.clickedButton() == remove)
        m_jobs->remove(m_entry, info.zipPath);
}

}  // namespace Zero
