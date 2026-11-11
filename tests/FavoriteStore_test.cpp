// SPDX-License-Identifier: GPL-3.0-only
// FavoriteStore: add / remove / toggle, persistence across instances, change signal, missing and corrupt files.
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "zero/favorites/FavoriteStore.h"

using namespace Zero::Favorites;

class FavoriteStoreTest : public QObject {
    Q_OBJECT
   private slots:
    void missingFileIsEmpty()
    {
        QTemporaryDir dir;
        FavoriteStore store(dir.filePath("zero/favorites.json"));
        QCOMPARE(store.count(Kind::World), 0);
        QCOMPARE(store.count(Kind::Server), 0);
        QVERIFY(!QFile::exists(dir.filePath("zero/favorites.json")));  // nothing is written until something changes
    }

    void addRemoveTogglePersist()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("zero/favorites.json");
        {
            FavoriteStore store(path);
            QSignalSpy changed(&store, &FavoriteStore::changed);
            QVERIFY(store.set(Kind::World, "inst-a/Nether Hub", true));
            QVERIFY(store.set(Kind::Server, "mc.example.org:25565", true));
            QVERIFY(store.set(Kind::World, "inst-a/Nether Hub", true));  // no change: no signal, no write
            QCOMPARE(changed.count(), 2);
            QCOMPARE(changed.first().at(0).value<Kind>(), Kind::World);
            QCOMPARE(changed.first().at(1).toString(), QStringLiteral("inst-a/Nether Hub"));
            QCOMPARE(changed.first().at(2).toBool(), true);
            QVERIFY(store.toggle(Kind::World, "inst-b/Village"));
            QVERIFY(!store.toggle(Kind::World, "inst-b/Village"));
            QVERIFY(store.contains(Kind::World, "inst-a/Nether Hub"));
            QVERIFY(!store.contains(Kind::Server, "inst-a/Nether Hub"));  // kinds are separate
            QVERIFY(store.set(Kind::World, "  ", true));  // blank keys are ignored
            QCOMPARE(store.count(Kind::World), 1);
        }
        FavoriteStore again(path);
        QCOMPARE(again.keys(Kind::World), (QSet<QString>{ "inst-a/Nether Hub" }));
        QCOMPARE(again.keys(Kind::Server), (QSet<QString>{ "mc.example.org:25565" }));
        QVERIFY(again.set(Kind::Server, "mc.example.org:25565", false));
        FavoriteStore third(path);
        QCOMPARE(third.count(Kind::Server), 0);

        // the file is readable JSON with sorted arrays
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(root.value("version").toInt(), 1);
        QCOMPARE(root.value("worlds").toArray().size(), 1);
    }

    void corruptFileIsMovedAside()
    {
        QTemporaryDir dir;
        QDir().mkpath(dir.filePath("zero"));
        const QString path = dir.filePath("zero/favorites.json");
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("{ not json");
        }
        FavoriteStore store(path);
        QCOMPARE(store.count(Kind::World), 0);
        QVERIFY(QFile::exists(path + ".corrupt"));
        QVERIFY(!QFile::exists(path));
        QVERIFY(store.set(Kind::World, "x/y", true));  // starts over with a fresh file
        QVERIFY(QFile::exists(path));
    }

    void ignoresMalformedEntries()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("favorites.json");
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(R"({"version":1,"worlds":["a/b", 3, "", null, " c/d "],"servers":"nope"})");
        }
        FavoriteStore store(path);
        QCOMPARE(store.keys(Kind::World), (QSet<QString>{ "a/b", "c/d" }));
        QCOMPARE(store.count(Kind::Server), 0);
    }
};

QTEST_GUILESS_MAIN(FavoriteStoreTest)
#include "FavoriteStore_test.moc"
