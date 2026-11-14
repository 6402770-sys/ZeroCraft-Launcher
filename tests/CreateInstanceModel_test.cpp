// SPDX-License-Identifier: GPL-3.0-only
// Zero::Create core: version rows (order, latest, recommended), filters + search, loader availability from metadata (Forge /
// NeoForge / LiteLoader by their Minecraft requirement, Fabric / Quilt by the intermediary list, unknown lists), loader
// version order and preselection, meta adapters, name suggestion + validation, import source rules.
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "zero/content/CreateInstanceModel.h"

using namespace Zero::Create;

namespace {

QDateTime day(int y, int m, int d)
{
    return QDateTime(QDate(y, m, d), QTime(12, 0), QTimeZone::UTC);
}

MinecraftVersionFact mc(const QString& id, const QString& type, const QDateTime& released, bool recommended = false)
{
    return { id, type, released, recommended };
}

LoaderVersionFact lv(const QString& uid, const QString& version, const QString& requiresMc, const QDateTime& released, bool recommended = false)
{
    return { uid, version, requiresMc, recommended, released };
}

Meta::Version::Ptr metaVersion(const QString& uid, const QString& id, const QString& type, qint64 time, const QString& requiresUid = {},
                               const QString& equals = {}, bool recommended = false)
{
    auto v = std::make_shared<Meta::Version>(uid, id);
    v->setType(type);
    v->setTime(time);
    v->setRecommended(recommended);
    if (!requiresUid.isEmpty()) {
        Meta::RequireSet reqs;
        Meta::Require r;
        r.uid = requiresUid;
        r.equalsVersion = equals;
        reqs.insert(r);
        v->setRequires(reqs, {});
    }
    return v;
}

QList<VersionRow> sampleRows()
{
    return buildVersionRows({
        mc("1.20.1", "release", day(2023, 6, 12), true),
        mc("23w31a", "snapshot", day(2023, 8, 1)),
        mc("1.20.2", "release", day(2023, 9, 21)),
        mc("13w12~", "old_snapshot", day(2013, 3, 19)),
        mc("b1.7.3", "old_beta", day(2011, 7, 8)),
        mc("a1.2.6", "old_alpha", day(2010, 12, 3)),
        mc("1.21.11_unobfuscated", "experiment", day(2025, 12, 1)),
        mc("1.12.2", "release", day(2017, 9, 18)),
    });
}

LoaderCatalog sampleCatalog()
{
    LoaderCatalog c;
    c.setLoaderVersions(Loader::Forge, {
                                           lv(loaderUid(Loader::Forge), "47.4.28", "1.20.1", day(2025, 5, 1), true),
                                           lv(loaderUid(Loader::Forge), "47.4.29", "1.20.1", day(2025, 6, 1)),
                                           lv(loaderUid(Loader::Forge), "14.23.5.2860", "1.12.2", day(2021, 1, 1), true),
                                       });
    c.setLoaderVersions(Loader::NeoForge, { lv(loaderUid(Loader::NeoForge), "47.1.106", "1.20.1", day(2024, 1, 1)) });
    c.setLoaderVersions(Loader::Fabric, {
                                            lv(loaderUid(Loader::Fabric), "0.16.0", {}, day(2024, 7, 1)),
                                            lv(loaderUid(Loader::Fabric), "0.16.5", {}, day(2024, 9, 1), true),
                                            lv(loaderUid(Loader::Fabric), "0.17.0", {}, day(2025, 1, 1)),
                                        });
    c.setLoaderVersions(Loader::Quilt, { lv(loaderUid(Loader::Quilt), "0.26.0", {}, day(2024, 6, 1), true) });
    c.setLoaderVersions(Loader::LiteLoader, { lv(loaderUid(Loader::LiteLoader), "1.12.2-SNAPSHOT", "1.12.2", day(2017, 11, 28)) });
    c.setIntermediaryVersions({ "1.20.1", "1.20.2", "23w31a" });
    return c;
}

}  // namespace

