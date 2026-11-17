// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the change timeline model (zero/history/ChangeTimeline): ordering, which entries are undoable (the newest active one
// of each mechanism only), the "Undone" tag, captions, day grouping with an injected clock and time zone, empty sources, and the readers.
#include <QDir>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include "zero/history/ChangeTimeline.h"

using namespace Zero;
using namespace Zero::History::Timeline;

namespace {

const QTimeZone kZone(QByteArray("UTC"));

qint64 at(int d, int h, int m = 0, int month = 11)
{
    return QDateTime(QDate(2026, month, d), QTime(h, m), kZone).toMSecsSinceEpoch();
}

const QDateTime kNow(QDate(2026, 11, 17), QTime(15, 0), kZone);

Mods::Snapshot::Info modSnap(qint64 created, const QStringList& names, const QString& outcome = QStringLiteral("ok"), bool rolledBack = false,
                             bool dismissed = false)
{
    Mods::Snapshot::Info info;
    info.valid = true;
    info.dir = QStringLiteral("snap/") + QString::number(created);
    info.createdMs = created;
    info.outcome = outcome;
    info.rolledBack = rolledBack;
    info.dismissed = dismissed;
    for (const QString& name : names) {
        Mods::Snapshot::ModEntry mod;
        mod.name = name;
        mod.oldVersion = QStringLiteral("1.0");
        mod.newVersion = QStringLiteral("2.0");
        mod.changed = true;
        info.mods.append(mod);
    }
    return info;
}

Backup::Pack::Info packSnap(qint64 created, const QString& name, const QString& from, const QString& to, bool rolledBack = false,
                            const QString& kind = QString(), const QString& label = QString())
{
    Backup::Pack::Info info;
    info.valid = true;
    info.zipPath = QStringLiteral("pack-") + QString::number(created) + QStringLiteral(".zip");
    info.createdMs = created;
    info.outcome = QStringLiteral("ok");
    info.rolledBack = rolledBack;
    info.meta.packName = name;
    info.meta.packId = QStringLiteral("p1");
    info.meta.oldVersion = from;
    info.meta.newVersion = to;
    info.meta.kind = kind;
    info.meta.label = label;
    return info;
}

Content::Packs::HistoryEntry orderEntry(qint64 ms, const QString& id)
{
    Content::Packs::HistoryEntry h;
    h.id = id;
    h.time = QDateTime::fromMSecsSinceEpoch(ms, kZone);
    return h;
}

Downloads::HistoryEntry download(qint64 ms, const QString& title, Downloads::DownloadResult::State state = Downloads::DownloadResult::State::Done,
                                 const QString& instance = QStringLiteral("i1"))
{
    Downloads::HistoryEntry h;
    h.id = title;
    h.title = title;
    h.instanceId = instance;
    h.kind = Downloads::Kind::Mod;
    h.state = state;
    h.bytes = 2 * 1024 * 1024;
    h.finishedAt = QDateTime::fromMSecsSinceEpoch(ms, kZone);
    return h;
}

History::Session session(qint64 start, qint64 end, History::Outcome outcome, const QString& instance = QStringLiteral("i1"))
{
    History::Session s;
    s.instanceId = instance;
    s.startedMs = start;
    s.endedMs = end;
    s.outcome = outcome;
    return s;
}

}  // namespace

class ChangeTimelineTest : public QObject {
    Q_OBJECT
   private slots:
    void emptySources()
    {
        QVERIFY(build(Sources()).isEmpty());
        QVERIFY(groupByDay({}, kNow).isEmpty());
    }

    void newestFirstAcrossSources()
    {
        Sources s;
        s.instanceId = QStringLiteral("i1");
        s.modSnapshots = { modSnap(at(15, 9), { QStringLiteral("Sodium") }) };
        s.packSnapshots = { packSnap(at(16, 9), QStringLiteral("Fabulous"), QStringLiteral("1.0"), QStringLiteral("1.1")) };
        s.packOrder = { orderEntry(at(14, 9), QStringLiteral("a")) };
        s.downloads = { download(at(17, 8), QStringLiteral("Iris")) };
        s.sessions = { session(at(17, 10), at(17, 11), History::Outcome::Played) };
        const QList<Entry> list = build(s);
        QCOMPARE(list.size(), 5);
        QCOMPARE(int(list[0].kind), int(Kind::Session));
        QCOMPARE(int(list[1].kind), int(Kind::Download));
        QCOMPARE(int(list[2].kind), int(Kind::ModpackUpdate));
        QCOMPARE(int(list[3].kind), int(Kind::ModsUpdate));
        QCOMPARE(int(list[4].kind), int(Kind::PackOrder));
        for (int i = 1; i < list.size(); ++i)
            QVERIFY(list[i - 1].timeMs >= list[i].timeMs);
    }

    void modsUpdateOnlyNewestIsUndoable()
    {
        Sources s;
        s.modSnapshots = { modSnap(at(16, 9), { QStringLiteral("A") }), modSnap(at(15, 9), { QStringLiteral("B") }) };
        const QList<Entry> list = build(s);
        QCOMPARE(list.size(), 2);
        QVERIFY(list[0].undoable);
        QCOMPARE(list[0].undoRef, QStringLiteral("snap/") + QString::number(at(16, 9)));
        QVERIFY(!list[1].undoable);
        QVERIFY(list[1].undoRef.isEmpty());
    }

