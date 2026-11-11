// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2021 Jamie Mansfield <jmansfield@cadixdev.org>
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
 *      Copyright 2020-2021 MultiMC Contributors
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

#include "TechnicModel.h"
#include "Application.h"
#include "BuildConfig.h"
#include "Json.h"
#include "settings/SettingsObject.h"

#include "net/ApiRequest.h"
#include "ui/widgets/ProjectItem.h"
#include "zero/content/TechnicDetailsStore.h"

#include <QFileInfo>
#include <QIcon>
#include <QUrl>

Technic::ListModel::ListModel(QObject* parent) : QAbstractListModel(parent)
{
    using Zero::Content::TechnicDetailsStore;
    auto fetchDetails = [this](const QString& slug, TechnicDetailsStore::Reply reply) {
        const QString url = QString("%1modpack/%2?build=%3")
                                .arg(BuildConfig.TECHNIC_API_BASE_URL, QString::fromLatin1(QUrl::toPercentEncoding(slug)),
                                     BuildConfig.TECHNIC_API_BUILD);
        fetchJson(QString("Technic::PackMeta(%1)").arg(slug), QUrl(url), std::move(reply));
    };
    auto fetchVersions = [this](const QString& slug, const QString& solderBase, TechnicDetailsStore::Reply reply) {
        fetchJson(QString("Technic::SolderMeta(%1)").arg(slug), QUrl(Zero::Content::solderPackUrl(solderBase, slug)), std::move(reply));
    };
    m_details = new TechnicDetailsStore(fetchDetails, fetchVersions, this);
    connect(m_details, &TechnicDetailsStore::detailsReady, this, [this](const QString& slug) {
        applyDetailsToRows(slug);
        emit detailsReady(slug);
    });
    connect(m_details, &TechnicDetailsStore::detailsFailed, this, &ListModel::detailsFailed);
    connect(m_details, &TechnicDetailsStore::versionsReady, this, &ListModel::versionsReady);
    connect(m_details, &TechnicDetailsStore::versionsFailed, this, &ListModel::versionsFailed);
}

Technic::ListModel::~ListModel() {}

QVariant Technic::ListModel::data(const QModelIndex& index, int role) const
{
    int pos = index.row();
    if (pos >= modpacks.size() || pos < 0 || !index.isValid()) {
        return QString("INVALID INDEX %1").arg(pos);
    }

    Modpack pack = modpacks.at(pos);
    switch (role) {
        case Qt::ToolTipRole:
            // The description is HTML from the Technic API: show a short plain-text summary.
            return Zero::Content::plainTextSummary(pack.description, 100);
        case Qt::DecorationRole: {
            if (m_logoMap.contains(pack.logoName)) {
                return (m_logoMap.value(pack.logoName));
            }
            QIcon icon = QIcon::fromTheme("screenshot-placeholder");
            ((ListModel*)this)->requestLogo(pack.logoName, pack.logoUrl);
            return icon;
        }
        case Qt::UserRole: {
            QVariant v;
            v.setValue(pack);
            return v;
        }
        case Qt::DisplayRole:
            return pack.name;
        case Qt::SizeHintRole:
            return QSize(0, 58);
        // Custom data
        case UserDataTypes::TITLE:
            return pack.name;
        case UserDataTypes::DESCRIPTION:
            return Zero::Content::plainText(pack.description);
        case UserDataTypes::INSTALLED:
            return false;
        default:
            break;
    }

    return {};
}

int Technic::ListModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : 1;
}

int Technic::ListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : modpacks.size();
}

void Technic::ListModel::searchWithTerm(const QString& term)
{
    if (currentSearchTerm == term && currentSearchTerm.isNull() == term.isNull()) {
        return;
    }
    currentSearchTerm = term;
    if (hasActiveSearchJob()) {
        jobPtr->abort();
        searchState = ResetRequested;
        return;
    }

    beginResetModel();
    modpacks.clear();
    endResetModel();
    searchState = None;

    performSearch();
}

