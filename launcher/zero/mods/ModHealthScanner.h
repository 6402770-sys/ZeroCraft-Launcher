// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Mod Intelligence: async wrapper. Lists the mods folder, parses the jars and runs the analysis on the global thread
// pool; read-only. Only files directly inside the mods folder count: `*.jar` is enabled, `*.jar.disabled` / `*.disabled` is disabled;
// symbolic links are skipped.
#pragma once

#include <QObject>
#include <QString>

#include "zero/mods/ModHealth.h"

namespace Zero {
namespace Mods {

/// Synchronous core of the scanner (what the pool job runs): list `modsDir`, parse, analyze with the context read from
/// `<instanceRoot>/mmc-pack.json`. Exposed for tests and for callers that already run on a worker thread.
Report scanModsFolder(const QString& modsDir, const InstanceContext& context);

class ModHealthScanner : public QObject {
    Q_OBJECT
   public:
    explicit ModHealthScanner(QObject* parent = nullptr);

    /// Starts a scan on `QThreadPool::globalInstance()`. A newer call supersedes older ones: only the latest emits `reportReady`.
    void analyze(const QString& instanceId, const QString& modsDir, const QString& instanceRoot);

    bool isScanning() const { return m_scanning; }
    quint64 generation() const { return m_generation; }

   signals:
    void reportReady(const QString& instanceId, const Zero::Mods::Report& report);

   private:
    void onReport(quint64 generation, const QString& instanceId, const Report& report);

    quint64 m_generation = 0;
    bool m_scanning = false;
};

}  // namespace Mods
}  // namespace Zero