class CreateInstanceModelTest : public QObject {
    Q_OBJECT
   private slots:
    void typesFromMeta()
    {
        QCOMPARE(versionTypeFromMeta("release"), VersionType::Release);
        QCOMPARE(versionTypeFromMeta("snapshot"), VersionType::Snapshot);
        QCOMPARE(versionTypeFromMeta("old_snapshot"), VersionType::Snapshot);
        QCOMPARE(versionTypeFromMeta("old_beta"), VersionType::Beta);
        QCOMPARE(versionTypeFromMeta("old_alpha"), VersionType::Alpha);
        QCOMPARE(versionTypeFromMeta("experiment"), VersionType::Experiment);
        QCOMPARE(versionTypeFromMeta("weird"), VersionType::Other);
        QCOMPARE(versionTypeLabel(VersionType::Beta), QString("Beta"));
        QVERIFY(versionTypeLabel(VersionType::Other).isEmpty());
        QCOMPARE(filterFor(VersionType::Alpha), VersionFilter::Old);
        QCOMPARE(filterFor(VersionType::Experiment), VersionFilter::Experiments);
        QCOMPARE(filterFor(VersionType::Release), VersionFilter::Releases);
    }

    void rowsAreNewestFirstWithLatestRelease()
    {
        const auto rows = sampleRows();
        QCOMPARE(rows.size(), 8);
        QCOMPARE(rows.first().id, QString("1.21.11_unobfuscated"));
        QCOMPARE(rows.last().id, QString("a1.2.6"));
        int latest = 0;
        for (const auto& r : rows)
            latest += r.latest ? 1 : 0;
        QCOMPARE(latest, 1);
        QVERIFY(rows.at(indexOfVersion(rows, "1.20.2")).latest);  // newest *release*, not the newer experiment
        QVERIFY(!rows.at(indexOfVersion(rows, "1.20.1")).latest);
    }

    void equalTimesKeepInputOrder()
    {
        const auto rows = buildVersionRows({ mc("b", "release", day(2020, 1, 1)), mc("a", "release", day(2020, 1, 1)) });
        QCOMPARE(rows.at(0).id, QString("b"));
        QCOMPARE(rows.at(1).id, QString("a"));
        QVERIFY(rows.at(0).latest);
    }

    void filtersAndSearch()
    {
        const auto rows = sampleRows();
        auto ids = [](const QList<VersionRow>& list) {
            QStringList out;
            for (const auto& r : list)
                out << r.id;
            return out;
        };
        QCOMPARE(ids(filterVersionRows(rows, VersionFilter::Releases, {})), QStringList({ "1.20.2", "1.20.1", "1.12.2" }));
        QCOMPARE(ids(filterVersionRows(rows, VersionFilter::Snapshots, {})), QStringList({ "23w31a", "13w12~" }));
        QCOMPARE(ids(filterVersionRows(rows, VersionFilter::Old, {})), QStringList({ "b1.7.3", "a1.2.6" }));
        QCOMPARE(ids(filterVersionRows(rows, VersionFilter::Experiments, {})), QStringList({ "1.21.11_unobfuscated" }));
        QCOMPARE(ids(filterVersionRows(rows, VersionFilter::Releases, " 1.20 ")), QStringList({ "1.20.2", "1.20.1" }));
        QCOMPARE(ids(filterVersionRows(rows, VersionFilter::Snapshots, "W31")), QStringList({ "23w31a" }));
        QVERIFY(filterVersionRows(rows, VersionFilter::Releases, "nothing").isEmpty());
    }

    void preselection()
    {
        const auto releases = filterVersionRows(sampleRows(), VersionFilter::Releases, {});
        QCOMPARE(releases.at(preferredVersionRow(releases)).id, QString("1.20.1"));  // the recommended one
        const auto snapshots = filterVersionRows(sampleRows(), VersionFilter::Snapshots, {});
        QCOMPARE(preferredVersionRow(snapshots), 0);
        QCOMPARE(preferredVersionRow({}), -1);
        QCOMPARE(indexOfVersion(releases, "missing"), -1);
    }

