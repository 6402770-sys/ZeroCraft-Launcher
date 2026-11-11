// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - per-slug cache of Technic pack details and Solder version lists (see TechnicDetailsStore.h).
#include "TechnicDetailsStore.h"

#include <QCoreApplication>
#include <QPointer>

namespace Zero {
namespace Content {

TechnicDetailsStore::TechnicDetailsStore(DetailsFetcher detailsFetcher, VersionsFetcher versionsFetcher, QObject* parent)
    : QObject(parent), m_detailsFetcher(std::move(detailsFetcher)), m_versionsFetcher(std::move(versionsFetcher))
{}

std::optional<TechnicPackDetails> TechnicDetailsStore::details(const QString& slug) const
{
    const auto it = m_details.constFind(slug);
    if (it == m_details.constEnd()) {
        return std::nullopt;
    }
    return *it;
}

bool TechnicDetailsStore::requestDetails(const QString& slug)
{
    if (m_details.contains(slug)) {
        return true;
    }
    if (m_detailsInFlight.contains(slug)) {
        return false;
    }
    if (slug.isEmpty() || !m_detailsFetcher) {
        emit detailsFailed(slug, QCoreApplication::translate("TechnicMeta", "This pack has no Technic name."));
        return false;
    }
    const quint64 token = m_nextToken++;
    m_detailsInFlight.insert(slug, token);
    QPointer<TechnicDetailsStore> self(this);
    m_detailsFetcher(slug, [self, slug, token](Result<QByteArray> reply) {
        if (self) {
            self->onDetailsReply(slug, token, reply);
        }
    });
    return false;
}

void TechnicDetailsStore::insertDetails(const TechnicPackDetails& details)
{
    if (details.slug.isEmpty()) {
        return;
    }
    m_detailsInFlight.remove(details.slug);
    m_details.insert(details.slug, details);
    emit detailsReady(details.slug);
}

void TechnicDetailsStore::onDetailsReply(const QString& slug, quint64 token, const Result<QByteArray>& reply)
{
    const auto it = m_detailsInFlight.constFind(slug);
    if (it == m_detailsInFlight.constEnd() || *it != token) {
        return;  // answered twice, or superseded
    }
    m_detailsInFlight.remove(slug);

    if (!reply) {
        emit detailsFailed(slug, reply.error());
        return;
    }
    auto parsed = parsePackDetails(*reply);
    if (!parsed) {
        emit detailsFailed(slug, parsed.error());
        return;
    }
    if (parsed->slug != slug) {
        // The API answers with the canonical slug; keep the requested one as the key so the caller finds it.
        qDebug() << "Technic answered" << slug << "with slug" << parsed->slug;
        parsed->slug = slug;
    }
    m_details.insert(slug, *parsed);
    emit detailsReady(slug);
}

std::optional<TechnicSolderVersions> TechnicDetailsStore::versions(const QString& slug) const
{
    const auto it = m_versions.constFind(slug);
    if (it == m_versions.constEnd()) {
        return std::nullopt;
    }
    return *it;
}

bool TechnicDetailsStore::requestVersions(const QString& slug, const QString& solderBase)
{
    if (m_versions.contains(slug)) {
        return true;
    }
    if (m_versionsInFlight.contains(slug)) {
        return false;
    }
    if (slug.isEmpty() || solderBase.trimmed().isEmpty() || !m_versionsFetcher) {
        emit versionsFailed(slug, QCoreApplication::translate("TechnicMeta", "This pack has no Solder API."));
        return false;
    }
    const quint64 token = m_nextToken++;
    m_versionsInFlight.insert(slug, token);
    QPointer<TechnicDetailsStore> self(this);
    m_versionsFetcher(slug, solderBase, [self, slug, token](Result<QByteArray> reply) {
        if (self) {
            self->onVersionsReply(slug, token, reply);
        }
    });
    return false;
}

void TechnicDetailsStore::onVersionsReply(const QString& slug, quint64 token, const Result<QByteArray>& reply)
{
    const auto it = m_versionsInFlight.constFind(slug);
    if (it == m_versionsInFlight.constEnd() || *it != token) {
        return;
    }
    m_versionsInFlight.remove(slug);

    if (!reply) {
        emit versionsFailed(slug, reply.error());
        return;
    }
    auto parsed = parseSolderVersions(*reply);
    if (!parsed) {
        emit versionsFailed(slug, parsed.error());
        return;
    }
    m_versions.insert(slug, *parsed);
    emit versionsReady(slug);
}

}  // namespace Content
}  // namespace Zero
