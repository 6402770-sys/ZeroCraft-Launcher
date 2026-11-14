// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - where "Boost performance" learns what Modrinth has for this Minecraft version + loader. `PerformanceCatalog` is the
// small async seam the dialog asks (tests derive fakes, no network); `ModrinthPerformanceCatalog` is the production one: for every slug the
// project details (name, icon, the real project id), then its versions filtered by the target's Minecraft version and loaders, through the
// same ModrinthSource the Discover > Mods section uses. All answers echo the caller's token, so an answer for a replaced request can be
// dropped; failures are signals (the dialog shows them inline with Retry), never modal boxes.
#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "tasks/Task.h"
#include "zero/content/ModpackSource.h"
#include "zero/perf/PerformanceProfile.h"

namespace Zero::Content {
class ModrinthSource;
}

namespace Zero::Perf {

class PerformanceCatalog : public QObject {
    Q_OBJECT
   public:
    struct Request {
        QStringList slugs;  ///< the projects to look up (slugsFor(loader))
        QString mcVersion;
        ModPlatform::ModLoaderTypes loaders;
    };

    using QObject::QObject;
    ~PerformanceCatalog() override = default;

    /// Starts the lookup of `request`; answers loaded() or failed() with `token`. A new call replaces the one running (which never answers).
    virtual void load(quint64 token, const Request& request) = 0;
    /// Stops the lookup; nothing is answered for it.
    virtual void cancel() = 0;

   signals:
    void loaded(quint64 token, const Zero::Perf::FoundMap& found);
    void failed(quint64 token, const QString& reason);
};

class ModrinthPerformanceCatalog : public PerformanceCatalog {
    Q_OBJECT
   public:
    static constexpr int kTimeoutMs = 30000;  ///< the whole lookup; a provider that never answers ends as a failure

    explicit ModrinthPerformanceCatalog(QObject* parent = nullptr);
    ~ModrinthPerformanceCatalog() override;

    void load(quint64 token, const Request& request) override;
    void cancel() override;

   private:
    void startProject(const QString& slug);
    void requestVersions(const QString& slug, const Zero::Content::ModpackItemPtr& item);
    void projectDone(const QString& slug);
    void fail(const QString& reason);

    Zero::Content::ModrinthSource* m_source;
    quint64 m_token = 0;
    bool m_active = false;
    Request m_request;
    FoundMap m_found;
    QSet<QString> m_pending;                 ///< slugs not answered yet
    QHash<quint64, QString> m_generations;   ///< version request generation -> slug
    quint64 m_nextGeneration = 0x7065726600000000ULL;
    QHash<Task*, Task::Ptr> m_jobs;          ///< the project requests in flight
    QTimer m_timeout;
};

}  // namespace Zero::Perf

Q_DECLARE_METATYPE(Zero::Perf::Found)
Q_DECLARE_METATYPE(Zero::Perf::FoundMap)
