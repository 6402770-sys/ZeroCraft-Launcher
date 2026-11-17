// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2022 Jamie Mansfield <jmansfield@cadixdev.org>
 *  Copyright (c) 2022 dada513 <dada513@protonmail.com>
 *  Copyright (C) 2022 Tayou <git@tayou.org>
 *  Copyright (C) 2024 TheKodeToad <TheKodeToad@proton.me>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "LauncherPage.h"
#include "ui_LauncherPage.h"

#include <QDir>
#include <QFileDialog>
#include <QMenuBar>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QTextCharFormat>

#include <FileSystem.h>
#include "Application.h"
#include "BuildConfig.h"
#include "DesktopServices.h"
#include "Json.h"
#include "settings/Setting.h"
#include "settings/SettingsObject.h"
#include "ui/themes/ITheme.h"
#include "ui/themes/ThemeManager.h"
#include "updater/ExternalUpdater.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "ui/zero/ZeroStyle.h"
#include "zero/backup/BackupDestination.h"
#include "zero/backup/ScheduledBackup.h"
#include "zero/backup/WorldBackupJobs.h"
#include "zero/session/WhilePlaying.h"

#include <QApplication>
#include <QProcess>

// FIXME: possibly move elsewhere
enum InstSortMode {
    // Sort alphabetically by name.
    Sort_Name,
    // Sort by which instance was launched most recently.
    Sort_LastLaunch,
    // Sort by which instance has the most playtime.
    Sort_Playtime,
};