    void modsUpdateRolledBackAndDismissed()
    {
        Sources s;
        // the newest was rolled back: the older one is NOT offered behind it
        s.modSnapshots = { modSnap(at(16, 9), { QStringLiteral("A") }, QStringLiteral("ok"), true), modSnap(at(15, 9), { QStringLiteral("B") }) };
        QList<Entry> list = build(s);
        QVERIFY(list[0].undone);
        QVERIFY(list[0].caption.endsWith(QStringLiteral("Undone")));
        QVERIFY(list[0].caption.contains(QStringLiteral("1.0 → 2.0")));
        QCOMPARE(int(list[0].tone), int(Tone::Muted));
        QVERIFY(!list[0].undoable);
        QVERIFY(!list[1].undoable);

        // dismissed: not undoable, but it did not get undone either
        s.modSnapshots = { modSnap(at(16, 9), { QStringLiteral("A") }, QStringLiteral("ok"), false, true) };
        list = build(s);
        QVERIFY(!list[0].undoable);
        QVERIFY(!list[0].undone);
    }

    void unfinishedAndInvalidSnapshotsAreSkipped()
    {
        Sources s;
        s.modSnapshots = { modSnap(at(16, 9), { QStringLiteral("A") }, QString()), Mods::Snapshot::Info(), modSnap(at(15, 9), { QStringLiteral("B") }) };
        const QList<Entry> list = build(s);
        QCOMPARE(list.size(), 1);
        QVERIFY(!list[0].undoable);  // the newest snapshot is the unfinished one: newestActive offers nothing behind it
    }

