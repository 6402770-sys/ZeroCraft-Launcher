// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the change timeline of one instance: what happened to it (mod updates, modpack updates, changes brought back from a test
// copy, resource pack order changes, installs, play sessions), newest first. Read-only aggregation of histories that already exist; the
// only undo it knows is the one each source already has (ModSnapshot, PackSnapshot, PackOrder): the NEWEST active entry of each of those
// is `undoable`, nothing else is. No widgets, no application: `build()` works on data that was read already, `readSources()` reads the files.
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

#include "zero/backup/PackSnapshot.h"
#include "zero/content/PackOrder.h"
#include "zero/downloads/DownloadText.h"
#include "zero/history/SessionHistory.h"
#include "zero/mods/ModSnapshot.h"

namespace Zero {
namespace Downloads {
class DownloadCenter;
}
namespace History {
namespace Timeline {

constexpr int kShownFirst = 100;  ///< the page shows this many entries, then "Show more"

enum class Kind { ModsUpdate, ModpackUpdate, BringBack, PackOrder, Download, Session };
enum class Tone { Normal, Success, Warning, Error, Muted };

struct Entry {
    qint64 timeMs = 0;
    Kind kind = Kind::Download;
    QString title;
    QString caption;
    Tone tone = Tone::Normal;
    bool undoable = false;  ///< the newest active entry of its mechanism
    bool undone = false;    ///< rolled back already (the caption ends with "Undone")
    QString undoRef;        ///< ModsUpdate: the snapshot folder; ModpackUpdate / BringBack: the zip; PackOrder: the history entry id
};

/// Everything the builder needs, already read. Lists are in the order their readers give (newest first); the builder sorts anyway.
struct Sources {
    QString instanceId;  ///< downloads and sessions of other instances are ignored (empty = no filter)
    QString packId;      ///< ManagedPackID of the instance (PackSnapshot::newestActive)
    QList<Mods::Snapshot::Info> modSnapshots;
    QList<Backup::Pack::Info> packSnapshots;
    QList<Content::Packs::HistoryEntry> packOrder;
    QList<Downloads::HistoryEntry> downloads;
    QList<Session> sessions;
};

/// Newest first (stable for equal times: mod update, modpack, bring back, pack order, download, session).
QList<Entry> build(const Sources& sources);

struct Group {
    QString label;  ///< "Today", "Yesterday", "Mon 14 Nov" (the year is added when it is not the current one)
    QList<Entry> entries;
};

/// Consecutive entries of one calendar day (in the time zone of `now`) under one header. `entries` newest first.
QList<Group> groupByDay(const QList<Entry>& entries, const QDateTime& now);
QString dayLabel(qint64 timeMs, const QDateTime& now);

/// "Sodium, Lithium, Iris +2 more" (at most 3 names).
QString namesCaption(const QStringList& names);

struct Locations {
    QString dataRoot;
    QString instanceId;
    QString instanceRoot;
    QString packId;
};

/// Reads the files (snapshots, pack-order.json) and the two services (either may be null). Synchronous; the lists are small.
Sources readSources(const Locations& where, Downloads::DownloadCenter* downloads, SessionHistory* sessions);

}  // namespace Timeline
}  // namespace History
}  // namespace Zero
