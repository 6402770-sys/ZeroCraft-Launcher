// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - per-slug cache of Technic pack details and Solder version lists. The network is injected as two fetchers,
// so tests can answer requests late, out of order or not at all.
//
// Rules: a cached slug is never fetched again; requests for a slug already in flight join it (one fetch, one signal); a
// failure is not cached, so the next request for that slug fetches again. A reply that does not belong to the request in
// flight for its slug (answered twice, or after a newer request replaced it) is dropped. The store answers for every slug it
// was asked about; deciding which slug is still wanted (TechnicSelection) is the caller's job.
#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

#include "Result.h"
#include "zero/content/TechnicMeta.h"

namespace Zero {
namespace Content {

class TechnicDetailsStore : public QObject {
    Q_OBJECT
   public:
    /// Delivers a raw response body, or a network error as the error value. Must be called at most once.
    using Reply = std::function<void(Result<QByteArray>)>;
    /// Fetches `modpack/<slug>` from the Technic API.
    using DetailsFetcher = std::function<void(const QString& slug, Reply reply)>;
    /// Fetches `<solderBase>/modpack/<slug>` from a Solder API.
    using VersionsFetcher = std::function<void(const QString& slug, const QString& solderBase, Reply reply)>;

    TechnicDetailsStore(DetailsFetcher detailsFetcher, VersionsFetcher versionsFetcher, QObject* parent = nullptr);

    /// Cached details for `slug`, if loaded.
    std::optional<TechnicPackDetails> details(const QString& slug) const;
    bool isLoadingDetails(const QString& slug) const { return m_detailsInFlight.contains(slug); }
    /// Returns true when the details are already cached (nothing is fetched or emitted then). Otherwise starts a fetch, or
    /// joins the one in flight, and detailsReady() or detailsFailed() follows for `slug` (possibly before this returns, when
    /// the fetcher answers synchronously). An empty slug fails right away.
    bool requestDetails(const QString& slug);
    /// Caches details obtained elsewhere (e.g. a search by slug that already returned them) and emits detailsReady(). A
    /// fetch in flight for the same slug is superseded: its reply is dropped.
    void insertDetails(const TechnicPackDetails& details);

    /// Cached Solder versions for `slug`, if loaded.
    std::optional<TechnicSolderVersions> versions(const QString& slug) const;
    bool isLoadingVersions(const QString& slug) const { return m_versionsInFlight.contains(slug); }
    /// Like requestDetails(), for the Solder build list of `slug` at `solderBase`.
    bool requestVersions(const QString& slug, const QString& solderBase);

   signals:
    void detailsReady(const QString& slug);
    void detailsFailed(const QString& slug, const QString& reason);
    void versionsReady(const QString& slug);
    void versionsFailed(const QString& slug, const QString& reason);

   private:
    void onDetailsReply(const QString& slug, quint64 token, const Result<QByteArray>& reply);
    void onVersionsReply(const QString& slug, quint64 token, const Result<QByteArray>& reply);

    DetailsFetcher m_detailsFetcher;
    VersionsFetcher m_versionsFetcher;
    QHash<QString, TechnicPackDetails> m_details;
    QHash<QString, TechnicSolderVersions> m_versions;
    QHash<QString, quint64> m_detailsInFlight;  ///< slug -> token of the request in flight
    QHash<QString, quint64> m_versionsInFlight;
    quint64 m_nextToken = 1;
};

}  // namespace Content
}  // namespace Zero