    void modsCaptions()
    {
        Sources s;
        s.modSnapshots = { modSnap(at(16, 9), { QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C") }),
                           modSnap(at(15, 9), { QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"), QStringLiteral("D"), QStringLiteral("E") }),
                           modSnap(at(14, 9), { QStringLiteral("Solo") }) };
        const QList<Entry> list = build(s);
        QCOMPARE(list[0].title, QStringLiteral("Updated 3 mods"));
        QCOMPARE(list[0].caption, QStringLiteral("A, B, C"));
        QCOMPARE(list[1].title, QStringLiteral("Updated 5 mods"));
        QCOMPARE(list[1].caption, QStringLiteral("A, B, C +2 more"));
        QCOMPARE(list[2].title, QStringLiteral("Updated Solo"));
        QCOMPARE(list[2].caption, QStringLiteral("1.0 → 2.0"));
        QCOMPARE(namesCaption({}), QString());
    }

    void modpackUpdateAndBringBack()
    {
        Sources s;
        s.packId = QStringLiteral("p1");
        s.packSnapshots = { packSnap(at(16, 9), QStringLiteral("Fabulous"), QStringLiteral("1.0"), QStringLiteral("1.2")),
                            packSnap(at(15, 9), QStringLiteral("Fabulous"), QStringLiteral("0.9"), QStringLiteral("1.0"), false,
                                     QStringLiteral("bring-back"), QStringLiteral("Fabulous (test copy)")),
                            packSnap(at(14, 9), QStringLiteral("Fabulous"), QStringLiteral("0.8"), QStringLiteral("0.9"), true) };
        const QList<Entry> list = build(s);
        QCOMPARE(list.size(), 3);
        QCOMPARE(list[0].title, QStringLiteral("Updated Fabulous to 1.2"));
        QCOMPARE(list[0].caption, QStringLiteral("from 1.0"));
        QVERIFY(list[0].undoable);
        QCOMPARE(list[0].undoRef, QStringLiteral("pack-%1.zip").arg(at(16, 9)));
        QCOMPARE(int(list[1].kind), int(Kind::BringBack));
        QCOMPARE(list[1].title, QStringLiteral("Brought changes back from Fabulous (test copy)"));
        QVERIFY(!list[1].undoable);  // only the newest active one
        QVERIFY(list[2].undone);
        QVERIFY(list[2].caption.endsWith(QStringLiteral("Undone")));
        QVERIFY(!list[2].undoable);

        // the newest was undone: the next active one becomes the one PackSnapshot::newestActive offers
        s.packSnapshots[0].rolledBack = true;
        const QList<Entry> after = build(s);
        QVERIFY(after[0].undone);
        QVERIFY(!after[0].undoable);
        QVERIFY(after[1].undoable);
        QCOMPARE(int(after[1].kind), int(Kind::BringBack));
    }

    void packOrderOnlyNewestUndoable()
    {
        Sources s;
        s.packOrder = { orderEntry(at(16, 9), QStringLiteral("b")), orderEntry(at(15, 9), QStringLiteral("a")) };
        const QList<Entry> list = build(s);
        QCOMPARE(list.size(), 2);
        QCOMPARE(list[0].title, QStringLiteral("Changed resource pack order"));
        QVERIFY(list[0].undoable);
        QCOMPARE(list[0].undoRef, QStringLiteral("b"));
        QVERIFY(!list[1].undoable);
    }

    void downloads()
    {
        using State = Downloads::DownloadResult::State;
        Sources s;
        s.instanceId = QStringLiteral("i1");
        auto failed = download(at(16, 9), QStringLiteral("Broken"), State::Failed);
        failed.error = QStringLiteral("Network error");
        auto update = download(at(16, 10), QStringLiteral("Update 2 mods"));
        update.kind = Downloads::Kind::Update;
        s.downloads = { download(at(16, 12), QStringLiteral("Other"), State::Done, QStringLiteral("i2")), update, failed,
                        download(at(15, 9), QStringLiteral("Cancelled one"), State::Cancelled), download(at(14, 9), QStringLiteral("Sodium")) };
        const QList<Entry> list = build(s);
        QCOMPARE(list.size(), 3);  // other instance and the finished update are left out
        QCOMPARE(list[0].title, QStringLiteral("Install failed: Broken"));
        QCOMPARE(int(list[0].tone), int(Tone::Warning));
        QVERIFY(list[0].caption.contains(QStringLiteral("Network error")));
        QCOMPARE(list[1].title, QStringLiteral("Install cancelled: Cancelled one"));
        QCOMPARE(int(list[1].tone), int(Tone::Muted));
        QCOMPARE(list[2].title, QStringLiteral("Installed Sodium"));
        QCOMPARE(int(list[2].tone), int(Tone::Success));
        QCOMPARE(list[2].caption, QStringLiteral("Mod · 2.0 MB"));
        for (const Entry& e : list)
            QVERIFY(!e.undoable);
    }

    void sessions()
    {
        Sources s;
        s.instanceId = QStringLiteral("i1");
        s.sessions = { session(at(17, 8), at(17, 9, 30), History::Outcome::Played), session(at(16, 8), at(16, 8, 5), History::Outcome::Crashed),
                       session(at(15, 8), at(15, 8), History::Outcome::Failed), session(at(14, 8), at(14, 9), History::Outcome::Played, QStringLiteral("i2")) };
        const QList<Entry> list = build(s);
        QCOMPARE(list.size(), 3);
        QCOMPARE(list[0].title, QStringLiteral("Played 1 h 30 min"));
        QCOMPARE(int(list[0].tone), int(Tone::Muted));
        QCOMPARE(list[1].title, QStringLiteral("Crashed after 5 min"));
        QCOMPARE(int(list[1].tone), int(Tone::Error));
        QCOMPARE(list[2].title, QStringLiteral("Didn't start"));
        QCOMPARE(int(list[2].tone), int(Tone::Warning));
    }

    void dayGroups()
    {
        QCOMPARE(dayLabel(at(17, 0, 1), kNow), QStringLiteral("Today"));
        QCOMPARE(dayLabel(at(16, 23, 59), kNow), QStringLiteral("Yesterday"));
        QCOMPARE(dayLabel(at(14, 12), kNow), QStringLiteral("Sat 14 Nov"));
        QCOMPARE(dayLabel(QDateTime(QDate(2025, 12, 31), QTime(12, 0), kZone).toMSecsSinceEpoch(), kNow), QStringLiteral("Wed 31 Dec 2025"));
    }

    void groupingFollowsTheTimeZoneOfNow()
    {
        Sources s;
        s.sessions = { session(at(17, 1), at(17, 1, 30), History::Outcome::Played), session(at(16, 20), at(16, 21), History::Outcome::Played),
                       session(at(16, 10), at(16, 11), History::Outcome::Played), session(at(10, 10), at(10, 11), History::Outcome::Played) };
        QList<Group> groups = groupByDay(build(s), kNow);
        QCOMPARE(groups.size(), 3);
        QCOMPARE(groups[0].label, QStringLiteral("Today"));
        QCOMPARE(groups[1].label, QStringLiteral("Yesterday"));
        QCOMPARE(groups[1].entries.size(), 2);
        QCOMPARE(groups[2].label, QStringLiteral("Tue 10 Nov"));

        // 01:30 UTC is still the previous day at UTC-5
        const QDateTime west(QDate(2026, 11, 17), QTime(15, 0), QTimeZone(QByteArray("America/Bogota")));
        if (west.timeZone().isValid()) {
            groups = groupByDay(build(s), west);
            QCOMPARE(groups[0].label, QStringLiteral("Yesterday"));
        }
    }

    void readersTolerateMissingFiles()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        Locations where;
        where.dataRoot = tmp.path();
        where.instanceId = QStringLiteral("i1");
        where.instanceRoot = QDir(tmp.path()).filePath(QStringLiteral("inst"));
        const Sources s = readSources(where, nullptr, nullptr);
        QVERIFY(s.modSnapshots.isEmpty());
        QVERIFY(s.packSnapshots.isEmpty());
        QVERIFY(s.packOrder.isEmpty());
        QVERIFY(build(s).isEmpty());
    }
};

QTEST_GUILESS_MAIN(ChangeTimelineTest)

#include "ChangeTimeline_test.moc"