LauncherPage::LauncherPage(QWidget* parent) : QWidget(parent), ui(new Ui::LauncherPage)
{
    ui->setupUi(this);

    ui->sortingModeGroup->setId(ui->sortByNameBtn, Sort_Name);
    ui->sortingModeGroup->setId(ui->sortLastLaunchedBtn, Sort_LastLaunch);
    ui->sortingModeGroup->setId(ui->sortByPlaytimeBtn, Sort_Playtime);

    // ZERO: what the launcher does while a game runs
    {
        auto* box = new QGroupBox(tr("While playing"), ui->scrollAreaWidgetContents);
        auto* lay = new QVBoxLayout(box);
        m_whileTray = new QRadioButton(tr("Hide to the tray"), box);
        m_whileTray->setToolTip(tr("The launcher waits in the tray and comes back when you close the game."));
        m_whileOpen = new QRadioButton(tr("Keep it open"), box);
        m_whileOpen->setToolTip(tr("The launcher stays on screen while you play."));
        m_whileClose = new QRadioButton(tr("Close the launcher"), box);
        m_whileClose->setToolTip(tr("The launcher closes when the game window opens and comes back when the game ends."));
        lay->addWidget(m_whileTray);
        lay->addWidget(m_whileOpen);
        lay->addWidget(m_whileClose);
        ui->verticalLayout_8->insertWidget(1, box);
    }

    // ZERO: "Before you play" (zero/prelaunch): the mod and malware check when Play is pressed
    {
        auto* box = new QGroupBox(tr("Before you play"), ui->scrollAreaWidgetContents);
        auto* lay = new QVBoxLayout(box);
        m_checkMods = new QCheckBox(tr("Check mods before playing"), box);
        m_checkMods->setToolTip(tr("When you press Play, ZERO looks for known malware, duplicate or wrong mods, missing dependencies and packs the "
                                   "game turned off. Only the first check of new mods takes a moment."));
        lay->addWidget(m_checkMods);
        m_skipRow = new QWidget(box);
        auto* skipLay = new QHBoxLayout(m_skipRow);
        skipLay->setContentsMargins(0, 0, 0, 0);
        m_skipLabel = new QLabel(m_skipRow);
        Zero::Style::applyRole(m_skipLabel, Zero::Role::Caption);
        Zero::Style::setProperty(m_skipLabel, "zeroTone", "secondary");
        skipLay->addWidget(m_skipLabel);
        skipLay->addStretch(1);
        auto* again = new QPushButton(tr("Check them again"), m_skipRow);
        Zero::Style::setProperty(again, "zeroVariant", "secondary");
        Zero::Style::setProperty(again, "zeroSize", "sm");
        again->setAutoDefault(false);
        skipLay->addWidget(again);
        lay->addWidget(m_skipRow);
        connect(again, &QPushButton::clicked, this, [this]() {
            auto* list = APPLICATION->instances();
            for (int i = 0; i < list->count(); ++i) {
                list->at(i)->settings()->set("ZeroSkipPreLaunchCheck", false);
            }
            refreshSkippedInstances();
        });
        ui->verticalLayout_8->insertWidget(2, box);
    }

    // ZERO: "Updates for everything" (zero/updates): the daily look for mod updates of all instances
    {
        auto* box = new QGroupBox(tr("Updates"), ui->scrollAreaWidgetContents);
        auto* lay = new QVBoxLayout(box);
        m_autoCheckUpdates = new QCheckBox(tr("Check for mod updates once a day"), box);
        m_autoCheckUpdates->setToolTip(tr("While no game is running, ZERO looks for newer versions of the mods in your instances. Nothing is "
                                          "downloaded until you press Update on Home."));
        lay->addWidget(m_autoCheckUpdates);
        ui->verticalLayout_8->insertWidget(3, box);
    }

    // ZERO: "World backups" (zero/backup): automatic backups after playing, the schedule and where the backups are saved
    {
        auto* box = new QGroupBox(tr("World backups"), ui->scrollAreaWidgetContents);
        auto* lay = new QVBoxLayout(box);
        auto caption = [box](const QString& text, const char* tone = "secondary") {
            auto* label = new QLabel(text, box);
            label->setWordWrap(true);
            Zero::Style::applyRole(label, Zero::Role::Caption);
            Zero::Style::setProperty(label, "zeroTone", tone);
            return label;
        };

        m_backupAfterPlay = new QCheckBox(tr("Back up worlds after playing"), box);
        m_backupAfterPlay->setToolTip(tr("Applies to all instances. Backs up the worlds you played when the game closes and keeps the 5 newest "
                                         "automatic backups per world. Worlds over 2 GB are skipped."));
        lay->addWidget(m_backupAfterPlay);

        auto* scheduleRow = new QHBoxLayout();
        scheduleRow->addWidget(new QLabel(tr("Scheduled backups"), box));
        m_backupSchedule = new QComboBox(box);
        m_backupSchedule->addItem(tr("Off"), QStringLiteral("off"));
        m_backupSchedule->addItem(tr("Every day"), QStringLiteral("daily"));
        m_backupSchedule->addItem(tr("Every week"), QStringLiteral("weekly"));
        scheduleRow->addWidget(m_backupSchedule);
        scheduleRow->addStretch(1);
        lay->addLayout(scheduleRow);
        lay->addWidget(caption(tr("Backs up worlds that changed since their last backup, only while no game is running.")));

        auto* destRow = new QHBoxLayout();
        destRow->addWidget(new QLabel(tr("Save backups to"), box));
        m_backupDestination = new QComboBox(box);
        m_backupDestination->addItem(tr("ZERO data folder (default)"), QString());
        for (const auto& cloud : Zero::Backup::detectCloudFolders(Zero::Backup::systemEnvironment()))
            m_backupDestination->addItem(cloud.label, cloud.path);
        destRow->addWidget(m_backupDestination, 1);
        auto* choose = new QPushButton(tr("Choose folder..."), box);
        Zero::Style::setProperty(choose, "zeroVariant", "secondary");
        Zero::Style::setProperty(choose, "zeroSize", "sm");
        choose->setAutoDefault(false);
        destRow->addWidget(choose);
        lay->addLayout(destRow);
        m_backupPath = caption(QString());
        m_backupPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
        lay->addWidget(m_backupPath);
        m_backupWarning = caption(QString(), "warning");
        lay->addWidget(m_backupWarning);

        connect(m_backupDestination, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { refreshBackupDestination(); });
        connect(choose, &QPushButton::clicked, this, [this]() {
            QString start = m_backupDestination->currentData().toString();
            if (start.isEmpty())
                start = QDir::homePath();
            const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose where to save backups"), start);
            if (!dir.isEmpty())
                selectBackupFolder(dir);
        });
        // the same checkbox lives on the Worlds page: follow it
        connect(APPLICATION->settings()->getSetting("ZeroAutoBackupWorlds").get(), &Setting::SettingChanged, m_backupAfterPlay, [this]() {
            const bool on = APPLICATION->settings()->get("ZeroAutoBackupWorlds").toBool();
            if (m_backupAfterPlay->isChecked() != on)
                m_backupAfterPlay->setChecked(on);
        });
        ui->verticalLayout_8->insertWidget(4, box);
    }

    loadSettings();

    ui->updateSettingsBox->setHidden(!APPLICATION->updater());
}