    void availabilityFollowsMetadata()
    {
        const LoaderCatalog c = sampleCatalog();
        auto a = c.availability("1.20.1");
        QCOMPARE(a.available, QList<Loader>({ Loader::Fabric, Loader::Forge, Loader::NeoForge, Loader::Quilt }));
        QCOMPARE(a.unavailable, QList<Loader>({ Loader::LiteLoader }));
        QVERIFY(a.unknown.isEmpty());

        a = c.availability("1.12.2");  // no intermediary for 1.12.2: Fabric and Quilt are unavailable, whatever the version number
        QCOMPARE(a.available, QList<Loader>({ Loader::Forge, Loader::LiteLoader }));
        QCOMPARE(a.unavailable, QList<Loader>({ Loader::Fabric, Loader::NeoForge, Loader::Quilt }));

        a = c.availability("23w31a");  // a snapshot with intermediary: Fabric/Quilt available (the old 1.14 rule would refuse it)
        QCOMPARE(a.available, QList<Loader>({ Loader::Fabric, Loader::Quilt }));

        a = c.availability("1.2.5");
        QVERIFY(a.available.isEmpty());
        QCOMPARE(unavailableCaption("1.2.5", a.unavailable), QString("Not available for 1.2.5: Fabric, Forge, NeoForge, Quilt, LiteLoader"));
        QVERIFY(unavailableCaption("1.2.5", {}).isEmpty());
    }

    void unknownListsAreNeitherAvailableNorUnavailable()
    {
        LoaderCatalog c;
        c.setLoaderVersions(Loader::Fabric, { lv(loaderUid(Loader::Fabric), "0.16.5", {}, day(2024, 9, 1)) });
        c.setLoaderVersions(Loader::Forge, { lv(loaderUid(Loader::Forge), "47.4.28", "1.20.1", day(2025, 5, 1)) });
        auto a = c.availability("1.20.1");
        QCOMPARE(a.available, QList<Loader>({ Loader::Forge }));
        QCOMPARE(a.unknown, QList<Loader>({ Loader::Fabric, Loader::NeoForge, Loader::Quilt, Loader::LiteLoader }));
        QVERIFY(!c.isKnown(Loader::Fabric));  // the intermediary is still missing
        c.setIntermediaryVersions({ "1.20.1" });
        QVERIFY(c.isKnown(Loader::Fabric));
        QVERIFY(c.availability("1.20.1").available.contains(Loader::Fabric));
        c.clearIntermediary();
        QVERIFY(!c.isKnown(Loader::Fabric));
        c.clearLoader(Loader::Forge);
        QVERIFY(c.availability("1.20.1").unknown.contains(Loader::Forge));

        LoaderCatalog empty;
        empty.setLoaderVersions(Loader::Quilt, {});
        empty.setIntermediaryVersions({ "1.20.1" });
        QVERIFY(empty.availability("1.20.1").unavailable.contains(Loader::Quilt));  // known list without versions
    }

    void loaderVersionsNewestFirstWithRecommended()
    {
        const LoaderCatalog c = sampleCatalog();
        const auto forge = c.versionsFor(Loader::Forge, "1.20.1");
        QCOMPARE(forge.size(), 2);
        QCOMPARE(forge.at(0).version, QString("47.4.29"));
        QCOMPARE(forge.at(preferredLoaderRow(forge)).version, QString("47.4.28"));
        const auto fabric = c.versionsFor(Loader::Fabric, "1.20.2");
        QCOMPARE(fabric.size(), 3);
        QCOMPARE(fabric.at(0).version, QString("0.17.0"));
        QCOMPARE(fabric.at(preferredLoaderRow(fabric)).version, QString("0.16.5"));
        QVERIFY(c.versionsFor(Loader::Fabric, "1.12.2").isEmpty());
        QVERIFY(c.versionsFor(Loader::Forge, {}).isEmpty());
        const auto neo = c.versionsFor(Loader::NeoForge, "1.20.1");
        QCOMPARE(preferredLoaderRow(neo), 0);  // nothing recommended: the newest
        QCOMPARE(preferredLoaderRow({}), -1);
    }