void Technic::ListModel::performSearch()
{
    if (hasActiveSearchJob())
        return;

    auto netJob = makeShared<NetJob>("Technic::Search", APPLICATION->network());
    QString searchUrl = "";
    if (currentSearchTerm.isEmpty()) {
        searchUrl = QString("%1trending?build=%2").arg(BuildConfig.TECHNIC_API_BASE_URL, BuildConfig.TECHNIC_API_BUILD);
        searchMode = List;
    } else if (currentSearchTerm.startsWith("http://api.technicpack.net/modpack/")) {
        searchUrl = QString("https://%1?build=%2").arg(currentSearchTerm.mid(7), BuildConfig.TECHNIC_API_BUILD);
        searchMode = Single;
    } else if (currentSearchTerm.startsWith("https://api.technicpack.net/modpack/")) {
        searchUrl = QString("%1?build=%2").arg(currentSearchTerm, BuildConfig.TECHNIC_API_BUILD);
        searchMode = Single;
    } else if (currentSearchTerm.startsWith("#")) {
        searchUrl = QString("https://api.technicpack.net/modpack/%1?build=%2").arg(currentSearchTerm.mid(1), BuildConfig.TECHNIC_API_BUILD);
        searchMode = Single;
    } else {
        searchUrl =
            QString("%1search?build=%2&q=%3").arg(BuildConfig.TECHNIC_API_BASE_URL, BuildConfig.TECHNIC_API_BUILD, currentSearchTerm);
        searchMode = List;
    }
    auto clientId = APPLICATION->settings()->get("TechnicClientID").toString();
    if (!clientId.isEmpty()) {
        searchUrl += "&cid=" + clientId;
    }
    auto [action, response] = Net::ApiRequest::makeByteArray(QUrl(searchUrl));
    netJob->addNetAction(action);
    jobPtr = netJob;
    jobPtr->start();
    connect(netJob.get(), &NetJob::succeeded, this, [this, response] { searchRequestFinished(response); });
    connect(netJob.get(), &NetJob::failed, this, &ListModel::searchRequestFailed);
}

void Technic::ListModel::searchRequestFinished(QByteArray* responsePtr)
{
    // NOTE(TheKodeToad): moving the response out to avoid it from being destroyed by jobPtr.reset()
    QByteArray response = std::move(*responsePtr);
    jobPtr.reset();

    QList<Modpack> newList;
    switch (searchMode) {
        case List: {
            auto packs = Zero::Content::parseSearchResponse(response);
            if (!packs) {
                qCritical() << "Couldn't parse technic search results:" << packs.error();
                return;
            }
            for (const auto& summary : *packs) {
                Modpack pack;
                pack.name = summary.name;
                pack.slug = summary.slug;
                if (summary.iconUrl.isEmpty()) {
                    pack.logoUrl = "null";
                    pack.logoName = "null";
                } else {
                    pack.logoUrl = summary.iconUrl;
                    pack.logoName = pack.slug + "." + QFileInfo(QUrl(summary.iconUrl).fileName()).suffix();
                }
                pack.broken = false;
                newList.append(pack);
            }
            break;
        }
        case Single: {
            auto details = Zero::Content::parsePackDetails(response);
            if (!details) {
                // Invalid API url or unknown pack: no results
                qWarning() << "Technic search by slug found nothing:" << details.error();
                break;
            }
            Modpack pack;
            pack.name = Zero::Content::previewTitle(&*details, {}, details->slug);
            pack.slug = details->slug;
            if (details->iconUrl.isEmpty()) {
                pack.logoUrl = "null";
                pack.logoName = "null";
            } else {
                pack.logoUrl = details->iconUrl;
                pack.logoName = pack.slug + "." + QFileInfo(QUrl(details->iconUrl).fileName()).suffix();
            }
            pack.broken = false;
            newList.append(pack);
            // The response already holds the full details: cache them so selecting the result does not fetch again.
            m_details->insertDetails(*details);
            break;
        }
    }
    searchState = Finished;

    // When you have a Qt build with assertions turned on, proceeding here will abort the application
    if (newList.size() == 0) {
        return;
    }

    // Rows of packs whose details were loaded before (earlier search, or the response above) show them right away.
    for (auto& pack : newList) {
        if (const auto details = m_details->details(pack.slug)) {
            fillFromDetails(pack, *details);
        }
    }

    beginInsertRows(QModelIndex(), modpacks.size(), modpacks.size() + newList.size() - 1);
    modpacks.append(newList);
    endInsertRows();
}

void Technic::ListModel::getLogo(const QString& logo, const QString& logoUrl, Technic::LogoCallback callback)
{
    if (m_logoMap.contains(logo)) {
        callback(APPLICATION->metacache()->resolveEntry("TechnicPacks", QString("logos/%1").arg(logo))->getFullPath());
    } else {
        requestLogo(logo, logoUrl);
    }
}

void Technic::ListModel::searchRequestFailed()
{
    jobPtr.reset();

    if (searchState == ResetRequested) {
        beginResetModel();
        modpacks.clear();
        endResetModel();

        performSearch();
    } else {
        searchState = Finished;
    }
}