LauncherPage::~LauncherPage()
{
    delete ui;
}

bool LauncherPage::apply()
{
    applySettings();
    return true;
}

bool LauncherPage::confirmInstanceDirPath(const QString& rawDir, const QString& cookedDir)
{
    if (FS::checkProblemticPathJava(QDir(cookedDir))) {
        QMessageBox warning;
        warning.setText(
            tr("You're trying to specify an instance folder which\'s path "
               "contains at least one \'!\'. "
               "Java is known to cause problems if that is the case, your "
               "instances (probably) won't start!"));
        warning.setInformativeText(
            tr("Do you really want to use this path? "
               "Selecting \"No\" will close this and not alter your instance path."));
        warning.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        if (warning.exec() != QMessageBox::Ok)
            return false;
    } else if (DesktopServices::isFlatpak() && rawDir.startsWith("/run/user")) {
        QMessageBox warning;
        warning.setText(tr("You're trying to specify an instance folder "
                           "which was granted temporarily via Flatpak.\n"
                           "This is known to cause problems. "
                           "After a restart the launcher might break, "
                           "because it will no longer have access to that directory.\n\n"
                           "Granting %1 access to it via Flatseal is recommended.")
                            .arg(BuildConfig.LAUNCHER_DISPLAYNAME));
        warning.setInformativeText(tr("Do you want to proceed anyway?"));
        warning.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        if (warning.exec() != QMessageBox::Ok)
            return false;
    }
    return true;
}

void LauncherPage::on_instDirBrowseBtn_clicked()
{
    QString rawDir = QFileDialog::getExistingDirectory(this, tr("Instance Folder"), ui->instDirTextBox->text());
    if (!rawDir.isEmpty() && QDir(rawDir).exists()) {
        QString cookedDir = FS::NormalizePath(rawDir);
        if (confirmInstanceDirPath(rawDir, cookedDir))
            ui->instDirTextBox->setText(cookedDir);
    }
}

void LauncherPage::on_addInstDirBtn_clicked()
{
    QString rawDir = QFileDialog::getExistingDirectory(this, tr("Additional Instance Folder"));
    if (rawDir.isEmpty() || !QDir(rawDir).exists())
        return;

    QString cookedDir = FS::NormalizePath(rawDir);
    if (cookedDir == FS::NormalizePath(ui->instDirTextBox->text())) {
        QMessageBox::warning(this, tr("Duplicate directory"), tr("This is already your primary instance directory."));
        return;
    }

    if (!ui->additionalInstDirsList->findItems(cookedDir, Qt::MatchFixedString).isEmpty()) {
        QMessageBox::warning(this, tr("Duplicate directory"), tr("This directory has already been added."));
        return;
    }

    if (!confirmInstanceDirPath(rawDir, cookedDir))
        return;

    ui->additionalInstDirsList->addItem(cookedDir);
}

void LauncherPage::on_removeInstDirBtn_clicked()
{
    qDeleteAll(ui->additionalInstDirsList->selectedItems());
}