    void loaderIdentity()
    {
        QCOMPARE(loaderOrder().size(), 5);
        for (Loader l : loaderOrder()) {
            QCOMPARE(loaderFromUid(loaderUid(l)), std::optional<Loader>(l));
            QCOMPARE(loaderFromName(loaderName(l).toUpper()), std::optional<Loader>(l));
        }
        QCOMPARE(loaderUid(Loader::NeoForge), QString("net.neoforged"));
        QCOMPARE(loaderUid(Loader::Quilt), QString("org.quiltmc.quilt-loader"));
        QVERIFY(!loaderFromUid("net.minecraft"));
        QVERIFY(!loaderFromName("rift"));
        QVERIFY(usesIntermediary(Loader::Quilt));
        QVERIFY(!usesIntermediary(Loader::LiteLoader));
        QCOMPARE(moddedMetaUids().size(), 7);
        QVERIFY(moddedMetaUids().contains(kIntermediaryUid));
    }

    void metaAdapters()
    {
        const QList<Meta::Version::Ptr> mcList{ metaVersion(kMinecraftUid, "1.20.1", "release", 1686571200, {}, {}, true),
                                                metaVersion(kMinecraftUid, "23w31a", "snapshot", 1690891200) };
        const auto facts = minecraftFacts(mcList);
        QCOMPARE(facts.size(), 2);
        QCOMPARE(facts.at(0).id, QString("1.20.1"));
        QCOMPARE(facts.at(0).type, QString("release"));
        QVERIFY(facts.at(0).recommended);
        QCOMPARE(facts.at(0).released, QDateTime::fromSecsSinceEpoch(1686571200, QTimeZone::UTC));

        const QList<Meta::Version::Ptr> forgeList{ metaVersion(loaderUid(Loader::Forge), "47.4.28", {}, 100, kMinecraftUid, "1.20.1", true) };
        const auto forge = loaderFacts(forgeList);
        QCOMPARE(forge.size(), 1);
        QCOMPARE(forge.at(0).uid, loaderUid(Loader::Forge));
        QCOMPARE(forge.at(0).requiresMinecraft, QString("1.20.1"));
        QVERIFY(forge.at(0).recommended);

        const QList<Meta::Version::Ptr> fabricList{ metaVersion(loaderUid(Loader::Fabric), "0.16.5", "release", 100, kIntermediaryUid) };
        QVERIFY(loaderFacts(fabricList).at(0).requiresMinecraft.isEmpty());

        const QList<Meta::Version::Ptr> intermediary{ metaVersion(kIntermediaryUid, "1.20.1", "release", 100, kMinecraftUid, "1.20.1"),
                                                      metaVersion(kIntermediaryUid, "24w14potato_original", "snapshot", 100, kMinecraftUid,
                                                                  "24w14potato"),
                                                      metaVersion(kIntermediaryUid, "1.14", "release", 100) };
        QCOMPARE(intermediaryMinecraftIds(intermediary), QSet<QString>({ "1.20.1", "24w14potato", "1.14" }));
        QVERIFY(minecraftFacts({ nullptr }).isEmpty());
    }