void Technic::ListModel::logoLoaded(QString logo, QString out)
{
    m_loadingLogos.removeAll(logo);
    m_logoMap.insert(logo, QIcon(out));
    for (int i = 0; i < modpacks.size(); i++) {
        if (modpacks[i].logoName == logo) {
            emit dataChanged(createIndex(i, 0), createIndex(i, 0), { Qt::DecorationRole });
        }
    }
}

void Technic::ListModel::logoFailed(QString logo)
{
    m_failedLogos.append(logo);
    m_loadingLogos.removeAll(logo);
}

void Technic::ListModel::requestLogo(QString logo, QString url)
{
    if (m_loadingLogos.contains(logo) || m_failedLogos.contains(logo) || logo == "null") {
        return;
    }

    MetaEntryPtr entry = APPLICATION->metacache()->resolveEntry("TechnicPacks", QString("logos/%1").arg(logo));
    auto job = new NetJob(QString("Technic Icon Download %1").arg(logo), APPLICATION->network());
    job->setAskRetry(false);
    job->addNetAction(Net::ApiRequest::makeCached(QUrl(url), entry));

    auto fullPath = entry->getFullPath();

    connect(job, &NetJob::succeeded, this, [this, logo, fullPath, job] {
        job->deleteLater();
        logoLoaded(logo, fullPath);
    });

    connect(job, &NetJob::failed, this, [this, logo, job] {
        job->deleteLater();
        logoFailed(logo);
    });

    job->start();

    m_loadingLogos.append(logo);
}

std::optional<Zero::Content::TechnicPackDetails> Technic::ListModel::cachedDetails(const QString& slug) const
{
    return m_details->details(slug);
}

bool Technic::ListModel::requestDetails(const QString& slug)
{
    return m_details->requestDetails(slug);
}

std::optional<Zero::Content::TechnicSolderVersions> Technic::ListModel::cachedVersions(const QString& slug) const
{
    return m_details->versions(slug);
}

bool Technic::ListModel::requestVersions(const QString& slug, const QString& solderBase)
{
    return m_details->requestVersions(slug, solderBase);
}

void Technic::ListModel::fetchJson(const QString& name, const QUrl& url, std::function<void(Result<QByteArray>)> reply)
{
    auto job = makeShared<NetJob>(name, APPLICATION->network());
    // Background preview requests: failures go to the caller, never into a retry dialog.
    job->setAskRetry(false);
    auto [action, response] = Net::ApiRequest::makeByteArray(url);
    job->addNetAction(action);

    // Exactly one answer per request, whichever way the job ends.
    auto answered = std::make_shared<bool>(false);
    auto answer = [this, job = job.get(), answered, reply](Result<QByteArray> result) {
        if (*answered) {
            return;
        }
        *answered = true;
        m_detailJobs.remove(job);  // releases the job with deleteLater(): safe inside its own signal
        reply(std::move(result));
    };
    connect(job.get(), &NetJob::succeeded, this, [answer, response] {
        // NOTE(TheKodeToad): moving the response out to avoid it from being destroyed with the job
        answer(QByteArray(std::move(*response)));
    });
    connect(job.get(), &NetJob::failed, this, [answer](const QString& reason) { answer(std::unexpected(reason)); });
    connect(job.get(), &NetJob::aborted, this, [answer] { answer(std::unexpected(tr("The request was cancelled."))); });

    m_detailJobs.insert(job.get(), job);
    job->start();
}

void Technic::ListModel::fillFromDetails(Modpack& pack, const Zero::Content::TechnicPackDetails& details)
{
    pack.metadataLoaded = true;
    if (pack.name.trimmed().isEmpty()) {
        pack.name = Zero::Content::previewTitle(&details, pack.name, pack.slug);
    }
    pack.author = details.author;
    pack.description = details.description;
    pack.minecraftVersion = details.minecraftVersion;
    pack.currentVersion = details.version;
    pack.websiteUrl = details.platformUrl;
    pack.isSolder = details.isSolder();
    pack.url = details.installUrl();
}

void Technic::ListModel::applyDetailsToRows(const QString& slug)
{
    const auto details = m_details->details(slug);
    if (!details) {
        return;
    }
    for (int i = 0; i < modpacks.size(); i++) {
        if (modpacks[i].slug != slug) {
            continue;
        }
        fillFromDetails(modpacks[i], *details);
        emit dataChanged(index(i, 0), index(i, 0),
                         { Qt::DisplayRole, Qt::ToolTipRole, Qt::UserRole, UserDataTypes::TITLE, UserDataTypes::DESCRIPTION });
    }
}
