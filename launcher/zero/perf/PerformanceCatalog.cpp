// SPDX-License-Identifier: GPL-3.0-only
#include "PerformanceCatalog.h"

#include <QCoreApplication>

#include "modplatform/modrinth/ModrinthAPI.h"
#include "net/NetJob.h"
#include "zero/content/ModpackSources.h"

namespace Zero::Perf {

namespace {
QString trText(const char* text)
{
    return QCoreApplication::translate("Zero::Perf::PerformanceCatalog", text);
}
}  // namespace

ModrinthPerformanceCatalog::ModrinthPerformanceCatalog(QObject* parent)
    : PerformanceCatalog(parent), m_source(new Content::ModrinthSource(ModPlatform::ResourceType::Mod, this))
{
    qRegisterMetaType<Zero::Perf::FoundMap>("Zero::Perf::FoundMap");
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this]() {
        if (m_active) {
            fail(trText("Modrinth did not answer in time."));
        }
    });
    connect(m_source, &Content::ModpackSource::versionsFinished, this,
            [this](quint64 generation, const Content::ModpackItemPtr& item, const QList<Content::ModpackVersion>&) {
                const QString slug = m_generations.take(generation);
                if (!m_active || slug.isEmpty() || !item || !item->pack) {
                    return;  // an answer of a replaced request
                }
                Found found;
                found.slug = slug;
                found.pack = item->pack;
                found.versions = item->pack->versions;
                m_found.insert(slug, found);
                projectDone(slug);
            });
    connect(m_source, &Content::ModpackSource::versionsFailed, this, [this](quint64 generation, const QString& reason) {
        if (m_generations.take(generation).isEmpty() || !m_active) {
            return;
        }
        fail(reason);
    });
}

ModrinthPerformanceCatalog::~ModrinthPerformanceCatalog()
{
    cancel();
}

void ModrinthPerformanceCatalog::load(quint64 token, const Request& request)
{
    cancel();
    m_token = token;
    m_request = request;
    m_found.clear();
    m_pending = QSet<QString>(request.slugs.begin(), request.slugs.end());
    m_active = true;
    if (m_pending.isEmpty()) {
        m_active = false;
        emit loaded(token, m_found);
        return;
    }
    Content::VersionFilter filter;
    if (!request.mcVersion.isEmpty()) {
        filter.mcVersions = { request.mcVersion };
    }
    filter.loaders = request.loaders;
    m_source->setVersionFilter(filter);
    m_timeout.start(kTimeoutMs);
    for (const QString& slug : request.slugs) {
        startProject(slug);
        if (!m_active) {
            return;  // a failure answered at once
        }
    }
}

void ModrinthPerformanceCatalog::cancel()
{
    m_active = false;
    m_timeout.stop();
    m_generations.clear();
    m_pending.clear();
    const auto jobs = m_jobs;
    m_jobs.clear();
    for (auto it = jobs.cbegin(); it != jobs.cend(); ++it) {
        it.value()->abort();
    }
}

void ModrinthPerformanceCatalog::startProject(const QString& slug)
{
    auto [job, response] = ModrinthAPI::get().getProjectTask(slug, false, false);
    Task* raw = job.get();
    const quint64 token = m_token;
    connect(raw, &Task::succeeded, this, [this, raw, response, slug, token]() {
        const ModPlatform::IndexedPack details = *response;
        m_jobs.remove(raw);
        if (!m_active || token != m_token) {
            return;
        }
        requestVersions(slug, Content::itemFromPack(Content::Provider::Modrinth, details));
    });
    connect(raw, &Task::failed, this, [this, raw, token](const QString& reason) {
        m_jobs.remove(raw);
        if (m_active && token == m_token) {
            fail(reason);
        }
    });
    connect(raw, &Task::aborted, this, [this, raw]() { m_jobs.remove(raw); });
    m_jobs.insert(raw, job);
    job->start();
}

void ModrinthPerformanceCatalog::requestVersions(const QString& slug, const Content::ModpackItemPtr& item)
{
    const quint64 generation = ++m_nextGeneration;
    m_generations.insert(generation, slug);
    m_source->versions(item, generation);  // may answer at once
}

void ModrinthPerformanceCatalog::projectDone(const QString& slug)
{
    m_pending.remove(slug);
    if (!m_pending.isEmpty()) {
        return;
    }
    m_active = false;
    m_timeout.stop();
    emit loaded(m_token, m_found);
}

void ModrinthPerformanceCatalog::fail(const QString& reason)
{
    const quint64 token = m_token;
    cancel();
    emit failed(token, reason);
}

}  // namespace Zero::Perf
