// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - read-only screenshot scanner for the Screenshot Gallery. No widgets, no APPLICATION: the instances come from an
// injected provider. It only lists `<gameRoot>/screenshots/*.png|jpg|jpeg`; it never writes, renames or deletes anything.
// See docs/zero/ARCHITECTURE.md ("Screenshot Gallery").
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <functional>

namespace Zero {
namespace Gallery {

struct ScreenshotEntry {
    QString instanceId;
    QString instanceName;
    QString path;      ///< absolute path of the image
    QString fileName;  ///< file name with extension
    qint64 modifiedMs = 0;
    qint64 sizeBytes = 0;
};

/// One instance to scan (`screenshotsDir` = `<gameRoot>/screenshots`; it may not exist).
struct InstanceSource {
    QString id;
    QString name;
    QString screenshotsDir;
};

/// Every `*.png`, `*.jpg`, `*.jpeg` (case-insensitive) directly inside `dir`. Symbolic links, junctions, folders and other
/// non-files are skipped; a missing `dir` gives an empty list. Sorted by file name. Pure, read-only.
QList<ScreenshotEntry> scanScreenshotsDir(const QString& instanceId, const QString& instanceName, const QString& dir);

/// Sort orders of the gallery (persisted as "newest" | "oldest" | "instance" in ZeroScreenshotsSort).
enum class SortMode { Newest, Oldest, Instance };

QString sortModeToString(SortMode mode);
/// Unknown / empty text gives SortMode::Newest.
SortMode sortModeFromString(const QString& text);

/// Case-insensitive substring search over the file name and the instance name. Empty text matches all.
bool matchesSearch(const ScreenshotEntry& entry, const QString& text);

/// Stable sort: Newest = latest modified first; Oldest = earliest first; Instance = instance name A-Z, then newest first.
/// Ties fall back to the file name, then the path.
void sortScreenshots(QList<ScreenshotEntry>& list, SortMode mode);

/// Async scanner. rescan() reads the sources on the GUI thread, scans on QThreadPool::globalInstance() and delivers the
/// result back to this object. Every rescan() starts a new generation: results of an older generation are dropped.
class ScreenshotScanner : public QObject {
    Q_OBJECT
   public:
    /// Called on the GUI thread by every rescan().
    using SourceProvider = std::function<QList<InstanceSource>()>;

    explicit ScreenshotScanner(SourceProvider provider, QObject* parent = nullptr);

    void rescan();

    const QList<ScreenshotEntry>& entries() const { return m_entries; }
    bool isScanning() const { return m_scanning; }
    /// True once a scan delivered its result at least once.
    bool hasScanned() const { return m_hasScanned; }
    quint64 generation() const { return m_generation; }

   signals:
    /// The entry list was replaced.
    void listChanged();
    void scanningChanged(bool scanning);

   private:
    void onScanned(quint64 generation, const QList<ScreenshotEntry>& found);
    void setScanning(bool scanning);

    SourceProvider m_provider;
    QList<ScreenshotEntry> m_entries;
    quint64 m_generation = 0;
    bool m_scanning = false;
    bool m_hasScanned = false;
};

}  // namespace Gallery
}  // namespace Zero