void LauncherPage::on_iconsDirBrowseBtn_clicked()
{
    QString rawDir = QFileDialog::getExistingDirectory(this, tr("Icons Folder"), ui->iconsDirTextBox->text());

    // do not allow current dir - it's dirty. Do not allow dirs that don't exist
    if (!rawDir.isEmpty() && QDir(rawDir).exists()) {
        QString cookedDir = FS::NormalizePath(rawDir);
        ui->iconsDirTextBox->setText(cookedDir);
    }
}

void LauncherPage::on_modsDirBrowseBtn_clicked()
{
    QString rawDir = QFileDialog::getExistingDirectory(this, tr("Mods Folder"), ui->modsDirTextBox->text());

    // do not allow current dir - it's dirty. Do not allow dirs that don't exist
    if (!rawDir.isEmpty() && QDir(rawDir).exists()) {
        QString cookedDir = FS::NormalizePath(rawDir);
        ui->modsDirTextBox->setText(cookedDir);
    }
}

void LauncherPage::on_downloadsDirBrowseBtn_clicked()
{
    QString rawDir = QFileDialog::getExistingDirectory(this, tr("Downloads Folder"), ui->downloadsDirTextBox->text());

    if (!rawDir.isEmpty() && QDir(rawDir).exists()) {
        QString cookedDir = FS::NormalizePath(rawDir);
        ui->downloadsDirTextBox->setText(cookedDir);
    }
}

void LauncherPage::on_javaDirBrowseBtn_clicked()
{
    QString rawDir = QFileDialog::getExistingDirectory(this, tr("Java Folder"), ui->javaDirTextBox->text());

    if (!rawDir.isEmpty() && QDir(rawDir).exists()) {
        QString cookedDir = FS::NormalizePath(rawDir);
        ui->javaDirTextBox->setText(cookedDir);
    }
}

void LauncherPage::on_templateDirBrowseBtn_clicked()
{
    QString rawDir = QFileDialog::getExistingDirectory(this, tr("Instance Template Folder"), ui->templateDirTextBox->text());

    if (!rawDir.isEmpty() && QDir(rawDir).exists()) {
        QString cookedDir = FS::NormalizePath(rawDir);
        ui->templateDirTextBox->setText(cookedDir);
    }
}

void LauncherPage::on_skinsDirBrowseBtn_clicked()
{
    QString rawDir = QFileDialog::getExistingDirectory(this, tr("Skins Folder"), ui->skinsDirTextBox->text());

    // do not allow current dir - it's dirty. Do not allow dirs that don't exist
    if (!rawDir.isEmpty() && QDir(rawDir).exists()) {
        QString cookedDir = FS::NormalizePath(rawDir);
        ui->skinsDirTextBox->setText(cookedDir);
    }
}

void LauncherPage::on_metadataEnableBtn_clicked()
{
    ui->metadataWarningLabel->setHidden(ui->metadataEnableBtn->isChecked());
}

// ZERO: selects `folder` in the "Save backups to" list (a detected cloud folder or the default when it is one, else a custom item)
void LauncherPage::selectBackupFolder(const QString& folder)
{
    const QString cleaned = folder.trimmed().isEmpty() ? QString() : QDir::cleanPath(folder.trimmed());
    int index = -1;
    for (int i = 0; i < m_backupDestination->count(); ++i) {
        if (m_backupDestination->itemData(i).toString().compare(cleaned, Qt::CaseInsensitive) == 0) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        if (m_backupCustomIndex < 0) {
            m_backupDestination->addItem(QString(), cleaned);
            m_backupCustomIndex = m_backupDestination->count() - 1;
        }
        index = m_backupCustomIndex;
        m_backupDestination->setItemText(index, QDir::toNativeSeparators(cleaned));
        m_backupDestination->setItemData(index, cleaned);
    }
    if (m_backupDestination->currentIndex() == index)
        refreshBackupDestination();
    else
        m_backupDestination->setCurrentIndex(index);
}

