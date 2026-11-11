// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2021-2022 Jamie Mansfield <jmansfield@cadixdev.org>
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

#include "TechnicPage.h"
#include "ui/widgets/ProjectItem.h"
#include "ui_TechnicPage.h"

#include <QKeyEvent>

#include "ui/dialogs/NewInstanceDialog.h"

#include "StringUtils.h"
#include "TechnicModel.h"
#include "modplatform/technic/SingleZipPackInstallTask.h"
#include "modplatform/technic/SolderPackInstallTask.h"

#include "Application.h"

TechnicPage::TechnicPage(NewInstanceDialog* dialog, QWidget* parent)
    : QWidget(parent), ui(new Ui::TechnicPage), dialog(dialog), model(new Technic::ListModel(this)), m_fetch_progress(this, false)
{
    ui->setupUi(this);

    ui->packView->setModel(model);
    ui->versionSelectionBox->view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ui->versionSelectionBox->view()->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    m_search_timer.setTimerType(Qt::TimerType::CoarseTimer);
    m_search_timer.setSingleShot(true);

    connect(&m_search_timer, &QTimer::timeout, this, &TechnicPage::triggerSearch);

    connect(ui->searchEdit, &QLineEdit::textEdited, this, [this] {
        if (m_search_timer.isActive()) {
            m_search_timer.stop();
        }
        m_search_timer.start(350);
    });

    m_fetch_progress.hideIfInactive(true);
    m_fetch_progress.setFixedHeight(24);
    m_fetch_progress.progressFormat("");

    ui->verticalLayout->insertWidget(1, &m_fetch_progress);

    connect(ui->packView->selectionModel(), &QItemSelectionModel::currentChanged, this, &TechnicPage::onSelectionChanged);
    connect(ui->versionSelectionBox, &QComboBox::currentTextChanged, this, &TechnicPage::onVersionSelectionChanged);
    connect(model, &Technic::ListModel::detailsReady, this, &TechnicPage::onDetailsReady);
    connect(model, &Technic::ListModel::detailsFailed, this, &TechnicPage::onDetailsFailed);
    connect(model, &Technic::ListModel::versionsReady, this, &TechnicPage::onVersionsReady);
    connect(model, &Technic::ListModel::versionsFailed, this, &TechnicPage::onVersionsFailed);

    ui->packView->setItemDelegate(new ProjectItemDelegate(this));
}

TechnicPage::~TechnicPage()
{
    delete ui;
}

bool TechnicPage::shouldDisplay() const
{
    return true;
}

void TechnicPage::retranslate()
{
    ui->retranslateUi(this);
}

void TechnicPage::openedImpl()
{
    suggestCurrent();
    triggerSearch();
}

void TechnicPage::triggerSearch()
{
    model->searchWithTerm(ui->searchEdit->text());
    m_fetch_progress.watch(model->activeSearchJob().get());
}

void TechnicPage::onSelectionChanged(QModelIndex first, [[maybe_unused]] QModelIndex second)
{
    ui->versionSelectionBox->clear();
    m_detailsApplied = false;
    m_versionsApplied = false;

    if (!first.isValid()) {
        current = {};
        m_selection.clear();
        ui->packDescription->clear();
        if (isOpened) {
            dialog->setSuggestedPack();
        }
        return;
    }

    QVariant raw = model->data(first, Qt::UserRole);
    Q_ASSERT(raw.canConvert<Technic::Modpack>());
    current = raw.value<Technic::Modpack>();
    m_selection.select(current.slug);
    suggestCurrent();
}

void TechnicPage::suggestCurrent()
{
    if (!isOpened) {
        return;
    }
    if (current.broken) {
        dialog->setSuggestedPack();
        return;
    }

    // Reset the dialog to this pack right away: its own name and no install task (OK disabled) until its details are
    // shown, so nothing of the previously selected pack stays behind. This also resets the icon; the pack's logo follows.
    const QString title = Zero::Content::previewTitle(nullptr, current.name, current.slug);
    dialog->setSuggestedPack(title);

    QString editedLogoName = "technic_" + current.logoName;
    model->getLogo(current.logoName, current.logoUrl,
                   [this, editedLogoName](const QString& logo) { dialog->setSuggestedIconFromFile(logo, editedLogoName); });

    if (m_detailsApplied) {
        // Page opened again with the details already shown: only the install task is missing.
        selectVersion();
        return;
    }
    if (const auto details = model->cachedDetails(current.slug)) {
        applyDetails(*details);
        return;
    }
    ui->packDescription->setHtml(Zero::Content::previewLoadingHtml(title));
    model->requestDetails(current.slug);
}