    void nameSuggestionAndValidation()
    {
        QCOMPARE(suggestName("1.20.1"), QString("1.20.1"));
        QCOMPARE(suggestName("1.20.1", Loader::Fabric), QString("1.20.1 Fabric"));
        QCOMPARE(suggestName("1.20.1", Loader::NeoForge), QString("1.20.1 NeoForge"));
        QVERIFY(suggestName({}, Loader::Forge).isEmpty());

        QVERIFY(validateName("My world").isEmpty());
        QVERIFY(!validateName("").isEmpty());
        QVERIFY(!validateName("   ").isEmpty());
        QVERIFY(validateName(QString(kMaxNameLength, 'a')).isEmpty());
        QVERIFY(validateName("  " + QString(kMaxNameLength, 'a') + "  ").isEmpty());  // trimmed first
        QVERIFY(!validateName(QString(kMaxNameLength + 1, 'a')).isEmpty());
    }

    void localImportFiles()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto write = [&](const QString& name, const QByteArray& bytes) {
            QFile f(dir.filePath(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(bytes);
        };
        write("Pack One.zip", QByteArray("PK\x03\x04", 4) + QByteArray(64, '\0'));
        write("Fabulous.mrpack", QByteArray("PK\x03\x04", 4) + QByteArray(64, '\0'));
        write("notes.txt", "hello");

        QCOMPARE(resolveLocalFile("").kind, ImportSource::Kind::None);
        auto s = resolveLocalFile(dir.filePath("Pack One.zip"));
        QCOMPARE(s.kind, ImportSource::Kind::LocalFile);
        QVERIFY(s.isImportable());
        QCOMPARE(s.url, QUrl::fromLocalFile(dir.filePath("Pack One.zip")));
        QCOMPARE(s.suggestedName, QString("Pack One"));
        QCOMPARE(s.fileName, QString("Pack One.zip"));
        QVERIFY(detectedFormat(s.fileName).isEmpty());

        s = resolveLocalFile(dir.filePath("Fabulous.mrpack"));
        QCOMPARE(s.kind, ImportSource::Kind::LocalFile);
        QCOMPARE(s.suggestedName, QString("Fabulous"));
        QCOMPARE(detectedFormat(s.fileName), QString("Modrinth pack"));

        s = resolveLocalFile(dir.filePath("notes.txt"));
        QCOMPARE(s.kind, ImportSource::Kind::Invalid);
        QVERIFY(!s.error.isEmpty());
        QVERIFY(!s.isImportable());

        s = resolveLocalFile(dir.filePath("missing.zip"));
        QCOMPARE(s.kind, ImportSource::Kind::Invalid);
        s = resolveLocalFile(dir.path());  // a folder is not a pack
        QCOMPARE(s.kind, ImportSource::Kind::Invalid);
    }

    void importUrls()
    {
        QCOMPARE(resolveUrl("  ").kind, ImportSource::Kind::None);
        auto s = resolveUrl("https://example.org/packs/Cool%20Pack.zip");
        QCOMPARE(s.kind, ImportSource::Kind::RemoteUrl);
        QCOMPARE(s.url, QUrl::fromUserInput("https://example.org/packs/Cool%20Pack.zip"));
        QCOMPARE(s.suggestedName, QString("Cool Pack"));

        s = resolveUrl("https://www.curseforge.com/minecraft/modpacks/x/download/123?client=y");
        QCOMPARE(s.kind, ImportSource::Kind::RemoteUrl);
        QCOMPARE(s.url.toString(), QString("https://www.curseforge.com/minecraft/modpacks/x/download/123/file"));

        s = resolveUrl("curseforge://install?addonId=1&fileId=2");
        QCOMPARE(s.kind, ImportSource::Kind::CurseForgeLink);
        QVERIFY(!s.isImportable());

        QCOMPARE(resolveUrl("ftp://example.org/a.zip").kind, ImportSource::Kind::Invalid);
        QCOMPARE(resolveUrl("example.org/a.zip").kind, ImportSource::Kind::Invalid);
        QCOMPARE(resolveUrl("C:/packs/a.zip").kind, ImportSource::Kind::Invalid);
        QCOMPARE(resolveUrl("https://").kind, ImportSource::Kind::Invalid);
    }
};

QTEST_GUILESS_MAIN(CreateInstanceModelTest)

#include "CreateInstanceModel_test.moc"
