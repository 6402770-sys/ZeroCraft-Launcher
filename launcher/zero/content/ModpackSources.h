// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the production ModpackSource implementations over Prism's provider code (docs/zero/CONTENT.md, "Preserve
// engines"): ModrinthAPI / FlameAPI searches, project details and project versions, and the Technic API through
// TechnicMeta + TechnicDetailsStore. All requests run as NetJobs without a retry dialog; every answer (or failure) is a signal.
// Needs APPLICATION (network, metacache), so it is not unit-tested; the section is tested with fakes.
#pragma once

#include <QHash>
#include <QSet>
#include <QUrl>

#include <functional>

#include "Result.h"
#include "net/NetJob.h"
#include "zero/content/ModpackSource.h"
#include "zero/content/TechnicDetailsStore.h"

namespace Zero::Content {

/// Shared plumbing: owns the running jobs and fetches a URL as bytes.
class NetSource : public ModpackSource {
    Q_OBJECT
   public:
    using ModpackSource::ModpackSource;

    /// How long a versions answer may take before versionsFailed("timed out") - the provider callbacks never fire on a parse failure.
    static constexpr int kVersionsTimeoutMs = 20000;
    void setVersionsTimeoutMs(int ms) { m_versionsTimeoutMs = ms; }

   protected:
    void hold(const Task::Ptr& job);
    /// Lets go of a job (by pointer, so a signal handler never keeps its own job alive). Deleted with deleteLater().
    void release(Task* job);
    /// GETs `url` through Prism's network stack; `reply` is called exactly once, on the GUI thread.
    void fetch(const QString& jobName, const QUrl& url, std::function<void(Result<QByteArray>)> reply);

    int m_versionsTimeoutMs = kVersionsTimeoutMs;

   private:
    QHash<Task*, Task::Ptr> m_jobs;
};

/// `args` with the search text percent-encoded: Prism's Modrinth / CurseForge URL builders paste the text into the query string
/// unescaped, so "&", "#" or "%" would break the request. (QUrl's tolerant parsing keeps the %XX as they are.)
ResourceAPI::SearchArgs withEncodedSearch(ResourceAPI::SearchArgs args);

/// Modrinth and CurseForge: both speak ResourceAPI. `type` is the kind of project the source serves (its categories and its versions
/// requests); the search type travels in the SearchArgs.
class ResourceApiSource : public NetSource {
    Q_OBJECT
   public:
    ResourceApiSource(Provider provider, const ResourceAPI& api, ModPlatform::ResourceType type = ModPlatform::ResourceType::Modpack,
                      QObject* parent = nullptr);

    ModPlatform::ResourceType resourceType() const { return m_type; }

    Provider provider() const override { return m_provider; }
    void search(const ResourceAPI::SearchArgs& args, quint64 generation) override;
    void details(const ModpackItemPtr& item, quint64 generation) override;
    void versions(const ModpackItemPtr& item, quint64 generation) override;
    void versionsFor(const ModpackItemPtr& item, quint64 generation, const VersionFilter& filter) override;
    void changelog(const ModpackItemPtr& item, const QString& versionId, quint64 generation) override;
    void requestCategories() override;

   protected:
    /// The URL of one version's changelog and how to read it from the answer.
    virtual QUrl changelogUrl(const ModpackItem& item, const QString& versionId) const = 0;
    virtual QString parseChangelog(const QByteArray& data) const = 0;
    /// The provider's category request (`requestCategories()` runs it once).
    virtual std::pair<Task::Ptr, QByteArray*> categoriesTask() const = 0;
    virtual QList<ModPlatform::Category> parseCategories(const QByteArray& data) const = 0;

   private:
    /// The request behind versions() (`cache`: the answer is stored in the pack, signal versionsFinished) and versionsFor().
    void requestVersions(const ModpackItemPtr& item, quint64 generation, const VersionFilter& filter, bool cache);
    Provider m_provider;
    const ResourceAPI& m_api;
    ModPlatform::ResourceType m_type;
    bool m_categoriesRequested = false;
    Task::Ptr m_categoriesJob;
};

class ModrinthSource : public ResourceApiSource {
    Q_OBJECT
   public:
    explicit ModrinthSource(QObject* parent = nullptr) : ModrinthSource(ModPlatform::ResourceType::Modpack, parent) {}
    explicit ModrinthSource(ModPlatform::ResourceType type, QObject* parent = nullptr);

    /// As the base class, but first reads the project's original gallery pictures: Prism's parser keeps Modrinth's gallery `url` (a 350 px
    /// WebP) and drops `raw_url`; the picture viewer wants the original, the grid keeps the small one as the thumbnail.
    void details(const ModpackItemPtr& item, quint64 generation) override;

   protected:
    QUrl changelogUrl(const ModpackItem& item, const QString& versionId) const override;
    QString parseChangelog(const QByteArray& data) const override;
    std::pair<Task::Ptr, QByteArray*> categoriesTask() const override;
    QList<ModPlatform::Category> parseCategories(const QByteArray& data) const override;

   private:
    QHash<QString, QHash<QString, QString>> m_originals;  ///< project id -> (gallery url -> raw_url)
};

class CurseForgeSource : public ResourceApiSource {
    Q_OBJECT
   public:
    explicit CurseForgeSource(QObject* parent = nullptr) : CurseForgeSource(ModPlatform::ResourceType::Modpack, parent) {}
    explicit CurseForgeSource(ModPlatform::ResourceType type, QObject* parent = nullptr);

   protected:
    QUrl changelogUrl(const ModpackItem& item, const QString& versionId) const override;
    QString parseChangelog(const QByteArray& data) const override;
    std::pair<Task::Ptr, QByteArray*> categoriesTask() const override;
    QList<ModPlatform::Category> parseCategories(const QByteArray& data) const override;
};

/// Technic: `trending` / `search?q=` for the list (one page), details and Solder builds through TechnicDetailsStore.
class TechnicSource : public NetSource {
    Q_OBJECT
   public:
    explicit TechnicSource(QObject* parent = nullptr);

    Provider provider() const override { return Provider::Technic; }
    bool pages() const override { return false; }
    void search(const ResourceAPI::SearchArgs& args, quint64 generation) override;
    void details(const ModpackItemPtr& item, quint64 generation) override;
    void versions(const ModpackItemPtr& item, quint64 generation) override;
    void changelog(const ModpackItemPtr& item, const QString& versionId, quint64 generation) override;

   private:
    struct Pending {
        quint64 generation = 0;
        ModpackItemPtr item;
        bool wantsVersions = false;
        std::shared_ptr<bool> done;
    };

    void onDetailsReady(const QString& slug);
    void onDetailsFailed(const QString& slug, const QString& reason);
    void onVersionsReady(const QString& slug);
    void onVersionsFailed(const QString& slug, const QString& reason);
    void answerVersions(const Pending& pending);
    void ensureDetails(const QString& slug);

    TechnicDetailsStore* m_store;
    QHash<QString, QList<Pending>> m_pending;  ///< waiting for the pack details
    QHash<QString, QList<Pending>> m_builds;   ///< waiting for the Solder build list
};

}  // namespace Zero::Content