// ZERO: shows where the selected choice saves to, and a warning when that folder cannot be written
void LauncherPage::refreshBackupDestination()
{
    const QString folder = m_backupDestination->currentData().toString();
    if (folder.isEmpty()) {
        m_backupPath->setText(tr("Saved in %1").arg(QDir::toNativeSeparators(Zero::Backup::backupsRoot(APPLICATION->dataRoot()))));
        m_backupWarning->hide();
        return;
    }
    m_backupPath->setText(tr("Saved in %1").arg(QDir::toNativeSeparators(Zero::Backup::rootInFolder(folder))));
    const Zero::Backup::Probe probe = Zero::Backup::probeFolder(folder);
    m_backupWarning->setText(tr("%1 Backups go to the ZERO data folder until this works.").arg(probe.error));
    m_backupWarning->setVisible(!probe.ok);
}

void LauncherPage::applySettings()
{
    auto* s = APPLICATION->settings();

    // Updates
    if (APPLICATION->updater()) {
        APPLICATION->updater()->setAutomaticallyChecksForUpdates(ui->autoUpdateCheckBox->isChecked());
        APPLICATION->updater()->setUpdateCheckInterval(ui->updateIntervalSpinBox->value() * 3600);
    }

    s->set("MenuBarInsteadOfToolBar", ui->preferMenuBarCheckBox->isChecked());
    s->set("ZeroWhilePlaying",
           Zero::Session::whilePlayingToString(m_whileClose->isChecked()  ? Zero::Session::WhilePlayingMode::Close
                                               : m_whileOpen->isChecked() ? Zero::Session::WhilePlayingMode::Open
                                                                          : Zero::Session::WhilePlayingMode::Tray));

    s->set("ZeroPreLaunchCheck", m_checkMods->isChecked());
    s->set("ZeroAutoCheckUpdates", m_autoCheckUpdates->isChecked());

    s->set("ZeroAutoBackupWorlds", m_backupAfterPlay->isChecked());
    s->set("ZeroBackupSchedule", m_backupSchedule->currentData().toString());
    {
        const QString folder = m_backupDestination->currentData().toString();
        s->set("ZeroBackupRoot", folder);
        // a new destination: its old and new backups are listed from now on
        if (!folder.isEmpty()) {
            if (auto* jobs = Zero::Backup::WorldBackupJobs::global())
                jobs->rememberRoot(Zero::Backup::rootInFolder(folder));
        }
    }

    s->set("NumberOfConcurrentTasks", ui->numberOfConcurrentTasksSpinBox->value());
    s->set("NumberOfConcurrentDownloads", ui->numberOfConcurrentDownloadsSpinBox->value());
    s->set("NetworkRetryLimit", ui->networkRetryLimitSpinBox->value());
    s->set("RequestTimeout", ui->timeoutSecondsSpinBox->value());

    // Console settings
    s->set("ConsoleMaxLines", ui->lineLimitSpinBox->value());
    s->set("ConsoleOverflowStop", ui->checkStopLogging->checkState() != Qt::Unchecked);

    // Folders
    // TODO: Offer to move instances to new instance folder.
    s->set("InstanceDir", ui->instDirTextBox->text());
    {
        QStringList additionalDirs;
        for (int i = 0; i < ui->additionalInstDirsList->count(); ++i) {
            additionalDirs << ui->additionalInstDirsList->item(i)->text();
        }
        s->set("AdditionalInstanceDirs", additionalDirs);
    }
    s->set("CentralModsDir", ui->modsDirTextBox->text());
    s->set("IconsDir", ui->iconsDirTextBox->text());
    s->set("DownloadsDir", ui->downloadsDirTextBox->text());
    s->set("SkinsDir", ui->skinsDirTextBox->text());
    s->set("JavaDir", ui->javaDirTextBox->text());
    s->set("TemplateDir", ui->templateDirTextBox->text());
    s->set("DownloadsDirWatchRecursive", ui->downloadsDirWatchRecursiveCheckBox->isChecked());
    s->set("MoveModsFromDownloadsDir", ui->downloadsDirMoveCheckBox->isChecked());

    // Instance
    auto sortMode = (InstSortMode)ui->sortingModeGroup->checkedId();
    switch (sortMode) {
        case Sort_LastLaunch:
            s->set("InstSortMode", "LastLaunch");
            break;
        case Sort_Playtime:
            s->set("InstSortMode", "Playtime");
            break;
        case Sort_Name:
        default:
            s->set("InstSortMode", "Name");
            break;
    }

    if (ui->askToRenameDirBtn->isChecked()) {
        s->set("InstRenamingMode", "AskEverytime");
    } else if (ui->alwaysRenameDirBtn->isChecked()) {
        s->set("InstRenamingMode", "PhysicalDir");
    } else if (ui->neverRenameDirBtn->isChecked()) {
        s->set("InstRenamingMode", "MetadataOnly");
    }

    s->set("EditInstanceOnDoubleClick", ui->editInstanceOnDoubleClick->isChecked());

    // Mods
    s->set("ModMetadataDisabled", !ui->metadataEnableBtn->isChecked());
    s->set("ModDependenciesDisabled", !ui->dependenciesEnableBtn->isChecked());
    s->set("ShowModIncompat", ui->showModIncompatCheckBox->isChecked());
    s->set("SkipModpackUpdatePrompt", !ui->modpackUpdatePromptBtn->isChecked());
    s->set("DownloadGameFilesDuringInstanceCreation", ui->downloadGameFilesBtn->isChecked());

    switch (ui->modUpdateChannelComboBox->currentIndex()) {
        case 1:
            s->set("ModUpdateReleaseTypes", "[\"release\"]");
            break;
        case 2:
            s->set("ModUpdateReleaseTypes", "[\"release\", \"beta\"]");
            break;
        case 0:
        default:
            s->set("ModUpdateReleaseTypes", "[]");
            break;
    }
}
void LauncherPage::refreshSkippedInstances()
{
    int skipped = 0;
    auto* list = APPLICATION->instances();
    for (int i = 0; list && i < list->count(); ++i) {
        skipped += list->at(i)->settings()->get("ZeroSkipPreLaunchCheck").toBool() ? 1 : 0;
    }
    m_skipRow->setVisible(skipped > 0);
    m_skipLabel->setText(skipped == 1 ? tr("1 instance skips the mod and pack checks.")
                                      : tr("%1 instances skip the mod and pack checks.").arg(skipped));
}

