// SPDX-License-Identifier: GPL-3.0-only
#include "ModpackSources.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>

#include <memory>

#include "Application.h"
#include "BuildConfig.h"
#include "Json.h"
#include "Markdown.h"
#include "QObjectPtr.h"
#include "StringUtils.h"
#include "modplatform/flame/FlameAPI.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "net/ApiRequest.h"
#include "settings/SettingsObject.h"

namespace Zero::Content {

namespace {
QString trText(const char* text)
{
    return QCoreApplication::translate("Zero::Content::ModpackSources", text);
}
}  // namespace

// ---- NetSource ----

void NetSource::hold(const Task::Ptr& job)
{
    m_jobs.insert(job.get(), job);
}

void NetSource::release(Task* job)
{
    // shared_qobject_ptr deletes with deleteLater(): safe inside the job's own signal.
    m_jobs.remove(job);
}

void NetSource::fetch(const QString& jobName, const QUrl& url, std::function<void(Result<QByteArray>)> reply)
{
    auto job = makeShared<NetJob>(jobName, APPLICATION->network());
    job->setAskRetry(false);  // browsing: failures are shown inline, never as a retry dialog
    auto [action, response] = Net::ApiRequest::makeByteArray(url);
    job->addNetAction(action);

    auto answered = std::make_shared<bool>(false);
    auto answer = [this, raw = job.get(), answered, reply](Result<QByteArray> result) {
        if (*answered) {
            return;
        }
        *answered = true;
        m_jobs.remove(raw);
        reply(std::move(result));
    };
    connect(job.get(), &NetJob::succeeded, this, [answer, response] { answer(QByteArray(*response)); });
    connect(job.get(), &NetJob::failed, this, [answer](const QString& reason) { answer(std::unexpected(reason)); });
    connect(job.get(), &NetJob::aborted, this, [answer] { answer(std::unexpected(trText("The request was cancelled."))); });
    hold(job);
    job->start();
}

// ---- Modrinth / CurseForge ----

ResourceAPI::SearchArgs withEncodedSearch(ResourceAPI::SearchArgs args)
{
    if (args.search.has_value()) {
        args.search = QString::fromLatin1(QUrl::toPercentEncoding(*args.search));
    }
    return args;
}

ResourceApiSource::ResourceApiSource(Provider provider, const ResourceAPI& api, ModPlatform::ResourceType type, QObject* parent)
    : NetSource(parent), m_provider(provider), m_api(api), m_type(type)
{}

void ResourceApiSource::search(const ResourceAPI::SearchArgs& args, quint64 generation)
{
    auto [job, response] = m_api.searchProjectsTask(withEncodedSearch(args));
    job->setAskRetry(false);
    const int offset = args.offset;
    const Provider provider = m_provider;
    NetJob* raw = job.get();
    connect(raw, &NetJob::succeeded, this, [this, raw, response, generation, offset, provider]() {
        const QList<ModPlatform::IndexedPack> hits = *response;
        QList<ModpackItemPtr> items;
        items.reserve(hits.size());
        for (const auto& hit : hits) {
            items.append(itemFromPack(provider, hit));
        }
        release(raw);
        emit searchFinished(generation, offset, items, int(hits.size()));
    });
    connect(raw, &NetJob::failed, this, [this, raw, generation](const QString& reason) {
        release(raw);
        emit searchFailed(generation, reason);
    });
    connect(raw, &NetJob::aborted, this, [this, raw, generation]() {
        release(raw);
        emit searchFailed(generation, trText("The request was cancelled."));
    });
    hold(job);
    job->start();
}

void ResourceApiSource::details(const ModpackItemPtr& item, quint64 generation)
{
    auto [job, response] = m_api.getProjectTask(item->id, true, false);
    NetJob* raw = job.get();
    connect(raw, &NetJob::succeeded, this, [this, raw, response, item, generation]() {
        const ModPlatform::IndexedPack details = *response;
        release(raw);
        applyProjectDetails(*item, details);
        emit detailsFinished(generation, item);
    });
    connect(raw, &NetJob::failed, this, [this, raw, generation](const QString& reason) {
        release(raw);
        emit detailsFailed(generation, reason);
    });
    connect(raw, &NetJob::aborted, this, [this, raw, generation]() {
        release(raw);
        emit detailsFailed(generation, trText("The request was cancelled."));
    });
    hold(job);
    job->start();
}

void ResourceApiSource::versions(const ModpackItemPtr& item, quint64 generation)
{
    if (!item->pack) {
        emit versionsFailed(generation, trText("This pack has no version list."));
        return;
    }
    // ResourceAPI::getProjectVersions never calls back when the answer cannot be parsed: whatever happens first (answer,
    // failure, timeout) settles the request, and the timer settles it as a failure.
    auto done = std::make_shared<bool>(false);
    ResourceAPI::Callback<QVector<ModPlatform::IndexedVersion>> callbacks;
    callbacks.onSucceed = [this, item, generation, done](QVector<ModPlatform::IndexedVersion>& found) {
        if (*done) {
            return;
        }
        *done = true;
        item->pack->versions = QList<ModPlatform::IndexedVersion>(found.begin(), found.end());
        item->pack->versionsLoaded = true;
        QList<ModpackVersion> out;
        out.reserve(found.size());
        for (const auto& v : found) {
            out.append(versionFromIndexed(v));
        }
        emit versionsFinished(generation, item, out);
    };
    callbacks.onFail = [this, generation, done](const QString& reason, int) {
        if (*done) {
            return;
        }
        *done = true;
        emit versionsFailed(generation, reason);
    };
    callbacks.onAbort = [this, generation, done]() {
        if (*done) {
            return;
        }
        *done = true;
        emit versionsFailed(generation, trText("The request was cancelled."));
    };
    // Modpacks ask for every version; a section with a target instance (Mods) narrows the request to its Minecraft version and loaders.
    const VersionFilter& filter = versionFilter();
    std::optional<std::vector<Version>> mcVersions;
    if (!filter.mcVersions.isEmpty()) {
        mcVersions.emplace();
        for (const QString& version : filter.mcVersions) {
            mcVersions->emplace_back(version);
        }
    }
    std::optional<ModPlatform::ModLoaderTypes> loaders;
    if (filter.loaders != ModPlatform::ModLoaderTypes()) {
        loaders = filter.loaders;
    }
    auto job = m_api.getProjectVersions(
        { .pack = item->pack, .mcVersions = mcVersions, .loaders = loaders, .resourceType = m_type, .includeChangelog = false }, callbacks);
    if (!job) {
        *done = true;
        emit versionsFailed(generation, trText("This pack has no version list."));
        return;
    }
    if (auto* net = dynamic_cast<NetJob*>(job.get())) {
        net->setAskRetry(false);
        connect(net, &NetJob::finished, this, [this, net]() { release(net); });
    }
    hold(job);
    job->start();
    QTimer::singleShot(m_versionsTimeoutMs, this, [this, generation, done]() {
        if (*done) {
            return;
        }
        *done = true;
        emit versionsFailed(generation, trText("The provider did not answer in time."));
    });
}

void ResourceApiSource::changelog(const ModpackItemPtr& item, const QString& versionId, quint64 generation)
{
    fetch(QStringLiteral("Zero::Changelog"), changelogUrl(*item, versionId), [this, generation, versionId](Result<QByteArray> result) {
        if (!result) {
            emit changelogFailed(generation, versionId, result.error());
            return;
        }
        emit changelogFinished(generation, versionId, parseChangelog(*result));
    });
}

void ResourceApiSource::requestCategories()
{
    if (m_categoriesRequested) {
        return;
    }
    m_categoriesRequested = true;
    auto [task, response] = categoriesTask();
    m_categoriesJob = task;
    connect(task.get(), &Task::succeeded, this, [this, response]() {
        m_categories = parseCategories(*response);
        emit categoriesReady();
    });
    connect(task.get(), &Task::failed, this, [this]() { m_categoriesRequested = false; });  // asked again with the next request
    task->start();
}

// ---- Modrinth ----

ModrinthSource::ModrinthSource(ModPlatform::ResourceType type, QObject* parent)
    : ResourceApiSource(Provider::Modrinth, ModrinthAPI::get(), type, parent)
{
    // Runs before the section's handler (connected earlier): the details answer replaces the gallery with the 350 px pictures, and the
    // originals found before are put back as the viewer's pictures.
    connect(this, &ModpackSource::detailsFinished, this, [this](quint64, const ModpackItemPtr& item) {
        const auto originals = m_originals.take(item->id);
        if (!item->pack || originals.isEmpty()) {
            return;
        }
        for (auto& image : item->pack->gallery) {
            const QString raw = originals.value(image.url);
            if (!raw.isEmpty() && raw != image.url) {
                image.thumbnailUrl = image.url;
                image.url = raw;
            }
        }
    });
}

void ModrinthSource::details(const ModpackItemPtr& item, quint64 generation)
{
    const QUrl url(BuildConfig.MODRINTH_PROD_URL + QStringLiteral("/project/") + item->id);
    fetch(QStringLiteral("Modrinth::GalleryOriginals"), url, [this, item, generation](Result<QByteArray> result) {
        if (result) {
            QHash<QString, QString> originals;
            for (const auto& value : QJsonDocument::fromJson(*result).object().value(QStringLiteral("gallery")).toArray()) {
                const QJsonObject entry = value.toObject();
                const QString raw = entry.value(QStringLiteral("raw_url")).toString();
                if (!raw.isEmpty()) {
                    originals.insert(entry.value(QStringLiteral("url")).toString(), raw);
                }
            }
            m_originals.insert(item->id, originals);
        }
        ResourceApiSource::details(item, generation);  // without the originals when this request failed: the 350 px pictures
    });
}

QUrl ModrinthSource::changelogUrl(const ModpackItem&, const QString& versionId) const
{
    return QUrl(BuildConfig.MODRINTH_PROD_URL + QStringLiteral("/version/") + versionId);
}

QString ModrinthSource::parseChangelog(const QByteArray& data) const
{
    const auto doc = QJsonDocument::fromJson(data);
    const QString markdown = doc.object().value(QStringLiteral("changelog")).toString();
    return markdown.trimmed().isEmpty() ? QString() : StringUtils::htmlListPatch(markdownToHTML(markdown));
}

std::pair<Task::Ptr, QByteArray*> ModrinthSource::categoriesTask() const
{
    return ModrinthAPI::getCategories();
}

QList<ModPlatform::Category> ModrinthSource::parseCategories(const QByteArray& data) const
{
    return ModrinthAPI::loadCategories(data, resourceType());
}

// ---- CurseForge ----

CurseForgeSource::CurseForgeSource(ModPlatform::ResourceType type, QObject* parent)
    : ResourceApiSource(Provider::CurseForge, FlameAPI::get(), type, parent)
{}

QUrl CurseForgeSource::changelogUrl(const ModpackItem& item, const QString& versionId) const
{
    return QUrl(BuildConfig.FLAME_BASE_URL + QStringLiteral("/mods/%1/files/%2/changelog").arg(item.id, versionId));
}

QString CurseForgeSource::parseChangelog(const QByteArray& data) const
{
    const auto doc = QJsonDocument::fromJson(data);
    const QString html = doc.object().value(QStringLiteral("data")).toString();
    return html.trimmed().isEmpty() ? QString() : StringUtils::htmlListPatch(html);
}

std::pair<Task::Ptr, QByteArray*> CurseForgeSource::categoriesTask() const
{
    return FlameAPI::getCategories(resourceType());
}

QList<ModPlatform::Category> CurseForgeSource::parseCategories(const QByteArray& data) const
{
    return FlameAPI::loadCategories(data);
}

// ---- Technic ----

TechnicSource::TechnicSource(QObject* parent) : NetSource(parent)
{
    auto fetchDetails = [this](const QString& slug, TechnicDetailsStore::Reply reply) {
        const QString url = QStringLiteral("%1modpack/%2?build=%3")
                                .arg(BuildConfig.TECHNIC_API_BASE_URL, QString::fromLatin1(QUrl::toPercentEncoding(slug)),
                                     BuildConfig.TECHNIC_API_BUILD);
        fetch(QStringLiteral("Technic::PackMeta(%1)").arg(slug), QUrl(url), std::move(reply));
    };
    auto fetchVersions = [this](const QString& slug, const QString& solderBase, TechnicDetailsStore::Reply reply) {
        fetch(QStringLiteral("Technic::SolderMeta(%1)").arg(slug), QUrl(solderPackUrl(solderBase, slug)), std::move(reply));
    };
    m_store = new TechnicDetailsStore(fetchDetails, fetchVersions, this);
    connect(m_store, &TechnicDetailsStore::detailsReady, this, &TechnicSource::onDetailsReady);
    connect(m_store, &TechnicDetailsStore::detailsFailed, this, &TechnicSource::onDetailsFailed);
    connect(m_store, &TechnicDetailsStore::versionsReady, this, &TechnicSource::onVersionsReady);
    connect(m_store, &TechnicDetailsStore::versionsFailed, this, &TechnicSource::onVersionsFailed);
}

void TechnicSource::search(const ResourceAPI::SearchArgs& args, quint64 generation)
{
    const QString query = args.search.value_or(QString()).trimmed();
    QString url;
    if (query.isEmpty()) {
        url = QStringLiteral("%1trending?build=%2").arg(BuildConfig.TECHNIC_API_BASE_URL, BuildConfig.TECHNIC_API_BUILD);
    } else {
        url = QStringLiteral("%1search?build=%2&q=%3")
                  .arg(BuildConfig.TECHNIC_API_BASE_URL, BuildConfig.TECHNIC_API_BUILD,
                       QString::fromLatin1(QUrl::toPercentEncoding(query)));
    }
    if (const QString clientId = APPLICATION->settings()->get("TechnicClientID").toString(); !clientId.isEmpty()) {
        url += QStringLiteral("&cid=") + clientId;
    }
    fetch(QStringLiteral("Technic::Search"), QUrl(url), [this, generation](Result<QByteArray> result) {
        if (!result) {
            emit searchFailed(generation, result.error());
            return;
        }
        const auto packs = parseSearchResponse(*result);
        if (!packs) {
            emit searchFailed(generation, packs.error());
            return;
        }
        QList<ModpackItemPtr> items;
        for (const auto& summary : *packs) {
            auto item = itemFromTechnicSummary(summary);
            if (const auto cached = m_store->details(summary.slug)) {
                applyTechnicDetails(*item, *cached);
            }
            items.append(item);
        }
        emit searchFinished(generation, 0, items, int(items.size()));
    });
}

void TechnicSource::ensureDetails(const QString& slug)
{
    m_store->requestDetails(slug);  // answers detailsReady / detailsFailed (possibly before it returns)
}

void TechnicSource::details(const ModpackItemPtr& item, quint64 generation)
{
    if (const auto cached = m_store->details(item->slug)) {
        applyTechnicDetails(*item, *cached);
        emit detailsFinished(generation, item);
        return;
    }
    Pending pending;
    pending.generation = generation;
    pending.item = item;
    pending.done = std::make_shared<bool>(false);
    m_pending[item->slug].append(pending);
    ensureDetails(item->slug);
}

void TechnicSource::versions(const ModpackItemPtr& item, quint64 generation)
{
    Pending pending;
    pending.generation = generation;
    pending.item = item;
    pending.wantsVersions = true;
    pending.done = std::make_shared<bool>(false);
    QTimer::singleShot(m_versionsTimeoutMs, this, [this, pending]() {
        if (*pending.done) {
            return;
        }
        *pending.done = true;
        emit versionsFailed(pending.generation, trText("The provider did not answer in time."));
    });
    if (m_store->details(item->slug)) {
        answerVersions(pending);
        return;
    }
    m_pending[item->slug].append(pending);
    ensureDetails(item->slug);
}

void TechnicSource::changelog(const ModpackItemPtr&, const QString& versionId, quint64 generation)
{
    // Technic has no per-version changelog (its news feed is part of the details).
    emit changelogFinished(generation, versionId, QString());
}

void TechnicSource::onDetailsReady(const QString& slug)
{
    const auto details = m_store->details(slug);
    if (!details) {
        return;
    }
    const QList<Pending> waiting = m_pending.take(slug);
    for (const auto& pending : waiting) {
        if (*pending.done) {
            continue;
        }
        applyTechnicDetails(*pending.item, *details);
        if (pending.wantsVersions) {
            answerVersions(pending);
        } else {
            *pending.done = true;
            emit detailsFinished(pending.generation, pending.item);
        }
    }
}

void TechnicSource::onDetailsFailed(const QString& slug, const QString& reason)
{
    const QList<Pending> waiting = m_pending.take(slug);
    for (const auto& pending : waiting) {
        if (*pending.done) {
            continue;
        }
        *pending.done = true;
        if (pending.wantsVersions) {
            emit versionsFailed(pending.generation, reason);
        } else {
            emit detailsFailed(pending.generation, reason);
        }
    }
}

void TechnicSource::answerVersions(const Pending& pending)
{
    if (*pending.done) {
        return;
    }
    const QString slug = pending.item->slug;
    const auto details = m_store->details(slug);
    if (!details) {
        *pending.done = true;
        emit versionsFailed(pending.generation, trText("The pack details are not available."));
        return;
    }
    if (!details->isSolder()) {
        // A single-zip pack has one version: the one the details name.
        ModpackVersion version;
        version.id = details->version;
        version.name = details->version;
        version.gameVersions = QStringList{ details->minecraftVersion };
        version.downloadUrl = details->downloadUrl;
        QList<ModpackVersion> list;
        if (!version.name.isEmpty()) {
            list.append(version);
        }
        *pending.done = true;
        emit versionsFinished(pending.generation, pending.item, list);
        return;
    }
    if (const auto builds = m_store->versions(slug)) {
        // Newest build first; the Solder build list is oldest first.
        QList<ModpackVersion> list;
        for (auto i = builds->builds.size(); (i--) != 0;) {
            ModpackVersion v;
            v.id = builds->builds.at(i);
            v.name = builds->builds.at(i);
            v.gameVersions = QStringList{ details->minecraftVersion };
            v.recommended = v.id == builds->recommended;
            list.append(v);
        }
        *pending.done = true;
        emit versionsFinished(pending.generation, pending.item, list);
        return;
    }
    m_builds[slug].append(pending);
    m_store->requestVersions(slug, details->installUrl());
}

void TechnicSource::onVersionsReady(const QString& slug)
{
    const QList<Pending> waiting = m_builds.take(slug);
    for (const auto& pending : waiting) {
        answerVersions(pending);
    }
}

void TechnicSource::onVersionsFailed(const QString& slug, const QString& reason)
{
    const QList<Pending> waiting = m_builds.take(slug);
    for (const auto& pending : waiting) {
        if (*pending.done) {
            continue;
        }
        *pending.done = true;
        emit versionsFailed(pending.generation, reason);
    }
}

}  // namespace Zero::Content
