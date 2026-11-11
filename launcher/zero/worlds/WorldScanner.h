// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - read-only world scanner for the Worlds hub. No widgets, no APPLICATION: the instances come from an injected
// provider. Reads `<instance>/saves/<world>/level.dat` through Prism's World class and measures folder sizes; it never
// writes, renames or deletes anything. See docs/zero/ARCHITECTURE.md ("Worlds Hub").
#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <functional>

namespace Zero {
namespace Worlds {

struct WorldEntry {
    QString instanceId;
    QString instanceName;
    QString folderPath;  ///< absolute path of the world folder
    QString folderName;
    QString name;           ///< level.dat LevelName (the folder name when absent)
    QString versionName;    ///< Data.Version.Name, empty when absent
    QString gameMode;       ///< translated display string: Survival / Creative / Adventure / Spectator; empty when unknown
    int gameTypeCode = -1;  ///< GameType::type (0 survival, 1 creative, 2 adventure, 3 spectator, -1 unknown)
    bool hardcore = false;
    qint64 lastPlayedMs = 0;
    qint64 seed = 0;
    QString iconPath;       ///< absolute path of icon.png, empty when the world has none
    qint64 sizeBytes = -1;  ///< -1 = not computed yet

    /// Stable identity across rescans: `<instanceId>/<folderName>`.
    QString key() const { return instanceId + QLatin1Char('/') + folderName; }

    bool operator==(const WorldEntry&) const = default;
};

/// One instance to scan (`savesDir` = the instance's `saves` folder; it may not exist).
struct InstanceSource {
    QString id;
    QString name;
    QString savesDir;
};

/// Every subfolder of `savesDir` that has a level.dat which parses (zip files, loose files and invalid folders are skipped, and so
/// are folders whose name starts with `.zero-`: leftovers of an interrupted Smart Backup restore; a missing `savesDir` gives an
/// empty list). Sizes are NOT computed (sizeBytes stays -1). Sorted by folder name. Pure.
QList<WorldEntry> scanWorldsDir(const QString& instanceId, const QString& instanceName, const QString& savesDir);

/// Total size in bytes of the regular files under `path`. Symbolic links and junctions are neither followed nor counted, so a
/// link back up the tree cannot loop. 0 for a missing folder.
qint64 folderSize(const QString& path);

/// Sort orders of the Worlds hub (persisted as "recent" | "name" | "size" | "instance" in ZeroWorldsSort).
enum class SortMode { Recent, Name, Size, Instance };

QString sortModeToString(SortMode mode);
/// Unknown / empty text gives SortMode::Recent.
SortMode sortModeFromString(const QString& text);

/// Case-insensitive substring search over the world name, folder name, instance name and version name. Empty text matches all.
bool matchesSearch(const WorldEntry& entry, const QString& text);

/// Stable sort: Recent = last played first; Name = A-Z; Size = biggest first (sizes not computed yet last); Instance = instance
/// name A-Z, then last played first. Ties fall back to the name, then the key.
void sortWorlds(QList<WorldEntry>& worlds, SortMode mode);

/// Async scanner. rescan() reads the sources on the GUI thread, scans on QThreadPool::globalInstance() and delivers the
/// result back to this object. Every rescan() starts a new generation: results of an older generation are dropped.
class WorldScanner : public QObject {
    Q_OBJECT
   public:
    /// Called on the GUI thread by every rescan().
    using SourceProvider = std::function<QList<InstanceSource>()>;

    explicit WorldScanner(SourceProvider provider, QObject* parent = nullptr);

    /// Starts a scan (replacing any scan in flight). Phase 1 emits worldsChanged() as soon as the metadata is read, phase 2
    /// computes the sizes (one world per job) and emits sizeReady() for each.
    void rescan();

    const QList<WorldEntry>& entries() const { return m_entries; }
    /// True from rescan() until the last size of the current generation arrived.
    bool isScanning() const { return m_scanning; }
    /// True once a scan delivered its worlds at least once.
    bool hasScanned() const { return m_hasScanned; }
    quint64 generation() const { return m_generation; }

   signals:
    /// The entry list was replaced (phase 1 of a scan). Sizes of worlds that were already known are carried over; new ones are -1.
    void worldsChanged();
    void sizeReady(const QString& key, qint64 bytes);
    void scanningChanged(bool scanning);

   private:
    void onScanned(quint64 generation, const QList<WorldEntry>& found);
    void onSize(quint64 generation, const QString& key, qint64 bytes);
    void setScanning(bool scanning);

    SourceProvider m_provider;
    QList<WorldEntry> m_entries;
    QHash<QString, int> m_index;  // key -> position in m_entries
    quint64 m_generation = 0;
    int m_pendingSizes = 0;
    bool m_scanning = false;
    bool m_hasScanned = false;
};

}  // namespace Worlds
}  // namespace Zero