void LauncherPage::loadSettings()
{
    auto* s = APPLICATION->settings();
    // Updates
    if (APPLICATION->updater()) {
        ui->autoUpdateCheckBox->setChecked(APPLICATION->updater()->getAutomaticallyChecksForUpdates());
        ui->updateIntervalSpinBox->setValue(APPLICATION->updater()->getUpdateCheckInterval() / 3600);
    }

    ui->preferMenuBarCheckBox->setChecked(s->get("MenuBarInsteadOfToolBar").toBool());
    switch (Zero::Session::whilePlayingFromString(s->get("ZeroWhilePlaying").toString())) {
        case Zero::Session::WhilePlayingMode::Open:
            m_whileOpen->setChecked(true);
            break;
        case Zero::Session::WhilePlayingMode::Close:
            m_whileClose->setChecked(true);
            break;
        case Zero::Session::WhilePlayingMode::Tray:
            m_whileTray->setChecked(true);
            break;
    }

    m_checkMods->setChecked(s->get("ZeroPreLaunchCheck").toBool());
    m_autoCheckUpdates->setChecked(s->get("ZeroAutoCheckUpdates").toBool());
    m_backupAfterPlay->setChecked(s->get("ZeroAutoBackupWorlds").toBool());
    m_backupSchedule->setCurrentIndex(qMax(0, m_backupSchedule->findData(Zero::Backup::scheduleToString(
                                                      Zero::Backup::scheduleFromString(s->get("ZeroBackupSchedule").toString())))));
    selectBackupFolder(s->get("ZeroBackupRoot").toString());
    refreshSkippedInstances();

    ui->numberOfConcurrentTasksSpinBox->setValue(s->get("NumberOfConcurrentTasks").toInt());
    ui->numberOfConcurrentDownloadsSpinBox->setValue(s->get("NumberOfConcurrentDownloads").toInt());
    ui->networkRetryLimitSpinBox->setValue(s->get("NetworkRetryLimit").toInt());
    ui->timeoutSecondsSpinBox->setValue(s->get("RequestTimeout").toInt());

    // Console settings
    ui->lineLimitSpinBox->setValue(s->get("ConsoleMaxLines").toInt());
    ui->checkStopLogging->setChecked(s->get("ConsoleOverflowStop").toBool());

    // Folders
    ui->instDirTextBox->setText(s->get("InstanceDir").toString());
    ui->additionalInstDirsList->clear();
    ui->additionalInstDirsList->addItems(s->get("AdditionalInstanceDirs").toStringList());
    ui->modsDirTextBox->setText(s->get("CentralModsDir").toString());
    ui->iconsDirTextBox->setText(s->get("IconsDir").toString());
    ui->downloadsDirTextBox->setText(s->get("DownloadsDir").toString());
    ui->skinsDirTextBox->setText(s->get("SkinsDir").toString());
    ui->javaDirTextBox->setText(s->get("JavaDir").toString());
    ui->templateDirTextBox->setText(s->get("TemplateDir").toString());
    ui->downloadsDirWatchRecursiveCheckBox->setChecked(s->get("DownloadsDirWatchRecursive").toBool());
    ui->downloadsDirMoveCheckBox->setChecked(s->get("MoveModsFromDownloadsDir").toBool());

    // Instance
    QString sortMode = s->get("InstSortMode").toString();
    if (sortMode == "LastLaunch") {
        ui->sortLastLaunchedBtn->setChecked(true);
    } else if (sortMode == "Playtime") {
        ui->sortByPlaytimeBtn->setChecked(true);
    } else {
        ui->sortByNameBtn->setChecked(true);
    }

    ui->editInstanceOnDoubleClick->setChecked(s->get("EditInstanceOnDoubleClick").toBool());

    QString renamingMode = s->get("InstRenamingMode").toString();
    ui->askToRenameDirBtn->setChecked(renamingMode == "AskEverytime");
    ui->alwaysRenameDirBtn->setChecked(renamingMode == "PhysicalDir");
    ui->neverRenameDirBtn->setChecked(renamingMode == "MetadataOnly");

    // Mods
    ui->metadataEnableBtn->setChecked(!s->get("ModMetadataDisabled").toBool());
    ui->metadataWarningLabel->setHidden(ui->metadataEnableBtn->isChecked());
    ui->dependenciesEnableBtn->setChecked(!s->get("ModDependenciesDisabled").toBool());
    ui->showModIncompatCheckBox->setChecked(s->get("ShowModIncompat").toBool());
    ui->modpackUpdatePromptBtn->setChecked(!s->get("SkipModpackUpdatePrompt").toBool());
    ui->downloadGameFilesBtn->setChecked(s->get("DownloadGameFilesDuringInstanceCreation").toBool());

    const auto releaseTypesSetting = Json::toStringList(s->get("ModUpdateReleaseTypes").toString());
    if (releaseTypesSetting.size() == 1 && releaseTypesSetting.contains("release", Qt::CaseInsensitive)) {
        ui->modUpdateChannelComboBox->setCurrentIndex(1);
    } else if (releaseTypesSetting.size() == 2 && releaseTypesSetting.contains("release", Qt::CaseInsensitive) &&
               releaseTypesSetting.contains("beta", Qt::CaseInsensitive)) {
        ui->modUpdateChannelComboBox->setCurrentIndex(2);
    } else {
        ui->modUpdateChannelComboBox->setCurrentIndex(0);
    }
}

void LauncherPage::retranslate()
{
    ui->retranslateUi(this);
}
