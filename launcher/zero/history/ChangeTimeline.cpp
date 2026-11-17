// SPDX-License-Identifier: GPL-3.0-only
#include "ChangeTimeline.h"

#include <QCoreApplication>
#include <QLocale>
#include <QTimeZone>
#include <algorithm>

#include "ui/zero/ZeroFormat.h"
#include "zero/downloads/DownloadCenter.h"

namespace Zero {
namespace History {
namespace Timeline {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("ChangeTimeline", text);
}

QString undoneTag()
{
    return tr("Undone");
}

QString withTag(const QString& caption, bool undone)
{
    if (!undone)
        return caption;
    return caption.isEmpty() ? undoneTag() : caption + QStringLiteral(" · ") + undoneTag();
}

QString kindWord(Downloads::Kind kind)
{
    switch (kind) {
        case Downloads::Kind::Mod:
            return tr("Mod");
        case Downloads::Kind::ResourcePack:
            return tr("Resource pack");
        case Downloads::Kind::ShaderPack:
            return tr("Shader pack");
        case Downloads::Kind::World:
            return tr("World");
        case Downloads::Kind::Modpack:
            return tr("Modpack");
        case Downloads::Kind::Update:
            return tr("Update");
        case Downloads::Kind::Mixed:
            break;
    }
    return {};
}

int rank(Kind kind)
{
    return int(kind);
}

void addMods(const Sources& s, QList<Entry>* out)
{
    // ModSnapshot::newestActive's rule: only the newest snapshot, and only when it is finished, valid, not rolled back and not dismissed.
    QList<Mods::Snapshot::Info> sorted = s.modSnapshots;
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.createdMs > b.createdMs; });
    for (qsizetype i = 0; i < sorted.size(); ++i) {
        const Mods::Snapshot::Info& info = sorted.at(i);
        if (!info.valid || info.outcome.isEmpty()) {  // unreadable, or the update has not finished: not a change to show
            continue;
        }
        Entry e;
        e.kind = Kind::ModsUpdate;
        e.timeMs = info.createdMs;
        QStringList names = info.changedNames();
        if (names.isEmpty()) {
            for (const auto& mod : info.mods)
                names.append(mod.name);
        }
        const bool failed = info.outcome == QLatin1String(Mods::Snapshot::kOutcomeFailed);
        if (failed) {
            e.title = tr("Mod update did not finish");
            e.tone = Tone::Warning;
        } else if (names.size() == 1) {
            e.title = tr("Updated %1").arg(names.first());
        } else {
            e.title = tr("Updated %1 mods").arg(names.size());
        }
        if (names.size() == 1 && !info.mods.isEmpty() && !failed) {
            const auto& mod = info.mods.first();
            if (!mod.oldVersion.isEmpty() && !mod.newVersion.isEmpty())
                e.caption = QStringLiteral("%1 → %2").arg(mod.oldVersion, mod.newVersion);
        } else {
            e.caption = namesCaption(names);
        }
        e.undone = info.rolledBack;
        e.undoable = i == 0 && !info.rolledBack && !info.dismissed;
        if (e.undoable)
            e.undoRef = info.dir;
        if (e.undone)
            e.tone = Tone::Muted;
        e.caption = withTag(e.caption, e.undone);
        out->append(e);  // an older snapshot is never offered behind a newer one: only index 0 can be undoable
    }
}

void addPacks(const Sources& s, QList<Entry>* out)
{
    QList<Backup::Pack::Info> sorted = s.packSnapshots;
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.createdMs > b.createdMs; });
    const Backup::Pack::Info active = Backup::Pack::newestActive(sorted, s.packId);
    for (const Backup::Pack::Info& info : std::as_const(sorted)) {
        if (!info.valid || info.outcome == QLatin1String(Backup::Pack::kOutcomePending))
            continue;
        Entry e;
        e.timeMs = info.createdMs;
        const bool bringBack = info.meta.kind == QLatin1String(Backup::Pack::kKindBringBack);
        const bool failed = info.outcome == QLatin1String(Backup::Pack::kOutcomeFailed);
        const QString pack = info.meta.packName.isEmpty() ? info.meta.instanceName : info.meta.packName;
        if (bringBack) {
            e.kind = Kind::BringBack;
            const QString label = info.meta.label;
            e.title = failed ? tr("Bringing changes back did not finish")
                             : tr("Brought changes back from %1").arg(label.isEmpty() ? tr("a test copy") : label);
        } else {
            e.kind = Kind::ModpackUpdate;
            if (failed)
                e.title = tr("Update of %1 did not finish").arg(pack);
            else
                e.title = info.meta.newVersion.isEmpty() ? tr("Updated %1").arg(pack) : tr("Updated %1 to %2").arg(pack, info.meta.newVersion);
            if (!info.meta.oldVersion.isEmpty())
                e.caption = tr("from %1").arg(info.meta.oldVersion);
        }
        if (failed)
            e.tone = Tone::Warning;
        e.undone = info.rolledBack;
        e.undoable = active.valid && active.zipPath == info.zipPath;
        if (e.undoable)
            e.undoRef = info.zipPath;
        if (e.undone)
            e.tone = Tone::Muted;
        e.caption = withTag(e.caption, e.undone);
        out->append(e);
    }
}