void TechnicPage::applyDetails(const Zero::Content::TechnicPackDetails& details)
{
    current.name = Zero::Content::previewTitle(&details, current.name, current.slug);
    current.author = details.author;
    current.description = details.description;
    current.minecraftVersion = details.minecraftVersion;
    current.currentVersion = details.version;
    current.websiteUrl = details.platformUrl;
    // A zip URL wins over Solder; a Solder base comes without trailing slashes.
    current.isSolder = details.isSolder();
    current.url = details.installUrl();
    current.metadataLoaded = true;
    m_detailsApplied = true;

    ui->packDescription->setHtml(StringUtils::htmlListPatch(Zero::Content::previewHtml(details, current.name)));

    fillVersions();
    selectVersion();
}

void TechnicPage::fillVersions()
{
    ui->versionSelectionBox->clear();

    if (!current.isSolder) {
        // If the pack isn't a Solder pack, it only has the single version
        ui->versionSelectionBox->addItem(current.currentVersion);
        return;
    }
    if (const auto versions = model->cachedVersions(current.slug)) {
        m_versionsApplied = true;
        // reverse foreach, so that the newest versions are first
        for (auto i = versions->builds.size(); (i--) != 0;) {
            ui->versionSelectionBox->addItem(versions->builds.at(i));
        }
        ui->versionSelectionBox->setCurrentText(versions->recommended);
        return;
    }
    // Until the versions are pulled from the Solder instance, display the current version so we can display something quicker
    ui->versionSelectionBox->addItem(current.currentVersion);
    model->requestVersions(current.slug, current.url);
}

void TechnicPage::selectVersion()
{
    if (!isOpened) {
        return;
    }
    if (current.broken) {
        dialog->setSuggestedPack();
        return;
    }
    if (!m_detailsApplied || current.url.isEmpty()) {
        // Details still loading, failed, or the pack has no download: the dialog keeps this pack's name without a task.
        return;
    }

    if (!current.isSolder) {
        dialog->setSuggestedPack(current.name, selectedVersion,
                                 new Technic::SingleZipPackInstallTask(current.url, current.minecraftVersion));
    } else {
        dialog->setSuggestedPack(current.name, selectedVersion,
                                 new Technic::SolderPackInstallTask(APPLICATION->network(), current.url, current.slug, selectedVersion,
                                                                    current.minecraftVersion));
    }
}

void TechnicPage::onDetailsReady(const QString& slug)
{
    if (!m_selection.isCurrent(slug) || m_detailsApplied) {
        return;  // another pack's answer, or already shown
    }
    if (const auto details = model->cachedDetails(slug)) {
        applyDetails(*details);
    }
}

void TechnicPage::onDetailsFailed(const QString& slug, const QString& reason)
{
    if (!m_selection.isCurrent(slug) || m_detailsApplied) {
        return;
    }
    qWarning() << "Couldn't load the Technic details of" << slug << ":" << reason;
    // Inline, not a popup: the dialog keeps the pack's name without an install task. Selecting the pack again retries.
    const QString title = Zero::Content::previewTitle(nullptr, current.name, current.slug);
    ui->packDescription->setHtml(Zero::Content::previewFailedHtml(title, reason));
}

void TechnicPage::onVersionsReady(const QString& slug)
{
    if (!m_selection.isCurrent(slug) || !m_detailsApplied || !current.isSolder || m_versionsApplied) {
        return;
    }
    fillVersions();
    selectVersion();
}

void TechnicPage::onVersionsFailed(const QString& slug, const QString& reason)
{
    if (!m_selection.isCurrent(slug)) {
        return;
    }
    // The version box keeps the pack's current version, which installs fine.
    qWarning() << "Couldn't load the Solder versions of" << slug << ":" << reason;
}

void TechnicPage::onVersionSelectionChanged(const QString& version)
{
    if (version.isNull() || version.isEmpty()) {
        selectedVersion = "";
        return;
    }

    selectedVersion = version;
    selectVersion();
}

void TechnicPage::setSearchTerm(QString term)
{
    ui->searchEdit->setText(term);
}

QString TechnicPage::getSerachTerm() const
{
    return ui->searchEdit->text();
}
