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

#pragma once

#include <QTimer>
#include <QWidget>

#include "TechnicData.h"
#include "net/NetJob.h"
#include "ui/pages/modplatform/ModpackProviderBasePage.h"
#include "ui/widgets/ProgressWidget.h"
#include "zero/content/TechnicMeta.h"

namespace Ui {
class TechnicPage;
}

class NewInstanceDialog;

namespace Technic {
class ListModel;
}

class TechnicPage : public QWidget, public ModpackProviderBasePage {
    Q_OBJECT

   public:
    explicit TechnicPage(NewInstanceDialog* dialog, QWidget* parent = nullptr);
    ~TechnicPage() override;
    QString displayName() const override { return "Technic"; }
    QIcon icon() const override { return QIcon::fromTheme("technic"); }
    QString id() const override { return "technic"; }
    QString helpPage() const override { return "Technic-platform"; }
    bool shouldDisplay() const override;
    void retranslate() override;

    void openedImpl() override;

    /** Programatically set the term in the search bar. */
    void setSearchTerm(QString /*unused*/) override;
    /** Get the current term in the search bar. */
    QString getSerachTerm() const override;

   private:
    void suggestCurrent();
    void applyDetails(const Zero::Content::TechnicPackDetails& details);
    void fillVersions();
    void selectVersion();

   private slots:
    void triggerSearch();
    void onSelectionChanged(QModelIndex first, QModelIndex second);
    void onDetailsReady(const QString& slug);
    void onDetailsFailed(const QString& slug, const QString& reason);
    void onVersionsReady(const QString& slug);
    void onVersionsFailed(const QString& slug, const QString& reason);
    void onVersionSelectionChanged(const QString& version);

   private:
    Ui::TechnicPage* ui = nullptr;
    NewInstanceDialog* dialog = nullptr;
    Technic::ListModel* model = nullptr;

    /// The selected pack: its list row, completed by applyDetails() once its details are shown.
    Technic::Modpack current;
    /// The slug whose answers this page still wants; details and Solder answers for any other slug are stale.
    Zero::Content::TechnicSelection m_selection;
    /// The details of `current` are shown and can build its install task.
    bool m_detailsApplied = false;
    /// The Solder build list of `current` is in the version box.
    bool m_versionsApplied = false;
    QString selectedVersion;

    ProgressWidget m_fetch_progress;

    // Used to do instant searching with a delay to cache quick changes
    QTimer m_search_timer;
};