void addPackOrder(const Sources& s, QList<Entry>* out)
{
    QList<Content::Packs::HistoryEntry> sorted = s.packOrder;
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.time > b.time; });
    bool first = true;
    for (const auto& h : std::as_const(sorted)) {
        Entry e;
        e.kind = Kind::PackOrder;
        e.timeMs = h.time.toMSecsSinceEpoch();
        e.title = tr("Changed resource pack order");
        e.undoable = first;  // undoLast() restores the newest entry only
        if (first)
            e.undoRef = h.id;
        first = false;
        out->append(e);
    }
}

void addDownloads(const Sources& s, QList<Entry>* out)
{
    using State = Downloads::DownloadResult::State;
    for (const Downloads::HistoryEntry& h : s.downloads) {
        if (!s.instanceId.isEmpty() && h.instanceId != s.instanceId)
            continue;
        // A finished mod update is already there as its snapshot (with the Undo): not twice.
        if (h.kind == Downloads::Kind::Update && h.state == State::Done)
            continue;
        Entry e;
        e.kind = Kind::Download;
        e.timeMs = h.finishedMs();
        switch (h.state) {
            case State::Done:
                e.title = tr("Installed %1").arg(h.title);
                e.tone = Tone::Success;
                break;
            case State::Failed:
                e.title = tr("Install failed: %1").arg(h.title);
                e.tone = Tone::Warning;
                break;
            case State::Cancelled:
                e.title = tr("Install cancelled: %1").arg(h.title);
                e.tone = Tone::Muted;
                break;
        }
        QStringList parts;
        const QString word = kindWord(h.kind);
        if (!word.isEmpty())
            parts << word;
        if (h.state == State::Failed && !h.error.isEmpty())
            parts << h.error;
        else if (h.state == State::Done && h.bytes > 0)
            parts << Zero::Format::bytes(h.bytes);
        e.caption = parts.join(QStringLiteral(" · "));
        out->append(e);
    }
}

void addSessions(const Sources& s, QList<Entry>* out)
{
    for (const Session& session : s.sessions) {
        if (!s.instanceId.isEmpty() && session.instanceId != s.instanceId)
            continue;
        Entry e;
        e.kind = Kind::Session;
        e.timeMs = session.endedMs > 0 ? session.endedMs : session.startedMs;
        const QString duration = Zero::Format::playtime(session.durationMs() / 1000);
        switch (session.outcome) {
            case Outcome::Played:
            case Outcome::Stopped:
                e.title = tr("Played %1").arg(duration);
                e.tone = Tone::Muted;
                break;
            case Outcome::Crashed:
                e.title = tr("Crashed after %1").arg(duration);
                e.tone = Tone::Error;
                break;
            case Outcome::Failed:
                e.title = tr("Didn't start");
                e.tone = Tone::Warning;
                break;
        }
        out->append(e);
    }
}

}  // namespace

QString namesCaption(const QStringList& names)
{
    if (names.isEmpty())
        return {};
    QString text = names.mid(0, 3).join(QStringLiteral(", "));
    if (names.size() > 3)
        text += tr(" +%1 more").arg(names.size() - 3);
    return text;
}

QList<Entry> build(const Sources& sources)
{
    QList<Entry> out;
    addMods(sources, &out);
    addPacks(sources, &out);
    addPackOrder(sources, &out);
    addDownloads(sources, &out);
    addSessions(sources, &out);
    std::stable_sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        if (a.timeMs != b.timeMs)
            return a.timeMs > b.timeMs;
        return rank(a.kind) < rank(b.kind);
    });
    return out;
}

QString dayLabel(qint64 timeMs, const QDateTime& now)
{
    const QDate day = QDateTime::fromMSecsSinceEpoch(timeMs, now.timeZone()).date();
    const QDate today = now.date();
    if (day == today)
        return tr("Today");
    if (day == today.addDays(-1))
        return tr("Yesterday");
    const QLocale english(QLocale::English);
    return english.toString(day, day.year() == today.year() ? QStringLiteral("ddd d MMM") : QStringLiteral("ddd d MMM yyyy"));
}

QList<Group> groupByDay(const QList<Entry>& entries, const QDateTime& now)
{
    QList<Group> groups;
    QDate current;
    for (const Entry& e : entries) {
        const QDate day = QDateTime::fromMSecsSinceEpoch(e.timeMs, now.timeZone()).date();
        if (groups.isEmpty() || day != current) {
            Group g;
            g.label = dayLabel(e.timeMs, now);
            groups.append(g);
            current = day;
        }
        groups.last().entries.append(e);
    }
    return groups;
}

Sources readSources(const Locations& where, Downloads::DownloadCenter* downloads, SessionHistory* sessions)
{
    Sources s;
    s.instanceId = where.instanceId;
    s.packId = where.packId;
    s.modSnapshots = Mods::Snapshot::listSnapshots(Mods::Snapshot::snapshotsDirFor(where.dataRoot, where.instanceId));
    s.packSnapshots = Backup::Pack::listSnapshots(Backup::Pack::snapshotsDirFor(where.dataRoot, where.instanceId));
    if (!where.instanceRoot.isEmpty())
        s.packOrder = Content::Packs::readHistory(Content::Packs::historyPathFor(where.instanceRoot));
    if (downloads)
        s.downloads = downloads->history();
    if (sessions)
        s.sessions = sessions->sessions();
    return s;
}

}  // namespace Timeline
}  // namespace History
}  // namespace Zero
